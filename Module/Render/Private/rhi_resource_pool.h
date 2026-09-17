#pragma once

#include "Render/Private/rhi_resource_table.h"
#include "Render/Public/RHIResourceType/rhi_resource_type.h"

namespace Render{

    class RHIResourcePool{
        public:
            ResourceTable<VertexBufferSpec> vertexBufferTable;
            ResourceTable<UniformBufferSpec> uniformBufferTable;
            ResourceTable<ShaderSourceSpec> shaderSourceTable;
            ResourceTable<ShaderProgramSpec> shaderProgramTable;
            ResourceTable<PipelineSpec> PipelineTable;
            ResourceTable<RHITextureSpec> TextureTable;
    };
}
