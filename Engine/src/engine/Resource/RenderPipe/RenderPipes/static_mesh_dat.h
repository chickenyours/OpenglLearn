#pragma once

#include <unordered_set>
#include <unordered_map>
#include "engine/Resource/RenderPipe/renderPipe.h"

#include "engine/Resource/RenderPipe/RenderItems/model_render_item.h"

#include "engine/Environment/environment.h"

#include "engine/Resource/RenderPipe/Passes/camera_pass.h"
#include "engine/Resource/RenderPipe/Passes/scene_pass.h"
#include "engine/Resource/RenderPipe/Passes/screen_pass.h"
#include "engine/Resource/RenderPipe/Passes/environment_pass.h"

namespace Render{
    class StaicMeshDAT: public RenderPipe {
        virtual int Init(const RenderPipeContex& cfg) override;
        virtual void SetConfig(const RenderPipeContex& cfg) override;
        virtual void RenderCall() override;
    };
}
