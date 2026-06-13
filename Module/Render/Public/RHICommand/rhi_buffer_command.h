#pragma once

#include "Render/Public/rhi_resource_handle.h"
#include "Render/Public/RHIResourceType/Buffer/vertex_buffer.h"

namespace Render{

    using CreateVertexBufferCallback = std::function<void(RenderResourceHandle<VertexBufferSpec>)>;
    struct CreateVertexBufferCommand{
        VertexLayout vertexLayout;
        uint32_t numVertex = 0;
        void* Data = nullptr;
        CreateVertexBufferCallback OnFinish;
    };

    struct DeleteVertexBufferCommand{
        RenderResourceHandle<VertexBufferSpec> handle;
    };

    using OnFinishCallback = std::function<void()>;
    using BackDoorExecution = std::function<void()>;
    struct BackDoorExecutionCommand{
        BackDoorExecution execution;
        OnFinishCallback OnFinish;
    };
}