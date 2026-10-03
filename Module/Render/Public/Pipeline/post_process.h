#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

#include <glm/glm.hpp>

namespace Render {

inline constexpr std::uint32_t PostProcessBinding = 5;
inline constexpr std::uint32_t MaxBloomLevels = 6;

enum class ToneMapMode : std::uint32_t { Reinhard = 0, ACES = 1, None = 2 };
enum class PostFilter : std::uint32_t {
    None = 0, Grayscale = 1, Gray = Grayscale, Invert = 2, Sharpen = 3,
    Emboss = 4, Edge = 5, Gaussian = 6, Ripple = 7
};

struct PostProcessSettings {
    bool bloomEnabled = true;
    float bloomThreshold = 0.8f;
    float bloomStrength = 0.4f;
    std::uint32_t bloomLevels = 5;
    bool ssaoEnabled = true;
    float ssaoRadius = 0.5f;
    float ssaoBias = 0.025f;
    float ssaoPower = 1.5f;
    float exposure = 1.0f;
    float gamma = 2.2f;
    ToneMapMode toneMap = ToneMapMode::Reinhard;
    PostFilter filter = PostFilter::None;
    float saturation = 1.0f;
    float contrast = 1.0f;
    float vignette = 0.0f;
    float sharpenStrength = 1.0f;
};

// These limits describe the current renderer, not device capabilities. The
// host clamps each Bloom pyramid dimension to at least one texel.
inline bool ValidatePostProcessSettings(const PostProcessSettings& settings,
                                        std::string* error = nullptr) {
    if (error) error->clear();
    const auto fail = [&](const char* message) {
        if (error) *error = message;
        return false;
    };
    const auto inRange = [](float value, float minimum, float maximum) {
        return std::isfinite(value) && value >= minimum && value <= maximum;
    };
    if (settings.bloomLevels == 0 || settings.bloomLevels > MaxBloomLevels)
        return fail("Bloom levels must be between 1 and 6");
    if (!inRange(settings.bloomThreshold, 0.0f, 1000000.0f) ||
        !inRange(settings.bloomStrength, 0.0f, 32.0f))
        return fail("Invalid Bloom threshold or strength");
    if (!inRange(settings.ssaoRadius, 0.001f, 1000.0f) ||
        !inRange(settings.ssaoBias, 0.0f, settings.ssaoRadius) ||
        !inRange(settings.ssaoPower, 0.01f, 16.0f))
        return fail("Invalid SSAO radius, bias or power");
    if (!inRange(settings.exposure, 0.001f, 64.0f) || !inRange(settings.gamma, 0.1f, 8.0f))
        return fail("Invalid post-process exposure or gamma");
    if (settings.toneMap != ToneMapMode::Reinhard && settings.toneMap != ToneMapMode::ACES &&
        settings.toneMap != ToneMapMode::None)
        return fail("Unknown tone-map mode");
    if (static_cast<std::uint32_t>(settings.filter) > static_cast<std::uint32_t>(PostFilter::Ripple))
        return fail("Unknown post-process filter");
    if (!inRange(settings.saturation, 0.0f, 4.0f) || !inRange(settings.contrast, 0.0f, 4.0f) ||
        !inRange(settings.vignette, 0.0f, 1.0f) || !inRange(settings.sharpenStrength, 0.0f, 4.0f))
        return fail("Invalid color grading or filter strength");
    return true;
}

// Shared std140 block at binding 5. A whole update fits the RHI's 256-byte
// inline command payload. Texture slots are pass-local: 0=input/scene,
// 1=Bloom/second image, 2=scene depth. SSAO feeds PBR ambient lighting at slot 14;
// the final composite must not darken direct lighting with AO a second time.
struct alignas(16) PostProcessConstants {
    glm::mat4 projection{1.0f};
    glm::mat4 inverseProjection{1.0f};
    glm::vec4 bloom{0.8f, 0.4f, 1.0f, 0.0f}; // threshold, strength, blur direction XY
    glm::vec4 ssao{0.5f, 0.025f, 1.5f, 1.0f}; // view-space radius, bias, power, enabled
    glm::vec4 output{1.0f, 2.2f, 0.0f, 0.0f}; // exposure, gamma, tone-map enum, filter enum
    glm::vec4 screen{1.0f, 1.0f, 0.0f, 1.0f}; // inverse source dimensions, unused, Bloom enabled
    glm::vec4 grading{1.0f, 1.0f, 0.0f, 1.0f}; // saturation, contrast, vignette, sharpen strength
    glm::vec4 misc{0.1f, 100.0f, 0.0f, 1.0f}; // near, far, time, SSAO enabled
};

static_assert(std::is_standard_layout_v<PostProcessConstants>);
static_assert(std::is_trivially_copyable_v<PostProcessConstants>);
static_assert(alignof(PostProcessConstants) == 16);
static_assert(offsetof(PostProcessConstants, projection) == 0);
static_assert(offsetof(PostProcessConstants, inverseProjection) == 64);
static_assert(offsetof(PostProcessConstants, bloom) == 128);
static_assert(offsetof(PostProcessConstants, ssao) == 144);
static_assert(offsetof(PostProcessConstants, output) == 160);
static_assert(offsetof(PostProcessConstants, screen) == 176);
static_assert(offsetof(PostProcessConstants, grading) == 192);
static_assert(offsetof(PostProcessConstants, misc) == 208);
static_assert(sizeof(PostProcessConstants) == 224);

// Fill the settings portion; the pass recorder supplies projection matrices,
// source dimensions and time and selects the Gaussian direction for each pass.
inline PostProcessConstants MakePostProcessConstants(const PostProcessSettings& settings) {
    PostProcessConstants result;
    result.bloom.x = settings.bloomThreshold;
    result.bloom.y = settings.bloomStrength;
    result.ssao = {settings.ssaoRadius, settings.ssaoBias, settings.ssaoPower, settings.ssaoEnabled ? 1.0f : 0.0f};
    result.output = {settings.exposure, settings.gamma, float(settings.toneMap), float(settings.filter)};
    result.screen.w = settings.bloomEnabled ? 1.0f : 0.0f;
    result.grading = {settings.saturation, settings.contrast, settings.vignette, settings.sharpenStrength};
    result.misc.w = settings.ssaoEnabled ? 1.0f : 0.0f;
    return result;
}

} // namespace Render
