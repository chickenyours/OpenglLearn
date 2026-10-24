#pragma once
#include <string>
#include "Render/Private/Pipeline/fiber_volume_exact_shaders.h"
namespace Render::PipelineDetail {
inline std::string FiberVolumeBlockGLSL() { return R"GLSL(
layout(std140,binding=5) uniform FiberVolume {
    mat4 fiberInverseModel;
    vec4 fiberGridMinStep; // local min.xyz, uniform voxel edge in metres
    vec4 fiberGridSize;    // xyz resolution, atlas tile columns
    vec4 fiberAtlas;       // atlas width,height,physical width scale,enabled
    vec4 fiberTrace;       // step growth,bias in voxels,pass,sky mode:0 off/1 axes/2 source
    vec4 fiberSun;         // local direction toward sun
    vec4 fiberPoints[4];   // local light positions
    vec4 fiberCameraLocal; // xyz local camera; w legacy far-start bias in voxels
    mat4 fiberModel;
    mat4 fiberViewProjection;
    mat4 fiberInverseViewProjection;
    vec4 fiberCameraForward;
    vec4 fiberOpaque; // enabled, shell thickness, blocker trace pass, linear atlas filter
    vec4 fiberQuality; // exact integration, screen shell, proxy enabled, capsule count
    vec4 fiberOpaqueCapsules[32]; // local startRadius/endRadius pairs, collision proxy not mesh
};
layout(binding=10) uniform sampler2D fiberDirectVisibilityTexture;
layout(binding=11) uniform sampler2D fiberExtraVisibilityTexture;
ivec2 FiberAtlasPixel(ivec3 p) {
    int columns=int(fiberGridSize.w);
    return ivec2((p.z%columns)*int(fiberGridSize.x)+p.x,(p.z/columns)*int(fiberGridSize.y)+p.y);
}
ivec3 FiberAtlasVoxel() {
    ivec2 p=ivec2(gl_FragCoord.xy),size=ivec2(fiberGridSize.xy);
    return ivec3(p%size,(p.x/size.x)+(p.y/size.y)*int(fiberGridSize.w));
}
vec4 FiberVolumeSampleLegacy(sampler2D image,vec3 p,vec4 outsideValue) {
    ivec3 size=ivec3(fiberGridSize.xyz),a=ivec3(floor(p));vec3 f=fract(p);
    vec4 result=outsideValue;
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
        ivec3 q=a+ivec3(x,y,z);
        if(any(lessThan(q,ivec3(0)))||any(greaterThanEqual(q,size)))continue;
        vec3 w=mix(vec3(1.0)-f,f,vec3(x,y,z));
        result+=(texelFetch(image,FiberAtlasPixel(q),0)-outsideValue)*(w.x*w.y*w.z);
    }
    return result;
}
vec4 FiberVolumeSampleLinear(sampler2D image,vec3 p,vec4 outsideValue) {
    vec3 size=fiberGridSize.xyz;
    // The legacy trilinear reconstruction has support [-1,size] in voxel
    // center coordinates. Clamp XY to this slice's texel centers so hardware
    // bilinear filtering cannot read an adjacent atlas tile. The separate
    // support factor restores the original constant border, including corners.
    vec2 support=clamp(p.xy+vec2(1.0),vec2(0.0),vec2(1.0))*
        clamp(size.xy-p.xy,vec2(0.0),vec2(1.0));
    if(support.x<=0.0||support.y<=0.0||p.z<=-1.0||p.z>=size.z)return outsideValue;
    vec2 xy=clamp(p.xy,vec2(0.0),size.xy-vec2(1.0));
    int first=int(floor(p.z));float zWeight=fract(p.z);
    vec4 result=outsideValue;
    for(int side=0;side<2;++side) {
        int slice=first+side;
        if(slice<0||slice>=int(size.z))continue;
        vec2 tile=vec2(FiberAtlasPixel(ivec3(0,0,slice)));
        vec2 uv=(tile+xy+vec2(.5))/fiberAtlas.xy;
        float weight=support.x*support.y*(side==0?1.0-zWeight:zWeight);
        // RHI atlases are Linear-filtered, non-mipped: density is RGBA32F,
        // optical paths and blockers are RGBA16F. Explicit
        // LOD zero avoids derivative-dependent selection in divergent traces.
        // This factors the same eight weights into two bilinear samples;
        // hardware interpolation precision need not be bit-identical.
        result+=(textureLod(image,uv,0.0)-outsideValue)*weight;
    }
    return result;
}
vec4 FiberVolumeSample(sampler2D image,vec3 localPosition) {
    vec3 p=(localPosition-fiberGridMinStep.xyz)/fiberGridMinStep.w-.5;
    return fiberOpaque.w>.5?FiberVolumeSampleLinear(image,p,vec4(0.0)):
        FiberVolumeSampleLegacy(image,p,vec4(0.0));
}
vec4 FiberVolumeVisibilitySample(sampler2D image,vec3 localPosition) {
    vec3 p=(localPosition-fiberGridMinStep.xyz)/fiberGridMinStep.w-.5;
    // Outside the finite blocker grid remains unoccluded, unlike density.
    vec4 result=fiberOpaque.w>.5?FiberVolumeSampleLinear(image,p,vec4(1.0)):
        FiberVolumeSampleLegacy(image,p,vec4(1.0));
    return clamp(result,0.0,1.0);
}
vec4 FiberOpaqueDirectVisibility(vec3 localPosition) {
    return fiberOpaque.x>.5?FiberVolumeVisibilitySample(fiberDirectVisibilityTexture,localPosition):vec4(1.0);
}
vec4 FiberOpaqueExtraVisibility(vec3 localPosition) {
    return fiberOpaque.x>.5?FiberVolumeVisibilitySample(fiberExtraVisibilityTexture,localPosition):vec4(1.0);
}
)GLSL"; }
inline std::string FiberVolumeVertexShader() { return R"GLSL(#version 450 core
layout(location=0) in vec4 positionRadius;
layout(location=0) out vec4 segmentPoint;
void main(){segmentPoint=positionRadius;gl_Position=vec4(positionRadius.xyz,1.0);}
)GLSL"; }
inline std::string FiberVolumeGeometryShader() {
    // gl_FragCoord is fragment-only: the GS needs only the uniform block.
    auto block=FiberVolumeBlockGLSL();block.erase(block.find("ivec2 FiberAtlasPixel"));
    return std::string(R"GLSL(#version 450 core
layout(triangles,invocations=8) in;
layout(triangle_strip,max_vertices=64) out;
layout(location=0) in vec4 segmentPoint[];
layout(location=0) flat out vec4 fiberA;
layout(location=1) flat out vec4 fiberB;
)GLSL")+block+R"GLSL(
void FiberCorner(vec2 p) {
    gl_Position=vec4(p/fiberAtlas.xy*2.0-1.0,0.0,1.0);EmitVertex();
}
void main() {
    // Each line becomes two ribbon triangles. Count the physical line once.
    if((gl_PrimitiveIDIn&1)!=0)return;
    vec4 a=segmentPoint[0],b=segmentPoint[2];
    float radius=max(abs(a.w),abs(b.w))*fiberAtlas.z;
    vec3 low=(min(a.xyz,b.xyz)-radius-fiberGridMinStep.xyz)/fiberGridMinStep.w;
    vec3 high=(max(a.xyz,b.xyz)+radius-fiberGridMinStep.xyz)/fiberGridMinStep.w;
    ivec3 first=max(ivec3(floor(low)),ivec3(0));
    ivec3 last=min(ivec3(floor(high)),ivec3(fiberGridSize.xyz)-ivec3(1));
    // Eight independent GS invocations cover at most sixteen slices each at
    // the 128-cell budget. Four corners per slice stay within max_vertices=64
    // even for a single physical segment spanning the complete volume.
    for(int z=int(gl_InvocationID);z<int(fiberGridSize.z);z+=8) {
        if(z<first.z||z>last.z)continue;
        vec2 tile=vec2(z%int(fiberGridSize.w),z/int(fiberGridSize.w))*fiberGridSize.xy;
        vec2 p0=tile+vec2(first.xy),p1=tile+vec2(last.xy+ivec2(1));
        fiberA=vec4(a.xyz,abs(a.w));fiberB=vec4(b.xyz,abs(b.w));FiberCorner(p0);
        fiberA=vec4(a.xyz,abs(a.w));fiberB=vec4(b.xyz,abs(b.w));FiberCorner(vec2(p1.x,p0.y));
        fiberA=vec4(a.xyz,abs(a.w));fiberB=vec4(b.xyz,abs(b.w));FiberCorner(vec2(p0.x,p1.y));
        fiberA=vec4(a.xyz,abs(a.w));fiberB=vec4(b.xyz,abs(b.w));FiberCorner(p1);EndPrimitive();
    }
}
)GLSL";
}
inline std::string FiberVolumeDensityShader() {
    return std::string(R"GLSL(#version 450 core
layout(location=0) flat in vec4 fiberA;
layout(location=1) flat in vec4 fiberB;
layout(location=0) out vec4 result;
)GLSL")+FiberVolumeBlockGLSL()+R"GLSL(
void main() {
    ivec3 voxel=FiberAtlasVoxel();if(voxel.z>=int(fiberGridSize.z))discard;
    float h=fiberGridMinStep.w;
    vec3 low=fiberGridMinStep.xyz+vec3(voxel)*h,high=low+vec3(h);
    vec3 d=fiberB.xyz-fiberA.xyz;float lengthSegment=length(d);
    if(lengthSegment<1e-10)discard;
    // Segment/cell slab intersection: total injected length is conserved and
    // independent of minimum raster footprint. Parallel-on-boundary segments
    // use half-open cells, so they never inject their full length twice.
    float enter=0.0,leave=1.0;
    for(int i=0;i<3;++i) {
        if(abs(d[i])<1e-10) { if(fiberA[i]<low[i]||fiberA[i]>=high[i])discard; }
        else { float a=(low[i]-fiberA[i])/d[i],b=(high[i]-fiberA[i])/d[i];enter=max(enter,min(a,b));leave=min(leave,max(a,b)); }
    }
    if(leave<=enter)discard;
    float r=mix(fiberA.w,fiberB.w,clamp(.5*(enter+leave),0.0,1.0));
    // length[m]*diameter[m]/cell volume[m^3] = extinction[1/m].
    // This host conserved geometric density integrates to dimensionless N.
    // The source instead adds width/minVoxel once per visited segment/cell;
    // it is a different voxel-occupancy estimator, not a per-cell equivalence.
    float extinction=lengthSegment*(leave-enter)*(2.0*r*fiberAtlas.z)/(h*h*h);
    float bounded=min(extinction,60000.0);
    // Signed root-to-tip first moment. Additive accumulation yields the
    // extinction-weighted tangent in each voxel without another resource.
    result=vec4(bounded,bounded*(d/lengthSegment));
}
)GLSL";
}
inline std::string FiberVolumePathsShader() {
    return std::string(R"GLSL(#version 450 core
layout(binding=12) uniform sampler2D fiberDensity;
layout(location=0) out vec4 result;
)GLSL")+FiberVolumeBlockGLSL()+FiberVolumeExactGLSL()+R"GLSL(
vec3 FiberPathSafe(vec3 value,vec3 fallbackValue) {
    float lengthSquared=dot(value,value);
    return lengthSquared>1e-12?value*inversesqrt(lengthSquared):fallbackValue;
}
vec4 FiberDensityCell(ivec3 voxel) {
    if(any(lessThan(voxel,ivec3(0)))||any(greaterThanEqual(voxel,ivec3(fiberGridSize.xyz))))return vec4(0.0);
    return texelFetch(fiberDensity,FiberAtlasPixel(voxel),0);
}
vec3 FiberDensityTangent(ivec3 voxel) {
    vec4 cell=FiberDensityCell(voxel);
    vec3 mean=cell.gba/max(cell.r,1e-8);
    if(dot(mean,mean)>1e-8)return normalize(mean);
    // Empty or opposing-strand cancellation: use six neighboring weighted
    // moments. A strongest-neighbor fallback avoids cancelling them again.
    vec4 sum=vec4(0.0),strongest=cell;
    for(int axis=0;axis<3;++axis)for(int side=-1;side<=1;side+=2) {
        ivec3 offset=ivec3(0);offset[axis]=side;
        vec4 neighbor=FiberDensityCell(voxel+offset);sum+=neighbor;
        if(neighbor.r>strongest.r)strongest=neighbor;
    }
    mean=sum.gba/max(sum.r,1e-8);
    if(dot(mean,mean)>1e-8)return normalize(mean);
    mean=strongest.gba/max(strongest.r,1e-8);
    return FiberPathSafe(mean,vec3(0.0,1.0,0.0));
}
float FiberTracePath(vec3 p,vec3 direction,float maxDistance) {
    if(fiberQuality.x>.5)return FiberTraceTrilinearExact(fiberDensity,p,direction,maxDistance,false);
    float norm=length(direction);if(norm<1e-8)return 0.0;vec3 ray=direction/norm;
    vec3 low=fiberGridMinStep.xyz,high=low+fiberGridSize.xyz*fiberGridMinStep.w;
    float endDistance=maxDistance;
    for(int i=0;i<3;++i)if(abs(ray[i])>1e-8)
        endDistance=min(endDistance,((ray[i]>0.0?high[i]:low[i])-p[i])/ray[i]);
    float distance=fiberGridMinStep.w*max(fiberTrace.y,0.0),count=0.0;
    float farStart=fiberGridMinStep.w*max(max(fiberTrace.y,fiberCameraLocal.w),0.0);
    float nearEnd=min(farStart,endDistance);
    // Keep the far sample phase exactly as before. Advancing the whole march
    // from a new start can miss a different distant density peak. Add only the
    // nonnegative interval that the coarse-grid bias used to exclude.
    for(int i=0;i<32;++i){
        if(distance>=nearEnd)break;
        float ds=i==31?nearEnd-distance:min(fiberGridMinStep.w*.25,nearEnd-distance);
        count+=max(FiberVolumeSample(fiberDensity,p+ray*(distance+.5*ds)).r,0.0)*ds;
        if(count>=80.0)return 80.0;
        distance+=ds;
    }
    distance=farStart;
    float step=fiberGridMinStep.w*.5;
    for(int i=0;i<96;++i) {
        if(distance>=endDistance)break;
        float ds=min(step,endDistance-distance);
        // density[1/m] * ds[m] = dimensionless fiber encounters.
        count+=max(FiberVolumeSample(fiberDensity,p+ray*(distance+.5*ds)).r,0.0)*ds;
        // Every remaining increment is nonnegative, so the final min(N,80)
        // is already fixed. This changes neither the budget nor the step law.
        if(count>=80.0)return 80.0;
        distance+=ds;step*=fiberTrace.x;
    }
    return min(count,80.0);
}
void main() {
    ivec3 voxel=FiberAtlasVoxel();if(voxel.z>=int(fiberGridSize.z)){result=vec4(0.0);return;}
    vec3 p=fiberGridMinStep.xyz+(vec3(voxel)+.5)*fiberGridMinStep.w;
    if(fiberTrace.z<.5) {
        result.x=fiberSun.w>.5?FiberTracePath(p,fiberSun.xyz,1e6):0.0;
        for(int i=0;i<3;++i){vec3 d=fiberPoints[i].xyz-p;result[i+1]=fiberPoints[i].w>.5?FiberTracePath(p,d,length(d)):0.0;}
    } else if(fiberTrace.z<1.5) {
        vec3 d=fiberPoints[3].xyz-p;result=vec4(fiberPoints[3].w>.5?FiberTracePath(p,d,length(d)):0.0,0.0,0.0,0.0);
        // Six independent direction counts. Average transmitted radiance in
        // the fiber shader, never N before its nonlinear exponential. This is
        // host quadrature of the precomputed Sky SH, not the
        // original engine's directional/variance DSVolume IBL integration.
        if(fiberTrace.w>.5&&fiberTrace.w<1.5)for(int i=0;i<3;++i) {
            vec3 axis=vec3(0.0);axis[i]=1.0;
            result[i+1]=FiberTracePath(p,axis,1e6);
        }
    } else {
        result=vec4(0.0);
        if(fiberTrace.w>1.5) {
            vec3 T=FiberDensityTangent(voxel);
            vec3 V=FiberPathSafe(fiberCameraLocal.xyz-p,vec3(0.0,0.0,1.0));
            // This radial projection has the same cross(T,V) in the ordinary
            // case, and exactly matches the fiber shader's degenerate basis.
            vec3 radial=FiberPathSafe(V-T*dot(T,V),vec3(0.0,0.0,1.0));
            vec3 B=FiberPathSafe(cross(T,radial),vec3(1.0,0.0,0.0));
            vec3 C=FiberPathSafe(cross(B,T),radial);
            vec3 positive=FiberPathSafe(C+B,C),negative=FiberPathSafe(C-B,C);
            // Trace the source's two oblique directions individually. No
            // interpolation of unrelated ±XYZ depths before exponentiation.
            result.r=FiberTracePath(p,positive,1e6);
            result.g=FiberTracePath(p,negative,1e6);
        } else if(fiberTrace.w>.5) {
            for(int i=0;i<3;++i){vec3 axis=vec3(0.0);axis[i]=-1.0;result[i]=FiberTracePath(p,axis,1e6);}
        }
    }
}
)GLSL";
}
// Source rasterDepthDSVolume injects only a screen-visible, behind-surface shell
// selected by camera-forward distance. It is not a closed opaque-object SDF.
inline std::string FiberVolumeOpaqueShellShader() {
    return std::string(R"GLSL(#version 450 core
layout(binding=12) uniform sampler2D fiberOpaqueDepth;
layout(location=0) out vec4 result;
)GLSL")+FiberVolumeBlockGLSL()+R"GLSL(
float FiberCapsuleMask(vec3 p) {
    if(fiberQuality.z<.5)return 0.0;
    for(int i=0;i<16;++i) {
        if(i>=int(fiberQuality.w))break;
        vec4 a=fiberOpaqueCapsules[2*i],b=fiberOpaqueCapsules[2*i+1];
        if(max(a.w,b.w)<=0.0)continue;
        vec3 axis=b.xyz-a.xyz,relative=p-a.xyz;float lengthSquared=dot(axis,axis);
        if(lengthSquared<1e-16) {
            if(length(relative)<=max(a.w,b.w))return 1.0;
            continue;
        }
        float axisLength=sqrt(lengthSquared),slope=(b.w-a.w)/axisLength;
        float parameter;
        // The larger endpoint sphere contains the entire sweep in this case.
        if(abs(slope)>=1.0)parameter=b.w>a.w?1.0:0.0;
        else {
            vec3 direction=axis/axisLength;
            float along=dot(relative,direction),radial=length(relative-along*direction);
            // Closest sphere of a varying-radius sweep. Plain projection is
            // correct only for equal radii and misses the tapered side wall.
            parameter=clamp((along+slope*radial/sqrt(max(1.0-slope*slope,1e-12)))/axisLength,0.0,1.0);
        }
        if(length(p-mix(a.xyz,b.xyz,parameter))<=mix(a.w,b.w,parameter))return 1.0;
    }
    return 0.0;
}
void main() {
    result=vec4(0.0);
    ivec3 voxel=FiberAtlasVoxel();if(voxel.z>=int(fiberGridSize.z))return;
    vec3 p=fiberGridMinStep.xyz+(vec3(voxel)+.5)*fiberGridMinStep.w;
    // A bounded local collision proxy supplements the observed screen shell.
    // It is deliberately independent of camera depth and never changes R/N.
    result.r=FiberCapsuleMask(p);
    if(result.r>=1.0||fiberQuality.y<.5)return;
    vec4 world=fiberModel*vec4(p,1.0);
    vec4 clip=fiberViewProjection*world;
    if(clip.w<=1e-8)return;
    vec3 ndc=clip.xyz/clip.w;
    vec2 uv=ndc.xy*.5+.5;
    if(any(lessThan(uv,vec2(0.0)))||any(greaterThanEqual(uv,vec2(1.0))))return;
    float projectedDepth=ndc.z*.5+.5;
    if(projectedDepth<0.0||projectedDepth>1.0)return;
    ivec2 pixel=ivec2(uv*vec2(textureSize(fiberOpaqueDepth,0)));
    float depth=texelFetch(fiberOpaqueDepth,pixel,0).r;
    // Native reverse-depth surface>voxel becomes surface<voxel in this RHI.
    if(depth>=1.0||depth>=projectedDepth)return;
    vec4 surface=fiberInverseViewProjection*vec4(ndc.xy,depth*2.0-1.0,1.0);
    if(abs(surface.w)<1e-8)return;
    float distanceToSurface=abs(dot(fiberCameraForward.xyz,surface.xyz/surface.w-world.xyz));
    result.r=max(result.r,distanceToSurface<fiberOpaque.y?1.0:0.0);
}
)GLSL";
}
inline std::string FiberVolumeOpaquePathsShader() {
    return std::string(R"GLSL(#version 450 core
layout(binding=12) uniform sampler2D fiberDensity;
layout(binding=13) uniform sampler2D fiberOpaqueMask;
layout(location=0) out vec4 result;
)GLSL")+FiberVolumeBlockGLSL()+FiberVolumeExactGLSL()+R"GLSL(
vec3 FiberOpaqueSafe(vec3 value,vec3 fallbackValue) {
    float n=dot(value,value);return n>1e-12?value*inversesqrt(n):fallbackValue;
}
vec4 FiberOpaqueDensityCell(ivec3 voxel) {
    if(any(lessThan(voxel,ivec3(0)))||any(greaterThanEqual(voxel,ivec3(fiberGridSize.xyz))))return vec4(0.0);
    return texelFetch(fiberDensity,FiberAtlasPixel(voxel),0);
}
vec3 FiberOpaqueTangent(ivec3 voxel) {
    vec4 cell=FiberOpaqueDensityCell(voxel);
    vec3 mean=cell.gba/max(cell.r,1e-8);
    if(dot(mean,mean)>1e-8)return normalize(mean);
    vec4 sum=vec4(0.0),strongest=cell;
    for(int axis=0;axis<3;++axis)for(int side=-1;side<=1;side+=2) {
        ivec3 offset=ivec3(0);offset[axis]=side;
        vec4 neighbor=FiberOpaqueDensityCell(voxel+offset);sum+=neighbor;
        if(neighbor.r>strongest.r)strongest=neighbor;
    }
    mean=sum.gba/max(sum.r,1e-8);
    if(dot(mean,mean)>1e-8)return normalize(mean);
    return FiberOpaqueSafe(strongest.gba/max(strongest.r,1e-8),vec3(0.0,1.0,0.0));
}
float FiberTraceOpaqueVisibility(vec3 p,vec3 direction,float maxDistance) {
    if(fiberQuality.x>.5)return 1.0-FiberTraceTrilinearExact(fiberOpaqueMask,p,direction,maxDistance,true);
    float norm=length(direction);if(norm<1e-8)return 1.0;
    vec3 ray=direction/norm,low=fiberGridMinStep.xyz,high=low+fiberGridSize.xyz*fiberGridMinStep.w;
    float endDistance=maxDistance;
    for(int axis=0;axis<3;++axis)if(abs(ray[axis])>1e-8)
        endDistance=min(endDistance,((ray[axis]>0.0?high[axis]:low[axis])-p[axis])/ray[axis]);
    float distance=fiberGridMinStep.w*max(fiberTrace.y,0.0),blocker=0.0;
    float farStart=fiberGridMinStep.w*max(max(fiberTrace.y,fiberCameraLocal.w),0.0);
    float nearEnd=min(farStart,endDistance);
    for(int i=0;i<32;++i){
        if(distance>=nearEnd)break;
        float ds=i==31?nearEnd-distance:min(fiberGridMinStep.w*.25,nearEnd-distance);
        blocker=max(blocker,clamp(FiberVolumeSample(fiberOpaqueMask,p+ray*(distance+.5*ds)).r,0.0,1.0));
        if(blocker>=1.0)return 0.0;
        distance+=ds;
    }
    distance=farStart;
    float step=fiberGridMinStep.w*.5;
    for(int i=0;i<96;++i) {
        if(distance>=endDistance)break;
        float ds=min(step,endDistance-distance);
        // G is a visibility blocker, not extinction or an encounter count.
        blocker=max(blocker,clamp(FiberVolumeSample(fiberOpaqueMask,p+ray*(distance+.5*ds)).r,0.0,1.0));
        // max(G,remaining samples in [0,1]) cannot change a saturated blocker.
        if(blocker>=1.0)return 0.0;
        distance+=ds;step*=fiberTrace.x;
    }
    return 1.0-clamp(blocker,0.0,1.0);
}
void main() {
    result=vec4(1.0);
    ivec3 voxel=FiberAtlasVoxel();if(voxel.z>=int(fiberGridSize.z))return;
    // The shell producer uses distance<threshold; a zero threshold therefore
    // writes no blockers anywhere. N and the mean tangent remain independent.
    if(fiberOpaque.y<=0.0&&fiberQuality.z<.5)return;
    vec3 p=fiberGridMinStep.xyz+(vec3(voxel)+.5)*fiberGridMinStep.w;
    if(fiberOpaque.z<.5) {
        if(fiberSun.w>.5)result.x=FiberTraceOpaqueVisibility(p,fiberSun.xyz,1e6);
        for(int i=0;i<3;++i)if(fiberPoints[i].w>.5) {
            vec3 d=fiberPoints[i].xyz-p;result[i+1]=FiberTraceOpaqueVisibility(p,d,length(d));
        }
    } else {
        if(fiberPoints[3].w>.5) {
            vec3 d=fiberPoints[3].xyz-p;result.x=FiberTraceOpaqueVisibility(p,d,length(d));
        }
        if(fiberTrace.w>1.5) {
            vec3 T=FiberOpaqueTangent(voxel),V=FiberOpaqueSafe(fiberCameraLocal.xyz-p,vec3(0.0,0.0,1.0));
            vec3 radial=FiberOpaqueSafe(V-T*dot(T,V),vec3(0.0,0.0,1.0));
            vec3 B=FiberOpaqueSafe(cross(T,radial),vec3(1.0,0.0,0.0));
            vec3 C=FiberOpaqueSafe(cross(B,T),radial);
            result.y=FiberTraceOpaqueVisibility(p,FiberOpaqueSafe(C+B,C),1e6);
            result.z=FiberTraceOpaqueVisibility(p,FiberOpaqueSafe(C-B,C),1e6);
        }
    }
}
)GLSL";
}
} // namespace Render::PipelineDetail
