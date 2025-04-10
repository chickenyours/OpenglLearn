#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>


namespace Render{
    class Camera;

    struct PassRenderContext {
        const Camera* camera = nullptr;
        // 可继续扩展：灯光、帧缓存、时间、输入、主光方向等
    };
}