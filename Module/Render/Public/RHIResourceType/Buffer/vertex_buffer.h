#pragma once

#include <vector>
#include "DebugTool/ConsoleHelp/color_log.h"

namespace Render{
    enum class VertexFieldType{
        Float,
        Vec2,
        Vec3,
        Vec4,
        Mat2,
        Mat3,
        Mat4,
        Int,
        Byte
    };

    
    struct VertexLayout{
        std::vector<VertexFieldType> typeSlots;
    };

    struct VertexBufferSpec{
        VertexLayout layout;
        uint32_t num;
        uint32_t rhi_id;
    };
}