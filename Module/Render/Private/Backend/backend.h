#pragma once

#include <optional>
#include "Render/Private/Backend/rhi_backend_context.h"
#include "Render/Public/RHICommand/rhi_command.h"
#include "Render/Public/RHIResourceType/rhi_resource_type.h"

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
        public:
            virtual ~IBackend() = default;
            

    };
}