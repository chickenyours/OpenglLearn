#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <GLFW/glfw3.h>

#include "Render/Public/render_backend_context.h"
#include "DebugTool/ConsoleHelp/color_log.h"

namespace ApplicationWindow {

struct WindowSpec {
    uint32_t width = 1280;
    uint32_t height = 720;
    std::string title = "Application";
    bool vsync = true;
};

class Window {
public:
    Window() = default;

    explicit Window(WindowSpec spec)
        : spec_(std::move(spec)) {}

    ~Window() {
        Shutdown();
    }

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    Window(Window&& other) noexcept {
        MoveFrom(std::move(other));
    }

    Window& operator=(Window&& other) noexcept {
        if (this != &other) {
            Shutdown();
            MoveFrom(std::move(other));
        }

        return *this;
    }

    const WindowSpec& GetSpec() const {
        return spec_;
    }

    bool IsActivated() const {
        return isActivated_;
    }

    GLFWwindow* GetNativeWindow() const {
        return window_;
    }

    bool Activate() {
        if (isActivated_) {
            return true;
        }

        if (!glfwInitialized_) {
            if (!glfwInit()) {
                return false;
            }

            glfwInitialized_ = true;
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    #if defined(__APPLE__)
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    #endif

        window_ = glfwCreateWindow(
            static_cast<int>(spec_.width),
            static_cast<int>(spec_.height),
            spec_.title.c_str(),
            nullptr,
            nullptr
        );

        if (window_ == nullptr) {
            return false;
        }

        glfwMakeContextCurrent(window_);
        glfwSwapInterval(spec_.vsync ? 1 : 0);
        glfwMakeContextCurrent(nullptr);

        isActivated_ = true;
        return true;
    }

    void Shutdown() {
        if (window_ != nullptr) {
            glfwDestroyWindow(window_);
            window_ = nullptr;
        }

        isActivated_ = false;
    }

    bool ShouldClose() const {
        if (window_ == nullptr) {
            return true;
        }

        return glfwWindowShouldClose(window_);
    }

    void PollEvents() {
        glfwPollEvents();
    }

    std::optional<Render::OpenglBackendContext> GetRenderContextAsOpengl() {
        if (!isActivated_ || window_ == nullptr) {
            return std::nullopt;
        }

        GLFWwindow* capturedWindow = window_;

        Render::OpenglBackendContext context;

        context.thread_TakeRenderContext = [capturedWindow]() {
            glfwMakeContextCurrent(capturedWindow);
            if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
                LOG_ERROR("OpenglBackend", "failed to load glad");
                return;
            }

            auto version = glGetString(GL_VERSION);
            if (!version) {
                LOG_ERROR("OpenglBackend", "no current OpenGL context");
                return;
            }

            LOG_INFO("OpenglBackend", reinterpret_cast<const char*>(version));
        };

        context.thread_DetachRenderContext = []() {
            glfwMakeContextCurrent(nullptr);
        };

        context.thread_Swap = [capturedWindow]() {
            glfwSwapBuffers(capturedWindow);
        };

        return context;
    }

private:
    void MoveFrom(Window&& other) noexcept {
        spec_ = std::move(other.spec_);
        window_ = other.window_;
        isActivated_ = other.isActivated_;

        other.window_ = nullptr;
        other.isActivated_ = false;
    }

private:
    WindowSpec spec_;
    GLFWwindow* window_ = nullptr;
    bool isActivated_ = false;

private:
    inline static bool glfwInitialized_ = false;
};

}