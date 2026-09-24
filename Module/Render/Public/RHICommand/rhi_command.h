#pragma once
#include <chrono>

#include "Render/Public/RHICommand/rhi_buffer_command.h"
#include "Render/Public/RHICommand/FrameCommand/rhi_frame_command.h"
#include "Render/Public/RHICommand/FrameCommand/rhi_frame_encoder.h"
#include "Render/Public/RHICommand/rhi_shader_command.h"
#include "Render/Public/RHICommand/rhi_texture_command.h"

namespace Render{
    struct FrameCommands{
        ObjectWeakPtr<RHIFrameCommandBuffer> buffer;
        OnVertexBufferFinishCallback OnFinish;
        std::chrono::steady_clock::time_point submittedAt = std::chrono::steady_clock::now();
    };
}
