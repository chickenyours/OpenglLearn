#pragma once

#include <functional>
#include "object_ptr.h"
#include "Render/Public/rhi_resource_handle.h"
#include "Render/Public/RHIResourceType/Texture/texture.h"

namespace Render{

    using CreateTextureCallback = std::function<void(RenderResourceHandle<RHITextureSpec>)>;
    using DeleteTextureCallback = std::function<void()>;
    struct CreateTextureCommand{
        CreateRHITextureSpec spec;
        CreateTextureCallback OnFinished;
    };
    struct DeleteTextureCommand{
        RenderResourceHandle<RHITextureSpec> handle;
        DeleteTextureCallback OnFinish;
    };
}