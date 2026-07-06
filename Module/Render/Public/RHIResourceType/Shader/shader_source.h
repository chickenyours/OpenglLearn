#pragma once

#include <cstdint>

namespace Render{

    enum class ShaderSourceType{
        Vertex,
        Geometry,
        Fragment,
        Compute
    };

    struct CreateShaderSourceDesc{
        ShaderSourceType type;
        const char* codeSource;
        size_t size;
    };

    struct ShaderSourceSpec{
        ShaderSourceType type;
        uint32_t rhi_id;
    };
}