#pragma once

#include <utility>

#include "object_ptr.h"
#include "Render/Public/RHICommand/FrameCommand/rhi_frame_command.h"
#include "Render/Public/RHICommand/FrameCommand/rhi_frame_command_buffer.h"

namespace Render {

// A small, backend-neutral recording facade. It only validates frame command
// ordering; resource submission remains RHIDevice's responsibility. Call Cancel
// when abandoning an unsubmitted encoder so its resource pins are released.
class RHIFrameEncoder {
public:
    explicit RHIFrameEncoder(ObjectWeakPtr<RHIFrameCommandBuffer> buffer)
        : buffer_(std::move(buffer)),
          recordingGeneration_(buffer_ ? buffer_->recordingGeneration_.load() : 0) {}

    RHIFrameEncoder(const RHIFrameEncoder&) = delete;
    RHIFrameEncoder& operator=(const RHIFrameEncoder&) = delete;
    RHIFrameEncoder(RHIFrameEncoder&&) noexcept = default;
    RHIFrameEncoder& operator=(RHIFrameEncoder&&) noexcept = default;

    bool Begin(const RHICommand::BeginFrame& command) {
        if (!HasRecordingLease() || state_ != State::Initial) return false;
        state_ = buffer_->PushCommand(command) ? State::Recording : State::Initial;
        return state_ == State::Recording;
    }

    bool SetViewport(const RHICommand::SetViewport& command) {
        return Record(command);
    }

    bool SetRenderTarget(const RHICommand::SetRenderTarget& command) {
        if (command.width == 0 || command.height == 0) return false;
        return Record(command);
    }

    bool ResolveRenderTarget(const RHICommand::ResolveRenderTarget& command) {
        if (!command.source.IsValid() || !command.destination.IsValid() ||
            command.source == command.destination || (!command.color && !command.depth)) return false;
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

    bool UpdateUniformBufferBytes(
        RenderResourceHandle<UniformBufferSpec> buffer,
        const void* data,
        uint32_t size,
        uint32_t offset = 0
    ) {
        if (!data || size == 0 || size > RHICommand::MaxInlineUniformBytes) return false;
        RHICommand::UpdateUniformBuffer command{};
        command.buffer = buffer;
        command.offset = offset;
        command.size = size;
        std::memcpy(command.data.data(), data, size);
        return Record(command);
    }

    bool KeepAlive(std::shared_ptr<const void> resource) {
        return state_ == State::Recording && HasRecordingLease() &&
            buffer_->KeepAlive(std::move(resource), recordingGeneration_);
    }

    // Explicitly discard an unsubmitted frame and return its buffer to the pool.
    // A submitted frame belongs to RHIDevice and cannot be cancelled here.
    bool Cancel() {
        if (!HasRecordingLease() || state_ == State::Cancelled) return false;
        if (!buffer_->Discard(buffer_, recordingGeneration_)) return false;
        state_ = State::Cancelled;
        return true;
    }

    bool Draw(const RHICommand::Draw& command = {}) {
        return Record(command);
    }

    bool DrawIndexed(const RHICommand::DrawIndexed& command = {}) {
        return Record(command);
    }

    bool End(bool present = true) {
        if (!HasRecordingLease() || state_ != State::Recording) return false;
        if (!buffer_->PushCommand(RHICommand::EndFrame{present})) return false;
        state_ = State::Ended;
        return true;
    }

    bool IsRecording() const { return state_ == State::Recording && HasRecordingLease(); }
    bool IsEnded() const { return state_ == State::Ended; }

    ObjectWeakPtr<RHIFrameCommandBuffer> GetCommandBuffer() const {
        return buffer_;
    }

private:
    enum class State { Initial, Recording, Ended, Cancelled };

    bool HasRecordingLease() const {
        return buffer_ && buffer_->IsCurrentRecording(recordingGeneration_);
    }

    template <typename T>
    bool Record(const T& command) {
        return HasRecordingLease() && state_ == State::Recording && buffer_->PushCommand(command);
    }

    ObjectWeakPtr<RHIFrameCommandBuffer> buffer_;
    uint64_t recordingGeneration_ = 0;
    State state_ = State::Initial;
};

} // namespace Render
