#pragma once

#include <cstdint>
#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <stdexcept>
#include <vector>

#include "module_base.h"
#include "Render/Public/Material/material.h"
#include "Render/Public/render_backend_context.h"
#include "Render/Public/Pipeline/material_render_pipeline.h"
#include "MaterialLab/Public/showcase_scene.h"
#include "MaterialLab/Public/gi_room_scene.h"

namespace MaterialLab {

struct FrameSettings {
    // Internal rendering dimensions; effects.outputSize optionally preserves
    // a larger presentation extent after the final reconstruction pass.
    std::uint32_t width = 1280, height = 800;
    float lightTime = 0.0f;
    float exposure = 1.0f;
    float cameraYaw = 0.0f, cameraPitch = 0.08f, cameraDistance = 18.0f;
    // -1 draws the gallery. An index isolates a material for editing/testing.
    int isolatedMaterial = -1;
    bool present = true;
    Render::MaterialPipelineSettings effects;
    bool shadowStudy = false; // contact, grazing slope, thin caster and curved receiver
    bool lightingStudy = false; // transmission, flowing water, area light and one diffuse bounce
    bool closedLightingStudy = false; // ceiling/right/front walls; orbit inside to inspect local sky occlusion
    bool waitForProbeBake = false; // deterministic offline capture; interactive updates run on a CPU worker
    bool showcase = false; // connected indoor gallery / outdoor courtyard
    bool showcaseHud = true;
    ShowcaseSettings showcaseState;
    bool giRoom = false; // sealed room illuminated only by the traced world radiance cache
    bool giRoomHud = true;
    GiRoomSettings giRoomState;
    float displayFps = 0; // optional HUD sample; update infrequently outside the renderer
    std::optional<Render::PipelineCamera> camera; // free-camera override; legacy orbit remains available
    glm::uvec2 OutputExtent() const {
        return effects.outputSize.x&&effects.outputSize.y?effects.outputSize:glm::uvec2(width,height);
    }
    float OutputAspect() const {const auto size=OutputExtent();return float(size.x)/size.y;}
};

inline void SetShowcaseResolution(FrameSettings& frame,std::uint32_t outputWidth,std::uint32_t outputHeight,float scale=1) {
    if(outputWidth==0||outputHeight==0||outputWidth>8192||outputHeight>8192||
       !std::isfinite(scale)||scale<.5f||scale>1)
        throw std::invalid_argument("Showcase resolution requires 1..8192 output dimensions and .5..1 render scale");
    frame.width=std::max(1u,unsigned(std::lround(outputWidth*scale)));
    frame.height=std::max(1u,unsigned(std::lround(outputHeight*scale)));
    frame.effects.outputSize=frame.width==outputWidth&&frame.height==outputHeight?glm::uvec2(0):glm::uvec2(outputWidth,outputHeight);
}

struct MaterialInfo {
    std::string name;
    float metallic = 0.0f, roughness = 0.5f;
    bool normalMap = false, metallicMap = false, roughnessMap = false, aoMap = false;
};

struct FramePixels {
    std::uint32_t width = 0, height = 0;
    // RGBA8, first row at the bottom, as returned by OpenGL diagnostics.
    std::vector<std::uint8_t> rgba;
};

struct FrameTiming {
    double gpuMilliseconds = 0;
    double elapsedMilliseconds = 0;
};

// Standalone material test module. The caller owns the window/context and must
// keep it alive until Shutdown. All public calls run on the application's thread.
class MaterialLabModule final : public IModule {
public:
    explicit MaterialLabModule(Render::OpenglBackendContext context,
                               std::string legacyTextureDirectory = {});
    explicit MaterialLabModule(Render::VulkanBackendContext context,
                               std::string legacyTextureDirectory = {});
    ~MaterialLabModule() override;
    MaterialLabModule(const MaterialLabModule&) = delete;
    MaterialLabModule& operator=(const MaterialLabModule&) = delete;
    const char* GetName() const noexcept override { return "MaterialLab"; }
    bool Startup() override;
    void Shutdown() override;
    bool IsStarted() const noexcept override;
    const std::string& LastError() const noexcept { return error_; }

    std::size_t MaterialCount() const;
    MaterialInfo InspectMaterial(std::size_t index) const;
    bool SetMaterialParameter(std::size_t index, std::string_view name,
                              const Render::Material::ParameterValue& value);
    bool ResetMaterial(std::size_t index);
    // Reliable, completed submission: deliberately bounded to one frame in flight
    // for this small laboratory. No rendering work bypasses the RHI encoder.
    bool Render(const FrameSettings& settings);
    // Test-only backend diagnostic, called after Render(present=false).
    FramePixels Readback();
    Render::PipelineStatistics PipelineStatistics() const;
    std::shared_ptr<const Render::DiffuseProbeVolume> PreparedDiffuseProbes() const;
    std::shared_ptr<const Render::RealtimeGiScene> PreparedRealtimeGiScene() const;
    std::vector<glm::vec4> ReadbackRealtimeGiCache(); // test-only RGBA32F rows
    std::shared_ptr<const Render::LumenGiScene> PreparedLumenGiScene() const;
    bool ShowcaseGiUpdating() const;
    std::vector<glm::vec4> ReadbackLumenSurfaceCache(bool directLighting=false); // test-only outgoing diffuse radiance
    std::vector<glm::vec4> ReadbackLumenGather(); // test-only incident irradiance at the internal rendering extent
    // Test diagnostics: minimum CSM depth, AO minimum and maximum HDR channel.
    glm::vec3 InspectPipelineBuffers();
    // Diagnostic batch; settings.present selects presentation. No pixel readback.
    // Vulkan sums per-frame GPU timestamps; the GL batch interval also includes
    // idle gaps between submissions. Wall time includes submission/completion.
    FrameTiming MeasureFrames(const FrameSettings& settings, std::uint32_t count = 24, float timeStep = 0);

private:
    struct Impl;
    Render::OpenglBackendContext context_;
    std::optional<Render::VulkanBackendContext> vulkanContext_;
    std::string legacyTextureDirectory_;
    std::unique_ptr<Impl> impl_;
    std::string error_;
};

} // namespace MaterialLab
