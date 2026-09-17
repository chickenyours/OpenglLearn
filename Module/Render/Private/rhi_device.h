#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>

#include "object_ptr.h"
#include "Render/Public/render_backend_context.h"
#include "Render/Public/RHICommand/FrameCommand/rhi_frame_command.h"
#include "Render/Public/RHICommand/FrameCommand/rhi_frame_command_buffer.h"
#include "Render/Public/RHICommand/FrameCommand/rhi_frame_encoder.h"
#include "Render/Public/RHICommand/rhi_pipeline_command.h"
#include "Render/Public/RHICommand/rhi_shader_command.h"
#include "Render/Private/Backend/rhi_backend_context.h"
#include "Render/Private/Backend/backend.h"
#include "Render/Private/rhi_command_return_system.h"
#include "Render/Private/Backend/Opengl/backend.h"

namespace Render {

using RHICommands = RHICommandSystem<
    CreateVertexBufferCommand,
    UpdateVertexBufferCommand,
    DeleteVertexBufferCommand,
    CreateUniformBufferCommand,
    DeleteUniformBufferCommand,
    CreateShaderSourceCommand,
    CreateGraphicShaderProgramCommand,
    CreatePipelineCommand,
    DeletePipelineCommand,
    CreateTextureCommand,
    DeleteTextureCommand,
    BackDoorExecutionCommand,
    FrameCommands
>;

class RHIDevice {
private:
    RHICommands commandSystem_;
    RHIResourcePool resourcePool_;
    RHIFrameCommandBufferPool frameCommandPool_;
    ObjectPtr<IBackend> backend_;
    std::thread renderThread_;
    BackendType backendType_ = BackendType::Opengl;
    std::condition_variable rendercv_;
    std::mutex waitMutex_;
    std::atomic_bool isRun_{false};

public:
    RHICommandReturnSystem returnSystem;

private:
    template <typename T>
    void threadAny_PushCommand(T command) {
        commandSystem_.GetQueue<T>().threadAny_Push(1, &command);
        rendercv_.notify_one();
    }

    template <typename T>
    static bool ReadFrameCommand(
        const RHIFrameCommandBuffer& buffer,
        size_t& cursor,
        T& output
    ) {
        static_assert(std::is_trivially_copyable_v<T>);
        if (cursor + sizeof(T) > buffer.memory_p) return false;
        std::memcpy(&output, buffer.bytebuffer_.data() + cursor, sizeof(T));
        cursor += sizeof(T);
        return true;
    }

    template <typename T, typename Method>
    bool DispatchFrameCommand(
        const RHIFrameCommandBuffer& buffer,
        size_t& cursor,
        Method method
    ) {
        T command{};
        if (!ReadFrameCommand(buffer, cursor, command)) {
            LOG_ERROR("RHIDevice", "truncated frame command buffer");
            return false;
        }
        (backend_.Get()->*method)(command);
        return true;
    }

    bool thread_ProcessFrameCommands(const RHIFrameCommandBuffer& buffer) {
        size_t cursor = 0;
        for (size_t i = 0; i < buffer.commandCount; ++i) {
            CommandId id{};
            if (!ReadFrameCommand(buffer, cursor, id) ||
                !RHICommand::FrameCommandsSet::valid(id)) {
                LOG_ERROR("RHIDevice", "invalid frame command id");
                return false;
            }

            bool ok = false;
            switch (id.value) {
            case 0:
                ok = DispatchFrameCommand<RHICommand::SetBackgroundColor>(
                    buffer, cursor, &IBackend::SetBackgroundColor); break;
            case 1:
                ok = DispatchFrameCommand<RHICommand::Flip>(
                    buffer, cursor, &IBackend::Flip); break;
            case 2:
                ok = DispatchFrameCommand<RHICommand::SetVertexBuffer>(
                    buffer, cursor, &IBackend::SetVertexBuffer); break;
            case 3:
                ok = DispatchFrameCommand<RHICommand::SetPipeline>(
                    buffer, cursor, &IBackend::SetPipeline); break;
            case 4:
                ok = DispatchFrameCommand<RHICommand::Draw>(
                    buffer, cursor, &IBackend::Draw); break;
            case 5:
                ok = DispatchFrameCommand<RHICommand::DrawRect>(
                    buffer, cursor, &IBackend::DrawRect); break;
            case 6:
                ok = DispatchFrameCommand<RHICommand::BeginFrame>(
                    buffer, cursor, &IBackend::BeginFrame); break;
            case 7:
                ok = DispatchFrameCommand<RHICommand::EndFrame>(
                    buffer, cursor, &IBackend::EndFrame); break;
            case 8:
                ok = DispatchFrameCommand<RHICommand::SetViewport>(
                    buffer, cursor, &IBackend::SetViewport); break;
            case 9:
                ok = DispatchFrameCommand<RHICommand::SetScissor>(
                    buffer, cursor, &IBackend::SetScissor); break;
            case 10:
                ok = DispatchFrameCommand<RHICommand::BindTexture>(
                    buffer, cursor, &IBackend::BindTexture); break;
            case 11:
                ok = DispatchFrameCommand<RHICommand::BindUniformBuffer>(
                    buffer, cursor, &IBackend::BindUniformBuffer); break;
            case 12:
                ok = DispatchFrameCommand<RHICommand::UpdateUniformBuffer>(
                    buffer, cursor, &IBackend::UpdateUniformBuffer); break;
            case 13:
                ok = DispatchFrameCommand<RHICommand::DrawIndexed>(
                    buffer, cursor, &IBackend::DrawIndexed); break;
            case 14:
                ok = DispatchFrameCommand<RHICommand::DrawInstance>(
                    buffer, cursor, &IBackend::DrawInstance); break;
            default:
                break;
            }
            if (!ok) return false;
        }
        return cursor == buffer.memory_p;
    }

