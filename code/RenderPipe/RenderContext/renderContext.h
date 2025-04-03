#pragma once

#include "code/camera/camera.h"

namespace Render{
    struct RenderContext {
        const Camera* camera = nullptr;
        // 可继续扩展：灯光、帧缓存、时间、输入、主光方向等
    };
}