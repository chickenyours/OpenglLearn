#pragma once

#include "Render/Public/RHIResourceType/Shader/shader.h"
#include "Render/Public/RHIResourceType/Buffer/vertex_buffer.h"
#include "Render/Public/rhi_resource_handle.h"

namespace Render{

    enum class PrimitiveTopology{
        TriangleList,
        TriangleStrip,
        LineList,
        PointList
    };

    enum class CullMode{
        None,
        Front,
        Back
    };

    enum class CompareOp{
        Never,
        Less,
        LessEqual,
        Equal,
        GreaterEqual,
        Greater,
        Always
    };

    enum class BlendMode{
        Opaque,
        Alpha,
        Additive
    };

    enum class Format{
        Unknown,
        RGBA8,
        Depth24Stencil8
    };

    enum class PolygonMode{
        Fill,
        Line
    };


    struct PipelineSpec{
        RenderResourceHandle<ShaderProgramSpec> shaderProgram;
        PrimitiveTopology topology = PrimitiveTopology::TriangleList;
        VertexLayout expectVertexLayout;

        CullMode cullMode = CullMode::Back;
        PolygonMode polygonMode = PolygonMode::Fill;
        bool frontFaceCounterClockwise = true;
        bool depthTest = true;
        bool depthWrite = true;
        CompareOp depthCompare = CompareOp::LessEqual;

        bool blendEnable = false;
        BlendMode blendMode = BlendMode::Opaque;

        Format colorFormat = Format::RGBA8;
        Format depthFormat = Format::Depth24Stencil8;
    };

    struct CreatePipelineDesc{
        PipelineSpec spec;
    };
}
