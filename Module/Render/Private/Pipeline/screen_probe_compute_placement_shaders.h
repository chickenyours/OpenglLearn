#pragma once
#include <stdexcept>
#include "screen_probe_placement_shaders.h"

namespace Render::ScreenProbePlacementDetail {
// One invocation owns one logical candidate slot and writes its original two
// interleaved texels directly. The original selection is evaluated once: its
// normalPart argument only changes the two successful return values. Record
// those normals at the same return sites without changing any selection math.
inline std::string ComputeMarkShader(bool uePlacement=false) {
    auto source=Common();
    const auto replace=[&](const std::string& from,const std::string& to){
        const auto at=source.find(from);
        if(at==std::string::npos||source.find(from,at+from.size())!=std::string::npos)
            throw std::logic_error("Compute probe placement source changed");
        source.replace(at,from.size(),to);
    };
    replace("in vec2 vUV;\nlayout(location=0) out vec4 outColor;","");
    if(uePlacement)return source+UePlacementGLSL()+R"GLSL(
layout(local_size_x=64) in;
layout(rgba32f,set=3,binding=0) uniform writeonly image2D spMarkedOutput;
void main(){uint id=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*(gl_NumWorkGroups.x*64u);
    uint columns=uint(spGrid.x*spGrid.z),capacity=columns*uint(spGrid.y);if(id>=capacity)return;
    ivec2 slot=ivec2(id%columns,id/columns),tile=ivec2(slot.x/spGrid.z,slot.y);
    vec4 position,normalDepth;SPUeMarkCandidate(tile,slot.x%spGrid.z,position,normalDepth);
    imageStore(spMarkedOutput,ivec2(slot.x*2,slot.y),position);
    imageStore(spMarkedOutput,ivec2(slot.x*2+1,slot.y),normalDepth);}
)GLSL";
    replace("vec4 SPGiSelectRepresentative(ivec2 tile,int sampleIndex,bool normalPart) {",
        "vec4 spPairNormal;\nvec4 SPGiSelectRepresentative(ivec2 tile,int sampleIndex,bool normalPart) {\n    spPairNormal=vec4(0);");
    replace("return normalPart?vec4(seedN,seedDepth):vec4(seedP,1);",
        "{spPairNormal=vec4(seedN,seedDepth);return normalPart?vec4(seedN,seedDepth):vec4(seedP,1);}");
    replace("    return normalPart?vec4(selectedN,selectedDepth):vec4(selectedP,state);",
        "    spPairNormal=vec4(selectedN,selectedDepth);\n    return normalPart?vec4(selectedN,selectedDepth):vec4(selectedP,state);");
    return source+R"GLSL(
layout(local_size_x=64) in;
layout(rgba32f,set=3,binding=0) uniform writeonly image2D spMarkedOutput;
void main(){
    uint id=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*(gl_NumWorkGroups.x*gl_WorkGroupSize.x);
    uint columns=uint(spGrid.x*spGrid.z),capacity=columns*uint(spGrid.y);
    if(id>=capacity)return;
    ivec2 slot=ivec2(id%columns,id/columns),tile=ivec2(slot.x/spGrid.z,slot.y);
    int sampleIndex=slot.x%spGrid.z;vec4 position=vec4(0),normalDepth=vec4(0);
    if((spTracing.w&2)!=0){
        if(!(spOptions.z<.5&&spScreen.w!=1)){
            position=SPGiSelectRepresentative(tile,sampleIndex,false);normalDepth=spPairNormal;
        }
    }else{
        ivec2 pixel=SPCandidatePixel(tile,sampleIndex);vec3 p,n;float depth;
        if(SPSurface(pixel,p,n,depth)){
            float state=SPUniformCoverage(pixel,p,n,depth)<SP_MIN_WEIGHT?2.0:1.0;
            position=vec4(p,state);normalDepth=vec4(n,depth);
        }
    }
    imageStore(spMarkedOutput,ivec2(slot.x*2,slot.y),position);
    imageStore(spMarkedOutput,ivec2(slot.x*2+1,slot.y),normalDepth);
}
)GLSL";
}
} // namespace Render::ScreenProbePlacementDetail
