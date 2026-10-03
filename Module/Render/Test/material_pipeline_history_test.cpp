#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include "ApplicationWindow/public/window.h"
#include "Render/Private/rhi_device.h"
#include "Render/Public/Pipeline/material_render_pipeline.h"

namespace {
using namespace Render;
using namespace std::chrono_literals;

void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
struct Completion {
    bool done = false;
    GLenum error = GL_NO_ERROR;
};
struct Recorded {
    RHIFrameEncoder encoder;
    std::uint64_t token;
    PipelineStatistics statistics;
};

// Empty scenes still execute the production HDR, TAA, history-depth-copy and
// display passes. Callbacks own their result so timeout/unwind cannot leave
// references to local stack variables on the render thread.
class HistoryFixture {
public:
    explicit HistoryFixture(OpenglBackendContext context) : pipeline_(device_) {
        device_.Run(BackendType::Opengl, std::move(context));
        settings_.shadows.enabled = false;
        settings_.shadows.atlasResolution = 128;
        settings_.reflection.enabled = false;
        settings_.post.bloomEnabled = false;
        settings_.post.ssaoEnabled = false;
        settings_.temporal.antialiasing = AntialiasingMode::TAA;
        settings_.temporal.msaaSamples = 1;
        SetCameraX(0);
    }
    ~HistoryFixture() {
        pipeline_.Shutdown();
        // Also releases any unsubmitted recording leases left by a failed check.
        device_.StopAndRelease();
        while (!device_.returnSystem.callbacks.empty()) device_.returnSystem.DrainCallbacks();
    }

