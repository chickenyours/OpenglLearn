#pragma once

#include <deque>
#include <vector>
#include <mutex>
#include <atomic>

#include "Render/Public/RHI/rhi_command.h"

namespace Render::RHI {

    /**
     * @brief 双命令队列管理器
     * 
     * 包含两个独立队列：
     * - 创建队列：处理资源创建命令（CreateTexture, CreateBuffer, CreateInputLayout）
     * - 删除队列：处理资源删除命令（DestroyTexture, DestroyBuffer, DestroyInputLayout）
     * 
     * 设计目的：
     * - 分离创建和删除操作，避免资源悬挂引用
     * - 支持批量处理，提高渲染线程执行效率
     * - 线程安全，支持多线程并发投递命令
     */
    class DualCommandQueue {
    public:
        DualCommandQueue() = default;
        ~DualCommandQueue() = default;

        DualCommandQueue(const DualCommandQueue&) = delete;
        DualCommandQueue& operator=(const DualCommandQueue&) = delete;

        /**
         * @brief 推入创建命令
         */
        void PushCreateCommand(RHICommand&& cmd);

        /**
         * @brief 推入删除命令
         */
        void PushDestroyCommand(RHICommand&& cmd);

        /**
         * @brief 弹出所有创建命令
         */
        void PopAllCreateCommands(std::vector<RHICommand>& out);

        /**
         * @brief 弹出所有删除命令
         */
        void PopAllDestroyCommands(std::vector<RHICommand>& out);

        /**
         * @brief 判断创建队列是否为空
         */
        bool CreateQueueEmpty() const;

        /**
         * @brief 判断删除队列是否为空
         */
        bool DestroyQueueEmpty() const;

        /**
         * @brief 判断是否任一队列非空
         */
        bool AnyQueueNonEmpty() const;

        /**
         * @brief 获取创建队列大小
         */
        size_t CreateQueueSize() const;

        /**
         * @brief 获取删除队列大小
         */
        size_t DestroyQueueSize() const;

        /**
         * @brief 获取总命令数
         */
        size_t TotalSize() const;

    private:
        mutable std::mutex createMutex_;
        mutable std::mutex destroyMutex_;
        std::deque<RHICommand> createQueue_;
        std::deque<RHICommand> destroyQueue_;
    };

} // namespace Render::RHI