    template <typename Command, typename CreateFn>
    void ProcessCreateQueue(CreateFn create) {
        auto& queue = commandSystem_.GetQueue<Command>();
        while (!queue.thread_IsConsumeQueueEmpty()) {
            Command command = queue.thread_Pop();
            auto handle = (backend_.Get()->*create)(command);
            if (command.OnFinish) {
                auto callback = std::move(command.OnFinish);
                returnSystem.callbacks.push(
                    [callback = std::move(callback), handle]() mutable {
                        callback(handle);
                    });
            }
        }
    }

    template <typename Command, typename DeleteFn>
    void ProcessDeleteQueue(DeleteFn destroy) {
        auto& queue = commandSystem_.GetQueue<Command>();
        while (!queue.thread_IsConsumeQueueEmpty()) {
            Command command = queue.thread_Pop();
            (backend_.Get()->*destroy)(command);
            if (command.OnFinish) {
                returnSystem.callbacks.push(std::move(command.OnFinish));
            }
        }
    }

    void thread_ProcessCurrentQueue() {
        ProcessCreateQueue<CreateVertexBufferCommand>(&IBackend::CreateVertexBuffer);
        auto& meshUpdates = commandSystem_.GetQueue<UpdateVertexBufferCommand>();
        while (!meshUpdates.thread_IsConsumeQueueEmpty()) {
            UpdateVertexBufferCommand command = meshUpdates.thread_Pop();
            const bool succeeded = backend_->UpdateVertexBuffer(command);
            if (command.OnFinish) {
                auto callback = std::move(command.OnFinish);
                returnSystem.callbacks.push(
                    [callback = std::move(callback), succeeded]() mutable {
                        callback(succeeded);
                    });
            }
        }
        ProcessDeleteQueue<DeleteVertexBufferCommand>(&IBackend::DeleteVertexBuffer);
        ProcessCreateQueue<CreateUniformBufferCommand>(&IBackend::CreateUniformBuffer);
        ProcessDeleteQueue<DeleteUniformBufferCommand>(&IBackend::DeleteUniformBuffer);
        ProcessCreateQueue<CreateTextureCommand>(&IBackend::CreateTexture);
        ProcessDeleteQueue<DeleteTextureCommand>(&IBackend::DeleteTexture);
        ProcessCreateQueue<CreateShaderSourceCommand>(&IBackend::CreateShaderSource);
        ProcessCreateQueue<CreateGraphicShaderProgramCommand>(
            &IBackend::CreateGraphicShaderProgram);
        ProcessCreateQueue<CreatePipelineCommand>(&IBackend::CreatePipeline);
        ProcessDeleteQueue<DeletePipelineCommand>(&IBackend::DeletePipeline);

        auto& frames = commandSystem_.GetQueue<FrameCommands>();
        while (!frames.thread_IsConsumeQueueEmpty()) {
            FrameCommands command = frames.thread_Pop();
            ObjectWeakPtr<RHIFrameCommandBuffer> buffer = command.buffer;
            bool succeeded = false;
            if (buffer) succeeded = thread_ProcessFrameCommands(*buffer);
            if (buffer) {
                buffer->Reset();
                frameCommandPool_.threadAny_Recycle(buffer);
            }
            if (command.OnFinish) {
                returnSystem.callbacks.push(std::move(command.OnFinish));
            }
            if (!succeeded) {
                LOG_ERROR("RHIDevice", "frame submission failed");
            }
        }

        auto& backDoor = commandSystem_.GetQueue<BackDoorExecutionCommand>();
        while (!backDoor.thread_IsConsumeQueueEmpty()) {
            BackDoorExecutionCommand command = backDoor.thread_Pop();
            if (command.execution) command.execution();
            if (command.OnFinish) {
                returnSystem.callbacks.push(std::move(command.OnFinish));
            }
        }
    }

