#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Render {

inline constexpr std::uint32_t SceneEffectsBinding = 4;
inline constexpr std::uint32_t ShadowAtlasTextureSlot = 12;
inline constexpr std::uint32_t ReflectionTextureSlot = 13;
inline constexpr std::uint32_t AmbientOcclusionTextureSlot = 14;
inline constexpr std::uint32_t ShadowCascadeCount = 4;

// Pass-owned data, independent of any material instance. Upload this block in
// chunks of at most MaxInlineUniformBytes; its layout exceeds one inline update.
struct alignas(16) SceneEffectsConstants {
    std::array<glm::mat4, 4> lightViewProjection{
        glm::mat4(1), glm::mat4(1), glm::mat4(1), glm::mat4(1)};
    glm::mat4 mainView{1};
    glm::mat4 reflectionViewProjection{1};
    glm::vec4 cascadeSplits{0};
    // xyz: direction in which sunlight travels; w: radiance multiplier.
    glm::vec4 sunDirectionIntensity{-0.5773503f, -0.5773503f, -0.5773503f, 1.0f};
    glm::vec4 sunColor{1.0f, 0.97f, 0.92f, 0.0f};
    // World-space plane: keep dot(plane, vec4(worldPosition,1)) >= 0.
    glm::vec4 clipPlane{0.0f, 1.0f, 0.0f, 0.0f};
    // shadowsEnabled, reflectionEnabled, clipEnabled, full atlas resolution.
    glm::vec4 effects{0.0f, 0.0f, 0.0f, 2048.0f};
    // normalized depth bias, world normal offset, cascade blend fraction,
    // reflection strength multiplier. Match z to BuildCascades' cascadeBlend.
    glm::vec4 shadowParams{0.0008f, 0.025f, 0.1f, 1.0f};
    // inverse viewport width/height, main-view SSAO enabled, reserved.
    glm::vec4 screenAndAo{0.0f};
};
static_assert(std::is_trivially_copyable_v<SceneEffectsConstants>);
static_assert(offsetof(SceneEffectsConstants, lightViewProjection) == 0);
static_assert(offsetof(SceneEffectsConstants, mainView) == 256);
static_assert(offsetof(SceneEffectsConstants, reflectionViewProjection) == 320);
static_assert(offsetof(SceneEffectsConstants, cascadeSplits) == 384);
static_assert(offsetof(SceneEffectsConstants, sunDirectionIntensity) == 400);
static_assert(offsetof(SceneEffectsConstants, sunColor) == 416);
static_assert(offsetof(SceneEffectsConstants, clipPlane) == 432);
static_assert(offsetof(SceneEffectsConstants, effects) == 448);
static_assert(offsetof(SceneEffectsConstants, shadowParams) == 464);
static_assert(offsetof(SceneEffectsConstants, screenAndAo) == 480);
static_assert(sizeof(SceneEffectsConstants) == 496);

struct CascadeShadowData {
    std::array<glm::mat4, 4> lightViewProjection;
    glm::vec4 cascadeSplits{0};
};

namespace SceneEffectsDetail {
inline bool Finite(const glm::mat4& matrix) {
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(matrix[column][row])) return false;
    return true;
}
inline bool Finite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
inline glm::vec4 NormalizePlane(glm::vec4 plane) {
    const float length = glm::length(glm::vec3(plane));
    if (!Finite(glm::vec3(plane)) || !std::isfinite(plane.w) || !std::isfinite(length) || length < 1e-6f)
        throw std::invalid_argument("Reflection plane must have a finite nonzero normal");
    return plane / length;
}
} // namespace SceneEffectsDetail

