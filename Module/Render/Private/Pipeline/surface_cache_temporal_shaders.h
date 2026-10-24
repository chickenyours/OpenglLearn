#pragma once
#include <string>
#include "surface_cache_motion_recovery_shaders.h"
#include "surface_cache_disocclusion_shaders.h"

namespace Render::SurfaceCacheDetail {
// Static receiver ownership is already rasterized in the current position
// image. Reuse it only for CPU-certified rigid/static instances; moving owners
// remain zero until their original previous-position raster draw overlays them.
inline std::string StaticReceiverMotionShader() {return R"GLSL(#version 450 core
layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D smCurrentPositions;
layout(std140,binding=1) uniform StaticReceiverMotionData {mat4 smInverseView;uvec4 smStaticOwners;};
void main() {
    vec4 p=texelFetch(smCurrentPositions,ivec2(gl_FragCoord.xy),0);outColor=vec4(0);
    if(!(p.w>=1.0&&p.w<=float(smStaticOwners.z))||floor(p.w)!=p.w)return;
    uint owner=uint(p.w)-1u;
    if((smStaticOwners[owner/32u]&(1u<<(owner%32u)))==0u)return;
    vec3 world=(smInverseView*vec4(p.xyz,1)).xyz;
    if(any(isnan(world))||any(isinf(world)))return;
    outColor=vec4(world,p.w);
}
)GLSL";}
// std140, 272 bytes; binding 13 is independent of SurfaceCacheData (12).
// Accumulate the reconstructed per-pixel final gather, never probe indices:
// adaptive probes may be regenerated in another order on the following frame.
inline std::string TemporalConstantsGLSL() { return R"GLSL(
layout(std140,binding=13) uniform SurfaceCacheTemporalData {
    mat4 stCurrentInverseView;
    mat4 stPreviousViewProjection;
    mat4 stPreviousView;
    ivec4 stScreen; // z: valid1/local2/epoch4/stable8/key32/contact64/recovery128/fade[8:11]/guide4096/stop8192/young16384/owner32768/residual65536/exposure131072/shadow262144/nativeContactMetadata2097152/nativeCurrentCoverage4194304/nativeForcedContactRiskUnion8388608
    vec4 stReject;
    vec4 stClamp;
    vec4 stRecovery; // strength, radius in pixels, young frame limit, albedo threshold
    vec4 stDynamic;  // scene motion active, reactive/one-frame stop cap, clamp sigma, coherent coverage confidence
};
bool STFinite(vec3 value) { return !any(isnan(value))&&!any(isinf(value)); }
// Forced raster contacts retain the true screen soft mask as one input.
// A current-only measurement, not the mixed GI base, certifies risk union.
// Unknown budgets and failed sources cannot promote the contact gate.
float STNativeContactMaskUnion(float soft,vec4 metadata,vec4 current) {
    if(isnan(soft)||isinf(soft))return soft;
    bool physical=metadata.x>0.0&&metadata.w>0.0;
    bool reconstructed=metadata.x==.25&&metadata.w==0.0;
    if((!physical&&!reconstructed)||!STFinite(metadata.xyz)||isnan(metadata.w)||isinf(metadata.w)||
        metadata.y<0.0||metadata.y>1.0||!(current.a>0.0)||isnan(current.a)||isinf(current.a)||!STFinite(current.rgb))
        return soft;
    return max(soft,metadata.y);
}
// A quantized albedo signature (not an object/material ID) travels in existing
// position alpha. RGB position and normal/depth remain exact, without a target.
float STMaterialKey(vec3 albedo,float readability) {
    ivec3 q=ivec3(floor(clamp(albedo,vec3(0),vec3(1))*31.0+.5));
    float coverage=floor(clamp(readability,0.0,1.0)*31.0+.5);
    return 4.0+32.0*float(q.x+32*q.y+1024*q.z)+coverage;
}
// Owner palette (1..64) plus 4-bit RGB and 3-bit coverage, all exactly
// representable below 2^22 in RGBA32F. The old signature stays unchanged.
float STObjectMaterialKey(vec3 albedo,float readability,float owner) {
    ivec3 q=ivec3(floor(clamp(albedo,vec3(0),vec3(1))*15.0+.5));
    float coverage=floor(clamp(readability,0.0,1.0)*7.0+.5);
    return 2097152.0+32768.0*(owner-1.0)+8.0*float(q.x+16*q.y+256*q.z)+coverage;
}
// The two remaining exact integer bits carry exposure lifetime independently
// of history count (which can be permanently capped at two on an edge).
float STBaseMaterialKey(float key) {return key>=4194304.0?mod(key,4194304.0):key;}
float STExposureAge(float key) {return clamp(floor(key/4194304.0),0.0,3.0);}
float STObjectOwner(float key) {key=STBaseMaterialKey(key);return key>=2097152.0&&key<4194304.0?floor((key-2097152.0)/32768.0)+1.0:0.0;}
bool STGeometryValid(vec4 position,vec4 normal) {
    return position.w>.5&&position.z<0.0&&normal.w>.5&&
        STFinite(position.xyz)&&STFinite(normal.xyz)&&dot(normal.xyz,normal.xyz)>.25;
}
)GLSL"; }

inline std::string TemporalShader(bool objectMotion=false,bool disocclusion=false,bool shadowResponse=false) {
    return std::string("#version 450 core\n#define ST_OBJECT_MOTION ")+(objectMotion?"1\n":"0\n")+
        "#define ST_DISOCCLUSION "+(disocclusion?"1\n":"0\n")+"#define ST_SHADOW_RESPONSE "+(shadowResponse?"1\n":"0\n")+TemporalConstantsGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D stRawGi;
layout(binding=1) uniform sampler2D stViewPositions;
layout(binding=2) uniform sampler2D stWorldNormals;
layout(binding=3) uniform sampler2D stHistory;
layout(binding=4) uniform sampler2D stHistoryPositions;
layout(binding=5) uniform sampler2D stHistoryNormals;
layout(binding=6) uniform sampler2D stCurrentAlbedo;
#if ST_SHADOW_RESPONSE
layout(std140,binding=14) uniform ShadowMotionData {vec4 stShadowBoundsMin,stShadowBoundsMax;};
float STShadowInfluence(vec3 world) {
    if(stShadowBoundsMax.w<.5)return 0.0;
    vec3 outside=max(max(stShadowBoundsMin.xyz-world,world-stShadowBoundsMax.xyz),vec3(0));
    float radius=max(stShadowBoundsMin.w,.00001);
    return 1.0-smoothstep(.5*radius,radius,length(outside));
}
#else
float STShadowInfluence(vec3 world){return 0.0;}
#endif
#if ST_OBJECT_MOTION
layout(binding=9) uniform sampler2D stObjectPreviousPositions;
layout(std140,binding=1) uniform SurfaceCacheObjectMotionData {mat4 stObjectPreviousNormals[64];};
#endif
float STCurrentMaterialKey(ivec2 pixel,vec3 albedo,float readability) {
#if ST_OBJECT_MOTION
    float owner=texelFetch(stObjectPreviousPositions,pixel,0).w;
    if((stScreen.z&32768)!=0&&owner>=1.0&&owner<=64.0)return STObjectMaterialKey(albedo,readability,owner);
#endif
    return STMaterialKey(albedo,readability);
}
)GLSL"+MotionRecoveryGLSL()+(disocclusion?DisocclusionRecoveryGLSL():std::string{})+R"GLSL(

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

vec3 STRecoverCurrent(ivec2 pixel,vec3 world,vec3 normal,float depth,float footprint,
    vec3 current,float readability,float count) {
#if ST_DISOCCLUSION
    vec3 recovered=(stScreen.z&16384)!=0?
        STRestoreDisoccludedCurrent(pixel,world,normal,depth,footprint,current,readability,count):
        STRecoverYoungCurrent(pixel,world,normal,depth,footprint,current,readability,count);
    if((stScreen.z&65536)!=0&&stDynamic.x<.5&&count<2.0)
        recovered=STRecoverMotionCurrent(pixel,world,normal,depth,footprint,recovered,readability,true);
    else if((stScreen.z&128)!=0&&(stScreen.z&16384)==0&&count<2.0)
#else
    vec3 recovered=STRecoverYoungCurrent(pixel,world,normal,depth,footprint,current,readability,count);
    if((stScreen.z&128)!=0&&count<2.0)
#endif
        recovered=STRecoverMotionCurrent(pixel,world,normal,depth,footprint,recovered,readability,false);
    return recovered;
}

// A compatible current-frame neighborhood can bound moving history. It only
// clips history; the current sample is never replaced by a spatial average.
// The frame-count cap uses its configurable motion threshold independently.
// Our view-dependent probe reconstruction also needs this optional bound at
// slow camera motion; UE instead gates its optional clamp by moving lighting.
// Static reconstructed lighting is spatially correlated, so it remains free
// to converge across independent ray phases without an unconditional clamp.
vec3 STClampMovingHistoryResponse(ivec2 pixel,vec3 world,vec3 normal,float tolerance,float sigma,vec3 current,vec3 history,
    bool evaluateConfidence,bool lightingResponse,out float confidence,out float lightingReactivity) {
    confidence=0.0;lightingReactivity=0.0;
    bool gatherStrict=evaluateConfidence||lightingResponse;
    vec3 sum=vec3(0),squareSum=vec3(0);float count=0.0;
    vec3 strictSum=vec3(0),strictSquareSum=vec3(0);float strictCount=0.0;
    vec3 albedo=gatherStrict?texelFetch(stCurrentAlbedo,pixel,0).rgb:vec3(0);
    const vec3 responseLuma=vec3(.2126,.7152,.0722);
    float historyLuma=dot(history,responseLuma),strictLumaSquare=0.0,brighter=0.0,darker=0.0;
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
        if(gatherStrict&&raw.a>=.999&&dot(normal,neighborNormal)>.984807753&&
            max(abs(dot(delta,normal)),abs(dot(delta,neighborNormal)))<=min(tolerance,stClamp.z)) {
            vec3 neighborAlbedo=texelFetch(stCurrentAlbedo,at,0).rgb,materialDelta=abs(neighborAlbedo-albedo);
            if(STFinite(albedo)&&STFinite(neighborAlbedo)&&max(materialDelta.x,max(materialDelta.y,materialDelta.z))<=.02) {
                strictSum+=value;strictSquareSum+=value*value;strictCount+=1.0;
                if(lightingResponse) {
                    float light=dot(value,responseLuma),epsilon=max(.02*abs(light),.000001);
                    strictLumaSquare+=light*light;
                    brighter+=light>historyLuma+epsilon?1.0:0.0;
                    darker+=light<historyLuma-epsilon?1.0:0.0;
                }
            }
        }
    }
    if(count<2.0)return history;
    vec3 mean=sum/count;
    vec3 deviation=sqrt(max(squareSum/count-mean*mean,vec3(0)));
    if(gatherStrict&&strictCount>=5.0&&STFinite(strictSum)&&STFinite(strictSquareSum)) {
        vec3 localMean=strictSum/strictCount;
        vec3 localVariance=max(strictSquareSum/strictCount-localMean*localMean,vec3(0));
        // The history must agree with the current *unaccumulated* same-surface
        // neighborhood. Geometry alone cannot certify a view-dependent probe
        // estimator. This symmetric RGB test is only a conservative heuristic;
        // it never selects transport samples or changes center readability.
        vec3 scale=max(sqrt(localVariance),max(.01*abs(localMean),vec3(.000001)));
        vec3 normalizedResidual=abs(history-localMean)/scale;
        if(STFinite(localMean)&&STFinite(localVariance)&&STFinite(normalizedResidual)) {
            float error=max(normalizedResidual.x,max(normalizedResidual.y,normalizedResidual.z));
            if(evaluateConfidence)confidence=1.0-smoothstep(.5,1.5,error);
            if(lightingResponse) {
                // A static receiver can keep valid geometry while its old
                // shadow is obsolete. React only to a coherent same-material
                // current neighborhood, not to a single noisy ray estimate.
                // This only shortens temporal reuse; it never chooses rays or
                // replaces current radiance with a spatial average.
                float meanLuma=dot(localMean,responseLuma);
                float variance=max(strictLumaSquare/strictCount-meanLuma*meanLuma,0.0);
                float scale=max(sqrt(variance),max(.04*abs(meanLuma),.000001));
                float residual=abs(historyLuma-meanLuma)/scale;
                float agreement=max(brighter,darker)/strictCount;
                lightingReactivity=smoothstep(1.5,3.0,residual)*smoothstep(.6,.9,agreement);
            }
        }
    }
    vec3 extent=max(sigma,0.0)*deviation;
    return clamp(history,min(mean-extent,current),max(mean+extent,current));
}
vec3 STClampMovingHistory(ivec2 pixel,vec3 world,vec3 normal,float tolerance,float sigma,vec3 current,vec3 history,
    bool evaluateConfidence,out float confidence) {
    float unused;
    return STClampMovingHistoryResponse(pixel,world,normal,tolerance,sigma,current,history,evaluateConfidence,false,confidence,unused);
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
    // Cached transport already has its own sampling history. Only a genuine
    // current receiver measurement accumulates another screen-history frame.
    if((stScreen.z&1048576)!=0&&texelFetch(stRecoveryContactMask,pixel,0).x<=0.0)return;
    vec3 normal=normalize(n.xyz);
    if((stScreen.z&1)==0||stScreen.w<=1) {
        outColor.rgb=STRecoverCurrent(pixel,world,normal,-view.z,footprint,current,raw.a,1.0);return;
    }
    vec3 expectedWorld=world,expectedNormal=normal;float receiverOwner=0.0;bool receiverMoved=false;
#if ST_OBJECT_MOTION
    if((stScreen.z&32768)!=0&&stDynamic.x>.5) {
        vec4 receiver=texelFetch(stObjectPreviousPositions,pixel,0);
        if(receiver.w>=1.0&&receiver.w<=64.0&&STFinite(receiver.xyz)) {
            int owner=int(receiver.w+.5)-1;
            if(stObjectPreviousNormals[owner][3].w<=1.5) {
                // Preserve the original world reconstruction exactly on static
                // receivers; identity raster/inverse-view round trips can differ.
                receiverOwner=receiver.w;
            } else {
                vec3 transformed=mat3(stObjectPreviousNormals[owner])*normal;
                if(STFinite(transformed)&&dot(transformed,transformed)>1e-12) {
                    expectedWorld=receiver.xyz;expectedNormal=normalize(transformed);receiverOwner=receiver.w;receiverMoved=true;
                }
            }
        }
    }
#endif
    vec4 clip=stPreviousViewProjection*vec4(expectedWorld,1);
    float expectedDepth=-(stPreviousView*vec4(expectedWorld,1)).z;
    if(!STFinite(clip.xyz)||!(clip.w>0.0)||!(expectedDepth>0.0)) {
        outColor.rgb=STRecoverCurrent(pixel,world,normal,-view.z,footprint,current,raw.a,1.0);return;
    }
    vec3 ndc=clip.xyz/clip.w;
    if(any(lessThan(ndc,vec3(-1)))||any(greaterThan(ndc,vec3(1)))) {
        outColor.rgb=STRecoverCurrent(pixel,world,normal,-view.z,footprint,current,raw.a,1.0);return;
    }
    vec2 historyPixel=(ndc.xy*.5+.5)*vec2(stScreen.xy)-.5;
    ivec2 origin=ivec2(floor(historyPixel));vec2 fraction=fract(historyPixel);
    float baseTolerance=max(stReject.x*expectedDepth,stClamp.z);
    float distanceTolerance=baseTolerance+min(1.5*footprint,baseTolerance);
    if(isnan(distanceTolerance)||isinf(distanceTolerance))distanceTolerance=baseTolerance;
    bool localCameraAdmission=(stScreen.z&2)!=0;
    float extendedLimit=min(64.0,2.0*float(stScreen.w));
    float strictPlaneTolerance=max(stClamp.z,min(baseTolerance,.25*footprint));
    vec3 sum=vec3(0);float countSum=0.0,weightSum=0.0,extendedCountSum=0.0,strictWeightSum=0.0,reuseExposureAge=0.0;
    float rigidSupport=0.0;vec3 rigidSum=vec3(0);
    float rigidKey=receiverOwner>0.0?STCurrentMaterialKey(pixel,texelFetch(stCurrentAlbedo,pixel,0).rgb,raw.a):0.0;
    bool stabilizeCamera=(stScreen.z&8)!=0&&raw.a>=.5;
    vec3 material=stabilizeCamera?texelFetch(stCurrentAlbedo,pixel,0).rgb:vec3(0);
    float materialKey=STCurrentMaterialKey(pixel,material,raw.a);
    float stabilityLimit=min(64.0,max(32.0,float(stScreen.w)));
    vec3 stableSum=vec3(0);float stableWeight=0.0,stableCountSum=0.0;
    for(int corner=0;corner<4;++corner) {
        ivec2 offset=ivec2(corner%2,corner/2),at=origin+offset;
        if(any(lessThan(at,ivec2(0)))||any(greaterThanEqual(at,stScreen.xy)))continue;
        float weight=(offset.x==0?1.0-fraction.x:fraction.x)*(offset.y==0?1.0-fraction.y:fraction.y);
        if(weight<=0.0)continue;
        vec4 value=texelFetch(stHistory,at,0),oldPosition=texelFetch(stHistoryPositions,at,0),oldNormal=texelFetch(stHistoryNormals,at,0);
        if(!(value.a>=1.0)||isnan(value.a)||isinf(value.a)||!STFinite(value.rgb)||
            oldPosition.w<.5||!STFinite(oldPosition.xyz)||!STFinite(oldNormal.xyz)||
            !(oldNormal.w>0.0)||dot(oldNormal.xyz,oldNormal.xyz)<.25)continue;
        if(receiverOwner>0.0&&STObjectOwner(oldPosition.w)!=receiverOwner)continue;
        vec3 historyNormal=normalize(oldNormal.xyz),delta=oldPosition.xyz-expectedWorld;
        if(dot(expectedNormal,historyNormal)<stReject.y||length(delta)>distanceTolerance||
            abs(oldNormal.w-expectedDepth)>distanceTolerance||
            abs(dot(delta,expectedNormal))>baseTolerance||abs(dot(delta,historyNormal))>baseTolerance)continue;
        reuseExposureAge=max(reuseExposureAge,STExposureAge(oldPosition.w));
        sum+=max(value.rgb,vec3(0))*weight;
        countSum+=min(value.a,float(stScreen.w))*weight;weightSum+=weight;
        if(receiverOwner>0.0&&STBaseMaterialKey(oldPosition.w)==rigidKey&&dot(expectedNormal,historyNormal)>.984807753&&
            length(delta)<=min(distanceTolerance,max(2.0*footprint,stClamp.z))&&
            max(abs(dot(delta,expectedNormal)),abs(dot(delta,historyNormal)))<=max(stClamp.z,.0005*expectedDepth)) {
            rigidSupport+=weight;rigidSum+=max(value.rgb,vec3(0))*weight;
        }
        if(stabilizeCamera&&STFinite(material)&&STBaseMaterialKey(oldPosition.w)==materialKey&&dot(normal,historyNormal)>.984807753&&
            length(delta)<=min(distanceTolerance,max(2.0*footprint,stClamp.z))&&
            max(abs(dot(delta,normal)),abs(dot(delta,historyNormal)))<=max(stClamp.z,.0005*expectedDepth)) {
            stableSum+=max(value.rgb,vec3(0))*weight;stableWeight+=weight;
            stableCountSum+=min(value.a,stabilityLimit)*weight;
        }
        if(localCameraAdmission&&STBaseMaterialKey(oldPosition.w)==2.0&&dot(normal,historyNormal)>.984807753&&
            max(abs(dot(delta,normal)),abs(dot(delta,historyNormal)))<=strictPlaneTolerance) {
            extendedCountSum+=min(value.a,extendedLimit)*weight;strictWeightSum+=weight;
        }
    }
    if(weightSum<.0001) {
        outColor.rgb=STRecoverCurrent(pixel,world,normal,-view.z,footprint,current,raw.a,1.0);return;
    }
    vec3 history=sum/weightSum;
    bool stableCameraReceiver=stabilizeCamera&&stableWeight>=.5&&stableWeight>=.9*weightSum;
    if(stableCameraReceiver)history=stableSum/stableWeight;
    float count=min(countSum/weightSum+1.0,float(stScreen.w));
    float motion=length(historyPixel-vec2(pixel));
    bool fastUpdate=motion>stReject.w;
    if(fastUpdate)count=min(count,max(stReject.z,1.0));
    bool dynamicUpdate=stDynamic.x>.5;
    // Strong rigid support certifies the material point, not its illumination.
    // Keep its original stable history unless a coherent current neighborhood
    // certifies changed lighting; a moving receiver can leave a static shadow.
    // Weak/partial support retains the existing dynamic clamp below.
    bool rigidReceiver=dynamicUpdate&&receiverOwner>0.0&&raw.a>=.999&&rigidSupport>=.75&&rigidSupport>=.9*weightSum&&
        receiverMoved;
    if(rigidReceiver)history=rigidSum/rigidSupport;
    if(dynamicUpdate)count=min(count,max(stDynamic.y,1.0));
    bool edgeFast=false;
    if(stClamp.w>0.0&&motion>.1&&!stableCameraReceiver) {
        float edgeTolerance=max(stClamp.z,min(baseTolerance,.002*expectedDepth));
        edgeFast=weightSum<.9||STNearGeometryEdge(pixel,world,normal,edgeTolerance);
    }
    // Partial bilinear support, unavailable/partial current transport and
    // geometry edges keep the original responsive count. Position.w=2 records
    // full readability in a previous camera-only epoch; stationary metadata
    // and every disabled-option frame remain byte-for-byte on the old path.
    bool longHistoryCandidate=localCameraAdmission&&!dynamicUpdate&&!edgeFast&&stClamp.w>0.0&&raw.a>=.999&&
        weightSum>=.999&&strictWeightSum>=.999;
    if(longHistoryCandidate&&motion<=.1)
        longHistoryCandidate=!STNearGeometryEdge(pixel,world,normal,max(stClamp.z,min(baseTolerance,.002*expectedDepth)));
    float localConfidence=0.0,responseRecoveryCount=0.0;bool confidenceEvaluated=false;
    if(stableCameraReceiver&&!dynamicUpdate) {
        // Only unchanged-scene camera reprojection can take this path. Keep
        // measured same-material support instead of clamping a clean history
        // back toward a noisy, view-dependent current probe estimate.
        count=min(stableCountSum/stableWeight+1.0,stabilityLimit*min(stableWeight,1.0));
    } else if(dynamicUpdate) {
        // Scene motion is distinct from camera motion: keep compatible static
        // receivers, but rectify stale lighting even when the camera is still.
        // Existing plane/normal/depth history rejection above remains required.
        if(edgeFast)count=min(count,max(stClamp.w,1.0));
        float sigma=min(stClamp.x,max(stDynamic.z,.5));
        if(edgeFast)sigma=min(sigma,3.0);
#if ST_SHADOW_RESPONSE
        if(rigidReceiver&&(stScreen.z&524288)!=0) {
            float lightingReactivity;
            vec3 bounded=STClampMovingHistoryResponse(pixel,world,normal,baseTolerance,sigma,current,history,
                false,true,localConfidence,lightingReactivity);
            // Decouple temporal response from young-current filtering. A
            // reduced history count must not broaden the contact shadow.
            if((stScreen.z&131072)!=0&&reuseExposureAge<.5&&weightSum>=.9&&!edgeFast)
                responseRecoveryCount=max(count,min(max(stDynamic.y,1.0),float(stScreen.w)));
            if(lightingReactivity>0.0) {
                responseRecoveryCount=max(responseRecoveryCount,count);
                history=mix(history,bounded,lightingReactivity);
                // A coherent lighting change can invalidate shading on a
                // valid material point. Retain cap4 elsewhere; strong changes
                // shorten reuse to 1.5 frames to release old shadow contours.
                count=min(count,mix(max(stDynamic.y,1.0),min(max(stDynamic.y,1.0),1.5),lightingReactivity));
            }
        }
#endif
        if(!rigidReceiver) {
            float shadowInfluence=ST_SHADOW_RESPONSE!=0?STShadowInfluence(world):0.0;
            if(shadowInfluence>0.0&&(stScreen.z&262144)!=0&&!receiverMoved&&raw.a>=.999) {
                float lightingReactivity;
                // Fresh sampling supplies an independent geometric age. A
                // mature, fully supported receiver already used the dynamic
                // cap before this response; keep that current recovery policy
                // after a prior reactive count reduction as well. Newly
                // exposed/partial/edge receivers retain ordinary young recovery.
                if((stScreen.z&131072)!=0&&reuseExposureAge<.5&&weightSum>=.9&&!edgeFast)
                    responseRecoveryCount=max(count,min(max(stDynamic.y,1.0),float(stScreen.w)));
                history=STClampMovingHistoryResponse(pixel,world,normal,baseTolerance,sigma,current,history,false,true,localConfidence,lightingReactivity);
                lightingReactivity*=shadowInfluence;
                if(lightingReactivity>0.0) {
                    // Keep the original young-current recovery input. A more
                    // responsive temporal weight must not silently increase
                    // the current signal's spatial blur or contact radius.
                    responseRecoveryCount=max(responseRecoveryCount,count);
                    count=min(count,mix(max(stDynamic.y,1.0),min(max(stDynamic.y,1.0),2.0),lightingReactivity));
                }
            } else history=STClampMovingHistory(pixel,world,normal,baseTolerance,sigma,current,history,false,localConfidence);
        }
    } else if(edgeFast) {
        // stClamp.w is the edge history cap; zero exactly disables this path.
        count=min(count,max(stClamp.w,1.0));
        history=STClampMovingHistory(pixel,world,normal,baseTolerance,min(stClamp.x,3.0),current,history,false,localConfidence);
    } else if(stClamp.y>.5&&(fastUpdate||(stClamp.y>1.5&&motion>.1))) {
        // Gentle bounds also rectify slowly moving reconstruction changes;
        // only the separate fastUpdate threshold shortens the frame count.
        // stClamp.y: 0 disables, 1 preserves fast-only legacy bounds, 2 also
        // bounds slow camera motion, independently of the geometry-edge cap.
        history=STClampMovingHistory(pixel,world,normal,baseTolerance,stClamp.x,current,history,longHistoryCandidate,localConfidence);
        confidenceEvaluated=longHistoryCandidate;
    }
    if(longHistoryCandidate) {
        if(!confidenceEvaluated) {
            // Very slow motion or a disabled moving clamp still needs the
            // photometric test. Its return value is deliberately not applied:
            // the user's original RGB clamp setting remains authoritative.
            STClampMovingHistory(pixel,world,normal,baseTolerance,stClamp.x,current,history,true,localConfidence);
        }
        if(localConfidence>0.0) {
            float extendedCount=min(extendedCountSum/weightSum+1.0,extendedLimit);
            count=mix(count,max(count,extendedCount),clamp(localConfidence,0.0,1.0));
        }
    }
    if((stScreen.z&8192)!=0)count=min(count,max(stDynamic.y,1.0));
    current=STRecoverCurrent(pixel,world,normal,-view.z,footprint,current,raw.a,responseRecoveryCount>0.0?responseRecoveryCount:count);
    // Count measures temporal reuse only. Young-current spatial recovery never
    // raises it; mature histories retain the previous bounded-EMA behavior.
    outColor=vec4(mix(history,current,1.0/max(count,1.0)),count);
}
)GLSL";
}

