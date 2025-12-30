#pragma once

namespace Render{
    class Camera;
    struct RenderPipeConfig{
        unsigned int targetBufferWidth;
        unsigned int targetBufferHeight;
        Camera* camera;
    };
}