// OpenGL clip depth [-1,1], right-handed camera view. atlasResolution is the
// whole square 2x2 atlas, so each cascade receives atlasResolution/2 texels.
// Casters outside the camera slice are retained by casterPadding along light Z.
inline CascadeShadowData BuildCascades(
    const glm::mat4& view, const glm::mat4& projection,
    float cameraNear, float cameraFar, const glm::vec3& sunDirection,
    float shadowDistance = 100.0f, float lambda = 0.9f,
    std::uint32_t atlasResolution = 2048, float casterPadding = 30.0f,
    float cascadeBlend = 0.1f) {
    using namespace SceneEffectsDetail;
    if (!Finite(view) || !Finite(projection) || !std::isfinite(cameraNear) ||
        !std::isfinite(cameraFar) || cameraNear <= 0 || cameraFar <= cameraNear ||
        !Finite(sunDirection) || glm::dot(sunDirection, sunDirection) < 1e-12f ||
        !std::isfinite(shadowDistance) || shadowDistance <= cameraNear ||
        !std::isfinite(lambda) || lambda < 0 || lambda > 1 ||
        atlasResolution < 8 || atlasResolution % 2 != 0 ||
        !std::isfinite(casterPadding) || casterPadding < 0 ||
        !std::isfinite(cascadeBlend) || cascadeBlend < 0 || cascadeBlend > 0.45f)
        throw std::invalid_argument("Invalid cascaded shadow camera/settings");
    const glm::mat4 inverseViewProjection = glm::inverse(projection * view);
    if (!Finite(inverseViewProjection))
        throw std::invalid_argument("Cascaded shadow camera matrix is singular");
    std::array<glm::vec3, 4> cameraNearCorners, cameraFarCorners;
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
        const int index = y * 2 + x;
        glm::vec4 nearPoint = inverseViewProjection * glm::vec4(x * 2 - 1, y * 2 - 1, -1, 1);
        glm::vec4 farPoint = inverseViewProjection * glm::vec4(x * 2 - 1, y * 2 - 1, 1, 1);
        if (std::abs(nearPoint.w) < 1e-8f || std::abs(farPoint.w) < 1e-8f)
            throw std::invalid_argument("Cascaded shadow frustum must have a finite far plane");
        cameraNearCorners[index] = glm::vec3(nearPoint) / nearPoint.w;
        cameraFarCorners[index] = glm::vec3(farPoint) / farPoint.w;
    }
    const float farDistance = std::min(cameraFar, shadowDistance);
    std::array<float, 5> planes{cameraNear};
    CascadeShadowData result;
    for (int cascade = 0; cascade < 4; ++cascade) {
        const float fraction = float(cascade + 1) / 4.0f;
        const float logarithmic = cameraNear * std::pow(farDistance / cameraNear, fraction);
        const float linear = cameraNear + (farDistance - cameraNear) * fraction;
        planes[cascade + 1] = lambda * logarithmic + (1.0f - lambda) * linear;
        result.cascadeSplits[cascade] = planes[cascade + 1];
    }
    result.cascadeSplits.w = planes[4] = farDistance;
    const glm::vec3 direction = glm::normalize(sunDirection);
    const glm::vec3 up = std::abs(direction.y) < 0.95f ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
    // Rotation only: snapping is anchored to the world/light basis, not a view
    // translated to the moving camera, which would defeat texel stabilization.
    const glm::mat4 lightView = glm::lookAt(glm::vec3(0), direction, up);
    const float tileResolution = float(atlasResolution / 2);
    for (int cascade = 0; cascade < 4; ++cascade) {
        float sliceNear = planes[cascade];
        if (cascade > 0) {
            // The shader's first transition interval starts at view depth 0,
            // since the pass block does not carry the camera's near distance.
            const float previousStart = cascade == 1 ? 0.0f : planes[cascade - 1];
            sliceNear -= (planes[cascade] - previousStart) * cascadeBlend;
            sliceNear = std::max(sliceNear, cameraNear);
        }
        const float nearRatio = (sliceNear - cameraNear) / (cameraFar - cameraNear);
        const float farRatio = (planes[cascade + 1] - cameraNear) / (cameraFar - cameraNear);
        std::array<glm::vec3, 8> corners;
        glm::vec3 center(0);
        for (int i = 0; i < 4; ++i) {
            corners[i] = glm::mix(cameraNearCorners[i], cameraFarCorners[i], nearRatio);
            corners[i + 4] = glm::mix(cameraNearCorners[i], cameraFarCorners[i], farRatio);
            center += corners[i] + corners[i + 4];
        }
        center /= 8.0f;
        float radius = 0;
        for (const auto& corner : corners) radius = std::max(radius, glm::length(corner - center));
        radius = std::max(std::ceil(radius * 16.0f) / 16.0f, 0.0625f);
        // Reserve a full texel on each side so center rounding never clips a
        // receiver corner and 3x3 PCF has a guard region inside the atlas tile.
        const float unitsPerTexel = 2.0f * radius / (tileResolution - 2.0f);
        const float extent = unitsPerTexel * tileResolution * 0.5f;
        glm::vec3 centerLight = glm::vec3(lightView * glm::vec4(center, 1));
        centerLight.x = std::floor(centerLight.x / unitsPerTexel + 0.5f) * unitsPerTexel;
        centerLight.y = std::floor(centerLight.y / unitsPerTexel + 0.5f) * unitsPerTexel;
        float minimumZ = glm::vec3(lightView * glm::vec4(corners[0], 1)).z;
        float maximumZ = minimumZ;
        for (const auto& corner : corners) {
            const float z = (lightView * glm::vec4(corner, 1)).z;
            minimumZ = std::min(minimumZ, z);
            maximumZ = std::max(maximumZ, z);
        }
        const glm::mat4 lightProjection = glm::ortho(
            centerLight.x - extent, centerLight.x + extent,
            centerLight.y - extent, centerLight.y + extent,
            -maximumZ - casterPadding, -minimumZ + casterPadding);
        result.lightViewProjection[cascade] = lightProjection * lightView;
    }
    return result;
}

inline glm::mat4 ReflectionMatrix(glm::vec4 worldPlane) {
    worldPlane = SceneEffectsDetail::NormalizePlane(worldPlane);
    const glm::vec3 n(worldPlane);
    glm::mat4 result(1);
    for (int column = 0; column < 3; ++column)
        for (int row = 0; row < 3; ++row)
            result[column][row] -= 2.0f * n[column] * n[row];
    result[3] = glm::vec4(-2.0f * worldPlane.w * n, 1);
    return result;
}

struct ReflectionCamera {
    glm::mat4 view{1}, projection{1}, viewProjection{1};
    glm::vec3 position{0};
    glm::vec4 clipPlane{0, 1, 0, 0};
};

// Planar reflection, not SSR. The caller excludes the mirror itself, records a
// reflected scene pass into a separate color target, and enables its clipPlane.
// Reflection reverses winding: use cull-none or reverse the front-face setting.
inline ReflectionCamera ReflectCamera(const glm::mat4& view, const glm::mat4& projection,
                                      glm::vec4 worldPlane) {
    if (!SceneEffectsDetail::Finite(view) || !SceneEffectsDetail::Finite(projection))
        throw std::invalid_argument("Reflection camera must be finite");
    ReflectionCamera result;
    result.clipPlane = SceneEffectsDetail::NormalizePlane(worldPlane);
    const glm::mat4 reflection = ReflectionMatrix(result.clipPlane);
    const glm::mat4 inverseView = glm::inverse(view);
    if (!SceneEffectsDetail::Finite(inverseView))
        throw std::invalid_argument("Reflection view matrix is singular");
    result.view = view * reflection;
    result.projection = projection;
    result.viewProjection = projection * result.view;
    const glm::vec4 position = reflection * inverseView[3];
    result.position = glm::vec3(position) / position.w;
    return result;
}

} // namespace Render
