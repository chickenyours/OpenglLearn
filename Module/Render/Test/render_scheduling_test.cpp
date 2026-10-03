#include <cassert>
#include <memory>
#include "Render/Public/Pipeline/view_frustum.h"
#include "Render/Private/rhi_device.h"

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
    static void PumpFrame(RHIDevice& device) { device.thread_ProcessNextFrame(); }
    static bool Pending(RHIDevice& device) {
        return device.latestFrame_.has_value() || !device.commandSystem_.thread_empty() || device.commandSystem_.hasPendingCommands();
    }
};
}

// Override every operation used below; no window or GL context is created.
class RecordingBackend : public Render::OpenglBackend {
public:
    explicit RecordingBackend(Render::RHIDevice& owner)
        : OpenglBackend({}), device(owner) {}
    Render::RHIDevice& device;
    int creates = 0, updates = 0, frames = 0, createsAtFrame = -1;
    uint64_t lastFrameIndex = 0;
    int shaderSourceDeletes = 0, shaderProgramDeletes = 0;
    std::function<void()> onFrame;
    Render::RHICommand::UpdateUniformBuffer lastUniformUpdate{};
    void DeleteShaderSource(const Render::DeleteShaderSourceCommand&) override {
        ++shaderSourceDeletes;
    }
    void DeleteShaderProgram(const Render::DeleteShaderProgramCommand&) override {
        ++shaderProgramDeletes;
    }
    void UpdateUniformBuffer(const Render::RHICommand::UpdateUniformBuffer& command) override {
        lastUniformUpdate = command;
    }
    Render::RenderResourceHandle<Render::VertexBufferSpec> CreateVertexBuffer(
        const Render::CreateVertexBufferCommand&) override {
        ++creates;
        if(creates == 3) {
            auto frame = device.BeginFrame({});
            assert(frame.End(false));
            assert(device.async_SubmitFrameCommands(frame.GetCommandBuffer()));
        }
        return {static_cast<std::uint32_t>(creates), 0};
    }
    bool UpdateVertexBuffer(const Render::UpdateVertexBufferCommand&) override {
        ++updates;
        return true;
    }
    void BeginFrame(const Render::RHICommand::BeginFrame& command) override {
        ++frames;
        lastFrameIndex = command.frameIndex;
        createsAtFrame = creates;
        if (onFrame) onFrame();
    }
    void EndFrame(const Render::RHICommand::EndFrame&) override {}
};

namespace {
int orphanCalls = 0;
std::array<std::byte, 64> uniformBytes;
void APIENTRY FakeBindBuffer(GLenum, GLuint) {}
void APIENTRY FakeBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum) {
    assert(target == GL_UNIFORM_BUFFER && size == 64 && data == nullptr);
    ++orphanCalls;
    uniformBytes.fill(std::byte{0});
}
void APIENTRY FakeBufferSubData(GLenum, GLintptr offset, GLsizeiptr size, const void* data) {
    std::memcpy(uniformBytes.data() + offset, data, static_cast<size_t>(size));
}
void TestUniformStreaming() {
    auto oldBind = glad_glBindBuffer;
    auto oldData = glad_glBufferData;
    auto oldSubData = glad_glBufferSubData;
    glad_glBindBuffer = FakeBindBuffer;
    glad_glBufferData = FakeBufferData;
    glad_glBufferSubData = FakeBufferSubData;
    Render::RHIResourcePool pool;
    Render::OpenglBackend backend({&pool});
    auto handle = pool.uniformBufferTable.Add({64, 1, Render::BufferUsage::Dynamic});
    Render::RHICommand::UpdateUniformBuffer command{};
    command.buffer = handle;
    command.size = 64;
    command.data.fill(std::byte{42});
    backend.UpdateUniformBuffer(command);
    assert(orphanCalls == 1 && uniformBytes[63] == std::byte{42});
    command.offset = 8; command.size = 4;
    command.data.fill(std::byte{7});
    backend.UpdateUniformBuffer(command);
    assert(orphanCalls == 1); // partial update must not discard other constants
    assert(uniformBytes[8] == std::byte{7} && uniformBytes[12] == std::byte{42});
    glad_glBindBuffer = oldBind;
    glad_glBufferData = oldData;
    glad_glBufferSubData = oldSubData;
}

