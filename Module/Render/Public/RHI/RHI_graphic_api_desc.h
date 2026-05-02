#pragma once

#include <functional>
#include <memory>
#include <string>

namespace Render{
    enum class RHIDeviceBackendType{
        OpenGL,
        Vulkan,
        D3D12,
        UnInit
    };

    struct OpenGLDesc {
        using Callback = std::function<bool()>;
        using VoidCallback = std::function<void()>;

        Callback makeCurrent;      // 在当前线程绑定 OpenGL context
        VoidCallback doneCurrent;   // 从当前线程解绑，可选
        VoidCallback swapBuffers;   // 交换缓冲，可选

        std::string debugName;

        bool valid() const {
            return static_cast<bool>(makeCurrent);
        }
    };

}