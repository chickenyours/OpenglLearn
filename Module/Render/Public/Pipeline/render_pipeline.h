#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>

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
            // Each layer has a scene-sorted group followed by a painter-ordered
            // group. Never reorder sprites A/B/A just to reduce state changes.
            const bool orderedA = a.PreservesSubmissionOrder();
            const bool orderedB = b.PreservesSubmissionOrder();
            if(orderedA != orderedB) return !orderedA;
            if(orderedA) return false;
            if(a.layer == RenderLayer::Transparent && a.viewDepth != b.viewDepth) {
                return a.viewDepth > b.viewDepth;
            }
            if(a.sortKey != b.sortKey) return a.sortKey < b.sortKey;
            const auto pipelineA = a.EffectivePipeline();
            const auto pipelineB = b.EffectivePipeline();
            if(pipelineA.id != pipelineB.id) return pipelineA.id < pipelineB.id;
            if(pipelineA.version != pipelineB.version) return pipelineA.version < pipelineB.version;
            if(a.texture.id != b.texture.id) return a.texture.id < b.texture.id;
            return a.mesh.id < b.mesh.id;
        });

        RenderResourceHandle<PipelineSpec> pipeline;
        std::map<std::uint32_t, RenderResourceHandle<RHITextureSpec>> textures;
        RenderResourceHandle<VertexBufferSpec> mesh;
        const Material::MaterialSnapshot* boundMaterial = nullptr;
        for(const RenderItem& item : frame.items) {
            const auto itemPipeline = item.EffectivePipeline();
            if(itemPipeline != pipeline) {
                if(!encoder.BindPipeline(itemPipeline)) return false;
                pipeline = itemPipeline;
            }
            const auto bindTexture = [&](std::uint32_t slot, RenderResourceHandle<RHITextureSpec> texture) {
                const auto previous = textures.find(slot);
                if(previous != textures.end() && previous->second == texture) return true;
                if(!encoder.BindTexture(texture, slot)) return false;
                textures[slot] = texture;
                return true;
            };
            if(item.material) {
                const auto& material = *item.material;
                if(!encoder.KeepAlive(item.material)) return false;
                for(const auto& binding : material.Resources().textures)
                    if(!bindTexture(binding.slot, binding.texture)) return false;
                if(boundMaterial != &material) {
                    const auto& bytes = material.Parameters().Bytes();
                    const auto& resources = material.Resources();
                    // Full values in every recorded frame: replacing an older
                    // frame can never discard an update needed by a newer one.
                    if(!bytes.empty()) {
                        if(!encoder.UpdateUniformBufferBytes(resources.parameterBuffer,
                            bytes.data(), static_cast<std::uint32_t>(bytes.size()))) return false;
                        if(!encoder.BindUniformBuffer(resources.parameterBuffer,
                            resources.parameterBinding)) return false;
                    }
                    boundMaterial = &material;
                }
            } else {
                // Explicitly unbind absent legacy textures; never sample the
                // previous material's slot zero by accident.
                if(!bindTexture(0, item.texture)) return false;
                boundMaterial = nullptr;
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
    bool recordSucceeded = true;
};

} // namespace Render
