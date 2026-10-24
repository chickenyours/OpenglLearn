#pragma once
#include <string>

namespace Render::PipelineDetail {
// calcShading %419..553. All inputs and outputs are scene-linear; the source
// SRGB texture conversion precedes filtering and this HSV block. Native random
// and arc are authored UNORM8 attributes, not hashes or geometric arc lengths.
inline std::string StrandColorGLSL() { return R"GLSL(
int StrandGradientIndex(float randomValue,vec3 weights,float choiceOffset) {
    float denominator=max(weights.x+weights.y+weights.z,0.0001);
    float choice=fract(randomValue+choiceOffset);
    return choice<weights.x/denominator?0:choice<(weights.x+weights.y)/denominator?1:2;
}
vec3 StrandColorVariation(vec3 color,float randomValue,vec4 offsets,vec3 variation) {
    float maximum=max(color.r,max(color.g,color.b));
    float minimum=min(color.r,min(color.g,color.b));
    float delta=maximum-minimum;
    float inverseDelta=delta!=0.0?1.0/delta:0.0;
    float saturation=maximum!=0.0?delta/maximum:0.0;
    float hue=color.r==maximum?(color.g-color.b)*inverseDelta:
        color.g==maximum?2.0+(color.b-color.r)*inverseDelta:4.0+(color.r-color.g)*inverseDelta;
    hue=fract(hue*0.1666666716337204);
    hue=fract(hue+(2.0*fract(randomValue+offsets.y)-0.9960784316062927)*variation.x);
    saturation=mix(saturation,fract(randomValue+offsets.z),min(variation.y,0.9990000128746033));
    float value=mix(maximum,fract(randomValue+offsets.w),min(variation.z,0.9990000128746033));
    vec3 triangle=clamp(3.0*abs(2.0*fract(vec3(hue)+vec3(0.0,0.6666666865348816,0.3333333432674408))-1.0)-1.0,0.0,1.0);
    return (vec3(1.0)+(triangle-1.0)*saturation)*value;
}
vec3 StrandGradientColor(sampler2D gradientTable,vec2 coordinates,vec3 weights,vec4 offsets,vec3 variation) {
    int selected=StrandGradientIndex(coordinates.y,weights,offsets.x);
    // Three independent source 1D textures are rows of the host atlas. Sample
    // exact row centers to prevent bilinear mixing between A/B/C.
    vec3 sampled=textureLod(gradientTable,vec2(clamp(coordinates.x,0.0,1.0),(float(selected)+.5)/3.0),0.0).rgb;
    return StrandColorVariation(sampled,coordinates.y,offsets,variation);
}
)GLSL"; }
} // namespace Render::PipelineDetail
