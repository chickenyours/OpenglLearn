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

    private:
        bool InitGlfw();
        void ReleaseGlfw();

    private:
        ApplicationConfig config_;
        GLFWwindow* window_ = nullptr;
        bool isInited_ = false;
    };

}
