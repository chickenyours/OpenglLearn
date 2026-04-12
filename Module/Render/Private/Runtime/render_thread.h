#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <vector>

#include "Render/Public/RHI/rhi_command.h"
#include "Render/Private/RHI/rhi_command_queue.h"
#include "Render/Private/RHI/rhi_backend_executor.h"

namespace Render::RHI {

    /**
     * @brief 专职渲染线程
     * 持有图形上下文，维护运行循环，等待并执行命令
     */
    class RenderThread {
    public:
        RenderThread();
        ~RenderThread();

        RenderThread(const RenderThread&) = delete;
        RenderThread& operator=(const RenderThread&) = delete;

        /**
         * @brief 启动渲染线程
         */
        bool Start();

        /**
         * @brief 停止渲染线程
         */
        void Stop();

        /**
         * @brief 唤醒渲染线程（当队列从空变非空时调用）
         */
        void WakeUp();

        /**
         * @brief 投递命令到队列
         * @param cmd 命令
         */
        void Enqueue(RHICommand&& cmd);

        /**
         * @brief 批量执行一次命令
         * 在渲染线程循环中被调用
         */
        void DrainOnce();

        /**
         * @brief 判断线程是否正在运行
         */
        bool IsRunning() const { return running_; }

        /**
         * @brief 获取后端执行器
         */
        IRHIBackendExecutor* GetExecutor() { return executor_.get(); }

    private:
        /**
         * @brief 线程主循环
         */
        void ThreadMain();

    private:
        std::thread thread_;
        mutable std::mutex mutex_;
        std::condition_variable cv_;
        bool running_ = false;
        bool stopRequested_ = false;

        RHICommandQueue queue_;
        std::unique_ptr<IRHIBackendExecutor> executor_;
        std::vector<RHICommand> commandBuffer_; // 用于批量取命令
    };

} // namespace Render::RHI