    void Run() {
        CheckPipeline(pipeline_.Initialize(), "Initialize");
        Resize(64, 64);
        Check(!pipeline_.CompleteFrame(0) && !pipeline_.DiscardFrame(0), "Zero token must be rejected");

        // Recording alone must not create usable history, nor may a second
        // recording or a target resize race the pending history images.
        auto abandonedFirst = Record();
        Check(!abandonedFirst.statistics.historyUsed && !abandonedFirst.statistics.cameraHistoryUsed,
              "A fresh pipeline must have no completed history");
        CheckPendingGuards(abandonedFirst.token);
        Discard(abandonedFirst);
        Check(!pipeline_.CompleteFrame(abandonedFirst.token), "A discarded frame must not complete later");
        auto first = Record();
        Check(!first.statistics.historyUsed && !first.statistics.cameraHistoryUsed,
              "A recorded then discarded first frame must not become history");
        Complete(first);

        auto reused = Record();
        Check(reused.statistics.historyUsed && reused.statistics.cameraHistoryUsed,
              "An executed and completed frame must become TAA/camera history");
        Check(!pipeline_.CompleteFrame(first.token) && !pipeline_.DiscardFrame(first.token),
              "Stale completion/discard must be rejected while another frame is pending");
        Check(pipeline_.RecordedFrameToken() == reused.token, "A stale token must not consume the active token");
        Complete(reused);
        Check(!pipeline_.CompleteFrame(reused.token) && !pipeline_.DiscardFrame(reused.token),
              "A consumed token must not be accepted twice");

        // The abandoned camera is far enough away to trigger a camera cut.
        // Returning to the last EXECUTED camera must retain its old history;
        // accidentally committing the discarded camera would fail this check.
        SetCameraX(4);
        auto abandonedCut = Record();
        Check(!abandonedCut.statistics.historyUsed && !abandonedCut.statistics.cameraHistoryUsed,
              "The test camera displacement must trigger a history cut");
        Discard(abandonedCut);
        SetCameraX(0);
        auto afterDiscard = Record();
        Check(afterDiscard.statistics.historyUsed && afterDiscard.statistics.cameraHistoryUsed,
              "Discard must preserve the last completed image and camera metadata");
        Complete(afterDiscard);

        // Reset during a pending frame invalidates its future history metadata
        // without freeing the one-in-flight token or admitting another Record.
        auto resetPending = Record();
        Check(resetPending.statistics.historyUsed, "Reset test requires established history");
        pipeline_.ResetHistory();
        Check(pipeline_.RecordedFrameToken() == resetPending.token, "ResetHistory must preserve the pending token");
        CheckPendingGuards(resetPending.token);
        Complete(resetPending);
        auto afterReset = Record();
        Check(!afterReset.statistics.historyUsed && !afterReset.statistics.cameraHistoryUsed,
              "Completing an invalidated pending frame must not resurrect history");
        Complete(afterReset);
        auto reestablished = Record();
        Check(reestablished.statistics.historyUsed, "History must recover after a fresh completed frame");
        Discard(reestablished);

        // New render targets require fresh history. Tokens remain unique across
        // resize, so a delayed completion cannot acknowledge a newer frame.
        Resize(32, 32);
        auto resized = Record();
        Check(!resized.statistics.historyUsed && !resized.statistics.cameraHistoryUsed,
              "Resize must invalidate previous image/camera history");
        Check(!pipeline_.CompleteFrame(afterReset.token) && !pipeline_.DiscardFrame(afterReset.token),
              "Pre-resize tokens must remain stale");
        Check(pipeline_.RecordedFrameToken() == resized.token, "Stale pre-resize token consumed the new frame");
        Complete(resized);
        auto resizedReuse = Record();
        Check(resizedReuse.statistics.historyUsed, "History must be reusable at the new size");
        Discard(resizedReuse);

        // Shutdown also drops pending metadata. Its resource lease remains in
        // the abandoned encoder until Cancel, while stale tokens stay rejected
        // after the same pipeline object is initialized again.
        auto beforeShutdown = Record();
        const auto oldToken = beforeShutdown.token;
        pipeline_.Shutdown();
        Check(pipeline_.RecordedFrameToken() == 0 && !pipeline_.DebugTextures().sceneColor.IsValid(),
              "Shutdown must clear pending history and borrowed targets");
        Check(beforeShutdown.encoder.Cancel(), "Cannot cancel the shutdown frame's recording lease");
        Check(!pipeline_.CompleteFrame(oldToken) && !pipeline_.DiscardFrame(oldToken),
              "Shutdown must reject its pending token");
        CheckPipeline(pipeline_.Initialize(), "Reinitialize");
        Resize(64, 64);
        auto restarted = Record();
        Check(restarted.token != oldToken && !restarted.statistics.historyUsed && !restarted.statistics.cameraHistoryUsed,
              "Restart must use a new token and empty history");
        Check(!pipeline_.CompleteFrame(oldToken) && !pipeline_.DiscardFrame(oldToken),
              "A pre-shutdown token must not acknowledge a restarted frame");
        Check(pipeline_.RecordedFrameToken() == restarted.token, "Stale shutdown token consumed the restarted frame");
        Complete(restarted);
        auto restartedReuse = Record();
        Check(restartedReuse.statistics.historyUsed, "Restarted pipeline must accumulate history normally");
        Complete(restartedReuse);

        settings_.lighting.specularAA.z = 0;
        auto changedSpecularAA = Record();
        Check(!changedSpecularAA.statistics.historyUsed, "Changing specular filtering must invalidate HDR history");
        Complete(changedSpecularAA);
        auto reusedSpecularAA = Record();
        Check(reusedSpecularAA.statistics.historyUsed, "Unchanged specular settings must retain history");
        Complete(reusedSpecularAA);
        for (const auto invalid : {glm::vec4(-1, .2f, 1, 0), glm::vec4(.15f, 2, 1, 0), glm::vec4(.15f, .2f, .5f, 0)}) {
            settings_.lighting.specularAA = invalid;
            auto encoder = Begin();
            RenderFrame empty;
            Check(!pipeline_.Record(encoder, empty, camera_, settings_), "Invalid specular AA settings accepted");
            Check(encoder.Cancel() && pipeline_.RecordedFrameToken() == 0, "Rejected settings left a pending frame");
        }
        settings_.lighting.specularAA = Material::PbrPassConstants{}.specularAA;
        auto restoredSpecularAA = Record(); Complete(restoredSpecularAA);

        std::cout << "Material pipeline GPU history tests passed: completion, discard, pending reset, resize, restart and specular settings\n";
    }

private:
    void CheckPipeline(bool success, const char* operation) {
        Check(success, std::string(operation) + ": " + pipeline_.LastError());
    }
    void SetCameraX(float x) {
        camera_.position = {x, 0, 5};
        camera_.view = glm::lookAt(camera_.position, glm::vec3(x, 0, 0), glm::vec3(0, 1, 0));
    }
    void Resize(std::uint32_t width, std::uint32_t height) {
        CheckPipeline(pipeline_.Resize(width, height, 128, 1, 1), "Resize");
        width_ = width; height_ = height;
        camera_.projection = glm::perspective(glm::radians(50.0f), float(width) / height, 0.1f, 100.0f);
    }
    RHIFrameEncoder Begin() {
        RHICommand::BeginFrame begin;
        begin.frameIndex = ++frameIndex_;
        begin.framebufferWidth = width_; begin.framebufferHeight = height_;
        return device_.BeginFrame(begin);
    }
    Recorded Record() {
        auto encoder = Begin();
        try {
            RenderFrame empty; empty.frameIndex = frameIndex_;
            CheckPipeline(pipeline_.Record(encoder, empty, camera_, settings_), "Record");
            const auto token = pipeline_.RecordedFrameToken();
            Check(token != 0, "A successfully recorded frame must have a token");
            return {std::move(encoder), token, pipeline_.Statistics()};
        } catch (...) { encoder.Cancel(); throw; }
    }
    void CheckPendingGuards(std::uint64_t token) {
        auto blockedEncoder = Begin();
        RenderFrame empty;
        const bool recorded = pipeline_.Record(blockedEncoder, empty, camera_, settings_);
        const bool cancelled = blockedEncoder.Cancel();
        Check(!recorded && cancelled, "A second Record must reject a pending frame without leaking its encoder");
        Check(!pipeline_.Resize(width_ + 1, height_, 128, 1, 1), "Resize must reject a pending frame");
        Check(!pipeline_.CompleteFrame(token + 1) && !pipeline_.DiscardFrame(token + 1),
              "Unknown future tokens must be rejected");
        Check(pipeline_.RecordedFrameToken() == token, "Failed operations must preserve the pending token");
    }
    void Discard(Recorded& recorded) {
        Check(recorded.encoder.Cancel(), "Cannot cancel an unsubmitted frame");
        CheckPipeline(pipeline_.DiscardFrame(recorded.token), "Discard");
        Check(pipeline_.RecordedFrameToken() == 0, "Discard must consume its pending token");
    }
    void Complete(Recorded& recorded) {
        Check(recorded.encoder.End(false), "Cannot finish history frame");
        auto completed = std::make_shared<Completion>();
        Check(device_.async_SubmitFrameCommands(recorded.encoder.GetCommandBuffer(),
            [completed] { completed->done = true; }), "Cannot reliably submit history frame");
        Wait(completed, "history frame execution");
        CheckPipeline(pipeline_.CompleteFrame(recorded.token), "Complete");
        Check(pipeline_.RecordedFrameToken() == 0, "Complete must consume its pending token");
        auto checked = std::make_shared<Completion>();
        device_.async_ExecuteCode([checked] { checked->error = glGetError(); },
            [checked] { checked->done = true; });
        Wait(checked, "history frame GL validation");
        Check(checked->error == GL_NO_ERROR, "OpenGL error in real history passes: " + std::to_string(checked->error));
    }
    void Wait(const std::shared_ptr<Completion>& completion, const char* operation) {
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!completion->done) {
            device_.returnSystem.DrainCallbacks();
            if (completion->done) return;
            Check(std::chrono::steady_clock::now() < deadline, std::string("Timed out waiting for ") + operation);
            glfwPollEvents();
            device_.returnSystem.WaitForCallbacks(2ms);
        }
    }

    RHIDevice device_; // Must outlive the pipeline, callbacks and recording leases.
    MaterialRenderPipeline pipeline_;
    MaterialPipelineSettings settings_;
    PipelineCamera camera_;
    std::uint32_t width_ = 64, height_ = 64;
    std::uint64_t frameIndex_ = 0;
};
}

int main() {
    try {
        Check(glfwInit() == GLFW_TRUE, "GLFW initialization failed");
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        {
            ApplicationWindow::Window window({64, 64, "Material history test", false});
            Check(window.Activate(), "Cannot create hidden OpenGL window");
            const auto context = window.GetRenderContextAsOpengl();
            Check(context.has_value(), "Hidden window has no OpenGL render context");
            HistoryFixture fixture(*context);
            fixture.Run();
        }
        glfwTerminate();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Material pipeline history test failed: " << error.what() << '\n';
        glfwTerminate();
        return 1;
    }
}
