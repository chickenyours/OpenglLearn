#pragma once

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "Render/Public/Material/material_runtime.h"

#include "Render/Public/RHICommand/FrameCommand/rhi_frame_command.h"
#include "Render/Public/RHIResourceType/Buffer/uniform_buffer.h"
#include "Render/Public/RHIResourceType/Buffer/vertex_buffer.h"
#include "Render/Public/RHIResourceType/Pipeline/pipeline.h"
#include "Render/Public/RHIResourceType/Texture/texture.h"
#include "Render/Public/rhi_resource_handle.h"
#include "Render/Public/Pipeline/strand_rendering.h"

namespace Render {

enum class RenderLayer : std::uint8_t { Opaque, Cutout, Transparent, Overlay };
enum class RenderOrder : std::uint8_t { Scene, Submission };

// Supplied by geometry extraction only when these world bounds contain every
// vertex emitted by the shadow shader, including deformation and instancing.
// Leaving them unset conservatively retains the original shadow draws.
struct ShadowCasterBounds { glm::vec3 minimum{0}, maximum{0}; };

struct RenderItem {
    RenderResourceHandle<VertexBufferSpec> mesh;
    RenderResourceHandle<PipelineSpec> pipeline;
    RenderResourceHandle<RHITextureSpec> texture;
    glm::mat4 model{1.0f};
    RHICommand::DrawIndexed draw{};
    RenderLayer layer = RenderLayer::Opaque;
    std::uint64_t sortKey = 0;
    float viewDepth = 0.0f;
    // Optional upper-layer material. Legacy pipeline/texture packets still work.
    std::shared_ptr<const Material::MaterialSnapshot> material;
    RenderOrder order = RenderOrder::Scene;
    bool castsShadows = true;
    bool visibleInReflections = true;
    // Scene extraction tags fixtures paired with point/rectangle lights.
    // Their visible emission remains; analytic lighting owns transported energy.
    bool analyticEmission = false;
    std::int32_t surfaceCacheObject=-1; // certified source-instance index; -1 retains geometric lookup
    std::optional<ShadowCasterBounds> shadowCasterBounds;

    RenderLayer EffectiveLayer() const {
        if (layer == RenderLayer::Overlay) return layer;
        return material && material->GetDomain() == Material::Domain::Translucent
            ? RenderLayer::Transparent : layer;
    }

    RenderResourceHandle<PipelineSpec> EffectivePipeline() const {
        return material ? material->Pipeline() : pipeline;
    }
    bool PreservesSubmissionOrder() const {
        return order == RenderOrder::Submission ||
            (material && material->GetDomain() == Material::Domain::Sprite);
    }
};

struct RenderFrame {
    std::uint64_t frameIndex = 0;
    std::vector<RenderItem> items;
    std::vector<StrandRenderItem> strands;
};

// Double-buffered extraction boundary. Gameplay/ECS writes only the building
// frame; rendering consumes an immutable snapshot after Publish().
class RenderWorld {
public:
    void BeginExtract(std::uint64_t frameIndex, std::size_t reserve = 0) {
        building_.frameIndex = frameIndex;
        building_.items.clear();
        building_.strands.clear();
        if(reserve > building_.items.capacity()) building_.items.reserve(reserve);
    }

    bool Add(RenderItem item) {
        if(!item.mesh.IsValid() || !item.EffectivePipeline().IsValid()) return false;
        building_.items.push_back(std::move(item));
        return true;
    }

    void Publish() {
        std::lock_guard<std::mutex> lock(mutex_);
        published_ = std::move(building_);
        building_ = RenderFrame{};
        hasPublished_ = true;
    }

    bool Consume(RenderFrame& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!hasPublished_) return false;
        out = std::move(published_);
        published_ = RenderFrame{};
        hasPublished_ = false;
        return true;
    }

private:
    RenderFrame building_;
    RenderFrame published_;
    std::mutex mutex_;
    bool hasPublished_ = false;
};

} // namespace Render
