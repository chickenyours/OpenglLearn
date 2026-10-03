#pragma once

#include <algorithm>
#include <cstdint>
#include <mutex>
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

namespace Render {

enum class RenderLayer : std::uint8_t { Opaque, Cutout, Transparent, Overlay };
enum class RenderOrder : std::uint8_t { Scene, Submission };

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
};

// Double-buffered extraction boundary. Gameplay/ECS writes only the building
// frame; rendering consumes an immutable snapshot after Publish().
class RenderWorld {
public:
    void BeginExtract(std::uint64_t frameIndex, std::size_t reserve = 0) {
        building_.frameIndex = frameIndex;
        building_.items.clear();
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
