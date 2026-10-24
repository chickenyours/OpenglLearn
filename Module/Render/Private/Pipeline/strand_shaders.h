#pragma once
#include <string>
#include <stdexcept>
#include <utility>
#include "Render/Private/Pipeline/fiber_scattering_shaders.h"
#include "Render/Private/Pipeline/fiber_volume_shaders.h"
#include "Render/Private/Pipeline/strand_color_shaders.h"
#include "Render/Private/Pipeline/strand_ntt_shaders.h"
#include "Render/Private/Pipeline/strand_indirect_shaders.h"
#include "Render/Private/Pipeline/strand_direct_shaders.h"
#include "Render/Private/Pipeline/strand_debug_shaders.h"
#include "Render/Private/Pipeline/fiber_point_paths_shaders.h"
#include "Render/Private/Pipeline/strand_curve_shaders.h"
#include "Render/Private/Pipeline/strand_native_ds_query.h"
#include "Render/Public/Material/pbr_material.h"

namespace Render::PipelineDetail {
inline std::string StrandVertexShader(bool revealage=false,bool segmentColors=false,bool curveGeometry=false) {
    std::string source=R"GLSL(#version 450 core
invariant gl_Position;
layout(location=0) in vec4 positionRadius;
layout(location=1) in vec4 tangentArc;
layout(location=2) in vec2 colorCoordinates;
layout(location=3) in vec4 previousPositionRadius;
layout(location=4) in vec4 previousTangentArc;
layout(std140,binding=0) uniform StrandView {
    mat4 strandViewProjection; vec4 strandCamera; vec4 strandViewport;
    mat4 strandPreviousViewProjection;
    mat4 strandCurrentUnjitteredViewProjection;
    mat4 strandPreviousUnjitteredViewProjection;
    vec4 strandPreviousCamera, strandMotionState;
};
layout(std140,binding=1) uniform StrandObject {mat4 strandModel;};
layout(std140,binding=8) uniform StrandMotionObject {
    mat4 strandPreviousModel;vec4 strandMotionObject;
};
layout(std140,binding=2) uniform StrandMaterial {
    vec4 strandRootColor, strandTipColor, strandSurface, strandVolume, strandRaster, strandFiber, strandScattering, strandShadows, strandIbl;
    vec4 strandColorWeights, strandColorOffsets, strandColorVariation, strandNativeMoments, strandSpatialIncident;
    vec4 strandOpaqueShadowParameters;
    vec4 strandMomentLut[256];
};
#ifndef STRAND_REVEAL_ONLY
layout(location=0) out vec3 strandWorldPosition;
layout(location=1) out vec3 strandWorldTangent;
layout(location=2) out float strandArc;
layout(location=5) out vec2 strandColorCoordinates;
layout(location=7) out vec3 strandShutterCamera;
#endif
layout(location=3) noperspective out float strandSide;
layout(location=4) out float strandCoverage;
vec3 StrandSafe(vec3 v,vec3 fallbackValue) {
    float l=dot(v,v);return l>1e-12?v*inversesqrt(l):fallbackValue;
}
void main() {
    vec3 center=(strandModel*vec4(positionRadius.xyz,1.0)).xyz;
    vec3 T=StrandSafe(mat3(strandModel)*tangentArc.xyz,vec3(0.0,-1.0,0.0));
    vec3 camera=strandCamera.xyz;
    mat4 rasterProjection=strandViewProjection;
    float modelScale=max(length(strandModel[0].xyz),max(length(strandModel[1].xyz),length(strandModel[2].xyz)));
    float signedRadius=positionRadius.w;
    // z is exposure age, so existing/default zero constants retain current
    // raster arithmetic. Invalid prior geometry/model always remains current.
    if(strandMotionState.z>0.0&&strandMotionState.x>.5&&strandMotionObject.x>.5){
        float sampleTime=1.0-clamp(strandMotionState.z,0.0,1.0);
        vec3 oldCenter=(strandPreviousModel*vec4(previousPositionRadius.xyz,1.0)).xyz;
        vec3 oldT=StrandSafe(mat3(strandPreviousModel)*previousTangentArc.xyz,T);
        center=mix(oldCenter,center,sampleTime);T=StrandSafe(mix(oldT,T,sampleTime),T);
        camera=mix(strandPreviousCamera.xyz,camera,sampleTime);
        float oldScale=max(length(strandPreviousModel[0].xyz),max(length(strandPreviousModel[1].xyz),length(strandPreviousModel[2].xyz)));
        modelScale=mix(oldScale,modelScale,sampleTime);
        signedRadius=mix(previousPositionRadius.w,signedRadius,sampleTime);
        rasterProjection=strandPreviousUnjitteredViewProjection*(1.0-sampleTime)+strandCurrentUnjitteredViewProjection*sampleTime;
        // Every time sample shares CURRENT jitter. Previous-frame jitter is
        // not physical camera motion and must not widen a static ribbon.
        vec2 jitterNdc=vec2(strandPreviousCamera.w,strandMotionState.w);
        for(int column=0;column<4;++column)rasterProjection[column].xy+=jitterNdc*rasterProjection[column].w;
    }
    vec3 V=StrandSafe(camera-center,vec3(0.0,0.0,1.0));
    vec3 axis=abs(T.y)<.95?vec3(0.0,1.0,0.0):vec3(1.0,0.0,0.0);
    vec3 side=StrandSafe(cross(T,V),StrandSafe(cross(T,axis),vec3(1.0,0.0,0.0)));
    float fullWidth=max(2.0*abs(signedRadius)*strandVolume.y,strandShadows.z);
    if(strandShadows.w>0.0)fullWidth=min(fullWidth,strandShadows.w);
    float radius=.5*fullWidth*modelScale;
    vec4 clip=rasterProjection*vec4(center,1.0);
    vec4 edge=rasterProjection*vec4(center+side*radius,1.0);
    vec2 delta=(edge.xy/max(abs(edge.w),1e-7)-clip.xy/max(abs(clip.w),1e-7))*strandViewport.xy*.5;
    float physicalHalfWidth=length(delta);
    vec2 pixelDirection=physicalHalfWidth>1e-7?delta/physicalHalfWidth:vec2(1.0,0.0);
    float filteredHalfWidth=min(physicalHalfWidth,.5*strandRaster.y);
    // Include the complete half-pixel filter support outside the physical
    // ribbon. Enlarging only inward and then multiplying a second edge mask
    // discarded most of a subpixel fiber's intended area.
    float filterSupport=.5*(abs(pixelDirection.x)+abs(pixelDirection.y));
    float halfWidth=strandShadows.y>.5?max(filteredHalfWidth+filterSupport,.5*strandRaster.x):
        clamp(physicalHalfWidth,.5*strandRaster.x,.5*strandRaster.y);
    float signedSide=positionRadius.w<0.0?-1.0:1.0;
    clip.xy+=signedSide*pixelDirection*halfWidth*2.0/strandViewport.xy*clip.w;
    gl_Position=clip;
#ifndef STRAND_REVEAL_ONLY
    strandWorldPosition=center;strandWorldTangent=T;
    strandArc=clamp(tangentArc.w,0.0,1.0);
    strandColorCoordinates=colorCoordinates;
    strandShutterCamera=camera;
#endif
    strandSide=signedSide;
    // Widening a subpixel fiber must preserve its integrated coverage.
    strandCoverage=strandShadows.y>.5?filteredHalfWidth:
        min(1.0,physicalHalfWidth/max(halfWidth,1e-7));
}
)GLSL";
    if(segmentColors&&!revealage) {
        const auto declarations=source.find("vec3 StrandSafe(");
        source.insert(declarations,"layout(binding=6) uniform sampler2D strandSegmentColors;\nlayout(location=8) out vec3 strandSegmentColor;\n");
        const auto body=source.find("    strandWorldPosition=center;");
        source.insert(body,curveGeometry?
            "    strandSegmentColor=vec3(0.0);\n    if(strandNativeMoments.w>.5){int width=textureSize(strandSegmentColors,0).x;int a=int(curve.shadeStart),b=int(curve.shadeEnd);strandSegmentColor=mix(texelFetch(strandSegmentColors,ivec2(a%width,a/width),0).rgb,texelFetch(strandSegmentColors,ivec2(b%width,b/width),0).rgb,curve.t);}\n":
            "    strandSegmentColor=vec3(0.0);\n    if(strandNativeMoments.w>.5){int point=gl_VertexID/2;int width=textureSize(strandSegmentColors,0).x;strandSegmentColor=texelFetch(strandSegmentColors,ivec2(point%width,point/width),0).rgb;}\n");
    }
    if(revealage)source.insert(source.find('\n')+1,"#define STRAND_REVEAL_ONLY 1\n");
    if(curveGeometry)source=StrandCurveVertexMain(std::move(source));
    return source;
}

