#pragma once
#include <cstdint>
#include <string>

namespace Render {
// The 208-byte block matches SurfaceCacheGiConstants. These pure GLSL helpers
// are shared by card lighting and the opt-in opaque PBR area-shadow variant.
inline std::string SurfaceCacheDataGLSL() {return R"GLSL(
layout(std140,binding=12) uniform SurfaceCacheData {
    mat4 scInverseView;
    ivec4 scAtlas,scGeometry,scScreen;
    vec4 scParameters,scSunDirectionIntensity,scSunColor;
    vec4 scSpotPositionRadius,scSpotDirectionOuter,scSpotIntensityInner;
};
)GLSL";}
inline std::string SoftwareBvhVisibilityGLSL(std::uint32_t textureSlot=9) {
    return "\nlayout(binding="+std::to_string(textureSlot)+") uniform sampler2D scTriangles;\n"+R"GLSL(
vec4 SCGeometry(int index){return texelFetch(scTriangles,ivec2(index%scGeometry.y,index/scGeometry.y),0);}
bool SCBox(vec3 p,vec3 d,vec3 low,vec3 high,float maximum,out float near) {
    near=0;float far=maximum;
    for(int axis=0;axis<3;++axis) {
        if(abs(d[axis])<1e-9){if(p[axis]<low[axis]||p[axis]>high[axis])return false;continue;}
        float a=(low[axis]-p[axis])/d[axis],b=(high[axis]-p[axis])/d[axis];
        near=max(near,min(a,b));far=min(far,max(a,b));if(near>far)return false;
    }return true;
}
bool SCIntersect(vec3 p,vec3 d,inout float maximum,out vec3 n,out int triangle,out bool exhausted,bool anyHit) {
    n=vec3(0);triangle=-1;exhausted=false;if(scGeometry.z==0)return false;
    // Exact-median BVH construction has at most 18 root-to-leaf entries for
    // the validated <= 2^20 triangles. Retain overflow/exhaustion guards.
    int stack[32];stack[0]=0;int size=1;bool found=false;
    for(int visit=0;visit<16384&&size>0;++visit) {
        int node=stack[--size];vec4 low=SCGeometry(node*2),high=SCGeometry(node*2+1);float near;
        if(!SCBox(p,d,low.xyz,high.xyz,maximum,near))continue;
        if(high.w>=0) {
            if(size>29){exhausted=true;return found;}
            int left=int(low.w),right=int(high.w);float ln,rn;
            bool l=SCBox(p,d,SCGeometry(left*2).xyz,SCGeometry(left*2+1).xyz,maximum,ln);
            bool r=SCBox(p,d,SCGeometry(right*2).xyz,SCGeometry(right*2+1).xyz,maximum,rn);
            if(l&&r){stack[size++]=ln<rn?right:left;stack[size++]=ln<rn?left:right;}
            else if(l)stack[size++]=left;else if(r)stack[size++]=right;
            continue;
        }
        for(int i=0;i<int(-high.w);++i) {
            int offset=scGeometry.x+(int(low.w)+i)*5;
            vec3 a=SCGeometry(offset).xyz,ab=SCGeometry(offset+1).xyz-a,ac=SCGeometry(offset+2).xyz-a;
            vec3 q=cross(d,ac);float determinant=dot(ab,q);if(abs(determinant)<1e-9)continue;
            vec3 relative=p-a;float u=dot(relative,q)/determinant;if(u< -1e-6||u>1.000001)continue;
            vec3 r=cross(relative,ab);float v=dot(d,r)/determinant;if(v< -1e-6||u+v>1.000001)continue;
            float distance=dot(ac,r)/determinant;if(distance<1e-5||distance>=maximum)continue;
            maximum=distance;n=normalize(cross(ab,ac));triangle=int(low.w)+i;found=true;if(anyHit)return true;
        }
    }
    exhausted=size>0;return found;
}
bool SCVisible(vec3 p,vec3 n,vec3 d,float distance) {
    vec3 unused;int triangle;bool exhausted;distance=max(0.0,distance-2*scParameters.y);
    bool found=SCIntersect(p+n*scParameters.y,d,distance,unused,triangle,exhausted,true);
    return !found&&!exhausted;
}
)GLSL";
}
} // namespace Render