std::vector<GLuint> deletedSources, deletedPrograms, usedPrograms;
void APIENTRY FakeDeleteShader(GLuint shader) { deletedSources.push_back(shader); }
void APIENTRY FakeDeleteProgram(GLuint program) { deletedPrograms.push_back(program); }
void APIENTRY FakeUseProgram(GLuint program) { usedPrograms.push_back(program); }
void APIENTRY FakeEnum(GLenum) {}
void APIENTRY FakeEnumPair(GLenum, GLenum) {}
void APIENTRY FakeBool(GLboolean) {}

void TestShaderDeletion() {
    auto oldDeleteShader = glad_glDeleteShader;
    auto oldDeleteProgram = glad_glDeleteProgram;
    auto oldUseProgram = glad_glUseProgram;
    auto oldPolygonMode = glad_glPolygonMode;
    auto oldFrontFace = glad_glFrontFace;
    auto oldCullFace = glad_glCullFace;
    auto oldEnable = glad_glEnable;
    auto oldDisable = glad_glDisable;
    auto oldDepthMask = glad_glDepthMask;
    auto oldDepthFunc = glad_glDepthFunc;
    auto oldBlendFunc = glad_glBlendFunc;
    glad_glDeleteShader = FakeDeleteShader;
    glad_glDeleteProgram = FakeDeleteProgram;
    glad_glUseProgram = FakeUseProgram;
    glad_glPolygonMode = FakeEnumPair;
    glad_glFrontFace = FakeEnum;
    glad_glCullFace = FakeEnum;
    glad_glEnable = FakeEnum;
    glad_glDisable = FakeEnum;
    glad_glDepthMask = FakeBool;
    glad_glDepthFunc = FakeEnum;
    glad_glBlendFunc = FakeEnumPair;

    Render::RHIResourcePool pool;
    Render::OpenglBackend backend({&pool});
    auto source = pool.shaderSourceTable.Add({Render::ShaderSourceType::Vertex, 41});
    backend.DeleteShaderSource({source, {}});
    assert(!pool.shaderSourceTable.Contains(source));
    auto replacementSource = pool.shaderSourceTable.Add({Render::ShaderSourceType::Vertex, 42});
    assert(replacementSource.id == source.id && replacementSource.version != source.version);
    backend.DeleteShaderSource({source, {}});
    assert(deletedSources == std::vector<GLuint>{41});
    assert(pool.shaderSourceTable.Contains(replacementSource));

    auto program = pool.shaderProgramTable.Add({Render::ShaderProgramType::Graphics, 51});
    Render::PipelineSpec pipelineSpec{};
    pipelineSpec.shaderProgram = program;
    auto pipeline = pool.PipelineTable.Add(pipelineSpec);
    backend.SetPipeline({pipeline});
    assert(usedPrograms.back() == 51);
    backend.DeleteShaderProgram({program, {}});
    assert(!pool.shaderProgramTable.Contains(program) && usedPrograms.back() == 0);
    auto replacementProgram = pool.shaderProgramTable.Add({Render::ShaderProgramType::Graphics, 52});
    assert(replacementProgram.id == program.id && replacementProgram.version != program.version);
    backend.DeleteShaderProgram({program, {}});
    assert(deletedPrograms == std::vector<GLuint>{51});
    const auto beforeStaleBind = usedPrograms.size();
    backend.SetPipeline({pipeline});
    assert(usedPrograms.size() == beforeStaleBind + 1 && usedPrograms.back() == 0);
    pipelineSpec.shaderProgram = replacementProgram;
    auto replacementPipeline = pool.PipelineTable.Add(pipelineSpec);
    backend.SetPipeline({replacementPipeline});
    assert(usedPrograms.back() == 52);
    backend.DeletePipeline({replacementPipeline, {}});
    const auto beforeDeletedPipelineBind = usedPrograms.size();
    backend.SetPipeline({replacementPipeline});
    assert(usedPrograms.size() == beforeDeletedPipelineBind + 1 && usedPrograms.back() == 0);

    glad_glDeleteShader = oldDeleteShader;
    glad_glDeleteProgram = oldDeleteProgram;
    glad_glUseProgram = oldUseProgram;
    glad_glPolygonMode = oldPolygonMode;
    glad_glFrontFace = oldFrontFace;
    glad_glCullFace = oldCullFace;
    glad_glEnable = oldEnable;
    glad_glDisable = oldDisable;
    glad_glDepthMask = oldDepthMask;
    glad_glDepthFunc = oldDepthFunc;
    glad_glBlendFunc = oldBlendFunc;
}

