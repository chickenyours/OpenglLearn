#include "Render/Private/Runtime/render_thread.h"
#include "Render/Private/Backend/Opengl/gl_backend_executor.h"
#include "engine/DebugTool/ConsoleHelp/color_log.h"

#include <GLFW/glfw3.h>

namespace Render::RHI {

    RenderThread::RenderThread()
        : executor_(std::make_unique<GLBackendExecutor>()) {
    }

    RenderThread::~RenderThread() {
        Stop();
    }

    void RenderThread::SetGLContext(GLFWwindow* window, void* context) {
        glWindow_ = window;
        glContext_ = context;
    }

    bool RenderThread::Start() {
        if (running_) {
            return true;
        }

        stopRequested_ = false;

        // 启动线程
        thread_ = std::thread(&RenderThread::ThreadMain, this);

        // 等待线程真正启动
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return running_.load(); });
        }

        LOG_INFO("RenderThread", "Started successfully.");
        return true;
    }

    void RenderThread::Stop() {
        if (!running_) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopRequested_ = true;
        }

        Wakeup();

        if (thread_.joinable()) {
            thread_.join();
        }

        if (executor_) {
            executor_->Shutdown();
        }

        running_ = false;
        LOG_INFO("RenderThread", "Stopped.");
    }

    void RenderThread::Wakeup() {
        cv_.notify_one();
    }

    void RenderThread::EnqueueCreateCommand(RHICommand&& cmd) {
        queue_.PushCreateCommand(std::move(cmd));
    }

    void RenderThread::EnqueueDestroyCommand(RHICommand&& cmd) {
        queue_.PushDestroyCommand(std::move(cmd));
    }

    void RenderThread::DrainOnce() {
        commandBuffer_.clear();
        
        // 先处理删除命令，再处理创建命令
        // 这样可以确保先释放资源，再创建新资源
        queue_.PopAllDestroyCommands(commandBuffer_);
        queue_.PopAllCreateCommands(commandBuffer_);

        if (commandBuffer_.empty()) {
            return;
        }

        for (const auto& cmd : commandBuffer_) {
            executor_->Execute(cmd);
        }
    }

    void RenderThread::ThreadMain() {
        // 使 OpenGL 上下文成为当前线程上下文
        // GLFW 的上下文与窗口绑定，通过 glfwMakeContextCurrent 切换
        if (glWindow_) {
            glfwMakeContextCurrent(glWindow_);
            LOG_INFO("RenderThread", "OpenGL context acquired in render thread");
        }

        // 初始化执行器
        if (!executor_->Initialize()) {
            LOG_ERROR("RenderThread", "Failed to initialize executor!");
            return;
        }

        // 标记为运行状态并唤醒等待的线程
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = true;
        }
        cv_.notify_one();

        LOG_INFO("RenderThread", "Thread main loop started.");

        while (true) {
            // 等待命令或停止请求
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] {
                    return stopRequested_.load() || queue_.AnyQueueNonEmpty();
                });
            }

            // 检查停止请求
            if (stopRequested_) {
                break;
            }

            // 批量执行命令
            DrainOnce();
        }

        LOG_INFO("RenderThread", "Thread main loop ended.");
    }

} // namespace Render::RHI
