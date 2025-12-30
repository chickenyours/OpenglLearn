#pragma once

namespace Render{
    class Camera;
    struct PassConfig {
        unsigned int targetBufferWidth;
        unsigned int targetBufferHeight;
        Camera* camera;
    };
}