#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "Render/Public/Pipeline/scene_effects.h"

namespace {

bool Near(float a, float b, float tolerance = 1e-4f) {
    return std::abs(a - b) <= tolerance;
}

void AssertMatrixNear(const glm::mat4& a, const glm::mat4& b, float tolerance = 1e-4f) {
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            assert(Near(a[column][row], b[column][row], tolerance));
}

template <class F>
void AssertInvalid(F&& action) {
    bool rejected = false;
    try { action(); } catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
}

void CheckSplitsAndReceiverCoverage() {
    constexpr float nearPlane = 0.1f, farPlane = 300.0f, shadowDistance = 80.0f;
    const glm::mat4 view = glm::lookAt(glm::vec3(4, 6, 9), glm::vec3(0), glm::vec3(0, 1, 0));
    const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, nearPlane, farPlane);
    const glm::vec3 direction(-1, -2, -1);
    const auto shadows = Render::BuildCascades(view, projection, nearPlane, farPlane, direction,
                                              shadowDistance, 0.9f, 2048, 30, 0.1f);
    float previous = nearPlane;
    float previousPrevious = nearPlane;
    const glm::mat4 inverse = glm::inverse(projection * view);
    for (int cascade = 0; cascade < 4; ++cascade) {
        const float farDistance = shadows.cascadeSplits[cascade];
        assert(farDistance > previous && farDistance <= shadowDistance);
        const float ratio = float(cascade + 1) / 4;
        const float expected = 0.9f * nearPlane * std::pow(shadowDistance / nearPlane, ratio)
                             + 0.1f * (nearPlane + (shadowDistance - nearPlane) * ratio);
        assert(Near(farDistance, expected, 1e-4f));
        assert(Render::SceneEffectsDetail::Finite(shadows.lightViewProjection[cascade]));
        // Include the overlap required by blending into the next cascade.
        const float transitionStart = cascade == 1 ? 0.0f : previousPrevious;
        const float nearDistance = std::max(nearPlane,
            previous - (cascade ? (previous - transitionStart) * 0.1f : 0));
        for (int y = -1; y <= 1; y += 2) for (int x = -1; x <= 1; x += 2) {
            const glm::vec4 nearCorner = inverse * glm::vec4(float(x), float(y), -1, 1);
            const glm::vec4 farCorner = inverse * glm::vec4(float(x), float(y), 1, 1);
            for (float distance : {nearDistance, farDistance}) {
                const glm::vec3 corner = glm::mix(glm::vec3(nearCorner) / nearCorner.w,
                    glm::vec3(farCorner) / farCorner.w, (distance - nearPlane) / (farPlane - nearPlane));
                glm::vec4 clip = shadows.lightViewProjection[cascade] * glm::vec4(corner, 1);
                clip /= clip.w;
                assert(std::abs(clip.x) <= 1.0001f);
                assert(std::abs(clip.y) <= 1.0001f);
                assert(std::abs(clip.z) <= 1.0001f);
            }
        }
        previousPrevious = previous;
        previous = farDistance;
    }
    assert(shadows.cascadeSplits.w == shadowDistance);
    const auto bounded = Render::BuildCascades(view, projection, nearPlane, farPlane, direction, 500);
    assert(bounded.cascadeSplits.w == farPlane);
    const auto linear = Render::BuildCascades(view, projection, nearPlane, farPlane, direction,
                                             shadowDistance, 0);
    const auto logarithmic = Render::BuildCascades(view, projection, nearPlane, farPlane, direction,
                                                  shadowDistance, 1);
    assert(Near(linear.cascadeSplits.x, nearPlane + (shadowDistance - nearPlane) * 0.25f));
    assert(Near(logarithmic.cascadeSplits.x, nearPlane * std::pow(shadowDistance / nearPlane, 0.25f)));
}

