#include "Render/Public/Pipeline/material_render_pipeline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <limits>
#include <stdexcept>
#include <glm/gtc/matrix_inverse.hpp>
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Private/Pipeline/post_process_shaders.h"
#include "Render/Private/Pipeline/temporal_effects_shaders.h"
#include "Render/Private/Pipeline/hair_temporal_shaders.h"
#include "Render/Private/Pipeline/indirect_lighting_shaders.h"
#include "Render/Private/Pipeline/realtime_gi_shaders.h"
#include "Render/Private/Pipeline/lumen_gi_shaders.h"
#include "Render/Private/Pipeline/screen_probe_placement_shaders.h"
#include "Render/Private/Pipeline/screen_probe_compute_placement_shaders.h"
#include "Render/Private/Pipeline/screen_probe_trace_shaders.h"
#include "Render/Private/Pipeline/probe_sdf_shaders.h"
#include "Render/Private/Pipeline/probe_sdf_visualization_shaders.h"
#include "Render/Private/Pipeline/surface_cache_probe_shaders.h"
#include "Render/Private/Pipeline/surface_cache_probe_origin_shaders.h"
#include "Render/Private/Pipeline/surface_cache_temporal_shaders.h"
#include "Render/Private/Pipeline/surface_cache_spatial_shaders.h"
#include "Render/Private/Pipeline/surface_cache_contact_shaders.h"
#include "Render/Private/Pipeline/surface_cache_exposure_shaders.h"
#include "Render/Private/Pipeline/surface_cache_shaders.h"
#include "Render/Private/Pipeline/surface_cache_transport_shaders.h"
#include "Render/Private/Pipeline/surface_cache_transport_compute_shaders.h"
#include "Render/Private/Pipeline/surface_cache_native_probe_state.h"
#include "Render/Private/Pipeline/ue_lumen_high_short_range_ao_state.h"
#include "Render/Private/Pipeline/ue_lumen_screen_probe_gather_shaders.h"
#include "Render/Private/Pipeline/ue_lumen_high_temporal_state.h"
#include "Render/Private/Pipeline/ue_lumen_probe_importance_state.h"
#include "Render/Private/Pipeline/ue_lumen_hit_scene_color_state.h"
#include "Render/Private/Pipeline/ue_lumen_direct_lighting_schedule_state.h"
#include "Render/Public/Pipeline/surface_cache.h"
#include "Render/Public/Pipeline/probe_sdf_scene.h"
#include "Render/Public/Pipeline/probe_sdf_clipmap.h"
#include "Render/Public/Pipeline/surface_pass.h"
#include "Render/Public/Pipeline/view_frustum.h"
#include "Render/Private/Pipeline/strand_render_pass.h"
#include "Render/Private/Pipeline/strand_shutter_pass.h"
#include "Render/Private/Pipeline/strand_opaque_content.h"
#include "Render/Private/Pipeline/strand_shadow_pass.h"
#include "Render/Private/Pipeline/strand_opacity_shadow_pass.h"
#include "Render/Private/Pipeline/source_display_pass.h"
#include "Render/Private/Pipeline/eye_shell_render_pass.h"
#include "Render/Private/Pipeline/skin_diffusion_shaders.h"

namespace Render {
namespace {
void Require(bool result, const char* message) { if (!result) throw std::runtime_error(message); }
bool Finite(const glm::mat4& matrix) {
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
        if (!std::isfinite(matrix[c][r])) return false;
    return true;
}
bool Finite(const glm::vec3& value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
bool SameMatrix(const glm::mat4& a,const glm::mat4& b){
    for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(a[c][r]!=b[c][r])return false;
    return true;
}
struct Target {
    RenderResourceHandle<RenderTargetSpec> framebuffer;
    RenderResourceHandle<RHITextureSpec> color, depth;
    std::uint32_t width = 0, height = 0;
};
Target MakeTarget(PipelineDetail::ResourceBuilder& builder, std::uint32_t w, std::uint32_t h, bool color, bool depth,
                  std::uint32_t samples = 1) {
    Target result; result.width = w; result.height = h;
    if (color) result.color = builder.Texture(w, h, RHITextureFormat::RGBA16F, samples);
    if (depth) result.depth = builder.Texture(w, h, RHITextureFormat::Depth32F, samples);
    result.framebuffer = builder.Target(result.color, result.depth);
    return result;
}
void BindTarget(RHIFrameEncoder& encoder, const Target& target, std::uint8_t clear = RHICommand::ClearNone,
                glm::vec4 color = glm::vec4(0)) {
    RHICommand::SetRenderTarget command;
    command.target = target.framebuffer; command.width = target.width; command.height = target.height;
    command.clearFlags = clear; command.clearColor = color;
    Require(encoder.SetRenderTarget(command), "Cannot record render target");
}
void Upload(RHIFrameEncoder& encoder, RenderResourceHandle<UniformBufferSpec> buffer,
            const void* data, std::uint32_t size, std::uint32_t binding) {
    const auto* bytes = static_cast<const std::byte*>(data);
    for (std::uint32_t offset = 0; offset < size; offset += 256)
        Require(encoder.UpdateUniformBufferBytes(buffer, bytes + offset, std::min(256u, size - offset), offset), "Cannot upload pass data");
    Require(encoder.BindUniformBuffer(buffer, binding), "Cannot bind pass buffer");
}
} // namespace

struct MaterialRenderPipeline::Impl {
    RHIDevice& device;
    std::string error;
    std::shared_ptr<const void> assetsOwner, targetsOwner;
    RenderResourceHandle<VertexBufferSpec> fullscreen;
    RenderResourceHandle<UniformBufferSpec> viewBuffer, objectBuffer, lightingBuffer, effectsBuffer, postBuffer, temporalBuffer;
    RenderResourceHandle<UniformBufferSpec> surfaceBuffer, areaBuffer, indirectBuffer;
    RenderResourceHandle<UniformBufferSpec> skyBuffer;
    RenderResourceHandle<PipelineSpec> skyBackground;
    RenderResourceHandle<RHITextureSpec> skyAtlas, skyBrdf;
    std::shared_ptr<const SkyEnvironment> skyEnvironment;
    std::shared_ptr<const void> skyOwner;
    RenderResourceHandle<UniformBufferSpec> emptyProbeBuffer, probeDataBuffer, probeRuntimeBuffer;
    RenderResourceHandle<UniformBufferSpec> emptyProbeVisibility, probeVisibilityBuffer;
    std::shared_ptr<const DiffuseProbeVolume> probeVolume;
    std::shared_ptr<const void> probeOwner;
    std::shared_ptr<const void> realtimeOwner,realtimeGeometryOwner;
    std::shared_ptr<const RealtimeGiScene> realtimeScene;
    bool realtimeGeometryChanged=false,lumenGeometryChanged=false;
    unsigned lumenReactiveFrames=0;
    RenderResourceHandle<PipelineSpec> realtimeTrace,realtimeIntegrate;
    RenderResourceHandle<UniformBufferSpec> realtimeBuffer;
    RenderResourceHandle<RHITextureSpec> realtimeGeometry;
    Target realtimeRays;
    std::array<Target,2> realtimeCache;
    struct RealtimeHistory {
        std::uint64_t key=0,sequence=0;std::uint32_t image=0;bool valid=false;
        std::shared_ptr<const RealtimeGiScene> scene;
    } realtimeCompleted,realtimePending;
    std::shared_ptr<const void> lumenProgramsOwner,lumenSceneOwner,lumenCacheOwner,lumenDistanceOwner,lumenViewOwner;
    std::shared_ptr<const LumenGiScene> lumenScene;
    RenderResourceHandle<RHITextureSpec> lumenAttributes,lumenDistanceField;
    RenderResourceHandle<UniformBufferSpec> lumenBuffer;
    RenderResourceHandle<PipelineSpec> lumenDirect,lumenBounce,lumenIncidentFilter,lumenCompose,lumenWorldProbes,lumenGuide,lumenGather,lumenTemporal,lumenFilter,lumenResolve,lumenResolveDirect,lumenReflect,lumenReflectResolve,lumenReflectTemporal,lumenReflectFilter;
    std::array<Target,2> lumenDirectCache,lumenCache,lumenIncidentRaw,lumenIncidentCache,lumenGatherHistory,lumenGuides,lumenFilterWork;
    Target lumenRaw,lumenReflectionRaw,lumenReflectionGuide,lumenReflectionFull,lumenSpecularSource;
    Target lumenReflectionFullGrazing;
    std::array<Target,2> lumenReflectionGuides,lumenReflectionHistory,lumenGrazingHistory,lumenReflectionWork;
    struct LumenHistory {
        std::uint64_t key=0,sequence=0,viewKey=0,lightingKey=0;std::uint32_t image=0,viewImage=0;
        glm::mat4 vp{1};bool valid=false,viewValid=false;
        std::shared_ptr<const LumenGiScene> scene;
        unsigned reactiveFrames=0;
    } lumenCompleted,lumenPending;
    RenderResourceHandle<PipelineSpec> extract, downsample, upsample, ssao, ssaoBlur, composite, filter;
    RenderResourceHandle<PipelineSpec> copy, spatialUpsample, fxaa, taa, depthCopy, motionBlur;
    RenderResourceHandle<PipelineSpec> indirectGeometry, indirectGather, indirectDenoise, indirectUpsample;
    Target scene, reflection, sceneMsaa, reflectionMsaa, shadows, aoRaw, aoBlurred, display, filtered, blurred;
    Target opaqueSnapshot, diffuseCapture, normalCapture, indirectHalf, indirectFull;
    std::array<Target, 2> indirectWork;
    Target indirectPositions, indirectPositionsRaster, receiverCaptureMrt;
    std::shared_ptr<const void> placementProgramsOwner,placementTargetsOwner;
    std::shared_ptr<const void> placementUeProgramsOwner;
    RenderResourceHandle<PipelineSpec> placementUeMarkProgram,placementUeSpawnProgram;
    Compute::PipelineHandle placementUeComputeMark;
    RenderResourceHandle<UniformBufferSpec> placementBuffer;
    RenderResourceHandle<PipelineSpec> placementUniformProgram,placementMarkProgram,placementSpawnProgram,placementCoverageProgram,placementMarkerProgram;
    std::array<Target,2> placementUniform,placementMarked,placementSpawned;
    std::shared_ptr<Compute::Device> placementComputeOwner;
    Compute::PipelineHandle placementComputeMark;
    bool placementComputeChecked=false,placementComputeSupported=false,placementMarkedStorage=false,placementSpawnedStorage=false;
    std::shared_ptr<const void> placementComparisonOwner;
    Target placementComparisonMarked,placementComparisonSpawned;
    bool placementComparisonValid=false;
    std::uint32_t placementComparisonImage=0;
    std::uint64_t placementComparisonSequence=0;
    Target placementCoverage;
    struct PlacementState {
        bool valid=false;std::uint32_t image=0,phase=0,spacing=0,candidates=0,sourceWidth=0,sourceHeight=0;
        float threshold=0;bool adaptive=false;std::uint64_t sequence=0;bool cosineHemisphere=false;
        glm::mat4 viewProjection{1};
        std::uint64_t geometryGeneration=0;
        glm::vec4 refinement{0};
        glm::vec2 visibilityParameters{0};
        glm::vec4 curvature{0};
    } placementCompleted,placementPending;
    std::shared_ptr<const void> traceProgramsOwner,traceTargetsOwner;
    RenderResourceHandle<PipelineSpec> traceHzbBaseProgram,traceHzbReduceProgram,traceRayProgram,traceDisplayProgram;
    RenderResourceHandle<PipelineSpec> traceSafeRayProgram;
    std::shared_ptr<const void> traceSingleProgramsOwner,sdfSingleProgramsOwner;
    RenderResourceHandle<PipelineSpec> traceSingleRayProgram,traceSingleSafeRayProgram,sdfSingleFallbackProgram,sdfSingleSafeFallbackProgram;
    std::shared_ptr<const void> traceOutputProgramsOwner,sdfOutputProgramsOwner,traceOutputTargetsOwner;
    RenderResourceHandle<PipelineSpec> traceOutputRayProgram,traceOutputSafeRayProgram,traceOutputPackProgram;
    RenderResourceHandle<PipelineSpec> sdfOutputFallbackProgram,sdfOutputSafeFallbackProgram;
    std::array<RenderResourceHandle<RHITextureSpec>,4> traceOutputParts;
    Target traceOutputTarget;
    bool traceGeometryCapabilityChecked=false,traceGeometrySupported=false;
    Target traceSource;
    std::array<Target,ScreenProbeTraceMaxHzbLevels> traceHzb;
    std::array<Target,2> traceRays;
    std::uint32_t traceHzbLevels=0,traceResolution=0;
    struct TraceState {bool valid=false;unsigned image=0,resolution=0;bool sdf=false,volume=false,uploadsPruned=false;} traceCompleted,tracePending;
    std::shared_ptr<const void> sdfProgramsOwner,sdfMeshOwner,sdfWorldOwner,sdfViewOwner;
    std::shared_ptr<const ProbeSdfScene> sdfScene;
    RenderResourceHandle<UniformBufferSpec> sdfBuffer;
    RenderResourceHandle<PipelineSpec> sdfFallbackProgram,sdfSliceProgram,sdfHitMarkerProgram;
    RenderResourceHandle<PipelineSpec> sdfSafeFallbackProgram;
    // Nonzero execution mask minus one: fast, reuse, fast+reuse. Each mask
    // caches legacy fragment/MRT/GS modes and unsafe/safe-origin programs.
    std::array<std::array<std::shared_ptr<const void>,3>,3> sdfTraceVariantProgramsOwner;
    std::array<std::array<std::array<RenderResourceHandle<PipelineSpec>,2>,3>,3> sdfTraceVariantPrograms;
    // Native fragment/MRT fallback, indexed by the existing fast/reuse mask.
    std::array<std::array<std::shared_ptr<const void>,2>,64> sdfNativeProgramsOwner;
    std::array<std::array<std::array<RenderResourceHandle<PipelineSpec>,2>,2>,64> sdfNativePrograms;
    std::array<RenderResourceHandle<PipelineSpec>,3> sdfVolumePrograms;
    std::array<std::shared_ptr<const void>,3> sdfVolumeOwners;
    RenderResourceHandle<RHITextureSpec> sdfMeshTexture,sdfObjectTexture,sdfGlobalTexture,sdfGeometryTexture,sdfRangeTexture;
    std::array<Target,2> sdfRays;
    std::shared_ptr<Compute::Device> sdfCompactOwner,sdfCompactDataOwner;
    Compute::PipelineHandle sdfCompactReset;
    std::array<Compute::PipelineHandle,2> sdfCompactClassify,sdfCompactTrace;
    std::array<std::shared_ptr<Compute::Device>,3> sdfTraceVariantCompactOwner;
    std::array<std::array<Compute::PipelineHandle,2>,3> sdfTraceVariantCompactClassify,sdfTraceVariantCompactTrace;
    Compute::BufferHandle sdfCompactControl,sdfCompactQueue;
    std::uint32_t sdfCompactCapacity=0;
    std::uint64_t sdfCompactMaxBufferBytes=0;
    bool sdfCompactCapabilityChecked=false,sdfCompactSupported=false,sdfRaysStorage=false;
    bool sdfNativeUploadsPruned=false;
    std::uint64_t sdfUploadedGeneration=0,sdfMeshGeneration=0;
    std::uint64_t sdfUploadedGlobalGeneration=0,sdfUploadedGeometryGeneration=0,sdfUploadedRangeGeneration=0;
    unsigned sdfGlobalHeight=0,sdfObjectHeight=0,sdfGeometryHeight=0,sdfRangeHeight=0;
    struct SdfMeshUpload {RenderResourceHandle<RHITextureSpec> texture;std::shared_ptr<const void> owner;std::vector<std::shared_ptr<const ProbeMeshSdf>> assets;};
    std::map<std::vector<std::uintptr_t>,SdfMeshUpload> sdfMeshUploads;
    std::shared_ptr<const void> surfaceCacheProgramsOwner,surfaceCacheDataOwner,surfaceCacheViewOwner,surfaceCacheConstantsOwner;
    bool surfaceCacheHalfReceiverRadiance=false;
    std::shared_ptr<const void> surfaceCacheNativeLightingProgramsOwner,surfaceCacheNativeContactProgramsOwner;
    std::shared_ptr<const void> surfaceCacheNativeProbeOriginProgramsOwner;
    std::shared_ptr<const void> surfaceCacheReusedNativeProbeOriginProgramsOwner;
    std::array<std::shared_ptr<const void>,2> surfaceCacheNativeOriginContactProgramsOwner;
    std::array<std::array<RenderResourceHandle<PipelineSpec>,4>,2> surfaceCacheNativeOriginContactPrograms;
    std::array<std::shared_ptr<const void>,2> surfaceCacheReusedNativeOriginContactProgramsOwner;
    std::array<std::array<RenderResourceHandle<PipelineSpec>,4>,2> surfaceCacheReusedNativeOriginContactPrograms;
    std::array<std::shared_ptr<const void>,16> surfaceCacheQueryProbeOriginOwners;
    std::array<RenderResourceHandle<PipelineSpec>,16> surfaceCacheQueryProbeOriginPrograms;
    std::array<std::shared_ptr<const void>,256> surfaceCacheQueryContactOwners;
    std::array<std::array<RenderResourceHandle<PipelineSpec>,4>,256> surfaceCacheQueryContactPrograms;
    std::array<std::array<RenderResourceHandle<PipelineSpec>,2>,256> surfaceCacheQueryContactOriginPrograms;
    std::array<std::array<std::shared_ptr<const void>,6>,256> surfaceCacheReceiverTraceDiagnosticOwners;
    std::array<std::array<RenderResourceHandle<PipelineSpec>,12>,256> surfaceCacheReceiverTraceDiagnosticPrograms;
    std::array<std::shared_ptr<const void>,256> surfaceCacheReceiverTraceMrtOwners;
    std::array<std::array<RenderResourceHandle<PipelineSpec>,2>,256> surfaceCacheReceiverTraceMrtPrograms;
    unsigned surfaceCacheQueryProbeOriginKey=0;
    bool surfaceCacheQueryProbeOriginActive=false;
    bool surfaceCacheHardwareCapabilityChecked=false,surfaceCacheHardwareSupported=false;
    bool surfaceCacheNativeOriginsActive=false;
    bool surfaceCacheReusedNativeOriginsActive=false;
    std::shared_ptr<const void> areaLightShadowConstantsOwner;
    RenderResourceHandle<UniformBufferSpec> areaLightShadowBuffer;
    std::shared_ptr<const void> areaCasterPreparationOwner;
    std::vector<RenderResourceHandle<UniformBufferSpec>> areaCasterObjectBuffers,areaCasterMaterialBuffers;
    std::vector<std::uint32_t> areaCasterParameterSizes;
    struct AreaLightShadowState {
        bool valid=false;std::uint64_t key=0;Target atlas;std::shared_ptr<const void> owner;
    } areaLightShadowCompleted,areaLightShadowPending;
    struct AreaLightShadowAtlasLease {Target atlas;std::shared_ptr<const void> owner;};
    std::vector<AreaLightShadowAtlasLease> areaLightShadowAtlasPool;
    std::shared_ptr<const void> surfaceCacheProbeOwner;
    std::shared_ptr<const SurfaceCacheScene> surfaceCacheAsset;
    std::shared_ptr<const ProbeSdfScene> surfaceCacheGeometryScene;
    std::shared_ptr<const RealtimeGiScene> surfaceCacheUploadedOwnerGeometry;
    RenderResourceHandle<UniformBufferSpec> surfaceCacheBuffer;
    RenderResourceHandle<UniformBufferSpec> surfaceCacheOriginSafetyBuffer;
    RenderResourceHandle<PipelineSpec> surfaceCacheProbeOriginProgram,surfaceCacheProbeOriginMarkerProgram;
    RenderResourceHandle<PipelineSpec> surfaceCacheNativeProbeOriginProgram;
    RenderResourceHandle<PipelineSpec> surfaceCacheReusedNativeProbeOriginProgram;
    RenderResourceHandle<PipelineSpec> surfaceCacheLightingProgram,surfaceCacheGatherProgram,surfaceCacheResolveProgram,surfaceCacheCoverageProgram;
    RenderResourceHandle<PipelineSpec> surfaceCacheNativeLightingProgram;
    RenderResourceHandle<PipelineSpec> surfaceCacheNativeContactGatherProgram,surfaceCacheNativeFreshGatherProgram;
    RenderResourceHandle<PipelineSpec> surfaceCacheNativeSamplingDiagnosticProgram,surfaceCacheNativeFreshSamplingDiagnosticProgram;
    RenderResourceHandle<PipelineSpec> surfaceCacheProbeIntegrateProgram,surfaceCacheProbeActiveMaskProgram,surfaceCacheProbeDirectionalProgram,surfaceCacheProbeReconstructProgram,surfaceCacheProbeCoverageProgram;
    RenderResourceHandle<RHITextureSpec> surfaceCacheAttributes,surfaceCacheMetadata,surfaceCacheGeometry;
    Target surfaceCacheLighting,surfaceCacheRaw,surfaceCacheResolved,surfaceCacheCoverage,surfaceCacheSource;
    PipelineDetail::NativeProbeTransportState<Target> nativeProbeTransport;
    PipelineDetail::UeLumenHighShortRangeAOState<Target> ueHighAO;
    PipelineDetail::UeLumenHighScreenProbeGatherState<Target> ueLumenHighGather;
    PipelineDetail::UeLumenHighTemporalState<Target> ueHighTemporal;
    PipelineDetail::UeLumenProbeImportanceState<Target> ueLumenProbeImportance;
    PipelineDetail::UeLumenHitSceneColorState<Target> ueHitSceneColor;
    PipelineDetail::UeLumenDirectLightingScheduleState ueLumenDirectLightingSchedule;
    std::shared_ptr<const void> transportProgramsOwner,transportOwner;
    std::shared_ptr<const void> lightingQueryProgramsOwner,lightingQueryOwner;
    RenderResourceHandle<PipelineSpec> lightingQueryProgram;
    Target lightingQueryTarget;
    std::shared_ptr<const void> lightingActualProgramsOwner,lightingActualOwner;
    RenderResourceHandle<PipelineSpec> lightingActualProgram;
    RenderResourceHandle<RHITextureSpec> lightingActualQueryTexture;
    Target lightingActualTarget;
    bool lightingActualNativeVisibility=false;
    RenderResourceHandle<PipelineSpec> transportUpdate,transportUpdateCoherent,transportResolve,transportResolveCore,transportResolveLow,transportResolveFull,transportClassify,transportDirect,transportFilter,transportReceiverFilter,transportReceiverPhysicalFilter,transportReconstructionProgram;
    RenderResourceHandle<PipelineSpec> transportDeferredCore,transportDeferredLow,transportDeferredFull,transportDeferredReconstruction,transportDeferredFilter;
    RenderResourceHandle<UniformBufferSpec> transportBuffer,transportPlacementBuffer,transportReceiverFilterBuffer;
    std::array<Target,2> transport;
    Target transportFiltered;
    std::shared_ptr<const void> transportViewOwner,transportReceiverFilterOwner;
    Target transportReceiverTarget,transportMask,transportSamplingMask,transportBase,transportReconstructed,transportReconstructedMask,transportReceiverScratch;
    Target transportLowFrequency,transportLowFrequencyMask,transportLowFrequencyBase,transportClassification;
    Target transportReconstructedBase;
    bool transportDeferredView=false,transportCompactDeferredMix=false;
    Target transportCompactRaw;
    bool transportCompactView=false;
    bool transportHalfReceiverRadiance=false,transportReceiverScratchHalfRadiance=false,transportCompactHalfRadiance=false;
    std::shared_ptr<Compute::Device> transportCompactOwner,transportCompactDataOwner;
    Compute::PipelineHandle transportCompactReset,transportCompactClassify,transportCompactTrace;
    Compute::BufferHandle transportCompactControl,transportCompactQueue;
    std::uint32_t transportCompactCapacity=0;
    std::array<std::array<RenderResourceHandle<RHITextureSpec>,3>,2> transportCoefficients;
    std::array<RenderResourceHandle<RHITextureSpec>,3> transportFilteredCoefficients;
    std::shared_ptr<const void> transportDiagnosticProgramsOwner,transportDiagnosticOwner;
    std::array<RenderResourceHandle<PipelineSpec>,4> transportDiagnosticPrograms;
    std::array<Target,2> transportDiagnostics;
    std::shared_ptr<const void> transportPrecisionOwner;
    RenderResourceHandle<RHITextureSpec> transportPrecision;
    std::vector<glm::vec4> transportPrecisionPixels;
    std::vector<ProbeSdfInstance> transportPrecisionInputs;
    std::vector<std::uint32_t> transportPrecisionForInput;
    // Hold the last snapshot so the topology identity backing the precision
    // map remains alive until that map has been rebuilt or released.
    std::shared_ptr<const RealtimeGiScene> transportPrecisionGeometry;
    struct TransportPrecisionUploadCompletion {unsigned pending=0;bool success=true;};
    // Callbacks own this completion independently of Impl, including when
    // preparation throws or the pipeline is cleared before they are drained.
    std::shared_ptr<TransportPrecisionUploadCompletion> transportPrecisionUploads;
    const ProbeSdfScene* transportPrecisionScene=nullptr;
    std::uint64_t transportPrecisionGeneration=0;
    struct TransportState {bool valid=false;std::uint64_t key=0;unsigned phase=0,image=0;std::uint64_t generation=0;} transportCompleted,transportPending;
    Target surfaceCacheProbeIrradiance,surfaceCacheProbeActiveMask,surfaceCacheProbeDirectional,surfaceCacheProbeOrigins;
    bool surfaceCacheSafeOriginsActive=false;
    Target surfaceCacheProbeCoverage;
    ScreenProbePlacementConstants placementFrameConstants;
    std::array<ScreenProbePlacementConstants,2> placementInitialFrameConstants{};
    ScreenProbePlacementConstants placementCommittedFrameConstants{};
    std::uint32_t placementPendingBaseSamplingSeed=0,placementCommittedBaseSamplingSeed=0;
    std::uint64_t placementCommittedGiTemporalSequence=0;
    bool placementCommittedGiTemporalSequenceAvailable=false;
    std::uint64_t surfaceCacheGeneration=0,surfaceCacheGeometryGeneration=0;
    std::uint32_t surfaceCacheUploadedCardCount=0;
    unsigned surfaceCacheAttributeHeight=0,surfaceCacheMetadataHeight=0,surfaceCacheGeometryHeight=0;
    struct SurfaceCacheState {bool valid=false;std::uint64_t lightingKey=0;bool probes=false;bool spatial=false;unsigned spatialImage=0;bool spatialRadianceOnly=false;bool rawCollected=true;} surfaceCacheCompleted,surfaceCachePending;
    struct SurfaceCacheGatherCacheState {std::uint64_t key=0;unsigned stableFrames=0;bool valid=false;}
        surfaceCacheGatherCacheCompleted,surfaceCacheGatherCachePending;
    std::shared_ptr<const void> surfaceCacheSpatialProgramsOwner,surfaceCacheSpatialOwner;
    std::shared_ptr<const void> surfaceCacheSpatialRadianceProgramsOwner;
    RenderResourceHandle<UniformBufferSpec> surfaceCacheSpatialBuffer;
    RenderResourceHandle<PipelineSpec> surfaceCacheSpatialNeighborsProgram,surfaceCacheSpatialFilterProgram;
    RenderResourceHandle<PipelineSpec> surfaceCacheSpatialRadianceFilterProgram,surfaceCacheSpatialRadianceIntegrateProgram,surfaceCacheSpatialRadianceDirectionalProgram;
    std::array<Target,2> surfaceCacheSpatialRays;
    Target surfaceCacheSpatialNeighbors,surfaceCacheSpatialIrradiance,surfaceCacheSpatialGi;
    std::shared_ptr<const void> surfaceCacheContactProgramsOwner,surfaceCacheContactOwner;
    RenderResourceHandle<UniformBufferSpec> surfaceCacheContactBuffer;
    RenderResourceHandle<PipelineSpec> surfaceCacheContactRiskProgram,surfaceCacheContactMaskProgram,surfaceCacheContactGatherProgram,surfaceCacheContactFilterProgram;
    RenderResourceHandle<PipelineSpec> surfaceCacheContactResolveProgram,surfaceCacheContactMaskExpandProgram;
    Target surfaceCacheContactRisk,surfaceCacheContactMask,surfaceCacheContactUnfilteredGi,surfaceCacheContactGi;
    Target surfaceCacheBudgetScreenRisk;
    RenderResourceHandle<PipelineSpec> surfaceCacheBudgetScreenRiskProgram,surfaceCacheBudgetMaskProgram;
    std::shared_ptr<const void> surfaceCacheBudgetRiskOwner;
    Target surfaceCacheContactFilterScratch;
    Target surfaceCacheContactMeasurements,surfaceCacheContactResult;
    std::shared_ptr<const void> surfaceCacheFreshProgramsOwner,surfaceCacheFreshOwner,surfaceCacheSamplingDiagnosticsOwner;
    RenderResourceHandle<UniformBufferSpec> surfaceCacheFreshBuffer;
    RenderResourceHandle<PipelineSpec> surfaceCacheFreshClassifyProgram,surfaceCacheFreshObjectClassifyProgram,surfaceCacheFreshGatherProgram,surfaceCacheFreshFilterProgram,surfaceCacheFreshResolveProgram;
    RenderResourceHandle<PipelineSpec> surfaceCacheFreshPositionProgram,surfaceCacheFreshObjectPositionProgram;
    RenderResourceHandle<PipelineSpec> surfaceCacheSamplingDiagnosticProgram,surfaceCacheFreshSamplingDiagnosticProgram;
    Target surfaceCacheFreshSampling,surfaceCacheReceiverSampling,surfaceCacheReceiverOrigins;
    std::shared_ptr<const void> surfaceCacheReceiverOriginsOwner;
    Target surfaceCacheReceiverTraceMrt;
    std::array<RenderResourceHandle<RHITextureSpec>,3> surfaceCacheReceiverTracePacked{};
    std::shared_ptr<const void> surfaceCacheReceiverTraceMrtOwner;
    std::shared_ptr<const void> surfaceCacheReceiverTraceSamplingOwner;
    std::shared_ptr<const void> surfaceCacheTemporalProgramsOwner,surfaceCacheTemporalOwner;
    bool surfaceCacheTemporalHalfReceiverRadiance=false;
    RenderResourceHandle<UniformBufferSpec> surfaceCacheTemporalBuffer;
    RenderResourceHandle<PipelineSpec> surfaceCacheTemporalProgram,surfaceCacheTemporalPositionProgram,surfaceCacheTemporalNormalProgram;
    RenderResourceHandle<PipelineSpec> surfaceCacheTemporalDisplayProgram,surfaceCacheTemporalCountProgram;
    RenderResourceHandle<PipelineSpec> surfaceCacheObjectTemporalProgram,surfaceCacheObjectTemporalPositionProgram;
    RenderResourceHandle<PipelineSpec> surfaceCacheDisocclusionTemporalProgram,surfaceCacheObjectDisocclusionTemporalProgram;
    std::shared_ptr<const void> surfaceCacheDisocclusionTemporalProgramsOwner,surfaceCacheObjectDisocclusionTemporalProgramsOwner;
    std::shared_ptr<const void> surfaceCacheObjectTemporalProgramsOwner,surfaceCacheObjectMotionOwner;
    std::shared_ptr<const void> surfaceCacheShadowTemporalProgramsOwner;
    RenderResourceHandle<UniformBufferSpec> surfaceCacheShadowMotionBuffer;
    RenderResourceHandle<PipelineSpec> surfaceCacheShadowTemporalProgram,surfaceCacheShadowObjectTemporalProgram;
    RenderResourceHandle<PipelineSpec> surfaceCacheShadowDisocclusionTemporalProgram,surfaceCacheShadowObjectDisocclusionTemporalProgram;
    RenderResourceHandle<UniformBufferSpec> surfaceCacheObjectMotionBuffer;
    std::shared_ptr<const void> surfaceCacheStaticMotionProgramsOwner;
    RenderResourceHandle<PipelineSpec> surfaceCacheStaticMotionProgram;
    RenderResourceHandle<UniformBufferSpec> surfaceCacheStaticMotionBuffer;
    std::array<float,64> surfaceCacheObjectMotionStates{};
    Target surfaceCacheObjectPreviousPositions;
    std::array<Target,2> surfaceCacheTemporalHistory,surfaceCacheTemporalPositions,surfaceCacheTemporalNormals;
    Target surfaceCacheTemporalGi;
    // Immutable snapshots contain copied transforms/identities, never a view
    // into ProbeSdfScene, which callers may update in place before Record().
    struct SurfaceCacheMotionSnapshot {
        struct Instance {
            std::shared_ptr<const ProbeMeshSdf> source;
            glm::mat4 transform{1};
            std::uint32_t userId=0;
            bool movable=false;
        };
        std::vector<Instance> instances;
        std::uint64_t cacheGeneration=0,geometryGeneration=0;
        bool certified=false;
    };
    static constexpr unsigned SurfaceCacheShadowBoundsHistoryCapacity=17;
    struct SurfaceCacheTemporalMotionState {
        std::shared_ptr<const SurfaceCacheMotionSnapshot> snapshot;
        std::uint64_t stableKey=0;
        std::uint32_t settleRemaining=0;
        bool compatible=false,dynamicLighting=false;
        glm::vec4 shadowBoundsMin{0},shadowBoundsMax{0};
        std::array<glm::vec4,SurfaceCacheShadowBoundsHistoryCapacity> shadowBoundsHistoryMin{},shadowBoundsHistoryMax{};
        unsigned shadowBoundsHistoryCursor=0;
    };
    struct SurfaceCacheTemporalState {
        bool valid=false;unsigned image=0;std::uint64_t key=0,sequence=0;PipelineCamera camera;
        std::shared_ptr<const SurfaceCacheMotionSnapshot> motionSnapshot;
        std::uint64_t stableKey=0;
        std::uint32_t dynamicSettleRemaining=0;
        glm::vec4 shadowBoundsMin{0},shadowBoundsMax{0};
        std::array<glm::vec4,SurfaceCacheShadowBoundsHistoryCapacity> shadowBoundsHistoryMin{},shadowBoundsHistoryMax{};
        unsigned shadowBoundsHistoryCursor=0;
        bool cameraMotion=false; // completed camera-only epoch, for first-motion rejection
        unsigned motionStabilityFade=0;
        unsigned stableLightingFrames=0;
        unsigned freshSamplingSettleRemaining=0;
    } surfaceCacheTemporalCompleted,surfaceCacheTemporalPending;
    Target opaqueSnapshotDepth, reflectionSnapshot, reflectionSnapshotDepth;
    std::array<Target, 2> historyColor, historyDepth;
    std::array<Target, 6> bloomDown, bloomWork;
    std::uint32_t width = 0, height = 0, atlasResolution = 0;
    std::uint32_t samples = 1;
    float reflectionScale = 1;
    struct History {
        PipelineCamera camera;
        glm::mat4 jitteredVP{1};
        std::uint64_t contentHash = 0, sequence = 0;
        std::uint32_t image = 0;
        AntialiasingMode mode = AntialiasingMode::None;
        bool valid = false, taaValid = false, hairTaaValid = false;
        std::optional<double> strandTimeSeconds;
    } completed, pending;
    std::uint64_t nextToken = 0, pendingToken = 0, ueLumenViewFrame=0;
    std::uint64_t opaqueDepthEpoch=0;
    RenderResourceHandle<RHITextureSpec> lastBloom;
    PipelineStatistics statistics;
    ForwardRenderPipeline forward;
    PipelineDetail::StrandRenderPass strandPass;
    PipelineDetail::StrandShutterPass strandShutterPass;
    PipelineDetail::StrandShadowPass strandShadowPass;
    PipelineDetail::StrandOpacityShadowPass strandOpacityShadowPass;
    PipelineDetail::SourceDisplayPass sourceDisplayPass;
    Target opaqueStrandShadows;
    std::shared_ptr<const void> opaqueStrandShadowsOwner;
    RenderResourceHandle<RHITextureSpec> temporalDepthOverride;
    std::shared_ptr<const void> hairTemporalOwner;
    Target preHairSnapshot;
    std::array<Target,2> hairHistoryColor,hairHistoryDepth;
    RenderResourceHandle<PipelineSpec> hairTemporal,hairTemporalComposite;
    PipelineDetail::EyeShellRenderPass eyeShellPass;
    RenderResourceHandle<RHITextureSpec> displayColor;
    std::shared_ptr<const void> skinProgramsOwner,skinTargetsOwner;
    RenderResourceHandle<PipelineSpec> skinBlurProgram,skinBurleyProgram,skinComposeProgram;
    RenderResourceHandle<UniformBufferSpec> skinBuffer;
    Target skinDiffuse,skinProfile,skinAlbedo;
    RenderResourceHandle<RHITextureSpec> skinAttachmentDepth;
    struct SkinDepthVariant {
        RenderResourceHandle<PipelineSpec> pipeline;
        std::shared_ptr<const void> owner;
        bool used=false;
    };
    std::map<std::pair<std::uint32_t,std::uint32_t>,SkinDepthVariant> skinDepthVariants;
    std::array<Target,2> skinWork;

