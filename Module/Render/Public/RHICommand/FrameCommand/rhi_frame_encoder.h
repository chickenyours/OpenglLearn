#pragma once

#include <utility>

#include "object_ptr.h"
#include "Render/Public/RHICommand/FrameCommand/rhi_frame_command.h"
#include "Render/Public/RHICommand/FrameCommand/rhi_frame_command_buffer.h"

namespace Render {

// A small, backend-neutral recording facade. It only validates frame command
// ordering; resource ownership and submission remain RHIDevice responsibilities.
class RHIFrameEncoder {
public:
    explicit RHIFrameEncoder(ObjectWeakPtr<RHIFrameCommandBuffer> buffer)
        : buffer_(std::move(buffer)) {}

    RHIFrameEncoder(const RHIFrameEncoder&) = delete;
    RHIFrameEncoder& operator=(const RHIFrameEncoder&) = delete;
    RHIFrameEncoder(RHIFrameEncoder&&) noexcept = default;
    RHIFrameEncoder& operator=(RHIFrameEncoder&&) noexcept = default;

    bool Begin(const RHICommand::BeginFrame& command) {
        if (!buffer_ || state_ != State::Initial) return false;
        state_ = buffer_->PushCommand(command) ? State::Recording : State::Initial;
        return state_ == State::Recording;
    }

    bool SetViewport(const RHICommand::SetViewport& command) {
        return Record(command);
    }

    bool SetScissor(const RHICommand::SetScissor& command) {
        return Record(command);
    }

    bool BindPipeline(RenderResourceHandle<PipelineSpec> pipeline) {
        return Record(RHICommand::SetPipeline{pipeline});
    }

    bool BindMesh(RenderResourceHandle<VertexBufferSpec> mesh) {
        return Record(RHICommand::SetVertexBuffer{mesh});
    }

    bool BindTexture(RenderResourceHandle<RHITextureSpec> texture, uint32_t slot) {
        return Record(RHICommand::BindTexture{texture, slot});
    }

    bool BindUniformBuffer(
        RenderResourceHandle<UniformBufferSpec> buffer,
        uint32_t binding,
        uint32_t offset = 0,
        uint32_t size = 0
    ) {
        return Record(RHICommand::BindUniformBuffer{buffer, binding, offset, size});
    }

    template <typename T>
    bool UpdateUniformBuffer(
        RenderResourceHandle<UniformBufferSpec> buffer,
        const T& value,
        uint32_t offset = 0
    ) {
        return Record(RHICommand::MakeUniformUpdate(buffer, value, offset));
    }

    bool Draw(const RHICommand::Draw& command = {}) {
        return Record(command);
    }

    bool DrawIndexed(const RHICommand::DrawIndexed& command = {}) {
        return Record(command);
    }

    bool End(bool present = true) {
        if (!buffer_ || state_ != State::Recording) return false;
        if (!buffer_->PushCommand(RHICommand::EndFrame{present})) return false;
        state_ = State::Ended;
        return true;
    }

    bool IsRecording() const { return state_ == State::Recording; }
    bool IsEnded() const { return state_ == State::Ended; }

    ObjectWeakPtr<RHIFrameCommandBuffer> GetCommandBuffer() const {
        return buffer_;
    }

private:
    enum class State { Initial, Recording, Ended };

    template <typename T>
    bool Record(const T& command) {
        return buffer_ && state_ == State::Recording && buffer_->PushCommand(command);
    }

    ObjectWeakPtr<RHIFrameCommandBuffer> buffer_;
    State state_ = State::Initial;
};

} // namespace Render
