#include "Render/Public/Sprite/sprite_batch.h"
#include "Render/Private/rhi_device.h"
#include <iostream>
#include <stdexcept>

namespace Render {
struct RHIDeviceTestAccess {
    static void Install(RHIDevice& device, IBackend* backend) {
        device.backend_ = ObjectPtr<IBackend>(backend);
        device.isRun_.store(true);
    }
    static void Pump(RHIDevice& device) {
        device.commandSystem_.thread_SwitchQueues();
        device.thread_ProcessCurrentQueue();
    }
};
}

namespace {
using namespace Render;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// No window or GL context. The real RHIDevice resource and frame queues execute
// against this recording backend, including their actual callback dispatch.
class RecordingBackend final : public OpenglBackend {
public:
    RecordingBackend() : OpenglBackend({}) {}
    unsigned creates = 0, deletes = 0, uploads = 0, draws = 0, frames = 0;
    bool failUpload = false;
    uint32_t uploadedIndices = 0;
    template<class T> RenderResourceHandle<T> Handle() { return {++creates, 0}; }
    RenderResourceHandle<VertexBufferSpec> CreateVertexBuffer(const CreateVertexBufferCommand&) override {
        return Handle<VertexBufferSpec>();
    }
    bool UpdateVertexBuffer(const UpdateVertexBufferCommand& command) override {
        ++uploads;
        uploadedIndices = command.numIndex;
        Require(command.vertexData.size() > 0 && command.indexData.size() == command.numIndex * sizeof(uint32_t), "mesh upload lost copied bytes");
        return !failUpload;
    }
    RenderResourceHandle<RHITextureSpec> CreateTexture(const CreateTextureCommand& command) override {
        Require(command.data.size() == size_t(command.spec.width) * command.spec.height * 4, "atlas upload lost copied bytes");
        return Handle<RHITextureSpec>();
    }
    RenderResourceHandle<ShaderSourceSpec> CreateShaderSource(const CreateShaderSourceCommand&) override {
        return Handle<ShaderSourceSpec>();
    }
    RenderResourceHandle<ShaderProgramSpec> CreateGraphicShaderProgram(const CreateGraphicShaderProgramCommand&) override {
        return Handle<ShaderProgramSpec>();
    }
    RenderResourceHandle<PipelineSpec> CreatePipeline(const CreatePipelineCommand& command) override {
        Require(!command.desc.spec.depthTest && command.desc.spec.blendEnable, "sprite pipeline depth/blend state incorrect");
        return Handle<PipelineSpec>();
    }
    void DeleteVertexBuffer(const DeleteVertexBufferCommand&) override { ++deletes; }
    void DeleteTexture(const DeleteTextureCommand&) override { ++deletes; }
    void DeleteShaderSource(const DeleteShaderSourceCommand&) override { ++deletes; }
    void DeleteShaderProgram(const DeleteShaderProgramCommand&) override { ++deletes; }
    void DeletePipeline(const DeletePipelineCommand&) override { ++deletes; }
    void BeginFrame(const RHICommand::BeginFrame&) override { ++frames; }
    void EndFrame(const RHICommand::EndFrame&) override {}
    void SetPipeline(const RHICommand::SetPipeline&) override {}
    void SetVertexBuffer(const RHICommand::SetVertexBuffer&) override {}
    void BindTexture(const RHICommand::BindTexture&) override {}
    void DrawIndexed(const RHICommand::DrawIndexed& command) override {
        Require(uploads > 0 && command.indexCount == uploadedIndices, "draw executed without its mesh upload");
        ++draws;
    }
};

struct Fixture {
    ObjectPtr<RHIDevice> device{new RHIDevice};
    RecordingBackend* backend = new RecordingBackend;
    Fixture() { RHIDeviceTestAccess::Install(*device, backend); }
    void Pump(int iterations = 1) {
        for (int i = 0; i < iterations; ++i) {
            RHIDeviceTestAccess::Pump(*device);
            device->returnSystem.DrainCallbacks();
        }
    }
};

void TestUploadDependencyAndInFlightExclusion() {
    Fixture fixture;
    SpriteBatch2D batch(fixture.device.GenWeakPtr());
    Require(batch.Initialize({}), "initialization was not accepted");
    Require(!batch.Ready(), "batch ready before GPU callbacks");
    fixture.Pump(10);
    Require(batch.Ready() && fixture.backend->creates == 6, "GPU creation chain did not finish");
    Require(batch.Begin(10, 10), "cannot begin ready batch");
    batch.Rect({0, 0}, {2, 2}, {1, 1, 1, 1});
    batch.Text("HP: 12\nWAVE 1", {-9, 8}, .2f, {1, 1, 1, 1});
    auto frame = fixture.device->BeginFrame({});
    Require(batch.Flush(frame) && frame.End(false), "cannot record batch");
    int completions = 0;
    Require(batch.Submit(frame.GetCommandBuffer(), [&] { ++completions; }), "cannot submit batch");
    Require(!batch.Ready() && !batch.Begin(10, 10), "in-flight dynamic mesh can be overwritten");
    RHIDeviceTestAccess::Pump(*fixture.device);
    Require(fixture.backend->uploads == 1 && fixture.backend->draws == 0, "draw submitted before upload callback");
    fixture.device->returnSystem.DrainCallbacks();
    Require(fixture.backend->draws == 0 && completions == 0, "draw/completion occurred before frame execution");
    fixture.Pump();
    Require(fixture.backend->draws == 1 && completions == 1 && batch.Ready(), "frame did not retire correctly");

    // Empty frames still submit/present without uploading an empty invalid mesh.
    Require(batch.Begin(10, 10), "cannot begin empty batch");
    auto empty = fixture.device->BeginFrame({});
    Require(batch.Flush(empty) && empty.End(false), "cannot record empty frame");
    Require(batch.Submit(empty.GetCommandBuffer(), [&] { ++completions; }), "cannot submit empty frame");
    fixture.Pump();
    Require(fixture.backend->uploads == 1 && fixture.backend->frames == 2 && completions == 2, "empty frame behavior incorrect");
    batch.Shutdown();
    fixture.Pump(3);
    Require(fixture.backend->deletes == fixture.backend->creates, "completed batch leaked GPU resources");
}

void TestShutdownDuringInitialization() {
    Fixture fixture;
    {
        SpriteBatch2D batch(fixture.device.GenWeakPtr());
        Require(batch.Initialize({}), "initialization was not accepted");
        batch.Shutdown();
    }
    fixture.Pump(10);
    Require(fixture.backend->creates == 3 && fixture.backend->deletes == 3, "shutdown continued shader chain or leaked pending resources");
}

void TestShutdownRetainsSubmittedFrame() {
    Fixture fixture;
    auto batch = std::make_unique<SpriteBatch2D>(fixture.device.GenWeakPtr());
    Require(batch->Initialize({}), "initialization was not accepted");
    fixture.Pump(10);
    Require(batch->Begin(10, 10), "cannot begin ready batch");
    batch->Rect({0, 0}, {2, 2}, {1, 1, 1, 1});
    auto frame = fixture.device->BeginFrame({});
    Require(batch->Flush(frame) && frame.End(false), "cannot record frame");
    int completions = 0;
    Require(batch->Submit(frame.GetCommandBuffer(), [&] { ++completions; }), "cannot submit frame");
    fixture.Pump(); // upload completes and the callback submits the frame
    batch.reset();
    Require(fixture.backend->deletes == 0, "resources released before their recorded frame");
    fixture.Pump(5);
    Require(fixture.backend->draws == 1 && completions == 1, "shutdown discarded accepted GPU frame");
    Require(fixture.backend->deletes == fixture.backend->creates, "submitted frame lifetime leaked resources");
}

void TestFailedUploadCancelsFrame() {
    Fixture fixture;
    SpriteBatch2D batch(fixture.device.GenWeakPtr());
    Require(batch.Initialize({}), "initialization was not accepted");
    fixture.Pump(10);
    Require(batch.Begin(10, 10), "cannot begin ready batch");
    batch.Rect({0, 0}, {2, 2}, {1, 1, 1, 1});
    auto frame = fixture.device->BeginFrame({});
    Require(batch.Flush(frame) && frame.End(false), "cannot record frame");
    fixture.backend->failUpload = true;
    int completions = 0;
    Require(batch.Submit(frame.GetCommandBuffer(), [&] { ++completions; }), "cannot submit frame");
    fixture.Pump(3);
    Require(completions == 1 && fixture.backend->draws == 0 && !batch.Error().empty(), "failed upload drew stale geometry or lost its completion");
    batch.Shutdown();
    fixture.Pump(3);
    Require(fixture.backend->deletes == fixture.backend->creates, "failed upload kept its frame/resource lease alive");
}
}

int main() {
    try {
        TestUploadDependencyAndInFlightExclusion();
        TestShutdownDuringInitialization();
        TestShutdownRetainsSubmittedFrame();
        TestFailedUploadCancelsFrame();
        std::cout << "Sprite RHI dependency and lifecycle tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