    void thread_InitBackend(BackendType type, void* specData) {
        RHIBackContext context{};
        context.resourcePool = &resourcePool_;
        if (type == BackendType::Opengl) {
            backend_ = ObjectPtr<IBackend>(new OpenglBackend(context));
            backend_->Init(specData);
        }
    }

    void thread_run(BackendType type, OpenglBackendContext specData) {
        thread_InitBackend(type, &specData);
        if (!backend_) {
            isRun_.store(false);
            return;
        }

        for (;;) {
            std::unique_lock<std::mutex> lock(waitMutex_);
            rendercv_.wait(lock, [&]() {
                return !isRun_.load() || commandSystem_.hasPendingCommands();
            });
            const bool hasPending = commandSystem_.hasPendingCommands();
            if (!isRun_.load() && !hasPending) break;
            lock.unlock();
            if (hasPending) {
                commandSystem_.thread_SwitchQueues();
                thread_ProcessCurrentQueue();
            }
        }

        backend_->Shutdown();
        backend_.Reset();
        LOG_INFO("RHIDevice", "render thread stopped");
    }

public:
    RHIDevice() = default;
    RHIDevice(const RHIDevice&) = delete;
    RHIDevice& operator=(const RHIDevice&) = delete;

    ~RHIDevice() { StopAndRelease(); }

    void Run(BackendType type, OpenglBackendContext specData) {
        bool expected = false;
        if (isRun_.compare_exchange_strong(expected, true)) {
            backendType_ = type;
            renderThread_ = std::thread(&RHIDevice::thread_run, this, type, specData);
        }
    }

    void StopAndRelease() {
        if (isRun_.exchange(false)) rendercv_.notify_one();
        if (renderThread_.joinable()) renderThread_.join();
    }

    bool IsRunning() const { return isRun_.load(); }

    const RHIResourcePool& GetResourcePool() const { return resourcePool_; }

    void async_CreateVertexBuffer(
        VertexLayout vertexLayout,
        uint32_t numVertex = 0,
        void* data = nullptr,
        CreateVertexBufferCallback callback = nullptr
    ) {
        size_t stride = 0;
        for (VertexFieldType type : vertexLayout.typeSlots) {
            switch (type) {
            case VertexFieldType::Byte: stride += 1; break;
            case VertexFieldType::Float:
            case VertexFieldType::Int: stride += 4; break;
            case VertexFieldType::Vec2: stride += 8; break;
            case VertexFieldType::Vec3: stride += 12; break;
            case VertexFieldType::Vec4:
            case VertexFieldType::Mat2: stride += 16; break;
            case VertexFieldType::Mat3: stride += 36; break;
            case VertexFieldType::Mat4: stride += 64; break;
            }
        }
        CreateMeshBufferDesc desc{};
        desc.vertexLayout = std::move(vertexLayout);
        desc.vertexCount = numVertex;
        desc.vertexData = data;
        desc.vertexByteSize = stride * numVertex;
        async_CreateMeshBuffer(desc, std::move(callback));
    }

    void async_CreateMeshBuffer(
        const CreateMeshBufferDesc& desc,
        CreateVertexBufferCallback callback = nullptr
    ) {
        CreateVertexBufferCommand command{};
        command.vertexLayout = desc.vertexLayout;
        command.numVertex = desc.vertexCount;
        command.numIndex = desc.indexCount;
        command.indexType = desc.indexType;
        command.usage = desc.usage;
        command.OnFinish = std::move(callback);
        if (desc.vertexData && desc.vertexByteSize > 0) {
            command.vertexData.resize(desc.vertexByteSize);
            std::memcpy(command.vertexData.data(), desc.vertexData, desc.vertexByteSize);
        }
        if (desc.indexData && desc.indexByteSize > 0) {
            command.indexData.resize(desc.indexByteSize);
            std::memcpy(command.indexData.data(), desc.indexData, desc.indexByteSize);
        }
        threadAny_PushCommand(std::move(command));
    }

    void async_DeleteVertexBuffer(
        RenderResourceHandle<VertexBufferSpec> handle,
        OnVertexBufferFinishCallback callback = nullptr
    ) {
        threadAny_PushCommand(DeleteVertexBufferCommand{handle, std::move(callback)});
    }

