#pragma once

#include <unordered_set>
#include <unordered_map>
#include "code/Resource/RenderPipe/renderPipe.h"

#include "code/Resource/RenderPipe/RenderItems/model_render_item.h"

#include "code/Environment/environment.h"

#include "code/Resource/RenderPipe/Passes/camera_pass.h"
#include "code/Resource/RenderPipe/Passes/scene_pass.h"
#include "code/Resource/RenderPipe/Passes/screen_pass.h"
#include "code/Resource/RenderPipe/Passes/environment_pass.h"

namespace Render{
    class StaicMeshDAT: public RenderPipe {
        virtual int Init(const RenderPipeContex& cfg) override;
        virtual void SetConfig(const RenderPipeContex& cfg) override;
        virtual void RenderCall() override;
    };
}