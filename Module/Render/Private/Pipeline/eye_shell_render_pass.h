#pragma once
#include <memory>
#include <span>
#include "Render/Public/Pipeline/eye_shell.h"
#include "Render/Public/Material/pbr_material.h"
#include "Render/Public/Pipeline/render_world.h"
#include "Render/Public/RHICommand/FrameCommand/rhi_frame_encoder.h"

namespace Render::PipelineDetail {
struct EyeShellPassTarget {
    RenderResourceHandle<RenderTargetSpec> framebuffer;
    RenderResourceHandle<RHITextureSpec> color,depth;
    std::uint32_t width=0,height=0,samples=1;
};
struct EyeShellPassInputs {
    // Resolved source images. Color may alias target.color: the pass first
    // snapshots it into its own attachment before modifying the destination.
    RenderResourceHandle<RHITextureSpec> color,depth;
    glm::mat4 view{1},projection{1};
    glm::vec3 cameraPosition{0};float nearPlane=.1f;
    Material::PbrPassConstants lighting;
    glm::vec3 sunDirection{0,-1,0},sunColor{1};float sunIntensity=0;
};
struct EyeShellPassResult {std::uint32_t draws=0,passes=0;};
bool IsEyeShellMaterial(const Material::MaterialSnapshot& material);
class EyeShellRenderPass {
public:
    explicit EyeShellRenderPass(RHIDevice& device);
    ~EyeShellRenderPass();
    EyeShellPassResult Record(RHIFrameEncoder& encoder,std::span<const RenderItem> items,
        const EyeShellPassTarget& target,const EyeShellPassInputs& inputs,const EyeShellSettings& settings);
    void Shutdown();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace Render::PipelineDetail
