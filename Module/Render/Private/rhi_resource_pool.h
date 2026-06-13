#pragma once

#include "Render/Private/rhi_resource_table.h"
#include "Render/Public/RHIResourceType/Buffer/vertex_buffer.h"

namespace Render{
  
    class RHIResourcePool{
        public:
            ResourceTable<VertexBufferSpec> vertexBufferTable;
    };
}