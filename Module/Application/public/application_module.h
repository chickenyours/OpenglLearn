#pragma once

#include "module.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace Application {

    struct ApplicationConfig {
        int width = 800;
        int height = 600;
        const char* title = "OpenGL Window";
        int majorVersion = 4;
        int minorVersion = 6;
    };

    class ApplicationModule final : public ::IModule {
    public:
        ApplicationModule() = default;
        ~ApplicationModule() override;

        ApplicationModule(const ApplicationModule&) = delete;
        ApplicationModule& operator=(const ApplicationModule&) = delete;

        const char* GetName() const noexcept override;
        bool Startup() override;
        void Shutdown() override;
        bool IsStarted() const noexcept override;

        void SetConfig(const ApplicationConfig& config) noexcept;

        GLFWwindow* GetWindow() noexcept;
        const GLFWwindow* GetWindow() const noexcept;

        using GLProcAddressLoader = void* (*)(const char* name);
        GLProcAddressLoader GetGLProcAddressLoader() const noexcept;

        /**
         * @brief 获取 OpenGL 上下文指针
         * @return OpenGL 上下文指针 (HGLRC)
         */
        void* GetGLContext() noexcept;

        /**
         * @brief 释放当前线程的 OpenGL 上下文（用于交接给渲染线程）
         * @return 如果成功释放则返回 true
         * 
         * 注意：调用此接口后，当前线程不能再使用 OpenGL 调用
         */
        bool ReleaseGLContext() noexcept;

        /**
         * @brief 在指定线程使 OpenGL 上下文成为当前上下文
         * @param window 窗口指针
         * @param context OpenGL 上下文指针
         * @return 如果成功则返回 true
         */
        static bool MakeGLContextCurrent(GLFWwindow* window, void* context) noexcept;

    private:
        bool InitGlfw();
        void ReleaseGlfw();

    private:
        ApplicationConfig config_;
        GLFWwindow* window_ = nullptr;
        void* glContext_ = nullptr;  // OpenGL 上下文指针 (HGLRC)
        bool isInited_ = false;
        bool contextReleased_ = false;  // 上下文是否已释放
    };

}
