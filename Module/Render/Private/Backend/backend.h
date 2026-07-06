#pragma once

#include <optional>

#include "Render/Public/RHICommand/rhi_command.h"
#include "Render/Public/RHIResourceType/rhi_resource_type.h"
#include "Render/Private/Backend/rhi_backend_context.h"


namespace Render{
    // 后端类方法必须要只在同一个线程内调用
    class IBackend{
        friend class RHIDevice;
        protected:
            RHIBackContext rhiContext_;
            IBackend(const RHIBackContext& rhiContext):rhiContext_(rhiContext){}
            virtual void Init(void* specData) = 0;
            // buffer
            virtual RenderResourceHandle<VertexBufferSpec> CreateVertexBuffer(const CreateVertexBufferCommand& command) = 0;
            virtual void DeleteVertexBuffer(const DeleteVertexBufferCommand& command) = 0;
            // shader
            virtual RenderResourceHandle<ShaderProgramSpec> CreateGraphicShaderProgram(const CreateGraphicShaderProgramCommand& command) = 0;
            virtual RenderResourceHandle<ShaderSourceSpec> CreateShaderSource(const CreateShaderSourceCommand& command) = 0;
            // pipeline
            virtual RenderResourceHandle<PipelineSpec> CreatePipeline(const CreatePipelineCommand&) = 0;
            virtual void DeletePipeline(const DeletePipelineCommand&) = 0;
            // frame 
            virtual void SetBackgroundColor(const RHICommand::SetBackgroundColor& command) = 0;
            virtual void Flip(const RHICommand::Flip& command) = 0;
            virtual void SetVertexBuffer(const RHICommand::SetVertexBuffer& command) = 0;
            virtual void SetPipeline(const RHICommand::SetPipeline& command) = 0;
            virtual void Draw(const RHICommand::Draw& command) = 0;
            virtual void DrawInstance(const RHICommand::DrawInstance& command) = 0;
            virtual void DrawRect(const RHICommand::DrawRect& command) = 0;
        public:
            virtual ~IBackend() = default;
            

    };
}