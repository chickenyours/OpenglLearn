#pragma once

#include "Render/Private/rhi_device.h"
#include "Render/Public/Pipeline/mesh_upload_queue.h"

namespace Render {

class RHIMeshUploadQueue final : public IMeshUploadQueue {
public:
    explicit RHIMeshUploadQueue(RHIDevice& device) : device_(device) {}

    void Create(const CreateMeshBufferDesc& desc, CreateCallback callback) override {
        device_.async_CreateMeshBuffer(desc, std::move(callback));
    }
    void Update(RenderResourceHandle<VertexBufferSpec> handle,
                const UpdateMeshBufferDesc& desc,
                UpdateCallback callback) override {
        device_.async_UpdateMeshBuffer(handle, desc, std::move(callback));
    }
    void Destroy(RenderResourceHandle<VertexBufferSpec> handle) override {
        if(handle.IsValid()) device_.async_DeleteVertexBuffer(handle);
    }

private:
    RHIDevice& device_;
};

} // namespace Render
