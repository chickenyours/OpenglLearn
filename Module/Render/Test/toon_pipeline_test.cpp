#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "Render/Private/rhi_device.h"
#include "Render/Public/Pipeline/toon_render_pipeline.h"

namespace {
void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <class Edit>
void RejectSettings(Edit edit, const char* reason) {
    Render::ToonPipelineSettings settings;
    edit(settings);
    std::string error;
    Check(!Render::ValidateToonPipelineSettings(settings, &error), reason);
    Check(!error.empty(), std::string("Missing validation diagnostic: ") + reason);
    // Error reporting is optional; validation must work without a destination.
    Check(!Render::ValidateToonPipelineSettings(settings), reason);
}

void CheckSettings() {
    using namespace Render;
    ToonPipelineSettings settings;
    std::string error = "old error";
    Check(ValidateToonPipelineSettings(settings, &error) && error.empty(),
          "Default Toon settings must be valid and clear stale diagnostics");

    // Artists may turn entire effects off or use the two extremes of the cel
    // band. These are valid settings, not a reason to force another pipeline.
    settings.outlinesEnabled = settings.shadowsEnabled = false;
    settings.groundEnabled = settings.groundShadows = false;
    settings.outlineWidth = settings.bandStrength = settings.highlightStrength = 0;
    settings.normalWrap = 0;
    settings.bandThreshold = -1;
    Check(ValidateToonPipelineSettings(settings), "Disabled Toon effects must remain valid");
    settings.bandThreshold = 1;
    settings.normalWrap = 1;
    settings.bandStrength = 1;
    settings.outlineWidth = 8;
    Check(ValidateToonPipelineSettings(settings), "Documented cel/outline endpoints must be accepted");

    RejectSettings([](auto& value) { value.lightDirection = glm::vec3(0); },
                   "Zero light direction cannot produce a shadow camera");
    RejectSettings([](auto& value) { value.bandThreshold = 1.01f; },
                   "Cel threshold outside normal dot-product range accepted");
    RejectSettings([](auto& value) { value.bandSoftness = 0; },
                   "Zero band softness accepted");
    RejectSettings([](auto& value) { value.normalWrap = -0.1f; },
                   "Negative normal wrap accepted");
    RejectSettings([](auto& value) { value.outlineWidth = -1; },
                   "Negative screen-space outline width accepted");
    RejectSettings([](auto& value) { value.selfShadowStrength = 1.1f; },
                   "Self-shadow strength above one accepted");
    RejectSettings([](auto& value) { value.backgroundColor.a = -0.1f; },
                   "Invalid background alpha accepted");
    RejectSettings([](auto& value) { value.shadowTint.r = 1.1f; },
                   "Out-of-range shadow tint accepted");
    RejectSettings([](auto& value) { value.shadowDepthBias = -0.001f; },
                   "Negative shadow depth bias accepted");
    RejectSettings([](auto& value) { value.groundExtent = 0; },
                   "Degenerate ground receiver accepted");
    RejectSettings([](auto& value) { value.shadowRadius = 0; },
                   "Degenerate shadow radius accepted");
    RejectSettings([](auto& value) { value.shadowDistance = value.shadowRadius; },
                   "Shadow camera inside its radius accepted");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    RejectSettings([=](auto& value) { value.lightDirection.x = nan; },
                   "Non-finite light direction accepted");
    RejectSettings([=](auto& value) { value.sceneCenter.y = infinity; },
                   "Non-finite shadow center accepted");
    RejectSettings([=](auto& value) { value.exposure = infinity; },
                   "Non-finite exposure accepted");
    RejectSettings([=](auto& value) { value.groundHeight = nan; },
                   "Non-finite ground height accepted");
    RejectSettings([=](auto& value) { value.outlineColor.b = nan; },
                   "Non-finite outline color accepted");
}

void CheckStoppedDeviceLifecycle() {
    using namespace Render;
    RHIDevice device;
    ToonRenderPipeline pipeline(device);
    Check(!pipeline.Resize(640, 480) && !pipeline.LastError().empty(),
          "Resize must reject an uninitialized Toon pipeline");
    Check(!pipeline.Initialize() && !pipeline.LastError().empty(),
          "Initialize must reject a stopped RHIDevice without waiting for callbacks");

    RHIFrameEncoder encoder({});
    Check(!pipeline.Record(encoder, RenderFrame{}, PipelineCamera{}) && !pipeline.LastError().empty(),
          "Record must reject an uninitialized Toon pipeline");
    Check(pipeline.RecordedFrameToken() == 0,
          "A rejected recording must not publish a pending token");
    Check(!pipeline.CompleteFrame(0) && !pipeline.DiscardFrame(0),
          "Zero frame tokens must never be accepted");
    Check(!pipeline.CompleteFrame(1) && !pipeline.DiscardFrame(1),
          "A never-recorded frame token must never be accepted");

    pipeline.Shutdown();
    pipeline.Shutdown();
    const auto debug = pipeline.DebugTextures();
    Check(!debug.sceneColor.IsValid() && !debug.sceneDepth.IsValid() && !debug.shadowMap.IsValid(),
          "Shutdown must invalidate borrowed Toon targets");
    const auto statistics = pipeline.Statistics();
    Check(statistics.scenePasses == 0 && statistics.outlinePasses == 0 &&
          statistics.shadowPasses == 0 && statistics.groundPasses == 0 &&
          statistics.resolvePasses == 0 && statistics.postPasses == 0,
          "Shutdown must clear Toon pass statistics");
    Check(pipeline.RecordedFrameToken() == 0 && !device.IsRunning(),
          "Toon pipeline must not own or start its borrowed device");
}
} // namespace

int main() {
    try {
        CheckSettings();
        CheckStoppedDeviceLifecycle();
        std::cout << "Toon settings and lifecycle tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Toon pipeline test failed: " << error.what() << '\n';
        return 1;
    }
}
