#pragma once
#include <array>
#include <glm/glm.hpp>

namespace Render {
// Source StrandIndirectLightCS input after probe decompression. Coefficients
// are +X,-X,+Y,-Y,+Z,-Z in scene-linear units, before the material's .8 gain.
// A caller may supply a source probe or an explicitly documented adapter.
struct StrandIndirectLightingSettings {
    bool enabled=false;
    std::array<glm::vec3,6> coefficients{};
    float multiplier=1.f; // separate source producer CB multiplier
    float exposureEV=0.f; // host photometric-unit adapter; not source exposure
    float rotation=0.f;   // radians about world +Y
    // Extra preview ambient is outside the selected native probe incident
    // chain. Scale only the hair's ambient pedestal while this input is on;
    // 1 preserves the generic adapter, 0 uses the probe alone. Opaque lighting
    // and the material/producer multipliers remain independent.
    float hostAmbientScale=1.f;
};
}