// Save geometry alongside each accumulated history image. A separate pass
// keeps this compatible with the renderer's single color-attachment targets.
// Position alpha is validity (2 certifies full readability for the old local
// experiment; >=4 carries the motion-stability albedo/readability signature).
// Normal alpha is positive view depth. Disabled position alpha remains 1.
inline std::string TemporalGeometryShader(bool normals,bool objectMotion=false,bool freshSampling=false) {
    return std::string("#version 450 core\n#define ST_OBJECT_MOTION ")+(objectMotion?"1\n":"0\n")+
        "#define ST_FRESH_SAMPLING "+(freshSampling?"1\n":"0\n")+"#define ST_STORE_NORMALS "+(normals?"1\n":"0\n")+
        TemporalConstantsGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D stRawGi;
layout(binding=1) uniform sampler2D stViewPositions;
layout(binding=2) uniform sampler2D stWorldNormals;
layout(binding=3) uniform sampler2D stCurrentAlbedo;
#if ST_FRESH_SAMPLING
layout(binding=5) uniform sampler2D stFreshSampling;
#endif
#if ST_OBJECT_MOTION
layout(binding=4) uniform sampler2D stObjectPreviousPositions;
#endif
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    vec4 p=texelFetch(stViewPositions,pixel,0),n=texelFetch(stWorldNormals,pixel,0);outColor=vec4(0);
    float readability=texelFetch(stRawGi,pixel,0).a;
    if(!(readability>0.0)||!STGeometryValid(p,n))return;
#if ST_STORE_NORMALS
    outColor=vec4(normalize(n.xyz),-p.z);
#else
    vec3 world=(stCurrentInverseView*vec4(p.xyz,1)).xyz;
    if(STFinite(world)) {
        float validity=(stScreen.z&4)!=0&&readability>=.999?2.0:1.0;
        if((stScreen.z&32)!=0) {
            vec3 albedo=texelFetch(stCurrentAlbedo,pixel,0).rgb;
            if(STFinite(albedo))validity=STMaterialKey(albedo,readability);
        }
#if ST_OBJECT_MOTION
        if((stScreen.z&32768)!=0) {
            float owner=texelFetch(stObjectPreviousPositions,pixel,0).w;
            vec3 albedo=texelFetch(stCurrentAlbedo,pixel,0).rgb;
            if(owner>=1.0&&owner<=64.0&&STFinite(albedo))validity=STObjectMaterialKey(albedo,readability,owner);
        }
#endif
#if ST_FRESH_SAMPLING
        if((stScreen.z&131072)!=0) {
            float age=clamp(floor((texelFetch(stFreshSampling,pixel,0).w-1.0)*.5+.5),0.0,3.0);
            validity+=age*4194304.0;
        }
#endif
        outColor=vec4(world,validity);
    }
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
#if !ST_HISTORY_VIEW
layout(binding=2) uniform sampler2D stViewPositions;
layout(binding=3) uniform sampler2D stWorldNormals;
layout(binding=4) uniform sampler2D stAlbedo;
layout(binding=5) uniform sampler2D stContactMask;
layout(binding=8) uniform sampler2D stNativeContactMetadata;
layout(binding=10) uniform sampler2D stNativeContactCurrent;
float STDisplayContactMaskAt(ivec2 pixel) {
    float soft=texelFetch(stContactMask,pixel,0).x;
    // No metadata/current sampler fetch occurs for the retained path or for a
    // full receiver image without an independently retained current signal.
    if((stScreen.z&8388608)==0||(stScreen.z&4194304)==0)return soft;
    return STNativeContactMaskUnion(soft,texelFetch(stNativeContactMetadata,pixel,0),
        texelFetch(stNativeContactCurrent,pixel,0));
}
bool STDisplayCompatible(ivec2 at,vec3 world,vec3 normal,vec3 albedo,float planeTolerance) {
    if(any(lessThan(at,ivec2(0)))||any(greaterThanEqual(at,stScreen.xy)))return false;
    vec4 raw=texelFetch(stRawGi,at,0),p=texelFetch(stViewPositions,at,0),n=texelFetch(stWorldNormals,at,0);
    if(!(raw.a>0.0)||!STFinite(raw.rgb)||!STGeometryValid(p,n))return false;
    vec3 neighborWorld=(stCurrentInverseView*vec4(p.xyz,1)).xyz,neighborNormal=normalize(n.xyz);
    vec3 delta=neighborWorld-world,material=texelFetch(stAlbedo,at,0).rgb;
    return STFinite(neighborWorld)&&STFinite(material)&&dot(normal,neighborNormal)>.984807753&&
        max(abs(dot(delta,normal)),abs(dot(delta,neighborNormal)))<=planeTolerance&&
        max(abs(material.x-albedo.x),max(abs(material.y-albedo.y),abs(material.z-albedo.z)))<=.025;
}
vec3 STMotionDisplay(ivec2 pixel,vec4 value,float readability,float footprint) {
    float fade=float((stScreen.z>>8)&15)/8.0;
    if(fade<=0.0||!(readability>0.0)||!(value.a>=1.0)||!STFinite(value.rgb))return value.rgb;
    vec4 p=texelFetch(stViewPositions,pixel,0),nd=texelFetch(stWorldNormals,pixel,0);
    vec3 albedo=texelFetch(stAlbedo,pixel,0).rgb;
    if(!STGeometryValid(p,nd)||!STFinite(albedo))return value.rgb;
    vec3 world=(stCurrentInverseView*vec4(p.xyz,1)).xyz,normal=normalize(nd.xyz);
    float planeTolerance=max(stClamp.z,min(.0005*(-p.z),.2*footprint));
    if(!STFinite(world)||isnan(planeTolerance)||isinf(planeTolerance))return value.rgb;
    int radius=value.a<4.0?2:1;
    vec3 current=max(value.rgb,vec3(0)),sum=current;float mass=1.0;
    float centerMask=(stScreen.z&64)!=0?STDisplayContactMaskAt(pixel):0.0;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
        if(x==0&&y==0)continue;
        ivec2 direction=ivec2(x,y),at=pixel+direction*radius;
        if(!STDisplayCompatible(at,world,normal,albedo,planeTolerance))continue;
        if(radius==2&&!STDisplayCompatible(pixel+direction,world,normal,albedo,planeTolerance))continue;
        vec4 neighbor=texelFetch(stAccumulated,at,0),raw=texelFetch(stRawGi,at,0);
        if(!(neighbor.a>=1.0)||!STFinite(neighbor.rgb))continue;
        vec3 color=max(neighbor.rgb,vec3(0));
        vec3 scale=vec3(.004)+.35*max(current,color),difference=(color-current)/scale;
        float weight=exp2(-.75*float(x*x+y*y)-dot(difference,difference));
        weight*=min(readability,raw.a)/max(readability,raw.a);
        if((stScreen.z&64)!=0)weight*=exp2(-8.0*abs(centerMask-STDisplayContactMaskAt(at)));
        sum+=color*weight;mass+=weight;
    }
    float strength=mix(.7,.35,smoothstep(2.0,32.0,value.a))*clamp(fade,0.0,1.0);
    // Display-only: never copy filtered RGB or spatial support into the actual
    // temporal history. Center readability remains authoritative, even at zero.
    return mix(current,sum/mass,strength);
}
#endif
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);vec4 value=texelFetch(stAccumulated,pixel,0);
#if ST_HISTORY_VIEW
    if(value.a<.5) {outColor=vec4(0,0,0,1);return;}
    float t=clamp(value.a/float(max(stScreen.w,1)),0.0,1.0);
    vec3 color=clamp(vec3(1.5-abs(4.0*t-3.0),1.5-abs(4.0*t-2.0),1.5-abs(4.0*t-1.0)),0.0,1.0);
    outColor=vec4(color,1);
#else
    vec4 raw=texelFetch(stRawGi,pixel,0);
    if((stScreen.z&1048576)!=0&&texelFetch(stContactMask,pixel,0).x<=0.0){outColor=vec4(raw.rgb,clamp(raw.a,0.0,1.0));return;}
    if(((stScreen.z>>8)&15)==0) {outColor=vec4(value.rgb,clamp(raw.a,0.0,1.0));return;}
    vec4 p=texelFetch(stViewPositions,pixel,0);
    vec3 world=(stCurrentInverseView*vec4(p.xyz,1)).xyz;
    float footprint=max(length(dFdx(world)),length(dFdy(world)));
    outColor=vec4(STMotionDisplay(pixel,value,raw.a,footprint),clamp(raw.a,0.0,1.0));
#endif
}
)GLSL";
}
} // namespace Render::SurfaceCacheDetail