// mode 0: original weighted blend, 1: nearest-depth discovery, 2: weighted
// tail after the selected depth, 3: aggregate every fragment at that depth.
// No graphics storage/atomic API is required.
// Apply the same specialization after the paired-MRT wrapper's source edits.
// Disabled code is absent from the shader source, not a dynamic uniform branch.
inline std::string StrandPointPathsGLSL() {
    auto source=FiberPointPathsGLSL();
    const std::string name="vec2 FiberPointIblPath(";
    const auto function=source.find(name);
    if(function==std::string::npos)throw std::runtime_error("Fiber point path function contract changed");
    source.replace(function,name.size(),"vec2 FiberPointDirectPath(");
    const std::string guard="if(!enabled||fiberAtlas.w<.5||fiberTrace.w<=1.5)";
    const auto at=source.find(guard,function);
    if(at==std::string::npos)throw std::runtime_error("Fiber point path enable contract changed");
    source.replace(at,guard.size(),"if(!enabled||fiberAtlas.w<.5)");
    const auto body=source.find('{',function);
    source.insert(body+1,"\n    if(enabled&&StrandNativeDsEnabled())return StrandNativeDsPath(localPosition,localDirection,maxDistance);\n");
    source+=R"GLSL(
vec2 FiberPointIblPath(vec3 P,vec3 L,float distance,bool enabled) {
    if(fiberTrace.w<=1.5)return vec2(0.0,1.0);
    return FiberPointDirectPath(P,L,distance,enabled);
}
)GLSL";
    return source;
}
inline std::string StrandColorFeatures(std::string source,bool diagnostics,bool pointPaths) {
    if(!pointPaths){
        const std::string define="#define STRAND_LOCAL_DIRECT_PATHS 1\n";
        if(const auto at=source.find(define);at!=std::string::npos)source.erase(at,define.size());
        const auto library=StrandPointPathsGLSL();const auto at=source.find(library);
        if(at!=std::string::npos)source.erase(at,library.size());
        const auto begin=source.find("void StrandConsumedIblPaths("),end=source.find("vec3 StrandSourceIbl(",begin);
        if(begin!=std::string::npos&&end!=std::string::npos)source.replace(begin,end-begin,
            "void StrandConsumedIblPaths(vec3 T,vec3 V,inout vec2 counts,inout vec2 visibility) {}\n");
    }
    if(!diagnostics){
        const auto library=StrandDebugGLSL();const auto at=source.find(library);
        if(at!=std::string::npos)source.erase(at,library.size());
        const auto begin=source.find("    int diagnostic="),end=source.find("    float sunVisibility=",begin);
        if(begin!=std::string::npos&&end!=std::string::npos)source.erase(begin,source.find('\n',end)+1-begin);
        const std::string branch="        if(diagnostic>0) {";
        for(auto at=source.find(branch);at!=std::string::npos;at=source.find(branch)){
            auto close=source.find('{',at);unsigned depth=0;
            for(;close<source.size();++close){if(source[close]=='{')++depth;else if(source[close]=='}'&&!--depth){++close;break;}}
            if(depth)throw std::runtime_error("Strand diagnostic branch contract changed");
            source.erase(at,close-at);
        }
        const auto selection=source.find("    if(diagnostic==1)"),finish=source.find("    color=clamp(",selection);
        if(selection!=std::string::npos&&finish!=std::string::npos)source.erase(selection,finish-selection);
    }
    return source;
}
// Applied after paired-MRT and diagnostic specializations, so coverage,
// exact-depth ties, log reveal and alpha weighting remain byte-for-byte shared.
inline std::string StrandSegmentColorConsumer(std::string source) {
    const auto main=source.find("void main()");
    const auto begin=source.find("    vec3 base=mix(",main);
    const auto finish=source.find("    color=clamp(",begin);
    if(main==std::string::npos||begin==std::string::npos||finish==std::string::npos)
        throw std::runtime_error("Segment color consumer lighting contract changed");
    const auto declaration=source.find("    vec3 color=base*ambient;",begin);
    if(declaration==std::string::npos)throw std::runtime_error("Segment color local declaration changed");
    source.replace(declaration,std::string("    vec3 color=base*ambient;").size(),"    color=base*ambient;");
    const auto end=source.find("    color=clamp(",begin);
    source.insert(end,"    }\n");
    source.insert(begin,"    vec3 color;\n    if(strandNativeMoments.w>.5)color=strandSegmentColor;\n    else {\n");
    source.insert(source.find("void main()"),"layout(location=8) in vec3 strandSegmentColor;\n");
    return source;
}
inline std::string StrandFragmentShader(bool revealage, int mode=0,bool diagnostics=true,bool pointPaths=true) {
    if(revealage)diagnostics=pointPaths=false;
    std::string source=R"GLSL(#version 450 core
layout(std140,binding=0) uniform StrandView {
    mat4 strandViewProjection; vec4 strandCamera; vec4 strandViewport;
    mat4 strandPreviousViewProjection;
    mat4 strandCurrentUnjitteredViewProjection;
    mat4 strandPreviousUnjitteredViewProjection;
    vec4 strandPreviousCamera, strandMotionState;
};
layout(std140,binding=2) uniform StrandMaterial {
    vec4 strandRootColor, strandTipColor, strandSurface, strandVolume, strandRaster, strandFiber, strandScattering, strandShadows, strandIbl;
    vec4 strandColorWeights, strandColorOffsets, strandColorVariation, strandNativeMoments, strandSpatialIncident;
    vec4 strandOpaqueShadowParameters;
    vec4 strandMomentLut[256];
};
layout(std140,binding=3) uniform StrandLights {
    vec4 pointPositions[4],pointColors[4],strandAmbient,unusedOutput,unusedAA;
};
layout(std140,binding=4) uniform StrandSun {vec4 strandSunDirection,strandSunColor;};
layout(std140,binding=11) uniform StrandSky {
    vec4 strandSkySH[9];vec4 strandSkyOptions;vec4 strandSkyTransform;
};
layout(location=0) in vec3 strandWorldPosition;
// A native segment uses START for the outgoing view vector, but END for
// incident light, source probe and optical-path sampling. Fragment mode sets
// both positions to its existing interpolated centerline point.
vec3 strandViewOrigin;
// Fragment fallback uses an explicit host phase. The native segment producer
// overrides this with the source END-index rotation before shared lighting.
float strandSourceShadowAngle;
layout(location=1) in vec3 strandWorldTangent;
layout(location=2) in float strandArc;
layout(location=3) noperspective in float strandSide;
layout(location=4) in float strandCoverage;
layout(location=5) in vec2 strandColorCoordinates;
layout(location=7) in vec3 strandShutterCamera;
layout(binding=1) uniform sampler2D strandGradientSampler;
#ifdef STRAND_LAYERED_TRANSPARENCY
layout(binding=2) uniform sampler2D strandPreviousDepth;
layout(binding=3) uniform sampler2D strandOpaqueDepth;
#endif
layout(location=0) out vec4 outColor;
vec3 StrandSafe(vec3 v,vec3 fallbackValue) {
    float l=dot(v,v);return l>1e-12?v*inversesqrt(l):fallbackValue;
}
float StrandLobe(vec3 T,vec3 H,float roughness) {
    float longitudinal=clamp(dot(T,H),-1.0,1.0);
    float exponent=clamp(2.0/max(roughness*roughness,.0025)-2.0,2.0,800.0);
    return pow(max(1.0-longitudinal*longitudinal,0.0),exponent*.5)*sqrt(exponent+2.0)*.28;
}
float StrandPixelCdf(float x,vec2 normal) {
    float a=.5*max(normal.x,normal.y),b=.5*min(normal.x,normal.y);
    if(b<1e-5)return clamp(.5+x/max(2.0*a,1e-6),0.0,1.0);
    float sum=a+b,difference=a-b;
    if(x<=-sum)return 0.0;
    if(x>=sum)return 1.0;
    if(x<-difference){float q=x+sum;return q*q/(8.0*a*b);}
    if(x<=difference)return .5+x/(2.0*a);
    float q=sum-x;return 1.0-q*q/(8.0*a*b);
}
vec3 StrandSkyDiffuse(vec3 tangent) {
    if(strandIndirectOptions.x>.5)return StrandIndirectRing(tangent);
    if(strandSkyOptions.x<.5 || strandSkyOptions.y<=0.0 || strandSkyOptions.z<=0.0)return vec3(0.0);
    vec3 T=vec3(strandSkyTransform.x*tangent.x+strandSkyTransform.y*tangent.z,tangent.y,
        -strandSkyTransform.y*tangent.x+strandSkyTransform.x*tangent.z);
    // SkyEnvironment stores cosine-convolved SH / pi. Average that same
    // precomputed response over the cylinder's normal circle perpendicular to
    // T: <n>=0, <n_i*n_j>=(delta_ij-T_i*T_j)/2. This avoids treating view-facing
    // ribbon normals as a surface or losing the opaque PBR environment.
    vec3 value=strandSkySH[0].rgb*.282095-
        .546274*(strandSkySH[4].rgb*T.x*T.y+strandSkySH[5].rgb*T.y*T.z+strandSkySH[7].rgb*T.x*T.z)+
        strandSkySH[6].rgb*(.157696*(1.0-3.0*T.z*T.z))+
        strandSkySH[8].rgb*(.273137*(T.y*T.y-T.x*T.x));
    return max(value,vec3(0.0))*strandSkyOptions.y*strandSkyOptions.z;
}
vec3 StrandSkyDirectional(vec3 direction) {
    if(strandIndirectOptions.x>.5)return StrandIndirectDirection(direction);
    if(strandSkyOptions.x<.5||strandSkyOptions.y<=0.0||strandSkyOptions.z<=0.0)return vec3(0.0);
    vec3 n=vec3(strandSkyTransform.x*direction.x+strandSkyTransform.y*direction.z,direction.y,
        -strandSkyTransform.y*direction.x+strandSkyTransform.x*direction.z);
    // Host adapter: retain the directional bands of the same baked E/pi SH.
    // Native StrandIndirectLightSRV is a different probe producer; this input
    // adapter is deliberately separate from the transcribed scattering solve.
    vec3 value=strandSkySH[0].rgb*.282095+
        .488603*(strandSkySH[1].rgb*n.y+strandSkySH[2].rgb*n.z+strandSkySH[3].rgb*n.x)+
        1.092548*(strandSkySH[4].rgb*n.x*n.y+strandSkySH[5].rgb*n.y*n.z+strandSkySH[7].rgb*n.x*n.z)+
        strandSkySH[6].rgb*(.315392*(3.0*n.z*n.z-1.0))+
        strandSkySH[8].rgb*(.546274*(n.x*n.x-n.y*n.y));
    return max(value,vec3(0.0))*strandSkyOptions.y*strandSkyOptions.z;
}
vec3 StrandLight(vec3 base,vec3 T,vec3 V,vec3 L,vec3 radiance,float pathCount);
)GLSL";
    source.insert(source.find("vec3 StrandSafe"),StrandIndirectGLSL());
    source.insert(source.find("vec3 StrandSafe"),StrandSourceDirectGLSL());
    source+=FiberVolumeBlockGLSL();
    source+=StrandNativeDsQueryGLSL();
    if(pointPaths)source+=StrandPointPathsGLSL();
    auto effectsBlock=Material::PbrSceneEffectsGLSL();
    effectsBlock.replace(effectsBlock.find("binding = 4"),11,"binding = 6");
    source+=effectsBlock;
    source+="\nlayout(binding=14) uniform sampler2D shadowAtlas;\n";
    source+=Material::PbrDirectionalShadowGLSL();
    source+=R"GLSL(
