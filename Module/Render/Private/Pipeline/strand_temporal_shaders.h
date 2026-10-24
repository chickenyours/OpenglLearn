#pragma once
#include "Render/Private/Pipeline/strand_transparency_shaders.h"

namespace Render::PipelineDetail {
inline std::string StrandResolvedColorCopyFragmentShader() {return R"GLSL(#version 450 core
layout(binding=0) uniform sampler2D strandResolvedColor;
layout(location=0) out vec4 outColor;
void main(){outColor=texelFetch(strandResolvedColor,ivec2(gl_FragCoord.xy),0);}
)GLSL";}
// Produces a separate raw/jittered-grid depth for downstream temporal effects.
// No previous fiber position is available here: this is not a velocity buffer.
inline std::string StrandTemporalResolveFragmentShader(std::uint32_t count) {
    if(count&&!StrandSupportedFrontDepthGroups(count))throw std::invalid_argument("Unsupported strand temporal depth-group count");
    std::string source=R"GLSL(#version 450 core
layout(std140,binding=0) uniform StrandView {
    mat4 strandViewProjection; vec4 strandCamera; vec4 strandViewport;
};
layout(binding=0) uniform sampler2D strandRevealage;
layout(binding=1) uniform sampler2D strandNearestDepth;
layout(binding=2) uniform sampler2D strandOpaqueDepth;
layout(location=0) out vec4 outCoverage;
)GLSL";
    for(std::uint32_t i=0;i<count;++i)
        source+="layout(binding="+std::to_string(3+i)+") uniform sampler2D strandFront"+std::to_string(i)+";\n";
    source+=R"GLSL(void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    float transmission=exp(-min(max(texelFetch(strandRevealage,pixel,0).r,0.0),80.0));
)GLSL";
    for(std::uint32_t i=0;i<count;++i)
        source+="    transmission*=1.0-clamp(texelFetch(strandFront"+std::to_string(i)+",pixel,0).a,0.0,1.0);\n";
    source+=R"GLSL(    float alpha=clamp(1.0-transmission,0.0,1.0);
    float nearest=texelFetch(strandNearestDepth,pixel,0).r;
    float opaque=texelFetch(strandOpaqueDepth,pixel,0).r;
    bool writeHair=nearest<1.0&&nearest<=opaque&&alpha>=strandViewport.w;
    gl_FragDepth=writeHair?min(nearest,opaque):opaque;
    // R: full strand coverage; G: high-coverage hair won the depth merge.
    outCoverage=vec4(alpha,writeHair?1.0:0.0,0.0,1.0);
}
)GLSL";
    return source;
}
} // namespace Render::PipelineDetail
