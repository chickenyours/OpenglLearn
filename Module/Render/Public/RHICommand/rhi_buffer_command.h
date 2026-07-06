#pragma once

#include "object_ptr.h"
#include "Render/Public/rhi_resource_handle.h"
#include "Render/Public/RHIResourceType/Buffer/vertex_buffer.h"
#include "Render/Public/RHICommand/FrameCommand/rhi_frame_command_buffer.h"

namespace Render{
    using OnFinishCallback = std::function<void()>;
    using BackDoorExecution = std::function<void()>;
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
    
    struct BackDoorExecutionCommand{
        BackDoorExecution execution;
        OnFinishCallback OnFinish;
    };
}