layout(binding=12) uniform sampler2D strandOpticalPaths;
layout(binding=13) uniform sampler2D strandOpticalPathsExtra;
layout(binding=15) uniform sampler2D strandAmbientOpticalPaths;
)GLSL";
    source+=StrandNttGLSL();
    source+="\n#define FIBER_MOMENT_LUT 1\n#define FIBER_NATIVE_MOMENT_LUT 1\n#define FIBER_NATIVE_MOMENT_ACTIVE (strandNativeMoments.x>.5)\n#define FIBER_NATIVE_MOMENT_ROUGHNESS strandFiber.y\n";
    source+=FiberScatteringGLSL();
    source+=StrandColorGLSL();
    source+=R"GLSL(
// Explicit host six-axis forward adapter, retained for the editor's legacy
// Forward-only comparison mode. Native Diana uses the dual-direction closure.
vec3 StrandNativeDsForwardEnvironment(vec3 base,vec3 tangent,vec3 localPosition) {
    vec3 q=log2(clamp(base,vec3(.001),vec3(1.0)));
    q=q*q*(.01385651808232069*sqrt(max(strandFiber.w,0.0)));
    vec3 result=vec3(0.0);
    for(int i=0;i<3;++i) {
        vec3 axis=vec3(0.0);axis[i]=1.0;
        vec2 positive=StrandNativeDsPath(localPosition,axis,1e4);
        vec2 negative=StrandNativeDsPath(localPosition,-axis,1e4);
        float theta=abs(tangent[i]);
        vec3 af=clamp(vec3(FiberMoments(theta,q.r).r,FiberMoments(theta,q.g).r,FiberMoments(theta,q.b).r),vec3(.0001),vec3(.99));
        result+=.5*(pow(af,vec3(max(positive.x,0.0)*max(strandScattering.z,0.0)))*positive.y+
                     pow(af,vec3(max(negative.x,0.0)*max(strandScattering.z,0.0)))*negative.y);
    }
    return result/3.0;
}
vec3 StrandIblIncident(vec3 base,vec3 direction) {
    vec3 value=(max(strandAmbient.rgb,vec3(0.0))+StrandSkyDirectional(direction))*strandRaster.w;
    // Preserve the previous adapter exactly while disabled. Emissive
    // %643..645/%6543 use these precise weights and the already-linear albedo
    // clamped before both absorption and incident colorization.
    if(strandSpatialIncident.w>.5)return mix(value,
        vec3(dot(value,vec3(.2126729041337967,.7151522040367126,.07217500358819962)))*
        max(base,vec3(.001)),strandScattering.w);
    return mix(value,vec3(dot(value,vec3(.2126,.7152,.0722)))*base,strandScattering.w);
}
void StrandConsumedIblPaths(vec3 T,vec3 V,inout vec2 counts,inout vec2 visibility) {
    // Native per-shading-point directions must not consume paths baked from
    // a different voxel mean tangent. This optional host integration queries
    // the same two actual L directions as StrandSourceIbl and all diagnostics.
    if((strandNativeMoments.z<.5&&!StrandNativeDsEnabled())||fiberAtlas.w<.5||fiberTrace.w<=1.5||
       strandScattering.x<.5||strandIbl.y<.5||strandIbl.z<.5)return;
    vec3 radial=StrandSafe(V-T*dot(T,V),vec3(0.0,0.0,1.0));
    vec3 B=StrandSafe(cross(T,radial),vec3(1.0,0.0,0.0));
    vec3 N=StrandSafe(cross(B,T),radial);
    vec3 directions[2]=vec3[2](StrandSafe(N+B,N),StrandSafe(N-B,N));
    vec3 localPosition=(fiberInverseModel*vec4(strandWorldPosition,1.0)).xyz;
    for(int i=0;i<2;++i) {
        vec2 path=FiberPointIblPath(localPosition,mat3(fiberInverseModel)*directions[i],1e6,true);
        counts[i]=path.x;visibility[i]=path.y;
    }
}
vec3 StrandSourceIbl(vec3 base,vec3 T,vec3 V,vec2 sourceCounts,vec2 sourceVisibility) {
    vec3 radial=StrandSafe(V-T*dot(T,V),vec3(0.0,0.0,1.0));
    vec3 B=StrandSafe(cross(T,radial),vec3(1.0,0.0,0.0));
    vec3 N=StrandSafe(cross(B,T),radial);
    vec3 directions[2]=vec3[2](StrandSafe(N+B,N),StrandSafe(N-B,N));
    // Original Emissive %6493..6555 queries one spatial probe response along
    // normalize(worldShadingPoint-mCenterPos), then reuses it for both L+/L-.
    // This center is the transformed groom AABB estimate, not a recovered
    // runtime probe position. The disabled path retains the old adapter.
    vec3 incident=strandSpatialIncident.w>.5?StrandIblIncident(base,
        StrandSafe(strandWorldPosition-strandSpatialIncident.xyz,vec3(0.0,1.0,0.0))):vec3(0.0);
    vec3 result=vec3(0.0);
    for(int i=0;i<2;++i) {
        vec3 L=directions[i];
        // The cached fallback bakes these oblique paths from each voxel's mean
        // tangent. Optional StrandConsumedIblPaths replaces both N/G inputs
        // with actual point/direction queries before this unchanged closure.
        float count=fiberAtlas.w>.5&&fiberTrace.w>1.5?max(sourceCounts[i],0.0):0.0;
        vec3 response=FiberIblDirection(base,T,V,L,strandFiber,strandSurface.x,strandVolume.w,
            vec3(strandSurface.z,strandVolume.x,strandSurface.w),strandScattering.y,
            count,strandScattering.z,strandIbl.x,sourceVisibility[i]);
        // Source two-direction quadrature is pi/2 times their SUM. The SH is
        // already E/pi, so no additional pi division belongs in this adapter.
        result+=(strandSpatialIncident.w>.5?incident:StrandIblIncident(base,L))*response*(.5*FiberPi);
    }
    return result;
}
vec3 StrandLight(vec3 base,vec3 T,vec3 V,vec3 L,vec3 radiance,float pathCount,float visibility) {
    if(strandScattering.x>.5&&strandColorVariation.w>.5)
        return FiberSourceDirect(base,T,V,L,strandFiber,strandSurface.x,strandVolume.w,
            vec3(strandSurface.z,strandVolume.x,strandSurface.w),strandScattering.y,strandScattering.z,pathCount,
            fiberAtlas.w>.5,strandIbl.x,visibility)*radiance;
    if(strandScattering.x>.5)
        return FiberScatter(base,T,V,L,strandFiber,strandSurface.x,strandVolume.w,
            vec3(strandSurface.z,strandVolume.x,strandSurface.w),strandScattering.y,strandScattering.z,pathCount,fiberAtlas.w>.5)*radiance*visibility;
    float sinTL=sqrt(max(1.0-dot(T,L)*dot(T,L),0.0));
    vec3 N=StrandSafe(V-T*dot(T,V),vec3(0.0,0.0,1.0));
    vec3 H=StrandSafe(V+L,N);
    float shift=strandVolume.w;
    float primary=StrandLobe(StrandSafe(T+N*shift,T),H,strandSurface.x);
    float secondary=StrandLobe(StrandSafe(T-N*(shift+.12),T),H,strandSurface.y);
    float fresnel=.046+.954*pow(1.0-clamp(dot(V,H),0.0,1.0),5.0);
    vec3 specular=vec3(primary*strandSurface.z)+sqrt(max(base,vec3(0.0)))*secondary*strandSurface.w;
    float front=.25+.75*sqrt(max(1.0-dot(T,V)*dot(T,V),0.0));
    float through=pow(clamp(dot(-V,L),0.0,1.0),2.0)*strandVolume.x;
    return (base*(sinTL*front/3.14159265+through)+specular*fresnel*sinTL)*radiance*
        (fiberAtlas.w>.5?exp(-max(pathCount,0.0)*strandScattering.z):1.0)*visibility;
}
float StrandOpaqueShadowVisibility(float rawVisibility,float power) {
    // Selected native Material.hm_shadowPow (byte120), after mean CSM PCF.
    // Exact endpoints avoid log/pow zero behavior and one retains old pixels.
    if(rawVisibility<=0.0)return 0.0;
    if(rawVisibility>=1.0)return 1.0;
    if(power==1.0)return rawVisibility;
    return clamp(pow(rawVisibility,power),0.0,1.0);
}
vec3 StrandExternallyShadowedLight(vec3 base,vec3 T,vec3 V,vec3 L,vec3 radiance,
    float pathCount,float dsVisibility,float externalVisibility) {
    float visibility=externalVisibility*dsVisibility;
    if(strandScattering.x>.5&&strandColorVariation.w>.5)
        return FiberSourceDirect(base,T,V,L,strandFiber,strandSurface.x,strandVolume.w,
            vec3(strandSurface.z,strandVolume.x,strandSurface.w),strandScattering.y,strandScattering.z,pathCount,
            fiberAtlas.w>.5,strandIbl.x,visibility,dsVisibility)*radiance;
    // Generic and tangent-lobe materials retain their combined-visibility
    // contract. Only the source closure distinguishes the input to K.
    return StrandLight(base,T,V,L,radiance,pathCount,visibility);
}
void main() {
    strandViewOrigin=strandWorldPosition;
    strandSourceShadowAngle=sourceShadowFilter.z;
    // Derivatives are evaluated before the ribbon coverage discard, matching
    // the existing PBR shadow receiver's derivative validity contract.
    vec3 shadowDx=dFdx(strandWorldPosition),shadowDy=dFdy(strandWorldPosition);
    float derivative=max(fwidth(strandSide),.01);
    float edge=1.0-smoothstep(1.0-derivative,1.0,abs(strandSide));
    float alpha=clamp(edge*strandCoverage*strandVolume.z,0.0,.995);
    if(strandShadows.y>.5) {
        // Area of a unit square pixel intersected by the physical strip.
        // Integrating the projected two-axis pixel footprint also preserves
        // oblique subpixel fibers; a 1D normal-direction box alone loses area
        // at diagonal pixel phases. Native Wu/tile ownership remains separate.
        vec2 derivatives=vec2(dFdx(strandSide),dFdy(strandSide));
        float gradient=max(length(derivatives),1e-6);
        float distancePixels=abs(strandSide)/gradient;
        vec2 normal=abs(derivatives)/gradient;
        float coverage=StrandPixelCdf(strandCoverage-distancePixels,normal)-
            StrandPixelCdf(-strandCoverage-distancePixels,normal);
        alpha=clamp(coverage*strandVolume.z,0.0,1.0);
    }
    if(alpha<.001)discard;
#ifdef STRAND_LAYERED_TRANSPARENCY
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    if(gl_FragCoord.z>texelFetch(strandOpaqueDepth,pixel,0).r)discard;
    // Depth32F retains the same raster depth; no fixed bias swallows nearby
    // fine strands. Equal-depth groups retain all their coverage deposits.
#ifdef STRAND_EQUAL_DEPTH_GROUP
    float selectedDepth=texelFetch(strandPreviousDepth,pixel,0).r;
    if(selectedDepth>=1.0||gl_FragCoord.z!=selectedDepth)discard;
#else
    if(strandViewport.z>.5&&gl_FragCoord.z<=texelFetch(strandPreviousDepth,pixel,0).r)discard;
#endif
#endif
)GLSL";
    if(revealage) source+=R"GLSL(
    outColor=vec4(-log(max(1.0-alpha,.0001)),0.0,0.0,0.0);
}
)GLSL";
    else source+=R"GLSL(
    vec3 base=mix(strandRootColor.rgb,strandTipColor.rgb,strandArc);
    if(strandColorWeights.w>.5&&all(greaterThanEqual(strandColorCoordinates,vec2(0.0))))
        base=StrandGradientColor(strandGradientSampler,strandColorCoordinates,strandColorWeights.xyz,strandColorOffsets,strandColorVariation.xyz);
    vec3 T=StrandSafe(strandWorldTangent,vec3(0.0,-1.0,0.0));
    vec3 V=StrandSafe(strandShutterCamera-strandViewOrigin,vec3(0.0,0.0,1.0));
    vec4 paths=vec4(0.0),extraPaths=vec4(0.0),directVisibility=vec4(1.0),extraVisibility=vec4(1.0);vec3 negativePaths=vec3(0.0);
    if(fiberAtlas.w>.5&&!StrandNativeDsEnabled()) {
        vec3 localPosition=(fiberInverseModel*vec4(strandWorldPosition,1.0)).xyz;
        paths=FiberVolumeSample(strandOpticalPaths,localPosition);
        directVisibility=FiberOpaqueDirectVisibility(localPosition);
        extraVisibility=FiberOpaqueExtraVisibility(localPosition);
        extraPaths=FiberVolumeSample(strandOpticalPathsExtra,localPosition);
        if(fiberTrace.w>.5)negativePaths=FiberVolumeSample(strandAmbientOpticalPaths,localPosition).rgb;
    }
    // The dual-direction closure consumes N=0 outside its source-volume mode;
    // six-axis legacy paths must not be mislabeled as L+/L- diagnostics.
    vec2 consumedIblCounts=fiberAtlas.w>.5&&fiberTrace.w>1.5?negativePaths.rg:vec2(0.0);
    vec2 consumedIblVisibility=extraVisibility.gb;
    StrandConsumedIblPaths(T,V,consumedIblCounts,consumedIblVisibility);
    vec3 ambient=max(strandAmbient.rgb,vec3(0.0));
    vec3 color=base*ambient;
    if(strandScattering.x>.5) {
        if(strandIbl.y>.5&&strandIbl.z>.5)color=StrandSourceIbl(base,T,V,consumedIblCounts,consumedIblVisibility);
        else {
            ambient+=StrandSkyDiffuse(T);
            if(fiberAtlas.w>.5&&fiberTrace.w>.5) {
                vec3 localT=StrandSafe(mat3(fiberInverseModel)*T,vec3(0.0,1.0,0.0));
                if(StrandNativeDsEnabled())ambient*=StrandNativeDsForwardEnvironment(base,localT,
                    (dsWorldToLocal*vec4(strandWorldPosition,1.0)).xyz);
                else ambient*=FiberEnvironmentTransmission(base,localT,strandFiber,extraPaths.yzw,negativePaths,strandScattering.z);
            }
            ambient*=strandRaster.w;
            color=mix(ambient,vec3(dot(ambient,vec3(.2126,.7152,.0722)))*base,strandScattering.w);
        }
    }
    // nativeMoments.y is a reserved scalar in the existing material prefix.
    // Beauty retains its original color arithmetic; diagnostics neither
    // change coverage nor substitute a different lighting environment.
    int diagnostic=int(strandNativeMoments.y+.5);
    vec3 indirectColor=color,directColor=vec3(0.0),ballisticDirect=vec3(0.0),ballisticIndirect=vec3(0.0);
    bool ballisticDiagnostic=diagnostic>=4&&diagnostic<=10;
    vec3 diagnosticMask=diagnostic==8?vec3(1.0,0.0,0.0):
        (diagnostic==9?vec3(0.0,1.0,0.0):(diagnostic==10?vec3(0.0,0.0,1.0):vec3(1.0)));
    if(ballisticDiagnostic) {
        if(strandScattering.x>.5&&strandIbl.y>.5&&strandIbl.z>.5)
            ballisticIndirect=StrandDebugBallisticIbl(base,T,V,consumedIblCounts,consumedIblVisibility,diagnosticMask);
        // The legacy indirect adapter has no dual-scattering split. Treat
        // its entire output as ballistic only in the all-lobes views.
        else if(diagnostic<8)ballisticIndirect=indirectColor;
    }
    float sunVisibility=directVisibility.x;
    if(strandSunDirection.w>0.0) {
        vec3 L=StrandSafe(-strandSunDirection.xyz,vec3(0.0,1.0,0.0));
        if(StrandNativeDsEnabled()) {
            vec3 localP=(dsWorldToLocal*vec4(strandWorldPosition,1.0)).xyz;
            vec2 path=StrandNativeDsPath(localP,mat3(dsWorldToLocal)*L,1e6);
            paths.x=path.x;directVisibility.x=path.y;
        }
        // A cylinder's lit radial normal is independent of the camera-facing
        // ribbon width. Reuse the exact opaque PBR CSM sampling implementation.
        vec3 radialNormal=StrandSafe(L-T*dot(T,L),StrandSafe(cross(T,V),vec3(1.0,0.0,0.0)));
        float rawOpaqueVisibility=strandShadows.x>.5?DirectionalVisibility(strandWorldPosition,radialNormal,shadowDx,shadowDy,strandSourceShadowAngle):1.0;
        float opaqueVisibility=StrandOpaqueShadowVisibility(rawOpaqueVisibility,strandOpaqueShadowParameters.x);
        color+=StrandExternallyShadowedLight(base,T,V,L,max(strandSunColor.rgb,vec3(0.0))*strandSunDirection.w,
            paths.x,directVisibility.x,opaqueVisibility);
        if(diagnostic>0) {
            sunVisibility=opaqueVisibility*directVisibility.x;
            directColor+=StrandExternallyShadowedLight(base,T,V,L,max(strandSunColor.rgb,vec3(0.0))*strandSunDirection.w,
                paths.x,directVisibility.x,opaqueVisibility);
            if(ballisticDiagnostic)ballisticDirect+=StrandDebugExternallyShadowedBallisticLight(base,T,V,L,
                max(strandSunColor.rgb,vec3(0.0))*strandSunDirection.w,paths.x,directVisibility.x,opaqueVisibility,diagnosticMask);
        }
    }
