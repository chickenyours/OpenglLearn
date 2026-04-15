#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <vector>
#include <atomic>

#include "Render/Public/RHI/rhi_command.h"
#include "Render/Private/RHI/rhi_dual_command_queue.h"
#include "Render/Private/RHI/rhi_backend_executor.h"

struct GLFWwindow;

namespace Render::RHI {

    /**
     * @brief 专职渲染线程
     * 
     * 职责：
     * - 持有图形上下文
     * - 维护运行循环，等待并执行命令
     * - 使用双队列架构：创建队列 + 删除队列
     * - 事件驱动唤醒机制
     * 
     * 运行模式：
     * - 线程启动后进入等待状态
     * - 当有命令投递时，通过 Wakeup() 唤醒线程
     * - 线程唤醒后批量处理所有待处理命令
     * - 处理完毕后继续等待
     * 
     * 上下文交接：
     * - 可以通过 SetGLContext() 设置 OpenGL 上下文和窗口
     * - 渲染线程启动时会在 Initialize 中使上下文成为当前线程上下文
     */
    class RenderThread {
    public:
        RenderThread();
        ~RenderThread();

        RenderThread(const RenderThread&) = delete;
        RenderThread& operator=(const RenderThread&) = delete;

        /**
         * @brief 设置 OpenGL 上下文和窗口（用于上下文交接）
         * @param window GLFW 窗口指针
         * @param context OpenGL 上下文指针 (HGLRC)
         */
        void SetGLContext(GLFWwindow* window, void* context);

        /**
         * @brief 启动渲染线程
         */
        bool Start();

        /**
         * @brief 停止渲染线程
         */
        void Stop();

        /**
         * @brief 唤醒渲染线程执行命令
         * 
         * 注意：所有资源创建/删除操作后必须调用此接口
         */
        void Wakeup();

        /**
         * @brief 投递创建命令到创建队列
         * @param cmd 命令
         */
        void EnqueueCreateCommand(RHICommand&& cmd);

        /**
         * @brief 投递删除命令到删除队列
         * @param cmd 命令
         */
        void EnqueueDestroyCommand(RHICommand&& cmd);

        /**
         * @brief 批量执行一次命令（先删除后创建）
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

        /**
         * @brief 获取双命令队列
         */
        DualCommandQueue& GetCommandQueue() { return queue_; }

    private:
        /**
         * @brief 线程主循环
         */
        void ThreadMain();

    private:
        std::thread thread_;
        mutable std::mutex mutex_;
        std::condition_variable cv_;
        std::atomic<bool> running_{false};
        std::atomic<bool> stopRequested_{false};

        DualCommandQueue queue_;  // 双命令队列
        std::unique_ptr<IRHIBackendExecutor> executor_;
        std::vector<RHICommand> commandBuffer_;  // 用于批量取命令

        // OpenGL 上下文交接
        GLFWwindow* glWindow_ = nullptr;
        void* glContext_ = nullptr;
    };

} // namespace Render::RHI
