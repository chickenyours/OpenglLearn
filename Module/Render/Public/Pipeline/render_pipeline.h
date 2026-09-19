#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include <glm/glm.hpp>

#include "Render/Public/Pipeline/render_world.h"
#include "Render/Public/RHICommand/FrameCommand/rhi_frame_encoder.h"

namespace Render {

struct RenderView {
    glm::mat4 viewProjection{1.0f};
    glm::vec4 cameraPosition{0.0f, 0.0f, 0.0f, 1.0f};
    RenderResourceHandle<UniformBufferSpec> viewUniform;
    RenderResourceHandle<UniformBufferSpec> objectUniform;
};

struct ViewConstants {
    glm::mat4 viewProjection{1.0f};
    glm::vec4 cameraPosition{0.0f};
};

struct ObjectConstants {
    glm::mat4 model{1.0f};
};

class ForwardRenderPipeline {
public:
    bool Record(RHIFrameEncoder& encoder, RenderFrame& frame, const RenderView& view) const {
        if(!encoder.IsRecording()) return false;

        if(view.viewUniform.IsValid()) {
            if(!encoder.UpdateUniformBuffer(view.viewUniform,
                ViewConstants{view.viewProjection, view.cameraPosition})) return false;
            if(!encoder.BindUniformBuffer(view.viewUniform, 0)) return false;
        }

        std::stable_sort(frame.items.begin(), frame.items.end(), [](const RenderItem& a, const RenderItem& b) {
            if(a.layer != b.layer) return a.layer < b.layer;
            if(a.layer == RenderLayer::Transparent && a.viewDepth != b.viewDepth) {
                return a.viewDepth > b.viewDepth;
            }
            if(a.sortKey != b.sortKey) return a.sortKey < b.sortKey;
            if(a.pipeline.id != b.pipeline.id) return a.pipeline.id < b.pipeline.id;
            if(a.texture.id != b.texture.id) return a.texture.id < b.texture.id;
            return a.mesh.id < b.mesh.id;
        });

        RenderResourceHandle<PipelineSpec> pipeline;
        RenderResourceHandle<RHITextureSpec> texture;
        RenderResourceHandle<VertexBufferSpec> mesh;
        for(const RenderItem& item : frame.items) {
            if(item.pipeline != pipeline) {
                if(!encoder.BindPipeline(item.pipeline)) return false;
                pipeline = item.pipeline;
            }
            if(item.texture.IsValid() && item.texture != texture) {
                if(!encoder.BindTexture(item.texture, 0)) return false;
                texture = item.texture;
            }
            if(item.mesh != mesh) {
                if(!encoder.BindMesh(item.mesh)) return false;
                mesh = item.mesh;
            }
            if(view.objectUniform.IsValid()) {
                if(!encoder.UpdateUniformBuffer(view.objectUniform, ObjectConstants{item.model})) return false;
                if(!encoder.BindUniformBuffer(view.objectUniform, 1)) return false;
            }
            if(!encoder.DrawIndexed(item.draw)) return false;
        }
        return true;
    }
};

struct RenderFrameService {
    RHIFrameEncoder* encoder = nullptr;
    RenderView view{};
};

} // namespace Render
