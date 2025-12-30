#pragma once

#include <vector>
#include <glad/glad.h>
#include "code/Render/render_item.h"

#include "code/Resource/Material/material.h"

namespace Render{
    class StaticVertexArray: public RenderItem{
        std::vector<std::pair<GLuint,Resource::Material*>> object;
    };
}