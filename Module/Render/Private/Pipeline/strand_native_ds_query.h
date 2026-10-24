#pragma once
#include "Render/Private/Pipeline/strand_native_ds_math.h"
#include <string>

namespace Render::PipelineDetail {
// Texture DSVolume consumer. Source storage format / sampler creation are not
// captured. An RGBA32F tiled atlas implements six resource-cube levels, using
// explicit 3D clamp trilinear fetches. No 2D filtering crosses slice borders.
inline std::string StrandNativeDsQueryGLSL(){return StrandNativeDsMathGLSL()+R"GLSL(
layout(std140,binding=14) uniform StrandNativeDsVolume {
    mat4 dsWorldToLocal;
    vec4 dsMinimum,dsMaximum;
    uvec4 dsResolution;
    vec4 dsVoxel,dsInverseExtent,dsAtlas;
    uvec4 dsLevels[6];
    vec4 dsTrace,dsOpaque;
};
layout(binding=4) uniform sampler2D strandDsHierarchy;
bool StrandNativeDsEnabled(){return dsAtlas.w>.5;}
ivec2 StrandNativeDsPixel(ivec3 p,uint lod){
    uvec4 level=dsLevels[lod];int edge=int(level.y),columns=int(level.z);
    return ivec2((p.z%columns)*edge+p.x,int(level.x)+(p.z/columns)*edge+p.y);
}
vec2 StrandNativeDsLoad(ivec3 p,uint lod){
    int edge=int(dsLevels[lod].y);
    if(any(lessThan(p,ivec3(0)))||any(greaterThanEqual(p,ivec3(edge))))return vec2(0.0);
    return texelFetch(strandDsHierarchy,StrandNativeDsPixel(p,lod),0).rg;
}
vec2 StrandNativeDsFiltered(vec3 uv,uint lod){
    int edge=int(dsLevels[lod].y);
    vec3 at=uv*float(edge)-.5;ivec3 base=ivec3(floor(at));vec3 f=fract(at);
    vec2 c[8];for(int i=0;i<8;++i){
        ivec3 p=clamp(base+ivec3(i&1,(i>>1)&1,(i>>2)&1),ivec3(0),ivec3(edge-1));
        c[i]=StrandNativeDsLoad(p,lod);
    }
    return mix(mix(mix(c[0],c[1],f.x),mix(c[2],c[3],f.x),f.y),
               mix(mix(c[4],c[5],f.x),mix(c[6],c[7],f.x),f.y),f.z);
}
// Returns dimensionless N and opaque visibility 1-G. Native static Diana
// origin/direction jitter are both zero. Runtime nonzero jitter is not guessed.
vec2 StrandNativeDsPath(vec3 position,vec3 direction,float maxDistance){
    if(!StrandNativeDsEnabled())return vec2(0.0,1.0);
    float norm=length(direction);if(norm<1e-12||maxDistance<=0.0)return vec2(0.0,1.0);
    vec3 initialRay=direction/norm;
    vec3 origin=position+dsTrace.y*dsVoxel.xyz*initialRay;
    // Offset changes only start; preserve the original target. Per-axis
    // voxels make this more than a scalar shortening for nearby lights.
    vec3 delta=position+initialRay*maxDistance-origin;
    float remaining=length(delta);if(remaining<1e-12)return vec2(0.0,1.0);
    vec3 ray=delta/remaining;
    float enter=0.0,leave=remaining;
    for(int axis=0;axis<3;++axis){
        if(abs(ray[axis])<1e-12){
            if(origin[axis]<dsMinimum[axis]||origin[axis]>dsMaximum[axis])return vec2(.5,1.0);
        }else{
            float a=(dsMinimum[axis]-origin[axis])/ray[axis],b=(dsMaximum[axis]-origin[axis])/ray[axis];
            enter=max(enter,min(a,b));leave=min(leave,max(a,b));
        }
    }
    if(leave<=enter||leave<0.0)return vec2(.5,1.0);
    vec3 entry=origin+ray*enter;
    vec3 relativeEntry=entry-dsMinimum.xyz;
    float clippedLength=min(max(leave-enter,0.0),1000.0); // source 1e5 cm
    float h=StrandNativeDsMinVoxel(dsVoxel.xyz);
    vec3 step=h*ray;
    float count=ceil(clippedLength/h);if(count<=0.0)return vec2(0.0,1.0);
    float baseDs=clippedLength/count,cursor=0.0,factor=1.0;
    ivec3 lastCell=ivec3(-1);vec2 ng=vec2(0.0);
    // Explicit host finite-work guard for pathological anisotropic inputs;
    // not a recovered source cap. Four source attempts per group. The normal
    // Diana 5mm field exits by source count/G well before this bound.
    for(int group=0;group<65536;++group){
        vec2 reads[4];vec4 weights=vec4(0.0);
        for(int attempt=0;attempt<4;++attempt){
            reads[attempt]=vec2(0.0);
            if(cursor<count){
                vec3 q=clamp((relativeEntry+cursor*step)*dsInverseExtent.xyz,vec3(0.0),vec3(1.0));
                ivec3 cell=ivec3(q*vec3(dsResolution.xyz-uvec3(1)));
                if(any(notEqual(cell,lastCell))){
                    uint lod;float rounded,weight;
                    bool validLod=StrandNativeDsTryLod(factor,baseDs,dsVoxel.x,weight,lod,rounded);
                    // Negative float->uint in DXIL has no recovered result.
                    // Explicit host clamp0 (not asserted native behavior).
                    if(!validLod)lod=0u;
                    reads[attempt]=cursor<dsTrace.w?
                        StrandNativeDsFiltered(StrandNativeDsTextureUV(q,dsResolution.xyz,uint(dsAtlas.z)),lod):
                        StrandNativeDsLoad(cell>>int(lod),lod);
                    weights[attempt]=weight;
                    lastCell=cell;
                    factor=StrandNativeDsNextFactor(factor,dsTrace.x,uint(dsTrace.z));
                }
            }
            cursor=StrandNativeDsAdd(cursor,factor);
        }
        StrandNativeDsAccumulate4(reads,weights,ng);
        if(ng.y>=1.0||cursor>=count)break;
    }
    return vec2(ng.x,1.0-ng.y);
}
)GLSL";}
} // namespace Render::PipelineDetail
