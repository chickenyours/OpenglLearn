#pragma once

#include <memory>
#include <optional>
#include <string>
#include "Render/Public/Material/pbr_material.h"
#include "Render/Public/Pipeline/post_process.h"
#include "Render/Public/Pipeline/render_pipeline.h"
#include "Render/Public/Pipeline/scene_effects.h"
#include "Render/Public/Pipeline/temporal_effects.h"
#include "Render/Public/Pipeline/area_lights.h"
#include "Render/Public/Pipeline/indirect_lighting.h"
#include "Render/Public/Pipeline/sky_light.h"
#include "Render/Public/Pipeline/strand_lighting.h"
#include "Render/Public/Pipeline/strand_direct_lighting.h"
#include "Render/Public/Pipeline/source_display.h"
#include "Render/Public/Pipeline/diffuse_probe_volume.h"
#include "Render/Public/Pipeline/realtime_gi.h"
#include "Render/Public/Pipeline/lumen_gi.h"
#include "Render/Public/Pipeline/spot_light.h"
#include "Render/Public/Pipeline/screen_probe_placement.h"
#include "Render/Public/Pipeline/surface_cache_gi.h"
#include "Render/Public/Pipeline/skin_diffusion.h"
#include "Render/Public/Pipeline/eye_shell.h"
#include "Render/Public/Compute/gpu_compute.h"

