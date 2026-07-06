#pragma once

#include "Render/Private/rhi_resource_table.h"
#include "Render/Public/RHIResourceType/Buffer/vertex_buffer.h"
#include "Render/Public/RHIResourceType/Shader/shader.h"
#include "Render/Public/RHIResourceType/Shader/shader_source.h"
#include "Render/Public/RHIResourceType/Pipeline/pipeline.h"

namespace Render{
  
    class RHIResourcePool{
        public:
            ResourceTable<VertexBufferSpec> vertexBufferTable;
            ResourceTable<ShaderSourceSpec> shaderSourceTable;
            ResourceTable<ShaderProgramSpec> shaderProgramTable;
            ResourceTable<PipelineSpec> PipelineTable;
    };
}