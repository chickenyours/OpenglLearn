#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <glm/glm.hpp>

namespace Render {
// A documented reconstruction of spatial skin transport. The shipped RE
// material supplies its blend/control mask; these RGB kernel widths are not a
// recovered definition of RE's SSS profile channel three.
enum class SkinDiffusionKernel : std::uint32_t { Gaussian=0, SourceBurley=1 };
struct SkinDiffusionSettings {
    bool enabled=false;
    // SourceBurley ports the radial law found in shipped SSSSSMainPass; the
    // runtime branch/profile selection is unconfirmed, distances remain host settings.
    SkinDiffusionKernel kernel=SkinDiffusionKernel::Gaussian;
    std::uint32_t diskSamples=20;
    float strength=1.f;
    float radiusMillimeters=1.2f;
    glm::vec3 channelRadii{1.f,.45f,.25f};
    // World units per meter after the character's placement transform.
    float worldUnitScale=1.f;
    // 0: pre-scatter color; .5: square-root split; 1: local color after lighting diffusion.
    float albedoPreservation=.5f;
    float normalRejectCosine=.65f;
    float depthRejectScale=1.5f;
    float maxRadiusPixels=64.f;
};
inline bool ValidateSkinDiffusionSettings(const SkinDiffusionSettings& p,std::string* error=nullptr) {
    const auto fail=[&](const char* message){if(error)*error=message;return false;};
    if((p.kernel!=SkinDiffusionKernel::Gaussian&&p.kernel!=SkinDiffusionKernel::SourceBurley)||p.diskSamples<8||p.diskSamples>64)
        return fail("Skin diffusion kernel must be Gaussian/SourceBurley, disk samples 8..64");
    if(!std::isfinite(p.strength)||p.strength<0||p.strength>1 ||
       !std::isfinite(p.radiusMillimeters)||p.radiusMillimeters<0||p.radiusMillimeters>20)
        return fail("Skin diffusion strength/radius must be finite, 0..1 and 0..20 mm");
    if(!std::isfinite(p.albedoPreservation)||p.albedoPreservation<0||p.albedoPreservation>1)
        return fail("Skin diffusion albedo preservation must be finite, 0..1");
    for(int c=0;c<3;++c)if(!std::isfinite(p.channelRadii[c])||p.channelRadii[c]<.01f||p.channelRadii[c]>4)
        return fail("Skin diffusion RGB radius multipliers must be finite, .01..4");
    if(!std::isfinite(p.worldUnitScale)||p.worldUnitScale<=0||p.worldUnitScale>10000 ||
       !std::isfinite(p.normalRejectCosine)||p.normalRejectCosine< -1||p.normalRejectCosine>=1 ||
       !std::isfinite(p.depthRejectScale)||p.depthRejectScale<=0||p.depthRejectScale>20 ||
       !std::isfinite(p.maxRadiusPixels)||p.maxRadiusPixels<1||p.maxRadiusPixels>256)
        return fail("Invalid skin diffusion world scale, boundary rejection or pixel cap");
    if(error)error->clear();return true;
}
} // namespace Render