void TestFrameOwnership() {
    Render::RHIDevice device;
    auto* backend = new RecordingBackend(device);
    Render::RHIDeviceTestAccess::Install(device, backend);
    int destroyed = 0, deferred = 0;
    auto makeOwner = [&] {
        return std::shared_ptr<const void>(new int(1), [&](const void* value) {
            delete static_cast<const int*>(value);
            ++destroyed;
            // Both queue and pool reentry must run without either mutex held.
            device.async_ExecuteCode([&] { ++deferred; });
            auto temporary = device.BeginFrame({});
            assert(temporary.Cancel());
        });
    };

    auto owner = makeOwner();
    std::weak_ptr<const void> weak = owner;
    auto frame = device.BeginFrame({});
    assert(!frame.KeepAlive({}));
    assert(frame.KeepAlive(owner));
    assert(frame.End(false));
    assert(!frame.KeepAlive(owner));
    assert(device.async_SubmitFrameCommands(frame.GetCommandBuffer()));
    assert(!frame.Cancel());
    owner.reset();
    assert(!weak.expired());
    backend->onFrame = [&] { assert(!weak.expired()); };
    Render::RHIDeviceTestAccess::PumpFrame(device);
    backend->onFrame = {};
    assert(weak.expired() && destroyed == 1);

    // Retiring a latest frame cannot release a resource still used by FIFO work.
    owner = makeOwner();
    weak = owner;
    auto reliable = device.BeginFrame({});
    assert(reliable.KeepAlive(owner) && reliable.End(false));
    assert(device.async_SubmitFrameCommands(reliable.GetCommandBuffer()));
    auto latest = device.BeginFrame({});
    assert(latest.KeepAlive(owner) && latest.End(false));
    assert(device.async_SubmitLatestFrameCommands(latest.GetCommandBuffer()));
    owner.reset();
    auto newest = device.BeginFrame({});
    assert(newest.End(false));
    assert(device.async_SubmitLatestFrameCommands(newest.GetCommandBuffer()));
    assert(!weak.expired() && destroyed == 1);
    backend->onFrame = [&] { assert(!weak.expired()); };
    Render::RHIDeviceTestAccess::PumpFrame(device);
    backend->onFrame = {};
    assert(weak.expired() && destroyed == 2);
    Render::RHIDeviceTestAccess::PumpFrame(device);
    assert(!latest.Cancel()); // its buffer has already been retired/reused

    // Superseding the only owner exercises reentry from the submitter thread.
    owner = makeOwner();
    auto superseded = device.BeginFrame({});
    assert(superseded.KeepAlive(owner) && superseded.End(false));
    assert(device.async_SubmitLatestFrameCommands(superseded.GetCommandBuffer()));
    owner.reset();
    auto successor = device.BeginFrame({});
    assert(successor.End(false));
    assert(device.async_SubmitLatestFrameCommands(successor.GetCommandBuffer()));
    assert(destroyed == 3);
    Render::RHIDeviceTestAccess::PumpFrame(device);

    owner = makeOwner();
    auto cancelled = device.BeginFrame({});
    assert(cancelled.KeepAlive(owner));
    owner.reset();
    assert(cancelled.Cancel() && destroyed == 4);
    assert(!cancelled.Cancel() && !cancelled.Draw());
    auto active = device.BeginFrame({});
    assert(!cancelled.Cancel() && !frame.Cancel());
    assert(active.End(false) && active.Cancel()); // ended, but not submitted

    const auto allocated = device.GetRHIFrameCommandBufferPool()->GetAllocatedBufferCount();
    for (int i = 0; i < 100; ++i) {
        auto discarded = device.BeginFrame({});
        assert(discarded.Cancel());
    }
    assert(device.GetRHIFrameCommandBufferPool()->GetAllocatedBufferCount() == allocated);

    std::array<std::byte, 256> bytes;
    bytes.fill(std::byte{17});
    auto uniformFrame = device.BeginFrame({});
    assert(!uniformFrame.UpdateUniformBufferBytes({1, 0}, nullptr, 4));
    assert(!uniformFrame.UpdateUniformBufferBytes({1, 0}, bytes.data(), 0));
    assert(!uniformFrame.UpdateUniformBufferBytes({1, 0}, bytes.data(), 257));
    assert(uniformFrame.UpdateUniformBufferBytes({1, 0}, bytes.data(), 256, 16));
    bytes.fill(std::byte{23}); // the command owns its copied payload
    assert(uniformFrame.End(false));
    assert(device.async_SubmitFrameCommands(uniformFrame.GetCommandBuffer()));
    Render::RHIDeviceTestAccess::PumpFrame(device);
    assert(backend->lastUniformUpdate.size == 256 && backend->lastUniformUpdate.offset == 16);
    assert(backend->lastUniformUpdate.data[255] == std::byte{17});

    int deletionCallbacks = 0;
    device.async_DeleteShaderSource({1, 0}, [&] { ++deletionCallbacks; });
    device.async_DeleteShaderProgram({1, 0}, [&] { ++deletionCallbacks; });
    for (int i = 0; i < 8 && Render::RHIDeviceTestAccess::Pending(device); ++i)
        Render::RHIDeviceTestAccess::Pump(device);
    while (auto callback = device.returnSystem.callbacks.try_pop()) (*callback)();
    assert(backend->shaderSourceDeletes == 1 && backend->shaderProgramDeletes == 1);
    assert(deletionCallbacks == 2 && deferred == 4);
    assert(!Render::RHIDeviceTestAccess::Pending(device));
}

