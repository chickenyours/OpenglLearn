#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "pipeline_resource_builder.h"
#include "ue_lumen_direct_lighting_schedule_shaders.h"

namespace Render {class SurfaceCacheScene;}
namespace Render::PipelineDetail {
inline constexpr unsigned UeLumenDirectLightingMaxRects=256;
inline constexpr unsigned UeLumenDirectLightingRectBinding=15;
struct UeLumenDirectLightingRectConstants {
    glm::ivec4 atlas{0}; // width, height, actual rectangle count, reserved
    std::array<glm::ivec4,UeLumenDirectLightingMaxRects> rects{};
};
static_assert(sizeof(UeLumenDirectLightingRectConstants)==16*(1+UeLumenDirectLightingMaxRects));

struct UeLumenDirectLightingPlan {
    bool fullRefresh=false;
    std::uint32_t cardCount=0,pageTexels=0,budgetTexels=0;
    std::uint32_t scheduledTexels=0,writtenTexels=0,maximumBucket=15;
    std::array<std::uint32_t,16> histogram{}; // texel costs, not card counts
    std::vector<std::uint32_t> selectedCards;
    std::vector<glm::ivec4> rects; // actual flattened atlas x,y,width,height
};

struct UeLumenDirectLightingScheduleState {
    std::array<std::shared_ptr<const void>,4> programOwners;
    std::array<RenderResourceHandle<PipelineSpec>,4> programs;
    std::array<std::shared_ptr<const void>,2> fullProgramOwners;
    std::array<RenderResourceHandle<PipelineSpec>,2> fullPrograms;
    std::shared_ptr<const void> constantsOwner;
    RenderResourceHandle<UniformBufferSpec> constantsBuffer;
    struct History {
        bool valid=false;
        const SurfaceCacheScene* cache=nullptr;
        std::uint64_t globalLightingKey=0,frameIndex=0;
        std::uint32_t width=0,planeTexels=0,cardCount=0,pageResolution=0;
        std::vector<std::uint64_t> lastUpdate;
    } completed,pending;
    UeLumenDirectLightingPlan plan;
    bool pendingValid=false;
};
} // namespace Render::PipelineDetail
