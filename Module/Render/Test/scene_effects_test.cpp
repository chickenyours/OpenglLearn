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
    AssertInvalid([&] { Render::ReflectionMatrix(glm::vec4(0)); });
    AssertInvalid([&] { Render::ReflectionMatrix(glm::vec4(0, 1, 0, std::numeric_limits<float>::infinity())); });
    AssertInvalid([&] { Render::ReflectCamera(glm::mat4(0), projection, glm::vec4(0, 1, 0, 0)); });
}

} // namespace

int main() {
    CheckSplitsAndReceiverCoverage();
    CheckStableTexelsAndVerticalSun();
    CheckReflection();
    CheckInvalidInputs();
    std::cout << "scene_effects_test passed\n";
}
