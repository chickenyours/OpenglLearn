#pragma once
#include "Render/Private/Pipeline/strand_native_ds_math.h"
#include <string>

namespace Render::PipelineDetail {
// All positions and voxel dimensions at the RHI boundary are metres. Only
// the recovered fixed-point dose helper takes centimetres.
inline std::string NativeDSVolumeBlockGLSL(){return R"GLSL(
layout(std140,binding=14) uniform NativeDSVolume {
    mat4 nativeDsWorldToDS;
    vec4 nativeDsMinimum,nativeDsMaximum;
    uvec4 nativeDsResolution;
    vec4 nativeDsVoxel,nativeDsInverseExtent,nativeDsAtlas;
    uvec4 nativeDsLevels[6]; // offsetY, resource edge, slice columns, slice rows
    vec4 nativeDsTrace,nativeDsOpaqueInfo;
};
ivec2 NativeDSAtlasPixel(uvec3 cell,uint level){
    uvec4 levelInfo=nativeDsLevels[level];
    return ivec2((cell.z%levelInfo.z)*levelInfo.y+cell.x,levelInfo.x+(cell.z/levelInfo.z)*levelInfo.y+cell.y);
}
)GLSL";}
inline std::string NativeDSVolumeProducerGLSL(bool constants=true){return (constants?std::string(R"GLSL(
layout(std140,binding=0) uniform NativeDSProducer {
    mat4 nativeDsLocalToDS,nativeDsToWorld,nativeDsViewProjection,nativeDsInverseViewProjection;
    vec4 nativeDsForwardThreshold;
    vec4 nativeDsInjection; // raster width scale, trace-step scale, minimum voxel cm, interpolation injection
    uvec4 nativeDsCounts; // point count, original segment count, cube voxel count, unused
};
)GLSL"):std::string{})+R"GLSL(
layout(std430,set=2,binding=0) readonly buffer NativeDSPoints {vec4 nativeDsPoints[];};
layout(std430,set=2,binding=1) readonly buffer NativeDSSegments {uvec4 nativeDsSegments[];};
layout(std430,set=2,binding=2) buffer NativeDSPacked {uint nativeDsPacked[];};
layout(push_constant) uniform NativeDSDispatch {uvec4 nativeDsDispatch;};
uint NativeDSLinearInvocation(){return gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*gl_WorkGroupSize.x;}
uint NativeDSRawIndex(uvec3 cell){uint edge=uint(nativeDsAtlas.z);return cell.x+edge*(cell.y+edge*cell.z);}
)GLSL";}

inline std::string NativeDSVolumeClearShader(){return std::string(R"GLSL(#version 450 core
layout(local_size_x=256) in;
layout(rgba32f,set=3,binding=0) uniform writeonly image2D nativeDsOutput;
)GLSL")+NativeDSVolumeBlockGLSL()+NativeDSVolumeProducerGLSL(false)+R"GLSL(
void main(){uint index=NativeDSLinearInvocation(),edge=uint(nativeDsAtlas.z);
    if(index<edge*edge*edge)nativeDsPacked[index]=0u;
    uint width=uint(nativeDsAtlas.x),height=uint(nativeDsAtlas.y);
    if(index<width*height)imageStore(nativeDsOutput,ivec2(index%width,index/width),vec4(0.0));
}
)GLSL";}

inline std::string NativeDSVolumeRasterShader(){return std::string(R"GLSL(#version 450 core
layout(local_size_x=64) in;
)GLSL")+NativeDSVolumeBlockGLSL()+NativeDSVolumeProducerGLSL()+StrandNativeDsMathGLSL()+R"GLSL(
void main(){
    uint index=NativeDSLinearInvocation();if(index>=nativeDsCounts.y)return;
    uvec2 segment=nativeDsSegments[index].xy;
    vec4 a=nativeDsPoints[segment.x],b=nativeDsPoints[segment.y];
    vec3 start=(nativeDsLocalToDS*vec4(a.xyz,1.0)).xyz;
    vec3 end=(nativeDsLocalToDS*vec4(b.xyz,1.0)).xyz;
    vec3 delta=end-start;float distance=length(delta);if(!(distance>0.0))return;
    float step=nativeDsVoxel.w*nativeDsInjection.y;
    uint count=uint(min(ceil(distance/step),10.0*nativeDsAtlas.z));
    vec3 direction=delta/distance;ivec3 previous=ivec3(-1);
    float denominator=max(float(count)-1.0,1.0);
    for(uint i=0u;i<count;++i){
        vec3 position=start+(float(i)*step)*direction;
        vec3 coordinate=StrandNativeDsCoordinate(position,nativeDsMinimum.xyz,nativeDsInverseExtent.xyz,nativeDsResolution.xyz);
        uvec3 cell=uvec3(coordinate);if(all(equal(ivec3(cell),previous)))continue;
        previous=ivec3(cell);
        // Source 0.005 cm is a 50 um reference full width. The normalized
        // widths remain original diameter / authored average, independent of
        // rendered widthScale, curve subdivision and visible-child selection.
        float total=StrandNativeDsDoseTotal(nativeDsInjection.x,a.w,b.w,float(i)/denominator,nativeDsInjection.z);
        if(nativeDsInjection.w>.5){
            uint doses[8];StrandNativeDsIntpij(total,coordinate-vec3(cell),doses);
            for(uint z=0u;z<2u;++z)for(uint y=0u;y<2u;++y)for(uint x=0u;x<2u;++x){
                uint dose=doses[x+2u*y+4u*z];if(dose==0u)continue;
                uvec3 neighbor=min(cell+uvec3(x,y,z),nativeDsResolution.xyz-uvec3(1));
                atomicAdd(nativeDsPacked[NativeDSRawIndex(neighbor)],dose);
            }
        }else{
            uint dose=StrandNativeDsDose(total);if(dose!=0u)atomicAdd(nativeDsPacked[NativeDSRawIndex(cell)],dose);
        }
    }
}
)GLSL";}

inline std::string NativeDSVolumeOpaqueShader(){return std::string(R"GLSL(#version 450 core
layout(local_size_x=4,local_size_y=4,local_size_z=4) in;
layout(binding=3) uniform sampler2D nativeDsOpaqueDepth;
)GLSL")+NativeDSVolumeBlockGLSL()+NativeDSVolumeProducerGLSL()+R"GLSL(
void main(){
    uvec3 cell=gl_GlobalInvocationID.xyz;if(any(greaterThanEqual(cell,nativeDsResolution.xyz)))return;
    vec3 relative=vec3(cell)/vec3(nativeDsResolution.xyz-uvec3(1));
    vec3 point=nativeDsMinimum.xyz+relative*(nativeDsMaximum.xyz-nativeDsMinimum.xyz);
    vec3 world=(nativeDsToWorld*vec4(point,1.0)).xyz;
    vec4 clip=nativeDsViewProjection*vec4(world,1.0);
    if(!(clip.w>0.0))return;vec3 ndc=clip.xyz/clip.w;
    // RHI offscreen images have a bottom-left origin on both backends. This
    // is the host coordinate conversion from the source's top-left texture.
    vec2 uv=ndc.xy*.5+.5;if(any(lessThan(uv,vec2(0.0)))||any(greaterThanEqual(uv,vec2(1.0))))return;
    if(ndc.z< -1.0||ndc.z>1.0)return;
    ivec2 size=textureSize(nativeDsOpaqueDepth,0),pixel=ivec2(uv*vec2(size));
    float sceneDepth=texelFetch(nativeDsOpaqueDepth,pixel,0).r;
    float voxelReverse=1.0-(ndc.z*.5+.5),sceneReverse=1.0-sceneDepth;
    if(!(sceneReverse>voxelReverse))return;
    // As in source, reconstruct using the voxel's continuous NDC XY and the
    // sampled depth, not a snapped texel-center ray or a solid proxy volume.
    vec4 scene=nativeDsInverseViewProjection*vec4(ndc.xy,sceneDepth*2.0-1.0,1.0);
    if(abs(scene.w)<1e-20)return;
    float separation=abs(dot(world-scene.xyz/scene.w,nativeDsForwardThreshold.xyz));
    if(separation<nativeDsForwardThreshold.w)atomicOr(nativeDsPacked[NativeDSRawIndex(cell)],0xff000000u);
}
)GLSL";}

inline std::string NativeDSVolumeCopyShader(){return std::string(R"GLSL(#version 450 core
layout(local_size_x=4,local_size_y=4,local_size_z=4) in;
layout(rgba32f,set=3,binding=0) uniform writeonly image2D nativeDsOutput;
)GLSL")+NativeDSVolumeBlockGLSL()+NativeDSVolumeProducerGLSL(false)+StrandNativeDsMathGLSL()+R"GLSL(
void main(){uvec3 cell=gl_GlobalInvocationID.xyz;uint edge=uint(nativeDsAtlas.z);
    if(any(greaterThanEqual(cell,uvec3(edge))))return;
    vec2 rg=StrandNativeDsCopy(nativeDsPacked[NativeDSRawIndex(cell)]);
    imageStore(nativeDsOutput,NativeDSAtlasPixel(cell,0u),vec4(rg,0.0,0.0));
}
)GLSL";}

inline std::string NativeDSVolumeMipShader(){return std::string(R"GLSL(#version 450 core
layout(local_size_x=4,local_size_y=4,local_size_z=4) in;
layout(rgba32f,set=3,binding=0) uniform image2D nativeDsOutput;
)GLSL")+NativeDSVolumeBlockGLSL()+NativeDSVolumeProducerGLSL(false)+StrandNativeDsMathGLSL()+R"GLSL(
void main(){uint level=nativeDsDispatch.x;uvec3 cell=gl_GlobalInvocationID.xyz;
    if(any(greaterThanEqual(cell,uvec3(nativeDsLevels[level].y))))return;
    vec2 children[8];
    for(uint z=0u;z<2u;++z)for(uint y=0u;y<2u;++y)for(uint x=0u;x<2u;++x)
        children[x+2u*y+4u*z]=imageLoad(nativeDsOutput,NativeDSAtlasPixel(cell*2u+uvec3(x,y,z),level-1u)).rg;
    vec2 rg=StrandNativeDsMip(children);
    // Different levels occupy disjoint regions. Dispatch-level RHI image
    // barriers make the preceding level visible; no sampler aliases this UAV.
    imageStore(nativeDsOutput,NativeDSAtlasPixel(cell,level),vec4(rg,0.0,0.0));
}
)GLSL";}
} // namespace Render::PipelineDetail
