#include "application_module.h"

#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Application {

    ApplicationModule::~ApplicationModule() {
        Shutdown();
    }

    const char* ApplicationModule::GetName() const noexcept {
        return "Application";
    }

    bool ApplicationModule::Startup() {
        if (isInited_) {
            return true;
        }

        if (!InitGlfw()) {
            return false;
        }

        isInited_ = true;
        return true;
    }

    void ApplicationModule::Shutdown() {
        if (!isInited_) {
            return;
        }

        ReleaseGlfw();
        isInited_ = false;
    }

    bool ApplicationModule::IsStarted() const noexcept {
        return isInited_;
    }

    void ApplicationModule::SetConfig(const ApplicationConfig& config) noexcept {
        config_ = config;
    }

    GLFWwindow* ApplicationModule::GetWindow() noexcept {
        return window_;
    }

    const GLFWwindow* ApplicationModule::GetWindow() const noexcept {
        return window_;
    }

    ApplicationModule::GLProcAddressLoader ApplicationModule::GetGLProcAddressLoader() const noexcept {
        return reinterpret_cast<GLProcAddressLoader>(&glfwGetProcAddress);
    }

    void* ApplicationModule::GetGLContext() noexcept {
        // GLFW 不直接暴露上下文句柄，返回窗口指针即可
        // 渲染线程通过 glfwMakeContextCurrent(window) 获取上下文
        return window_;
    }

    bool ApplicationModule::ReleaseGLContext() noexcept {
        // 释放当前线程的 OpenGL 上下文
        glfwMakeContextCurrent(nullptr);
        contextReleased_ = true;
        return true;
    }

    bool ApplicationModule::MakeGLContextCurrent(GLFWwindow* window, void* context) noexcept {
        if (!window) {
            return false;
        }
        // 使用 GLFW API 使上下文成为当前线程上下文
        glfwMakeContextCurrent(window);
        return true;
    }

    bool ApplicationModule::InitGlfw() {
        if (!glfwInit()) {
            std::cerr << "GLFW init failed\n";
            return false;
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, config_.majorVersion);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, config_.minorVersion);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        window_ = glfwCreateWindow(
            config_.width,
            config_.height,
            config_.title,
            nullptr,
            nullptr
        );

        if (!window_) {
            glfwTerminate();
            std::cerr << "GLFW window creation failed\n";
            return false;
        }

        glfwMakeContextCurrent(window_);

        // GLFW 的上下文与窗口绑定，保存窗口指针即可
        glContext_ = window_;

        // 加载 OpenGL 函数
        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(&glfwGetProcAddress))) {
            std::cerr << "Failed to initialize GLAD\n";
            return false;
        }

        std::string version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        std::cout << "OpenGL Version: " << version << std::endl;

        return true;
    }

    void ApplicationModule::ReleaseGlfw() {
        if (window_) {
            // 如果上下文还未释放，先释放
            if (!contextReleased_) {
                glfwMakeContextCurrent(nullptr);
            }
            glfwDestroyWindow(window_);
            window_ = nullptr;
        }
        glContext_ = nullptr;
        contextReleased_ = false;
        glfwTerminate();
    }

}
