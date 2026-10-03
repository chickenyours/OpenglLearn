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
    glm::vec4 effects{0.0f, 0.0f, 0.0f, 4096.0f};
    // normalized depth bias, world normal offset, cascade blend fraction,
    // reflection strength multiplier. Match z to BuildCascades' cascadeBlend.
    glm::vec4 shadowParams{0.000002f, 0.0f, 0.1f, 1.0f};
    // inverse viewport width/height, main-view SSAO enabled,
    // shadow debug mode: 0=none, 1=visibility, 2=cascade index.
    glm::vec4 screenAndAo{0.0f};
    glm::vec4 cascadeWorldTexelSize{0.0f};
    glm::vec4 cascadeInverseDepthRange{0.0f};
    // Depth bias texels, normal bias texels, far-distance fade fraction,
    // Receiver-plane safety cap in texels before scaling by max(1,tan(faceAngle)).
    // This bounds each tap's correction, not an additional constant depth bias.
    glm::vec4 shadowFilter{0.05f, 0.20f, 0.10f, 4.0f};
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
static_assert(offsetof(SceneEffectsConstants, cascadeWorldTexelSize) == 496);
static_assert(offsetof(SceneEffectsConstants, cascadeInverseDepthRange) == 512);
static_assert(offsetof(SceneEffectsConstants, shadowFilter) == 528);
static_assert(sizeof(SceneEffectsConstants) == 544);

