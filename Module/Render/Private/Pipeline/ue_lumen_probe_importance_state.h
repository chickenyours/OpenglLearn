#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <glm/glm.hpp>
#include "pipeline_resource_builder.h"
#include "Render/Public/Compute/gpu_compute.h"
#include "ue_lumen_probe_importance_shaders.h"

namespace Render::PipelineDetail {
// The first ivec4 is also the complete tracing/scatter consumer ABI at UBO15.
struct UeLumenProbeImportanceConstants {
    glm::ivec4 traceInfo{16,1,0,0}; // max octahedron, enabled, ViewState frame % 8
    glm::mat4 historyViewProjection{1};
    glm::ivec4 historyExtent{0}; // previous viewport xy, uniform grid xy
    glm::ivec4 historyAtlas{0}; // spacing, flat radiance width/height, unused
    glm::vec4 historyJitter{0}; // previous source tile jitter xy
    glm::ivec4 historyFlags{0}; // history available, actual object-motion capture
};
static_assert(sizeof(UeLumenProbeImportanceConstants)==144);

template<class Target> struct UeLumenProbeImportanceState {
    std::shared_ptr<Compute::Device> computeOwner;
    std::array<Compute::PipelineHandle,3> programs;
    Compute::BufferHandle brdfSh;
    std::shared_ptr<const void> targetsOwner;
    RenderResourceHandle<UniformBufferSpec> constantsBuffer;
    Target lightingPdf,rayInfo;
    unsigned columns=0,rows=0;
    UeLumenProbeImportanceConstants recordedConstants;
    struct History {
        bool valid=false;
        glm::mat4 viewProjection{1};
        glm::ivec4 extent{0},atlas{0};
        glm::vec4 jitter{0};
        RenderResourceHandle<RHITextureSpec> uniform,radiance;
        std::shared_ptr<const void> placementOwner,radianceOwner;
    } completed,pending;
    bool recorded=false;
};
} // namespace Render::PipelineDetail
