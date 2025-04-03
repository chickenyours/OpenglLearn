#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

#include "code/RenderPipe/RenderContext/renderContext.h"
#include "code/RenderPipe/RenderContext/initContext.h"

namespace Render{


    class Pass{
        public:
        Pass();
        virtual void Init(const InitRenderContext& ctx) = 0;
        virtual void Update(const RenderContext& ctx) = 0;
        virtual void Release();
        ~Pass();
    };
}