#pragma once
#include "Render/Private/Pipeline/strand_shadow_pass.h"
#include <stdexcept>
#include <string>
#include <utility>

namespace Render::PipelineDetail {
// Reuse the ordered caster's actual CDF and physical projected-area expression.
// Only its rank/discard policy is replaced; this is an explicit HOST DOM.
inline std::pair<std::string,std::string> StrandOpacityShadowCoverageParts(){
    const auto source=StrandShadowFragmentShader();
    const auto main=source.find("void main()"),begin=source.find("    vec2 dd=",main),end=source.find("    ivec2 p=",begin);
    if(main==std::string::npos||begin==std::string::npos||end==std::string::npos)
        throw std::runtime_error("DOM requires the shared analytic strand shadow coverage contract");
    return {source.substr(0,main),source.substr(begin,end-begin)};
}
inline std::string StrandOpacityShadowNearestFragmentShader(){
    const auto parts=StrandOpacityShadowCoverageParts();
    return parts.first+"void main(){\n"+parts.second+R"GLSL(
    // Keep every positive projected contribution, including a<1/128 which
    // cannot survive the ordered caster's smallest threshold. No rank test.
    if(a<=0.0)discard;
}
)GLSL";
}
inline std::string StrandOpacityShadowTauFragmentShader(){
    const auto parts=StrandOpacityShadowCoverageParts();
    return parts.first+R"GLSL(
layout(binding=0) uniform sampler2D opacityNearestDepth;
layout(std140,binding=0) uniform ShadowView {mat4 vp;vec4 lightDirection;vec4 viewport;};
layout(location=0) out vec4 lowTau;
layout(location=1) out vec4 highTau;
void main(){
)GLSL"+parts.second+R"GLSL(
    if(a<=0.0)discard;
    float nearest=texelFetch(opacityNearestDepth,ivec2(gl_FragCoord.xy),0).r;
    float tau=-log(max(1.0-a,1e-6));
    float span=max(viewport.z,1e-7);
    // Nonuniform cumulative-depth nodes concentrate resolution near the first
    // fiber. Inter-node reconstruction remains a labeled depth approximation.
    lowTau=tau*step(vec4(gl_FragCoord.z),vec4(nearest)+span*vec4(0.0,1.0/256.0,1.0/128.0,1.0/64.0));
    highTau=tau*step(vec4(gl_FragCoord.z),vec4(nearest)+span*vec4(1.0/32.0,1.0/16.0,1.0/4.0,1.0));
}
)GLSL";
}
inline std::string StrandOpacityShadowPackFragmentShader(){
    return R"GLSL(#version 450 core
layout(binding=0) uniform sampler2D opacityOriginalOpaqueDepth;
layout(binding=1) uniform sampler2D opacityNearestDepth;
layout(binding=2) uniform sampler2D opacityLowTau;
layout(binding=3) uniform sampler2D opacityHighTau;
layout(location=0) out vec4 packedShadow;
void main(){
    ivec2 p=ivec2(gl_FragCoord.xy),o=textureSize(opacityNearestDepth,0);
    ivec2 q=p/o,local=p%o;
    float opaque=texelFetch(opacityOriginalOpaqueDepth,p,0).r;
    float nearest=texelFetch(opacityNearestDepth,local,0).r;
    vec4 low=texelFetch(opacityLowTau,local,0),high=texelFetch(opacityHighTau,local,0);
    vec3 payload=q.y==0?(q.x==0?vec3(nearest,low.rg):vec3(low.ba,high.r)):
                              (q.x==0?high.gba:vec3(0.0));
    // R always retains the original A-by-A opaque CSM texel. GBA is a
    // separate quadranted payload, not a downsampled opaque depth channel.
    packedShadow=vec4(opaque,payload);
}
)GLSL";
}
} // namespace Render::PipelineDetail
