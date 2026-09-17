#pragma once

#include <cstddef>
#include <functional>
#include <vector>

#include "object_ptr.h"
#include "Render/Public/rhi_resource_handle.h"
#include "Render/Public/RHIResourceType/Buffer/uniform_buffer.h"
#include "Render/Public/RHIResourceType/Buffer/vertex_buffer.h"

namespace Render {

using OnVertexBufferFinishCallback = std::function<void()>;
using BackDoorExecution = std::function<void()>;
using CreateVertexBufferCallback =
    std::function<void(RenderResourceHandle<VertexBufferSpec>)>;
using CreateUniformBufferCallback =
    std::function<void(RenderResourceHandle<UniformBufferSpec>)>;
using UpdateVertexBufferCallback = std::function<void(bool)>;

// Queued resource commands own upload bytes, so producer jobs may release their
// temporary arrays as soon as they submit an upload.
struct CreateVertexBufferCommand {
    VertexLayout vertexLayout;
    uint32_t numVertex = 0;
    uint32_t numIndex = 0;
    IndexType indexType = IndexType::UInt32;
    BufferUsage usage = BufferUsage::Static;
    std::vector<std::byte> vertexData;
    std::vector<std::byte> indexData;
    CreateVertexBufferCallback OnFinish;
};

struct DeleteVertexBufferCommand {
    RenderResourceHandle<VertexBufferSpec> handle;
    OnVertexBufferFinishCallback OnFinish;
};

struct UpdateVertexBufferCommand {
    RenderResourceHandle<VertexBufferSpec> handle;
    uint32_t numVertex = 0;
    uint32_t numIndex = 0;
    IndexType indexType = IndexType::UInt32;
    std::vector<std::byte> vertexData;
    std::vector<std::byte> indexData;
    UpdateVertexBufferCallback OnFinish;
};

struct CreateUniformBufferCommand {
    uint32_t byteSize = 0;
    BufferUsage usage = BufferUsage::Dynamic;
    std::vector<std::byte> initialData;
    CreateUniformBufferCallback OnFinish;
};

struct DeleteUniformBufferCommand {
    RenderResourceHandle<UniformBufferSpec> handle;
    OnVertexBufferFinishCallback OnFinish;
};

struct BackDoorExecutionCommand {
    BackDoorExecution execution;
    OnVertexBufferFinishCallback OnFinish;
};

} // namespace Render
