#pragma once

#include <glad/glad.h>

namespace Render{
    struct TextureBuffer{
        GLuint buffer;
    };
    
    struct DepthBuffer{
        GLuint depthBuffer;
    };

    struct BufferFrameObject{
        GLuint FBO;
    };
}

