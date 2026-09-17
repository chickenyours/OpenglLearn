#pragma once

#include <cstdint>

#include "Render/Public/RHIResourceType/Buffer/vertex_buffer.h"

namespace Render {

struct UniformBufferSpec {
    uint32_t byteSize = 0;
    uint32_t rhi_id = 0;
    BufferUsage usage = BufferUsage::Dynamic;
};

struct CreateUniformBufferDesc {
    uint32_t byteSize = 0;
    const void* initialData = nullptr;
    BufferUsage usage = BufferUsage::Dynamic;
};

} // namespace Render