struct CascadeShadowData {
    std::array<glm::mat4, 4> lightViewProjection;
    glm::vec4 cascadeSplits{0};
    glm::vec4 cascadeWorldTexelSize{0};
    glm::vec4 cascadeInverseDepthRange{0};
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
inline bool Finite(const glm::dmat4& matrix) {
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(matrix[column][row])) return false;
    return true;
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
// Fit the receiver sphere in camera space so camera translation/rotation cannot
// change its radius. Snap its world-space light center to a fixed texel grid.
// casterPadding extends only toward the light, where off-slice casters matter.
// filterGuardBandTexels includes the filter's complete footprint (including its
// fractional/bilinear reach); an additional half texel accommodates grid snap.
// Inputs and fitting use double precision, but the returned GPU matrices remain
// float. Very large worlds still need an origin-relative rendering convention.
inline CascadeShadowData BuildCascades(
    const glm::mat4& view, const glm::mat4& projection,
    float cameraNear, float cameraFar, const glm::vec3& sunDirection,
    float shadowDistance = 100.0f, float lambda = 0.65f,
    std::uint32_t atlasResolution = 4096, float casterPadding = 30.0f,
    float cascadeBlend = 0.1f, float filterGuardBandTexels = 3.0f) {
    using namespace SceneEffectsDetail;
    if (!Finite(view) || !Finite(projection) || !std::isfinite(cameraNear) ||
        !std::isfinite(cameraFar) || cameraNear <= 0 || cameraFar <= cameraNear ||
        !Finite(sunDirection) || glm::dot(glm::dvec3(sunDirection), glm::dvec3(sunDirection)) == 0.0 ||
        !std::isfinite(shadowDistance) || shadowDistance <= cameraNear ||
        !std::isfinite(lambda) || lambda < 0 || lambda > 1 ||
        atlasResolution < 8 || atlasResolution % 2 != 0 ||
        !std::isfinite(casterPadding) || casterPadding < 0 ||
        !std::isfinite(cascadeBlend) || cascadeBlend < 0 || cascadeBlend > 0.45f ||
        !std::isfinite(filterGuardBandTexels) || filterGuardBandTexels < 0 ||
        double(atlasResolution / 2) <= 2.0 * (double(filterGuardBandTexels) + 0.5))
        throw std::invalid_argument("Invalid cascaded shadow camera/settings");
    const glm::dmat4 inverseProjection = glm::inverse(glm::dmat4(projection));
    const glm::dmat4 inverseView = glm::inverse(glm::dmat4(view));
    if (!Finite(inverseProjection) || !Finite(inverseView))
        throw std::invalid_argument("Cascaded shadow camera matrix is singular");
    if (std::abs(inverseView[0][3]) > 1e-8 || std::abs(inverseView[1][3]) > 1e-8 ||
        std::abs(inverseView[2][3]) > 1e-8 || std::abs(inverseView[3][3] - 1.0) > 1e-8)
        throw std::invalid_argument("Cascaded shadow view must be affine");
    std::array<glm::dvec3, 4> cameraNearCorners, cameraFarCorners;
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
        const int index = y * 2 + x;
        const glm::dvec4 nearPoint = inverseProjection * glm::dvec4(x * 2 - 1, y * 2 - 1, -1, 1);
        const glm::dvec4 farPoint = inverseProjection * glm::dvec4(x * 2 - 1, y * 2 - 1, 1, 1);
        if (std::abs(nearPoint.w) < 1e-14 || std::abs(farPoint.w) < 1e-14)
            throw std::invalid_argument("Cascaded shadow frustum must have a finite far plane");
        cameraNearCorners[index] = glm::dvec3(nearPoint) / nearPoint.w;
        cameraFarCorners[index] = glm::dvec3(farPoint) / farPoint.w;
        if (cameraFarCorners[index].z >= cameraNearCorners[index].z)
            throw std::invalid_argument("Cascaded shadow projection must use right-handed forward depth");
    }
    const double farDistance = std::min(cameraFar, shadowDistance);
    std::array<double, 5> planes{cameraNear};
    CascadeShadowData result;
    for (int cascade = 0; cascade < 4; ++cascade) {
        const double fraction = double(cascade + 1) / 4.0;
        const double logarithmic = cameraNear * std::pow(farDistance / cameraNear, fraction);
        const double linear = cameraNear + (farDistance - cameraNear) * fraction;
        // Fit to the very same float splits that the shader will consume.
        result.cascadeSplits[cascade] = static_cast<float>(lambda * logarithmic + (1.0 - lambda) * linear);
        planes[cascade + 1] = result.cascadeSplits[cascade];
    }
    result.cascadeSplits.w = static_cast<float>(planes[4] = farDistance);
    const glm::dvec3 direction = glm::normalize(glm::dvec3(sunDirection));
    const glm::dvec3 up = std::abs(direction.y) < 0.95 ? glm::dvec3(0, 1, 0) : glm::dvec3(0, 0, 1);
    // Rotation only: snapping is anchored to the world/light basis, not a view
    // translated to the moving camera, which would defeat texel stabilization.
    const glm::dmat4 lightView = glm::lookAt(glm::dvec3(0), direction, up);
    const double tileResolution = double(atlasResolution / 2);
    // A conservative spectral-norm bound also supports affine scaled/sheared
    // cameras. Use one fixed envelope around unit stretch so float lookAt
    // roundoff cannot change the fit as an otherwise rigid camera rotates.
    const glm::dmat3 linearView(inverseView);
    const glm::dmat3 gram = glm::transpose(linearView) * linearView;
    double stretchSquared = 0;
    for (int row = 0; row < 3; ++row) {
        double rowSum = 0;
        for (int column = 0; column < 3; ++column) rowSum += std::abs(gram[column][row]);
        stretchSquared = std::max(stretchSquared, rowSum);
    }
    double stretch = std::sqrt(stretchSquared);
    if (std::abs(stretch - 1.0) < 1e-5) stretch = 1.00001;
    for (int cascade = 0; cascade < 4; ++cascade) {
        double sliceNear = planes[cascade];
        if (cascade > 0) {
            // The shader's first transition interval starts at view depth 0,
            // since the pass block does not carry the camera's near distance.
            const double previousStart = cascade == 1 ? 0.0 : planes[cascade - 1];
            sliceNear -= (planes[cascade] - previousStart) * cascadeBlend;
            sliceNear = std::max(sliceNear, double(cameraNear));
        }
        std::array<glm::dvec3, 8> cameraCorners, corners;
        glm::dvec3 cameraCenter(0);
        for (int i = 0; i < 4; ++i) {
            const auto ray = cameraFarCorners[i] - cameraNearCorners[i];
            // Intersect each frustum edge with exact camera-depth planes. This
            // avoids amplifying the float projection's far-plane roundoff.
            cameraCorners[i] = cameraNearCorners[i] + ray * ((-sliceNear - cameraNearCorners[i].z) / ray.z);
            cameraCorners[i + 4] = cameraNearCorners[i] + ray * ((-planes[cascade + 1] - cameraNearCorners[i].z) / ray.z);
            cameraCenter += cameraCorners[i] + cameraCorners[i + 4];
        }
        cameraCenter /= 8.0;
        double radius = 0;
        for (int i = 0; i < 8; ++i) {
            radius = std::max(radius, glm::length(cameraCorners[i] - cameraCenter));
            corners[i] = glm::dvec3(inverseView * glm::dvec4(cameraCorners[i], 1));
        }
        radius = std::max(std::ceil(radius * stretch * 16.0) / 16.0, 0.0625);
        const double unitsPerTexel = 2.0 * radius / (tileResolution - 2.0 * (filterGuardBandTexels + 0.5));
        const double extent = unitsPerTexel * tileResolution * 0.5;
        glm::dvec3 centerLight(lightView * inverseView * glm::dvec4(cameraCenter, 1));
        centerLight.x = std::floor(centerLight.x / unitsPerTexel + 0.5) * unitsPerTexel;
        centerLight.y = std::floor(centerLight.y / unitsPerTexel + 0.5) * unitsPerTexel;
        double minimumZ = (lightView * glm::dvec4(corners[0], 1)).z;
        double maximumZ = minimumZ;
        for (const auto& corner : corners) {
            const double z = (lightView * glm::dvec4(corner, 1)).z;
            minimumZ = std::min(minimumZ, z);
            maximumZ = std::max(maximumZ, z);
        }
        const double depthGuard = std::max(0.0001, unitsPerTexel * 0.5);
        minimumZ -= depthGuard;
        maximumZ += casterPadding + depthGuard;
        const glm::dmat4 lightProjection = glm::ortho(
            centerLight.x - extent, centerLight.x + extent,
            centerLight.y - extent, centerLight.y + extent,
            -maximumZ, -minimumZ);
        result.lightViewProjection[cascade] = glm::mat4(lightProjection * lightView);
        result.cascadeWorldTexelSize[cascade] = static_cast<float>(unitsPerTexel);
        result.cascadeInverseDepthRange[cascade] = static_cast<float>(1.0 / (maximumZ - minimumZ));
        if (!Finite(result.lightViewProjection[cascade]) ||
            !std::isfinite(result.cascadeWorldTexelSize[cascade]) || result.cascadeWorldTexelSize[cascade] <= 0 ||
            !std::isfinite(result.cascadeInverseDepthRange[cascade]) || result.cascadeInverseDepthRange[cascade] <= 0)
            throw std::invalid_argument("Cascaded shadow fit exceeds float GPU precision/range");
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
