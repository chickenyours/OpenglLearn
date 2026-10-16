#pragma once
#include <string>

namespace Render::SurfaceCacheDetail {
// std140, 240 bytes; binding 13 is independent of SurfaceCacheData (12).
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

// A compatible current-frame neighborhood can bound moving history. It only
// clips history; the current sample is never replaced by a spatial average.
// Restrict this to fast update, as UE's optional diffuse neighborhood clamp
// does. Static reconstructed probe lighting is spatially correlated, so an
// unconditional 3x3 clamp would stop independent ray phases from converging.
vec3 STClampMovingHistory(ivec2 pixel,vec3 world,vec3 normal,float tolerance,vec3 current,vec3 history) {
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
    vec3 extent=max(stClamp.x,0.0)*deviation;
    return clamp(history,min(mean-extent,current),max(mean+extent,current));
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
    if(stScreen.z==0||stScreen.w<=1)return;
    vec4 clip=stPreviousViewProjection*vec4(world,1);
    float expectedDepth=-(stPreviousView*vec4(world,1)).z;
    if(!STFinite(clip.xyz)||!(clip.w>0.0)||!(expectedDepth>0.0))return;
    vec3 ndc=clip.xyz/clip.w;
    if(any(lessThan(ndc,vec3(-1)))||any(greaterThan(ndc,vec3(1))))return;
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
    if(weightSum<.0001)return;
    vec3 history=sum/weightSum;
    float count=min(countSum/weightSum+1.0,float(stScreen.w));
    float motion=length(historyPixel-vec2(pixel));
    if(motion>stReject.w) {
        count=min(count,max(stReject.z,1.0));
        if(stClamp.y>.5)history=STClampMovingHistory(pixel,world,normal,baseTolerance,current,history);
    }
    // With count 1 this is exactly the current frame; thereafter it is a
    // running average until the configured history cap, then a bounded EMA.
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
