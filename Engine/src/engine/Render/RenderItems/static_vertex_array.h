#pragma once

#include <vector>
#include <glad/glad.h>
#include "engine/Render/render_item.h"

#include "engine/Resource/Material/material.h"

namespace Render{
    class StaticVertexArray: public RenderItem{
        std::vector<std::pair<GLuint,Resource::Material*>> object;
    };
}
