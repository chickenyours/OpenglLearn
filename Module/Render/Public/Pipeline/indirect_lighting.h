#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <glm/glm.hpp>

namespace Render {
inline constexpr std::uint32_t IndirectLightingBinding = 9;
inline constexpr std::uint32_t IndirectLightingTextureSlot = 15;
inline constexpr std::uint32_t MaxIndirectLightingSamples = 32;
inline constexpr std::uint32_t MaxIndirectLightingSteps = 64;
inline constexpr std::uint32_t IndirectLightingDenoisePasses = 3;
// Bound expensive ray/denoise work independently of display resolution.
inline constexpr std::uint32_t MaxIndirectLightingDimension = 640;

// Single diffuse bounce from visible opaque/cutout surfaces. Missing screen
// data contributes zero; this does not replace world-space indirect lighting.
struct IndirectLightingSettings {
    bool enabled = false;
    float intensity = 1.0f;
    float radius = 3.0f;
    float thickness = 0.15f;
    float bias = 0.03f;
    std::uint32_t sampleCount = 12;
    std::uint32_t stepCount = 24;
};

inline bool ValidateIndirectLightingSettings(const IndirectLightingSettings& settings,
                                             std::string* error = nullptr) {
    if (error) error->clear();
    const auto fail = [&](const char* message) { if (error) *error = message; return false; };
    const auto range = [](float value, float low, float high) {
        return std::isfinite(value) && value >= low && value <= high;
    };
    if (!range(settings.intensity, 0, 16) || !range(settings.radius, 0.01f, 100))
        return fail("Invalid indirect lighting intensity or radius");
    if (!range(settings.thickness, 0.0001f, settings.radius) ||
        !range(settings.bias, 0, settings.radius * 0.5f))
        return fail("Invalid indirect lighting thickness or surface bias");
    if (!settings.sampleCount || settings.sampleCount > MaxIndirectLightingSamples ||
        settings.stepCount < 4 || settings.stepCount > MaxIndirectLightingSteps)
        return fail("Indirect lighting requires 1..32 samples and 4..64 trace steps");
    return true;
}

struct alignas(16) IndirectLightingConstants {
    glm::mat4 projection{1};
    glm::mat4 inverseProjection{1};
    glm::mat4 view{1};
    glm::vec4 trace{3.0f, 0.15f, 0.03f, 1.0f}; // radius, thickness, bias, intensity
    glm::ivec4 options{12, 24, 0, 0}; // cosine samples, steps, enabled, denoise stride
};
static_assert(std::is_trivially_copyable_v<IndirectLightingConstants>);
static_assert(offsetof(IndirectLightingConstants, projection) == 0);
static_assert(offsetof(IndirectLightingConstants, inverseProjection) == 64);
static_assert(offsetof(IndirectLightingConstants, view) == 128);
static_assert(offsetof(IndirectLightingConstants, trace) == 192);
static_assert(offsetof(IndirectLightingConstants, options) == 208);
static_assert(sizeof(IndirectLightingConstants) == 224);

// Pass the same jittered projection and view used for all three capture maps.
inline IndirectLightingConstants MakeIndirectLightingConstants(const IndirectLightingSettings& settings,
                                                               const glm::mat4& projection,
                                                               const glm::mat4& view) {
    IndirectLightingConstants result;
    result.projection = projection;
    result.inverseProjection = glm::inverse(projection);
    result.view = view;
    result.trace = {settings.radius, settings.thickness, settings.bias, settings.intensity};
    result.options = {static_cast<int>(settings.sampleCount), static_cast<int>(settings.stepCount), settings.enabled ? 1 : 0, 0};
    return result;
}
} // namespace Render
