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
#include "MaterialLab/Public/probe_placement_scene.h"
#include "MaterialLab/Public/sealed_gi_room_scene.h"
#include "Render/Public/Pipeline/probe_sdf_scene.h"
#include "Render/Public/Pipeline/probe_sdf_lod.h"

struct GLFWwindow;
namespace DebugUI { class DebugUIModule; }

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
    bool screenProbeStudy = false, screenProbeStudyHud = true;
    bool sdfProbeStudy=false;
    bool sdfModelStudy=false; // imported Utah-teapot banquet and distance-driven raster/MDF tiers
    bool sdfClipmapStudy=false; // camera-following multilevel Global distance field
    bool surfaceCacheStudy=false; // camera-independent cards and unfiltered software-ray GI
    bool sealedGiRoomStudy=false; // closed colored room with a recessed rectangle emitter and porcelain props
    bool sdfModelRasterLod=true;
    ProbePlacementSceneSettings screenProbeStudyState;
    std::uint32_t placementUniformCount = 0, placementAdaptiveCount = 0, placementMarkedCount = 0;
    std::uint32_t traceHitCount=0,traceActiveCount=0;
    std::uint32_t sdfMeshHitCount=0,sdfGlobalHitCount=0,sdfUnreadableCount=0;
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

struct ScreenProbePlacementReadback {
    glm::uvec2 uniformGridSize{0};
    std::uint32_t candidateCount=0, sourceWidth=0, sourceHeight=0, frameIndex=0;
    // Position.xyz / status.w and normal.xyz / linear view depth.w.
    std::vector<glm::vec4> uniform, uniformNormals, marked, markedNormals, spawned, spawnedNormals;
};

struct ScreenProbeTraceReadback {
    glm::uvec2 atlasSize{0},sourceSize{0},uniformGridSize{0};
    std::uint32_t rayResolution=0,candidateCount=0,probeSlots=0;
    // Four texels per ray, probe-major then ray Y/X. Inactive radiance/status is zero.
    std::vector<glm::vec4> radianceStatus,hitWorldDistance,hitPixelStepsMip,rayDirectionDepthFetches;
    std::vector<glm::vec4> sourceHdr,sourceViewPositions,sourceWorldNormals;
    std::vector<glm::uvec2> hzbSizes;
    std::vector<std::vector<glm::vec4>> hzb;
};
struct ProbeSdfQualityStatus {
    Render::ProbeSdfLodStatistics cache;
    std::array<std::uint32_t,3> activeInstances{};
    std::uint32_t sceneBuilds=0,cachedSceneSwitches=0;
    bool pending=false;
    bool importedModels=false;
    std::array<std::uint64_t,3> modelTriangles{};
    std::uint64_t activeRasterTriangles=0,fullRasterTriangles=0;
    double tierSwitchMilliseconds=0;
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
    // Optional tooling, attached after Startup. The window must outlive this
    // module. Panels use BeginFrame/EndFrame; Render records the prepared UI
    // through the same RHI encoder, after scene post-processing and before End.
    bool EnableDebugUI(GLFWwindow* window, Render::BackendType backend);
    DebugUI::DebugUIModule* GetDebugUI() noexcept;
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
    ScreenProbePlacementReadback ReadbackScreenProbePlacement(); // diagnostic only; actual active slots, not atlas capacity
    ScreenProbeTraceReadback ReadbackScreenProbeTrace(bool includeSources=true,bool screenOnly=false);
    ScreenProbeTraceReadback ReadbackScreenProbeSources(); // receiver positions/normals only; no ray atlas or HZB download
    std::shared_ptr<const Render::ProbeSdfScene> PreparedProbeSdfScene() const;
    ProbeSdfQualityStatus ProbeSdfQualityStatistics() const;
    std::vector<glm::vec4> ReadbackProbeGlobalSdf();
    std::vector<glm::vec4> ReadbackProbeSdfRanges(); // conservative cell-brick min/max hierarchy
    std::vector<glm::vec4> ReadbackProbeSdfVolume(); // 3D raw view RGB / ray hit distance in alpha
    std::shared_ptr<const Render::SurfaceCacheScene> PreparedSurfaceCache() const;
    std::vector<glm::vec4> ReadbackSurfaceCacheAtlas(); // outgoing direct diffuse radiance / validity
    std::vector<glm::vec4> ReadbackSurfaceCacheRawGi(); // raw outgoing indirect diffuse radiance, no history/filter
    std::vector<glm::vec4> ReadbackSurfaceCacheProbeIrradiance(); // probe E/pi, no receiver albedo or history
    std::vector<glm::vec4> ReadbackSurfaceCacheSpatialGi(); // spatially filtered indirect radiance, before history
    std::vector<glm::vec4> ReadbackSurfaceCacheProbeCoverage(); // requires collectProbeCoverage or view 6; green base, orange adaptive, magenta unsupported
    std::vector<glm::vec4> ReadbackSurfaceCacheSpatialRays(); // packed RGBA32F filtered ray atlas; four texels per ray
    std::vector<glm::vec4> ReadbackSurfaceCacheSpatialNeighbors(); // index+1 / geometric weight, eight entries per probe
    std::vector<glm::vec4> ReadbackSurfaceCacheTemporalGi(); // accumulated indirect radiance / current coverage
    std::vector<glm::vec4> ReadbackSurfaceCacheTemporalHistory(); // accumulated indirect radiance / effective frame count
    // Diagnostic receiver-only edit: the incident card radiance stays fixed,
    // allowing the gather's application of the final pixel albedo to be tested.
    bool SetSurfaceCacheReceiverAlbedoForTest(std::uint32_t objectIndex,glm::vec3 albedo);
    std::vector<glm::vec4> ReadbackSurfaceCacheCoverage();
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
