#pragma once
#include <string>
#include "screen_probe_trace_shaders.h"

namespace Render::SurfaceCacheDetail {
// UE 5.7 diffuse ScreenProbe tracing: HZBTracing.ush:33-251 and
// LumenScreenProbeTracing.usf:105-168,287-348. Unlike the reflection helper,
// this uses .02 HZB thickness, .005 history depth and no cos85 normal gate.
// This renderer additionally executes the original SCNativeIntersect logical
// call for EVERY ray, retaining its precise retry (one or two native inits).
// Screen depth never replaces the exact production endpoint/triangle.
inline std::string UeLumenHitSceneColorConstantsGLSL(){return R"GLSL(
layout(std140,binding=11) uniform UeLumenHitSceneColorData {
    mat4 uhscPreviousViewProjection,uhscPreviousView;
    mat4 uhscPreviousFromCurrent[64];
    ivec4 uhscFlags; // history valid, certified owner count, frame mod8, width
    vec4 uhscCamera; // current world camera xyz, height
};
)GLSL";}

inline std::string UeLumenScreenTraceGLSL(){return UeLumenHitSceneColorConstantsGLSL()+R"GLSL(
layout(binding=2) uniform sampler2D uhscViewPositions;
layout(binding=3) uniform sampler2D uhscHistoryDepth; // deviceZ01, 0, 0, owner
layout(binding=6) uniform sampler2D uhscHistorySceneColor;
layout(binding=11) uniform sampler2D uhscClosestHzb; // positive closest depth
bool UHSCFinite(float p){return !isnan(p)&&!isinf(p);}
bool UHSCFinite(vec3 p){return !any(isnan(p))&&!any(isinf(p));}
bool UHSCFinite(vec4 p){return !any(isnan(p))&&!any(isinf(p));}
ivec2 UHSCExtent(){return ivec2(uhscFlags.w,int(uhscCamera.w));}
// The private packed resource changes only addressing: level0 is the full
// positions capture (UE BaseMip=-1); level1 is closest HZB mip0, and so on.
// Use physical full pixels / 2^level, never UV * rounded odd mip dimensions.
int UHSCMaxLevel(){
    ivec2 size=UHSCExtent();int level=0;
    for(int i=0;i<31;++i){if(all(equal(size,ivec2(1))))break;size=(size+1)/2;++level;}
    return level;
}
float UHSCClosestDepth(vec2 uv,int level){
    ivec2 extent=UHSCExtent();
    if(level<=0){
        ivec2 pixel=clamp(ivec2(floor(uv*vec2(extent))),ivec2(0),extent-1);
        vec4 p=texelFetch(uhscViewPositions,pixel,0);
        return p.w>=.5&&p.z<0.0&&UHSCFinite(p)?-p.z:1e20;
    }
    level=min(level,UHSCMaxLevel());ivec2 size=extent;int offset=0;
    for(int i=1;i<=level;++i){size=(size+1)/2;if(i<level)offset+=size.y;}
    ivec2 at=clamp(ivec2(floor(uv*vec2(extent)/exp2(float(level)))),ivec2(0),size-1);
    float depth=texelFetch(uhscClosestHzb,at+ivec2(0,offset),0).x;
    return UHSCFinite(depth)&&depth>0.0?depth:1e20;
}
// Perform UE's comparisons in reversed device Z. This keeps its above-surface,
// descending-Z plane and closest-HZB rules together despite the GL projection.
float UHSCReversedDeviceZ(float depth){
    if(depth>=1e19)return 0.0;
    mat4 projection=spViewProjection*spInverseView;
    vec4 clip=projection*vec4(0,0,-depth,1);
    return .5-.5*(clip.z/clip.w);
}
float UHSCViewDepth(float reversedZ){
    vec4 p=spInverseProjection*vec4(0,0,1.0-2.0*reversedZ,1);
    return -p.z/p.w;
}
vec3 UHSCWorldPosition(vec3 screenUvZ){
    vec4 p=spInverseProjection*vec4(screenUvZ.xy*2.0-1.0,1.0-2.0*screenUvZ.z,1);
    p/=p.w;return (spInverseView*vec4(p.xyz,1)).xyz;
}
// Exact diffuse defaults: FullResDepth=1, MaxIterations=50, RelativeThickness
// .02. SkipFoliageHits=1 makes effective certainty steps ZERO (Tracing.cpp714).
// Our opaque native cache has no foliage/hair shading-model representation.
bool UHSCTraceHZB(vec3 origin,vec3 direction,float maxDistance,out vec3 hitUvZ,out ivec2 firstPixel){
    hitUvZ=vec3(0);firstPixel=ivec2(-1);
    vec4 startClip=spViewProjection*vec4(origin,1);
    if(!UHSCFinite(startClip)||!(startClip.w>0.0)||
       any(lessThan(startClip.xy,vec2(-startClip.w)))||
       any(greaterThan(startClip.xy,vec2(startClip.w))))return false;
    // HZBTracing.ush55's max(w,1cm), converted once to renderer metres.
    vec3 startNdc=startClip.xyz/max(startClip.w,.01);
    vec3 start=vec3(startNdc.xy*.5+.5,.5-.5*startNdc.z);
    firstPixel=clamp(ivec2(floor(start.xy*vec2(UHSCExtent()))),ivec2(0),UHSCExtent()-1);
    vec3 viewOrigin=(spView*vec4(origin,1)).xyz;
    vec3 viewDirection=mat3(spView)*direction;
    // UE positive viewZ <-> this renderer negative viewZ.
    float endDistance=viewDirection.z>0.0?min(-.99*viewOrigin.z/viewDirection.z,maxDistance):maxDistance;
    if(!(endDistance>0.0)||!UHSCFinite(endDistance))return false;
    vec4 endClip=spViewProjection*vec4(origin+direction*endDistance,1);
    if(!UHSCFinite(endClip)||!(endClip.w>0.0))return false;
    vec3 endNdc=endClip.xyz/endClip.w;
    vec3 end=vec3(endNdc.xy*.5+.5,.5-.5*endNdc.z);
    vec3 delta=end-start;float leave=1.0;
    // HZBTracing.ush71-74 LineBoxIntersect against screen/depth [0,1].
    for(int axis=0;axis<3;++axis){
        if(delta[axis]>0.0)leave=min(leave,(1.0-start[axis])/delta[axis]);
        else if(delta[axis]<0.0)leave=min(leave,-start[axis]/delta[axis]);
        else if(start[axis]<0.0||start[axis]>1.0)return false;
    }
    if(!(leave>0.0)||!UHSCFinite(leave))return false;
    delta*=clamp(leave,0.0,1.0);
    vec2 floorOffset=vec2(delta.x<0.0?0.0:1.0,delta.y<0.0?0.0:1.0);
    vec2 baseTexel=1.0/vec2(UHSCExtent());
    vec2 offset=vec2(delta.x<0.0?-.005:.005,delta.y<0.0?-.005:.005)*baseTexel;
    vec2 plane=(floor(start.xy/baseTexel)+floorOffset)*baseTexel+offset;
    vec2 stepTimes=(plane-start.xy)/delta.xy;
    float time=min(stepTimes.x,stepTimes.y),lastAbove=0.0;
    // UE first steps out of its full-resolution source tile without testing.
    // No candidate exists when that step already reaches the clipped end.
    if(!(time<1.0)||!UHSCFinite(time))return false;
    vec3 sampleUvZ=start+time*delta;int level=0;
    for(int iteration=0;iteration<50&&level>=0&&time<1.0;++iteration){
        vec2 texel=exp2(float(level))*baseTexel;
        vec2 uvOffset=vec2(delta.x<0.0?-.005:.005,delta.y<0.0?-.005:.005)*texel;
        vec2 xyPlane=(floor(sampleUvZ.xy/texel)+floorOffset)*texel+uvOffset;
        float tileZ=UHSCReversedDeviceZ(UHSCClosestDepth(sampleUvZ.xy,level));
        vec3 intersection=(vec3(xyPlane,tileZ)-start)/delta;
        intersection.z=delta.z<0.0?intersection.z:1.0;
        float nextTime=min(min(intersection.x,intersection.y),intersection.z);
        bool above=sampleUvZ.z>tileZ;
        bool skipped=above&&nextTime!=intersection.z;
        if(skipped)lastAbove=nextTime;
        time=above?nextTime:time;
        sampleUvZ=start+min(time,1.0)*delta;
        level+=skipped?1:-1;
    }
    if(level>=0||!(time<1.0)||!UHSCFinite(sampleUvZ))return false;
    float tileZ=UHSCReversedDeviceZ(UHSCClosestDepth(sampleUvZ.xy,0));
    float hitDepth=UHSCViewDepth(tileZ),rayDepth=UHSCViewDepth(sampleUvZ.z);
    // The original one-sided relative-depth acceptance, not reflection .01.
    bool hit=(rayDepth-hitDepth)<.02*max(hitDepth,1e-7);
    // effective certainty steps=0 => bUncertain=false; all uncertain/no-hit
    // outcomes stay on the original native/Surface Cache fallback path.
    if(!hit||!UHSCFinite(hitDepth)||!UHSCFinite(rayDepth))return false;
    hitUvZ=sampleUvZ;return true;
}
float UHSCVignette(vec2 screenPosition){
    vec2 v=clamp(abs(screenPosition)*5.0-4.0,0.0,1.0);
    float value=1.0-dot(v,v);return UHSCFinite(value)?clamp(value,0.0,1.0):0.0;
}
float UHSCInterleavedGradientNoise(vec2 pixel){
    pixel+=float(uhscFlags.z&7)*(vec2(47.0,17.0)*.695);
    return fract(52.9829189*fract(dot(pixel,vec2(.06711056,.00583715))));
}
struct UeScreenTraceCandidate {
    vec3 radiance;float sceneDepth;float logicalDistance;int owner;ivec2 sourcePixel,startPixel,hitPixel;
};
bool UHSCTraceScreenCandidate(int probe,int ray,vec3 sourceWorld,vec3 sourceNormal,
    vec3 direction,float traceDistance,out UeScreenTraceCandidate candidate){
    candidate.radiance=vec3(0);candidate.sceneDepth=0.0;candidate.logicalDistance=0.0;candidate.owner=-1;
    candidate.sourcePixel=ivec2(-1);candidate.startPixel=ivec2(-1);candidate.hitPixel=ivec2(-1);
    if(uhscFlags.x==0||uhscFlags.y<=0||uhscFlags.y>64||
       uhscFlags.w<=0||!(uhscCamera.w>0.0))return false;
    ivec2 extent=UHSCExtent();
    vec4 sourceClip=spViewProjection*vec4(sourceWorld,1);
    if(!UHSCFinite(sourceClip)||!(sourceClip.w>0.0))return false;
    vec3 sourceNdc=sourceClip.xyz/sourceClip.w;
    if(any(greaterThanEqual(abs(sourceNdc.xy),vec2(1))))return false;
    ivec2 sourcePixel=ivec2(floor((sourceNdc.xy*.5+.5)*vec2(extent)));
    vec4 source=texelFetch(uhscViewPositions,sourcePixel,0);
    if(!UHSCFinite(source)||!(source.z<0.0)||source.w<1.0||source.w>float(uhscFlags.y))return false;
    // ScreenProbeTracing.usf110-118: normal bias from the half-pixel corner.
    // Opaque source normals are NOT flipped for the opposite ray hemisphere.
    vec4 cornerView=spInverseProjection*vec4(sourceNdc.xy+1.0/vec2(extent),sourceNdc.z,1);
    if(!UHSCFinite(cornerView)||cornerView.w==0.0)return false;
    cornerView/=cornerView.w;
    vec3 cornerWorld=(spInverseView*vec4(cornerView.xyz,1)).xyz;
    float normalBias=2.0*abs(dot(cornerWorld-sourceWorld,sourceNormal));
    vec3 screenOrigin=sourceWorld+normalBias*sourceNormal,hitUvZ;ivec2 firstPixel;
    if(!UHSCTraceHZB(screenOrigin,direction,traceDistance,hitUvZ,firstPixel))return false;
    if(any(lessThan(hitUvZ.xy,vec2(0)))||any(greaterThanEqual(hitUvZ.xy,vec2(1))))return false;
    ivec2 hitPixel=ivec2(floor(hitUvZ.xy*vec2(extent)));
    vec4 scene=texelFetch(uhscViewPositions,hitPixel,0);
    if(!UHSCFinite(scene)||!(scene.z<0.0)||scene.w<1.0||scene.w>float(uhscFlags.y))return false;
    int owner=int(scene.w)-1;if(scene.w!=float(owner+1)||owner>=64)return false;
    vec3 screenHitWorld=UHSCWorldPosition(hitUvZ);
    vec4 previousWorld=uhscPreviousFromCurrent[owner]*vec4(screenHitWorld,1);
    if(!UHSCFinite(previousWorld)||previousWorld.w==0.0)return false;
    previousWorld/=previousWorld.w;
    vec4 previousClip=uhscPreviousViewProjection*previousWorld;
    vec4 previousView=uhscPreviousView*previousWorld;
    if(!UHSCFinite(previousClip)||!UHSCFinite(previousView)||
       !(previousClip.w>0.0)||!(previousView.z<0.0))return false;
    vec3 previousNdc=previousClip.xyz/previousClip.w;
    if(!UHSCFinite(previousNdc))return false;
    // TracingCommon.ush106-113 and Tracing.usf305: UE's real 8x8 trace atlas,
    // not the compact four-tuple atlas and not the reflection source pixel.
    ivec2 traceCoord=ivec2(probe%spGrid.x,probe/spGrid.x)*spTracing.x+
        ivec2(ray%spTracing.x,ray/spTracing.x);
    float noise=UHSCInterleavedGradientNoise(vec2(traceCoord)+.5);
    float vignette=min(UHSCVignette(hitUvZ.xy*2.0-1.0),UHSCVignette(previousNdc.xy));
    if(vignette<noise)return false;
    vec2 previousUv=previousNdc.xy*.5+.5;
    if(any(lessThan(previousUv,vec2(0)))||any(greaterThanEqual(previousUv,vec2(1))))return false;
    ivec2 previousPixel=ivec2(floor(previousUv*vec2(extent)));
    vec4 historyDepth=texelFetch(uhscHistoryDepth,previousPixel,0);
    if(!UHSCFinite(historyDepth)||historyDepth.w!=float(owner+1))return false;
    float previousDeviceZ=previousNdc.z*.5+.5;
    if(!(abs(historyDepth.x-previousDeviceZ)<.005*mix(.5,2.0,noise)))return false;
    vec2 colorUv=clamp(previousUv,.5/vec2(extent),1.0-.5/vec2(extent));
    vec3 lighting=texelFetch(uhscHistorySceneColor,ivec2(floor(colorUv*vec2(extent))),0).rgb;
    if(!UHSCFinite(lighting))return false;
    // Original diffuse uses point-clamped previous scene RGB, corrected for
    // pre-exposure. This renderer's linear HDR has exposure correction one.
    // SkylightLeaking defaults to zero. Keep the actual RGBA32F trace ABI;
    // do not add R11G11B10 rounding noise without that format's final store.
    // UE Tracing.usf291-294 measures the logical transport hit from the
    // unshifted probe, even though TraceScreen uses a footprint-biased origin.
    float logicalDistance=min(length(screenHitWorld-sourceWorld),spTraceParams.x);
    if(isnan(logicalDistance)||isinf(logicalDistance))return false;
    candidate.radiance=max(lighting,vec3(0));candidate.sceneDepth=-scene.z;
    candidate.logicalDistance=logicalDistance;candidate.owner=owner;
    candidate.sourcePixel=sourcePixel;candidate.startPixel=firstPixel;candidate.hitPixel=hitPixel;
    return true;
}
// EXTRA PROJECT CERTIFICATION, not a UE ScreenTrace depth acceptance rule:
// require the independently queried nearest native hit to have the candidate's
// owner and current visible depth. Never publish the approximate screen hit.
bool UHSCMatchNativeHit(UeScreenTraceCandidate candidate,vec3 nativeHit,int owner){
    if(owner<0||owner!=candidate.owner||owner>=uhscFlags.y||owner>=64)return false;
    vec4 clip=spViewProjection*vec4(nativeHit,1);
    vec4 view=spView*vec4(nativeHit,1);
    if(!UHSCFinite(clip)||!UHSCFinite(view)||!(clip.w>0.0)||!(view.z<0.0))return false;
    vec2 ndc=clip.xy/clip.w;
    if(any(greaterThanEqual(abs(ndc),vec2(1))))return false;
    ivec2 pixel=ivec2(floor((ndc*.5+.5)*vec2(UHSCExtent())));
    // Native's 1mm origin differs from the screen texel-footprint origin. A
    // farther visible pixel must not lend RGB to a native source-pixel hit.
    // Require exactly the candidate's visible pixel and leave both the original
    // source pixel and UE's footprint-biased first-step tile. These are
    // provenance checks, not a new distance/error tolerance.
    if(any(notEqual(pixel,candidate.hitPixel))||all(equal(pixel,candidate.sourcePixel))||
       all(equal(pixel,candidate.startPixel)))return false;
    vec4 scene=texelFetch(uhscViewPositions,pixel,0);
    if(!UHSCFinite(scene)||scene.w!=float(owner+1)||!(scene.z<0.0))return false;
    float nativeDepth=-view.z;
    return abs(nativeDepth+scene.z)<.02*max(-scene.z,1e-7)&&
        abs(nativeDepth-candidate.sceneDepth)<.02*max(candidate.sceneDepth,1e-7);
}
)GLSL";}

// Exactly the view0 resolve RGB, before diagnostics, tone mapping or overlays.
// GI is already reflected/rho/AO/intensity modulated; alpha is not a RGB gate.
inline std::string UeLumenHitSceneColorSnapshotShader(){
    return std::string("#version 450 core\nin vec2 vUV;\n")+
        ScreenProbeTraceDetail::ConstantsGLSL()+UeLumenHitSceneColorConstantsGLSL()+R"GLSL(
layout(binding=0) uniform sampler2D uhscBaseScene;
layout(binding=1) uniform sampler2D uhscReflectedGi;
layout(binding=2) uniform sampler2D uhscCurrentPositions;
layout(location=0) out vec4 outSceneColor;
layout(location=1) out vec4 outDepth;
void main(){
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    vec4 base=texelFetch(uhscBaseScene,pixel,0),gi=texelFetch(uhscReflectedGi,pixel,0);
    outSceneColor=vec4(base.rgb+gi.rgb,base.a);outDepth=vec4(0);
    vec4 p=texelFetch(uhscCurrentPositions,pixel,0);
    if(p.w<.5||!(p.z<0.0)||any(isnan(p))||any(isinf(p)))return;
    vec4 clip=(spViewProjection*spInverseView)*vec4(p.xyz,1);
    if(!(clip.w>0.0)||any(isnan(clip))||any(isinf(clip)))return;
    outDepth=vec4(clip.z/clip.w*.5+.5,0,0,p.w);
}
)GLSL";
}
} // namespace Render::SurfaceCacheDetail