void TestStopDiscardsUnsubmittedFrames() {
    Render::RHIDevice device;
    auto* backend = new RecordingBackend(device);
    Render::RHIDeviceTestAccess::Install(device, backend);
    int released = 0, deletionCallbacks = 0;
    auto pin = std::shared_ptr<const void>(new int(7), [&](const void* value) {
        delete static_cast<const int*>(value);
        ++released;
        // Shutdown must drop the recording pin before stopping the consumer or
        // destroying its mutex/condition variable. This enqueue must remain safe.
        assert(device.IsRunning());
        device.async_DeleteShaderProgram({7, 0}, [&] { ++deletionCallbacks; });
    });
    std::weak_ptr<const void> weak = pin;
    auto abandoned = device.BeginFrame({});
    assert(abandoned.KeepAlive(pin));
    pin.reset();
    assert(!weak.expired());
    device.StopAndRelease(); // intentionally no Cancel/End/Submit
    assert(weak.expired() && released == 1);
    assert(!abandoned.IsRecording() && !abandoned.Cancel());
    assert(!device.IsRunning() && Render::RHIDeviceTestAccess::Pending(device));
    // The fake device has no worker; pump what the real stop loop drains.
    Render::RHIDeviceTestAccess::Pump(device);
    while (auto callback = device.returnSystem.callbacks.try_pop()) (*callback)();
    assert(backend->shaderProgramDeletes == 1 && deletionCallbacks == 1);
    device.StopAndRelease();
    assert(released == 1 && !Render::RHIDeviceTestAccess::Pending(device));
}
}

