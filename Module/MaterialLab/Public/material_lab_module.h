#pragma once

#include <cstdint>
#include <array>
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

// Untimed receiver-capture fixture: reuse actual gallery textures and schema
// on the primitive scene without changing its SDF/card material declarations.
struct ReceiverCaptureMaterialOverride {
    std::uint32_t objectIndex=0,galleryMaterialIndex=16;
    std::vector<Render::Material::ParameterOverride> parameters;
    bool cutout=false,omitMrtVariant=false,unknownOwner=false;
    bool omitMotionVariant=false,disableStaticWorldCapture=false;
    std::int32_t ownerOverride=-2; // -2 keeps the ordinary extraction tag
};

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
    // Stable six-prop rigid motion; never part of the static scene/material identity.
    SealedGiRoomPropOffsets sealedGiRoomPropOffsets{};
    bool sealedGiRoomPropMotion=false; // set before first frame; keep true while paused
    // Test-only primitive scene. Rasterization, SDF instances and Surface Cache
    // all consume these same objects/materials. Both arrays must be provided.
    std::shared_ptr<const std::vector<ProbePlacementObject>> probeSceneOverride;
    std::shared_ptr<const std::vector<SealedGiRoomMaterial>> probeSceneMaterialsOverride;
    std::shared_ptr<const std::vector<ReceiverCaptureMaterialOverride>> receiverCaptureMaterialOverrides;
    std::uint32_t placementUniformCount = 0, placementAdaptiveCount = 0, placementMarkedCount = 0;
    // Last explicitly inspected GPU origin result: active, full, clipped,
    // uncertain, then negative failure reasons 1..5. No per-frame readback.
    std::array<std::uint32_t,9> probeOriginInspection{};
    std::array<std::uint64_t,18> probeRayInspection{};
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
    // Vulkan only: current-frame GPU spans, excluding CPU submission / waiting.
    // Empty samples mean the scope did not execute or the backend lacks it;
    // each scope mean uses only its executed samples, never inserted zeros.
    double contactFilterGpuMilliseconds = 0;
    std::vector<double> gpuFrameMilliseconds;
    // Directional shadows, planar reflection, main opaque, probe placement,
    // post/output. NaN means absent; all values belong to this completed frame.
    std::vector<std::array<double,7>> architectureFrameMilliseconds;
    std::vector<double> contactFilterFrameMilliseconds;
    double contactTraceGpuMilliseconds = 0;
    std::vector<double> contactTraceFrameMilliseconds;
    double contactRiskGpuMilliseconds=0,contactMaskGpuMilliseconds=0,contactOriginsGpuMilliseconds=0,contactGatherGpuMilliseconds=0;
    std::vector<double> contactRiskFrameMilliseconds,contactMaskFrameMilliseconds,contactOriginsFrameMilliseconds,contactGatherFrameMilliseconds;
    double nativeProbeNeighborsGpuMilliseconds=0,nativeProbeFilterGpuMilliseconds=0,nativeProbeBaseGpuMilliseconds=0;
    std::vector<double> nativeProbeNeighborsFrameMilliseconds,nativeProbeFilterFrameMilliseconds,nativeProbeBaseFrameMilliseconds;
    double ueProbeImportanceGpuMilliseconds=0;
    std::vector<double> ueProbeImportanceFrameMilliseconds;
    double ueRadianceCacheGpuMilliseconds=0;
    std::vector<double> ueRadianceCacheFrameMilliseconds;
    double placementUniformGpuMilliseconds=0,placementMarkGpuMilliseconds=0,placementSpawnGpuMilliseconds=0;
    std::vector<double> placementUniformFrameMilliseconds,placementMarkFrameMilliseconds,placementSpawnFrameMilliseconds;
    double probeScreenTraceGpuMilliseconds = 0;
    std::vector<double> probeScreenTraceFrameMilliseconds;
    double probeSdfTraceGpuMilliseconds = 0;
    std::vector<double> probeSdfTraceFrameMilliseconds;
    // Nested parts of the SDF total: full kernel includes certification,
    // mesh/global marching, refinement and cache lookup; pack is transport.
    double sdfKernelGpuMilliseconds=0,sdfPackGpuMilliseconds=0;
    std::vector<double> sdfKernelFrameMilliseconds,sdfPackFrameMilliseconds;
    double probeReconstructionGpuMilliseconds = 0;
    std::vector<double> probeReconstructionFrameMilliseconds;
    double probeSpatialGpuMilliseconds = 0;
    std::vector<double> probeSpatialFrameMilliseconds;
    double surfaceCacheTemporalGpuMilliseconds = 0;
    std::vector<double> surfaceCacheTemporalFrameMilliseconds;
    double surfaceCacheLightingGpuMilliseconds = 0;
    std::vector<double> surfaceCacheLightingFrameMilliseconds;
    double receiverMotionGpuMilliseconds = 0;
    std::vector<double> receiverMotionFrameMilliseconds;
    double freshSamplingGpuMilliseconds = 0;
    std::vector<double> freshSamplingFrameMilliseconds;
    double accelerationBuildGpuMilliseconds = 0;
    std::vector<double> accelerationBuildFrameMilliseconds; // nested in contact/cache; do not add twice
    double areaShadowGpuMilliseconds = 0;
    std::vector<double> areaShadowFrameMilliseconds;
    // CPU wall intervals. Submission wait includes render-thread/GPU/present work, not CPU-only execution.
    std::vector<double> scenePreparationFrameMilliseconds,frameRecordingFrameMilliseconds,submitWaitFrameMilliseconds;
    // Per completed frame: regions, bytes, deferred regions, deferred bytes,
    // dedicated submissions, dedicated fence waits, allocations, staging capacity.
    std::vector<std::array<std::uint64_t,8>> textureUploadStatistics;
    // SDF scene preparation, Surface Cache scene preparation, remaining scene
    // preparation. These are exclusive frontend wall intervals.
    std::vector<std::array<double,3>> preparationDetailFrameMilliseconds;
    // Backend host spans: begin, command encoding, end, deferred copies,
    // texture-update preparation, AS preparation, AS geometry packing,
    // AS retirement, queue submit, fence wait, swap acquire, present,
    // timestamp read, pipeline creation. Several entries are nested.
    std::vector<std::array<double,14>> backendHostFrameMilliseconds;
    // Draws, descriptor allocations, UBO snapshot copies, AS builds,
    // AS storage reuses, AS buffer allocations, AS handle creations.
    std::vector<std::array<std::uint64_t,9>> backendHostFrameStatistics;
    std::vector<double> frameQueueFrameMilliseconds,renderThreadFrameMilliseconds;
    // Residual wall spans after render-thread frame execution: diagnostic
    // arrival, diagnostic service, then application callback delivery/commit.
    std::vector<double> postRenderDiagnosticFrameMilliseconds,diagnosticServiceFrameMilliseconds,completionDeliveryFrameMilliseconds;
    std::vector<std::array<double,2>> pipelineHostFrameMilliseconds; // resource preparation, area-shadow recording
    std::vector<std::array<std::uint32_t,16>> probeSdfCompactCounters;
    // Actual Vulkan mode, content lookups/reuses/comparisons/collisions,
    // graphics + frame-compute copied bytes, ring high-water, descriptor hits/writes.
    std::vector<std::array<std::uint64_t,9>> uniformSnapshots;
    // Passes, atlas bytes, updated/cache/prepared, eligible casters, actual
    // draws, culled caster-faces, material/object upload bytes, material pins,
    // atlas allocations/reuses, retained pool bytes and pool entries.
    std::vector<std::array<std::uint64_t,15>> shadowSubmission;
};