void CheckStableTexelsAndVerticalSun() {
    const glm::vec3 eye(0, 3, 6), target(0);
    const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
    const auto original = Render::BuildCascades(glm::lookAt(eye, target, glm::vec3(0, 1, 0)),
        projection, 0.1f, 100, glm::vec3(0, -1, 0));
    const glm::vec3 smallTranslation(0.00001f, 0, 0);
    const auto moved = Render::BuildCascades(glm::lookAt(eye + smallTranslation, target + smallTranslation,
        glm::vec3(0, 1, 0)), projection, 0.1f, 100, glm::vec3(0, -1, 0));
    for (int cascade = 0; cascade < 4; ++cascade) {
        assert(Render::SceneEffectsDetail::Finite(original.lightViewProjection[cascade]));
        // A tiny translation parallel to the light's image plane must not move
        // its receiver UVs continuously; XY remains on the same texel grid.
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 2; ++row)
                assert(Near(original.lightViewProjection[cascade][column][row],
                            moved.lightViewProjection[cascade][column][row], 1e-6f));
    }
}

// Independent receiver geometry for a conventional perspective camera. This
// tests projection coverage rather than repeating BuildCascades' unprojection.
std::array<glm::dvec3, 8> ReceiverCorners(const glm::mat4& view,
    const Render::CascadeShadowData& shadows, int cascade, double nearPlane,
    double verticalFov, double aspect, double blend) {
    const double previous = cascade == 0 ? nearPlane : shadows.cascadeSplits[cascade - 1];
    const double previousStart = cascade <= 1 ? 0.0 : shadows.cascadeSplits[cascade - 2];
    const double start = cascade == 0 ? nearPlane : std::max(nearPlane, previous - (previous - previousStart) * blend);
    const double end = shadows.cascadeSplits[cascade];
    const glm::dmat4 inverseView = glm::inverse(glm::dmat4(view));
    std::array<glm::dvec3, 8> corners;
    int index = 0;
    for (double distance : {start, end})
        for (double y : {-1.0, 1.0}) for (double x : {-1.0, 1.0}) {
            const double halfHeight = std::tan(verticalFov * 0.5) * distance;
            corners[index++] = glm::dvec3(inverseView *
                glm::dvec4(x * halfHeight * aspect, y * halfHeight, -distance, 1));
        }
    return corners;
}

void AssertReceiverCoverage(const glm::mat4& view, const Render::CascadeShadowData& shadows,
    float fov, float aspect, float nearPlane, float blend, unsigned atlas, float guard,
    float tolerance = 0.0001f) {
    const float edge = 1.0f - 2.0f * guard / float(atlas / 2);
    for (int cascade = 0; cascade < 4; ++cascade) {
        const auto corners = ReceiverCorners(view, shadows, cascade, nearPlane, fov, aspect, blend);
        for (const auto& world : corners) {
            // Deliberately use float arithmetic here, matching the GPU output.
            const glm::vec4 clip = shadows.lightViewProjection[cascade] * glm::vec4(glm::vec3(world), 1);
            assert(std::abs(clip.x / clip.w) <= edge + tolerance);
            assert(std::abs(clip.y / clip.w) <= edge + tolerance);
            assert(std::abs(clip.z / clip.w) <= 1.0f + tolerance);
        }
    }
}