int main() {
    TestUniformStreaming();
    TestShaderDeletion();
    TestFrameOwnership();
    TestStopDiscardsUnsubmittedFrames();
    // Move-only payload and partial double-buffer consumption must preserve FIFO.
    Render::ThreadSafeConsumeQueue<std::unique_ptr<int>> queue;
    queue.threadAny_Push(std::make_unique<int>(1));
    queue.threadAny_Push(std::make_unique<int>(2));
    queue.thread_Switch();
    assert(*queue.thread_Pop() == 1);
    queue.threadAny_Push(std::make_unique<int>(3));
    queue.thread_Switch();
    assert(*queue.thread_Pop() == 2);
    queue.thread_Switch();
    assert(*queue.thread_Pop() == 3);

    Render::RHIDevice device;
    auto* backend = new RecordingBackend(device);
    Render::RHIDeviceTestAccess::Install(device, backend);
    int callbacks = 0;
    for(int i = 0; i < 128; ++i) {
        device.async_CreateMeshBuffer({}, [&](auto) { ++callbacks; });
        device.async_UpdateMeshBuffer({1, 0}, {}, [&](bool ok) { assert(ok); ++callbacks; });
    }
    Render::RHIDeviceTestAccess::Pump(device);
    assert(backend->frames == 1 && backend->createsAtFrame == 3);
    assert(backend->creates == 8 && backend->updates == 8);
    // Continue with only consumer-side work left: no producer notification.
    assert(Render::RHIDeviceTestAccess::Pending(device));
    for(int i = 0; i < 32 && Render::RHIDeviceTestAccess::Pending(device); ++i)
        Render::RHIDeviceTestAccess::Pump(device);
    assert(!Render::RHIDeviceTestAccess::Pending(device));
    assert(backend->creates == 128 && backend->updates == 128);
    while(auto callback = device.returnSystem.callbacks.try_pop()) (*callback)();
    assert(callbacks == 256);

    // Thousands of main-thread ticks while presentation is stalled must not
    // allocate thousands of frame buffers or replay stale camera snapshots.
    int retired = 0;
    for(uint64_t i = 1; i <= 1000; ++i) {
        auto frame = device.BeginFrame({.frameIndex = i});
        assert(frame.End(false));
        assert(device.async_SubmitLatestFrameCommands(frame.GetCommandBuffer(), [&] { ++retired; }));
    }
    assert(device.GetRHIFrameCommandBufferPool()->GetAllocatedBufferCount() <= 3);
    assert(device.GetFrameStats().superseded == 999);
    Render::RHIDeviceTestAccess::Pump(device);
    assert(backend->frames == 2 && backend->lastFrameIndex == 1000);
    while(auto callback = device.returnSystem.callbacks.try_pop()) (*callback)();
    assert(retired == 1000);
    assert(!Render::RHIDeviceTestAccess::Pending(device));

    const Render::ViewFrustum frustum(glm::mat4(1.0f));
    assert(frustum.Intersects({-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}));
    assert(frustum.Intersects({-2, -2, -2}, {2, 2, 2}));
    assert(frustum.Intersects({1, 0, 0}, {2, 1, 1})); // touching is visible
    for(int axis = 0; axis < 3; ++axis) {
        glm::vec3 minimum(0), maximum(0.5f);
        minimum[axis] = 2; maximum[axis] = 3;
        assert(!frustum.Intersects(minimum, maximum));
        minimum[axis] = -3; maximum[axis] = -2;
        assert(!frustum.Intersects(minimum, maximum));
    }

    Render::RHICommandReturnSystem returns;
    assert(!returns.WaitForCallbacks(std::chrono::milliseconds(0)));
    int notified=0;
    std::thread producer([&]{returns.callbacks.push([&]{++notified;});});
    const bool woke=returns.WaitForCallbacks(std::chrono::seconds(1));
    producer.join();
    assert(woke && notified==0); // wait observes data, never executes callbacks
    assert(returns.DrainCallbacks()==1 && notified==1);
    assert(!returns.WaitForCallbacks(std::chrono::milliseconds(0)));
    std::function<void()> refill;
    int calls = 0;
    refill = [&] { ++calls; returns.callbacks.push(refill); };
    returns.callbacks.push(refill);
    assert(returns.DrainCallbacks() == 1);
    assert(calls == 1 && returns.callbacks.size() == 1);
    for(int i = 0; i < 100; ++i) returns.callbacks.push([] {});
    const auto processed = returns.DrainCallbacks();
    assert(processed > 0 && processed <= 64);
}
