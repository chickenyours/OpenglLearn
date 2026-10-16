#pragma once

#include <memory>
#include <string>
#include "Render/Public/Pipeline/material_render_pipeline.h"

namespace Render {

// A dedicated cel renderer over the same imported Material snapshots/vertex
// layout as the PBR renderer. Colors below are linear, except outline/background.
struct ToonPipelineSettings {
    glm::vec4 backgroundColor{1, 1, 1, 1};
    glm::vec3 lightDirection{0.45f, 0.8f, 0.6f}; // toward the light
    glm::vec3 lightColor{1};
    glm::vec3 shadowTint{0.62f, 0.60f, 0.74f};
    float exposure = 1;
    float bandThreshold = 0.08f, bandSoftness = 0.025f, normalWrap = 0.22f;
    float bandStrength = 0.65f, highlightStrength = 0.025f, highlightThreshold = 0.90f;
    bool outlinesEnabled = true;
    glm::vec3 outlineColor{0.10f, 0.085f, 0.15f}; // sRGB
    float outlineWidth = 1.1f; // screen pixels, independent of camera distance
    bool shadowsEnabled = true;
    float selfShadowStrength = 0.35f;
    float shadowDepthBias = 0.00015f, shadowNormalBias = 0.002f;
    glm::vec3 sceneCenter{0};
    float shadowRadius = 4.5f, shadowDistance = 12;
    bool groundEnabled = true, groundShadows = true;
    float groundHeight = -1.5f, groundExtent = 30, groundShadowStrength = 0.42f;
};

bool ValidateToonPipelineSettings(const ToonPipelineSettings& settings, std::string* error = nullptr);

struct ToonPipelineStatistics : PipelineStatistics {
    std::uint32_t outlinePasses = 0, groundPasses = 0;
};
struct ToonPipelineDebugTextures {
    RenderResourceHandle<RHITextureSpec> sceneColor, sceneDepth, shadowMap;
};

// Application-thread API. Uses RHI commands exclusively, no temporal history or
// PBR lighting passes. Owns MSAA/resolve, alpha-aware shadow, cel, hull outline,
// ground receiver and display passes. The device must outlive all recorded frames.
class ToonRenderPipeline {
public:
    explicit ToonRenderPipeline(RHIDevice& device);
    ~ToonRenderPipeline();
    ToonRenderPipeline(const ToonRenderPipeline&) = delete;
    ToonRenderPipeline& operator=(const ToonRenderPipeline&) = delete;
    bool Initialize();
    bool Resize(std::uint32_t width, std::uint32_t height,
        std::uint32_t shadowResolution = 2048, std::uint32_t msaaSamples = 4);
    bool Record(RHIFrameEncoder& encoder, const RenderFrame& scene,
        const PipelineCamera& camera, const ToonPipelineSettings& settings = {});
    std::uint64_t RecordedFrameToken() const noexcept;
    bool CompleteFrame(std::uint64_t token);
    bool DiscardFrame(std::uint64_t token);
    void Shutdown();
    const std::string& LastError() const noexcept;
    ToonPipelineStatistics Statistics() const;
    ToonPipelineDebugTextures DebugTextures() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace Render
