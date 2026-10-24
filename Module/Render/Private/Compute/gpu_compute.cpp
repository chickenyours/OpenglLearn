#include "Render/Public/Compute/gpu_compute.h"
#include "Render/Private/rhi_device.h"
#include <algorithm>
#include <future>
#include <mutex>
#include <stdexcept>

namespace Render::Compute {
struct Device::Impl {
    std::unique_ptr<RHIDevice> owned;
    RHIDevice* rhi = nullptr;
    mutable std::mutex mutex;
    std::vector<BufferHandle> buffers;
    std::vector<PipelineHandle> pipelines;

    template<class F> auto Call(F&& operation) const {
        using Result = std::invoke_result_t<F, IBackend&>;
        if (!rhi || !rhi->IsRunning()) throw std::runtime_error("GPU compute device is stopped");
        auto completion = std::make_shared<std::promise<Result>>();
        auto result = completion->get_future();
        rhi->async_ExecuteCode([this, completion, operation = std::forward<F>(operation)]() mutable {
            try {
                if constexpr (std::is_void_v<Result>) {
                    operation(*rhi->BackendDiagnostics()); completion->set_value();
                } else completion->set_value(operation(*rhi->BackendDiagnostics()));
            } catch (...) { completion->set_exception(std::current_exception()); }
        });
        return result.get();
    }
};
Device::Device(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
std::unique_ptr<Device> Device::CreateVulkan(bool validation) {
    if (!RegisterVulkanBackend()) throw std::runtime_error("Vulkan compute requires the Vulkan SDK and shaderc backend build");
    auto impl = std::make_unique<Impl>();
    impl->owned = std::make_unique<RHIDevice>();
    impl->rhi = impl->owned.get();
    VulkanBackendContext context;
    context.validation = validation;
    context.hardwareRayTracing = false;
    context.headlessCompute = true;
    impl->rhi->Run(BackendType::Vulkan, context);
    return std::unique_ptr<Device>(new Device(std::move(impl)));
}
std::unique_ptr<Device> Device::Borrow(RHIDevice& rhi) {
    auto impl = std::make_unique<Impl>();
    impl->rhi = &rhi;
    if (!impl->Call([](IBackend& b) { return b.ComputeDiagnostics().available; }))
        throw std::runtime_error("Selected RHI backend has no GPU compute support");
    return std::unique_ptr<Device>(new Device(std::move(impl)));
}
Device::~Device() {
    if (!impl_ || !impl_->rhi || !impl_->rhi->IsRunning()) return;
    // A frame pin can release a borrowed facade on the render thread. Waiting
    // for another task on that same thread would deadlock. Queue destruction;
    // RHIDevice must outlive a borrowed facade and owns shutdown cleanup too.
    if(!impl_->owned) {
        auto* rhi=impl_->rhi;
        try {
        rhi->async_ExecuteCode([rhi,pipelines=std::move(impl_->pipelines),buffers=std::move(impl_->buffers)] {
            auto* backend=rhi->BackendDiagnostics();
            for(auto h:pipelines)backend->ComputeDestroy(h);
            for(auto h:buffers)backend->ComputeDestroy(h);
        });
        } catch(...) { /* Backend shutdown still owns these allocations. */ }
        return;
    }
    try {
        impl_->Call([&](IBackend& backend) {
            for (auto h : impl_->pipelines) backend.ComputeDestroy(h);
            for (auto h : impl_->buffers) backend.ComputeDestroy(h);
        });
    } catch (...) { /* RHIDevice shutdown also owns all backend allocations. */ }
}
BufferHandle Device::CreateStorageBuffer(size_t bytes, const void* initial, std::string label) {
    std::lock_guard lock(impl_->mutex);
    auto h = impl_->Call([&](IBackend& b) { return b.ComputeCreateBuffer(bytes, initial, label); });
    impl_->buffers.push_back(h);
    return h;
}
void Device::Upload(BufferHandle h, const void* data, size_t bytes, size_t offset) {
    std::lock_guard lock(impl_->mutex);
    impl_->Call([&](IBackend& b) { b.ComputeUpload(h, data, bytes, offset); });
}
PipelineHandle Device::CreatePipeline(std::string glsl, uint32_t bindings, uint32_t pushBytes, std::string label) {
    std::lock_guard lock(impl_->mutex);
    auto h = impl_->Call([&](IBackend& b) { return b.ComputeCreatePipeline(glsl, bindings, pushBytes, label); });
    impl_->pipelines.push_back(h);
    return h;
}
void Device::Submit(const Batch& batch) {
    std::lock_guard lock(impl_->mutex);
    impl_->Call([&](IBackend& b) { b.ComputeSubmit(batch); });
}
PipelineHandle Device::CreateFramePipeline(const FramePipelineDesc& description) {
    std::lock_guard lock(impl_->mutex);
    auto h = impl_->Call([&](IBackend& b) { return b.ComputeCreateFramePipeline(description); });
    impl_->pipelines.push_back(h);
    return h;
}
std::vector<std::byte> Device::Readback(BufferHandle h, size_t bytes, size_t offset) {
    std::lock_guard lock(impl_->mutex);
    return impl_->Call([&](IBackend& b) { return b.ComputeReadback(h, bytes, offset); });
}
void Device::CopyToVertexBuffer(BufferHandle h, RenderResourceHandle<VertexBufferSpec> mesh, size_t bytes, size_t src, size_t dst) {
    std::lock_guard lock(impl_->mutex);
    impl_->Call([&](IBackend& b) { b.ComputeCopyToVertexBuffer(h, mesh, bytes, src, dst); });
}
void Device::Destroy(BufferHandle h) {
    std::lock_guard lock(impl_->mutex);
    impl_->Call([&](IBackend& b) { b.ComputeDestroy(h); });
    std::erase(impl_->buffers, h);
}
void Device::Destroy(PipelineHandle h) {
    std::lock_guard lock(impl_->mutex);
    impl_->Call([&](IBackend& b) { b.ComputeDestroy(h); });
    std::erase(impl_->pipelines, h);
}
Statistics Device::Diagnostics() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->Call([](IBackend& b) { return b.ComputeDiagnostics(); });
}
} // namespace Render::Compute
