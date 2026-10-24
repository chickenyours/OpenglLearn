#pragma once
#include "Render/Private/Pipeline/fiber_volume_exact_shaders.h"
#include <string>

namespace Render::PipelineDetail {
// Optional actual-shading-point transport through the same reconstructed field.
// Append after FiberVolumeBlockGLSL(). Callers supply LOCAL P and inverse-model
// transformed LOCAL L; normalization below keeps integration in local metres,
// just like the volume producer. This removes cached voxel-center/mean-tangent
// ray substitution, not the missing native 3D mips/jitter/occupancy estimator.
inline std::string FiberPointPathsGLSL() {return FiberVolumeExactGLSL()+R"GLSL(
layout(binding=4) uniform sampler2D fiberPointDensity;
layout(binding=5) uniform sampler2D fiberPointOpaqueMask;

bool FiberPointPathsFinished(vec2 totals,bool blockers) {
    // N and G are independent: a saturated blocker cannot stop density.
    return totals.x>=80.0&&(!blockers||totals.y>=1.0);
}
void FiberPointPathsAccumulate(vec3 point,float distance,bool blockers,inout vec2 totals) {
    if(totals.x<80.0)totals.x=min(totals.x+max(FiberVolumeSample(fiberPointDensity,point).r,0.0)*distance,80.0);
    if(blockers&&totals.y<1.0)totals.y=max(totals.y,clamp(FiberVolumeSample(fiberPointOpaqueMask,point).r,0.0,1.0));
}
vec2 FiberPointPathsExact(vec3 position,vec3 ray,float entryDistance,float endDistance,bool blockers) {
    float h=fiberGridMinStep.w,distance=max(h*max(fiberTrace.y,0.0),entryDistance);
    if(distance>=endDistance)return vec2(0.0,1.0);
    vec3 q=(position-fiberGridMinStep.xyz)/h-.5,velocity=ray/h,start=q+velocity*distance;
    vec3 nextPlane=vec3(1e30),planeStep=vec3(1e30);
    for(int axis=0;axis<3;++axis)if(abs(velocity[axis])>1e-12){
        float knot=velocity[axis]>0.0?floor(start[axis])+1.0:ceil(start[axis])-1.0;
        nextPlane[axis]=distance+(knot-start[axis])/velocity[axis];planeStep[axis]=abs(1.0/velocity[axis]);
    }
    vec2 totals=vec2(0.0);
    for(int iteration=0;iteration<400;++iteration){
        if(distance>=endDistance)break;
        float finish=min(endDistance,min(nextPlane.x,min(nextPlane.y,nextPlane.z)));
        float ds=max(finish-distance,0.0);
        if(ds>0.0){
            ivec3 cell=ivec3(floor(q+velocity*(distance+.5*ds)));
            vec3 origin=q+velocity*distance-vec3(cell),delta=velocity*ds;
            if(totals.x<80.0){
                vec4 density=FiberCellRayCubic(fiberPointDensity,cell,origin,delta);
                totals.x=min(totals.x+max(dot(density,vec4(1.0,.5,1.0/3.0,.25))*ds,0.0),80.0);
            }
            if(blockers&&totals.y<1.0){
                vec4 mask=FiberCellRayCubic(fiberPointOpaqueMask,cell,origin,delta);
                totals.y=max(totals.y,clamp(FiberCubicMaximum(mask),0.0,1.0));
            }
            if(FiberPointPathsFinished(totals,blockers))break;
        }
        for(int axis=0;axis<3;++axis)if(nextPlane[axis]<=finish)nextPlane[axis]+=planeStep[axis];
        distance=max(distance,finish);
    }
    return vec2(totals.x,1.0-totals.y);
}
// Return (dimensionless fiber count N, opaque visibility 1-max(G)). Disabled
// paths return before ANY optional texture read; a disabled blocker never
// samples the all-one visibility fallback as if it were an occupied mask.
vec2 FiberPointIblPath(vec3 localPosition,vec3 localDirection,float maxDistance,bool enabled) {
    if(!enabled||fiberAtlas.w<.5||fiberTrace.w<=1.5)return vec2(0.0,1.0);
    float norm=length(localDirection);
    if(norm<1e-8||maxDistance<=0.0)return vec2(0.0,1.0);
    vec3 ray=localDirection/norm,low=fiberGridMinStep.xyz,high=low+fiberGridSize.xyz*fiberGridMinStep.w;
    float entryDistance=0.0,endDistance=maxDistance;
    for(int axis=0;axis<3;++axis){
        if(abs(ray[axis])>1e-8){
            float first=(low[axis]-localPosition[axis])/ray[axis],last=(high[axis]-localPosition[axis])/ray[axis];
            entryDistance=max(entryDistance,min(first,last));endDistance=min(endDistance,max(first,last));
        }
        else if(localPosition[axis]<low[axis]||localPosition[axis]>high[axis])return vec2(0.0,1.0);
    }
    if(entryDistance>=endDistance)return vec2(0.0,1.0);
    bool blockers=fiberOpaque.x>.5&&(fiberOpaque.y>0.0||fiberQuality.z>.5);
    if(fiberQuality.x>.5)return FiberPointPathsExact(localPosition,ray,entryDistance,endDistance,blockers);
    // A trailing shutter point may lie outside the frozen current grid. Skip
    // empty space up to entry without charging the finite cell/step budget.
    // Bias remains measured from the physical origin, never entry+bias.
    float h=fiberGridMinStep.w,distance=max(h*max(fiberTrace.y,0.0),entryDistance);
    float farStart=h*max(max(fiberTrace.y,fiberCameraLocal.w),0.0),nearEnd=min(farStart,endDistance);
    vec2 totals=vec2(0.0);
    for(int iteration=0;iteration<32;++iteration){
        if(distance>=nearEnd)break;
        float ds=iteration==31?nearEnd-distance:min(h*.25,nearEnd-distance);
        FiberPointPathsAccumulate(localPosition+ray*(distance+.5*ds),ds,blockers,totals);
        if(FiberPointPathsFinished(totals,blockers))return vec2(totals.x,1.0-totals.y);
        distance+=ds;
    }
    // Preserve the established far growth phase; near preservation is an
    // additive interval, never a translation of the distant sample schedule.
    distance=max(farStart,entryDistance);float step=h*.5;
    for(int iteration=0;iteration<96;++iteration){
        if(distance>=endDistance)break;
        float ds=min(step,endDistance-distance);
        FiberPointPathsAccumulate(localPosition+ray*(distance+.5*ds),ds,blockers,totals);
        if(FiberPointPathsFinished(totals,blockers))break;
        distance+=ds;step*=fiberTrace.x;
    }
    return vec2(totals.x,1.0-totals.y);
}
)GLSL";}
} // namespace Render::PipelineDetail
