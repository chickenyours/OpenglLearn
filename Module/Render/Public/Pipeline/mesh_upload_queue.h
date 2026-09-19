#pragma once

#include <functional>

#include "Render/Public/RHIResourceType/Buffer/vertex_buffer.h"
#include "Render/Public/rhi_resource_handle.h"

namespace Render {

class IMeshUploadQueue {
public:
    using CreateCallback = std::function<void(RenderResourceHandle<VertexBufferSpec>)>;
    using UpdateCallback = std::function<void(bool)>;
    virtual ~IMeshUploadQueue() = default;

    virtual void Create(const CreateMeshBufferDesc& desc, CreateCallback callback) = 0;
    virtual void Update(RenderResourceHandle<VertexBufferSpec> handle,
                        const UpdateMeshBufferDesc& desc,
                        UpdateCallback callback) = 0;
    virtual void Destroy(RenderResourceHandle<VertexBufferSpec> handle) = 0;
};

} // namespace Render
