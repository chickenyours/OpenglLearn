#pragma once

#include <functional>

#include "Render/Public/rhi_resource_handle.h"
#include "Render/Public/RHIResourceType/Pipeline/pipeline.h"

namespace Render{

    using OnCreatePipelineCommandFinish = std::function<void(RenderResourceHandle<PipelineSpec>)>;
    struct CreatePipelineCommand
    {
        CreatePipelineDesc desc;
        OnCreatePipelineCommandFinish OnFinish;
    };

    using OnDeletePipelineCommandFinish = std::function<void()>;
    struct DeletePipelineCommand
    {
        RenderResourceHandle<PipelineSpec> handle;
        OnDeletePipelineCommandFinish OnFinish;
    };
}