#pragma once
#include <string>

namespace Render::SurfaceCacheDetail {
// std140, 272 bytes; binding 13 is independent of SurfaceCacheData (12).
// Accumulate the reconstructed per-pixel final gather, never probe indices:
// adaptive probes may be regenerated in another order on the following frame.
inline std::string TemporalConstantsGLSL() { return R"GLSL(
layout(std140,binding=13) uniform SurfaceCacheTemporalData {
    mat4 stCurrentInverseView;
    mat4 stPreviousViewProjection;
    mat4 stPreviousView;
    ivec4 stScreen;
    vec4 stReject;
    vec4 stClamp;
    vec4 stRecovery; // strength, radius in pixels, young frame limit, albedo threshold
    vec4 stDynamic;  // scene motion active, frame cap, clamp sigma, coherent coverage confidence
};
bool STFinite(vec3 value) { return !any(isnan(value))&&!any(isinf(value)); }
bool STGeometryValid(vec4 position,vec4 normal) {
    return position.w>.5&&position.z<0.0&&normal.w>.5&&
        STFinite(position.xyz)&&STFinite(normal.xyz)&&dot(normal.xyz,normal.xyz)>.25;
}
)GLSL"; }

inline std::string TemporalShader() {
    return std::string("#version 450 core\n")+TemporalConstantsGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D stRawGi;
layout(binding=1) uniform sampler2D stViewPositions;
layout(binding=2) uniform sampler2D stWorldNormals;
layout(binding=3) uniform sampler2D stHistory;
layout(binding=4) uniform sampler2D stHistoryPositions;
layout(binding=5) uniform sampler2D stHistoryNormals;
layout(binding=6) uniform sampler2D stCurrentAlbedo;

// A local adaptation of the low-confidence bilateral recovery in UE's
// LumenReSTIRGather.usf / LumenReflectionDenoiserSpatial.usf. The ordinary
// ScreenProbeGather instead increases probe-space filtering for moving traces.
// Filter only the current signal while its history is young; retain the real
// count and readability, so spatial taps never masquerade as temporal samples.
vec3 STRecoverYoungCurrent(ivec2 pixel,vec3 world,vec3 normal,float depth,float footprint,
    vec3 current,float readability,float count) {
    float youngLimit=max(stRecovery.z,1.0);
    if(stRecovery.x<=0.0||stRecovery.y<.5||youngLimit<=1.0||count>=youngLimit)return current;
    bool coherentConfidence=stDynamic.w>.5;
    vec3 albedo=texelFetch(stCurrentAlbedo,pixel,0).rgb;
    if(!STFinite(albedo))return current;
    int radius=clamp(int(floor(stRecovery.y+.5)),1,4);
    float pixelWorld=clamp(footprint,.000001,max(stClamp.z,.025*depth));
    if(isnan(pixelWorld)||isinf(pixelWorld))return current;
    // Depth is allowed to change tangentially on grazing surfaces. Symmetric
    // plane error remains tightly bounded and cannot grow with the full radius.
    float planeTolerance=max(stClamp.z,min(.001*depth,.2*pixelWorld));
    float albedoTolerance=max(stRecovery.w,0.0);
    const ivec2 directions[8]=ivec2[8](ivec2(-1,0),ivec2(1,0),ivec2(0,-1),ivec2(0,1),
        ivec2(-1,-1),ivec2(1,-1),ivec2(-1,1),ivec2(1,1));
    vec3 sum=current;float weightSum=1.0;
    for(int sampleIndex=0;sampleIndex<8;++sampleIndex) {
        ivec2 offset=directions[sampleIndex]*radius;
        bool compatible=true;vec3 endpoint=vec3(0);float endpointNormal=0.0,endpointPlane=0.0,endpointConfidence=1.0;
        // Walking every pixel prevents an outer tap jumping over an intervening
        // silhouette, material seam or unreadable strip. Radius is at most four.
        for(int step=1;step<=4;++step) {
            if(step>radius)break;
            ivec2 pathOffset=directions[sampleIndex]*step,at=pixel+pathOffset;
            if(any(lessThan(at,ivec2(0)))||any(greaterThanEqual(at,stScreen.xy))){compatible=false;break;}
            vec4 value=texelFetch(stRawGi,at,0),position=texelFetch(stViewPositions,at,0),nd=texelFetch(stWorldNormals,at,0);
            if(!(value.a>0.0)||(!coherentConfidence&&abs(value.a-readability)>.001)||!STFinite(value.rgb)||!STGeometryValid(position,nd)) {
                compatible=false;break;
            }
            vec3 neighborWorld=(stCurrentInverseView*vec4(position.xyz,1)).xyz,neighborNormal=normalize(nd.xyz);
            vec3 delta=neighborWorld-world;
            float agreement=dot(normal,neighborNormal);
            float plane=max(abs(dot(delta,normal)),abs(dot(delta,neighborNormal)));
            float worldLimit=1.5*pixelWorld*(length(vec2(pathOffset))+.75);
            vec3 neighborAlbedo=texelFetch(stCurrentAlbedo,at,0).rgb;
            vec3 albedoDifference=abs(neighborAlbedo-albedo);
            if(!STFinite(neighborWorld)||agreement<.9659258263||plane>planeTolerance||
                length(delta)>max(worldLimit,2.0*planeTolerance)||!STFinite(neighborAlbedo)||
                max(albedoDifference.x,max(albedoDifference.y,albedoDifference.z))>albedoTolerance) {
                compatible=false;break;
            }
            // Continuous coverage can change within the same surface where
            // contact and probe estimates blend. It is a confidence rather
            // than a surface identity. Unknown taps still stop the path, and
            // neither these weights nor spatial support increase center alpha
            // or the accumulated temporal sample count.
            if(coherentConfidence)endpointConfidence=min(endpointConfidence,
                min(value.a,readability)/max(value.a,readability));
            endpoint=max(value.rgb,vec3(0));endpointNormal=agreement;endpointPlane=plane;
        }
        if(!compatible)continue;
        float weight=exp2(-1.5*dot(vec2(offset),vec2(offset))/float(radius*radius))*
            pow(max(endpointNormal,0.0),32.0)*exp2(-2.0*endpointPlane*endpointPlane/(planeTolerance*planeTolerance));
        if(coherentConfidence)weight*=endpointConfidence;
        sum+=endpoint*weight;weightSum+=weight;
    }
    float young=1.0-smoothstep(1.0,youngLimit,max(count,1.0));
    return mix(current,sum/weightSum,clamp(stRecovery.x,0.0,1.0)*young);
}

// A compatible current-frame neighborhood can bound moving history. It only
// clips history; the current sample is never replaced by a spatial average.
// The frame-count cap uses its configurable motion threshold independently.
// Our view-dependent probe reconstruction also needs this optional bound at
// slow camera motion; UE instead gates its optional clamp by moving lighting.
// Static reconstructed lighting is spatially correlated, so it remains free
// to converge across independent ray phases without an unconditional clamp.
vec3 STClampMovingHistory(ivec2 pixel,vec3 world,vec3 normal,float tolerance,float sigma,vec3 current,vec3 history) {
    vec3 sum=vec3(0),squareSum=vec3(0);float count=0.0;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
        ivec2 at=pixel+ivec2(x,y);
        if(any(lessThan(at,ivec2(0)))||any(greaterThanEqual(at,stScreen.xy)))continue;
        vec4 raw=texelFetch(stRawGi,at,0),p=texelFetch(stViewPositions,at,0),n=texelFetch(stWorldNormals,at,0);
        if(!(raw.a>0.0)||!STFinite(raw.rgb)||!STGeometryValid(p,n))continue;
        vec3 neighborWorld=(stCurrentInverseView*vec4(p.xyz,1)).xyz,neighborNormal=normalize(n.xyz);
        vec3 delta=neighborWorld-world;
        if(!STFinite(neighborWorld)||dot(normal,neighborNormal)<stReject.y||
            abs(dot(delta,normal))>tolerance||abs(dot(delta,neighborNormal))>tolerance)continue;
        vec3 value=max(raw.rgb,vec3(0));sum+=value;squareSum+=value*value;count+=1.0;
    }
    if(count<2.0)return history;
    vec3 mean=sum/count;
    vec3 deviation=sqrt(max(squareSum/count-mean*mean,vec3(0)));
    vec3 extent=max(sigma,0.0)*deviation;
    return clamp(history,min(mean-extent,current),max(mean+extent,current));
}

// A local safety adaptation for this renderer's view-dependent probe
// reconstruction, rather than UE's moving-light classification. Preserve long
// histories on moving flat walls; only a narrow moving geometry-edge band
// needs the former fast response. Static edges continue accumulating normally.
bool STNearGeometryEdge(ivec2 pixel,vec3 world,vec3 normal,float planeTolerance) {
    const ivec2 offsets[24]=ivec2[24](
        ivec2(-1,0),ivec2(1,0),ivec2(0,-1),ivec2(0,1),
        ivec2(-2,0),ivec2(2,0),ivec2(0,-2),ivec2(0,2),
        ivec2(-1,-1),ivec2(1,-1),ivec2(-1,1),ivec2(1,1),
        ivec2(-2,-1),ivec2(2,-1),ivec2(-2,1),ivec2(2,1),
        ivec2(-1,-2),ivec2(1,-2),ivec2(-1,2),ivec2(1,2),
        ivec2(-2,-2),ivec2(2,-2),ivec2(-2,2),ivec2(2,2));
    float normalCosine=max(stReject.y,.9);
    for(int sampleIndex=0;sampleIndex<24;++sampleIndex) {
        ivec2 at=pixel+offsets[sampleIndex];
        if(any(lessThan(at,ivec2(0)))||any(greaterThanEqual(at,stScreen.xy)))return true;
        vec4 p=texelFetch(stViewPositions,at,0),n=texelFetch(stWorldNormals,at,0);
        if(!STGeometryValid(p,n))return true;
        vec3 neighborWorld=(stCurrentInverseView*vec4(p.xyz,1)).xyz,neighborNormal=normalize(n.xyz);
        if(!STFinite(neighborWorld)||dot(normal,neighborNormal)<normalCosine)return true;
        vec3 delta=neighborWorld-world;
        if(abs(dot(delta,normal))>planeTolerance||abs(dot(delta,neighborNormal))>planeTolerance)return true;
    }
    return false;
}

void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);outColor=vec4(0);
    vec4 raw=texelFetch(stRawGi,pixel,0),view=texelFetch(stViewPositions,pixel,0),n=texelFetch(stWorldNormals,pixel,0);
    vec3 world=(stCurrentInverseView*vec4(view.xyz,1)).xyz;
    // Evaluate derivatives before any per-pixel early return, including
    // invalid/background pixels, so the derivative quad remains well defined.
    // Bound their allowance below to keep silhouette derivatives conservative.
    float footprint=max(length(dFdx(world)),length(dFdy(world)));
    // Preserve known-black samples, but never fill an unreadable cache hole
    // with history. The display pass restores raw readability as its alpha.
    if(!(raw.a>0.0)||!STFinite(raw.rgb)||!STGeometryValid(view,n)||!STFinite(world))return;
    vec3 current=max(raw.rgb,vec3(0));outColor=vec4(current,1);
    vec3 normal=normalize(n.xyz);
    if(stScreen.z==0||stScreen.w<=1) {
        outColor.rgb=STRecoverYoungCurrent(pixel,world,normal,-view.z,footprint,current,raw.a,1.0);return;
    }
    vec4 clip=stPreviousViewProjection*vec4(world,1);
    float expectedDepth=-(stPreviousView*vec4(world,1)).z;
    if(!STFinite(clip.xyz)||!(clip.w>0.0)||!(expectedDepth>0.0)) {
        outColor.rgb=STRecoverYoungCurrent(pixel,world,normal,-view.z,footprint,current,raw.a,1.0);return;
    }
    vec3 ndc=clip.xyz/clip.w;
    if(any(lessThan(ndc,vec3(-1)))||any(greaterThan(ndc,vec3(1)))) {
        outColor.rgb=STRecoverYoungCurrent(pixel,world,normal,-view.z,footprint,current,raw.a,1.0);return;
    }
    vec2 historyPixel=(ndc.xy*.5+.5)*vec2(stScreen.xy)-.5;
    ivec2 origin=ivec2(floor(historyPixel));vec2 fraction=fract(historyPixel);
    float baseTolerance=max(stReject.x*expectedDepth,stClamp.z);
    float distanceTolerance=baseTolerance+min(1.5*footprint,baseTolerance);
    if(isnan(distanceTolerance)||isinf(distanceTolerance))distanceTolerance=baseTolerance;
    vec3 sum=vec3(0);float countSum=0.0,weightSum=0.0;
    for(int corner=0;corner<4;++corner) {
        ivec2 offset=ivec2(corner%2,corner/2),at=origin+offset;
        if(any(lessThan(at,ivec2(0)))||any(greaterThanEqual(at,stScreen.xy)))continue;
        float weight=(offset.x==0?1.0-fraction.x:fraction.x)*(offset.y==0?1.0-fraction.y:fraction.y);
        if(weight<=0.0)continue;
        vec4 value=texelFetch(stHistory,at,0),oldPosition=texelFetch(stHistoryPositions,at,0),oldNormal=texelFetch(stHistoryNormals,at,0);
        if(!(value.a>=1.0)||isnan(value.a)||isinf(value.a)||!STFinite(value.rgb)||
            oldPosition.w<.5||!STFinite(oldPosition.xyz)||!STFinite(oldNormal.xyz)||
            !(oldNormal.w>0.0)||dot(oldNormal.xyz,oldNormal.xyz)<.25)continue;
        vec3 historyNormal=normalize(oldNormal.xyz),delta=oldPosition.xyz-world;
        if(dot(normal,historyNormal)<stReject.y||length(delta)>distanceTolerance||
            abs(oldNormal.w-expectedDepth)>distanceTolerance||
            abs(dot(delta,normal))>baseTolerance||abs(dot(delta,historyNormal))>baseTolerance)continue;
        sum+=max(value.rgb,vec3(0))*weight;
        countSum+=min(value.a,float(stScreen.w))*weight;weightSum+=weight;
    }
    if(weightSum<.0001) {
        outColor.rgb=STRecoverYoungCurrent(pixel,world,normal,-view.z,footprint,current,raw.a,1.0);return;
    }
    vec3 history=sum/weightSum;
    float count=min(countSum/weightSum+1.0,float(stScreen.w));
    float motion=length(historyPixel-vec2(pixel));
    bool fastUpdate=motion>stReject.w;
    if(fastUpdate)count=min(count,max(stReject.z,1.0));
    bool dynamicUpdate=stDynamic.x>.5;
    if(dynamicUpdate)count=min(count,max(stDynamic.y,1.0));
    bool edgeFast=false;
    if(stClamp.w>0.0&&motion>.1) {
        float edgeTolerance=max(stClamp.z,min(baseTolerance,.002*expectedDepth));
        edgeFast=weightSum<.9||STNearGeometryEdge(pixel,world,normal,edgeTolerance);
    }
    if(dynamicUpdate) {
        // Scene motion is distinct from camera motion: keep compatible static
        // receivers, but rectify stale lighting even when the camera is still.
        // Existing plane/normal/depth history rejection above remains required.
        if(edgeFast)count=min(count,max(stClamp.w,1.0));
        float sigma=min(stClamp.x,max(stDynamic.z,.5));
        if(edgeFast)sigma=min(sigma,3.0);
        history=STClampMovingHistory(pixel,world,normal,baseTolerance,sigma,current,history);
    } else if(edgeFast) {
        // stClamp.w is the edge history cap; zero exactly disables this path.
        count=min(count,max(stClamp.w,1.0));
        history=STClampMovingHistory(pixel,world,normal,baseTolerance,min(stClamp.x,3.0),current,history);
    } else if(stClamp.y>.5&&(fastUpdate||(stClamp.y>1.5&&motion>.1))) {
        // Gentle bounds also rectify slowly moving reconstruction changes;
        // only the separate fastUpdate threshold shortens the frame count.
        // stClamp.y: 0 disables, 1 preserves fast-only legacy bounds, 2 also
        // bounds slow camera motion, independently of the geometry-edge cap.
        history=STClampMovingHistory(pixel,world,normal,baseTolerance,stClamp.x,current,history);
    }
    current=STRecoverYoungCurrent(pixel,world,normal,-view.z,footprint,current,raw.a,count);
    // Count measures temporal reuse only. Young-current spatial recovery never
    // raises it; mature histories retain the previous bounded-EMA behavior.
    outColor=vec4(mix(history,current,1.0/max(count,1.0)),count);
}
)GLSL";
}

