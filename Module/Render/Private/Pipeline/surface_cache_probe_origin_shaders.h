#pragma once
#include <string>
#include "Render/Private/Pipeline/screen_probe_trace_shaders.h"
#include "Render/Public/Pipeline/software_bvh_visibility.h"

namespace Render::SurfaceCacheDetail {
// Once per logical probe, before screen/SDF tracing. A very short primary-view
// query anchors depth-reconstructed positions back to their source triangle;
// a second sweep clips the original normal bias against nearby triangles.
// Neither query certifies that an
// arbitrary input point is outside every overlapping closed object. In
// particular, a first backface is retained and marked uncertain, never skipped.
//
// Inputs: uniform placement atlas 0, spawned placement atlas 1, BVH geometry 9;
// UBOs: PlacementData 9, SurfaceCacheData 12, OriginSafetyData 15.
// Output: RGBA32F, spGrid.x columns, logical probe order. xyz is the exact ray
// origin shared by both trace stages; w is 0 inactive, 1 full requested bias,
// 2 clipped, 3 near/backface uncertainty. Negative failure reasons remain
// unknown to the lighting integral: -1 invalid data/traversal exhaustion,
// -2 no source candidate in the short view segment, -3 source exceeds normal
// plane/along-view acceptance, -4 source normal/view orientation mismatch, -5 insufficient
// outward numeric clearance. It is not lighting
// confidence and must not be used to renormalize or brighten the GI integral.
inline std::string ProbeOriginShader(bool hardwareIntersection=false,bool reuseSoftwareCertification=false,
    bool reuseClosestBounds=false,bool fastGeometryAddressing=false) {
    return std::string("#version 450 core\n")+ScreenProbeTraceDetail::ConstantsGLSL()+
        (hardwareIntersection?(reuseSoftwareCertification?"#define PO_INTERSECT SCNativeOriginIntersectReusingCertification\n":
            "#define PO_INTERSECT SCNativeOriginIntersect\n"):"#define PO_INTERSECT SCIntersect\n")+
        SurfaceCacheDataGLSL()+SoftwareBvhVisibilityGLSL(9,reuseClosestBounds,fastGeometryAddressing)+
        (hardwareIntersection?NativeSurfaceCacheIntersectionGLSL():std::string{})+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D poUniform;
layout(binding=1) uniform sampler2D poSpawned;
layout(std140,binding=15) uniform OriginSafetyData {
    vec4 poSafety; // clearance fraction, numerical endpoint epsilon, raster source, UE HW origin policy
};
bool POFinite(vec3 value){return !any(isnan(value))&&!any(isinf(value));}
vec4 POProbe(int probe,int part) {
    bool uniformProbe=probe<spTraceAtlas.z;
    int slot=uniformProbe?probe:probe-spTraceAtlas.z;
    int columns=spGrid.x*(uniformProbe?1:spGrid.z);
    ivec2 at=ivec2(2*(slot%columns)+part,slot/columns);
    return uniformProbe?texelFetch(poUniform,at,0):texelFetch(poSpawned,at,0);
}
void main() {
    int probe=int(gl_FragCoord.x)+int(gl_FragCoord.y)*spGrid.x;
    outColor=vec4(0);if(probe>=spTraceAtlas.w)return;
    vec4 position=POProbe(probe,0),normalDepth=POProbe(probe,1);
    if(position.w<.5)return;
    outColor=vec4(position.xyz,-1);
    if(!POFinite(position.xyz)||!POFinite(normalDepth.xyz)||dot(normalDepth.xyz,normalDepth.xyz)<.5||
        scGeometry.z<=0)return;
    vec3 normal=normalize(normalDepth.xyz);
    float requestedBias=poSafety.w>.5?.001:spTraceParams.z,footprint=0.0;
    vec4 clip=spViewProjection*vec4(position.xyz,1);
    if(!(clip.w>0)||!POFinite(clip.xyz))return;
    vec3 ndc=clip.xyz/clip.w;
    vec4 corner=spInverseProjection*vec4(ndc.xy+1.0/vec2(spScreen.xy),ndc.z,1);
    if(abs(corner.w)<1e-10)return;corner/=corner.w;
    vec3 cornerWorld=(spInverseView*vec4(corner.xyz,1)).xyz;
    if(!POFinite(cornerWorld))return;
    footprint=length(cornerWorld-position.xyz);
    if(poSafety.w<=.5)requestedBias=max(requestedBias,2.0*abs(dot(cornerWorld-position.xyz,normal)));
    if(isnan(requestedBias)||isinf(requestedBias)||requestedBias<0)return;
    // Numeric endpoint clearance is independent of artistic/geometric ray bias.
    // The coordinate term covers several float ULPs at large world positions;
    // SCIntersect retains its existing 1e-5 triangle intersection t-min.
    float coordinateScale=max(1.0,max(abs(position.x),max(abs(position.y),abs(position.z))));
    float epsilon=max(poSafety.y,8.0*1.1920928955078125e-7*coordinateScale);

    // Unproject both ends of this view line, rather than normalizing p-camera:
    // the latter is not the primary ray for an orthographic projection.
    vec4 nearView=spInverseProjection*vec4(ndc.xy,-1,1),farView=spInverseProjection*vec4(ndc.xy,1,1);
    if(abs(nearView.w)<1e-10||abs(farView.w)<1e-10)return;
    vec3 nearWorld=(spInverseView*vec4(nearView.xyz/nearView.w,1)).xyz;
    vec3 farWorld=(spInverseView*vec4(farView.xyz/farView.w,1)).xyz;
    vec3 primaryDirection=farWorld-nearWorld;
    if(!POFinite(primaryDirection)||dot(primaryDirection,primaryDirection)<1e-12)return;
    primaryDirection=normalize(primaryDirection);
    // The diagnostic depth-reconstruction path needs a local Depth32F error
    // bound: one device-depth ULP can span different world distances during
    // camera motion. Direct raster positions do not inherit that quantization;
    // they retain the geometric float/footprint tolerances below. Never inflate
    // artistic ray bias or outward clearance to compensate for source error.
    // Differences are measured from the stored point so the forward/inverse
    // projection round-trip error is covered as well as the depth interval.
    vec3 depthLowerDelta=vec3(0),depthUpperDelta=vec3(0);
    float deviceDepth=ndc.z*.5+.5;
    if(poSafety.z<.5&&deviceDepth>=0.0&&deviceDepth<=1.0) {
        uint depthBits=floatBitsToUint(deviceDepth)&0x7fffffffu;
        float lowerDepth=uintBitsToFloat(depthBits>0u?depthBits-1u:0u);
        float upperDepth=uintBitsToFloat(min(depthBits+1u,0x3f800000u));
        vec4 lowerView=spInverseProjection*vec4(ndc.xy,lowerDepth*2.0-1.0,1);
        vec4 upperView=spInverseProjection*vec4(ndc.xy,upperDepth*2.0-1.0,1);
        if(abs(lowerView.w)<1e-10||abs(upperView.w)<1e-10)return;
        vec3 lowerWorld=(spInverseView*vec4(lowerView.xyz/lowerView.w,1)).xyz;
        vec3 upperWorld=(spInverseView*vec4(upperView.xyz/upperView.w,1)).xyz;
        if(!POFinite(lowerWorld)||!POFinite(upperWorld))return;
        depthLowerDelta=lowerWorld-position.xyz;depthUpperDelta=upperWorld-position.xyz;
    }
    // At grazing incidence, a small normal-plane reconstruction error becomes
    // a larger displacement along the view ray. Keep the original tight plane
    // tolerance plus the measured depth interval, while retaining the original
    // quarter-footprint cap on along-view displacement. Neither grows with the
    // artistic normal bias, and neither changes the outward sweep clearance.
    float baseNormalTolerance=max(8.0*epsilon,max(.01*footprint,32.0*1.1920928955078125e-7*coordinateScale));
    float storedNormalTolerance=baseNormalTolerance+max(abs(dot(depthLowerDelta,normal)),abs(dot(depthUpperDelta,normal)));
    float viewFacing=abs(dot(normal,-primaryDirection));
    float alongAcceptance=min(storedNormalTolerance/max(viewFacing,.02),max(baseNormalTolerance,.25*footprint));
    float anchorWindow=2.0*alongAcceptance,anchorDistance=2.0*anchorWindow;
    vec3 anchorStart=position.xyz-primaryDirection*anchorWindow,sourceNormal;int sourceTriangle;bool sourceExhausted;
    bool sourceFound=PO_INTERSECT(anchorStart,primaryDirection,anchorDistance,sourceNormal,sourceTriangle,sourceExhausted,false);
    if(sourceExhausted)return;
    if(!sourceFound){outColor.w=-2;return;}
    vec3 anchor=anchorStart+primaryDirection*anchorDistance;
    float alignment=dot(sourceNormal,normal);
    if(!POFinite(anchor))return;
    vec3 anchorDelta=anchor-position.xyz;
    float sourceNormalTolerance=baseNormalTolerance+max(abs(dot(depthLowerDelta,sourceNormal)),abs(dot(depthUpperDelta,sourceNormal)));
    if(length(anchorDelta)>alongAcceptance||abs(dot(anchorDelta,sourceNormal))>sourceNormalTolerance||
        abs(dot(anchorDelta,normal))>storedNormalTolerance){outColor.w=-3;return;}
    if(alignment<=.5||dot(sourceNormal,-primaryDirection)<=1e-5){outColor.w=-4;return;}

    float distance=requestedBias+epsilon;vec3 hitNormal;int triangle;bool exhausted;
    bool found=PO_INTERSECT(anchor,normal,distance,hitNormal,triangle,exhausted,false);
    if(exhausted)return;
    float bias=requestedBias,status=1;
    if(found) {
        // All faces constrain displacement, including exits from intersecting
        // geometry. The subsequent lighting rays still observe that geometry.
        bias=min(requestedBias,max(0.0,distance-epsilon)*clamp(poSafety.x,.05,.95));
        status=dot(hitNormal,normal)>=0||distance<=2.0*epsilon?3.0:2.0;
    }
    // An outward numeric clearance from the exact source plane and the nearby
    // occluder bound must both fit. Do not return an origin on/inside its source
    // when a clipped displacement is too small; those rays remain unknown.
    if(bias*alignment<epsilon){outColor.w=-5;return;}
    outColor=vec4(anchor+normal*bias,status);
}
)GLSL";
}

