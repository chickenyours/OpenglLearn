#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>
#include "Render/Public/Pipeline/temporal_effects.h"

namespace {
bool Near(float a, float b, float tolerance = 1e-5f) { return std::abs(a - b) < tolerance; }
glm::vec3 Ndc(const glm::mat4& matrix, const glm::vec3& point) {
    const glm::vec4 clip = matrix * glm::vec4(point, 1);
    return glm::vec3(clip) / clip.w;
}
template <typename F> void AssertInvalid(F&& action) {
    bool rejected = false;
    try { action(); } catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
}

void CheckSettings() {
    using namespace Render;
    TemporalEffectsSettings settings;
    std::string error;
    assert(ValidateTemporalEffectsSettings(settings, &error) && error.empty());
    for (auto mode : {AntialiasingMode::None, AntialiasingMode::FXAA, AntialiasingMode::TAA})
        for (auto samples : {1u, 2u, 4u, 8u}) {
            settings.antialiasing = mode; settings.msaaSamples = samples;
            assert(ValidateTemporalEffectsSettings(settings));
        }
    settings.msaaSamples = 3;
    assert(!ValidateTemporalEffectsSettings(settings, &error) && !error.empty());
    settings = {};
    settings.antialiasing = static_cast<AntialiasingMode>(9);
    assert(!ValidateTemporalEffectsSettings(settings));
    settings = {}; settings.historyWeight = 1;
    assert(!ValidateTemporalEffectsSettings(settings));
    settings = {}; settings.depthRejectRelative = -0.1f;
    assert(!ValidateTemporalEffectsSettings(settings));
    settings = {}; settings.fxaaMinThreshold = std::numeric_limits<float>::quiet_NaN();
    assert(!ValidateTemporalEffectsSettings(settings));
    settings = {}; settings.motionBlurShutterSeconds = std::numeric_limits<float>::infinity();
    assert(!ValidateTemporalEffectsSettings(settings));
    settings = {}; settings.motionBlurSamples = 33;
    assert(!ValidateTemporalEffectsSettings(settings));
    settings = {}; settings.motionBlurMaxPixels = -1;
    assert(!ValidateTemporalEffectsSettings(settings));
    settings = {}; settings.historyWeight = 0.85f; settings.motionBlurSamples = 20;
    const auto constants = MakeTemporalConstants(settings);
    assert(constants.temporal.x == 0.85f && constants.motion.z == 20);
    assert(constants.temporal.w == 0 && constants.motion.w == 0); // explicit history opt-in
}

void CheckSequence() {
    using namespace Render;
    assert(Halton(0, 2) == 0 && Halton(1, 2) == 0.5f && Halton(2, 2) == 0.25f);
    assert(Halton(3, 2) == 0.75f && Near(Halton(1, 3), 1.0f / 3));
    glm::vec2 samples[TemporalJitterPeriod];
    for (std::uint32_t i = 0; i < TemporalJitterPeriod; ++i) {
        const auto jitter = TemporalJitter(i, 1920, 1080);
        assert(std::abs(jitter.x * 1920) < 0.5f && std::abs(jitter.y * 1080) < 0.5f);
        const auto repeated = TemporalJitter(i + TemporalJitterPeriod, 1920, 1080);
        assert(jitter == repeated);
        const auto doubleResolution = TemporalJitter(i, 3840, 2160);
        assert(glm::length(doubleResolution * 2.0f - jitter) < 1e-8f);
        samples[i] = jitter;
        for (std::uint32_t previous = 0; previous < i; ++previous)
            assert(glm::length(samples[previous] - jitter) > 1e-8f);
    }
    const auto last = TemporalJitter(std::numeric_limits<std::uint64_t>::max(), 1, 1);
    assert(std::isfinite(last.x) && std::isfinite(last.y));
    AssertInvalid([] { Halton(1, 1); });
    AssertInvalid([] { TemporalJitter(0, 0, 100); });
    AssertInvalid([] { TemporalJitter(0, 100, 0); });
}

void CheckProjectionAndReconstruction() {
    using namespace Render;
    const glm::vec2 jitter = TemporalJitter(3, 800, 600);
    const glm::mat4 projections[] = {
        glm::perspective(glm::radians(65.0f), 4.0f / 3.0f, 0.1f, 200.0f),
        glm::ortho(-4.0f, 4.0f, -3.0f, 3.0f, 0.1f, 200.0f)
    };
    for (const auto& projection : projections) {
        const auto shifted = JitterProjection(projection, jitter);
        for (const auto& point : {glm::vec3(0, 0, -1), glm::vec3(1, -0.5f, -5), glm::vec3(-2, 1, -60)}) {
            const auto expected = Ndc(projection, point);
            const auto actual = Ndc(shifted, point);
            assert(Near(actual.x - expected.x, jitter.x * 2));
            assert(Near(actual.y - expected.y, jitter.y * 2));
            assert(Near(actual.z, expected.z));
            const glm::vec4 homogeneous = glm::inverse(shifted) * glm::vec4(actual, 1);
            const auto reconstructed = glm::vec3(homogeneous) / homogeneous.w;
            assert(glm::length(reconstructed - point) < 0.005f);
        }
    }
    AssertInvalid([] { JitterProjection(glm::mat4(1), {std::numeric_limits<float>::infinity(), 0}); });
    auto invalid = glm::mat4(1); invalid[1][2] = std::numeric_limits<float>::quiet_NaN();
    AssertInvalid([&] { JitterProjection(invalid, {0, 0}); });
}

void CheckJitterDoesNotCauseMotion() {
    using namespace Render;
    constexpr float width = 800, height = 600;
    const glm::mat4 projection = glm::perspective(glm::radians(60.0f), width / height, 0.1f, 100.0f);
    const glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0), glm::vec3(0, 1, 0));
    const auto currentVP = JitterProjection(projection, TemporalJitter(4, 800, 600)) * view;
    const auto previousVP = JitterProjection(projection, TemporalJitter(3, 800, 600)) * view;
    const glm::vec3 original(0.4f, 0.2f, 0);
    const auto currentNdc = Ndc(currentVP, original);
    glm::vec4 reconstructed = glm::inverse(currentVP) * glm::vec4(currentNdc, 1);
    reconstructed /= reconstructed.w;
    const glm::vec3 world(reconstructed);
    const glm::vec2 jitteredVelocity = glm::vec2(Ndc(currentVP, world) - Ndc(previousVP, world)) * 0.5f;
    assert(glm::length(jitteredVelocity * glm::vec2(width, height)) > 0.1f);
    const glm::vec2 unjitteredVelocity = glm::vec2(Ndc(projection * view, world) - Ndc(projection * view, world)) * 0.5f;
    assert(glm::length(unjitteredVelocity) == 0);
    // A translated camera must still produce camera motion at the same world point.
    const glm::mat4 movedView = glm::lookAt(glm::vec3(0.1f, 0, 5), glm::vec3(0.1f, 0, 0), glm::vec3(0, 1, 0));
    const glm::vec2 cameraVelocity = glm::vec2(Ndc(projection * movedView, world) - Ndc(projection * view, world)) * 0.5f;
    assert(cameraVelocity.x < 0 && Near(cameraVelocity.y, 0));
    assert(Near(cameraVelocity.x * width, -0.1f * projection[0][0] / 5.0f * 0.5f * width, 0.001f));
}
} // namespace

int main() {
    CheckSettings();
    CheckSequence();
    CheckProjectionAndReconstruction();
    CheckJitterDoesNotCauseMotion();
    std::cout << "temporal_effects_test passed\n";
}
