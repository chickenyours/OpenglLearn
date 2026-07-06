#pragma once

#include "Render/Public/RHICommand/rhi_buffer_command.h"
#include "Render/Public/RHICommand/FrameCommand/rhi_frame_command.h"
#include "Render/Public/RHICommand/rhi_shader_command.h"

namespace Render{
    struct FrameCommands{
        ObjectWeakPtr<RHIFrameCommandBuffer> buffer;
        OnFinishCallback OnFinish;
    };
}
