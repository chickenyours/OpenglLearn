#pragma once

#include <deque>
#include <vector>
#include <mutex>

#include "Render/Public/RHI/rhi_command.h"

namespace Render::RHI {

    /**
     * @brief 统一 RHI 命令队列
     * 线程安全的 FIFO 队列
     */
    class RHICommandQueue {
    public:
        RHICommandQueue() = default;
        ~RHICommandQueue() = default;

        RHICommandQueue(const RHICommandQueue&) = delete;
        RHICommandQueue& operator=(const RHICommandQueue&) = delete;

        /**
         * @brief 推入命令
         */
        void Push(RHICommand&& cmd);

        /**
         * @brief 弹出所有命令到输出向量
         * @param out 输出向量
         */
        void PopAll(std::vector<RHICommand>& out);

        /**
         * @brief 判断队列是否为空
         */
        bool Empty() const;

        /**
         * @brief 获取队列大小
         */
        size_t Size() const;

    private:
        mutable std::mutex mutex_;
        std::deque<RHICommand> commands_;
    };

} // namespace Render::RHI