void CheckMetricsGuardAndCasterRange() {
    const float fov = glm::radians(60.0f), aspect = 16.0f / 9;
    const glm::mat4 view = glm::lookAt(glm::vec3(4, 6, 9), glm::vec3(0), glm::vec3(0, 1, 0));
    const glm::mat4 projection = glm::perspective(fov, aspect, 0.1f, 300.0f);
    const glm::vec3 sun(-1, -2, -1);
    const auto fit = Render::BuildCascades(view, projection, 0.1f, 300, sun, 80, 0.65f, 4096, 30, 0.1f, 3);
    const auto noFilter = Render::BuildCascades(view, projection, 0.1f, 300, sun, 80, 0.65f, 4096, 30, 0.1f, 0);
    const auto noCasters = Render::BuildCascades(view, projection, 0.1f, 300, sun, 80, 0.65f, 4096, 0, 0.1f, 3);
    const auto widerFilter = Render::BuildCascades(view, projection, 0.1f, 300, sun, 80, 0.65f, 4096, 30, 0.1f, 8);
    AssertReceiverCoverage(view, fit, fov, aspect, 0.1f, 0.1f, 4096, 3);
    AssertReceiverCoverage(view, widerFilter, fov, aspect, 0.1f, 0.1f, 4096, 8);
    for (int cascade = 0; cascade < 4; ++cascade) {
        const auto& matrix = fit.lightViewProjection[cascade];
        const float xyScale = glm::length(glm::vec3(matrix[0][0], matrix[1][0], matrix[2][0]));
        const float depthScale = glm::length(glm::vec3(matrix[0][2], matrix[1][2], matrix[2][2]));
        assert(fit.cascadeWorldTexelSize[cascade] > 0 && fit.cascadeInverseDepthRange[cascade] > 0);
        assert(Near(fit.cascadeWorldTexelSize[cascade], 2.0f / (2048 * xyScale), 1e-6f));
        assert(Near(fit.cascadeInverseDepthRange[cascade], depthScale * 0.5f, 1e-7f));
        assert(Near(fit.cascadeWorldTexelSize[cascade] / noFilter.cascadeWorldTexelSize[cascade],
                    (2048.0f - 1.0f) / (2048.0f - 7.0f), 1e-6f));
        assert(widerFilter.cascadeWorldTexelSize[cascade] > fit.cascadeWorldTexelSize[cascade]);
        const float paddedRange = 1.0f / fit.cascadeInverseDepthRange[cascade];
        const float receiverRange = 1.0f / noCasters.cascadeInverseDepthRange[cascade];
        assert(Near(paddedRange - receiverRange, 30.0f, 0.0001f)); // Padding is one-sided, not 60.
        float farthestReceiver = -1;
        glm::vec3 downstreamReceiver(0);
        for (const auto& world : ReceiverCorners(view, fit, cascade, 0.1, fov, aspect, 0.1)) {
            const glm::vec3 receiver(world);
            const glm::vec4 upstream = matrix * glm::vec4(receiver - glm::normalize(sun) * 30.0f, 1);
            assert(std::abs(upstream.z) <= 1.0001f); // Off-frustum casters toward the sun remain inside.
            const float depth = (matrix * glm::vec4(receiver, 1)).z;
            if (depth > farthestReceiver) { farthestReceiver = depth; downstreamReceiver = receiver; }
        }
        assert((matrix * glm::vec4(downstreamReceiver + glm::normalize(sun) * 30.0f, 1)).z > 1.0f);
    }
    const Render::SceneEffectsConstants defaults;
    assert(sizeof(defaults) == 544 && defaults.effects.w == 4096);
    assert(defaults.shadowParams.x == 0.000002f && defaults.shadowParams.y == 0);
    assert(defaults.shadowFilter == glm::vec4(0.05f, 0.20f, 0.10f, 4.0f));
    const auto defaultFit = Render::BuildCascades(view, projection, 0.1f, 300, sun, 80);
    assert(defaultFit.cascadeSplits == fit.cascadeSplits);
    assert(defaultFit.cascadeWorldTexelSize == fit.cascadeWorldTexelSize);
}