// Debug overlay for the actual shared tracing origin. Unlike the normal probe
// sphere marker, this never moves its center by a display radius along a normal.
// Draw the fullscreen triangle once per logical probe with depth testing off.
inline std::string ProbeOriginMarkerVertexShader() {
    return std::string("#version 450 core\n")+ScreenProbeTraceDetail::ConstantsGLSL()+R"GLSL(
layout(location=0) in vec2 position;layout(location=1) in vec2 uv;
layout(binding=0) uniform sampler2D poOrigins;
out vec2 poMarkerPosition;flat out vec3 poMarkerColor;
void main() {
    poMarkerPosition=position;poMarkerColor=vec3(1,.02,.04);
    ivec2 size=textureSize(poOrigins,0);int probe=gl_InstanceID;
    if(probe>=spTraceAtlas.w){gl_Position=vec4(2,2,2,1);return;}
    vec4 origin=texelFetch(poOrigins,ivec2(probe%size.x,probe/size.x),0);
    if(origin.w==0||any(isnan(origin.xyz))||any(isinf(origin.xyz))) {
        gl_Position=vec4(2,2,2,1);return;
    }
    if(origin.w>2.5)poMarkerColor=vec3(.95,.03,.65);
    else if(origin.w>1.5)poMarkerColor=vec3(1,.38,.025);
    else if(origin.w>.5)poMarkerColor=vec3(.015,.85,.8);
    vec4 clip=spViewProjection*vec4(origin.xyz,1);
    if(clip.w<=0){gl_Position=vec4(2,2,2,1);return;}
    clip.xy+=position*max(spOptions.y,1.0)*2.0/vec2(spScreen.xy)*clip.w;
    gl_Position=clip;
}
)GLSL";
}
inline std::string ProbeOriginMarkerFragmentShader() {return R"GLSL(#version 450 core
in vec2 poMarkerPosition;flat in vec3 poMarkerColor;
layout(location=0) out vec4 outColor;
void main(){if(dot(poMarkerPosition,poMarkerPosition)>1)discard;outColor=vec4(poMarkerColor,1);}
)GLSL";}
} // namespace Render::SurfaceCacheDetail
