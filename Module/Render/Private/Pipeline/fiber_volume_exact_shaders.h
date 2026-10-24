#pragma once
#include <string>
namespace Render::PipelineDetail {
// Optional host quadrature of the existing reconstructed trilinear field.
// This is not the original native 3D mip hierarchy or its stochastic march.
// Each ray interval between texel-center planes has a cubic density profile.
inline std::string FiberVolumeExactGLSL() {return R"GLSL(
float FiberCubicValue(vec4 c,float t) {
    return ((c.w*t+c.z)*t+c.y)*t+c.x;
}
float FiberCubicMaximum(vec4 c) {
    float result=max(c.x,FiberCubicValue(c,1.0));
    if(abs(c.w)<1e-8) {
        if(abs(c.z)>1e-8) {
            float root=-c.y/(2.0*c.z);
            if(root>0.0&&root<1.0)result=max(result,FiberCubicValue(c,root));
        }
    } else {
        float discriminant=c.z*c.z-3.0*c.w*c.y;
        if(discriminant>=0.0) {
            float d=sqrt(discriminant);
            float first=(-c.z-d)/(3.0*c.w),second=(-c.z+d)/(3.0*c.w);
            if(first>0.0&&first<1.0)result=max(result,FiberCubicValue(c,first));
            if(second>0.0&&second<1.0)result=max(result,FiberCubicValue(c,second));
        }
    }
    return result;
}
vec4 FiberCellRayCubic(sampler2D image,ivec3 cell,vec3 origin,vec3 delta) {
    vec4 coefficients=vec4(0.0);
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
        ivec3 corner=cell+ivec3(x,y,z);
        if(any(lessThan(corner,ivec3(0)))||any(greaterThanEqual(corner,ivec3(fiberGridSize.xyz))))continue;
        float value=max(texelFetch(image,FiberAtlasPixel(corner),0).r,0.0);
        vec3 first=mix(vec3(1.0)-origin,origin,vec3(x,y,z));
        vec3 slope=delta*(vec3(x,y,z)*2.0-1.0);
        coefficients+=value*vec4(first.x*first.y*first.z,
            slope.x*first.y*first.z+first.x*slope.y*first.z+first.x*first.y*slope.z,
            slope.x*slope.y*first.z+slope.x*first.y*slope.z+first.x*slope.y*slope.z,
            slope.x*slope.y*slope.z);
    }
    return coefficients;
}
float FiberTraceTrilinearExact(sampler2D image,vec3 p,vec3 direction,float maxDistance,bool maximum) {
    float norm=length(direction);if(norm<1e-8)return 0.0;
    vec3 ray=direction/norm,low=fiberGridMinStep.xyz;
    float h=fiberGridMinStep.w;
    vec3 high=low+fiberGridSize.xyz*h;
    float endDistance=maxDistance;
    for(int axis=0;axis<3;++axis)if(abs(ray[axis])>1e-8)
        endDistance=min(endDistance,((ray[axis]>0.0?high[axis]:low[axis])-p[axis])/ray[axis]);
    // Integrate the same finite domain and physical start bias as the normal
    // path, with no change to density, visibility meaning or source closure.
    float distance=h*max(fiberTrace.y,0.0),result=0.0;
    if(distance>=endDistance)return 0.0;
    vec3 q=(p-low)/h-.5,velocity=ray/h,start=q+velocity*distance;
    vec3 nextPlane=vec3(1e30),planeStep=vec3(1e30);
    for(int axis=0;axis<3;++axis)if(abs(velocity[axis])>1e-12) {
        float knot=velocity[axis]>0.0?floor(start[axis])+1.0:ceil(start[axis])-1.0;
        nextPlane[axis]=distance+(knot-start[axis])/velocity[axis];
        planeStep[axis]=abs(1.0/velocity[axis]);
    }
    // A <=128^3 grid crosses at most 3*129 texel-center planes. Simultaneous
    // plane crossings advance all tied axes; zero-length FP intervals are safe.
    for(int iteration=0;iteration<400;++iteration) {
        if(distance>=endDistance)break;
        float finish=min(endDistance,min(nextPlane.x,min(nextPlane.y,nextPlane.z)));
        float ds=max(finish-distance,0.0);
        if(ds>0.0) {
            ivec3 cell=ivec3(floor(q+velocity*(distance+.5*ds)));
            vec4 c=FiberCellRayCubic(image,cell,q+velocity*distance-vec3(cell),velocity*ds);
            if(maximum)result=max(result,FiberCubicMaximum(c));
            else result+=max(dot(c,vec4(1.0,.5,1.0/3.0,.25))*ds,0.0);
            if(result>=(maximum?1.0:80.0))return maximum?1.0:80.0;
        }
        for(int axis=0;axis<3;++axis)if(nextPlane[axis]<=finish)nextPlane[axis]+=planeStep[axis];
        distance=max(distance,finish);
    }
    return maximum?clamp(result,0.0,1.0):min(result,80.0);
}
)GLSL";}
} // namespace Render::PipelineDetail