    explicit Impl(RHIDevice& device) : device(device), strandPass(device),strandShutterPass(device),strandShadowPass(device),strandOpacityShadowPass(device),sourceDisplayPass(device),eyeShellPass(device) {}
    void Initialize() {
        if (assetsOwner) return;
        PipelineDetail::ResourceBuilder builder(device);
        const auto vertex = FullscreenVertexShader();
        extract = builder.FullscreenPipeline(vertex, BloomExtractShader());
        downsample = builder.FullscreenPipeline(vertex, BloomDownsampleShader());
        upsample = builder.FullscreenPipeline(vertex, BloomUpsampleShader());
        ssao = builder.FullscreenPipeline(vertex, SsaoShader());
        ssaoBlur = builder.FullscreenPipeline(vertex, SsaoBlurShader());
        composite = builder.FullscreenPipeline(vertex, CompositeShader());
        filter = builder.FullscreenPipeline(vertex, FilterShader());
        copy = builder.FullscreenPipeline(vertex, CopyColorShader());
        spatialUpsample = builder.FullscreenPipeline(vertex, SpatialUpsampleShader());
        fxaa = builder.FullscreenPipeline(vertex, FxaaShader());
        taa = builder.FullscreenPipeline(vertex, TaaShader());
        depthCopy = builder.FullscreenPipeline(vertex, DepthHistoryCopyShader(), true);
        motionBlur = builder.FullscreenPipeline(vertex, CameraMotionBlurShader());
        indirectGeometry = builder.FullscreenPipeline(vertex, IndirectLightingGeometryShader());
        indirectGather = builder.FullscreenPipeline(vertex, IndirectLightingGatherShader(true));
        indirectDenoise = builder.FullscreenPipeline(vertex, IndirectLightingDenoiseShader(true));
        indirectUpsample = builder.FullscreenPipeline(vertex, IndirectLightingUpsampleShader(true));
        skyBackground = builder.FullscreenPipeline(vertex, std::string("#version 450 core\n") + SkyLightGLSL() + R"GLSL(
layout(std140,binding=0) uniform SkyViewData { mat4 inverseViewProjection; vec4 skyCamera; };
in vec2 vUV;
layout(location=0) out vec4 outColor;
void main() {
    vec4 world=inverseViewProjection*vec4(vUV*2.0-1.0,0,1);
    vec3 direction=world.xyz-world.w*skyCamera.xyz;
    outColor=vec4(clamp(SkyRadiance(direction,0.0)*skyOptions.y,vec3(0),vec3(60000)),0);
}
)GLSL");
        fullscreen = builder.FullscreenMesh();
        viewBuffer = builder.Uniform(sizeof(ViewConstants));
        objectBuffer = builder.Uniform(sizeof(ObjectConstants));
        lightingBuffer = builder.Uniform(sizeof(Material::PbrPassConstants));
        effectsBuffer = builder.Uniform(sizeof(SceneEffectsConstants));
        postBuffer = builder.Uniform(sizeof(PostProcessConstants));
        temporalBuffer = builder.Uniform(sizeof(TemporalConstants));
        surfaceBuffer = builder.Uniform(sizeof(SurfacePassConstants));
        areaBuffer = builder.Uniform(sizeof(AreaLightsConstants));
        indirectBuffer = builder.Uniform(sizeof(IndirectLightingConstants));
        skyBuffer = builder.Uniform(sizeof(SkyLightConstants));
        emptyProbeBuffer = builder.Uniform(sizeof(DiffuseProbeConstants));
        probeRuntimeBuffer = builder.Uniform(sizeof(DiffuseProbeRuntimeConstants));
        emptyProbeVisibility = builder.Uniform(sizeof(DiffuseProbeVisibilityConstants));
        realtimeBuffer=builder.Uniform(sizeof(RealtimeGiConstants));
        assetsOwner = builder.Finish();
    }
    void Resize(std::uint32_t w, std::uint32_t h, std::uint32_t resolution, std::uint32_t sampleCount, float scale) {
        Require(assetsOwner != nullptr, "Initialize pipeline before Resize");
        Require(w > 0 && h > 0 && w <= 8192 && h <= 8192, "Invalid viewport dimensions");
        Require(resolution >= 128 && resolution <= 8192 && resolution % 2 == 0, "CSM atlas must be even, 128..8192");
        Require(sampleCount == 1 || sampleCount == 2 || sampleCount == 4 || sampleCount == 8, "MSAA must be 1/2/4/8");
        Require(std::isfinite(scale) && scale >= 0.25f && scale <= 2, "Mirror scale must be 0.25..2");
        if (w == width && h == height && atlasResolution == resolution && samples == sampleCount && reflectionScale == scale) return;
        Require(!pendingToken, "Complete/discard recorded frame before resizing");
        const auto mirrorW = std::max(1u, static_cast<std::uint32_t>(std::ceil(w * scale)));
        const auto mirrorH = std::max(1u, static_cast<std::uint32_t>(std::ceil(h * scale)));
        Require(mirrorW <= 8192 && mirrorH <= 8192, "Mirror target exceeds 8192 pixels");
        // Build a complete replacement. Failure leaves the previous targets usable.
        PipelineDetail::ResourceBuilder builder(device);
        auto newScene = MakeTarget(builder, w, h, true, true);
        const auto halfW = std::max(1u, (w + 1) / 2), halfH = std::max(1u, (h + 1) / 2);
        auto newReflection = MakeTarget(builder, mirrorW, mirrorH, true, true);
        Target newSceneMsaa, newReflectionMsaa;
        if (sampleCount > 1) {
            newSceneMsaa = MakeTarget(builder, w, h, true, true, sampleCount);
            newReflectionMsaa = MakeTarget(builder, mirrorW, mirrorH, true, true, sampleCount);
        }
        auto newShadows = MakeTarget(builder, resolution, resolution, false, true);
        auto newAoRaw = MakeTarget(builder, halfW, halfH, true, false);
        auto newAoBlurred = MakeTarget(builder, halfW, halfH, true, false);
        auto newDisplay = MakeTarget(builder, w, h, true, false);
        auto newFiltered = MakeTarget(builder, w, h, true, false);
        auto newBlurred = MakeTarget(builder, w, h, true, false);
        auto newOpaque = MakeTarget(builder, w, h, true, true);
        Target newOpaqueDepth{{}, {}, newOpaque.depth, w, h};
        newOpaqueDepth.framebuffer = builder.Target({}, newOpaque.depth);
        auto newReflectionSnapshot = MakeTarget(builder, mirrorW, mirrorH, true, true);
        Target newReflectionDepth{{}, {}, newReflectionSnapshot.depth, mirrorW, mirrorH};
        newReflectionDepth.framebuffer = builder.Target({}, newReflectionSnapshot.depth);
        auto newDiffuse = MakeTarget(builder, w, h, true, true);
        auto newNormal = MakeTarget(builder, w, h, true, false);
        newNormal.framebuffer = builder.Target(newNormal.color, newDiffuse.depth);
        const auto longestHalf = std::max(halfW,halfH);
        const auto giW = longestHalf > MaxIndirectLightingDimension ? std::max(1u,halfW*MaxIndirectLightingDimension/longestHalf) : halfW;
        const auto giH = longestHalf > MaxIndirectLightingDimension ? std::max(1u,halfH*MaxIndirectLightingDimension/longestHalf) : halfH;
        auto newIndirectHalf = MakeTarget(builder, giW, giH, true, false);
        std::array<Target, 2> newIndirectWork;
        for (auto& target : newIndirectWork) target = MakeTarget(builder, giW, giH, true, false);
        auto newIndirectFull = MakeTarget(builder, w, h, true, false);
        Target newIndirectPositions;
        newIndirectPositions.width=w; newIndirectPositions.height=h;
        newIndirectPositions.color = builder.Texture(w,h,RHITextureFormat::RGBA32F);
        newIndirectPositions.framebuffer = builder.Target(newIndirectPositions.color,{});
        // The mesh capture compares against the same opaque/cutout prepass as
        // normal/rho capture. Keep a separate framebuffer: the legacy depth
        // reconstruction pass still samples newDiffuse.depth and must not
        // simultaneously attach that image for depth testing.
        Target newIndirectPositionsRaster=newIndirectPositions;
        newIndirectPositionsRaster.framebuffer=builder.Target(newIndirectPositions.color,newDiffuse.depth);
        Target newReceiverCaptureMrt=newDiffuse;
        newReceiverCaptureMrt.framebuffer=builder.TargetColors({newDiffuse.color,newNormal.color,newIndirectPositions.color,{}},newDiffuse.depth);
        std::array<Target, 2> newHistoryColor, newHistoryDepth;
        for (std::size_t i = 0; i < 2; ++i) {
            newHistoryColor[i] = MakeTarget(builder, w, h, true, false);
            newHistoryDepth[i] = MakeTarget(builder, w, h, false, true);
        }
        std::array<Target, 6> newDown, newWork;
        auto levelW = halfW, levelH = halfH;
        for (std::size_t i = 0; i < newDown.size(); ++i) {
            newDown[i] = MakeTarget(builder, levelW, levelH, true, false);
            newWork[i] = MakeTarget(builder, levelW, levelH, true, false);
            levelW = std::max(1u, (levelW + 1) / 2); levelH = std::max(1u, (levelH + 1) / 2);
        }
        auto newOwner = builder.Finish();
        scene = newScene; reflection = newReflection; shadows = newShadows; opaqueStrandShadows={};opaqueStrandShadowsOwner.reset();
        strandOpacityShadowPass.Reset();
        sceneMsaa = newSceneMsaa; reflectionMsaa = newReflectionMsaa;
        historyColor = newHistoryColor; historyDepth = newHistoryDepth;
        filtered = newFiltered; blurred = newBlurred;
        opaqueSnapshot = newOpaque; diffuseCapture = newDiffuse; normalCapture = newNormal;
        opaqueSnapshotDepth = newOpaqueDepth; reflectionSnapshot = newReflectionSnapshot; reflectionSnapshotDepth = newReflectionDepth;
        indirectHalf = newIndirectHalf; indirectFull = newIndirectFull;
        indirectWork = newIndirectWork;
        indirectPositions = newIndirectPositions;
        indirectPositionsRaster = newIndirectPositionsRaster;
        receiverCaptureMrt = newReceiverCaptureMrt;
        aoRaw = newAoRaw; aoBlurred = newAoBlurred; display = newDisplay;
        bloomDown = newDown; bloomWork = newWork;
        targetsOwner = std::move(newOwner);
        hairTemporalOwner.reset();preHairSnapshot={};hairHistoryColor={};hairHistoryDepth={};
        hairTemporal={};hairTemporalComposite={};
        strandShutterPass.Shutdown();
        width = w; height = h; atlasResolution = resolution; lastBloom = {};
        samples = sampleCount; reflectionScale = scale; completed = {}; pending = {};
    }
    RenderView View(const PipelineCamera& camera) const {
        RenderView result;
        result.viewProjection = camera.projection * camera.view;
        result.cameraPosition = glm::vec4(camera.position, 1);
        result.viewUniform = viewBuffer; result.objectUniform = objectBuffer;
        return result;
    }
    void Effects(RHIFrameEncoder& encoder, const SceneEffectsConstants& effects) {
        Upload(encoder, effectsBuffer, &effects, sizeof(effects), 4);
    }
    void Depth(RHIFrameEncoder& encoder, const RenderFrame& frame, const glm::mat4& vp, bool shadowOnly) {
        ViewConstants view; view.viewProjection = vp;
        Upload(encoder, viewBuffer, &view, sizeof(view), 0);
        for (const auto& item : frame.items) {
            if (item.EffectiveLayer() != RenderLayer::Opaque && item.EffectiveLayer() != RenderLayer::Cutout) continue;
            if (shadowOnly && !item.castsShadows) continue;
            if (!item.material || !item.material->Resources().shadowPipeline.IsValid()) continue;
            const auto& material = *item.material;
            const auto& resources = material.Resources();
            Require(encoder.KeepAlive(item.material), "Cannot retain depth material");
            Require(encoder.BindPipeline(resources.shadowPipeline), "Cannot bind depth variant");
            for (const auto& binding : resources.textures)
                Require(encoder.BindTexture(binding.texture, binding.slot), "Cannot bind depth texture");
            const auto& bytes = material.Parameters().Bytes();
            if (!bytes.empty()) Upload(encoder, resources.parameterBuffer, bytes.data(), static_cast<std::uint32_t>(bytes.size()), 2);
            Upload(encoder, objectBuffer, &item.model, sizeof(item.model), 1);
            Require(encoder.BindMesh(item.mesh) && encoder.DrawIndexed(item.draw), "Cannot draw depth item");
        }
    }
    void Scene(RHIFrameEncoder& encoder, const RenderFrame& source, const PipelineCamera& camera,
               bool reflectionOnly, bool overlayOnly = false, int transparency = -1) {
        RenderFrame frame; frame.frameIndex = source.frameIndex;
        for (const auto& item : source.items) {
            if(item.material&&PipelineDetail::IsEyeShellMaterial(*item.material))continue;
            if ((item.EffectiveLayer() == RenderLayer::Overlay) != overlayOnly) continue;
            if (transparency >= 0 && (item.EffectiveLayer() == RenderLayer::Transparent) != (transparency == 1)) continue;
            if (reflectionOnly && !item.visibleInReflections) continue;
            frame.items.push_back(item);
            frame.items.back().viewDepth = -(camera.view * item.model * glm::vec4(0, 0, 0, 1)).z;
        }
        Require(forward.Record(encoder, frame, View(camera)), "Cannot record material scene");
    }
    void CaptureSurface(RHIFrameEncoder& encoder, const RenderFrame& frame, const PipelineCamera& camera, bool normals,
        const SurfacePassConstants* ownerCapture=nullptr) {
        const ViewConstants view{camera.projection * camera.view, glm::vec4(camera.position, 1)};
        Upload(encoder, viewBuffer, &view, sizeof(view), 0);
        for (const auto& item : frame.items) {
            if (!item.material || (item.EffectiveLayer() != RenderLayer::Opaque && item.EffectiveLayer() != RenderLayer::Cutout)) continue;
            const auto& resources = item.material->Resources();
            const auto variant = normals ? resources.normalPipeline : resources.diffuseRadiancePipeline;
            if (!variant.IsValid()) continue;
            if(ownerCapture){auto capture=*ownerCapture;capture.options.w=item.surfaceCacheObject>=0?-float(item.surfaceCacheObject+1):-.75f;
                Upload(encoder,surfaceBuffer,&capture,sizeof(capture),SurfacePassBinding);}
            Require(encoder.KeepAlive(item.material) && encoder.BindPipeline(variant), "Cannot bind indirect source variant");
            for (const auto& binding : resources.textures)
                Require(encoder.BindTexture(binding.texture, binding.slot), "Cannot bind indirect source texture");
            const auto& bytes = item.material->Parameters().Bytes();
            if (!bytes.empty()) Upload(encoder, resources.parameterBuffer, bytes.data(), static_cast<std::uint32_t>(bytes.size()), 2);
            Upload(encoder, objectBuffer, &item.model, sizeof(item.model), 1);
            Require(encoder.BindMesh(item.mesh) && encoder.DrawIndexed(item.draw), "Cannot draw indirect source");
        }
    }
    void CaptureSurfacePositions(RHIFrameEncoder& encoder, const RenderFrame& frame,
                                 const PipelineCamera& camera, const SurfacePassConstants& sourceSurface) {
        // Interpolate the visible source position directly rather than
        // unprojecting quantized device depth. The existing normal variant
        // retains its alpha/cutout coverage and depth comparison semantics.
        auto positions=sourceSurface;
        positions.view=camera.view;positions.projection=camera.projection;
        positions.options.z=3;
        Upload(encoder,surfaceBuffer,&positions,sizeof(positions),SurfacePassBinding);
        BindTarget(encoder,indirectPositionsRaster,RHICommand::ClearColor);
        CaptureSurface(encoder,frame,camera,true,sourceSurface.options.w<0?&positions:nullptr);
        // Preserve rho/normal capture mode, animation time and every unrelated
        // surface option for the caller's remaining render passes.
        Upload(encoder,surfaceBuffer,&sourceSurface,sizeof(sourceSurface),SurfacePassBinding);
    }
    bool CanCaptureSurfaceMrt(const RenderFrame& frame) const {
        // Decide before any capture draw: mixing material coverage from two
        // capture implementations would make owner/depth consistency implicit.
        for(const auto& item:frame.items) {
            if(!item.material||(item.EffectiveLayer()!=RenderLayer::Opaque&&item.EffectiveLayer()!=RenderLayer::Cutout))continue;
            const auto& r=item.material->Resources();
            if(!r.receiverCapturePipeline.IsValid()||!r.diffuseRadiancePipeline.IsValid()||!r.normalPipeline.IsValid())return false;
        }
        return true;
    }
    void CaptureSurfaceMrt(RHIFrameEncoder& encoder,const RenderFrame& frame,
                           const PipelineCamera& camera,const SurfacePassConstants& sourceSurface) {
        const ViewConstants view{camera.projection*camera.view,glm::vec4(camera.position,1)};
        Upload(encoder,viewBuffer,&view,sizeof(view),0);
        for(const auto& item:frame.items) {
            if(!item.material||(item.EffectiveLayer()!=RenderLayer::Opaque&&item.EffectiveLayer()!=RenderLayer::Cutout))continue;
            const auto& resources=item.material->Resources();
            if(sourceSurface.options.w<0) {
                auto capture=sourceSurface;
                capture.options.w=item.surfaceCacheObject>=0?-float(item.surfaceCacheObject+1):-.75f;
                Upload(encoder,surfaceBuffer,&capture,sizeof(capture),SurfacePassBinding);
            }
            Require(encoder.KeepAlive(item.material)&&encoder.BindPipeline(resources.receiverCapturePipeline),"Cannot bind MRT receiver capture variant");
            for(const auto& binding:resources.textures)
                Require(encoder.BindTexture(binding.texture,binding.slot),"Cannot bind MRT receiver capture texture");
            const auto& bytes=item.material->Parameters().Bytes();
            if(!bytes.empty())Upload(encoder,resources.parameterBuffer,bytes.data(),static_cast<std::uint32_t>(bytes.size()),2);
            Upload(encoder,objectBuffer,&item.model,sizeof(item.model),1);
            Require(encoder.BindMesh(item.mesh)&&encoder.DrawIndexed(item.draw),"Cannot draw MRT receiver capture");
        }
        Upload(encoder,surfaceBuffer,&sourceSurface,sizeof(sourceSurface),SurfacePassBinding);
    }
    void Fullscreen(RHIFrameEncoder& encoder, const Target& target, RenderResourceHandle<PipelineSpec> pipeline,
                    RenderResourceHandle<RHITextureSpec> input, RenderResourceHandle<RHITextureSpec> second,
                    RenderResourceHandle<RHITextureSpec> depth, const PostProcessConstants& constants,
                    RenderResourceHandle<RHITextureSpec> geometry = {}) {
        BindTarget(encoder, target);
        Upload(encoder, postBuffer, &constants, sizeof(constants), PostProcessBinding);
        Require(encoder.BindPipeline(pipeline) && encoder.BindMesh(fullscreen) &&
                encoder.BindTexture(input, 0) && encoder.BindTexture(second, 1) && encoder.BindTexture(depth, 2) &&
                encoder.BindTexture(geometry, 3) &&
                encoder.DrawIndexed(RHICommand::DrawIndexed{3}), "Cannot record fullscreen pass");
        ++statistics.postPasses;
    }
    void Resolve(RHIFrameEncoder& encoder, const Target& source, const Target& destination, bool color, bool depth) {
        if (samples == 1) return;
        RHICommand::ResolveRenderTarget command;
        command.source = source.framebuffer; command.destination = destination.framebuffer;
        command.color = color; command.depth = depth;
        Require(encoder.ResolveRenderTarget(command), "Cannot resolve multisample target");
        ++statistics.resolvePasses;
    }
    void TemporalPass(RHIFrameEncoder& encoder, const Target& target, RenderResourceHandle<PipelineSpec> pipeline,
                      RenderResourceHandle<RHITextureSpec> input, RenderResourceHandle<RHITextureSpec> history,
                      RenderResourceHandle<RHITextureSpec> previousDepth, const TemporalConstants& constants) {
        BindTarget(encoder, target);
        Upload(encoder, temporalBuffer, &constants, sizeof(constants), TemporalEffectsBinding);
        Require(encoder.BindPipeline(pipeline) && encoder.BindMesh(fullscreen) &&
                encoder.BindTexture(input, 0) && encoder.BindTexture(history, 1) &&
                encoder.BindTexture(temporalDepthOverride.IsValid()?temporalDepthOverride:scene.depth, 2) && encoder.BindTexture(previousDepth, 3) &&
                encoder.BindTexture(constants.reactive.x > 0.5f ? opaqueSnapshot.color : RenderResourceHandle<RHITextureSpec>{}, 4) &&
                encoder.DrawIndexed(RHICommand::DrawIndexed{3}), "Cannot record temporal pass");
        ++statistics.postPasses;
    }
    void PrepareHairTemporal(){
        if(hairTemporalOwner)return;
        PipelineDetail::ResourceBuilder builder(device);
        preHairSnapshot=MakeTarget(builder,width,height,true,false);
        for(std::size_t i=0;i<2;++i){hairHistoryColor[i]=MakeTarget(builder,width,height,true,false);hairHistoryDepth[i]=MakeTarget(builder,width,height,false,true);}
        hairTemporal=builder.FullscreenPipeline(FullscreenVertexShader(),PipelineDetail::HairTemporalShader());
        hairTemporalComposite=builder.FullscreenPipeline(FullscreenVertexShader(),PipelineDetail::HairTemporalCompositeShader());
        hairTemporalOwner=builder.Finish();
    }
    void HairTemporalPass(RHIFrameEncoder& encoder,const PipelineDetail::StrandTemporalTextures& hair,
                          unsigned write,bool valid,const TemporalConstants& temporal){
        BindTarget(encoder,hairHistoryColor[write]);
        Upload(encoder,temporalBuffer,&temporal,sizeof(temporal),TemporalEffectsBinding);
        Require(encoder.BindPipeline(hairTemporal)&&encoder.BindMesh(fullscreen)&&
            encoder.BindTexture(hair.resolvedColor,0)&&encoder.BindTexture(valid?hairHistoryColor[completed.image].color:RenderResourceHandle<RHITextureSpec>{},1)&&
            encoder.BindTexture(hair.nearestDepth,2)&&encoder.BindTexture(valid?hairHistoryDepth[completed.image].depth:RenderResourceHandle<RHITextureSpec>{},3)&&
            encoder.BindTexture(hair.motion,4)&&encoder.DrawIndexed({3}),"Cannot accumulate premultiplied hair history");
        ++statistics.postPasses;
    }
    // Opaque objects still use camera reprojection. Strand geometry may retain
    // history when its explicit previous-input/transform contract is valid.
    // No opaque object velocity buffer yet. Reset accumulation when geometry/material
    // content changes. Live texture uploads require the caller's ResetHistory().
    std::uint64_t ContentHash(const RenderFrame& frame, const MaterialPipelineSettings& settings,bool includeModels=true) const {
        std::uint64_t hash = 14695981039346656037ull;
        const auto bytes = [&](const void* data, std::size_t count) {
            const auto* p = static_cast<const unsigned char*>(data);
            for (std::size_t i = 0; i < count; ++i) { hash ^= p[i]; hash *= 1099511628211ull; }
        };
        const auto add = [&](const auto& value) { bytes(&value, sizeof(value)); };
        for (const auto& item : frame.items) {
            if (item.layer == RenderLayer::Overlay) continue;
            add(item.mesh); add(item.EffectivePipeline()); add(item.texture);
            if(includeModels)add(item.model);
            add(item.draw.indexCount); add(item.draw.firstIndex); add(item.draw.baseVertex);
            add(item.draw.instanceCount); add(item.draw.firstInstance);
            add(item.layer); add(item.order); add(item.castsShadows); add(item.visibleInReflections);add(item.analyticEmission);
            add(item.surfaceCacheObject);
            if (item.material) {
                add(item.material->Resources().shadowPipeline);
                add(item.material->Resources().normalPipeline); add(item.material->Resources().diffuseRadiancePipeline);
                add(item.material->Resources().receiverCapturePipeline);
                add(item.material->Resources().ownerGatedPreviousWorldPipeline);add(item.material->Resources().staticWorldCapture);
                const auto& values = item.material->Parameters().Bytes(); bytes(values.data(), values.size());
                for (const auto& texture : item.material->Resources().textures) { add(texture.slot); add(texture.texture); }
            }
        }
        for(const auto& item:frame.strands){
            if(!item.geometry)continue;
            const bool continuous=settings.strandMotionVectors&&settings.strandTemporalDepth&&samples==1&&
                settings.temporal.antialiasing==AntialiasingMode::TAA&&item.previousTransformValid&&item.geometry->PreviousInputMatched();
            if(!continuous){add(item.geometry->Mesh());add(item.geometry->Revision());if(includeModels)add(item.model);}
            add(item.geometry->PointCount());add(item.geometry->IndexCount());
            add(item.guides);add(item.strandCountFraction);const auto& m=item.material;
            add(m.volumeProxyBlockers);
            if(m.volumeProxyBlockers&&item.opaqueCapsules){
                add(item.opaqueCapsules->size());
                for(const auto& capsule:*item.opaqueCapsules){add(capsule.startRadius);add(capsule.endRadius);}
            }
            add(m.rootColor);add(m.tipColor);add(m.sourceColor);add(m.gradientWeights);add(m.colorRandomOffsets);add(m.hsvVariation);
            add(m.gradientTable.get());add(m.nttTable.get());add(m.nativeMomentTable.get());add(m.nativeMomentParameters);add(m.useNativeMomentLut);add(m.useNttLookup);add(m.physicalScattering);add(m.sourceDirectScattering);
            add(m.roughness);add(m.secondaryRoughness);add(m.specularStrength);add(m.secondarySpecular);add(m.transmission);
            add(m.ior);add(m.longitudinalRoughness);add(m.azimuthalRoughness);add(m.absorptionScale);add(m.cuticleShift);
            add(m.sourceRoughnessMapping);
            add(m.multipleScattering);add(m.scatteringDensity);add(m.albedoBlendRate);add(m.indirectLightingMultiplier);add(m.backscatterScale);
            add(m.widthScale);add(m.opacity);add(m.minWidthMetres);add(m.maxWidthMetres);add(m.minPixelWidth);add(m.maxPixelWidth);add(m.areaPreservingCoverage);
            add(m.spatialVolume);add(m.normalizedVolumeWidth);add(m.volumeAverageWidth);add(m.volumeSkyTransmission);add(m.dualScatteringIbl);
            add(m.volumeVoxelSize);add(m.volumeResolution);add(m.volumeRasterWidthScale);add(m.volumeTraceStep);add(m.volumeBiasVoxels);add(m.volumePreserveNearField);
            add(m.sourceDSVolume);add(m.volumeRasterTraceStep);add(m.volumeTraceLimit);add(m.volumeInterpolationSamples);
            add(m.volumeInjectionRate);add(m.volumeInjectionLodFraction);add(m.volumeInjectionMinLodFactor);add(m.volumeInjectionQuality);
            add(m.receiveOpaqueShadows);add(m.opaqueShadowPower);add(m.volumeOpaqueBlockers);add(m.volumeHardwareFiltering);add(m.volumeDepthThreshold);add(m.castDirectionalShadows);add(m.sourceSpatialIbl);add(m.volumeExactIntegration);
            add(m.volumeStableOpaqueCache);add(m.volumePointIbl);add(m.sourceSegmentShading);add(m.sourceCurveGeometry);
        }
        add(settings.shadows.enabled); add(settings.shadows.sunDirection); add(settings.shadows.sunColor);
        add(settings.shadows.sunIntensity); add(settings.shadows.distance); add(settings.shadows.splitLambda);
        add(settings.shadows.depthBias); add(settings.shadows.normalBias); add(settings.shadows.cascadeBlend);
        add(settings.shadows.depthBiasTexels); add(settings.shadows.normalBiasTexels);
        add(settings.shadows.distanceFade); add(settings.shadows.receiverPlaneClampTexels);
        add(settings.shadows.casterPadding); add(settings.shadows.debugView);
        add(settings.shadows.filter); add(settings.shadows.sourcePoissonRadius); add(settings.shadows.sourcePoissonRotationRadians);
        add(settings.shadows.sourceSurfaceRadius);
        add(settings.shadows.strandCoverage);
        add(settings.shadows.strandOpacityScale);
        add(settings.reflection.enabled); add(settings.reflection.plane); add(settings.reflection.strength);
        add(settings.reflection.receiverBounds.has_value());
        if(settings.reflection.receiverBounds) {
            add(settings.reflection.receiverBounds->minimum);add(settings.reflection.receiverBounds->maximum);
        }
        add(settings.skinDiffusion.enabled);add(settings.skinDiffusion.strength);
        add(settings.skinDiffusion.radiusMillimeters);add(settings.skinDiffusion.channelRadii);
        add(settings.skinDiffusion.worldUnitScale);add(settings.skinDiffusion.normalRejectCosine);
        add(settings.skinDiffusion.albedoPreservation);
        add(settings.skinDiffusion.kernel);add(settings.skinDiffusion.diskSamples);
        add(settings.eyeShell.enabled);
        add(settings.strandFrontLayerTransparency);add(settings.strandFrontDepthGroups);
        add(settings.strandPairedLayerTransparency);
        add(settings.strandNativeOit);add(settings.strandNativeOitLayers);
        add(settings.strandDebugMode);
        add(settings.strandSourceDirect.enabled);
        if(settings.strandSourceDirect.enabled){
            add(settings.strandSourceDirect.count);add(settings.strandSourceDirect.iesTable.get());
            add(settings.strandSourceDirect.distanceToSourceMetres);
            for(std::uint32_t i=0;i<std::min(settings.strandSourceDirect.count,MaxStrandSourceLights);++i)
                add(settings.strandSourceDirect.lights[i]);
        }
        add(settings.strandIndirect.enabled);
        if(settings.strandIndirect.enabled){
            for(const auto& coefficient:settings.strandIndirect.coefficients)add(coefficient);
            add(settings.strandIndirect.multiplier);add(settings.strandIndirect.exposureEV);add(settings.strandIndirect.rotation);
            add(settings.strandIndirect.hostAmbientScale);
        }
        add(settings.temporal.hairCoverageHistoryRejection);add(settings.temporal.hairFastMotionPixels);
        add(settings.temporal.hairMotionBlurEnabled);add(settings.temporal.hairMotionBlurShutterSeconds);
        add(settings.strandTemporalDepth);add(settings.strandMotionVectors);add(settings.strandDepthWriteThreshold);
        add(settings.skinDiffusion.depthRejectScale);add(settings.skinDiffusion.maxRadiusPixels);
        add(settings.post.ssaoEnabled); add(settings.post.ssaoRadius); add(settings.post.ssaoBias); add(settings.post.ssaoPower);
        add(settings.lighting.ambientAndExposure.x); add(settings.lighting.ambientAndExposure.y); add(settings.lighting.ambientAndExposure.z);
        add(settings.lighting.specularAA);
        const auto areas = MakeAreaLightsConstants(settings.areaLights); bytes(&areas, sizeof(areas));
        add(settings.areaLights.stratifiedSamples);
        const auto& areaShadow=settings.surfaceCacheGi;
        add(areaShadow.cacheBilinear);
        add(areaShadow.reuseAnyHitBounds);
        add(areaShadow.reconstructionDebug);
        add(areaShadow.directionalReconstruction);
        add(areaShadow.safeProbeOrigins);add(areaShadow.originSafetyFraction);add(areaShadow.originSafetyEpsilon);
        add(areaShadow.contact.enabled);add(areaShadow.contact.raysPerPixel);add(areaShadow.contact.maxDistance);
        add(areaShadow.contact.parallaxAngleDegrees);add(areaShadow.contact.weightThreshold);
        add(areaShadow.contact.filterRadius);add(areaShadow.contact.filterStrength);add(areaShadow.contact.layerValidation);
        add(areaShadow.contact.coherentProbeRisk);
        add(areaShadow.contact.disocclusionSampling);add(areaShadow.contact.disocclusionRayMultiplier);
        add(areaShadow.temporal.staticConvergence);
        add(areaShadow.temporal.motionConsistentSpatialReuse);
        add(areaShadow.temporal.localCameraHistory);
        add(areaShadow.temporal.motionStability);
        add(areaShadow.temporal.motionSpatialFilter);
        add(areaShadow.temporal.motionDisocclusionRecovery);
        add(areaShadow.temporal.objectMotionReprojection);add(areaShadow.temporal.disocclusionFilter);
        add(areaShadow.probeOriginDiagnostics);
        add(areaShadow.areaLightShadows);
        if(areaShadow.areaLightShadows) {
            add(areaShadow.areaShadows.enabled);
            add(areaShadow.areaShadows.tileResolution);add(areaShadow.areaShadows.nearPlane);
            add(areaShadow.areaShadows.depthBiasTexels);add(areaShadow.areaShadows.normalBiasTexels);
        }
        const auto spot=MakeSpotLightConstants(settings.spotLight);
        add(spot.positionRadius.w);add(spot.directionOuter.w);add(spot.intensityInner);
        add(settings.indirect.enabled); add(settings.indirect.intensity); add(settings.indirect.radius);
        add(settings.indirect.thickness); add(settings.indirect.bias); add(settings.indirect.sampleCount); add(settings.indirect.stepCount);
        add(settings.sky.enabled); add(settings.sky.background); add(settings.sky.intensity);
        add(settings.sky.diffuseStrength); add(settings.sky.specularStrength); add(settings.sky.rotation);
        add(settings.sky.occlusionStrength); add(settings.sky.environment.get());
        add(settings.probes.enabled); add(settings.probes.intensity); add(settings.probes.normalBias);
        add(settings.probes.visibilityBias); add(settings.probes.volume.get());
        const auto& rt=settings.realtimeGi;
        add(rt.enabled);add(rt.scene.get());add(rt.origin);add(rt.spacing);add(rt.counts);
        add(rt.raysPerProbe);add(rt.probesPerFrame);add(rt.maxDistance);add(rt.rayBias);
        add(rt.normalBias);add(rt.visibilityBias);add(rt.historyWeight);add(rt.bounceFeedback);add(rt.intensity);
        const auto& lm=settings.lumenGi;
        add(lm.enabled);add(lm.scene.get());add(lm.screenTraces);add(lm.reflections);add(lm.intensity);
        add(lm.lightingView);add(lm.fullResolutionDirectLighting);add(lm.traceDirectLighting);
        add(lm.maxDistance);add(lm.rayBias);add(lm.thickness);add(lm.surfaceHistory);add(lm.bounceFeedback);
        add(lm.gatherHistory);add(lm.surfaceUpdatesPerFrame);add(lm.surfaceRays);add(lm.probeSpacing);add(lm.gatherRays);add(lm.screenSteps);
        add(lm.distanceFields);add(lm.reflectionRays);add(lm.reflectionResolutionScale);add(lm.reflectionTraceMaxDimension);
        add(lm.reflectionSourceResolutionScale);
        add(lm.origin);add(lm.spacing);add(lm.counts);
        const auto& placement=settings.screenProbePlacement;
        add(placement.enabled);
        if(placement.enabled) {
            add(placement.adaptive);add(placement.freeze);add(placement.baseSpacing);add(placement.candidateCount);
            add(placement.relativeDepthThreshold);add(placement.markerRadiusPixels);add(placement.debugView);add(placement.frameIndex);
            add(placement.cornerRefinement);add(placement.cornerDetectionRadius);add(placement.cornerProbeSupport);add(placement.cornerNormalAngleDegrees);
            add(placement.reconstructionVisibility);
            add(placement.continuousInterpolation);
            add(placement.curvatureAwareWeights);
            add(placement.curvatureRefinement);add(placement.curvatureNormalAngleDegrees);add(placement.curvaturePlaneErrorPixels);
            add(placement.trace.enabled);add(placement.trace.rayResolution);add(placement.trace.maxSteps);add(placement.trace.maxMip);add(placement.trace.singleInvocation);add(placement.trace.singlePassOutputs);add(placement.trace.compactSdfRays);
            add(placement.trace.displayMode);add(placement.trace.maxDistance);add(placement.trace.thickness);
            add(placement.trace.normalBias);add(placement.trace.relativeThickness);add(placement.trace.selectedProbe);add(placement.trace.showPanel);
            const auto& sdf=placement.trace.sdf;add(sdf.enabled);add(sdf.useMesh);add(sdf.useGlobal);add(sdf.meshTraceDistance);
            add(sdf.hardwareRayTracing);
            add(sdf.hitEpsilon);add(sdf.maxSteps);add(sdf.sliceView);add(sdf.sliceAxis);add(sdf.slicePosition);add(sdf.selectedObject);add(sdf.showHitPoints);
            add(sdf.visualMode);add(sdf.visualizeSelected);add(sdf.showVoxelGrid);add(sdf.visualEpsilon);add(sdf.meshQuality);add(sdf.automaticQuality);
            if(sdf.scene){add(sdf.scene.get());add(sdf.scene->Statistics().generation);}
        }
        // Animated point lights deliberately use the luminance-reactive history
        // weight in the TAA shader rather than resetting every animation frame.
        return hash;
    }
    bool CameraCut(const PipelineCamera& camera, const MaterialPipelineSettings& settings) const {
        if (!completed.valid || settings.cameraCut || settings.deltaSeconds > 0.25f) return true;
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
            if (std::abs(camera.projection[c][r] - completed.camera.projection[c][r]) > 1e-5f) return true;
        const auto oldDirection = glm::normalize(glm::vec3(glm::inverse(completed.camera.view)[2]));
        const auto direction = glm::normalize(glm::vec3(glm::inverse(camera.view)[2]));
        return glm::distance(camera.position, completed.camera.position) > 3.0f || glm::dot(oldDirection, direction) < 0.5f;
    }
    void Validate(const RenderFrame& frame, const PipelineCamera& camera, const MaterialPipelineSettings& settings) {
        Require(assetsOwner && targetsOwner, "Pipeline requires Initialize and Resize");
        Require(!pendingToken, "Complete/discard the previous recorded frame before Record");
        Require((settings.outputSize.x == 0 && settings.outputSize.y == 0) ||
                (settings.outputSize.x > 0 && settings.outputSize.y > 0 &&
                 settings.outputSize.x <= 8192 && settings.outputSize.y <= 8192),
                "Output extent must be {0,0} or both dimensions in 1..8192");
        Require(Finite(camera.view) && Finite(camera.projection) && Finite(camera.position) &&
                std::abs(glm::determinant(camera.projection)) > 1e-10f &&
                std::abs(glm::determinant(camera.view)) > 1e-10f, "Invalid camera matrices");
        Require(std::isfinite(camera.nearPlane) && std::isfinite(camera.farPlane) &&
                camera.nearPlane > 0 && camera.farPlane > camera.nearPlane, "Invalid camera clip range");
        Require(ValidateSkinDiffusionSettings(settings.skinDiffusion,&error),"Invalid skin diffusion settings");
        Require(!settings.skinDiffusion.enabled||samples==1,
                "Skin diffusion requires single-sample opaque depth; select TAA or FXAA instead of MSAA");
        Require(ValidatePostProcessSettings(settings.post, &error), "Invalid post-process settings");
        Require(ValidateTemporalEffectsSettings(settings.temporal, &error), "Invalid temporal settings");
        Require(std::uint32_t(settings.strandDebugMode)<=std::uint32_t(StrandDebugMode::IndirectIncident),"Invalid hair diagnostic mode");
        Require(!settings.strandTimeSeconds||std::isfinite(*settings.strandTimeSeconds),"Strand render timeline must be finite");
        Require(ValidateAreaLightsSettings(settings.areaLights, &error), "Invalid area light settings");
        Require(ValidateSpotLightSettings(settings.spotLight, &error), "Invalid spot light settings");
        Require(ValidateIndirectLightingSettings(settings.indirect, &error), "Invalid indirect lighting settings");
        Require(ValidateRealtimeGiSettings(settings.realtimeGi,&error),"Invalid realtime GI settings");
        Require(ValidateLumenGiSettings(settings.lumenGi,&error),"Invalid hybrid Lumen GI settings");
        Require(ValidateScreenProbePlacementSettings(settings.screenProbePlacement),"Invalid screen probe placement settings");
        Require(ValidateSurfaceCacheGiSettings(settings.surfaceCacheGi),"Invalid Surface Cache GI settings");
        Require(!settings.surfaceCacheGi.areaLightShadows||settings.areaLights.count<=1,
            "Cached Surface Cache rectangle shadows currently support one area light");
        Require(!settings.surfaceCacheGi.enabled||(!settings.lumenGi.enabled&&!settings.realtimeGi.enabled&&!settings.probes.enabled&&!settings.indirect.enabled),
            "Card Surface Cache GI needs an independent diffuse GI mode");
        if(settings.surfaceCacheGi.enabled) {
            Require(!settings.sky.enabled||settings.sky.intensity<=0||settings.sky.diffuseStrength<=0,
                "Card Surface Cache GI supports point, sun, spot and rectangle illumination; disable sky diffuse lighting");
        }
        if(settings.surfaceCacheGi.enabled&&settings.surfaceCacheGi.probeDiagnostics&&settings.screenProbePlacement.enabled&&
            settings.screenProbePlacement.trace.enabled&&settings.screenProbePlacement.trace.sdf.enabled)
            Require(settings.screenProbePlacement.trace.sdf.scene&&settings.screenProbePlacement.trace.sdf.scene->PackedGeometry()==settings.surfaceCacheGi.scene->PackedGeometry(),
                "Surface Cache diagnostics must trace the same packed geometry");
        if(settings.surfaceCacheGi.ueLumenHigh){
            const auto& ue=settings.surfaceCacheGi;
            Require(ue.enabled&&ue.nativeScreenProbeTransport&&ue.temporal.enabled&&ue.temporal.retainHistoryOnObjectMotion&&
                (ue.view==0||ue.view==2||ue.view==5)&&ue.reconstructionDebug==0,
                "UE High needs native screen probes, certified temporal snapshots and a supported GI/history view");
        }
        const bool cacheView=settings.lumenGi.lightingView!=LumenLightingView::Material;
        Require(!cacheView||(settings.lumenGi.enabled&&!settings.lumenGi.reflections&&!settings.reflection.enabled),
                "Surface Cache views require Lumen GI with reflections disabled");
        // Coverage is a pure resolve diagnostic and does not rebuild receiver
        // lighting, so it tolerates transparent items present in the scene.
        const bool opaqueOnlyView=cacheView&&settings.lumenGi.lightingView!=LumenLightingView::Coverage;
        if(opaqueOnlyView)for(const auto& item:frame.items)
            Require(item.EffectiveLayer()!=RenderLayer::Transparent,"Surface Cache views require opaque receivers");
        Require(!settings.lumenGi.enabled||(!settings.realtimeGi.enabled&&!settings.probes.enabled&&!settings.indirect.enabled),
                "Hybrid Lumen GI is an independent mode; disable DDGI, baked probes and standalone screen GI");
        Require(!settings.realtimeGi.enabled||(!settings.probes.enabled&&!settings.indirect.enabled),
                "Realtime GI replaces baked probes and screen GI; enable one GI mode at a time");
        Require(ValidateSkyLightSettings(settings.sky, &error), "Invalid sky light settings");
        Require(ValidateDiffuseProbeSettings(settings.probes, &error), "Invalid diffuse probe settings");
        Require(settings.temporal.msaaSamples == samples && settings.reflection.resolutionScale == reflectionScale,
                "Resize required after changing MSAA or mirror resolution scale");
        Require(std::isfinite(settings.deltaSeconds) && settings.deltaSeconds > 0 && settings.deltaSeconds <= 10,
                "Invalid frame duration");
        const auto& shadow = settings.shadows;
        const auto nonnegativeRadiance = [](glm::vec3 value) {
            return Finite(value) && glm::all(glm::greaterThanEqual(value, glm::vec3(0))) &&
                   glm::all(glm::lessThanEqual(value, glm::vec3(1000000)));
        };
        Require(shadow.atlasResolution == atlasResolution, "Resize required after changing CSM atlas resolution");
        Require(Finite(shadow.sunDirection) && std::isfinite(glm::length(shadow.sunDirection)) &&
                glm::length(shadow.sunDirection) > 1e-6f && nonnegativeRadiance(shadow.sunColor) &&
                std::isfinite(shadow.sunIntensity) && shadow.sunIntensity >= 0 && shadow.sunIntensity <= 1000000 &&
                std::isfinite(shadow.distance) && shadow.distance > camera.nearPlane &&
                std::isfinite(shadow.splitLambda) && shadow.splitLambda >= 0 && shadow.splitLambda <= 1 &&
                std::isfinite(shadow.depthBias) && shadow.depthBias >= 0 && shadow.depthBias <= 0.05f &&
                std::isfinite(shadow.normalBias) && shadow.normalBias >= 0 && shadow.normalBias <= 2 &&
                std::isfinite(shadow.cascadeBlend) && shadow.cascadeBlend >= 0 && shadow.cascadeBlend <= 0.3f,
                "Invalid directional light or CSM settings");
        Require(std::isfinite(shadow.depthBiasTexels) && shadow.depthBiasTexels >= 0 && shadow.depthBiasTexels <= 2 &&
                std::isfinite(shadow.normalBiasTexels) && shadow.normalBiasTexels >= 0 && shadow.normalBiasTexels <= 2 &&
                std::isfinite(shadow.distanceFade) && shadow.distanceFade >= 0 && shadow.distanceFade <= 0.5f &&
                std::isfinite(shadow.receiverPlaneClampTexels) && shadow.receiverPlaneClampTexels >= 0 && shadow.receiverPlaneClampTexels <= 16 &&
                std::isfinite(shadow.casterPadding) && shadow.casterPadding >= 0 && shadow.casterPadding <= 10000 &&
                static_cast<std::uint32_t>(shadow.debugView) <= static_cast<std::uint32_t>(ShadowDebugView::Cascades),
                "Invalid shadow filtering, bias, caster range or debug view");
        Require(static_cast<std::uint32_t>(shadow.strandCoverage) <= static_cast<std::uint32_t>(StrandShadowCoverage::DeepOpacity) &&
                std::isfinite(shadow.strandOpacityScale) && shadow.strandOpacityScale >= 0 && shadow.strandOpacityScale <= 2 &&
                static_cast<std::uint32_t>(shadow.filter) <= static_cast<std::uint32_t>(DirectionalShadowFilter::SourcePoisson8) &&
                std::isfinite(shadow.sourcePoissonRadius) && shadow.sourcePoissonRadius >= 0 && shadow.sourcePoissonRadius <= 0.05f &&
                std::isfinite(shadow.sourceSurfaceRadius) && shadow.sourceSurfaceRadius >= 0 && shadow.sourceSurfaceRadius <= 0.05f &&
                std::isfinite(shadow.sourcePoissonRotationRadians) && std::abs(shadow.sourcePoissonRotationRadians) <= 10000,
                "Invalid source directional shadow filter, UV radius or rotation");
        Require(nonnegativeRadiance(glm::vec3(settings.lighting.ambientAndExposure)), "Invalid ambient radiance");
        const auto& specularAA = settings.lighting.specularAA;
        Require(Finite(glm::vec3(specularAA)) && std::isfinite(specularAA.w) &&
                specularAA.x >= 0 && specularAA.x <= 1 && specularAA.y >= 0 && specularAA.y <= 1 &&
                (specularAA.z == 0 || specularAA.z == 1), "Invalid specular antialiasing settings");
        for (std::size_t i = 0; i < 4; ++i) {
            const glm::vec3 position(settings.lighting.lightPositions[i]);
            Require(Finite(position) && glm::all(glm::lessThanEqual(glm::abs(position), glm::vec3(1000000))) &&
                    nonnegativeRadiance(glm::vec3(settings.lighting.lightColors[i])) &&
                    std::isfinite(settings.lighting.lightColors[i].w) &&
                    settings.lighting.lightColors[i].w>=0 && settings.lighting.lightColors[i].w<=10000,
                    "Invalid point light position/radiance/source radius");
        }
        Require(std::isfinite(settings.reflection.strength) && settings.reflection.strength >= 0 &&
                settings.reflection.strength <= 1 && std::isfinite(settings.time), "Invalid reflection/time settings");
        if(settings.reflection.receiverBounds) {
            const auto& bounds=*settings.reflection.receiverBounds;
            Require(Finite(bounds.minimum)&&Finite(bounds.maximum)&&
                    glm::all(glm::lessThanEqual(bounds.minimum,bounds.maximum)),
                    "Invalid planar reflection receiver bounds");
        }
        for (const auto& item : frame.items) {
            Require(item.mesh.IsValid() && item.EffectivePipeline().IsValid() && Finite(item.model), "Invalid render item");
            if (item.material) for (const auto& texture : item.material->Resources().textures)
                Require(texture.slot < 8, "MaterialRenderPipeline reserves texture slots 8..15 for passes");
        }
    }
    void PrepareSky(const SkyLightSettings& settings) {
        if (!settings.enabled) return;
        auto environment = settings.environment;
        if (!environment) environment = DefaultSkyEnvironment();
        if (skyOwner && skyEnvironment == environment) return;
        PipelineDetail::ResourceBuilder builder(device);
        const auto image = [&](std::uint32_t w, std::uint32_t h, const std::vector<glm::vec4>& pixels) {
            CreateRHITextureSpec desc;
            desc.width=w; desc.height=h; desc.textureDataStoreType=desc.textureUseType=RHITextureFormat::RGBA16F;
            desc.data=pixels.data(); desc.mipmaps=false;
            desc.filterMode=RHIFilterMode::Linear; desc.addressMode=RHIAddressMode::ClampToEdge;
            return builder.Create<RHITextureSpec>([&](auto cb) { device.async_CreateTexture(desc,cb); });
        };
        const auto atlas=image(SkyAtlasWidth,SkyAtlasHeight,environment->ReflectionAtlas());
        const auto brdf=image(SkyBrdfLutSize,SkyBrdfLutSize,SkyBrdfIntegrationLut());
        auto owner=builder.Finish(); // Atomic replacement; old recordings pin the previous owner.
        skyAtlas=atlas; skyBrdf=brdf; skyEnvironment=std::move(environment); skyOwner=std::move(owner);
    }
    void PrepareProbes(const DiffuseProbeSettings& settings) {
        if(!settings.enabled || (probeOwner && probeVolume==settings.volume)) return;
        PipelineDetail::ResourceBuilder builder(device);
        CreateUniformBufferDesc desc; desc.byteSize=sizeof(DiffuseProbeConstants);
        desc.initialData=&settings.volume->Constants(); desc.usage=BufferUsage::Static;
        const auto buffer=builder.Create<UniformBufferSpec>([&](auto cb) { device.async_CreateUniformBuffer(desc,cb); });
        desc.byteSize=sizeof(DiffuseProbeVisibilityConstants); desc.initialData=&settings.volume->Visibility();
        const auto visibility=builder.Create<UniformBufferSpec>([&](auto cb) { device.async_CreateUniformBuffer(desc,cb); });
        auto owner=builder.Finish();
        probeDataBuffer=buffer; probeVisibilityBuffer=visibility; probeVolume=settings.volume; probeOwner=std::move(owner);
    }
    void PrepareRealtimeGi(const RealtimeGiSettings& settings) {
        realtimeGeometryChanged=realtimeCompleted.scene!=settings.scene;
        if(!settings.enabled)return;
        if(!realtimeOwner) {
            PipelineDetail::ResourceBuilder builder(device);
            const auto target=[&](std::uint32_t w,std::uint32_t h) {
                Target t;t.width=w;t.height=h;t.color=builder.Texture(w,h,RHITextureFormat::RGBA32F);t.framebuffer=builder.Target(t.color,{});return t;
            };
            realtimeTrace=builder.FullscreenPipeline(FullscreenVertexShader(),PipelineDetail::RealtimeGiTraceShader());
            realtimeIntegrate=builder.FullscreenPipeline(FullscreenVertexShader(),PipelineDetail::RealtimeGiIntegrateShader());
            realtimeRays=target(RealtimeGiMaxRays,MaxDiffuseProbes);
            realtimeCache[0]=target(MaxDiffuseProbes,RealtimeGiCacheRows);realtimeCache[1]=target(MaxDiffuseProbes,RealtimeGiCacheRows);
            realtimeOwner=builder.Finish();
        }
        if(realtimeScene!=settings.scene||!realtimeGeometryOwner) {
            PipelineDetail::ResourceBuilder builder(device);CreateRHITextureSpec desc;
            desc.width=settings.scene->Width();desc.height=settings.scene->Height();desc.data=settings.scene->Pixels().data();
            desc.textureDataStoreType=desc.textureUseType=RHITextureFormat::RGBA32F;desc.mipmaps=false;
            desc.filterMode=RHIFilterMode::Nearest;desc.addressMode=RHIAddressMode::ClampToEdge;
            auto texture=builder.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(desc,cb);});
            auto owner=builder.Finish();realtimeGeometry=texture;realtimeGeometryOwner=std::move(owner);realtimeScene=settings.scene;
        }
    }
    void RecordRealtimeGi(RHIFrameEncoder& encoder,const MaterialPipelineSettings& settings,RenderResourceHandle<PipelineSpec> overrideTrace={}) {
        const auto& s=settings.realtimeGi;if(!s.enabled)return;
        std::uint64_t key=14695981039346656037ull;
        const auto add=[&](const auto& v){const auto* b=reinterpret_cast<const unsigned char*>(&v);for(std::size_t i=0;i<sizeof(v);++i){key^=b[i];key*=1099511628211ull;}};
        add(s.scene->TopologyKey());add(s.origin);add(s.spacing);add(s.counts);add(s.raysPerProbe);add(s.maxDistance);
        bool hasSources=s.scene->HasEmission()||(settings.sky.enabled&&settings.sky.intensity>0&&settings.sky.diffuseStrength>0)||
            (settings.shadows.sunIntensity>0&&glm::any(glm::greaterThan(settings.shadows.sunColor,glm::vec3(0))));
        hasSources|=settings.spotLight.enabled&&glm::any(glm::greaterThan(settings.spotLight.intensity,glm::vec3(0)));
        for(const auto& color:settings.lighting.lightColors) hasSources|=glm::any(glm::greaterThan(glm::vec3(color),glm::vec3(0)));
        for(std::uint32_t i=0;i<settings.areaLights.count;++i) {
            const auto& l=settings.areaLights.lights[i];hasSources|=l.enabled&&glm::any(glm::greaterThan(l.radiance,glm::vec3(0)));
        }
        // A source-free scene has an exact zero-radiance solution; clear the
        // feedback instead of allowing a temporally cached light to linger.
        const bool reset=s.reset||!hasSources||!realtimeCompleted.valid||key!=realtimeCompleted.key;
        const auto sequence=reset?0:realtimeCompleted.sequence;
        const std::uint32_t total=s.counts.x*s.counts.y*s.counts.z;
        const auto batch=reset||realtimeGeometryChanged?total:std::min(s.probesPerFrame,total),first=reset||realtimeGeometryChanged?0u:std::uint32_t(sequence*s.probesPerFrame%total);
        const std::uint32_t previous=reset?0:realtimeCompleted.image,write=1-previous;
        if(reset) {
            Require(encoder.BindTexture({},RealtimeGiCacheTextureSlot),"Cannot unbind realtime history");
            BindTarget(encoder,realtimeCache[previous],RHICommand::ClearColor);
        }
        RealtimeGiConstants c;c.origin=glm::vec4(s.origin,0);c.spacing=glm::vec4(s.spacing,0);c.counts=glm::vec4(glm::vec3(s.counts),total);
        c.trace={s.maxDistance,s.rayBias,s.historyWeight,s.bounceFeedback};
        if(realtimeGeometryChanged&&!reset)c.trace.z=0;
        c.options={int(s.raysPerProbe),int(first),int(batch),int(sequence%4096)};
        c.geometry={int(s.scene->NodeCount()*2),int(s.scene->Width()),int(s.scene->NodeCount()),int(s.scene->TriangleCount())};
        c.sunDirectionIntensity=glm::vec4(glm::normalize(settings.shadows.sunDirection),settings.shadows.sunIntensity);
        c.sunColor=glm::vec4(settings.shadows.sunColor,1);c.runtime={reset?1:0,s.normalBias,s.visibilityBias,realtimeGeometryChanged?2:1};
        c.spotLight=MakeSpotLightConstants(settings.spotLight);
        Upload(encoder,realtimeBuffer,&c,sizeof(c),RealtimeGiBinding);
        BindTarget(encoder,realtimeRays);
        Require(encoder.BindPipeline(overrideTrace.IsValid()?overrideTrace:realtimeTrace)&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(realtimeGeometry,0)&&
                encoder.BindTexture(realtimeCache[previous].color,RealtimeGiCacheTextureSlot)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot trace realtime GI rays");
        BindTarget(encoder,realtimeCache[write]);
        Require(encoder.BindPipeline(realtimeIntegrate)&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(realtimeRays.color,0)&&
                encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot integrate realtime GI cache");
        realtimePending={key,sequence+1,write,true};
        realtimePending.scene=s.scene;
        statistics.realtimeGiPasses=2;statistics.realtimeGiUpdatedProbes=batch;statistics.realtimeGiRays=batch*s.raysPerProbe;
    }
    void Background(RHIFrameEncoder& encoder, const PipelineCamera& camera, const SkyLightSettings& settings) {
        if (!settings.enabled || !settings.background) return;
        const ViewConstants view{glm::inverse(camera.projection*camera.view),glm::vec4(camera.position,1)};
        Upload(encoder,viewBuffer,&view,sizeof(view),0);
        Require(encoder.BindPipeline(skyBackground) && encoder.BindMesh(fullscreen) &&
                encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot draw sky background");
        ++statistics.skyPasses;
    }
    #include "Render/Private/Pipeline/skin_diffusion_pipeline.inl"
    #include "Render/Private/Pipeline/lumen_gi_pipeline.inl"
    #include "Render/Private/Pipeline/screen_probe_placement_pipeline.inl"
    #include "Render/Private/Pipeline/probe_sdf_pipeline.inl"
    #include "Render/Private/Pipeline/surface_cache_object_motion_pipeline.inl"
    #include "Render/Private/Pipeline/surface_cache_temporal_pipeline.inl"
    #include "Render/Private/Pipeline/surface_cache_spatial_pipeline.inl"
    #include "Render/Private/Pipeline/surface_cache_contact_pipeline.inl"
    #include "Render/Private/Pipeline/surface_cache_transport_pipeline.inl"
    #include "Render/Private/Pipeline/ue_lumen_high_short_range_ao_pipeline.inl"
    #include "Render/Private/Pipeline/ue_lumen_screen_probe_gather_pipeline.inl"
    #include "Render/Private/Pipeline/ue_lumen_radiance_cache_pipeline.inl"
    #include "Render/Private/Pipeline/ue_lumen_probe_importance_pipeline.inl"
    #include "Render/Private/Pipeline/ue_lumen_hit_scene_color_pipeline.inl"
    #include "Render/Private/Pipeline/ue_lumen_high_temporal_pipeline.inl"
    #include "Render/Private/Pipeline/surface_cache_native_probe_pipeline.inl"
    #include "Render/Private/Pipeline/surface_cache_native_contact_pipeline.inl"
    #include "Render/Private/Pipeline/ue_lumen_direct_lighting_schedule_pipeline.inl"
    #include "Render/Private/Pipeline/surface_cache_pipeline.inl"
    #include "Render/Private/Pipeline/area_light_shadows_pipeline.inl"
    void Record(RHIFrameEncoder& encoder, const RenderFrame& frame, const PipelineCamera& camera,
                const MaterialPipelineSettings& requested) {
        Validate(frame, camera, requested);
        auto settings=requested;
        placementPendingBaseSamplingSeed=settings.screenProbePlacement.trace.samplingSeed;
        temporalDepthOverride={};
        if(settings.surfaceCacheGi.enabled&&settings.surfaceCacheGi.useScreenProbes) {
            auto& p=settings.screenProbePlacement;auto& t=p.trace;
            p.enabled=true;t.enabled=true;t.cosineHemisphere=!settings.surfaceCacheGi.ueLumenHigh;t.sdf.enabled=true;
            t.sdf.visualMode=0;t.sdf.sliceView=0;t.sdf.scene=settings.surfaceCacheGi.scene;
            t.maxDistance=settings.surfaceCacheGi.maxDistance;t.normalBias=settings.surfaceCacheGi.rayBias;
            // Stable single-frame quadrature: no automatic temporal cycling.
            if(p.frameIndex==std::numeric_limits<std::uint32_t>::max())p.frameIndex=0;
        }
        if(settings.lumenGi.enabled) {
            const auto& lm=settings.lumenGi;auto& rt=settings.realtimeGi;
            rt.enabled=true;rt.scene=lm.scene->Geometry();rt.origin=lm.origin;rt.spacing=lm.spacing;rt.counts=lm.counts;
            rt.maxDistance=lm.maxDistance;rt.rayBias=lm.rayBias;rt.intensity=lm.intensity;rt.bounceFeedback=0;
            rt.reset=lm.reset;rt.historyWeight=.7f;
        }
        Require(encoder.IsRecording(), "Pipeline needs a recording frame encoder");
        statistics = {};
        const auto prepareHostBegin=std::chrono::steady_clock::now();
        areaLightShadowPending=areaLightShadowCompleted;
        auto skyAssets=settings.sky;if(settings.lumenGi.enabled&&settings.lumenGi.reflections)skyAssets.enabled=true;
        PrepareSky(skyAssets);
        PrepareProbes(settings.probes);
        PrepareRealtimeGi(settings.realtimeGi);realtimePending={};
        PrepareLumen(settings.lumenGi);lumenPending={};
        const auto& sc=settings.surfaceCacheGi;
        const bool cacheProbeGather=sc.enabled&&sc.useScreenProbes&&!sc.splitReceiverGather&&(sc.view==0||sc.view==2||sc.view==6||(sc.view==5&&sc.temporal.enabled)||sc.probeDiagnostics||sc.probeOriginDiagnostics);
        const bool doPlacement=settings.screenProbePlacement.enabled&&!sc.nativeScreenProbeTransport&&!sc.splitReceiverGather&&(!sc.enabled||cacheProbeGather||sc.probeDiagnostics);
        transportPending={};
        placementPending={};tracePending={};surfaceCachePending={};surfaceCacheTemporalPending={};surfaceCacheGatherCachePending={};
        if(doPlacement) {
            PrepareScreenProbePlacement(settings.screenProbePlacement);
            PrepareScreenProbeTrace(settings.screenProbePlacement);
            PrepareProbeSdfTrace(settings.screenProbePlacement,settings.surfaceCacheGi,settings.surfaceCacheGi.deferResourceUploads);
        }
        PrepareSurfaceCacheGi(settings.surfaceCacheGi,settings.screenProbePlacement);
        if(sc.enabled&&sc.nativeScreenProbeTransport){
            PrepareNativeScreenProbeTransport(sc,settings.screenProbePlacement);
            if(!sc.ueLumenHigh)PrepareNativeContactRiskAndMaskOnly(sc);
        }
        if(sc.ueLumenHigh)PrepareUeLumenHighTemporal(sc);
        else {PrepareSurfaceTransport(sc);PrepareSurfaceCacheTemporal(sc);}
        if(!sc.nativeScreenProbeTransport){PrepareSurfaceCacheSpatial(settings.surfaceCacheGi);PrepareSurfaceCacheContact(settings.surfaceCacheGi);}
        statistics.hostPrepareMilliseconds=std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-prepareHostBegin).count();
        Require(encoder.KeepAlive(assetsOwner) && encoder.KeepAlive(targetsOwner), "Cannot pin pipeline resources");
        if (skyOwner) Require(encoder.KeepAlive(skyOwner), "Cannot pin sky environment resources");
        if (probeOwner) Require(encoder.KeepAlive(probeOwner), "Cannot pin diffuse probe resources");
        if(settings.realtimeGi.enabled) Require(encoder.KeepAlive(realtimeOwner)&&encoder.KeepAlive(realtimeGeometryOwner),"Cannot pin realtime GI resources");
        if(settings.lumenGi.enabled)Require(encoder.KeepAlive(lumenProgramsOwner)&&encoder.KeepAlive(lumenSceneOwner)&&encoder.KeepAlive(lumenCacheOwner)&&encoder.KeepAlive(lumenDistanceOwner)&&encoder.KeepAlive(lumenViewOwner),"Cannot pin hybrid GI resources");
        if(doPlacement)Require(encoder.KeepAlive(placementProgramsOwner)&&encoder.KeepAlive(placementTargetsOwner),"Cannot pin screen probe placement resources");
        const bool sdfVolume=doPlacement&&settings.screenProbePlacement.trace.enabled&&
            settings.screenProbePlacement.trace.sdf.enabled&&settings.screenProbePlacement.trace.sdf.visualMode!=0;
        if(doPlacement&&settings.screenProbePlacement.trace.enabled&&!sdfVolume)
            Require(encoder.KeepAlive(traceProgramsOwner)&&encoder.KeepAlive(traceTargetsOwner),"Cannot pin screen probe trace resources");
        if(doPlacement&&settings.screenProbePlacement.trace.singleInvocation&&traceSingleProgramsOwner)
            Require(encoder.KeepAlive(traceSingleProgramsOwner),"Cannot pin single-invocation screen trace programs");
        if(doPlacement&&settings.screenProbePlacement.trace.singleInvocation&&sdfSingleProgramsOwner)
            Require(encoder.KeepAlive(sdfSingleProgramsOwner),"Cannot pin single-invocation SDF trace programs");
        if(doPlacement&&settings.screenProbePlacement.trace.singlePassOutputs&&traceOutputProgramsOwner)
            Require(encoder.KeepAlive(traceOutputProgramsOwner)&&encoder.KeepAlive(traceOutputTargetsOwner),"Cannot pin probe trace output resources");
        if(doPlacement&&settings.screenProbePlacement.trace.singlePassOutputs&&sdfOutputProgramsOwner)
            Require(encoder.KeepAlive(sdfOutputProgramsOwner),"Cannot pin SDF trace output programs");
        if(doPlacement&&settings.screenProbePlacement.trace.compactSdfRays&&sdfCompactOwner&&sdfCompactDataOwner)
            Require(encoder.KeepAlive(sdfCompactOwner)&&encoder.KeepAlive(sdfCompactDataOwner),"Cannot pin compact SDF trace resources");
        if(doPlacement&&settings.screenProbePlacement.trace.enabled&&settings.screenProbePlacement.trace.sdf.enabled)
            Require(encoder.KeepAlive(sdfProgramsOwner)&&(sdfNativeUploadsPruned||
                (encoder.KeepAlive(sdfMeshOwner)&&encoder.KeepAlive(sdfWorldOwner)))&&
                (sdfVolume||encoder.KeepAlive(sdfViewOwner))&&encoder.KeepAlive(settings.screenProbePlacement.trace.sdf.scene),"Cannot pin SDF resources");
        if(sc.enabled)Require(encoder.KeepAlive(surfaceCacheConstantsOwner)&&encoder.KeepAlive(surfaceCacheDataOwner)&&
            encoder.KeepAlive(surfaceCacheAsset)&&encoder.KeepAlive(surfaceCacheGeometryScene),"Cannot pin card Surface Cache data");
        if(sc.enabled)Require(encoder.KeepAlive(surfaceCacheProgramsOwner)&&encoder.KeepAlive(surfaceCacheViewOwner),"Cannot pin card Surface Cache GI resources");
        if(sc.enabled&&sc.hardwareCacheVisibility&&surfaceCacheNativeLightingProgramsOwner)
            Require(encoder.KeepAlive(surfaceCacheNativeLightingProgramsOwner),"Cannot pin native card visibility resources");
        if(cacheProbeGather)Require(encoder.KeepAlive(surfaceCacheProbeOwner),"Cannot pin probe integration targets");
        if(SurfaceCacheTemporalActive(sc)&&!sc.ueLumenHigh)Require(encoder.KeepAlive(surfaceCacheTemporalOwner)&&encoder.KeepAlive(surfaceCacheTemporalProgramsOwner),"Cannot pin GI temporal history resources");
        if(SurfaceCacheTemporalActive(sc)&&sc.temporal.reactiveShadows&&surfaceCacheShadowTemporalProgramsOwner)
            Require(encoder.KeepAlive(surfaceCacheShadowTemporalProgramsOwner),"Cannot pin GI shadow response programs");
        if(SurfaceCacheTemporalActive(sc)&&sc.temporal.objectMotionReprojection&&surfaceCacheObjectTemporalProgramsOwner)
            Require(encoder.KeepAlive(surfaceCacheObjectTemporalProgramsOwner),"Cannot pin GI object temporal programs");
        if(SurfaceCacheTemporalActive(sc)&&sc.temporal.disocclusionFilter&&surfaceCacheDisocclusionTemporalProgramsOwner)
            Require(encoder.KeepAlive(surfaceCacheDisocclusionTemporalProgramsOwner),"Cannot pin GI disocclusion temporal program");
        if(SurfaceCacheTemporalActive(sc)&&sc.temporal.disocclusionFilter&&sc.temporal.objectMotionReprojection&&surfaceCacheObjectDisocclusionTemporalProgramsOwner)
            Require(encoder.KeepAlive(surfaceCacheObjectDisocclusionTemporalProgramsOwner),"Cannot pin GI object disocclusion temporal program");
        if(SurfaceCacheSpatialActive(sc))Require(encoder.KeepAlive(surfaceCacheSpatialOwner)&&encoder.KeepAlive(surfaceCacheSpatialProgramsOwner),"Cannot pin GI spatial resources");
        if(SurfaceCacheSpatialActive(sc)&&sc.spatial.radianceOnlyFiltering&&surfaceCacheSpatialRadianceProgramsOwner)
            Require(encoder.KeepAlive(surfaceCacheSpatialRadianceProgramsOwner),"Cannot pin compact GI spatial programs");
        if(SurfaceCacheContactActive(sc))Require(encoder.KeepAlive(surfaceCacheContactOwner)&&encoder.KeepAlive(surfaceCacheContactProgramsOwner),"Cannot pin GI contact resources");
        if(SurfaceCacheContactActive(sc)&&sc.hardwareContactTracing&&surfaceCacheNativeContactProgramsOwner)
            Require(encoder.KeepAlive(surfaceCacheNativeContactProgramsOwner),"Cannot pin native GI contact resources");
        if(sc.hardwareOriginTracing) {
            const auto& originProbeOwner=sc.reuseOriginSoftwareCertification?surfaceCacheReusedNativeProbeOriginProgramsOwner:
                surfaceCacheNativeProbeOriginProgramsOwner;
            if(originProbeOwner)
                Require(encoder.KeepAlive(originProbeOwner),"Cannot pin native probe origin resources");
            const auto& originContactOwner=(sc.reuseOriginSoftwareCertification?surfaceCacheReusedNativeOriginContactProgramsOwner:
                surfaceCacheNativeOriginContactProgramsOwner)[sc.hardwareContactTracing?1:0];
            if(!sc.receiverSoftwareOrigins&&SurfaceCacheContactActive(sc)&&originContactOwner)
                Require(encoder.KeepAlive(originContactOwner),"Cannot pin native receiver origin resources");
        }
        if(surfaceCacheQueryProbeOriginActive)
            Require(encoder.KeepAlive(surfaceCacheQueryProbeOriginOwners[surfaceCacheQueryProbeOriginKey]),"Cannot pin reused probe-origin resources");
        if(SurfaceCacheContactActive(sc)&&(sc.reuseClosestTraversal||sc.fastGeometryAddressing||sc.contact.splitReceiverOrigins||sc.contact.reuseLightingLookup||sc.contact.fastLightingAddressing)) {
            const bool nativeContact=sc.hardwareContactTracing&&surfaceCacheHardwareSupported&&bool(surfaceCacheNativeContactProgramsOwner);
            const bool nativeOrigins=sc.hardwareOriginTracing&&!sc.receiverSoftwareOrigins&&surfaceCacheHardwareSupported;
            const auto& queryOwner=surfaceCacheQueryContactOwners[SurfaceCacheQueryContactKey(sc,nativeContact,nativeOrigins)];
            if(queryOwner)Require(encoder.KeepAlive(queryOwner),"Cannot pin reused contact-intersection resources");
        }
        if(SurfaceCacheFreshSamplingActive(sc))
            Require(encoder.KeepAlive(surfaceCacheFreshProgramsOwner)&&encoder.KeepAlive(surfaceCacheFreshOwner),"Cannot pin GI fresh-sampling resources");
        if(SurfaceCacheContactActive(sc)&&sc.contact.collectSamplingDiagnostics)
            Require(encoder.KeepAlive(surfaceCacheSamplingDiagnosticsOwner),"Cannot pin GI receiver sampling diagnostics");
        if(SurfaceCacheContactActive(sc)&&sc.contact.collectSamplingDiagnostics&&sc.contact.traceDiagnosticPage>0) {
            const bool nativeContact=sc.hardwareContactTracing&&surfaceCacheHardwareSupported&&bool(surfaceCacheNativeContactProgramsOwner);
            const bool nativeOrigins=sc.hardwareOriginTracing&&!sc.receiverSoftwareOrigins&&surfaceCacheHardwareSupported;
            const auto& diagnosticOwner=surfaceCacheReceiverTraceDiagnosticOwners[SurfaceCacheQueryContactKey(sc,nativeContact,nativeOrigins)][sc.contact.traceDiagnosticPage-1];
            if(diagnosticOwner)Require(encoder.KeepAlive(diagnosticOwner),"Cannot pin receiver trace branch diagnostics");
        }
        if(SurfaceCacheContactActive(sc)&&sc.contact.splitReceiverOrigins)
            Require(encoder.KeepAlive(surfaceCacheReceiverOriginsOwner),"Cannot pin separated receiver origins");
        if(SurfaceCacheContactActive(sc)&&sc.contact.collectTraceDiagnostics) {
            const bool nativeContact=sc.hardwareContactTracing&&surfaceCacheHardwareSupported&&bool(surfaceCacheNativeContactProgramsOwner);
            const bool nativeOrigins=sc.hardwareOriginTracing&&!sc.receiverSoftwareOrigins&&surfaceCacheHardwareSupported;
            Require(encoder.KeepAlive(surfaceCacheReceiverTraceMrtOwner)&&encoder.KeepAlive(surfaceCacheReceiverTraceSamplingOwner)&&
                encoder.KeepAlive(surfaceCacheReceiverTraceMrtOwners[SurfaceCacheQueryContactKey(sc,nativeContact,nativeOrigins)]),
                "Cannot pin same-frame receiver trace diagnostics");
        }
        const auto& rt=settings.realtimeGi;
        const DiffuseProbeRuntimeConstants probeRuntime{rt.enabled?glm::vec4(2,rt.intensity,rt.normalBias,rt.visibilityBias):
            glm::vec4(settings.probes.enabled?1:0,settings.probes.intensity,settings.probes.normalBias,settings.probes.visibilityBias)};
        Upload(encoder,probeRuntimeBuffer,&probeRuntime,sizeof(probeRuntime),DiffuseProbeSettingsBinding);
        Require(encoder.BindUniformBuffer(settings.probes.enabled?probeDataBuffer:emptyProbeBuffer,DiffuseProbeBinding),"Cannot bind diffuse probe volume");
        Require(encoder.BindUniformBuffer(settings.probes.enabled?probeVisibilityBuffer:emptyProbeVisibility,DiffuseProbeVisibilityBinding),"Cannot bind diffuse probe visibility");
        SkyLightConstants sky;
        if (settings.sky.enabled) sky = MakeSkyLightConstants(settings.sky, *skyEnvironment);
        Upload(encoder, skyBuffer, &sky, sizeof(sky), SkyLightBinding);
        Require(encoder.BindTexture(settings.sky.enabled ? skyAtlas : RenderResourceHandle<RHITextureSpec>{}, SkyReflectionTextureSlot) &&
                encoder.BindTexture(skyAssets.enabled ? skyBrdf : RenderResourceHandle<RHITextureSpec>{}, SkyBrdfTextureSlot), "Cannot bind sky environment");
        statistics.validDiffuseProbes=settings.probes.enabled?settings.probes.volume->ValidProbes():0;
        statistics.reflectionWidth = reflection.width; statistics.reflectionHeight = reflection.height;
        statistics.msaaSamples = samples;
        const bool useTaa = settings.temporal.antialiasing == AntialiasingMode::TAA;
        const auto contentHash = ContentHash(frame, settings);
        const bool cut = CameraCut(camera, settings)||settings.realtimeGi.reset||settings.lumenGi.reset||
            (settings.lumenGi.enabled&&!lumenCompleted.valid);
        const bool historyValid = useTaa && !cut && completed.taaValid &&
            completed.mode == settings.temporal.antialiasing && completed.contentHash == contentHash;
        const auto sequence = historyValid ? completed.sequence : 0;
        PipelineCamera renderCamera = camera;
        if (useTaa) renderCamera.projection = JitterProjection(camera.projection, TemporalJitter(sequence, width, height));
        const auto writeHistory = historyValid ? 1u - completed.image : 0u;
        auto temporal = MakeTemporalConstants(settings.temporal);
        temporal.currentInverseViewProjection = glm::inverse(renderCamera.projection * camera.view);
        temporal.currentUnjitteredViewProjection = camera.projection * camera.view;
        temporal.previousViewProjection = completed.jitteredVP;
        temporal.previousInverseViewProjection = glm::inverse(completed.jitteredVP);
        temporal.previousUnjitteredViewProjection = completed.camera.projection * completed.camera.view;
        temporal.temporal.w = historyValid ? 1.0f : 0.0f;
        temporal.screen.x = 1.0f / width; temporal.screen.y = 1.0f / height;
        temporal.motion.x *= settings.temporal.motionBlurShutterSeconds / std::max(settings.deltaSeconds, 0.0001f);
        temporal.motion.w = cut ? 0.0f : 1.0f;
        temporal.cameraPosition = glm::vec4(camera.position, 0);
        statistics.historyUsed = historyValid; statistics.cameraHistoryUsed = !cut;
        const auto& sceneTarget = samples > 1 ? sceneMsaa : scene;
        const auto& reflectionTarget = samples > 1 ? reflectionMsaa : reflection;
        auto light = settings.lighting;
        // Exposure and display transform belong exclusively to post-processing.
        light.ambientAndExposure.w = 1;
        Upload(encoder, lightingBuffer, &light, sizeof(light), Material::PbrPassBinding);
        const auto areaConstants = MakeAreaLightsConstants(settings.areaLights);
        Upload(encoder, areaBuffer, &areaConstants, sizeof(areaConstants), AreaLightsBinding);
        SurfacePassConstants surface;
        surface.view = camera.view; surface.projection = renderCamera.projection;
        surface.screenTime = glm::vec4(1.0f / width, 1.0f / height, settings.time, 0);
        Upload(encoder, surfaceBuffer, &surface, sizeof(surface), SurfacePassBinding);
        const bool hasTransparent = std::any_of(frame.items.begin(), frame.items.end(), [](const auto& item) {
            return item.EffectiveLayer() == RenderLayer::Transparent;
        });
        const bool hasStrands = !frame.strands.empty();
        temporal.reactive.x = (hasTransparent || hasStrands) ? 1.0f : 0.0f;

        SceneEffectsConstants effects{};
        effects.mainView = camera.view;
        effects.sunDirectionIntensity = glm::vec4(glm::normalize(settings.shadows.sunDirection), settings.shadows.sunIntensity);
        effects.sunColor = glm::vec4(settings.shadows.sunColor, 1);
        effects.effects = glm::vec4(settings.shadows.enabled ? 1 : 0, 0, 0, float(atlasResolution));
        effects.shadowParams = glm::vec4(settings.shadows.depthBias, settings.shadows.normalBias,
                                         settings.shadows.cascadeBlend, settings.reflection.strength);
        effects.screenAndAo = glm::vec4(1.0f / width, 1.0f / height, 0, float(settings.shadows.debugView));
        const float sourceFilterGuard = settings.shadows.filter == DirectionalShadowFilter::SourcePoisson8
            ? std::max(3.0f, std::max(settings.shadows.sourcePoissonRadius, settings.shadows.sourceSurfaceRadius) * float(atlasResolution / 2) + 1.5f) : 3.0f;
        const auto cascades = BuildCascades(camera.view, camera.projection, camera.nearPlane, camera.farPlane,
            settings.shadows.sunDirection, settings.shadows.distance, settings.shadows.splitLambda,
            atlasResolution, settings.shadows.casterPadding, settings.shadows.cascadeBlend, sourceFilterGuard);
        effects.lightViewProjection = cascades.lightViewProjection;
        effects.cascadeSplits = cascades.cascadeSplits;
        effects.cascadeWorldTexelSize = cascades.cascadeWorldTexelSize;
        effects.cascadeInverseDepthRange = cascades.cascadeInverseDepthRange;
        effects.shadowFilter = glm::vec4(settings.shadows.depthBiasTexels, settings.shadows.normalBiasTexels,
            settings.shadows.distanceFade, settings.shadows.receiverPlaneClampTexels);
        effects.sourceShadowFilter = glm::vec4(float(settings.shadows.filter), settings.shadows.sourcePoissonRadius,
            settings.shadows.sourcePoissonRotationRadians, settings.shadows.sourceSurfaceRadius);
        effects.strandShadowOpacity.y=settings.shadows.strandOpacityScale;
        Effects(encoder, effects);

        Require(encoder.BindTexture({}, 12) && encoder.BindTexture({}, 13) && encoder.BindTexture({}, 14), "Cannot reset pass textures");
        Require(encoder.BindTexture({}, 8) && encoder.BindTexture({}, 9) && encoder.BindTexture({}, 15), "Cannot reset advanced pass textures");
        const auto areaShadowHostBegin=std::chrono::steady_clock::now();
        RecordAreaLightShadows(encoder,frame,settings);
        statistics.hostAreaShadowRecordMilliseconds=std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-areaShadowHostBegin).count();
        if(settings.lumenGi.enabled)RecordLumenSurface(encoder,settings);
        // Opaque cache diagnostics reconstruct a traced receiver directly;
        // no transparency/reflection or irradiance fallback consumes probes.
        if(!settings.lumenGi.enabled||settings.lumenGi.lightingView==LumenLightingView::Material) {
            RecordRealtimeGi(encoder,settings,settings.lumenGi.enabled?lumenWorldProbes:RenderResourceHandle<PipelineSpec>{});
            if(settings.lumenGi.enabled)statistics.lumenGiPasses+=2;
        }
        // Clear even when disabled: diagnostics never expose uninitialized depth.
        Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::DirectionalShadows),"Cannot time directional shadows");
        BindTarget(encoder, shadows, RHICommand::ClearDepth);
        if(rt.enabled) Require(encoder.BindTexture(realtimeCache[realtimePending.image].color,RealtimeGiCacheTextureSlot),"Cannot bind updated realtime GI cache");
        if (settings.shadows.enabled && (settings.shadows.sunIntensity > 0 ||
                settings.shadows.debugView != ShadowDebugView::None)) {
            const auto tileSize = atlasResolution / 2;
            for (std::uint32_t cascade = 0; cascade < 4; ++cascade) {
                const auto x = static_cast<std::int32_t>((cascade % 2) * tileSize);
                const auto y = static_cast<std::int32_t>((cascade / 2) * tileSize);
                Require(encoder.SetViewport({x, y, tileSize, tileSize}) &&
                        encoder.SetScissor({true, x, y, tileSize, tileSize}), "Cannot record CSM tile");
                Depth(encoder, frame, cascades.lightViewProjection[cascade], true);
                ++statistics.shadowPasses;
            }
        }
        bool hairCastsSun=false;
        for(const auto& item:frame.strands)hairCastsSun|=item.geometry&&!item.guides&&item.material.castDirectionalShadows;
        const bool deepOpacityShadows=hairCastsSun&&settings.shadows.enabled&&settings.shadows.strandCoverage==StrandShadowCoverage::DeepOpacity;
        auto surfaceShadowAtlas=shadows.depth;
        if(deepOpacityShadows) {
            const auto opacity=strandOpacityShadowPass.Record(encoder,frame.strands,cascades.lightViewProjection,
                -glm::normalize(settings.shadows.sunDirection),atlasResolution,shadows.depth);
            surfaceShadowAtlas=opacity.combinedAtlas;
            effects.strandShadowOpacity.x=1;
            effects.strandShadowDepthSpan=opacity.depthSpans;
            statistics.strandShadowDraws=opacity.nearestDraws+opacity.tauDraws;
            statistics.strandOpacityShadowPasses=8+opacity.packPasses;
            statistics.strandOpacityShadowResolution=atlasResolution/2;
            statistics.strandCurveShadowUploads=opacity.curveStatistics.uploads;
            statistics.strandCurveShadowPieces=opacity.curveStatistics.curvePieces;
            Effects(encoder,effects);
        } else if(hairCastsSun&&settings.shadows.enabled) {
            if(!opaqueStrandShadowsOwner){PipelineDetail::ResourceBuilder builder(device);
                opaqueStrandShadows=MakeTarget(builder,atlasResolution,atlasResolution,false,true);opaqueStrandShadowsOwner=builder.Finish();}
            Require(encoder.KeepAlive(opaqueStrandShadowsOwner),"Cannot retain opaque-only strand shadow atlas");
            PostProcessConstants shadowCopy;
            Require(encoder.SetScissor({false,0,0,atlasResolution,atlasResolution}),"Cannot reset strand shadow scissor");
            Fullscreen(encoder,opaqueStrandShadows,depthCopy,{},{},shadows.depth,shadowCopy);
            BindTarget(encoder,shadows);
            const auto tileSize=atlasResolution/2;
            for(std::uint32_t cascade=0;cascade<4;++cascade){
                const auto x=std::int32_t(cascade%2*tileSize),y=std::int32_t(cascade/2*tileSize);
                Require(encoder.SetViewport({x,y,tileSize,tileSize})&&encoder.SetScissor({true,x,y,tileSize,tileSize}),"Cannot set strand CSM tile");
                statistics.strandShadowDraws+=strandShadowPass.Record(encoder,frame.strands,cascades.lightViewProjection[cascade],
                    -glm::normalize(settings.shadows.sunDirection),tileSize);
                const auto curveShadow=strandShadowPass.CurveStatistics();
                statistics.strandCurveShadowUploads+=curveShadow.uploads;
                statistics.strandCurveShadowPieces+=curveShadow.curvePieces;
            }
        }
        Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::DirectionalShadows),"Cannot end directional shadow timing");
        Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::PlanarReflection),"Cannot time planar reflection");
        const bool mirrorEnabled=settings.reflection.enabled&&
            (!settings.reflection.receiverBounds||ViewFrustum(camera.projection*camera.view).Intersects(
                settings.reflection.receiverBounds->minimum,settings.reflection.receiverBounds->maximum));
        // Switching away from the shadow attachment must precede binding its
        // depth texture. The culled path initializes only the resolved mirror.
        BindTarget(encoder,mirrorEnabled?reflectionTarget:reflection,RHICommand::ClearColor|RHICommand::ClearDepth,
                   glm::vec4(0.006f,0.009f,0.015f,1));
        Require(encoder.BindTexture(surfaceShadowAtlas, 12), "Cannot bind CSM atlas");
        if (mirrorEnabled) {
            const auto reflected = ReflectCamera(camera.view, renderCamera.projection, settings.reflection.plane);
            effects.reflectionViewProjection = reflected.viewProjection;
            effects.clipPlane = reflected.clipPlane;
            effects.effects.z = 1;
            Effects(encoder, effects);
            PipelineCamera reflectedCamera = camera;
            reflectedCamera.view = reflected.view; reflectedCamera.projection = reflected.projection;
            reflectedCamera.position = reflected.position;
            surface.view = reflectedCamera.view; surface.projection = reflectedCamera.projection;
            surface.screenTime.x = 1.0f / reflectionTarget.width; surface.screenTime.y = 1.0f / reflectionTarget.height;
            Upload(encoder, surfaceBuffer, &surface, sizeof(surface), SurfacePassBinding);
            Background(encoder, reflectedCamera, settings.sky);
            Scene(encoder, frame, reflectedCamera, true, false, 0);
            if (hasTransparent) {
                Resolve(encoder, reflectionTarget, reflection, true, true);
                PostProcessConstants snapshotPost;
                Fullscreen(encoder, reflectionSnapshot, copy, reflection.color, {}, {}, snapshotPost);
                Fullscreen(encoder, reflectionSnapshotDepth, depthCopy, {}, {}, reflection.depth, snapshotPost);
                BindTarget(encoder, reflectionTarget);
                surface.screenTime.w = 1;
                Upload(encoder, surfaceBuffer, &surface, sizeof(surface), SurfacePassBinding);
                Require(encoder.BindTexture(reflectionSnapshot.color, 8) && encoder.BindTexture(reflectionSnapshot.depth, 9), "Cannot bind reflection-view refraction snapshot");
                Scene(encoder, frame, reflectedCamera, true, false, 1);
            }
            ++statistics.reflectionPasses;
            effects.effects.z = 0;
            effects.effects.y = 1;
            Resolve(encoder, reflectionTarget, reflection, true, false);
        }
        Effects(encoder, effects); // Restore clip state before the main-view depth prepass.
        Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::PlanarReflection),"Cannot end planar reflection timing");
        surface.view = camera.view; surface.projection = renderCamera.projection;
        surface.screenTime = glm::vec4(1.0f / width, 1.0f / height, settings.time, 0);
        Upload(encoder, surfaceBuffer, &surface, sizeof(surface), SurfacePassBinding);

        PostProcessConstants post{};
        post.projection = renderCamera.projection; post.inverseProjection = glm::inverse(renderCamera.projection);
        post.bloom = glm::vec4(settings.post.bloomThreshold, settings.post.bloomStrength, 0, 0);
        post.ssao = glm::vec4(settings.post.ssaoRadius, settings.post.ssaoBias, settings.post.ssaoPower, settings.post.ssaoEnabled ? 1 : 0);
        post.output = glm::vec4(settings.post.exposure, settings.post.gamma, float(settings.post.toneMap), float(settings.post.filter));
        post.screen = glm::vec4(1.0f / width, 1.0f / height, 0, settings.post.bloomEnabled ? 1 : 0);
        post.grading = glm::vec4(settings.post.saturation, settings.post.contrast, settings.post.vignette, settings.post.sharpenStrength);
        post.misc = glm::vec4(camera.nearPlane, camera.farPlane, settings.time, settings.post.ssaoEnabled ? 1 : 0);
        post.bloomStability = glm::vec4(settings.post.bloomSoftKnee, settings.post.bloomFireflyClamp,
                                       settings.post.bloomScatter, settings.post.bloomRadius);
        const bool shadowDebug = settings.shadows.debugView != ShadowDebugView::None;
        if (shadowDebug) {
            // Show linear visibility/cascade colors directly, without an output
            // transform changing their diagnostic meaning.
            post.output = glm::vec4(1, 1, float(ToneMapMode::None), float(PostFilter::None));
            post.screen.w = 0; post.grading = glm::vec4(1, 1, 0, 0);
        }
        if (settings.post.ssaoEnabled) {
            BindTarget(encoder, sceneTarget, RHICommand::ClearDepth);
            Depth(encoder, frame, renderCamera.projection * camera.view, false);
            Resolve(encoder, sceneTarget, scene, false, true);
            ++statistics.depthPasses;
            Fullscreen(encoder, aoRaw, ssao, {}, {}, scene.depth, post);
            auto blurPost = post;
            blurPost.screen.x = 1.0f / aoRaw.width; blurPost.screen.y = 1.0f / aoRaw.height;
            Fullscreen(encoder, aoBlurred, ssaoBlur, aoRaw.color, {}, scene.depth, blurPost);
            effects.screenAndAo.z = 1;
        } else {
            BindTarget(encoder, aoBlurred, RHICommand::ClearColor, glm::vec4(1));
        }
        if(settings.lumenGi.enabled&&!shadowDebug) {
            // GI/reflection grids follow the unjittered view. TAA jitter belongs
            // to the main raster grid, not sparse quadrature/normal selection.
            RecordLumenGather(encoder,frame,camera,settings,effects,surface,post,cut,contentHash);
        } else if (settings.indirect.enabled && !shadowDebug) {
            // Independent single-sample source grids; even objects without GI
            // color variants can occlude rays through their depth variant.
            auto captureEffects = effects; captureEffects.effects.y = 0; captureEffects.screenAndAo.z = 0;
            Effects(encoder, captureEffects);
            BindTarget(encoder, diffuseCapture, RHICommand::ClearColor | RHICommand::ClearDepth);
            Depth(encoder, frame, renderCamera.projection * camera.view, false);
            CaptureSurface(encoder, frame, renderCamera, false);
            BindTarget(encoder, normalCapture, RHICommand::ClearColor);
            CaptureSurface(encoder, frame, renderCamera, true);
            auto gi = MakeIndirectLightingConstants(settings.indirect, renderCamera.projection, camera.view);
            Upload(encoder, indirectBuffer, &gi, sizeof(gi), IndirectLightingBinding);
            Fullscreen(encoder, indirectPositions, indirectGeometry, {}, {}, diffuseCapture.depth, post);
            Fullscreen(encoder, indirectHalf, indirectGather, diffuseCapture.color, normalCapture.color, diffuseCapture.depth, post, indirectPositions.color);
            // Filter irradiance separately from scene color. Expanding the
            // geometric kernel removes sparse-ray patterns without blurring
            // material textures, silhouettes or the final HDR image.
            auto irradiance = indirectHalf.color;
            for (std::uint32_t pass = 0; pass < IndirectLightingDenoisePasses; ++pass) {
                gi.options.w = 1 << pass;
                Upload(encoder, indirectBuffer, &gi, sizeof(gi), IndirectLightingBinding);
                const auto& destination = indirectWork[pass % indirectWork.size()];
                Fullscreen(encoder, destination, indirectDenoise, irradiance, normalCapture.color, diffuseCapture.depth, post, indirectPositions.color);
                irradiance = destination.color;
            }
            Fullscreen(encoder, indirectFull, indirectUpsample, irradiance, normalCapture.color, diffuseCapture.depth, post, indirectPositions.color);
            statistics.indirectPasses = 6 + IndirectLightingDenoisePasses;
            surface.options.x = 1;
        } else {
            BindTarget(encoder, indirectFull, RHICommand::ClearColor);
            BindTarget(encoder, diffuseCapture, RHICommand::ClearColor | RHICommand::ClearDepth);
            BindTarget(encoder, normalCapture, RHICommand::ClearColor);
        }
        if(settings.lumenGi.enabled)surface.screenJitter=glm::vec4(useTaa?-TemporalJitter(sequence,width,height):glm::vec2(0),float(settings.lumenGi.lightingView),0);
        Upload(encoder, surfaceBuffer, &surface, sizeof(surface), SurfacePassBinding);
        Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::MainOpaque),"Cannot time main opaque rendering");
        if(sc.cachedDirectDiffuse){auto spec=settings.areaLights;spec.specularOnly=true;spec.samplesPerAxis=4;
            const auto constants=MakeAreaLightsConstants(spec);Upload(encoder,areaBuffer,&constants,sizeof(constants),AreaLightsBinding);}
        BindTarget(encoder, sceneTarget, RHICommand::ClearColor | RHICommand::ClearDepth, glm::vec4(0.006f, 0.009f, 0.015f, 1));
        Require(encoder.BindTexture(reflection.color, 13) && encoder.BindTexture(aoBlurred.color, 14), "Cannot bind reflection/SSAO");
        Require(encoder.BindTexture(settings.lumenGi.enabled&&!shadowDebug?indirectFull.color:
                rt.enabled?realtimeCache[realtimePending.image].color:indirectFull.color, 15), "Cannot bind indirect irradiance");
        if(surface.options.y>.5)Require(encoder.BindTexture(lumenReflectionFull.color,8)&&encoder.BindTexture(lumenReflectionFullGrazing.color,9),"Cannot bind hybrid scene reflections");
        Effects(encoder, effects);
        Background(encoder, renderCamera, settings.sky);
        Scene(encoder, frame, renderCamera, false, false, 0);
        ++statistics.scenePasses;
        Resolve(encoder, sceneTarget, scene, true, true);
        if(!shadowDebug)RecordSkinDiffusion(encoder,frame,renderCamera,settings.skinDiffusion,surface,sceneTarget);
        if(!shadowDebug) {
            PipelineDetail::EyeShellPassInputs inputs;
            inputs.color=scene.color;inputs.depth=scene.depth;
            inputs.view=renderCamera.view;inputs.projection=renderCamera.projection;
            inputs.cameraPosition=renderCamera.position;inputs.nearPlane=renderCamera.nearPlane;
            inputs.lighting=light;inputs.sunDirection=settings.shadows.sunDirection;
            inputs.sunColor=settings.shadows.sunColor;inputs.sunIntensity=settings.shadows.sunIntensity;
            const auto result=eyeShellPass.Record(encoder,frame.items,
                {sceneTarget.framebuffer,sceneTarget.color,sceneTarget.depth,width,height,samples},inputs,settings.eyeShell);
            statistics.eyeShellPasses=result.passes;statistics.eyeShellDraws=result.draws;
            if(result.passes)Resolve(encoder,sceneTarget,scene,true,false);
            Upload(encoder,lightingBuffer,&light,sizeof(light),3);
            Effects(encoder,effects);
        }
        if (hasTransparent || hasStrands) {
            // Copy to independent attachments at either sample count. Reading
            // the active scene color/depth would be undefined framebuffer feedback.
            Fullscreen(encoder, opaqueSnapshot, copy, scene.color, {}, {}, post);
            Fullscreen(encoder, opaqueSnapshotDepth, depthCopy, {}, {}, scene.depth, post);
            BindTarget(encoder, sceneTarget);
            surface.screenTime.w = 1;
            if(settings.lumenGi.enabled) {
                surface.options.y=0;
                const DiffuseProbeRuntimeConstants runtime{glm::vec4(2,rt.intensity,rt.normalBias,rt.visibilityBias)};
                Upload(encoder,probeRuntimeBuffer,&runtime,sizeof(runtime),DiffuseProbeSettingsBinding);
                Require(encoder.BindTexture(realtimeCache[realtimePending.image].color,15),"Cannot bind translucent world radiance");
            }
            Upload(encoder, surfaceBuffer, &surface, sizeof(surface), SurfacePassBinding);
            Require(encoder.BindTexture(opaqueSnapshot.color, 8) && encoder.BindTexture(opaqueSnapshot.depth, 9), "Cannot bind refraction snapshot");
            Scene(encoder, frame, renderCamera, false, false, 1);
            ++statistics.transparentPasses;
            Resolve(encoder, sceneTarget, scene, true, false);
        }

        Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::MainOpaque),"Cannot end main opaque timing");
        if(sc.cachedDirectDiffuse)Upload(encoder,areaBuffer,&areaConstants,sizeof(areaConstants),AreaLightsBinding);
        if(settings.surfaceCacheGi.enabled)
            RecordSurfaceCacheGi(encoder,frame,renderCamera,settings,effects,surface,post,cut);
        if(doPlacement&&!(settings.surfaceCacheGi.enabled&&settings.surfaceCacheGi.useScreenProbes))
            RecordScreenProbePlacement(encoder,frame,renderCamera,settings.screenProbePlacement,effects,surface,post);
        Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::PostAndOutput),"Cannot time post and output");
        const bool separateHair=hasStrands&&useTaa&&samples==1&&settings.strandTemporalDepth&&settings.strandMotionVectors;
        std::optional<PipelineDetail::StrandPassView> shutterView;
        if(separateHair){PrepareHairTemporal();Require(encoder.KeepAlive(hairTemporalOwner),"Cannot pin separate hair temporal resources");
            Fullscreen(encoder,preHairSnapshot,copy,scene.color,{},{},post);}
        if(hasStrands) {
            PipelineDetail::StrandPassView strandView;
            strandView.viewProjection=renderCamera.projection*renderCamera.view;
            strandView.cameraPosition=renderCamera.position;
            strandView.worldUnitsPerMetre=settings.skinDiffusion.worldUnitScale;
            strandView.sunDirection=settings.shadows.sunDirection;
            strandView.sunColor=settings.shadows.sunColor;
            strandView.sunIntensity=settings.shadows.sunIntensity;
            strandView.lighting=light;
            strandView.indirect=settings.strandIndirect;
            strandView.sourceDirect=settings.strandSourceDirect;
            strandView.frontLayerTransparency=settings.strandFrontLayerTransparency;
            strandView.frontDepthGroups=settings.strandFrontDepthGroups;
            strandView.pairedLayerTransparency=settings.strandPairedLayerTransparency;
            strandView.nativeOit=settings.strandNativeOit;
            strandView.nativeOitLayers=settings.strandNativeOitLayers;
            strandView.outputTemporalData=settings.strandTemporalDepth;
            strandView.outputMotionData=separateHair;
            strandView.motionHistoryValid=historyValid;
            strandView.previousViewProjection=temporal.previousViewProjection;
            strandView.currentUnjitteredViewProjection=temporal.currentUnjitteredViewProjection;
            strandView.previousUnjitteredViewProjection=temporal.previousUnjitteredViewProjection;
            strandView.previousCameraPosition=completed.camera.position;
            strandView.depthWriteThreshold=settings.strandDepthWriteThreshold;
            strandView.cameraForward=-glm::vec3(glm::inverse(camera.view)[2]);
            strandView.opaqueContentRevision=PipelineDetail::StrandOpaqueDepthContentHash(frame,opaqueDepthEpoch);
            strandView.blockerStableViewValid=true;
            strandView.blockerUnjitteredViewProjection=temporal.currentUnjitteredViewProjection;
            strandView.debugMode=settings.strandDebugMode;
            strandView.opaqueDepth=opaqueSnapshot.depth;
            strandView.effects=effects;
            strandView.shadowAtlas=hairCastsSun&&settings.shadows.enabled&&!deepOpacityShadows?opaqueStrandShadows.depth:shadows.depth;
            if(settings.sky.enabled) strandView.sky=MakeSkyLightConstants(settings.sky,*skyEnvironment);
            shutterView=strandView;
            strandPass.Record(encoder,frame.strands,
                {sceneTarget.framebuffer,sceneTarget.color,sceneTarget.depth,width,height,samples},strandView);
            const auto& volume=strandPass.VolumeStatistics();
            statistics.strandVolumePasses=volume.passes;statistics.strandBlockerPasses=volume.blockerPasses;statistics.strandVolumeResolution=volume.resolution;
            statistics.strandNativeDsInputSegments=volume.nativeInputSegments;statistics.strandNativeDsInjectedSegments=volume.nativeInjectedSegments;
            statistics.strandNativeDsInjectionFraction=volume.nativeInjectionFraction;statistics.strandNativeDsRasterWidthScale=volume.nativeRasterWidthScale;
            statistics.strandNativeDsAverageWidth=volume.nativeAverageWidth;
            statistics.strandVolumeUpdated=volume.updated!=0;statistics.strandVolumeVoxelSize=volume.effectiveVoxelSize;statistics.strandVolumeBiasMetres=volume.effectiveBiasMetres;
            Effects(encoder,effects); // Restore binding four for later scene overlays.
            // The private motion-object UBO shares slot eight with area lights.
            Upload(encoder,areaBuffer,&areaConstants,sizeof(areaConstants),AreaLightsBinding);
            Resolve(encoder,sceneTarget,scene,true,false);
            statistics.strandPasses=strandPass.PassCount();
            const auto segmentShading=strandPass.SegmentShadingStatistics();
            statistics.strandShadingPasses=segmentShading.passes;
            statistics.strandShadingComputePasses=segmentShading.computePasses;
            statistics.strandShadingRasterPasses=segmentShading.rasterPasses;
            statistics.strandShadingEvaluations=segmentShading.points;
            statistics.strandShadingOutputPoints=segmentShading.outputPoints;
            const auto curve=strandPass.CurveStatistics();
            statistics.strandCurveOriginalSegments=curve.originalSegments;
            statistics.strandCurveVisibleSegments=curve.visibleSegments;
            statistics.strandCurvePieces=curve.curvePieces;
            statistics.strandCurveUploads=curve.uploads;
            statistics.strandFrontDepthGroups=strandPass.FrontDepthGroups();
            statistics.strandTransparencyRasterPasses=strandPass.TransparencyRasterPassCount();
            statistics.strandPairedTransparencyActive=strandPass.PairedLayerTransparencyActive();
            statistics.strandNativeOitActive=strandPass.NativeOitActive();
            statistics.strandNativeOitComputePasses=strandPass.NativeOitComputePasses();
            statistics.strandNativeOitLayers=strandPass.NativeOitLayers();
            statistics.strandNativeOitBytes=strandPass.NativeOitBytes();
            statistics.strandTemporalPasses=strandPass.TemporalPassCount();
            statistics.strandMotionPasses=strandPass.MotionPassCount();
            statistics.strandMotionActive=strandPass.TemporalTextures().motion.IsValid();
            statistics.strandTemporalMultisampleFallback=strandPass.TemporalDataMultisampleFallback();
            temporalDepthOverride=strandPass.TemporalTextures().compositeDepth;
            statistics.strandTemporalDepthActive=temporalDepthOverride.IsValid();
            statistics.strandFrontLayerTransparencyActive=strandPass.FrontLayerTransparencyActive();
            statistics.strandTransparencyMultisampleFallback=strandPass.MultisampleTransparencyFallback();
            for(const auto& item:frame.strands) statistics.strandSegments+=item.geometry->IndexCount()/6;
        }
        auto hdrColor = scene.color;
        if(separateHair){
            const auto hair=strandPass.TemporalTextures();
            Require(hair.resolvedColor.IsValid()&&hair.motion.IsValid(),"Missing resolved hair layer/motion");
            // Accumulate the complete pre-hair background on opaque depth. Hair
            // alpha/color have their own history and cannot drag the face color.
            temporalDepthOverride={};auto bodyTemporal=temporal;bodyTemporal.reactive.x=hasTransparent?1.f:0.f;
            TemporalPass(encoder,historyColor[writeHistory],taa,preHairSnapshot.color,
                historyValid?historyColor[completed.image].color:RenderResourceHandle<RHITextureSpec>{},
                historyValid?historyDepth[completed.image].depth:RenderResourceHandle<RHITextureSpec>{},bodyTemporal);
            TemporalPass(encoder,historyDepth[writeHistory],depthCopy,{},{},{},bodyTemporal);
            auto hairConstants=temporal;const bool valid=historyValid&&completed.hairTaaValid;
            hairConstants.temporal.w=valid?1.f:0.f;
            hairConstants.reactive.z=settings.temporal.hairCoverageHistoryRejection?1.f:0.f;
            hairConstants.reactive.w=settings.temporal.hairFastMotionPixels;
            HairTemporalPass(encoder,hair,writeHistory,valid,hairConstants);
            Fullscreen(encoder,hairHistoryDepth[writeHistory],depthCopy,{},{},hair.nearestDepth,post);
            auto backgroundColor=historyColor[writeHistory].color;
            if(settings.temporal.motionBlurEnabled&&!shadowDebug){
                auto blurConstants=bodyTemporal;blurConstants.cameraPosition.w=1.f;
                TemporalPass(encoder,blurred,motionBlur,backgroundColor,{},{},blurConstants);
                backgroundColor=blurred.color;++statistics.motionBlurPasses;
            }
            auto hairColor=hairHistoryColor[writeHistory].color;
            if(settings.temporal.hairMotionBlurEnabled&&valid&&shutterView&&settings.strandTimeSeconds&&completed.strandTimeSeconds){
                const double elapsed=*settings.strandTimeSeconds-*completed.strandTimeSeconds;
                const bool provenance=std::all_of(frame.strands.begin(),frame.strands.end(),[](const auto& item){return item.geometry&&item.previousTransformValid&&item.geometry->PreviousInputMatched();});
                const bool physicalMotion=!SameMatrix(temporal.currentUnjitteredViewProjection,temporal.previousUnjitteredViewProjection)||
                    std::any_of(frame.strands.begin(),frame.strands.end(),[](const auto& item){return item.geometry&&
                        (item.geometry->HasDeformationMotion()||!SameMatrix(item.model,item.previousModel));});
                // Current jitter is not motion. Keep sharp temporal AA and avoid
                // four redundant geometry samples when all physical inputs stop.
                if(provenance&&physicalMotion&&std::isfinite(elapsed)&&elapsed>0&&elapsed<=.25){
                    const float fraction=float(std::clamp(double(settings.temporal.hairMotionBlurShutterSeconds)/elapsed,0.,1.));
                    strandShutterPass.Record(encoder,frame.strands,{sceneTarget.framebuffer,sceneTarget.color,sceneTarget.depth,width,height,1},*shutterView,fraction,true);
                    hairColor=strandShutterPass.PremultipliedColor();
                    const auto& shutter=strandShutterPass.Statistics();
                    statistics.strandShutterSamples=shutter.samples;statistics.strandShutterPasses=shutter.passes;
                    statistics.strandShutterRasterPasses=shutter.rasterPasses;statistics.strandShutterVolumePasses=shutter.volumePasses;
                }
            }
            Fullscreen(encoder,filtered,hairTemporalComposite,backgroundColor,hairColor,{},post);
            hdrColor=filtered.color;temporalDepthOverride=hair.compositeDepth;
            statistics.strandHairTemporalPasses=4;statistics.strandHairHistoryUsed=valid;
            statistics.temporalPasses+=2;
        }else if (useTaa) {
            TemporalPass(encoder, historyColor[writeHistory], taa, hdrColor,
                historyValid ? historyColor[completed.image].color : RenderResourceHandle<RHITextureSpec>{},
                historyValid ? historyDepth[completed.image].depth : RenderResourceHandle<RHITextureSpec>{}, temporal);
            // The destination is depth-only; CompareAlways + gl_FragDepth keeps
            // exact Depth32F history for disocclusion rejection.
            TemporalPass(encoder, historyDepth[writeHistory], depthCopy, {}, {}, {}, temporal);
            hdrColor = historyColor[writeHistory].color;
            ++statistics.temporalPasses;
        }
        if (settings.temporal.motionBlurEnabled && !shadowDebug && !separateHair) {
            auto blurConstants = temporal;
            blurConstants.cameraPosition.w = useTaa ? 1.0f : 0.0f;
            TemporalPass(encoder, blurred, motionBlur, hdrColor, {}, {}, blurConstants);
            hdrColor = blurred.color;
            ++statistics.motionBlurPasses;
        }

        lastBloom = {};
        if (settings.post.bloomEnabled && !shadowDebug) {
            auto levels = std::min<std::uint32_t>(settings.post.bloomLevels, static_cast<std::uint32_t>(bloomDown.size()));
            for (std::uint32_t i = 1; i < levels; ++i) {
                if (bloomDown[i - 1].width == 1 && bloomDown[i - 1].height == 1) { levels = i; break; }
            }
            Fullscreen(encoder, bloomDown[0], extract, hdrColor, {}, {}, post);
            for (std::uint32_t level = 1; level < levels; ++level)
                Fullscreen(encoder, bloomDown[level], downsample, bloomDown[level - 1].color, {}, {}, post);
            lastBloom = bloomDown[levels - 1].color;
            for (int level = static_cast<int>(levels) - 2; level >= 0; --level) {
                auto upPost = post;
                upPost.screen.x = 1.0f / bloomDown[level + 1].width;
                upPost.screen.y = 1.0f / bloomDown[level + 1].height;
                Fullscreen(encoder, bloomWork[level], upsample, lastBloom, bloomDown[level].color, {}, upPost);
                lastBloom = bloomWork[level].color;
            }
        }
        if(settings.sourceDisplay.enabled&&!shadowDebug){
            BindTarget(encoder,display);Upload(encoder,postBuffer,&post,sizeof(post),PostProcessBinding);
            sourceDisplayPass.Record(encoder,hdrColor,lastBloom,post,settings.sourceDisplay);++statistics.postPasses;
        }else Fullscreen(encoder, display, composite, hdrColor, lastBloom, {}, post);
        Fullscreen(encoder, filtered, filter, display.color, {}, {}, post);
        Target screenTarget;
        screenTarget.width = settings.outputSize.x ? settings.outputSize.x : width;
        screenTarget.height = settings.outputSize.y ? settings.outputSize.y : height;
        const bool resample = screenTarget.width != width || screenTarget.height != height;
        displayColor=filtered.color;
        if (settings.temporal.antialiasing == AntialiasingMode::FXAA) {
            // Keep the complete display transform in an owned RHI texture so
            // editor UI bridges can borrow it without copying the native window.
            TemporalPass(encoder,display,fxaa,filtered.color,{},{},temporal);
            displayColor=display.color;
        }
        Fullscreen(encoder,screenTarget,resample?spatialUpsample:copy,displayColor,{},{},post);
        if(std::any_of(frame.items.begin(),frame.items.end(),[](const auto& item){return item.EffectiveLayer()==RenderLayer::Overlay;})){
            // Hair and display passes reuse private UBO/texture slots. Restore
            // the PBR contract only when a following overlay actually uses it.
            surface.view=camera.view;surface.projection=camera.projection;
            Upload(encoder,surfaceBuffer,&surface,sizeof(surface),SurfacePassBinding);
            Upload(encoder,areaBuffer,&areaConstants,sizeof(areaConstants),AreaLightsBinding);
            Upload(encoder,lightingBuffer,&light,sizeof(light),Material::PbrPassBinding);
            Effects(encoder,effects);
            Require(encoder.BindTexture(opaqueSnapshot.color,8)&&encoder.BindTexture(opaqueSnapshot.depth,9)&&
                encoder.BindTexture(settings.sky.enabled?skyAtlas:RenderResourceHandle<RHITextureSpec>{},SkyReflectionTextureSlot)&&
                encoder.BindTexture(skyAssets.enabled?skyBrdf:RenderResourceHandle<RHITextureSpec>{},SkyBrdfTextureSlot)&&
                encoder.BindTexture(surfaceShadowAtlas,12)&&encoder.BindTexture(reflection.color,13)&&encoder.BindTexture(aoBlurred.color,14)&&
                encoder.BindTexture(settings.lumenGi.enabled&&!shadowDebug?indirectFull.color:
                    rt.enabled?realtimeCache[realtimePending.image].color:indirectFull.color,15),"Cannot restore overlay PBR textures");
        }
        Scene(encoder, frame, camera, false, true); // Overlay bypasses HDR/post effects.
        Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::PostAndOutput),"Cannot end post and output timing");
        pending = {};
        pending.camera = camera; pending.jitteredVP = renderCamera.projection * camera.view;
        pending.contentHash = contentHash; pending.sequence = sequence + 1;
        pending.image = writeHistory; pending.mode = settings.temporal.antialiasing;
        pending.valid = true; pending.taaValid = useTaa;pending.hairTaaValid=separateHair;
        pending.strandTimeSeconds=settings.strandTimeSeconds;
        pendingToken = ++nextToken;
    }
};

