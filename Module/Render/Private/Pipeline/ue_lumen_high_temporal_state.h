#pragma once
#include <array>
#include <memory>
#include <glm/glm.hpp>
#include "pipeline_resource_builder.h"
#include "ue_lumen_high_temporal_shaders.h"
#include "ue_lumen_card_world_attributes_shaders.h"

namespace Render::PipelineDetail {
struct alignas(16) UeLumenHighTemporalConstants {
    glm::mat4 previousViewProjection{1},previousView{1};
    glm::ivec4 flags{0}; // history valid, certified object mapping, official normal rejection
    glm::vec4 parameters{.01f,10.f,10.f,.9f};
};
static_assert(sizeof(UeLumenHighTemporalConstants)==160);
template<class Target> struct UeLumenHighTemporalState {
    std::shared_ptr<const void> programsOwner,targetsOwner;
    std::shared_ptr<const void> worldAttributesProgramsOwner,worldAttributesOwner;
    RenderResourceHandle<PipelineSpec> temporalProgram,countProgram,directProgram;
    RenderResourceHandle<PipelineSpec> worldAttributesProgram,worldDirectProgram;
    Target worldAttributes;
    bool worldAttributesActive=false;
    RenderResourceHandle<UniformBufferSpec> temporalBuffer,identityMotionBuffer,directBuffer;
    // The individual targets retain single-color framebuffers for existing
    // readback/debug aliases. mrt[] writes exactly four attachments per draw.
    // aux aliases metadata for external diagnostic binding compatibility; it
    // owns no separate texture. metadata.w carries the exact 24-bit payload.
    std::array<Target,2> history,metadata,positions,normals,aux,mrt;
};
} // namespace Render::PipelineDetail