namespace Render {
class RHIDevice;

struct PipelineCamera {
    glm::mat4 view{1}, projection{1};
    glm::vec3 position{0};
    float nearPlane = 0.1f, farPlane = 100.0f;
};
enum class ShadowDebugView : std::uint32_t { None, Visibility, Cascades };
enum class DirectionalShadowFilter : std::uint32_t { Tent, SourcePoisson8 };
// HOST casting policy; DeepOpacity stores eight continuous depth prefixes.
// The original game's hair shadow PSO is still unobserved.
enum class StrandShadowCoverage : std::uint32_t { Ordered, DeepOpacity };
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
    DirectionalShadowFilter filter = DirectionalShadowFilter::Tent;
    StrandShadowCoverage strandCoverage = StrandShadowCoverage::Ordered;
    // HOST effective extinction for hair projected onto surfaces. Zero keeps
    // opaque-only CSM; one preserves the unscaled DOM. No DS/hair-receive gain.
    float strandOpacityScale = 1.0f;
    // First-cascade normalized UV radius, not an angular light size or depth
    // variance. SourcePoisson8 scales it with each cascade's projection.
    float sourcePoissonRadius = 0.003f;
    // HOST surface-receiver calibration only, not a recovered character
    // shader parameter. Zero inherits sourcePoissonRadius. Explicit-phase
    // strand consumers keep sourcePoissonRadius even when this is wider.
    float sourceSurfaceRadius = 0.0f;
    // Explicit host phase for surfaces without an original strand END index.
    // Native segment shading instead restores the source per-END phase.
    float sourcePoissonRotationRadians = 0.0f;
    ShadowDebugView debugView = ShadowDebugView::None;
};
struct ReflectionReceiverBounds {
    glm::vec3 minimum{0}, maximum{0}; // world-space conservative AABB
};
struct ReflectionSettings {
    bool enabled = true;
    glm::vec4 plane{0, 1, 0, 3.25f}; // keep dot(plane,worldPosition)>=0
    float strength = 0.75f;
    float resolutionScale = 1.0f; // relative to viewport; 0.25..2, default full resolution
    // Supply bounds enclosing every surface using this planar reflection.
    // Omitted bounds retain the existing always-render behavior.
    std::optional<ReflectionReceiverBounds> receiverBounds;
};
struct MaterialPipelineSettings {
    ShadowSettings shadows;
    ReflectionSettings reflection;
    PostProcessSettings post;
    SourceDisplaySettings sourceDisplay;
    Material::PbrPassConstants lighting;
    float time = 0;
    TemporalEffectsSettings temporal;
    float deltaSeconds = 1.0f / 60.0f;
    // Monotonic render timeline for strand shutter interpolation. Kept separate
    // from animation time, which may pause. CompleteFrame alone advances history.
    std::optional<double> strandTimeSeconds;
    bool cameraCut = false;
    AreaLightsSettings areaLights;
    SpotLightSettings spotLight;
    IndirectLightingSettings indirect;
    SkyLightSettings sky;
    StrandIndirectLightingSettings strandIndirect;
    StrandSourceDirectSettings strandSourceDirect;
    // Sort nearest hair depth groups before the weighted tail. The current
    // adapter requires a single-sample target; multisample retains WBOIT.
    bool strandFrontLayerTransparency = false;
    std::uint32_t strandFrontDepthGroups = 3; // supported: 2, 3, 7
    // Pair HDR color and log-transmittance in one MRT draw per depth group.
    bool strandPairedLayerTransparency = true;
    // Software Wu coverage, persistent local packed4/8 -> object -> global4.
    // Deterministic pixel-owner GPU scheduling; single sample only.
    bool strandNativeOit = false;
    std::uint32_t strandNativeOitLayers = 4;
    StrandDebugMode strandDebugMode = StrandDebugMode::Beauty;
    bool strandTemporalDepth = true;
    bool strandMotionVectors = false;
    float strandDepthWriteThreshold = .95f;
    DiffuseProbeSettings probes;
    RealtimeGiSettings realtimeGi;
    LumenGiSettings lumenGi;
    ScreenProbePlacementSettings screenProbePlacement;
    SurfaceCacheGiSettings surfaceCacheGi;
    SkinDiffusionSettings skinDiffusion;
    EyeShellSettings eyeShell;
    // Presentation/HUD extent; {0,0} uses the internal Resize extent.
    // Both nonzero components must be 1..8192. Scene, depth, GI and temporal
    // histories retain the internal extent when only this value changes.
    glm::uvec2 outputSize{0};
};
struct PipelineStatistics {
    double hostPrepareMilliseconds=0,hostAreaShadowRecordMilliseconds=0;
    std::uint32_t shadowPasses = 0, reflectionPasses = 0, depthPasses = 0;
    std::uint32_t strandOpacityShadowPasses = 0, strandOpacityShadowResolution = 0;
    std::uint32_t scenePasses = 0, postPasses = 0;
    std::uint32_t resolvePasses = 0, temporalPasses = 0, motionBlurPasses = 0;
    bool historyUsed = false, cameraHistoryUsed = false;
    std::uint32_t reflectionWidth = 0, reflectionHeight = 0, msaaSamples = 1;
    std::uint32_t transparentPasses = 0, indirectPasses = 0;
    std::uint32_t skyPasses = 0;
    std::uint32_t validDiffuseProbes = 0;
    std::uint32_t realtimeGiPasses=0,realtimeGiUpdatedProbes=0,realtimeGiRays=0;
    std::uint32_t lumenGiPasses=0,lumenSurfaceTexels=0,lumenUpdatedSurfels=0,lumenScreenProbes=0;
    bool lumenHistoryUsed=false;
    std::uint32_t screenProbePlacementPasses=0,screenProbeUniformCapacity=0,screenProbeCandidateCapacity=0;
    bool screenProbeComputePlacement=false,screenProbePlacementComparisonCollected=false,screenProbePlacementComparisonReused=false;
    // Logical atlas invocation capacities; compute dispatch padding is excluded.
    std::uint64_t screenProbeMarkInvocationCapacity=0,screenProbeLegacyMarkInvocationCapacity=0;
    std::uint32_t screenProbePlacementComparisonPasses=0;
    bool screenProbePlacementFrozen=false;
    std::uint32_t screenProbeTracePasses=0,screenProbeHzbLevels=0;
    bool screenProbeSingleInvocationSupported=false,screenProbeSingleInvocation=false,probeSdfSingleInvocation=false;
    bool screenProbeSinglePassOutputs=false,probeSdfSinglePassOutputs=false;
    bool probeSdfCompactRays=false;
    bool probeSdfFastAtlasAddressing=false;
    bool probeSdfReuseTraceSamples=false;
    bool probeSdfHardwareRayTracing=false;
    bool probeSdfNativeUploadsPruned=false; // actual native path omitted unused MDF/GDF mirrors this frame
    bool probeSdfNativeCandidateReused=false;
    bool probeSdfDirectSoftwareRefinement=false; // actual direct triangle profile; hardware tracing is false
    std::uint32_t probeSdfCompactCapacity=0;
    std::uint64_t probeSdfUploadedTexels=0,probeSdfUploadRegions=0,probeSdfUpdatedVoxels=0,probeSdfUpdatedBricks=0;
    std::uint64_t probeSdfMeshUploadedTexels=0;
    bool probeSdfMeshCacheHit=false;
    std::uint32_t surfaceCachePasses=0,surfaceCacheTexels=0,surfaceCacheUpdatedTexels=0;
    // Actual UE lighting attachment writes, including padding on full refresh.
    std::uint32_t surfaceCacheLightingWrittenTexels=0;
    std::uint64_t surfaceCacheAttributeUploadedTexels=0,surfaceCacheMetadataUploadedTexels=0,surfaceCacheGeometryUploadedTexels=0,surfaceCacheUploadRegions=0;
    std::uint64_t surfaceCacheRays=0;
    std::uint32_t surfaceCacheProbePasses=0,surfaceCacheProbeSlots=0;
    bool surfaceCacheProbeCoverageCollected=false;
    bool surfaceCacheDirectionalCollected=false;
    bool surfaceCacheProbeOriginsCollected=false;
    std::uint64_t surfaceCacheProbeRayBudget=0;
    bool surfaceCacheLightingUpdated=false;
    std::uint32_t surfaceCacheTemporalPasses=0;
    bool surfaceCacheHistoryUsed=false;
    bool surfaceCacheGatherCacheReused=false;
    bool surfaceCacheRawGatherOmitted=false;
    bool surfaceCacheSplitTransport=false;
    bool surfaceCacheCompactReceivers=false;
    bool surfaceCacheMrtReceiverCapture=false; // actual path, false on whole-frame material fallback
    bool surfaceCacheStaticReceiverMotionCapture=false;
    std::uint32_t surfaceCacheReceiverMotionDraws=0,surfaceCacheReceiverMotionStaticOwners=0;
    bool surfaceCacheNativeScreenProbeTransport=false,surfaceCacheNativeProbeQueryDiagnostics=false;
    bool surfaceCacheUeLumenHigh=false; // actual fixed UE High gather / world RC / AO path
    bool surfaceCacheNativeAdaptiveFreshSampling=false;
    bool surfaceCacheNativeForceRasterCore=false;
    bool surfaceCacheNativeReceiverFilterSupportHalo=false; // actual compact sparse halo dispatches, false for full receiver fallback
    bool surfaceCacheNativeReconstructionFused=false,surfaceCacheNativeReconstructionSharedStencil=false;
    bool surfaceCacheNativeBaseTimedWithContactMask=false,surfaceCacheNativeReconstructionCompared=false;
    std::uint32_t surfaceCacheNativeReconstructionDraws=0,surfaceCacheNativeReconstructionReplayDraws=0;
    bool surfaceCacheNativeReconstructionComparisonShared=false;
    bool surfaceCacheNativeNeighborQueryDiagnostics=false; // neighbor .w counts actual native query initializations
    std::uint32_t surfaceCacheNativeReceiverDiagnosticPages=0;
    bool surfaceCacheWorldTransportReused=false,surfaceCacheTransportDiagnostics=false;
    bool surfaceCacheLightingQueriesCollected=false;
    std::uint32_t surfaceCacheWorldTransportPhase=0;
    std::uint32_t surfaceCacheSpatialPasses=0;
    bool surfaceCacheRadianceOnlyFiltering=false; // actual compact spatial filtering for this frame
    // Full filter attachment extent times passes, including inactive padding;
    // this is invocation capacity, not physical GPU transactions or active rays.
    std::uint64_t surfaceCacheSpatialFilterTexels=0,surfaceCacheSpatialRayTargetBytes=0;
    std::uint32_t surfaceCacheContactPasses=0;
    bool surfaceCacheContactCollected=false;
    bool surfaceCacheHardwareContact=false,surfaceCacheHardwareVisibility=false;
    bool surfaceCacheHardwareOrigins=false;
    bool surfaceCacheReusedOriginCertification=false;
    bool surfaceCacheSoftwareReceiverOrigins=false;
    bool surfaceCacheClosestTraversalReused=false,surfaceCacheFastGeometryAddressing=false;
    bool surfaceCacheSplitReceiverOrigins=false,surfaceCacheReceiverLookupReused=false;
    bool surfaceCacheReceiverFastLightingAddressing=false;
    bool surfaceCacheFreshSamplingCollected=false,surfaceCacheReceiverSamplingCollected=false;
    bool surfaceCacheReceiverTraceDiagnosticsCollected=false;
    std::uint32_t areaLightShadowPasses=0;
    std::uint64_t areaLightShadowBytes=0;
    bool areaLightShadowsUpdated=false,areaLightShadowCacheUsed=false;
    bool areaLightShadowCasterPreparationUsed=false;
    std::uint32_t areaLightShadowCasterCount=0,areaLightShadowCasterDraws=0,areaLightShadowCulledCasters=0;
    std::uint64_t areaLightShadowCasterParameterBytes=0,areaLightShadowCasterObjectBytes=0,areaLightShadowMaterialPins=0;
    std::uint32_t areaLightShadowAtlasAllocations=0,areaLightShadowAtlasReuses=0,areaLightShadowAtlasPoolEntries=0;
    std::uint64_t areaLightShadowAtlasPoolBytes=0; // retained pool only, not total renderer memory
    std::uint32_t strandPasses=0;
    std::uint32_t strandShadingPasses=0,strandShadingComputePasses=0,strandShadingRasterPasses=0;
    std::uint64_t strandShadingEvaluations=0,strandShadingOutputPoints=0;
    std::uint64_t strandCurveOriginalSegments=0,strandCurveVisibleSegments=0,strandCurvePieces=0;
    std::uint32_t strandCurveUploads=0;
    std::uint64_t strandCurveShadowPieces=0;
    std::uint32_t strandCurveShadowUploads=0;
    std::uint32_t strandFrontDepthGroups=0,strandTemporalPasses=0,strandShadowDraws=0;
    std::uint32_t strandTransparencyRasterPasses=0;
    bool strandPairedTransparencyActive=false;
    bool strandNativeOitActive=false;
    std::uint32_t strandNativeOitComputePasses=0,strandNativeOitLayers=0;
    std::uint64_t strandNativeOitBytes=0;
    std::uint32_t strandShutterSamples=0,strandShutterPasses=0,strandShutterRasterPasses=0,strandShutterVolumePasses=0;
    std::uint32_t strandMotionPasses=0,strandHairTemporalPasses=0;
    bool strandMotionActive=false,strandHairHistoryUsed=false;
    bool strandTemporalDepthActive=false,strandTemporalMultisampleFallback=false;
    bool strandFrontLayerTransparencyActive=false;
    bool strandTransparencyMultisampleFallback=false;
    std::uint64_t strandSegments=0;
    std::uint32_t skinDiffusionPasses=0,skinDiffusionDraws=0;
    std::uint32_t eyeShellPasses=0,eyeShellDraws=0;
    std::uint32_t strandVolumePasses=0,strandVolumeResolution=0,strandBlockerPasses=0;
    std::uint64_t strandNativeDsInputSegments=0,strandNativeDsInjectedSegments=0;
    // Actual submitted constants for the last native DS item; counts above
    // are summed across all native items in the frame. Zero when inactive.
    float strandNativeDsInjectionFraction=0,strandNativeDsRasterWidthScale=0,strandNativeDsAverageWidth=0;
    bool strandVolumeUpdated=false;
    float strandVolumeVoxelSize=0; // effective edge in transformed world units
    float strandVolumeBiasMetres=0; // effective near-field trace exclusion
};
// Borrowed diagnostic handles. Resize/Shutdown or a scene/resource replacement
// on the next Record can retire them; cache indices can flip each frame.
// Never delete or retain them as external rendering resources.
struct PipelineDebugTextures {
    RenderResourceHandle<RHITextureSpec> sceneColor, sceneDepth, shadowAtlas;
    RenderResourceHandle<RHITextureSpec> reflectionColor, ambientOcclusion, bloom;
    std::uint32_t reflectionWidth = 0, reflectionHeight = 0, msaaSamples = 1;
    RenderResourceHandle<RHITextureSpec> indirectIrradiance, diffuseRadiance, surfaceNormals, indirectDepth;
    RenderResourceHandle<RHITextureSpec> opaqueColor, opaqueDepth;
    RenderResourceHandle<RHITextureSpec> realtimeGiCache;
    RenderResourceHandle<RHITextureSpec> lumenSurfaceCache,lumenGather,lumenReflections;
    RenderResourceHandle<RHITextureSpec> lumenDirectLighting;
    RenderResourceHandle<RHITextureSpec> lumenReflectionSource,lumenReflectionSourceDepth;
    // Two texels per slot; see screen_probe_placement.h. Borrowed like all other
    // handles here, and published only after successful frame completion.
    RenderResourceHandle<RHITextureSpec> screenProbeUniform,screenProbeMarked,screenProbeSpawned;
    RenderResourceHandle<RHITextureSpec> screenProbeComparisonMarked,screenProbeComparisonSpawned;
    std::uint32_t screenProbeUniformWidth=0,screenProbeUniformHeight=0;
    std::uint32_t screenProbeCandidateWidth=0,screenProbeCandidateHeight=0;
    std::uint32_t screenProbeGridWidth=0,screenProbeGridHeight=0,screenProbeCandidateCount=0;
    std::uint32_t screenProbeSourceWidth=0,screenProbeSourceHeight=0,screenProbeFrameIndex=0;
    // CPU diagnostic evidence committed with the published placement image.
    // Uniform constants precede the reconstruction-only refinement flag edit;
    // placement constants retain the final ray/reconstruction UBO contents.
    ScreenProbePlacementConstants screenProbePlacementConstants{},screenProbePlacementUniformConstants{};
    std::uint32_t screenProbePlacementImage=0,screenProbeBaseSamplingSeed=0,screenProbeRaySamplingSeed=0;
    std::uint64_t screenProbeGiTemporalSequence=0;
    bool screenProbeGiTemporalSequenceAvailable=false;
    RenderResourceHandle<RHITextureSpec> screenProbeRays,screenProbeTraceSource,screenProbeTracePositions;
    // Borrowed, retired-frame diagnostic buffers. Never submit/read back in
    // ordinary rendering; the first sixteen control words can be inspected
    // directly by the backend after that frame's existing completion fence.
    Compute::BufferHandle probeSdfCompactControl,probeSdfCompactQueue;
    std::uint32_t probeSdfCompactCapacity=0;
    RenderResourceHandle<RHITextureSpec> screenProbeScreenRays,probeGlobalSdf,probeMeshSdf;
    // Native upload pruning deliberately leaves these raw MDF/GDF mirrors
    // stale/unallocated. They are unpublished until software/debug catches up.
    bool probeSdfUploadsPruned=false;
    RenderResourceHandle<RHITextureSpec> probeSdfVolumeView;
    RenderResourceHandle<RHITextureSpec> probeSdfRanges;
    std::array<RenderResourceHandle<RHITextureSpec>,ScreenProbeTraceMaxHzbLevels> screenProbeHzb{};
    std::array<glm::uvec2,ScreenProbeTraceMaxHzbLevels> screenProbeHzbSizes{};
    std::uint32_t screenProbeRayWidth=0,screenProbeRayHeight=0,screenProbeRayResolution=0,screenProbeHzbLevels=0;
    // Card radiance atlas and unfiltered reflected indirect radiance. Raw GI
    // alpha is the fraction of readable rays (an empty ray is valid black).
    RenderResourceHandle<RHITextureSpec> surfaceCacheAtlas,surfaceCacheRawGi,surfaceCacheCoverage;
    std::array<RenderResourceHandle<RHITextureSpec>,2> surfaceCacheTransportDiagnostics;
    RenderResourceHandle<RHITextureSpec> surfaceCacheTransport;
    bool surfaceCacheUeLumenHigh=false;
    Compute::BufferHandle ueRadianceCacheControl,ueRadianceCacheQueue,ueRadianceCacheAudit;
    bool ueRadianceCacheDiagnostics=false;
    float ueRadianceCacheMaxDistance=0;
    RenderResourceHandle<RHITextureSpec> ueHighHalfGi,ueHighMoving,ueHighRawAO,ueHighAOHistory;
    Compute::BufferHandle surfaceCacheReceiverCompactControl; // retired completed frame only; unavailable outside compact mode
    Compute::BufferHandle surfaceCacheReceiverCompactQueue; // existing pixel-ID queue; explicit untimed diagnostics only
    std::uint32_t surfaceCacheReceiverCompactCapacity=0;
    RenderResourceHandle<RHITextureSpec> surfaceCacheTransportMask;
    RenderResourceHandle<RHITextureSpec> surfaceCacheReceiverClassification; // explicit untimed audit; X queue selector, Y strict 3cm core, Z newly selected halo
    RenderResourceHandle<RHITextureSpec> surfaceCacheTransportReconstruction; // sparse current receiver field, before receiver filtering/history
    RenderResourceHandle<RHITextureSpec> surfaceCacheNativeBaseGi; // current native screen-probe receiver radiance before contact/history
    std::array<RenderResourceHandle<RHITextureSpec>,4> surfaceCacheNativeReceiverTracePages{}; // optional actual batches, 8 ray slots/pixel/page
    RenderResourceHandle<RHITextureSpec> surfaceCacheNativeNeighbors; // original neighbor table; optional native query count in W
    std::array<RenderResourceHandle<RHITextureSpec>,4> surfaceCacheNativeReconstructionComparison{};
    RenderResourceHandle<RHITextureSpec> surfaceCacheLightingQueries;
    RenderResourceHandle<RHITextureSpec> surfaceCacheProbeIrradiance;
    RenderResourceHandle<RHITextureSpec> surfaceCacheProbeDirectional; // four horizontal coefficient texels per probe
    RenderResourceHandle<RHITextureSpec> surfaceCacheProbeOrigins; // exact shared tracing origins / sweep status
    // Temporal GI keeps the raw readability alpha; history alpha is the
    // effective accumulated frame count. Both publish after frame completion.
    RenderResourceHandle<RHITextureSpec> surfaceCacheTemporalGi,surfaceCacheTemporalHistory;
    RenderResourceHandle<RHITextureSpec> surfaceCacheSpatialGi,surfaceCacheSpatialRays,surfaceCacheProbeCoverage,surfaceCacheSpatialNeighbors;
    // With the compact path, SpatialRays stores one radiance/status texel per
    // ray and SpatialMetadata retains the original four-texel packed records.
    // Diagnostic readbacks interleave them only when explicitly requested.
    RenderResourceHandle<RHITextureSpec> surfaceCacheSpatialMetadata;
    bool surfaceCacheSpatialRadianceOnly=false;
    RenderResourceHandle<RHITextureSpec> surfaceCacheContactGi,surfaceCacheContactMask,surfaceCacheContactProbeRisk;
    RenderResourceHandle<RHITextureSpec> surfaceCacheContactUnfilteredGi;
    RenderResourceHandle<RHITextureSpec> surfaceCacheFreshSampling,surfaceCacheReceiverSampling;
    std::array<RenderResourceHandle<RHITextureSpec>,3> surfaceCacheReceiverTracePacked{};
    std::uint32_t surfaceCacheProbeWidth=0,surfaceCacheProbeHeight=0;
    std::uint32_t surfaceCacheWidth=0,surfaceCacheHeight=0,surfaceCacheGiWidth=0,surfaceCacheGiHeight=0;
    // Borrowed internal-resolution display image after tone mapping, grading,
    // filtering and FXAA. Overlays drawn directly to the window are excluded.
    RenderResourceHandle<RHITextureSpec> displayColor;
    RenderResourceHandle<RHITextureSpec> skinDiffuse,skinDiffused,skinProfile,skinAlbedo;
    // Diagnostic views of the last strand volume; read only after submitted
    // work completes. Reading these is opt-in and stalls the GPU.
    RenderResourceHandle<RHITextureSpec> strandDensity,strandPaths,strandPathsExtra,strandPathsAmbient;
    RenderResourceHandle<RHITextureSpec> strandNearestDepth,strandCoverage,strandCompositeDepth;
    RenderResourceHandle<RHITextureSpec> strandMotion,strandResolvedColor,strandTemporalColor;
    // Last completed separate-hair frame, scene-linear backgrounds before
    // and after opaque TAA. No diagnostic copy or extra pass is recorded.
    RenderResourceHandle<RHITextureSpec> strandBackgroundColor,strandTemporalBackgroundColor;
    std::uint64_t completedTemporalSequence=0;
    glm::vec2 completedJitterUV{0};
    glm::mat4 completedJitteredViewProjection{1},completedUnjitteredViewProjection{1};
    RenderResourceHandle<RHITextureSpec> strandOpaqueBlockers,strandDirectVisibility,strandExtraVisibility;
    std::uint32_t strandVolumeWidth=0,strandVolumeHeight=0;
    RenderResourceHandle<RHITextureSpec> areaLightShadowAtlas;
    std::uint32_t areaLightShadowWidth=0,areaLightShadowHeight=0;
    RenderResourceHandle<RHITextureSpec> surfaceCacheObjectPreviousPositions;
    std::array<float,64> surfaceCacheObjectMotionStates{}; // 0 invalid, 1 static/cold, 2 moving
    // Untimed readback of the existing UE base SceneColor, including primary specular.
    RenderResourceHandle<RHITextureSpec> surfaceCacheDirectSource;
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
    // Mutable opaque geometry/texture uploads require the default true. Known
    // hair-only appearance changes can preserve the opaque depth cache.
    void ResetHistory(bool invalidateOpaqueDepth=true);
    // Notify in-place opaque mesh/texture uploads without discarding temporal
    // images. Handle/model/material changes are already part of the depth key.
    void InvalidateOpaqueDepth() noexcept;
    void Shutdown();
    const std::string& LastError() const noexcept;
    PipelineStatistics Statistics() const;
    PipelineDebugTextures DebugTextures() const;
    // Explicit diagnostic readback after completed submission; stalls GPU.
    // Last strand object: references, overflow/fallback tiles, max tile count,
    // reserved references. Ordinary Record/Statistics never perform readback.
    std::array<std::uint32_t,4> CaptureStrandNativeOitCounters() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace Render