MaterialRenderPipeline::MaterialRenderPipeline(RHIDevice& device) : impl_(std::make_unique<Impl>(device)) {}
MaterialRenderPipeline::~MaterialRenderPipeline() { Shutdown(); }
bool MaterialRenderPipeline::Initialize() {
    impl_->error.clear();
    try { impl_->Initialize(); return true; }
    catch (const std::exception& error) { impl_->error = error.what(); return false; }
}
bool MaterialRenderPipeline::Resize(std::uint32_t width, std::uint32_t height, std::uint32_t resolution,
                                    std::uint32_t samples, float scale) {
    impl_->error.clear();
    try { impl_->Resize(width, height, resolution, samples, scale); return true; }
    catch (const std::exception& error) { impl_->error = error.what(); return false; }
}
bool MaterialRenderPipeline::Record(RHIFrameEncoder& encoder, const RenderFrame& frame, const PipelineCamera& camera,
                                    const MaterialPipelineSettings& settings) {
    impl_->error.clear();
    try { impl_->Record(encoder, frame, camera, settings); return true; }
    catch (const std::exception& error) { impl_->strandPass.InvalidateVolume();impl_->strandShutterPass.InvalidateVolume(); impl_->CommitUeLumenDirectLightingSchedule(false); impl_->CommitUeLumenRadianceCache(false); impl_->CommitUeLumenProbeImportance(false); impl_->CommitUeLumenHitSceneColor(false); impl_->CompleteUeLumenHighShortRangeAO(false); impl_->error = error.what(); return false; }
}
void MaterialRenderPipeline::Shutdown() {
    impl_->eyeShellPass.Shutdown();
    impl_->hairTemporalOwner.reset();impl_->preHairSnapshot={};impl_->hairHistoryColor={};impl_->hairHistoryDepth={};
    impl_->opaqueStrandShadowsOwner.reset();impl_->opaqueStrandShadows={};
    impl_->sourceDisplayPass.Reset();
    impl_->strandShadowPass.Reset();
    impl_->strandOpacityShadowPass.Reset();
    impl_->strandPass.Shutdown(); impl_->displayColor={};
    impl_->strandShutterPass.Shutdown();
    impl_->skinProgramsOwner.reset();impl_->skinTargetsOwner.reset();
    impl_->skinDiffuse={};impl_->skinProfile={};impl_->skinAlbedo={};impl_->skinWork={};
    impl_->skinDepthVariants.clear();impl_->skinAttachmentDepth={};
    impl_->targetsOwner.reset(); impl_->assetsOwner.reset();
    impl_->receiverCaptureMrt={};
    impl_->skyOwner.reset(); impl_->skyEnvironment.reset();
    impl_->probeOwner.reset(); impl_->probeVolume.reset();
    impl_->realtimeOwner.reset();impl_->realtimeGeometryOwner.reset();impl_->realtimeScene.reset();
    impl_->realtimeCompleted={};impl_->realtimePending={};
    impl_->lumenProgramsOwner.reset();impl_->lumenSceneOwner.reset();impl_->lumenCacheOwner.reset();impl_->lumenDistanceOwner.reset();impl_->lumenViewOwner.reset();impl_->lumenScene.reset();
    impl_->lumenCompleted={};impl_->lumenPending={};
    impl_->placementProgramsOwner.reset();impl_->placementTargetsOwner.reset();
    impl_->placementUeProgramsOwner.reset();impl_->placementUeMarkProgram={};impl_->placementUeSpawnProgram={};impl_->placementUeComputeMark={};
    impl_->placementComputeOwner.reset();impl_->placementComputeMark={};
    impl_->placementComputeChecked=impl_->placementComputeSupported=impl_->placementMarkedStorage=false;
    impl_->placementComparisonOwner.reset();impl_->placementComparisonMarked={};impl_->placementComparisonSpawned={};impl_->placementComparisonValid=false;
    impl_->placementCompleted={};impl_->placementPending={};
    impl_->traceProgramsOwner.reset();impl_->traceTargetsOwner.reset();impl_->traceCompleted={};impl_->tracePending={};
    impl_->traceSingleProgramsOwner.reset();impl_->sdfSingleProgramsOwner.reset();
    impl_->traceOutputProgramsOwner.reset();impl_->sdfOutputProgramsOwner.reset();impl_->traceOutputTargetsOwner.reset();
    impl_->traceOutputTarget={};impl_->traceOutputParts={};
    impl_->traceGeometryCapabilityChecked=false;impl_->traceGeometrySupported=false;
    impl_->sdfProgramsOwner.reset();impl_->sdfMeshOwner.reset();impl_->sdfWorldOwner.reset();impl_->sdfViewOwner.reset();impl_->sdfScene.reset();
    impl_->sdfNativeUploadsPruned=false;
    impl_->sdfTraceVariantProgramsOwner={};impl_->sdfTraceVariantPrograms={};
    impl_->sdfNativeProgramsOwner={};impl_->sdfNativePrograms={};
    impl_->sdfTraceVariantCompactOwner={};impl_->sdfTraceVariantCompactClassify={};impl_->sdfTraceVariantCompactTrace={};
    impl_->sdfCompactOwner.reset();impl_->sdfCompactDataOwner.reset();impl_->sdfCompactReset={};impl_->sdfCompactClassify={};impl_->sdfCompactTrace={};
    impl_->sdfCompactControl={};impl_->sdfCompactQueue={};impl_->sdfCompactCapacity=0;impl_->sdfCompactMaxBufferBytes=0;
    impl_->sdfCompactCapabilityChecked=false;impl_->sdfCompactSupported=false;impl_->sdfRaysStorage=false;
    impl_->sdfMeshUploads.clear();
    impl_->surfaceCacheProgramsOwner.reset();impl_->surfaceCacheDataOwner.reset();impl_->surfaceCacheViewOwner.reset();impl_->surfaceCacheConstantsOwner.reset();
    impl_->surfaceCacheHalfReceiverRadiance=false;
    impl_->surfaceCacheNativeLightingProgramsOwner.reset();impl_->surfaceCacheNativeContactProgramsOwner.reset();
    impl_->surfaceCacheNativeProbeOriginProgramsOwner.reset();
    impl_->surfaceCacheReusedNativeProbeOriginProgramsOwner.reset();
    for(auto& owner:impl_->surfaceCacheNativeOriginContactProgramsOwner)owner.reset();
    for(auto& owner:impl_->surfaceCacheReusedNativeOriginContactProgramsOwner)owner.reset();
    impl_->surfaceCacheQueryProbeOriginOwners={};impl_->surfaceCacheQueryProbeOriginPrograms={};
    impl_->surfaceCacheQueryContactOwners={};impl_->surfaceCacheQueryContactPrograms={};
    impl_->surfaceCacheQueryContactOriginPrograms={};
    impl_->surfaceCacheReceiverTraceDiagnosticOwners={};impl_->surfaceCacheReceiverTraceDiagnosticPrograms={};
    impl_->surfaceCacheReceiverTraceMrtOwners={};impl_->surfaceCacheReceiverTraceMrtPrograms={};
    impl_->surfaceCacheReceiverTraceMrtOwner.reset();impl_->surfaceCacheReceiverTraceMrt={};impl_->surfaceCacheReceiverTracePacked={};
    impl_->surfaceCacheReceiverTraceSamplingOwner.reset();
    impl_->surfaceCacheReceiverOriginsOwner.reset();impl_->surfaceCacheReceiverOrigins={};
    impl_->surfaceCacheQueryProbeOriginActive=false;
    impl_->surfaceCacheNativeOriginsActive=impl_->surfaceCacheReusedNativeOriginsActive=false;
    impl_->surfaceCacheHardwareCapabilityChecked=impl_->surfaceCacheHardwareSupported=false;
    impl_->areaLightShadowCompleted={};impl_->areaLightShadowPending={};impl_->areaLightShadowConstantsOwner.reset();impl_->areaLightShadowBuffer={};
    impl_->areaLightShadowAtlasPool.clear();
    impl_->areaCasterPreparationOwner.reset();impl_->areaCasterObjectBuffers.clear();impl_->areaCasterMaterialBuffers.clear();impl_->areaCasterParameterSizes.clear();
    impl_->surfaceCacheProbeOwner.reset();
    impl_->nativeProbeTransport={};
    impl_->ueLumenDirectLightingSchedule={};impl_->ueHighAO={};impl_->ueLumenHighGather={};impl_->ueHighTemporal={};impl_->ueLumenProbeImportance={};impl_->ueHitSceneColor={};impl_->ueLumenViewFrame=0;
    impl_->surfaceCacheTemporalOwner.reset();impl_->surfaceCacheTemporalProgramsOwner.reset();
    impl_->surfaceCacheTemporalHalfReceiverRadiance=false;
    impl_->transportOwner.reset();impl_->transportProgramsOwner.reset();impl_->transportCompleted={};impl_->transportPending={};
    impl_->transportDiagnosticOwner.reset();impl_->transportDiagnosticProgramsOwner.reset();
    impl_->transportReceiverFilterOwner.reset();impl_->transportReceiverScratch={};
    impl_->transportPrecisionOwner.reset();impl_->transportPrecision={};impl_->transportPrecisionPixels.clear();
    impl_->transportPrecisionInputs.clear();impl_->transportPrecisionForInput.clear();impl_->transportPrecisionGeometry.reset();
    impl_->transportPrecisionUploads.reset();
    impl_->transportPrecisionScene=nullptr;impl_->transportPrecisionGeneration=0;
    impl_->transportViewOwner.reset();
    impl_->transportCompactOwner.reset();impl_->transportCompactDataOwner.reset();impl_->transportCompactRaw={};
    impl_->transportCompactReset={};impl_->transportCompactClassify={};impl_->transportCompactTrace={};
    impl_->transportCompactControl={};impl_->transportCompactQueue={};impl_->transportCompactCapacity=0;impl_->transportCompactView=false;
    impl_->transportReconstructedBase={};impl_->transportDeferredView=impl_->transportCompactDeferredMix=false;
    impl_->transportHalfReceiverRadiance=impl_->transportReceiverScratchHalfRadiance=impl_->transportCompactHalfRadiance=false;
    impl_->lightingQueryProgramsOwner.reset();impl_->lightingQueryOwner.reset();
    impl_->lightingActualProgramsOwner.reset();impl_->lightingActualOwner.reset();impl_->lightingActualProgram={};
    impl_->lightingActualQueryTexture={};impl_->lightingActualTarget={};impl_->lightingActualNativeVisibility=false;
    impl_->surfaceCacheObjectTemporalProgramsOwner.reset();impl_->surfaceCacheObjectMotionOwner.reset();
    impl_->surfaceCacheStaticMotionProgramsOwner.reset();impl_->surfaceCacheStaticMotionProgram={};impl_->surfaceCacheStaticMotionBuffer={};
    impl_->surfaceCacheObjectMotionStates={};
    impl_->surfaceCacheShadowTemporalProgramsOwner.reset();
    impl_->surfaceCacheDisocclusionTemporalProgramsOwner.reset();impl_->surfaceCacheObjectDisocclusionTemporalProgramsOwner.reset();
    impl_->surfaceCacheObjectPreviousPositions={};impl_->surfaceCacheObjectMotionBuffer={};
    impl_->surfaceCacheTemporalCompleted={};impl_->surfaceCacheTemporalPending={};
    impl_->surfaceCacheSpatialOwner.reset();impl_->surfaceCacheSpatialProgramsOwner.reset();
    impl_->surfaceCacheSpatialRadianceProgramsOwner.reset();
    impl_->surfaceCacheContactOwner.reset();impl_->surfaceCacheContactProgramsOwner.reset();
    impl_->surfaceCacheBudgetRiskOwner.reset();impl_->surfaceCacheBudgetScreenRisk={};
    impl_->surfaceCacheBudgetScreenRiskProgram={};impl_->surfaceCacheBudgetMaskProgram={};
    impl_->surfaceCacheGatherCacheCompleted={};impl_->surfaceCacheGatherCachePending={};
    impl_->surfaceCacheFreshProgramsOwner.reset();impl_->surfaceCacheFreshOwner.reset();impl_->surfaceCacheSamplingDiagnosticsOwner.reset();
    impl_->surfaceCacheAsset.reset();impl_->surfaceCacheGeometryScene.reset();impl_->surfaceCacheUploadedOwnerGeometry.reset();impl_->surfaceCacheCompleted={};impl_->surfaceCachePending={};
    impl_->surfaceCacheBuffer={};
    impl_->lumenReactiveFrames=0;impl_->lumenGeometryChanged=impl_->realtimeGeometryChanged=false;
    impl_->width = impl_->height = impl_->atlasResolution = 0;
    impl_->lastBloom = {};
    impl_->completed = {}; impl_->pending = {}; impl_->pendingToken = 0;
}
std::uint64_t MaterialRenderPipeline::RecordedFrameToken() const noexcept { return impl_->pendingToken; }
bool MaterialRenderPipeline::CompleteFrame(std::uint64_t token) {
    if (!token || token != impl_->pendingToken) { impl_->error = "Stale or absent frame completion token"; return false; }
    impl_->completed = impl_->pending; impl_->pending = {}; impl_->pendingToken = 0;
    ++impl_->ueLumenViewFrame;
    impl_->realtimeCompleted=impl_->realtimePending;impl_->realtimePending={};
    impl_->lumenCompleted=impl_->lumenPending;impl_->lumenPending={};
    impl_->placementCompleted=impl_->placementPending;impl_->placementPending={};
    if(impl_->placementCompleted.valid) {
        impl_->placementCommittedFrameConstants=impl_->placementFrameConstants;
        impl_->placementCommittedBaseSamplingSeed=impl_->placementPendingBaseSamplingSeed;
        impl_->placementCommittedGiTemporalSequenceAvailable=impl_->surfaceCacheTemporalPending.valid;
        impl_->placementCommittedGiTemporalSequence=impl_->surfaceCacheTemporalPending.valid?
            impl_->surfaceCacheTemporalPending.sequence-1:0;
    }
    impl_->CommitUeLumenDirectLightingSchedule(true);
    impl_->CommitUeLumenRadianceCache(true);
    impl_->CommitUeLumenProbeImportance(true);
    impl_->CommitUeLumenHitSceneColor(true);
    impl_->CompleteUeLumenHighShortRangeAO(true);
    impl_->tracePending.uploadsPruned=impl_->statistics.probeSdfNativeUploadsPruned;
    impl_->traceCompleted=impl_->tracePending;impl_->tracePending={};
    impl_->surfaceCacheCompleted=impl_->surfaceCachePending;impl_->surfaceCachePending={};
    impl_->transportCompleted=impl_->transportPending;impl_->transportPending={};
    impl_->surfaceCacheGatherCacheCompleted=impl_->surfaceCacheGatherCachePending;impl_->surfaceCacheGatherCachePending={};
    impl_->surfaceCacheTemporalCompleted=std::move(impl_->surfaceCacheTemporalPending);impl_->surfaceCacheTemporalPending={};
    impl_->areaLightShadowCompleted=std::move(impl_->areaLightShadowPending);impl_->areaLightShadowPending={};
    return true;
}
bool MaterialRenderPipeline::DiscardFrame(std::uint64_t token) {
    if (!token || token != impl_->pendingToken) { impl_->error = "Stale or absent discarded frame token"; return false; }
    impl_->strandPass.InvalidateVolume();
    impl_->strandShutterPass.InvalidateVolume();
    impl_->CommitUeLumenDirectLightingSchedule(false); impl_->CommitUeLumenRadianceCache(false);
    impl_->CommitUeLumenProbeImportance(false); impl_->CommitUeLumenHitSceneColor(false);
    impl_->CompleteUeLumenHighShortRangeAO(false);
    impl_->pending = {}; impl_->pendingToken = 0;
    impl_->realtimePending={};
    impl_->lumenPending={};
    impl_->placementPending={};
    impl_->tracePending={};
    impl_->surfaceCachePending={};
    impl_->surfaceCacheTemporalPending={};
    impl_->surfaceCacheGatherCachePending={};
    impl_->transportPending={};
    impl_->areaLightShadowPending={};
    return true;
}
void MaterialRenderPipeline::ResetHistory(bool invalidateOpaqueDepth) {
    if(invalidateOpaqueDepth)InvalidateOpaqueDepth();
    impl_->ResetUeLumenDirectLightingSchedule();impl_->ResetUeLumenHighShortRangeAOHistory();impl_->ResetUeLumenHitSceneColorHistory();
    impl_->completed.valid = impl_->completed.taaValid = false;
    impl_->pending.valid = impl_->pending.taaValid = false;
    impl_->lumenCompleted.viewValid=false;impl_->lumenPending.viewValid=false;
    impl_->surfaceCacheTemporalCompleted.valid=false;impl_->surfaceCacheTemporalPending.valid=false;
    impl_->transportCompleted={};impl_->transportPending={};
    impl_->surfaceCacheGatherCacheCompleted={};impl_->surfaceCacheGatherCachePending={};
}
void MaterialRenderPipeline::InvalidateOpaqueDepth() noexcept { ++impl_->opaqueDepthEpoch; }
const std::string& MaterialRenderPipeline::LastError() const noexcept { return impl_->error; }
PipelineStatistics MaterialRenderPipeline::Statistics() const { return impl_->statistics; }
std::array<std::uint32_t,4> MaterialRenderPipeline::CaptureStrandNativeOitCounters()const{
    return impl_->strandPass.CaptureNativeOitCounters();
}
PipelineDebugTextures MaterialRenderPipeline::DebugTextures() const {
    if (!impl_->targetsOwner) return {};
    PipelineDebugTextures result{impl_->scene.color, impl_->scene.depth, impl_->shadows.depth,
            impl_->reflection.color, impl_->aoBlurred.color, impl_->lastBloom,
            impl_->reflection.width, impl_->reflection.height, impl_->samples,
            impl_->indirectFull.color, impl_->diffuseCapture.color, impl_->normalCapture.color, impl_->diffuseCapture.depth,
            impl_->opaqueSnapshot.color, impl_->opaqueSnapshot.depth,
            impl_->realtimeCompleted.valid?impl_->realtimeCache[impl_->realtimeCompleted.image].color:RenderResourceHandle<RHITextureSpec>{},
            impl_->lumenCompleted.valid?impl_->lumenCache[impl_->lumenCompleted.image].color:RenderResourceHandle<RHITextureSpec>{},
            impl_->lumenCompleted.viewValid?impl_->indirectFull.color:RenderResourceHandle<RHITextureSpec>{},
            impl_->lumenCompleted.viewValid?impl_->lumenReflectionFull.color:RenderResourceHandle<RHITextureSpec>{},
            impl_->lumenCompleted.valid?impl_->lumenDirectCache[impl_->lumenCompleted.image].color:RenderResourceHandle<RHITextureSpec>{},
            impl_->lumenCompleted.viewValid?impl_->lumenSpecularSource.color:RenderResourceHandle<RHITextureSpec>{},
            impl_->lumenCompleted.viewValid?impl_->lumenSpecularSource.depth:RenderResourceHandle<RHITextureSpec>{}};
    result.displayColor=impl_->displayColor;
    if(impl_->statistics.surfaceCacheReceiverMotionDraws||impl_->statistics.surfaceCacheStaticReceiverMotionCapture) {
        result.surfaceCacheObjectPreviousPositions=impl_->surfaceCacheObjectPreviousPositions.color;
        result.surfaceCacheObjectMotionStates=impl_->surfaceCacheObjectMotionStates;
    }
    if(impl_->completed.valid){
        const auto& completed=impl_->completed;
        result.completedTemporalSequence=completed.sequence;
        if(completed.taaValid&&completed.sequence)
            result.completedJitterUV=TemporalJitter(completed.sequence-1,impl_->width,impl_->height);
        result.completedJitteredViewProjection=completed.jitteredVP;
        result.completedUnjitteredViewProjection=completed.camera.projection*completed.camera.view;
    }
    if(impl_->areaLightShadowCompleted.valid) {
        result.areaLightShadowAtlas=impl_->areaLightShadowCompleted.atlas.depth;
        result.areaLightShadowWidth=impl_->areaLightShadowCompleted.atlas.width;
        result.areaLightShadowHeight=impl_->areaLightShadowCompleted.atlas.height;
    }
    result.skinDiffuse=impl_->skinDiffuse.color;result.skinDiffused=impl_->skinWork[1].color;result.skinProfile=impl_->skinProfile.color;
    result.skinAlbedo=impl_->skinAlbedo.color;
    if(impl_->statistics.strandPasses>0){
        const auto temporal=impl_->strandPass.TemporalTextures();
        result.strandNearestDepth=temporal.nearestDepth;result.strandCoverage=temporal.coverage;result.strandCompositeDepth=temporal.compositeDepth;
        result.strandMotion=temporal.motion;result.strandResolvedColor=temporal.resolvedColor;
        if(impl_->completed.hairTaaValid){
            result.strandTemporalColor=impl_->hairHistoryColor[impl_->completed.image].color;
            result.strandBackgroundColor=impl_->preHairSnapshot.color;
            result.strandTemporalBackgroundColor=impl_->historyColor[impl_->completed.image].color;
        }
        const auto volume=impl_->strandPass.VolumeDebugTextures();
        result.strandDensity=volume.density;result.strandPaths=volume.paths;
        result.strandPathsExtra=volume.pathsExtra;result.strandPathsAmbient=volume.pathsAmbient;
        result.strandOpaqueBlockers=volume.opaqueBlockers;result.strandDirectVisibility=volume.directVisibility;result.strandExtraVisibility=volume.extraVisibility;
        result.strandVolumeWidth=volume.width;result.strandVolumeHeight=volume.height;
    }
    if(impl_->placementCompleted.valid) {
        const auto& state=impl_->placementCompleted;const auto image=state.image;
        result.screenProbeUniform=impl_->placementUniform[image].color;
        if(impl_->statistics.screenProbePlacementComparisonCollected){
            result.screenProbeComparisonMarked=impl_->placementComparisonMarked.color;
            result.screenProbeComparisonSpawned=impl_->placementComparisonSpawned.color;}
        result.screenProbeMarked=impl_->placementMarked[image].color;
        result.screenProbeSpawned=impl_->placementSpawned[image].color;
        result.screenProbeUniformWidth=impl_->placementUniform[image].width;result.screenProbeUniformHeight=impl_->placementUniform[image].height;
        result.screenProbeCandidateWidth=impl_->placementMarked[image].width;result.screenProbeCandidateHeight=impl_->placementMarked[image].height;
        result.screenProbeGridWidth=result.screenProbeUniformWidth/2;result.screenProbeGridHeight=result.screenProbeUniformHeight;
        result.screenProbeCandidateCount=state.candidates;result.screenProbeSourceWidth=state.sourceWidth;result.screenProbeSourceHeight=state.sourceHeight;
        result.screenProbeFrameIndex=state.phase;
        result.screenProbePlacementConstants=impl_->placementCommittedFrameConstants;
        result.screenProbePlacementUniformConstants=impl_->placementInitialFrameConstants[image];
        result.screenProbePlacementImage=image;
        result.screenProbeBaseSamplingSeed=impl_->placementCommittedBaseSamplingSeed;
        result.screenProbeRaySamplingSeed=std::uint32_t(impl_->placementCommittedFrameConstants.tracing.w)>>2;
        result.screenProbeGiTemporalSequence=impl_->placementCommittedGiTemporalSequence;
        result.screenProbeGiTemporalSequenceAvailable=impl_->placementCommittedGiTemporalSequenceAvailable;
    }
    if(impl_->traceCompleted.valid) {
        const auto& rays=impl_->traceRays[impl_->traceCompleted.image];
        result.screenProbeScreenRays=rays.color;
        result.screenProbeRays=impl_->traceCompleted.sdf?impl_->sdfRays[impl_->traceCompleted.image].color:rays.color;
        result.screenProbeRayWidth=rays.width;result.screenProbeRayHeight=rays.height;
        result.screenProbeRayResolution=impl_->traceCompleted.resolution;
        result.screenProbeTraceSource=impl_->traceSource.color;result.screenProbeTracePositions=impl_->indirectPositions.color;
        if(impl_->statistics.probeSdfCompactRays){
            result.probeSdfCompactControl=impl_->sdfCompactControl;result.probeSdfCompactQueue=impl_->sdfCompactQueue;
            result.probeSdfCompactCapacity=impl_->sdfCompactCapacity;
        }
        result.screenProbeHzbLevels=impl_->statistics.screenProbeHzbLevels;
        for(unsigned level=0;level<impl_->traceHzbLevels;++level) {
            result.screenProbeHzb[level]=impl_->traceHzb[level].color;
            result.screenProbeHzbSizes[level]={impl_->traceHzb[level].width,impl_->traceHzb[level].height};
        }
    }
    if(impl_->traceCompleted.sdf&&(impl_->traceCompleted.valid||impl_->traceCompleted.volume)) {
        result.probeSdfUploadsPruned=impl_->traceCompleted.uploadsPruned;
        if(!result.probeSdfUploadsPruned) {
            result.probeGlobalSdf=impl_->sdfGlobalTexture;result.probeMeshSdf=impl_->sdfMeshTexture;
            if(impl_->sdfScene&&impl_->sdfScene->Clipmaps())result.probeSdfRanges=impl_->sdfRangeTexture;
        }
        if(impl_->traceCompleted.volume)result.probeSdfVolumeView=impl_->placementCoverage.color;
    }
    if(impl_->surfaceCacheCompleted.valid) {
        if(impl_->transportCompleted.valid){result.screenProbeTracePositions=impl_->indirectPositions.color;
            result.screenProbeSourceWidth=impl_->width;result.screenProbeSourceHeight=impl_->height;
            if(!impl_->statistics.surfaceCacheNativeScreenProbeTransport)
                result.surfaceCacheTransport=impl_->transport[impl_->transportCompleted.image].color;
            if(impl_->statistics.surfaceCacheCompactReceivers){
                result.surfaceCacheReceiverCompactControl=impl_->transportCompactControl;
                result.surfaceCacheReceiverCompactQueue=impl_->transportCompactQueue;
                result.surfaceCacheReceiverCompactCapacity=impl_->transportCompactCapacity;}
            result.surfaceCacheTransportMask=impl_->transportMask.color;
            if((impl_->statistics.surfaceCacheNativeForceRasterCore||impl_->statistics.surfaceCacheNativeReceiverFilterSupportHalo)&&impl_->transportMask.color==impl_->transportReconstructedMask.color)
                result.surfaceCacheReceiverClassification=impl_->transportClassification.color;
            if(impl_->statistics.surfaceCacheSplitTransport&&impl_->transportReconstructed.color.IsValid()&&
                impl_->transportMask.color==impl_->transportReconstructedMask.color)
                result.surfaceCacheTransportReconstruction=impl_->transportReconstructed.color;
            if(impl_->statistics.surfaceCacheTransportDiagnostics)for(unsigned i=0;i<2;++i)
                if(i||!impl_->statistics.surfaceCacheNativeScreenProbeTransport)result.surfaceCacheTransportDiagnostics[i]=impl_->transportDiagnostics[i].color;}
        result.surfaceCacheAtlas=impl_->surfaceCacheLighting.color;
        if(impl_->statistics.surfaceCacheLightingQueriesCollected)result.surfaceCacheLightingQueries=impl_->lightingQueryTarget.color;
        if(impl_->surfaceCacheCompleted.rawCollected)result.surfaceCacheRawGi=impl_->surfaceCacheRaw.color;
        result.surfaceCacheSpatialGi=impl_->surfaceCacheRaw.color;
        if(impl_->surfaceCacheCompleted.rawCollected)result.surfaceCacheCoverage=impl_->surfaceCacheCoverage.color;
        result.surfaceCacheWidth=impl_->surfaceCacheLighting.width;result.surfaceCacheHeight=impl_->surfaceCacheLighting.height;
        result.surfaceCacheGiWidth=impl_->surfaceCacheRaw.width;result.surfaceCacheGiHeight=impl_->surfaceCacheRaw.height;
        if(impl_->surfaceCacheCompleted.probes) {
            result.surfaceCacheProbeCoverage=impl_->surfaceCacheProbeCoverage.color;
            if(impl_->traceCompleted.valid)result.surfaceCacheSpatialRays=impl_->traceCompleted.sdf?
                impl_->sdfRays[impl_->traceCompleted.image].color:impl_->traceRays[impl_->traceCompleted.image].color;
            if(impl_->surfaceCacheCompleted.rawCollected)result.surfaceCacheProbeIrradiance=impl_->surfaceCacheProbeIrradiance.color;
            result.surfaceCacheProbeDirectional=impl_->surfaceCacheProbeDirectional.color;
            if(impl_->statistics.surfaceCacheProbeOriginsCollected)result.surfaceCacheProbeOrigins=impl_->surfaceCacheProbeOrigins.color;
            result.surfaceCacheProbeWidth=impl_->surfaceCacheProbeIrradiance.width;
            result.surfaceCacheProbeHeight=impl_->surfaceCacheProbeIrradiance.height;
        }
        if(impl_->surfaceCacheCompleted.spatial) {
            result.surfaceCacheSpatialGi=impl_->surfaceCacheSpatialGi.color;
            result.surfaceCacheSpatialRays=impl_->surfaceCacheSpatialRays[impl_->surfaceCacheCompleted.spatialImage].color;
            result.surfaceCacheSpatialRadianceOnly=impl_->surfaceCacheCompleted.spatialRadianceOnly;
            if(result.surfaceCacheSpatialRadianceOnly&&impl_->traceCompleted.valid)
                result.surfaceCacheSpatialMetadata=impl_->sdfRays[impl_->traceCompleted.image].color;
            result.surfaceCacheSpatialNeighbors=impl_->surfaceCacheSpatialNeighbors.color;
        }
        if(impl_->statistics.surfaceCacheContactCollected) {
            result.surfaceCacheContactGi=impl_->surfaceCacheContactResult.color;
            result.surfaceCacheContactUnfilteredGi=impl_->surfaceCacheContactUnfilteredGi.color;
            result.surfaceCacheContactMask=impl_->surfaceCacheContactMask.color;
            result.surfaceCacheContactProbeRisk=impl_->surfaceCacheContactRisk.color;
            if(impl_->statistics.surfaceCacheFreshSamplingCollected)result.surfaceCacheFreshSampling=impl_->surfaceCacheFreshSampling.color;
            if(impl_->statistics.surfaceCacheReceiverSamplingCollected)result.surfaceCacheReceiverSampling=impl_->surfaceCacheReceiverSampling.color;
            if(impl_->statistics.surfaceCacheReceiverTraceDiagnosticsCollected)result.surfaceCacheReceiverTracePacked=impl_->surfaceCacheReceiverTracePacked;
        }
        if(impl_->statistics.surfaceCacheUeLumenHigh){
            const auto& cache=impl_->nativeProbeTransport.ueRadianceCache;
            result.surfaceCacheUeLumenHigh=true;
            if(cache.completed.valid){
                result.ueRadianceCacheControl=cache.control;result.ueRadianceCacheQueue=cache.traceQueue;
                result.ueRadianceCacheDiagnostics=cache.queryDiagnostics;
                if(cache.queryDiagnostics)result.ueRadianceCacheAudit=cache.traceAudit;
                result.ueRadianceCacheMaxDistance=impl_->placementCommittedFrameConstants.traceParams.x;
            }
            result.ueHighHalfGi=impl_->ueLumenHighGather.halfGi.color;
            result.ueHighMoving=impl_->ueLumenHighGather.lightIsMoving.color;
            result.ueHighRawAO=impl_->ueHighAO.rawAO.color;
            result.ueHighAOHistory=impl_->ueHighAO.history[impl_->surfaceCacheTemporalCompleted.image].color;
            result.surfaceCacheProbeIrradiance=impl_->ueLumenHighGather.irradiance.color;
            result.surfaceCacheSpatialNeighbors=impl_->ueLumenHighGather.neighbors.color;
            result.surfaceCacheNativeNeighbors=impl_->ueLumenHighGather.neighbors.color;
            result.surfaceCacheSpatialGi={};result.surfaceCacheProbeCoverage={};
        }
        else if(impl_->statistics.surfaceCacheNativeScreenProbeTransport){
            result.surfaceCacheNativeBaseGi=impl_->nativeProbeTransport.NativeBaseGI.color;
            result.surfaceCacheContactMask=impl_->nativeProbeTransport.NativeSoftMask.color;
            result.surfaceCacheContactProbeRisk=impl_->nativeProbeTransport.NativeProbeRisk.color;
            result.surfaceCacheSpatialNeighbors=impl_->nativeProbeTransport.neighbors.color;
            result.surfaceCacheNativeNeighbors=impl_->nativeProbeTransport.neighbors.color;
            if(impl_->nativeProbeTransport.reconstructionComparisonRecorded)
                for(unsigned i=0;i<4;++i)result.surfaceCacheNativeReconstructionComparison[i]=impl_->nativeProbeTransport.reconstructionComparison[i].color;
            if(impl_->statistics.surfaceCacheFreshSamplingCollected)result.surfaceCacheFreshSampling=impl_->surfaceCacheFreshSampling.color;
            if(impl_->statistics.surfaceCacheNativeReceiverDiagnosticPages==4)
                for(unsigned page=0;page<4;++page)result.surfaceCacheNativeReceiverTracePages[page]=impl_->nativeProbeTransport.receiverDiagnosticPages[page].color;
        }
        if(impl_->surfaceCacheTemporalCompleted.valid) {
            result.surfaceCacheTemporalGi=impl_->surfaceCacheTemporalGi.color;
            if(impl_->statistics.surfaceCacheUeLumenHigh)
                result.surfaceCacheDirectSource=impl_->surfaceCacheSource.color;
            result.surfaceCacheTemporalHistory=impl_->surfaceCacheTemporalHistory[impl_->surfaceCacheTemporalCompleted.image].color;
        }
    }
    return result;
}
} // namespace Render
