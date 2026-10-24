#pragma once
#include "Render/Private/Pipeline/strand_shaders.h"
#include <cstdint>
#include <stdexcept>
#include <string>

namespace Render::PipelineDetail {
inline bool StrandSupportedFrontDepthGroups(std::uint32_t count) {
    return count==2||count==3||count==7;
}
// Both MRT outputs use additive ONE/ONE blending. Keep alpha/coverage and all
// lighting sourced from the production fragment shader; the only selection
// change is hardware Equal/Greater against an independently discovered depth.
// That active depth attachment is never sampled by this shader.
inline std::string StrandPairedAccumulationFragmentShader(int mode) {
    if(mode!=2&&mode!=3)throw std::invalid_argument("Paired strands require a depth group or tail");
    auto source=StrandFragmentShader(false,mode);
    const std::string output="layout(location=0) out vec4 outColor;";
    const auto outputAt=source.find(output);
    if(outputAt==std::string::npos)throw std::runtime_error("Strand accumulation output contract changed");
    source.insert(outputAt+output.size(),"\nlayout(location=1) out vec4 strandLogReveal;");
    const auto begin=source.find("#ifdef STRAND_EQUAL_DEPTH_GROUP\n");
    const auto end=source.find("#endif",begin);
    if(begin==std::string::npos||end==std::string::npos)throw std::runtime_error("Strand depth-selection contract changed");
    source.replace(begin,end+6-begin,std::string("// Selected depth is tested by read-only hardware Equal/Greater.\n")+
        (mode==3?"    if(gl_FragCoord.z>=1.0)discard; // Cleared/empty selected-depth sentinel.\n":""));
    const std::string previous="layout(binding=2) uniform sampler2D strandPreviousDepth;\n";
    const auto previousAt=source.find(previous);
    if(previousAt==std::string::npos)throw std::runtime_error("Strand selected-depth sampler contract changed");
    source.erase(previousAt,previous.size());
    source.insert(source.find('\n')+1,"layout(early_fragment_tests) in;\n");
    const auto close=source.rfind('}');
    if(close==std::string::npos)throw std::runtime_error("Strand accumulation main contract changed");
    source.insert(close,"    strandLogReveal=vec4(-log(max(1.0-alpha,.0001)),0.0,0.0,0.0);\n");
    return source;
}
inline std::string StrandStableGroupCompositeFragmentShader() {
    auto source=StrandCompositeFragmentShader(false);
    const std::string threshold="accum.a>.00001";
    const auto at=source.find(threshold);
    if(at==std::string::npos)throw std::runtime_error("Strand group normalization contract changed");
    // FP32 scratch retains very thin positive deposits. An arbitrary weight
    // cutoff discards their RGB while leaving log coverage, causing dark edges.
    source.replace(at,threshold.size(),"accum.a>0.0");
    return source;
}
// Explicit depth groups plus a weighted tail. Three/seven groups resemble the
// near-layer capacity of native four/eight slots, not its packed MLAB algorithm.
inline std::string StrandLayeredCompositeFragmentShader(std::uint32_t count,bool stableAccumulation=false) {
    if(!StrandSupportedFrontDepthGroups(count))throw std::invalid_argument("Unsupported strand front depth-group count");
    std::string source=R"GLSL(#version 450 core
layout(binding=0) uniform sampler2D strandAccumulation;
layout(binding=1) uniform sampler2D strandRevealage;
in vec2 vUV;
layout(location=0) out vec4 outColor;
)GLSL";
    for(std::uint32_t i=0;i<count;++i)
        source+="layout(binding="+std::to_string(2+i)+") uniform sampler2D strandFront"+std::to_string(i)+";\n";
    source+=R"GLSL(void main() {
    vec4 accum=texture(strandAccumulation,vUV);
    float tailAlpha=1.0-exp(-min(max(texture(strandRevealage,vUV).r,0.0),80.0));
    vec3 tailColor=accum.a>.00001?accum.rgb/accum.a:vec3(0.0);
    vec3 premultiplied=vec3(0.0);
    float transmission=1.0;
    ivec2 pixel=ivec2(gl_FragCoord.xy);
)GLSL";
    for(std::uint32_t i=0;i<count;++i) {
        source+="    { vec4 front=texelFetch(strandFront"+std::to_string(i)+",pixel,0);\n";
        source+="      float alpha=clamp(front.a,0.0,1.0); premultiplied+=transmission*front.rgb*alpha; transmission*=1.0-alpha; }\n";
    }
    source+=R"GLSL(    premultiplied+=transmission*tailColor*tailAlpha;
    float alpha=clamp(1.0-transmission*(1.0-tailAlpha),0.0,1.0);
    // Render/RHI AlphaComposite consumes straight RGB; convert exactly once.
    vec3 color=alpha>0.0?premultiplied/alpha:vec3(0.0);
    outColor=vec4(clamp(color,vec3(0.0),vec3(60000.0)),alpha);
}
)GLSL";
    if(stableAccumulation) {
        const std::string threshold="accum.a>.00001";
        const auto at=source.find(threshold);
        if(at==std::string::npos)throw std::runtime_error("Strand tail normalization contract changed");
        source.replace(at,threshold.size(),"accum.a>0.0");
    }
    return source;
}
} // namespace Render::PipelineDetail
