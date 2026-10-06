#pragma once

#include <functional>

namespace Render{

    enum class BackendType{
        Opengl,
        Vulkan
    };



    struct OpenglBackendContext{
        std::function<void()> thread_TakeRenderContext = nullptr;
        std::function<void()> thread_DetachRenderContext = nullptr;
        std::function<void()> thread_Swap = nullptr;
    };

    // Startup data only; resources and frame commands are backend independent.
    struct VulkanBackendContext {
        void* window = nullptr; // GLFWwindow, borrowed until RHIDevice stops.
        bool vsync = true;
        bool validation = false;
        bool requireHardwareRayTracing = false;
        bool hardwareRayTracing = true; // Disable for software/hardware comparisons.
    };
    // Supplied by render_effects when built with the Vulkan SDK. Returns false
    // on builds without Vulkan; existing header-only OpenGL clients need no SDK.
    bool RegisterVulkanBackend();

}
