#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "pipeline_resource_builder.h"
#include "ue_lumen_hit_scene_color_shaders.h"
#include "ue_lumen_closest_hzb_shaders.h"
#include "Render/Public/Compute/gpu_compute.h"

namespace Render::PipelineDetail {
struct alignas(16) UeLumenClosestHzbConstants {
    glm::ivec4 extent{0},flags{0};
};
static_assert(sizeof(UeLumenClosestHzbConstants)==32);
// Shared verbatim with UeLumenHitSceneColorGLSL at UBO11. Matrices map
// current world-space native hits to the same owner's previous world space.
struct alignas(16) UeLumenHitSceneColorConstants {
    glm::mat4 previousViewProjection{1},previousView{1};
    std::array<glm::mat4,64> previousFromCurrent{};
    glm::ivec4 flags{0}; // history valid, certified owner count, frame % 8, width
    glm::vec4 camera{0}; // current camera world xyz, height
};
static_assert(offsetof(UeLumenHitSceneColorConstants,previousFromCurrent)==128);
static_assert(offsetof(UeLumenHitSceneColorConstants,flags)==4224);
static_assert(offsetof(UeLumenHitSceneColorConstants,camera)==4240);
static_assert(sizeof(UeLumenHitSceneColorConstants)==4256);

// Recording writes only the opposite history image. The enclosing renderer
// publishes pending at its existing successful frame-completion boundary.
template<class Target> struct UeLumenHitSceneColorState {
    std::shared_ptr<const void> programsOwner,targetsOwner,hzbOwner;
    std::shared_ptr<Compute::Device> computeOwner;
    Compute::PipelineHandle hzbProgram;
    RenderResourceHandle<UniformBufferSpec> hzbConstantsBuffer;
    std::vector<Target> levels; // level 1..L; full level 0 stays in current positions
    Target packed; // x=0, y=sum preceding level heights; no mip padding
    unsigned hzbWidth=0,hzbHeight=0;
    bool hzbRecorded=false;
    RenderResourceHandle<PipelineSpec> snapshotProgram;
    RenderResourceHandle<UniformBufferSpec> constantsBuffer;
    std::array<Target,2> sceneColor,depth,mrt;
    struct FrameState {
        bool valid=false;
        unsigned image=0,width=0,height=0;
        glm::mat4 viewProjection{1},view{1};
        std::uint64_t lightingKey=0;
    } completed,pending;
    UeLumenHitSceneColorConstants recordedConstants;
    std::uint64_t boundLightingKey=0;
    bool bound=false,recorded=false;
};
} // namespace Render::PipelineDetail
