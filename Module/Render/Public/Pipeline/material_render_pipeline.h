#pragma once

#include <memory>
#include <string>
#include "Render/Public/Material/pbr_material.h"
#include "Render/Public/Pipeline/post_process.h"
#include "Render/Public/Pipeline/render_pipeline.h"
#include "Render/Public/Pipeline/scene_effects.h"
#include "Render/Public/Pipeline/temporal_effects.h"

namespace Render {
class RHIDevice;

struct PipelineCamera {
    glm::mat4 view{1}, projection{1};
    glm::vec3 position{0};
    float nearPlane = 0.1f, farPlane = 100.0f;
};
enum class ShadowDebugView : std::uint32_t { None, Visibility, Cascades };
struct ShadowSettings {
    bool enabled = true;
    std::uint32_t atlasResolution = 4096; // four 2048x2048 tiles
    float distance = 45.0f, splitLambda = 0.65f;
    // Optional legacy extra offsets; most bias now scales with cascade texels.
    float depthBias = 0.000002f, normalBias = 0.0f, cascadeBlend = 0.1f;
    glm::vec3 sunDirection{-0.6f, -1.0f, -0.4f};
    glm::vec3 sunColor{1.0f, 0.94f, 0.84f};
    float sunIntensity = 2.0f;
    float depthBiasTexels = 0.05f, normalBiasTexels = 0.20f;
    // RPDB cap scales by max(1,tan(trueTriangleLightAngle)); zero disables RPDB.
    float distanceFade = 0.10f, receiverPlaneClampTexels = 4.0f;
    float casterPadding = 30.0f;
    ShadowDebugView debugView = ShadowDebugView::None;
};
struct ReflectionSettings {
    bool enabled = true;
    glm::vec4 plane{0, 1, 0, 3.25f}; // keep dot(plane,worldPosition)>=0
    float strength = 0.75f;
    float resolutionScale = 1.0f; // relative to viewport; 0.25..2, default full resolution
};
struct MaterialPipelineSettings {
    ShadowSettings shadows;
    ReflectionSettings reflection;
    PostProcessSettings post;
    Material::PbrPassConstants lighting;
    float time = 0;
    TemporalEffectsSettings temporal;
    float deltaSeconds = 1.0f / 60.0f;
    bool cameraCut = false;
};
struct PipelineStatistics {
    std::uint32_t shadowPasses = 0, reflectionPasses = 0, depthPasses = 0;
    std::uint32_t scenePasses = 0, postPasses = 0;
    std::uint32_t resolvePasses = 0, temporalPasses = 0, motionBlurPasses = 0;
    bool historyUsed = false, cameraHistoryUsed = false;
    std::uint32_t reflectionWidth = 0, reflectionHeight = 0, msaaSamples = 1;
};
// Borrowed diagnostic handles, valid until Resize/Shutdown. Never delete them.
struct PipelineDebugTextures {
    RenderResourceHandle<RHITextureSpec> sceneColor, sceneDepth, shadowAtlas;
    RenderResourceHandle<RHITextureSpec> reflectionColor, ambientOcclusion, bloom;
    std::uint32_t reflectionWidth = 0, reflectionHeight = 0, msaaSamples = 1;
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
    bool Resize(std::uint32_t width, std::uint32_t height, std::uint32_t shadowAtlasResolution = 4096,
                std::uint32_t msaaSamples = 1, float reflectionScale = 1.0f);
    bool Record(RHIFrameEncoder& encoder, const RenderFrame& scene,
                const PipelineCamera& camera, const MaterialPipelineSettings& settings);
    // One recorded frame at a time. Call CompleteFrame on the application thread
    // after reliable submission's completion callback, or DiscardFrame if never
    // submitted. A merely recorded/dropped frame must never become TAA history.
    std::uint64_t RecordedFrameToken() const noexcept;
    bool CompleteFrame(std::uint64_t token);
    bool DiscardFrame(std::uint64_t token);
    void ResetHistory(); // between frames; camera cuts can also use settings.cameraCut
    void Shutdown();
    const std::string& LastError() const noexcept;
    PipelineStatistics Statistics() const;
    PipelineDebugTextures DebugTextures() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace Render
