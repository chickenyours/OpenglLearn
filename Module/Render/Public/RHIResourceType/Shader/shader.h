#pragma once

#include <cstdint>
#include "Render/Public/RHIResourceType/Shader/shader_source.h"
#include "Render/Public/rhi_resource_handle.h"

namespace Render{

    enum class ShaderProgramType {
        Graphics,
        Compute
    };

    struct CreateGraphicShaderDesc{
        RenderResourceHandle<ShaderSourceSpec> vertexShaderSource;
        RenderResourceHandle<ShaderSourceSpec> fragmentShaderSource;
        RenderResourceHandle<ShaderSourceSpec> geometryShaderSource = {};
    };

    struct ShaderProgramSpec{
        ShaderProgramType type;
        uint32_t rhi_id;
    };
}