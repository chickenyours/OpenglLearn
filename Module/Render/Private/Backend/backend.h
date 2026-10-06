#pragma once

#include <optional>
#include <string>
#include <vector>
#include <unordered_map>

#include "Render/Public/RHICommand/rhi_command.h"
#include "Render/Public/RHICommand/rhi_pipeline_command.h"
#include "Render/Public/RHIResourceType/rhi_resource_type.h"
#include "Render/Private/Backend/rhi_backend_context.h"


namespace Render{
    struct BackendUniformBlock {
        uint32_t bytes=0,binding=0;
        std::unordered_map<std::string,uint32_t> offsets;
    };
    // 后端类方法必须要只在同一个线程内调用
    class IBackend{
        friend class RHIDevice;
        protected:
            RHIBackContext rhiContext_;
            IBackend(const RHIBackContext& rhiContext):rhiContext_(rhiContext){}
            virtual void Init(void* specData) = 0;
            virtual void Shutdown() = 0;
            // buffer
            virtual RenderResourceHandle<VertexBufferSpec> CreateVertexBuffer(const CreateVertexBufferCommand& command) = 0;
            virtual bool UpdateVertexBuffer(const UpdateVertexBufferCommand& command) = 0;
            virtual void DeleteVertexBuffer(const DeleteVertexBufferCommand& command) = 0;
            virtual RenderResourceHandle<UniformBufferSpec> CreateUniformBuffer(const CreateUniformBufferCommand& command) = 0;
            virtual void DeleteUniformBuffer(const DeleteUniformBufferCommand& command) = 0;
            // shader
            virtual RenderResourceHandle<ShaderProgramSpec> CreateGraphicShaderProgram(const CreateGraphicShaderProgramCommand& command) = 0;
            virtual RenderResourceHandle<ShaderSourceSpec> CreateShaderSource(const CreateShaderSourceCommand& command) = 0;
            // Defaults preserve source compatibility for external backends.
            virtual void DeleteShaderSource(const DeleteShaderSourceCommand&) {}
            virtual void DeleteShaderProgram(const DeleteShaderProgramCommand&) {}
            // pipeline
            virtual RenderResourceHandle<PipelineSpec> CreatePipeline(const CreatePipelineCommand&) = 0;
            virtual void DeletePipeline(const DeletePipelineCommand&) = 0;
            // texture
            virtual RenderResourceHandle<RHITextureSpec> CreateTexture(const CreateTextureCommand&) = 0;
            virtual void DeleteTexture(const DeleteTextureCommand&) = 0;
            virtual bool UpdateTexture(const UpdateTextureCommand&) { return false; }
            virtual RenderResourceHandle<RenderTargetSpec> CreateRenderTarget(const CreateRenderTargetCommand&) { return {}; }
            virtual void DeleteRenderTarget(const DeleteRenderTargetCommand&) {}
            // frame
            virtual void SetBackgroundColor(const RHICommand::SetBackgroundColor& command) = 0;
            virtual void Flip(const RHICommand::Flip& command) = 0;
            virtual void SetVertexBuffer(const RHICommand::SetVertexBuffer& command) = 0;
            virtual void SetPipeline(const RHICommand::SetPipeline& command) = 0;
            virtual void Draw(const RHICommand::Draw& command) = 0;
            virtual void DrawInstance(const RHICommand::DrawInstance& command) = 0;
            virtual void DrawRect(const RHICommand::DrawRect& command) = 0;
            virtual void BeginFrame(const RHICommand::BeginFrame& command) = 0;
            virtual void SetRenderTarget(const RHICommand::SetRenderTarget&) {}
            virtual void ResolveRenderTarget(const RHICommand::ResolveRenderTarget&) {}
            virtual void EndFrame(const RHICommand::EndFrame& command) = 0;
            virtual void SetViewport(const RHICommand::SetViewport& command) = 0;
            virtual void SetScissor(const RHICommand::SetScissor& command) = 0;
            virtual void BindTexture(const RHICommand::BindTexture& command) = 0;
            virtual void BindUniformBuffer(const RHICommand::BindUniformBuffer& command) = 0;
            virtual void UpdateUniformBuffer(const RHICommand::UpdateUniformBuffer& command) = 0;
            virtual void DrawIndexed(const RHICommand::DrawIndexed& command) = 0;
        public:
            virtual ~IBackend() = default;

            // Render-thread diagnostics, outside the frontend command protocol.
            virtual bool HardwareRayTracingAvailable() const { return false; }
            virtual std::string DeviceName() const { return "OpenGL"; }
            virtual std::vector<float> ReadTexture(RenderResourceHandle<RHITextureSpec>) { return {}; }
            virtual std::vector<uint8_t> ReadWindow() { return {}; }
            virtual double LastGpuMilliseconds() const { return 0; }
            virtual void AbortFrame() {}
            virtual uint32_t ValidationErrorCount() const { return 0; }
            virtual std::optional<BackendUniformBlock> ReflectUniformBlock(RenderResourceHandle<ShaderProgramSpec>, const std::string&) { return {}; }


    };
    // Registered explicitly, so standalone header-only OpenGL modules do not
    // acquire a link dependency on the Vulkan implementation.
    inline IBackend* (*VulkanBackendFactory)(const RHIBackContext&) = nullptr;
}