struct AreaLightShadowDepthReadback {
    std::uint32_t width=0,height=0;
    std::vector<float> depth;
    bool updated=false,cacheUsed=false;
};
struct SurfaceReceiverCaptureReadback {
    std::uint32_t width=0,height=0;
    std::vector<glm::vec4> reflectance,normals,positions;
    std::vector<float> depth;
    bool mrt=false;
};
struct SurfaceReceiverMotionReadback {
    std::uint32_t width=0,height=0,rasterDraws=0,staticOwners=0;
    std::vector<glm::vec4> previousWorldOwner;
    std::array<float,64> states{};
    bool staticCapture=false;
};
struct NativeReconstructionComparisonReadback {
    std::uint32_t width=0,height=0;
    std::array<std::vector<glm::vec4>,4> images; // old Base/raw, MRT Base/raw; identical producer snapshot
    bool sharedStencil=false,productionFused=false;
};
struct ScreenProbePlacementReadback {
    glm::uvec2 uniformGridSize{0};
    std::uint32_t candidateCount=0, sourceWidth=0, sourceHeight=0, frameIndex=0;
    Render::ScreenProbePlacementConstants placementConstants{},placementUniformConstants{};
    std::uint32_t placementImage=0,baseSamplingSeed=0,raySamplingSeed=0;
    std::uint64_t giTemporalSequence=0;
    bool giTemporalSequenceAvailable=false;
    // Position.xyz / status.w and normal.xyz / linear view depth.w.
    std::vector<glm::vec4> uniform, uniformNormals, marked, markedNormals, spawned, spawnedNormals;
};

