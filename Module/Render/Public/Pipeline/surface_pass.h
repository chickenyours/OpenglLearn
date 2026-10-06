#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <glm/glm.hpp>

namespace Render {
inline constexpr std::uint32_t SurfacePassBinding = 7;
inline constexpr std::uint32_t OpaqueColorTextureSlot = 8;
inline constexpr std::uint32_t OpaqueDepthTextureSlot = 9;
inline constexpr std::uint32_t DiffuseIrradianceTextureSlot = 15;

// View-local snapshots must match view/projection, viewport and jitter. They
// contain only opaque/cutout rendering and may never alias active attachments.
struct alignas(16) SurfacePassConstants {
    glm::mat4 view{1};
    glm::mat4 projection{1};
    // inverse width, inverse height, time in seconds, opaque snapshots ready.
    glm::vec4 screenTime{0};
    // Screen irradiance, hybrid reflections, direct-only GI capture, reserved.
    glm::vec4 options{0};
    // Main raster UV offset to the unjittered GI/reflection grids. Optical
    // snapshots retain the main projection/jitter and do not use this offset.
    // z: LumenLightingView (0=material, 1=surface cache, 2=indirect, 3=direct).
    glm::vec4 screenJitter{0};
};
static_assert(std::is_trivially_copyable_v<SurfacePassConstants>);
static_assert(offsetof(SurfacePassConstants, view) == 0);
static_assert(offsetof(SurfacePassConstants, projection) == 64);
static_assert(offsetof(SurfacePassConstants, screenTime) == 128);
static_assert(offsetof(SurfacePassConstants, options) == 144);
static_assert(offsetof(SurfacePassConstants,screenJitter)==160);
static_assert(sizeof(SurfacePassConstants) == 176);

inline std::string SurfacePassGLSL() {
    return R"GLSL(
layout(std140,binding=7) uniform SurfacePassData {
    mat4 surfaceView;
    mat4 surfaceProjection;
    vec4 surfaceScreenTime;
    vec4 surfaceOptions;
    vec4 surfaceScreenJitter;
};
)GLSL";
}
} // namespace Render
