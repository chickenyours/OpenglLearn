#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "code/ECS/Component/Camera/camera.h"

namespace Render{
    struct RenderPipeContex
    {
        glm::ivec2 outputResolution;     // 渲染目标的分辨率(所谓的"屏幕")
        ECS::Component::Camera* camera = nullptr;
    };

    struct PassContex
    {
        glm::ivec2 outputResolution;     // 渲染目标的分辨率
        ECS::Component::Camera* camera = nullptr;
    };
}