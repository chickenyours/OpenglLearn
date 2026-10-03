#pragma once

#include <functional>
#include "Render/Public/RHIResourceType/RenderTarget/render_target.h"

namespace Render {

using CreateRenderTargetCallback = std::function<void(RenderResourceHandle<RenderTargetSpec>)>;
using DeleteRenderTargetCallback = std::function<void()>;

struct CreateRenderTargetCommand {
    CreateRenderTargetDesc desc;
    CreateRenderTargetCallback OnFinish;
};
struct DeleteRenderTargetCommand {
    RenderResourceHandle<RenderTargetSpec> handle;
    DeleteRenderTargetCallback OnFinish;
};

} // namespace Render
