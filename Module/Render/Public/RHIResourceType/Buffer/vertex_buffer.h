#pragma once

#include <cstddef>
#include <cstdint>
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

    enum class BufferUsage{
        Static,
        Dynamic,
        Stream
    };

    enum class IndexType{
        UInt16,
        UInt32
    };


    struct VertexLayout{
        std::vector<VertexFieldType> typeSlots;
    };

    struct VertexBufferSpec{
        VertexLayout layout;
        uint32_t num = 0; // Backwards-compatible alias for vertexCount.
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        uint32_t vertexByteSize = 0;
        uint32_t indexByteSize = 0;
        uint32_t rhi_id = 0; // OpenGL backend: VAO.
        bool isUseElementBuffer = false;
        IndexType indexType = IndexType::UInt32;
        BufferUsage usage = BufferUsage::Static;
    };

    // The public descriptor only borrows memory. RHIDevice copies it into the
    // queued command before returning, so producer jobs may release temporary
    // vectors immediately after async_CreateMeshBuffer returns.
    struct CreateMeshBufferDesc{
        VertexLayout vertexLayout;
        uint32_t vertexCount = 0;
        const void* vertexData = nullptr;
        size_t vertexByteSize = 0;
        uint32_t indexCount = 0;
        const void* indexData = nullptr;
        size_t indexByteSize = 0;
        IndexType indexType = IndexType::UInt32;
        BufferUsage usage = BufferUsage::Static;
    };

    struct UpdateMeshBufferDesc{
        uint32_t vertexCount = 0;
        const void* vertexData = nullptr;
        size_t vertexByteSize = 0;
        uint32_t indexCount = 0;
        const void* indexData = nullptr;
        size_t indexByteSize = 0;
        IndexType indexType = IndexType::UInt32;
    };
}