void CheckRotationGridAndLargeCoordinates() {
    const float fov = glm::radians(55.0f), aspect = 1.5f;
    const glm::mat4 projection = glm::perspective(fov, aspect, 0.2f, 200.0f);
    const glm::vec3 eye(2, 3, 6), sun(-1, -2, -3);
    const auto viewForAngle = [&](float angle) {
        return glm::lookAt(eye, eye + glm::vec3(std::sin(angle), -0.25f, -std::cos(angle)), glm::vec3(0, 1, 0));
    };
    const auto view = viewForAngle(0);
    const auto baseline = Render::BuildCascades(view, projection, 0.2f, 200, sun, 80);
    for (float angle : {-1.5f, -0.25f, -0.0001f, 0.0001f, 0.3f, 1.25f}) {
        const auto rotatedView = viewForAngle(angle);
        const auto rotated = Render::BuildCascades(rotatedView, projection, 0.2f, 200, sun, 80);
        // Radius/texel scale must not jump between 1/16-unit fit bins as the
        // camera rotates. Only center and the tightly fitted depth range move.
        assert(rotated.cascadeWorldTexelSize == baseline.cascadeWorldTexelSize);
        for (int cascade = 0; cascade < 4; ++cascade)
            for (int column = 0; column < 3; ++column)
                for (int row = 0; row < 2; ++row)
                    assert(Near(rotated.lightViewProjection[cascade][column][row],
                                baseline.lightViewProjection[cascade][column][row], 1e-7f));
        AssertReceiverCoverage(rotatedView, rotated, fov, aspect, 0.2f, 0.1f, 4096, 3);
    }
    const glm::vec3 lightRight = glm::normalize(glm::cross(glm::normalize(sun), glm::vec3(0, 1, 0)));
    for (int step = -5; step <= 5; ++step) {
        const glm::vec3 translation = lightRight * (baseline.cascadeWorldTexelSize.x * step * 0.21f);
        const glm::mat4 translatedView = glm::translate(view, -translation);
        const auto translated = Render::BuildCascades(translatedView, projection, 0.2f, 200, sun, 80);
        assert(translated.cascadeWorldTexelSize == baseline.cascadeWorldTexelSize);
        for (int cascade = 0; cascade < 4; ++cascade) {
            const float pixels = (translated.lightViewProjection[cascade][3][0] -
                                  baseline.lightViewProjection[cascade][3][0]) * 1024.0f;
            assert(Near(pixels, std::round(pixels), 0.0001f));
        }
    }
    const glm::mat4 distantView = glm::translate(view, glm::vec3(-100000, 75000, -25000));
    const auto distant = Render::BuildCascades(distantView, projection, 0.2f, 200, sun, 80);
    assert(distant.cascadeWorldTexelSize == baseline.cascadeWorldTexelSize);
    // The output is deliberately still float, so allow GPU cancellation error
    // at large translations while requiring all receiver corners to remain in range.
    AssertReceiverCoverage(distantView, distant, fov, aspect, 0.2f, 0.1f, 4096, 0, 0.005f);
    const auto hugeSun = Render::BuildCascades(view, projection, 0.2f, 200, sun * 1e30f, 80);
    for (int cascade = 0; cascade < 4; ++cascade)
        AssertMatrixNear(hugeSun.lightViewProjection[cascade], baseline.lightViewProjection[cascade], 1e-5f);
    for (const auto& direction : {glm::vec3(0, -1, 0), glm::vec3(1e-7f, -1, 1e-7f), glm::vec3(0, 1, 0)}) {
        const auto vertical = Render::BuildCascades(view, projection, 0.2f, 200, direction, 80);
        AssertReceiverCoverage(view, vertical, fov, aspect, 0.2f, 0.1f, 4096, 3);
    }
    auto affineView = glm::scale(view, glm::vec3(0.8f, 1.2f, 1.0f));
    affineView[1][0] += 0.15f;
    const auto affine = Render::BuildCascades(affineView, projection, 0.2f, 200, sun, 80);
    AssertReceiverCoverage(affineView, affine, fov, aspect, 0.2f, 0.1f, 4096, 3);
}