    void async_UpdateMeshBuffer(
        RenderResourceHandle<VertexBufferSpec> handle,
        const UpdateMeshBufferDesc& desc,
        UpdateVertexBufferCallback callback = nullptr
    ) {
        UpdateVertexBufferCommand command{};
        command.handle = handle;
        command.numVertex = desc.vertexCount;
        command.numIndex = desc.indexCount;
        command.indexType = desc.indexType;
        command.OnFinish = std::move(callback);
        if (desc.vertexData && desc.vertexByteSize > 0) {
            command.vertexData.resize(desc.vertexByteSize);
            std::memcpy(command.vertexData.data(), desc.vertexData, desc.vertexByteSize);
        }
        if (desc.indexData && desc.indexByteSize > 0) {
            command.indexData.resize(desc.indexByteSize);
            std::memcpy(command.indexData.data(), desc.indexData, desc.indexByteSize);
        }
        threadAny_PushCommand(std::move(command));
    }

    void async_CreateUniformBuffer(
        const CreateUniformBufferDesc& desc,
        CreateUniformBufferCallback callback = nullptr
    ) {
        CreateUniformBufferCommand command{};
        command.byteSize = desc.byteSize;
        command.usage = desc.usage;
        command.OnFinish = std::move(callback);
        if (desc.initialData && desc.byteSize > 0) {
            command.initialData.resize(desc.byteSize);
            std::memcpy(command.initialData.data(), desc.initialData, desc.byteSize);
        }
        threadAny_PushCommand(std::move(command));
    }

    void async_DeleteUniformBuffer(
        RenderResourceHandle<UniformBufferSpec> handle,
        OnVertexBufferFinishCallback callback = nullptr
    ) {
        threadAny_PushCommand(DeleteUniformBufferCommand{handle, std::move(callback)});
    }

    void async_CreateGraphicShader(
        const CreateGraphicShaderDesc& desc,
        OnCreateGraphicShaderFinish callback = nullptr
    ) {
        threadAny_PushCommand(CreateGraphicShaderProgramCommand{desc, std::move(callback)});
    }

    void async_CreateShaderSource(
        const CreateShaderSourceDesc& desc,
        OnCreateShaderSourceFinish callback = nullptr
    ) {
        CreateShaderSourceCommand command{};
        command.type = desc.type;
        if (desc.codeSource && desc.size > 0) {
            command.source.assign(desc.codeSource, desc.size);
        }
        command.OnFinish = std::move(callback);
        threadAny_PushCommand(std::move(command));
    }

    void async_CreatePipeline(
        const CreatePipelineDesc& desc,
        OnCreatePipelineCommandFinish callback = nullptr
    ) {
        threadAny_PushCommand(CreatePipelineCommand{desc, std::move(callback)});
    }

    void async_DeletePipeline(
        RenderResourceHandle<PipelineSpec> handle,
        OnDeletePipelineCommandFinish callback = nullptr
    ) {
        threadAny_PushCommand(DeletePipelineCommand{handle, std::move(callback)});
    }

    void async_CreateTexture(
        const CreateRHITextureSpec& spec,
        CreateTextureCallback callback = nullptr
    ) {
        CreateTextureCommand command{};
        command.spec = spec;
        command.spec.data = nullptr;
        command.OnFinish = std::move(callback);
        const uint64_t byteSize = static_cast<uint64_t>(spec.width) * spec.height *
            GetTextureFormatByteSize(spec.textureUseType);
        if (spec.data && byteSize > 0 &&
            byteSize <= std::numeric_limits<size_t>::max()) {
            command.data.resize(static_cast<size_t>(byteSize));
            std::memcpy(command.data.data(), spec.data, command.data.size());
        }
        threadAny_PushCommand(std::move(command));
    }

    void async_DeleteTexture(
        RenderResourceHandle<RHITextureSpec> handle,
        DeleteTextureCallback callback = nullptr
    ) {
        threadAny_PushCommand(DeleteTextureCommand{handle, std::move(callback)});
    }

    bool async_SubmitFrameCommands(
        ObjectWeakPtr<RHIFrameCommandBuffer> commandBuffer,
        std::function<void()> callback = nullptr
    ) {
        if (!isRun_.load() || !commandBuffer || commandBuffer->GetCommandCount() == 0 ||
            !commandBuffer->Seal()) {
            return false;
        }
        threadAny_PushCommand(FrameCommands{commandBuffer, std::move(callback)});
        return true;
    }

    void async_ExecuteCode(
        std::function<void()> execution,
        std::function<void()> callback = nullptr
    ) {
        threadAny_PushCommand(
            BackDoorExecutionCommand{std::move(execution), std::move(callback)});
    }

    RHIFrameCommandBufferPool* GetRHIFrameCommandBufferPool() {
        return &frameCommandPool_;
    }

    RHIFrameEncoder BeginFrame(const RHICommand::BeginFrame& desc) {
        RHIFrameEncoder encoder(frameCommandPool_.threadAny_GetBuffer());
        encoder.Begin(desc);
        return encoder;
    }
};

} // namespace Render
