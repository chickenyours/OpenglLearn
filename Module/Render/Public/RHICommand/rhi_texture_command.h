#pragma once

#include <functional>
#include <cstddef>
#include <vector>
#include "object_ptr.h"
#include "Render/Public/rhi_resource_handle.h"
#include "Render/Public/RHIResourceType/Texture/texture.h"

namespace Render{

    using CreateTextureCallback = std::function<void(RenderResourceHandle<RHITextureSpec>)>;
    using DeleteTextureCallback = std::function<void()>;
    using UpdateTextureCallback = std::function<void(bool)>;
    struct CreateTextureCommand{
        CreateRHITextureSpec spec;
        std::vector<std::byte> data;
        CreateTextureCallback OnFinish;
    };
    struct DeleteTextureCommand{
        RenderResourceHandle<RHITextureSpec> handle;
        DeleteTextureCallback OnFinish;
    };
    struct UpdateTextureCommand {
        RenderResourceHandle<RHITextureSpec> handle;
        UpdateRHITextureDesc desc;
        std::vector<std::byte> data;
        UpdateTextureCallback OnFinish;
    };
}