struct ScreenProbePlacementComparisonReadback {
    std::uint32_t width=0,height=0;
    // Complete physical RGBA32F atlases, including every inactive candidate.
    std::array<std::vector<glm::vec4>,4> images; // actual marked/spawned, original marked/spawned
    bool reused=false;
};
struct ScreenProbeTraceReadback {
    glm::uvec2 atlasSize{0},sourceSize{0},uniformGridSize{0};
    std::uint32_t rayResolution=0,candidateCount=0,probeSlots=0;
    // Four texels per ray, probe-major then ray Y/X. Inactive radiance/status is zero.
    std::vector<glm::vec4> radianceStatus,hitWorldDistance,hitPixelStepsMip,rayDirectionDepthFetches;
    std::vector<glm::vec4> sourceHdr,sourceViewPositions,sourceWorldNormals;
    // Optional physical atlas including capacity padding, for transport parity checks.
    std::vector<glm::vec4> packed;
    std::vector<glm::uvec2> hzbSizes;
    std::vector<std::vector<glm::vec4>> hzb;
};
struct ProbeSdfCompactionReadback {
    std::array<std::uint32_t,16> control{};
    std::vector<std::uint32_t> queue; // 12 original uint words per pending ray
    std::uint32_t capacity=0;
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
// Explicit untimed diagnostics of the actual completed UE world-cache update.
struct UeLumenRadianceCacheReadback {
    bool available=false,diagnostics=false;
    std::array<std::uint32_t,64> control{};
    std::vector<std::uint32_t> queue,audit;
    float maxDistance=0;
};

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
    FramePixels ReadbackDisplay(); // owned post-process output; valid after present, excludes window overlays
    AreaLightShadowDepthReadback ReadbackAreaLightShadowDepth(); // diagnostic transfer; excluded from performance runs
    SurfaceReceiverCaptureReadback ReadbackSurfaceReceiverCapture(); // untimed capture parity; includes exact owner/depth
    SurfaceReceiverMotionReadback ReadbackSurfaceReceiverMotion(); // untimed old/new motion image, states and actual draw counts
    bool ResetRenderHistory(); // application thread, after a completed frame; includes world/screen transport phase
    Render::PipelineStatistics PipelineStatistics() const;
    std::shared_ptr<const Render::DiffuseProbeVolume> PreparedDiffuseProbes() const;
    std::shared_ptr<const Render::RealtimeGiScene> PreparedRealtimeGiScene() const;
    std::vector<glm::vec4> ReadbackRealtimeGiCache(); // test-only RGBA32F rows
    std::shared_ptr<const Render::LumenGiScene> PreparedLumenGiScene() const;
    bool ShowcaseGiUpdating() const;
    std::vector<glm::vec4> ReadbackLumenSurfaceCache(bool directLighting=false); // test-only outgoing diffuse radiance
    std::vector<glm::vec4> ReadbackLumenGather(); // test-only incident irradiance at the internal rendering extent
    ScreenProbePlacementReadback ReadbackScreenProbePlacement(); // diagnostic only; actual active slots, not atlas capacity
    ScreenProbePlacementComparisonReadback ReadbackScreenProbePlacementComparison(); // untimed same-frame full-atlas comparison; empty unless explicitly collected
    ScreenProbeTraceReadback ReadbackScreenProbeTrace(bool includeSources=true,bool screenOnly=false,bool includePacked=false);
    ProbeSdfCompactionReadback ReadbackProbeSdfCompaction(bool includeQueue=true);
    ScreenProbeTraceReadback ReadbackScreenProbeSources(); // receiver positions/normals only; no ray atlas or HZB download
    std::shared_ptr<const Render::ProbeSdfScene> PreparedProbeSdfScene() const;
    ProbeSdfQualityStatus ProbeSdfQualityStatistics() const;
    std::vector<glm::vec4> ReadbackProbeGlobalSdf();
    std::vector<glm::vec4> ReadbackProbeSdfRanges(); // conservative cell-brick min/max hierarchy
    std::vector<glm::vec4> ReadbackProbeSdfVolume(); // 3D raw view RGB / ray hit distance in alpha
    std::shared_ptr<const Render::SurfaceCacheScene> PreparedSurfaceCache() const;
    // Completed production base SceneColor: cached diffuse plus primary specular/emissive; no GI.
    std::vector<glm::vec4> ReadbackSurfaceCacheDirectSource();
    std::vector<glm::vec4> ReadbackSurfaceCacheAtlas(); // outgoing direct diffuse radiance / validity
    std::vector<glm::vec4> ReadbackSurfaceCacheRawGi(); // raw outgoing indirect diffuse radiance, no history/filter
    std::vector<glm::vec4> ReadbackSurfaceCacheProbeIrradiance(); // probe E/pi, no receiver albedo or history
    std::vector<glm::vec4> ReadbackSurfaceCacheProbeDirectional(); // 4 horizontal texels/probe: E/pi, world bx/by/bz; current raw or filtered integration
    std::vector<glm::vec4> ReadbackSurfaceCacheProbeOrigins(); // exact ray origin.xyz / 0 inactive, 1 full corrected bias, 2 clipped, 3 uncertain, negative unresolved reason
    std::vector<glm::vec4> ReadbackSurfaceCacheTransportTrace(bool local); // four texels per scheduled cache/local direction
    UeLumenRadianceCacheReadback ReadbackUeLumenRadianceCache(bool includeAudit=true);
    std::optional<std::array<std::uint32_t,16>> ReadbackSurfaceCacheReceiverCompaction(); // diagnostic only; nullopt for a noncompact completed frame
    std::optional<std::vector<std::uint32_t>> ReadbackSurfaceCacheReceiverCompactionQueue(); // exactly control[0] original pixel IDs; nullopt outside compact mode
    std::vector<glm::vec4> ReadbackSurfaceCacheTransport();
    std::vector<glm::vec4> ReadbackSurfaceCacheTransportMask();
    std::vector<glm::vec4> ReadbackSurfaceCacheReceiverClassification(); // explicit untimed core/halo audit; empty without a current sparse classification
    std::vector<glm::vec4> ReadbackSurfaceCacheTransportReconstruction(); // sparse current receiver field before receiver filtering/history; diagnostic only
    std::vector<glm::vec4> ReadbackSurfaceCacheNativeBaseGi(); // native probe receiver base before contact/history; empty outside that mode
    NativeReconstructionComparisonReadback ReadbackSurfaceCacheNativeReconstructionComparison(); // untimed, three replay draws only
    std::vector<glm::vec4> ReadbackSurfaceCacheNativeReceiverTrace(unsigned page); // same-frame 8-ray batch/page, four pages only when adaptive diagnostics collected
    std::vector<glm::vec4> ReadbackSurfaceCacheNativeNeighbors(); // native neighbor table; W is actual query count only when diagnostics flag is true
    std::vector<glm::vec4> ReadbackSurfaceCacheLightingQueries();
    std::vector<glm::vec4> ReadbackSurfaceCacheSpatialGi(); // spatially filtered indirect radiance, before history
    std::vector<glm::vec4> ReadbackSurfaceCacheContactGi(); // actual receiver contact resolve, before history; raw/spatial readbacks remain unchanged
    std::vector<glm::vec4> ReadbackSurfaceCacheContactUnfilteredGi(); // actual receiver resolve before its local current-frame filter
    std::vector<glm::vec4> ReadbackSurfaceCacheContactMask(); // x retrace blend / y risky probe mass / z reconstruction mass / w valid receiver
    std::vector<glm::vec4> ReadbackSurfaceCacheFreshSampling(); // confidence / planned rays / geometrically fresh / (valid + 2*remaining exposure lifetime)
    std::vector<glm::vec4> ReadbackSurfaceCacheReceiverSampling(); // same-loop diagnostic replay: selected / actually traced rays / base rays / readable rays
    std::array<std::vector<glm::vec4>,3> ReadbackSurfaceCacheContactTraceDiagnostics(); // same-frame integer counters packed by adjacent pairs in base 4096
    std::vector<glm::vec4> ReadbackSurfaceCacheContactProbeRisk(); // x full-budget harmonic inverse near distance / y near-hit fraction / z minimum hit / w collected
    std::vector<glm::vec4> ReadbackSurfaceCacheProbeCoverage(); // requires collectProbeCoverage or view 6; green base, orange adaptive, magenta unsupported
    std::vector<glm::vec4> ReadbackSurfaceCacheSpatialRays(); // packed RGBA32F filtered ray atlas; four texels per ray
    // Linear 16-entry table per probe: index+1 / geometric weight; entries 0-7
    // are remote, 8-15 local. Entry 8.z is the local active-candidate count.
    std::vector<glm::vec4> ReadbackSurfaceCacheSpatialNeighbors();
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
    // Vulkan also enables optional scope timestamps and preserves individual
    // samples for interleaved/warmed A/B comparisons and robust statistics.
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