#ifdef STRAND_LOCAL_DIRECT_PATHS
    if(strandSourceDirectOptions.x>.5) {
        vec3 localP=(fiberInverseModel*vec4(strandWorldPosition,1.0)).xyz;
        for(int i=0;i<min(int(strandSourceDirectOptions.y+.5),64);++i) {
            vec3 L,radiance;if(!StrandSourceDirectSample(i,strandWorldPosition,L,radiance))continue;
            vec3 localDelta=mat3(fiberInverseModel)*(strandSourceLights[i].positionRange.xyz-strandWorldPosition);
            vec2 path=FiberPointDirectPath(localP,localDelta,length(localDelta),fiberAtlas.w>.5);
            color+=StrandLight(base,T,V,L,radiance,path.x,path.y);
            if(diagnostic>0) {
                directColor+=StrandLight(base,T,V,L,radiance,path.x,path.y);
                if(ballisticDiagnostic)ballisticDirect+=StrandDebugBallisticLight(base,T,V,L,
                    radiance,path.x,path.y,diagnosticMask);
            }
        }
    }else
#endif
    for(int i=0;i<4;++i) {
        vec3 toLight=pointPositions[i].xyz-strandWorldPosition;
        float distanceSquared=max(dot(toLight,toLight),.0001);
        vec3 radiance=max(pointColors[i].rgb,vec3(0.0))/max(distanceSquared,pointColors[i].w*pointColors[i].w);
        if(StrandNativeDsEnabled()&&any(greaterThan(radiance,vec3(0.0)))) {
            vec3 localP=(dsWorldToLocal*vec4(strandWorldPosition,1.0)).xyz;
            vec3 delta=mat3(dsWorldToLocal)*toLight;
            vec2 path=StrandNativeDsPath(localP,delta,length(delta));
            if(i<3){paths[i+1]=path.x;directVisibility[i+1]=path.y;}
            else{extraPaths.x=path.x;extraVisibility.x=path.y;}
        }
        color+=StrandLight(base,T,V,StrandSafe(toLight,vec3(0.0,1.0,0.0)),radiance,i<3?paths[i+1]:extraPaths.x,i<3?directVisibility[i+1]:extraVisibility.x);
        if(diagnostic>0) {
            directColor+=StrandLight(base,T,V,StrandSafe(toLight,vec3(0.0,1.0,0.0)),radiance,
                i<3?paths[i+1]:extraPaths.x,i<3?directVisibility[i+1]:extraVisibility.x);
            if(ballisticDiagnostic)ballisticDirect+=StrandDebugBallisticLight(base,T,V,
                StrandSafe(toLight,vec3(0.0,1.0,0.0)),radiance,i<3?paths[i+1]:extraPaths.x,
                i<3?directVisibility[i+1]:extraVisibility.x,diagnosticMask);
        }
    }
    if(strandRaster.z>.5)color=base;
    if(diagnostic==1)color=base;
    else if(diagnostic==2)color=directColor;
    else if(diagnostic==3)color=indirectColor;
    else if(diagnostic==4)color=ballisticDirect;
    else if(diagnostic==5)color=max(directColor-ballisticDirect,vec3(0.0));
    else if(diagnostic==6)color=ballisticIndirect;
    else if(diagnostic==7)color=max(indirectColor-ballisticIndirect,vec3(0.0));
    else if(diagnostic>=8&&diagnostic<=10)color=ballisticDirect+ballisticIndirect;
    else if(diagnostic==11)color=vec3(StrandDebugCount(paths.x));
    else if(diagnostic==12)color=vec3(StrandDebugCount(consumedIblCounts.r),StrandDebugCount(consumedIblCounts.g),0.0);
    else if(diagnostic==13)color=clamp(vec3(sunVisibility,consumedIblVisibility.r,consumedIblVisibility.g),vec3(0.0),vec3(1.0));
    else if(diagnostic==14)color=StrandDebugIndirectIncident(base,T,V);
    color=clamp(color,vec3(0.0),vec3(1000.0));
