#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "Render/Public/rhi_resource_handle.h"

namespace Render { class RHIDevice; struct VertexBufferSpec; }

namespace Render::Compute {

// Opaque, device-scoped handles. No native API types escape the Render module.
struct BufferHandle {
    uint64_t id = 0, device = 0;
    explicit operator bool() const { return id != 0; }
    bool operator==(const BufferHandle&) const = default;
};
struct PipelineHandle {
    uint64_t id = 0, device = 0;
    explicit operator bool() const { return id != 0; }
    bool operator==(const PipelineHandle&) const = default;
};
enum class Access { ReadOnly, WriteOnly, ReadWrite };
struct Binding {
    uint32_t binding = 0;
    BufferHandle buffer;
    Access access = Access::ReadWrite;
};
struct Dispatch {
    PipelineHandle pipeline;
    std::vector<Binding> bindings;
    uint32_t groupsX = 1, groupsY = 1, groupsZ = 1;
    std::vector<std::byte> pushConstants;
};
struct Batch {
    // FIFO dependencies: writes from a dispatch are made visible to every
    // subsequent dispatch with explicit compute memory barriers.
    std::vector<Dispatch> dispatches;
};
// A native scene reuses the graphics backend's acceleration structure for the
// same packed geometry texture. geometryUniformOffset addresses its ivec4
// geometry header relative to the currently bound uniform-buffer range.
// surfaceCache selects the existing expanded Surface Cache candidate geometry.
// No native API handles or separately owned acceleration structures escape.
struct NativeSceneDesc {
    uint32_t geometryTextureSlot = 9, geometryUniformBinding = 12;
    uint32_t geometryUniformOffset = 80;
    bool surfaceCache = true;
};
// In-frame kernels share graphics textures and immutable uniform snapshots.
// Sets 0/1 are the existing uniform/texture slots; sets 2/3 are contiguous
// storage-buffer/image bindings. Optional nativeScene adds one acceleration
// structure at set=2,binding=8; a source declaration at set=2,binding=0 is
// remapped by the Vulkan frame compiler so existing ray helpers can be reused.
// Execution is recorded by RHIFrameEncoder without standalone submissions.
struct FramePipelineDesc {
    std::string source, label;
    uint32_t uniformMask = 0, sampledMask = 0;
    uint32_t storageBuffers = 0, storageImages = 0, pushConstantBytes = 0;
    std::optional<NativeSceneDesc> nativeScene;
};
struct Statistics {
    std::string deviceName;
    bool available = false;
    uint64_t submittedBatches = 0, dispatchedKernels = 0;
    uint64_t liveBuffers = 0, livePipelines = 0, allocatedBytes = 0;
    uint32_t validationErrors = 0;
    double lastSubmitMilliseconds = 0;
    uint64_t maxStorageBufferBytes = 0;
    uint32_t maxStorageBindings = 0;
    bool frameAvailable = false;
};

// Calls are serialized on the RHIDevice render thread. Submit waits for its
// fence; resource uploads, destruction and readback therefore cannot race GPU
// work. Call from the owning/UI/worker thread, never inside async_ExecuteCode.
// A borrowed RHIDevice must outlive this facade.
class Device {
public:
    static std::unique_ptr<Device> CreateVulkan(bool validation = false);
    static std::unique_ptr<Device> Borrow(RHIDevice& device);
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    BufferHandle CreateStorageBuffer(size_t bytes, const void* initial = nullptr,
                                     std::string label = {});
    void Upload(BufferHandle buffer, const void* data, size_t bytes, size_t offset = 0);
    // GLSL #version 450+, set=0, contiguous std430 storage bindings [0,N).
    // Every dispatch binds N buffers, including unused layout entries.
    PipelineHandle CreatePipeline(std::string glsl, uint32_t storageBindings,
                                  uint32_t pushConstantBytes = 0, std::string label = {});
    PipelineHandle CreateFramePipeline(const FramePipelineDesc& description);
    void Submit(const Batch& batch);
    std::vector<std::byte> Readback(BufferHandle buffer, size_t bytes = 0, size_t offset = 0);
    // Same-device drawing: copy computed bytes into an existing mesh buffer
    // on the GPU. Cross-device previews may use Readback + mesh upload.
    void CopyToVertexBuffer(BufferHandle buffer, RenderResourceHandle<VertexBufferSpec> vertexBufferHandle,
                            size_t bytes, size_t sourceOffset = 0, size_t destinationOffset = 0);
    void Destroy(BufferHandle buffer);
    void Destroy(PipelineHandle pipeline);
    Statistics Diagnostics() const;

private:
    struct Impl;
    explicit Device(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace Render::Compute
