#pragma once

#include "Render/Public/RHI/rhi_command.h"

namespace Render::RHI {

    /**
     * @brief 后端命令执行器接口
     */
    class IRHIBackendExecutor {
    public:
        virtual ~IRHIBackendExecutor() = default;

        /**
         * @brief 初始化执行器
         */
        virtual bool Initialize() = 0;

        /**
         * @brief 关闭执行器
         */
        virtual void Shutdown() = 0;

        /**
         * @brief 执行命令
         * @param cmd 要执行的命令
         */
        virtual void Execute(const RHICommand& cmd) = 0;

        /**
         * @brief 获取后端委托上下文
         */
        virtual BackendDelegateContext GetDelegateContext() = 0;
    };

} // namespace Render::RHI