#ifdef STRAND_PEEL_LAYER
    outColor=vec4(color,alpha);
#else
    // Bounded weighted blended transparency. The separate logarithmic reveal
    // sum gives an order-independent product of (1-alpha), with one RHI color
    // attachment per pass and no native blending calls outside the backends.
    float weight=clamp(alpha*4.0+.01,.01,4.0)*clamp(.05+10.0*pow(1.0-gl_FragCoord.z,2.0),.05,1.0);
    outColor=vec4(color*alpha*weight,alpha*weight);
#endif
}
)GLSL";
    if(!revealage&&diagnostics)source.insert(source.find("void main()"),StrandDebugGLSL());
    if(mode!=0)source.insert(source.find('\n')+1,"#define STRAND_LAYERED_TRANSPARENCY 1\n");
    if(mode==1)source.insert(source.find('\n')+1,"#define STRAND_PEEL_LAYER 1\n");
    if(mode==3)source.insert(source.find('\n')+1,"#define STRAND_EQUAL_DEPTH_GROUP 1\n");
    if(pointPaths)source.insert(source.find('\n')+1,"#define STRAND_LOCAL_DIRECT_PATHS 1\n");
    return StrandColorFeatures(std::move(source),diagnostics,pointPaths);
}
inline std::string StrandCompositeFragmentShader(bool frontLayers=false) {
    std::string source=R"GLSL(#version 450 core
layout(binding=0) uniform sampler2D strandAccumulation;
layout(binding=1) uniform sampler2D strandRevealage;
#ifdef STRAND_FRONT_LAYERS
layout(binding=2) uniform sampler2D strandFrontLayer0;
layout(binding=3) uniform sampler2D strandFrontLayer1;
#endif
in vec2 vUV;
layout(location=0) out vec4 outColor;
void main() {
    vec4 accum=texture(strandAccumulation,vUV);
    float opticalDepth=max(texture(strandRevealage,vUV).r,0.0);
    float alpha=1.0-exp(-min(opticalDepth,80.0));
    vec3 color=accum.a>.00001?accum.rgb/accum.a:vec3(0.0);
#ifdef STRAND_FRONT_LAYERS
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    vec4 front0=texelFetch(strandFrontLayer0,pixel,0);
    vec4 front1=texelFetch(strandFrontLayer1,pixel,0);
    float a0=clamp(front0.a,0.0,1.0),a1=clamp(front1.a,0.0,1.0);
    float t0=1.0-a0,t1=1.0-a1;
    vec3 premultiplied=front0.rgb*a0+t0*(front1.rgb*a1+t1*color*alpha);
    alpha=clamp(1.0-t0*t1*(1.0-alpha),0.0,1.0);
    // The existing AlphaComposite RHI mode takes straight RGB. Convert once
    // at this boundary so it reconstructs premultiplied over without alpha².
    color=alpha>0.0?premultiplied/alpha:vec3(0.0);
#endif
    outColor=vec4(clamp(color,vec3(0.0),vec3(60000.0)),clamp(alpha,0.0,1.0));
}
)GLSL";
    if(frontLayers)source.insert(source.find('\n')+1,"#define STRAND_FRONT_LAYERS 1\n");
    return source;
}
} // namespace Render::PipelineDetail
