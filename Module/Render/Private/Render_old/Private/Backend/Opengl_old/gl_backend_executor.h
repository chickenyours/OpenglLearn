#pragma once

#include <memory>

#include "Render/Private/RHI/rhi_backend_executor.h"
#include "Render/Private/RHI/rhi_backend_resource_table.h"

namespace Render::Backend::OpenGL {
    class GLDevice;
}

namespace Render::RHI {

    /**
     * @brief OpenGL 后端命令执行器
     */
    class GLBackendExecutor final : public IRHIBackendExecutor {
    public:
        GLBackendExecutor();
        ~GLBackendExecutor() override;

        bool Initialize() override;
        void Shutdown() override;
        void Execute(const RHICommand& cmd) override;

        BackendDelegateContext GetDelegateContext() override;

        /**
         * @brief 获取资源表
         */
        RHIBackendResourceTable& GetResourceTable() { return resources_; }

    private:
        // 命令执行函数
        void ExecuteCreateBuffer(const CreateBufferCmd& cmd);
        void ExecuteUpdateBuffer(const UpdateBufferCmd& cmd);
        void ExecuteDestroyBuffer(const DestroyBufferCmd& cmd);
        void ExecuteCopyBuffer(const CopyBufferCmd& cmd);

        void ExecuteCreateEmptyTexture(const CreateEmptyTextureCmd& cmd);
        void ExecuteCreateTexture(const CreateTextureCmd& cmd);
        void ExecuteUploadTexture(const UploadTextureCmd& cmd);
        void ExecuteUpdateTexture(const UpdateTextureCmd& cmd);
        void ExecuteDestroyTexture(const DestroyTextureCmd& cmd);

        void ExecuteCreateInputLayout(const CreateInputLayoutCmd& cmd);
        void ExecuteDestroyInputLayout(const DestroyInputLayoutCmd& cmd);

        void ExecuteBeginFrame(const BeginFrameCmd& cmd);
        void ExecuteEndFrame(const EndFrameCmd& cmd);

        void ExecuteDelegate(const ExecuteDelegateCmd& cmd);

    private:
        RHIBackendResourceTable resources_;
        bool initialized_ = false;
        void* nativeContext_ = nullptr;  // OpenGL 上下文指针
    };

} // namespace Render::RHI