void CheckReflection() {
    const glm::vec4 plane(0, 2, 0, -4); // y = 2; accepts unnormalized planes.
    const glm::mat4 reflection = Render::ReflectionMatrix(plane);
    AssertMatrixNear(reflection * reflection, glm::mat4(1));
    const glm::vec4 reflected = reflection * glm::vec4(3, 5, -7, 1);
    assert(Near(reflected.x, 3) && Near(reflected.y, -1) && Near(reflected.z, -7));
    const glm::vec4 fixed = reflection * glm::vec4(3, 2, -7, 1);
    assert(Near(fixed.y, 2));
    assert(Near(glm::determinant(reflection), -1));
    AssertMatrixNear(reflection, Render::ReflectionMatrix(-plane));

    const glm::mat4 view = glm::lookAt(glm::vec3(3, 5, 7), glm::vec3(0), glm::vec3(0, 1, 0));
    const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
    const auto camera = Render::ReflectCamera(view, projection, plane);
    assert(glm::length(camera.position - glm::vec3(3, -1, 7)) < 1e-4f);
    assert(camera.clipPlane == glm::vec4(0, 1, 0, -2));
    AssertMatrixNear(camera.projection, projection);
    AssertMatrixNear(camera.viewProjection, projection * view * reflection);
    const glm::vec4 eyeInView = camera.view * glm::vec4(camera.position, 1);
    assert(glm::length(glm::vec3(eyeInView)) < 1e-4f);
    const auto twice = Render::ReflectCamera(camera.view, projection, plane);
    AssertMatrixNear(twice.view, view);

    const glm::vec4 tiltedPlane(1, 2, -3, -4);
    const glm::vec4 point(8, -3, 2, 1);
    const auto tilted = Render::ReflectionMatrix(tiltedPlane);
    assert(Near(glm::dot(tiltedPlane, tilted * point), -glm::dot(tiltedPlane, point)));
}

void CheckInvalidInputs() {
    const glm::mat4 view(1);
    const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
    AssertInvalid([&] { Render::BuildCascades(view, projection, 0, 100, glm::vec3(0, -1, 0)); });
    AssertInvalid([&] { Render::BuildCascades(view, projection, 1, 1, glm::vec3(0, -1, 0)); });
    AssertInvalid([&] { Render::BuildCascades(view, projection, 0.1f, 100, glm::vec3(0)); });
    AssertInvalid([&] { Render::BuildCascades(view, projection, 0.1f, 100, glm::vec3(0, -1, 0), 80, 0.9f, 7); });
    AssertInvalid([&] { Render::BuildCascades(view, projection, 0.1f, 100, glm::vec3(0, -1, 0), 80, 1.1f); });
    AssertInvalid([&] { Render::BuildCascades(view, glm::mat4(0), 0.1f, 100, glm::vec3(0, -1, 0)); });
    AssertInvalid([&] { Render::BuildCascades(view, projection, 0.1f, 100, glm::vec3(0, -1, 0), 80, 0.65f, 4096, 30, 0.1f, -1); });
    AssertInvalid([&] { Render::BuildCascades(view, projection, 0.1f, 100, glm::vec3(0, -1, 0), 80, 0.65f, 4096, 30, 0.1f, 1024); });
    AssertInvalid([&] { Render::BuildCascades(view, projection, 0.1f, 100, glm::vec3(0, -1, 0), 80, 0.65f, 4096, 30, 0.1f,
        std::numeric_limits<float>::quiet_NaN()); });
    auto projectiveView = view; projectiveView[0][3] = 0.1f;
    AssertInvalid([&] { Render::BuildCascades(projectiveView, projection, 0.1f, 100, glm::vec3(0, -1, 0)); });
    AssertInvalid([&] { Render::ReflectionMatrix(glm::vec4(0)); });
    AssertInvalid([&] { Render::ReflectionMatrix(glm::vec4(0, 1, 0, std::numeric_limits<float>::infinity())); });
    AssertInvalid([&] { Render::ReflectCamera(glm::mat4(0), projection, glm::vec4(0, 1, 0, 0)); });
}

} // namespace

int main() {
    CheckSplitsAndReceiverCoverage();
    CheckStableTexelsAndVerticalSun();
    CheckMetricsGuardAndCasterRange();
    CheckRotationGridAndLargeCoordinates();
    CheckReflection();
    CheckInvalidInputs();
    std::cout << "scene_effects_test passed\n";
}
