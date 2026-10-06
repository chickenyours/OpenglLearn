#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <glm/glm.hpp>

namespace Render {

inline constexpr std::uint32_t TemporalEffectsBinding = 6;
inline constexpr std::uint32_t TemporalJitterPeriod = 8;
enum class AntialiasingMode : std::uint32_t { None = 0, FXAA = 1, TAA = 2 };

// MSAA is independently selected; supported sample counts still depend on the
// RHI/device. TAA and blur use camera motion only, without object motion vectors.
struct TemporalEffectsSettings {
    AntialiasingMode antialiasing = AntialiasingMode::FXAA;
    std::uint32_t msaaSamples = 1;
    float historyWeight = 0.9f;
    float depthRejectAbsolute = 0.05f; // world units along the previous view ray
    float depthRejectRelative = 0.01f; // fraction of current camera distance
    float fxaaEdgeThreshold = 0.125f;
    float fxaaMinThreshold = 0.0312f;
    bool motionBlurEnabled = false;
    float motionBlurStrength = 1.0f;
    float motionBlurShutterSeconds = 1.0f / 120.0f;
    float motionBlurMaxPixels = 32.0f; // total sample span, not radius
    std::uint32_t motionBlurSamples = 12;
};

inline bool ValidateTemporalEffectsSettings(const TemporalEffectsSettings& settings,
                                           std::string* error = nullptr) {
    if (error) error->clear();
    const auto fail = [&](const char* message) { if (error) *error = message; return false; };
    const auto range = [](float value, float lo, float hi) {
        return std::isfinite(value) && value >= lo && value <= hi;
    };
    if (settings.antialiasing != AntialiasingMode::None && settings.antialiasing != AntialiasingMode::FXAA &&
        settings.antialiasing != AntialiasingMode::TAA) return fail("Unknown antialiasing mode");
    if (settings.msaaSamples != 1 && settings.msaaSamples != 2 && settings.msaaSamples != 4 && settings.msaaSamples != 8)
        return fail("MSAA samples must be 1, 2, 4 or 8");
    if (!range(settings.historyWeight, 0, 0.98f) || !range(settings.depthRejectAbsolute, 0, 100) ||
        !range(settings.depthRejectRelative, 0, 0.25f)) return fail("Invalid temporal history settings");
    if (!range(settings.fxaaEdgeThreshold, 0, 1) || !range(settings.fxaaMinThreshold, 0, 1))
        return fail("Invalid FXAA edge thresholds");
    if (!range(settings.motionBlurStrength, 0, 4) || !range(settings.motionBlurShutterSeconds, 0.0001f, 0.25f) ||
        !range(settings.motionBlurMaxPixels, 0, 256) || settings.motionBlurSamples < 2 || settings.motionBlurSamples > 32)
        return fail("Invalid camera motion blur settings");
    return true;
}

// std140, binding 6. Slots: 0=current color, 1=history color, 2=current depth,
// 3=previous depth. Depths are conventional OpenGL [0,1], clear 1, never reversed.
// Current color/depth and history depth use their camera's jittered raster grid.
// TAA history COLOR/output uses the stable unjittered display grid. Previous
// jittered VP therefore addresses history depth, previous unjittered VP addresses
// history color. All history matrices refer to the LAST EXECUTED image, not a
// merely recorded frame. Reset history after resize, cuts or content changes.
struct alignas(16) TemporalConstants {
    glm::mat4 currentInverseViewProjection{1}; // current jittered inverse VP
    glm::mat4 previousViewProjection{1};       // previous jittered VP
    glm::mat4 previousInverseViewProjection{1};
    glm::mat4 currentUnjitteredViewProjection{1};
    glm::mat4 previousUnjitteredViewProjection{1};
    glm::vec4 temporal{0.9f, 0.05f, 0.01f, 0}; // history weight, abs/relative rejection, valid
    glm::vec4 screen{1, 1, 0.125f, 0.0312f};   // inverse width/height, FXAA thresholds
    glm::vec4 motion{1, 32, 12, 0};            // shutter/delta*strength, span pixels, samples, valid
    // xyz=current world camera; w=1 only for blur whose input color has already
    // been resolved by TAA. Other passes ignore w; default/raw color uses 0.
    glm::vec4 cameraPosition{0, 0, 0, 0};
    // x=opaque snapshot available, y=HDR difference sensitivity. When enabled,
    // TAA history alpha stores current transparent reactivity, not scene opacity.
    glm::vec4 reactive{0, 8, 0, 0};
};
static_assert(std::is_standard_layout_v<TemporalConstants>);
static_assert(std::is_trivially_copyable_v<TemporalConstants>);
static_assert(offsetof(TemporalConstants, currentInverseViewProjection) == 0);
static_assert(offsetof(TemporalConstants, previousViewProjection) == 64);
static_assert(offsetof(TemporalConstants, previousInverseViewProjection) == 128);
static_assert(offsetof(TemporalConstants, currentUnjitteredViewProjection) == 192);
static_assert(offsetof(TemporalConstants, previousUnjitteredViewProjection) == 256);
static_assert(offsetof(TemporalConstants, temporal) == 320);
static_assert(offsetof(TemporalConstants, screen) == 336);
static_assert(offsetof(TemporalConstants, motion) == 352);
static_assert(offsetof(TemporalConstants, cameraPosition) == 368);
static_assert(offsetof(TemporalConstants, reactive) == 384);
static_assert(sizeof(TemporalConstants) == 400);

inline TemporalConstants MakeTemporalConstants(const TemporalEffectsSettings& settings) {
    TemporalConstants result;
    result.temporal = {settings.historyWeight, settings.depthRejectAbsolute, settings.depthRejectRelative, 0};
    result.screen.z = settings.fxaaEdgeThreshold;
    result.screen.w = settings.fxaaMinThreshold;
    // The recorder supplies elapsed time, history validity, matrices and size.
    result.motion = {settings.motionBlurStrength, settings.motionBlurMaxPixels, float(settings.motionBlurSamples), 0};
    return result;
}

inline float Halton(std::uint64_t index, std::uint32_t base) {
    if (base < 2) throw std::invalid_argument("Halton base must be at least two");
    double result = 0, fraction = 1;
    while (index) {
        fraction /= base;
        result += fraction * double(index % base);
        index /= base;
    }
    return static_cast<float>(result);
}

// UV displacement, centered within +/- half a pixel. Advance frameIndex only
// when the corresponding history frame executes; repeat an eight-sample cycle.
inline glm::vec2 TemporalJitter(std::uint64_t frameIndex, std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) throw std::invalid_argument("Temporal jitter requires nonzero dimensions");
    const auto index = frameIndex % TemporalJitterPeriod + 1;
    return {(Halton(index, 2) - 0.5f) / width, (Halton(index, 3) - 0.5f) / height};
}

// Adds 2*jitterUV*clip.w to clip.xy, so the convention works for perspective
// and orthographic projections without assuming a particular matrix element.
inline glm::mat4 JitterProjection(glm::mat4 projection, glm::vec2 jitterUV) {
    if (!std::isfinite(jitterUV.x) || !std::isfinite(jitterUV.y))
        throw std::invalid_argument("Temporal jitter must be finite");
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(projection[column][row])) throw std::invalid_argument("Projection must be finite");
        projection[column][0] += 2.0f * jitterUV.x * projection[column][3];
        projection[column][1] += 2.0f * jitterUV.y * projection[column][3];
    }
    return projection;
}

} // namespace Render
