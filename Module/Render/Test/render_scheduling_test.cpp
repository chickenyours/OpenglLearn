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
}

int main() {
    TestUniformStreaming();
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
