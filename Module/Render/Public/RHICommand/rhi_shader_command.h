#pragma once

#include <functional>
#include <string>
#include "Render/Public/rhi_resource_handle.h"
#include "Render/Public/RHIResourceType/Shader/shader.h"

namespace Render{

    // 如果执行失败,返回的RenderResourceHandle是一个非法语句,回调需要判别
    using OnCreateGraphicShaderFinish = std::function<void(RenderResourceHandle<ShaderProgramSpec>)>;
    struct CreateGraphicShaderProgramCommand{
        CreateGraphicShaderDesc createDesc;
        OnCreateGraphicShaderFinish OnFinish;
    };

    using OnCreateShaderSourceFinish = std::function<void(RenderResourceHandle<ShaderSourceSpec>)>;
    struct CreateShaderSourceCommand{
        ShaderSourceType type = ShaderSourceType::Vertex;
        std::string source;
        OnCreateShaderSourceFinish OnFinish;
    };
}
