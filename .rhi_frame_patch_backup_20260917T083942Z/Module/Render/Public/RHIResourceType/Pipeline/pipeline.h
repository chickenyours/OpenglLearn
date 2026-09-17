#pragma once

#include "Render/Public/RHIResourceType/Shader/shader.h"
#include "Render/Public/RHIResourceType/Buffer/vertex_buffer.h"
#include "Render/Public/rhi_resource_handle.h"

namespace Render{

    enum class PrimitiveTopology{

    };

    enum class CullMode{

    };

    enum class CompareOp{

    };

    enum class BlendMode{

    };

    enum class Format{

    };

    
    struct PipelineSpec{
        RenderResourceHandle<ShaderProgramSpec> shaderProgram;
        PrimitiveTopology topology;
        VertexLayout expectVertexLayout;

        CullMode cullMode;
        bool depthTest;
        bool depthWrite;
        CompareOp depthCompare;

        bool blendEnable;
        BlendMode blendMode;

        Format colorFormat;
        Format depthFormat;
    };

    struct CreatePipelineDesc{
        PipelineSpec spec;
    };
}