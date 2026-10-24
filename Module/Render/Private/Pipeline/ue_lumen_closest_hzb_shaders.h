#pragma once
#include <string>

namespace Render::SurfaceCacheDetail {
// Closest view depth is monotonic inverse to UE's reversed device Z. One
// dispatch writes both an independent reduction level and its packed region.
inline std::string UeLumenClosestHzbReduceShader(){return R"GLSL(#version 450 core
layout(local_size_x=8,local_size_y=8) in;
layout(std140,binding=8) uniform UeClosestHzbData {
    ivec4 chExtent; // source xy, destination xy
    ivec4 chFlags; // source is current view positions, packed y offset, unused zw
};
layout(binding=0) uniform sampler2D chSource;
layout(rgba32f,set=3,binding=0) uniform writeonly image2D chLevel;
layout(rgba32f,set=3,binding=1) uniform writeonly image2D chPacked;
float CHDepth(ivec2 pixel){
    if(any(greaterThanEqual(pixel,chExtent.xy)))return 1e20;
    vec4 sampleValue=texelFetch(chSource,pixel,0);
    if(chFlags.x!=0){
        if(!(sampleValue.w>=.5)||!(sampleValue.z<0.0)||any(isnan(sampleValue))||any(isinf(sampleValue)))return 1e20;
        return -sampleValue.z;
    }
    return sampleValue.x>0.0&&!isnan(sampleValue.x)&&!isinf(sampleValue.x)?sampleValue.x:1e20;
}
void main(){
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(pixel,chExtent.zw)))return;
    ivec2 source=pixel*2;
    float depth=min(min(CHDepth(source),CHDepth(source+ivec2(1,0))),
        min(CHDepth(source+ivec2(0,1)),CHDepth(source+ivec2(1,1))));
    vec4 value=vec4(depth,0,0,0);
    imageStore(chLevel,pixel,value);
    imageStore(chPacked,pixel+ivec2(0,chFlags.y),value);
}
)GLSL";}
} // namespace Render::SurfaceCacheDetail
