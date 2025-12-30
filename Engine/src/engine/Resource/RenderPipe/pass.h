#pragma once

#include <glad/glad.h>

#include "engine/Resource/RenderPipe/UniformBindings.h"
#include "engine/Resource/RenderPipe/render_contex.h"
#include "engine/ToolAndAlgorithm/Opengl/debug.h"

namespace Render{
    class Pass{
        virtual bool Init(const PassContex& cfg) = 0;
        virtual void SetConfig(const PassContex& cfg) = 0;
        virtual void Update() = 0;
        // void AddItem(...) // input
        // output
        virtual void ClearCache() = 0;
    };
}
