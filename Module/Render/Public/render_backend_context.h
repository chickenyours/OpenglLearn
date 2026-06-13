#pragma once

#include <functional>

namespace Render{

    enum class BackendType{
        Opengl
    };

    

    struct OpenglBackendContext{
        std::function<void()> thread_TakeRenderContext = nullptr;
        std::function<void()> thread_DetachRenderContext = nullptr;
        std::function<void()> thread_Swap = nullptr;
    };

}