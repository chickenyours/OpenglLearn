#pragma once

#include <memory>
#include <string>
#include "Render/Public/Material/pbr_material.h"
#include "Render/Public/Pipeline/post_process.h"
#include "Render/Public/Pipeline/render_pipeline.h"
#include "Render/Public/Pipeline/scene_effects.h"

namespace Render {
class RHIDevice;

struct PipelineCamera {
    glm::mat4 view{1}, projection{1};
    glm::vec3 position{0};
    float nearPlane = 0.1f, farPlane = 100.0f;
};
struct ShadowSettings {
    bool enabled = true;
    std::uint32_t atlasResolution = 2048;
    float distance = 45.0f, splitLambda = 0.9f;
    float depthBias = 0.0006f, normalBias = 0.015f, cascadeBlend = 0.1f;
    glm::vec3 sunDirection{-0.6f, -1.0f, -0.4f};
    glm::vec3 sunColor{1.0f, 0.94f, 0.84f};
    float sunIntensity = 2.0f;
};
struct ReflectionSettings {
    bool enabled = true;
    glm::vec4 plane{0, 1, 0, 3.25f}; // keep dot(plane,worldPosition)>=0
    float strength = 0.75f;
};
struct MaterialPipelineSettings {
    ShadowSettings shadows;
    ReflectionSettings reflection;
    PostProcessSettings post;
    Material::PbrPassConstants lighting;
    float time = 0;
};
struct PipelineStatistics {
    std::uint32_t shadowPasses = 0, reflectionPasses = 0, depthPasses = 0;
    std::uint32_t scenePasses = 0, postPasses = 0;
};
// Borrowed diagnostic handles, valid until Resize/Shutdown. Never delete them.
struct PipelineDebugTextures {
    RenderResourceHandle<RHITextureSpec> sceneColor, sceneDepth, shadowAtlas;
    RenderResourceHandle<RHITextureSpec> reflectionColor, ambientOcclusion, bloom;
};

// The caller owns RHIDevice and Begin/End/Submit. Initialize/Resize/Shutdown and
// Record run on the application thread. Release the pipeline before the device.
// Frame pins retain pipeline resources/material snapshots across resize and
// queued submission. The caller must separately pin externally owned meshes and
// legacy pipeline/texture resources until their submitted frame has retired.
class MaterialRenderPipeline {
public:
    explicit MaterialRenderPipeline(RHIDevice& device);
    ~MaterialRenderPipeline();
    MaterialRenderPipeline(const MaterialRenderPipeline&) = delete;
    MaterialRenderPipeline& operator=(const MaterialRenderPipeline&) = delete;
    bool Initialize();
    bool Resize(std::uint32_t width, std::uint32_t height, std::uint32_t shadowAtlasResolution = 2048);
    bool Record(RHIFrameEncoder& encoder, const RenderFrame& scene,
                const PipelineCamera& camera, const MaterialPipelineSettings& settings);
    void Shutdown();
    const std::string& LastError() const noexcept;
    PipelineStatistics Statistics() const;
    PipelineDebugTextures DebugTextures() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace Render
