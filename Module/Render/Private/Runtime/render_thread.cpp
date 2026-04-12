#include "Render/Private/Runtime/render_thread.h"
#include "Render/Private/Backend/Opengl/gl_backend_executor.h"
#include <iostream>

namespace Render::RHI {

    RenderThread::RenderThread() 
        : executor_(std::make_unique<GLBackendExecutor>()) {
    }

    RenderThread::~RenderThread() {
        Stop();
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
            cv_.wait(lock, [this] { return running_; });
        }

        std::cout << "[RenderThread] Started successfully." << std::endl;
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

        WakeUp();

        if (thread_.joinable()) {
            thread_.join();
        }

        if (executor_) {
            executor_->Shutdown();
        }

        running_ = false;
        std::cout << "[RenderThread] Stopped." << std::endl;
    }

    void RenderThread::WakeUp() {
        cv_.notify_one();
    }

    void RenderThread::Enqueue(RHICommand&& cmd) {
        bool wasEmpty = queue_.Empty();
        queue_.Push(std::move(cmd));
        
        // 当队列从空变非空时唤醒线程
        if (wasEmpty) {
            WakeUp();
        }
    }

    void RenderThread::DrainOnce() {
        commandBuffer_.clear();
        queue_.PopAll(commandBuffer_);

        if (commandBuffer_.empty()) {
            return;
        }

        for (const auto& cmd : commandBuffer_) {
            executor_->Execute(cmd);
        }
    }

    void RenderThread::ThreadMain() {
        // 初始化执行器
        if (!executor_->Initialize()) {
            std::cerr << "[RenderThread] Failed to initialize executor!" << std::endl;
            return;
        }

        // 标记为运行状态并唤醒等待的线程
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = true;
        }
        cv_.notify_one();

        std::cout << "[RenderThread] Thread main loop started." << std::endl;

        while (true) {
            // 等待命令或停止请求
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { 
                    return stopRequested_ || !queue_.Empty(); 
                });
            }

            // 检查停止请求
            if (stopRequested_) {
                break;
            }

            // 批量执行命令
            DrainOnce();
        }

        std::cout << "[RenderThread] Thread main loop ended." << std::endl;
    }

} // namespace Render::RHI
