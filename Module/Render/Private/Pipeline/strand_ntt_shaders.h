#pragma once
#include <string>
namespace Render::PipelineDetail {
inline std::string StrandNttGLSL() { return R"GLSL(
layout(binding=0) uniform sampler2D strandNttSampler;
#define FIBER_NTT_LOOKUP 1
float FiberNttAzimuth(float sinThetaI,float cosPhi,float azimuthalRoughness) {
    if(strandIbl.w>.5) {
        vec2 rg=textureLod(strandNttSampler,vec2(.5-.5*sinThetaI,sqrt(max(azimuthalRoughness,.02))),0.0).rg;
        // Original table channels are linear floats. G is a float shift,
        // without gamma conversion or an additional nonnegative clamp.
        return rg.r*exp(-(rg.g+cosPhi)*(rg.g+cosPhi));
    }
    return exp2(-5.741926193237305-5.2658371925354*cosPhi);
}
)GLSL"; }
} // namespace Render::PipelineDetail
