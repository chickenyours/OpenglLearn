#pragma once


#include <glm/glm.hpp>

#include "Render/Public/RHICommand/FrameCommand/rhi_command_dispatcher.h"
#include "Render/Public/RHIResourceType/rhi_resource_type.h"

namespace Render::RHICommand{


    struct SetBackgroundColor{
        glm::vec4 color;
    };

    struct Flip{

    };

    struct SetVertexBuffer{
        RenderResourceHandle<VertexBufferSpec> buffer;
    };

    struct SetPipeline{
        RenderResourceHandle<PipelineSpec> pipeline;
    };

    struct Draw{
        
    };

    struct DrawInstance{

    };

    struct DrawRect{

    };

    using FrameCommandsSet = CommandTable<
        SetBackgroundColor,
        Flip,
        SetVertexBuffer,
        SetPipeline,
        Draw,
        DrawRect
    >;

}