// Save geometry alongside each accumulated history image. A separate pass
// keeps this compatible with the renderer's single color-attachment targets.
// Position alpha is validity; normal alpha is positive view depth.
inline std::string TemporalGeometryShader(bool normals) {
    return std::string("#version 450 core\n#define ST_STORE_NORMALS ")+(normals?"1\n":"0\n")+
        TemporalConstantsGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D stRawGi;
layout(binding=1) uniform sampler2D stViewPositions;
layout(binding=2) uniform sampler2D stWorldNormals;
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    vec4 p=texelFetch(stViewPositions,pixel,0),n=texelFetch(stWorldNormals,pixel,0);outColor=vec4(0);
    if(!(texelFetch(stRawGi,pixel,0).a>0.0)||!STGeometryValid(p,n))return;
#if ST_STORE_NORMALS
    outColor=vec4(normalize(n.xyz),-p.z);
#else
    vec3 world=(stCurrentInverseView*vec4(p.xyz,1)).xyz;
    if(STFinite(world))outColor=vec4(world,1);
#endif
}
)GLSL";
}

// Keep history's frame count separate from the public radiance/readability
// signal consumed by the existing resolve, coverage diagnostics and readback.
inline std::string TemporalDisplayShader(bool historyView=false) {
    return std::string("#version 450 core\n#define ST_HISTORY_VIEW ")+(historyView?"1\n":"0\n")+
        TemporalConstantsGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D stAccumulated;
layout(binding=1) uniform sampler2D stRawGi;
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);vec4 value=texelFetch(stAccumulated,pixel,0);
#if ST_HISTORY_VIEW
    if(value.a<.5) {outColor=vec4(0,0,0,1);return;}
    float t=clamp(value.a/float(max(stScreen.w,1)),0.0,1.0);
    vec3 color=clamp(vec3(1.5-abs(4.0*t-3.0),1.5-abs(4.0*t-2.0),1.5-abs(4.0*t-1.0)),0.0,1.0);
    outColor=vec4(color,1);
#else
    vec4 raw=texelFetch(stRawGi,pixel,0);
    outColor=vec4(value.rgb,clamp(raw.a,0.0,1.0));
#endif
}
)GLSL";
}
} // namespace Render::SurfaceCacheDetail
