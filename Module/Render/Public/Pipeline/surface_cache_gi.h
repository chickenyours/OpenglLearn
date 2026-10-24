#pragma once
#include <cmath>
#include <cstdint>
#include <memory>
#include <glm/glm.hpp>
#include "Render/Public/Pipeline/spot_light.h"
#include "Render/Public/Pipeline/area_light_shadows.h"
namespace Render {
class SurfaceCacheScene;
class ProbeSdfScene;
inline constexpr std::uint32_t SurfaceCacheSpatialNeighborCount=16;
// Probe-radiance filtering precedes integration, reconstruction and temporal
// accumulation. Visibility and hit-angle checks preserve geometry/contact edges.
struct SurfaceCacheSpatialSettings {
    bool enabled=true,visibilityCheck=true,localAdaptiveNeighbors=true;
    // Execution-only layout: filter one radiance texel per ray, referencing
    // immutable direction/hit metadata from the original packed ray atlas.
    bool radianceOnlyFiltering=false;
    std::uint32_t passes=3,halfKernelSize=1;
    float positionWeightScale=1000,maxHitAngleDegrees=10,normalThresholdDegrees=30;
    float strength=1,planeDistanceThreshold=.01f;
};
inline bool ValidateSurfaceCacheSpatialSettings(const SurfaceCacheSpatialSettings& s) {
    return s.passes>=1&&s.passes<=4&&s.halfKernelSize>=1&&s.halfKernelSize<=2&&
        std::isfinite(s.positionWeightScale)&&s.positionWeightScale>=1&&s.positionWeightScale<=100000&&
        std::isfinite(s.maxHitAngleDegrees)&&s.maxHitAngleDegrees>=1&&s.maxHitAngleDegrees<=60&&
        std::isfinite(s.normalThresholdDegrees)&&s.normalThresholdDegrees>=1&&s.normalThresholdDegrees<=90&&
        std::isfinite(s.strength)&&s.strength>=0&&s.strength<=1&&
        std::isfinite(s.planeDistanceThreshold)&&s.planeDistanceThreshold>=.0001f&&s.planeDistanceThreshold<=.1f;
}
// Reconstructing a probe's narrow contact shadow at a displaced receiver is
// unsafe even when both points share a plane. Retrace only that geometric risk
// band at the actual receiver; short black hits remain real measurements.
struct SurfaceCacheContactSettings {
    bool enabled=true,layerValidation=true; // retrace ambiguous nearby parallel layers instead of merging their light
    bool temporalDirections=true,coverageWeightedFilter=true; // independent receiver rays and continuous confidence weights
    bool geometryGuidedFilter=true,referenceReceiver=false; // per-receiver hit-parallax / diagnostic safe-origin reference
    bool screenContactDetection=true; // receiver-side close discontinuity supplement when probes miss a curved contact
    bool budgetedSampling=false; // opt-in 1..7 receiver rays; legacy validation retains its 8-ray floor
    bool halfResolutionScreenRisk=false; // share geometric classification; incompatible layer/normal pixels retrace their own stencil
    bool prioritizeContactCore=false; // double base directions only in the strong geometric-risk core
    bool coherentProbeRisk=true; // near-hit mass fills parallax-only holes at the probe source itself
    // Extra complete current-ray batches only at geometrically newly exposed
    // or still-young receivers. Cold stationary convergence is unchanged.
    bool disocclusionSampling=false;
    bool collectSamplingDiagnostics=false; // optional same-loop GPU diagnostic replay; never ordinary rendering
    // Execution experiments retain receiver eligibility, safe-origin math,
    // ray directions and lighting/filter rules. New switches default off.
    bool splitReceiverOrigins=false,reuseLightingLookup=false;
    bool fastLightingAddressing=false; // independent guarded addressing without retaining anchor context
    bool collectTraceTimings=false; // diagnostic nested pass timestamps; separate from ordinary profiling
    bool collectTraceDiagnostics=false; // same-frame four-MRT packed counters, diagnostic-only
    std::uint32_t traceDiagnosticPage=0; // 0 keeps original sampling RGBA; 1..6 are untimed replay counters
    std::uint32_t disocclusionRayMultiplier=4;
    // Additional current batches for certified moving receivers already inside
    // the original contact-risk mask. Requires reactive moving lighting and
    // disocclusionSampling; never restarts a geometric exposure lifetime.
    std::uint32_t movingReceiverRayMultiplier=1; // 1 (off), 2 or 4
    bool movingReceiverFullCurrent=false; // trace every certified moving receiver instead of only the original risk mask
    std::uint32_t raysPerPixel=8,filterRadius=1,filterPasses=3;
    std::uint32_t guideMaxRadius=4;
    float maxDistance=.1f,parallaxAngleDegrees=10,weightThreshold=.05f;
    float guideHitAngleDegrees=10;
    float filterStrength=1;
};
inline bool ValidateSurfaceCacheContactSettings(const SurfaceCacheContactSettings& s) {
    return (!s.prioritizeContactCore||s.raysPerPixel<=16)&&(!s.collectTraceDiagnostics||(s.collectSamplingDiagnostics&&s.traceDiagnosticPage==0))&&s.traceDiagnosticPage<=6&&s.raysPerPixel>=(s.budgetedSampling?1u:8u)&&s.raysPerPixel<=32&&s.disocclusionRayMultiplier>=1&&s.disocclusionRayMultiplier<=4&&(s.movingReceiverRayMultiplier==1||s.movingReceiverRayMultiplier==2||s.movingReceiverRayMultiplier==4)&&s.filterRadius>=1&&s.filterRadius<=2&&s.filterPasses>=1&&s.filterPasses<=3&&
        s.guideMaxRadius>=1&&s.guideMaxRadius<=4&&std::isfinite(s.guideHitAngleDegrees)&&s.guideHitAngleDegrees>=1&&s.guideHitAngleDegrees<=45&&
        std::isfinite(s.maxDistance)&&s.maxDistance>=.01f&&s.maxDistance<=2&&
        std::isfinite(s.parallaxAngleDegrees)&&s.parallaxAngleDegrees>=1&&s.parallaxAngleDegrees<=45&&
        std::isfinite(s.weightThreshold)&&s.weightThreshold>=.001f&&s.weightThreshold<=.5f&&
        std::isfinite(s.filterStrength)&&s.filterStrength>=0&&s.filterStrength<=1;
}
// Final-gather history, independent of TAA. Certified movable transforms reuse
// geometry-validated history with conservative reactive lighting bounds.
struct SurfaceCacheTemporalSettings {
    // Use a gentle moving clamp on interiors and a short, conservative history
    // at moving geometry edges. Both remain independent of final-image TAA.
    bool enabled=true,reset=false,neighborhoodClamp=true,edgeProtection=true,clampSlowMotion=true;
    // A certified stationary view averages fresh probe directions for 64 frames
    // and reuses only matching angular strata. Motion keeps responsive limits.
    bool staticConvergence=true;
    // A/B option: keep matching-stratum spatial reuse through valid moving
    // histories when static convergence is enabled. History response caps stay unchanged.
    bool motionConsistentSpatialReuse=false;
    // Opt-in experiment: consecutive camera-only frames may retain up to twice
    // maxFrames where geometry, readability and current lighting agree locally.
    // Stationary, dynamic-scene and uncertain/disoccluded receivers keep their
    // existing response. This is a bounded heuristic, not estimator certification.
    bool localCameraHistory=false;
    // Motion-only stabilization: bounded same-material camera reprojection and
    // guided display filtering. Static cold convergence stays unchanged.
    bool motionStability=false;
    bool motionSpatialFilter=false; // optional display-only residual filter; never feeds history or adds sample count
    bool motionDisocclusionRecovery=false; // opt-in spatial recovery from validated mature neighbor histories
    // Certified raster receiver motion, with previous-instance position/normal
    // and owner validation. Lighting response retains the dynamic history cap.
    bool objectMotionReprojection=false;
    // Local lighting-change response on certified moving-scene receivers.
    // Current radiance, geometry rejection and cold static accumulation are unchanged.
    bool reactiveShadows=false;
    // Also validate lighting on reprojected moving material points. Effective
    // only with reactiveShadows; false preserves the former rigid-receiver bypass.
    bool reactiveMovingReceivers=true;
    // Guided young-current pairs; unchanged-scene camera motion can also use
    // mature-neighbor temporal residuals after a stable-lighting cooldown.
    bool disocclusionFilter=false;
    std::uint32_t maxFrames=10,directionPhases=8;
    float distanceThreshold=.01f,normalThresholdDegrees=45;
    float movingMaxFrames=8,motionThresholdPixels=3,clampSigma=6,edgeMaxFrames=2;
    bool youngFilter=true,retainHistoryOnObjectMotion=true;
    std::uint32_t youngFilterRadius=3,dynamicSettleFrames=3;
    float youngFilterMaxFrames=4,youngFilterStrength=.75f;
    float dynamicMaxFrames=4,dynamicClampSigma=2;
};
inline bool ValidateSurfaceCacheTemporalSettings(const SurfaceCacheTemporalSettings& s) {
    return s.maxFrames>=1&&s.maxFrames<=64&&s.directionPhases>=1&&s.directionPhases<=64&&
        std::isfinite(s.distanceThreshold)&&s.distanceThreshold>=.0001f&&s.distanceThreshold<=.1f&&
        std::isfinite(s.normalThresholdDegrees)&&s.normalThresholdDegrees>=1&&s.normalThresholdDegrees<=90&&
        std::isfinite(s.movingMaxFrames)&&s.movingMaxFrames>=1&&s.movingMaxFrames<=64&&
        std::isfinite(s.motionThresholdPixels)&&s.motionThresholdPixels>=.01f&&s.motionThresholdPixels<=64&&
        std::isfinite(s.clampSigma)&&s.clampSigma>=.5f&&s.clampSigma<=10&&
        std::isfinite(s.edgeMaxFrames)&&s.edgeMaxFrames>=1&&s.edgeMaxFrames<=64&&
        s.youngFilterRadius>=1&&s.youngFilterRadius<=4&&s.dynamicSettleFrames<=16&&
        std::isfinite(s.youngFilterMaxFrames)&&s.youngFilterMaxFrames>=2&&s.youngFilterMaxFrames<=16&&
        std::isfinite(s.youngFilterStrength)&&s.youngFilterStrength>=0&&s.youngFilterStrength<=1&&
        std::isfinite(s.dynamicMaxFrames)&&s.dynamicMaxFrames>=1&&s.dynamicMaxFrames<=16&&
        std::isfinite(s.dynamicClampSigma)&&s.dynamicClampSigma>=.5f&&s.dynamicClampSigma<=6;
}
// First-bounce diffuse GI: adaptive screen probes integrate Surface Cache
// radiance, followed by geometry-aware reconstruction. The independent
// per-pixel software BVH path remains a reference. Optional final-gather history
// reprojects the reconstructed signal; raw GI remains available independently.
struct SurfaceCacheGiSettings {
    bool enabled=false,probeDiagnostics=false,useScreenProbes=true;
    // Opt-in world-space low-frequency transport. Near geometry and covered
    // cache holes retain complete current receiver rays, including color bleed.
    bool splitReceiverGather=false;
    bool sparseReceiverSampling=false; // current low-frequency residual lattice; contact cores remain full resolution
    bool coherentReceiverTracing=false; // independent core/lattice programs with carried hit-projection context
    bool compactReceiverTracing=false; // GPU classify/compact/indirect receiver compute; requires coherent sparse split
    bool halfReceiverRadiance=false; // opt-in screen radiance storage; geometry, masks and world transport stay float32
    bool deferredReceiverMix=false; // filter actual receiver estimates before mixing the low-frequency base
    // UE 5.7.1 High: fixed source-backed pipeline, separate from historical experiments.
    bool ueLumenHigh=false;
    bool nativeScreenProbeTransport=false; // current native 8x8 screen probes supply the receiver's low-frequency base
    bool nativeAdaptiveFreshSampling=false; // original exposure/moving budgets add independent 8-ray receiver batches
    bool nativeForceRasterCore=false; // retain complete current receiver estimates within the original conservative 3cm contact core
    bool nativeSyntheticReceiverFilterSupport=false; // certified interpolated current may support physical receiver filtering; never an independent sample
    bool nativeReceiverFilterRiskWeights=false; // receiver filter uses geometric risk Y, while X/W retain source/support classification
    bool nativeReceiverFilterSupportHalo=false; // compact sparse receivers add a geometry-bounded seven-pixel physical filter-support halo
    bool nativeComputePlacement=false; // compute Mark once per logical candidate slot; retains the interleaved placement ABI
    bool collectNativePlacementComparison=false; // untimed same-frame original Mark/Spawn comparison only
    bool nativeTemporalContactMetadata=false; // original soft-mask/hit-distance gates for native temporal recovery/display
    bool mrtReceiverCapture=false; // one mesh traversal for rho / normal / raster position; whole-frame legacy fallback
    bool reuseStaticReceiverMotionCapture=false; // certified static owners from current capture; moving-only raster overlay
    bool fuseNativeReconstruction=false; // same native probe stencil writes BaseGI and raw contact risk in one MRT
    // Untimed same-producer replay: 0 off, 1 ordinary, 2 mismatched curved
    // metadata hooks, 3 failed certified origins. Never enabled for FPS runs.
    std::uint32_t nativeReconstructionComparison=0;
    // Untimed capture-equivalence fixture only. A zero normal disables clipping.
    glm::vec4 diagnosticReceiverClipPlane{0};
    bool geometryReceiverRisk=false; // geometry-only cache parallax/near-hit correction strength
    std::uint32_t receiverLatticeMax=4;
    bool cachedDirectDiffuse=false;
    bool sparseNativeVertexUpdates=false;
    std::uint32_t transportRays=16,transportUpdatePeriod=4,transportTexelStride=1;
    float receiverRiskDistance=.15f;
    // Reuse only after 64 identical geometry/lighting/algorithm/camera frames.
    // Cached frames add no samples to history; any change resumes tracing.
    bool cacheSettledGather=false;
    bool omitUnusedRawGather=false; // normal filtered view only; raw diagnostic handles are explicitly unavailable
    std::uint32_t cachedAreaSamplesPerAxis=0; // 0 follows main shading; 4/8 select independent stable midpoint quadrature
    // Independent execution-only A/B controls. Unsupported backends retain
    // software traversal. Probe SDF/GDF and safe-origin certification are unchanged.
    bool hardwareContactTracing=false,hardwareCacheVisibility=false;
    // Candidate search tightens per-probe/selected-receiver source/bias safety
    // intervals before original software nearest-face certification. Expanded
    // hardware candidates alone cannot certify grazing/coplanar geometry.
    bool hardwareOriginTracing=false;
    // Independent receiver-only policy: short source/clearance segments can
    // retain software traversal while main probes use hardware candidates.
    bool receiverSoftwareOrigins=false;
    // Reuse an authoritative software fallback rather than certifying it twice.
    // The original native-origin helper remains selectable for comparison.
    bool reuseOriginSoftwareCertification=false;
    // Independent exact execution experiments. Keep legacy traversal/address
    // calculation available; neither changes sampling or origin acceptance.
    bool reuseClosestTraversal=false,fastGeometryAddressing=false;
    // Rebuild the same native triangle AS into retained storage/handles when
    // topology capacity is unchanged. The query and BUILD mode stay identical.
    bool reuseAccelerationStorage=false;
    // Fixed-topology native scenes may UPDATE retained BLAS/TLAS. Incompatible
    // topology/policy changes retain a full BUILD; this implies storage reuse.
    bool updateAccelerationStructures=false;
    // Queue owned texture copies into the next consuming frame instead of
    // waiting for a dedicated submission after each changed region.
    bool deferResourceUploads=false;
    bool cacheBilinear=true; // same-card lighting filtering with depth/normal visibility
    bool reuseAnyHitBounds=true; // reuse the unchanged parent-tested bounds only in early-out visibility queries
    bool directionalReconstruction=true; // bounded hemisphere-fit direction model, evaluated at receiver normal
    bool safeProbeOrigins=true; // once-per-probe triangle sweep limits normal displacement across nearby geometry
    bool rasterSourcePositions=true; // direct RGBA32F visible positions; false retains depth-unprojection diagnostic
    bool probeOriginDiagnostics=false; // overlay the actual ray origins, without the ordinary marker display offset
    float originSafetyFraction=.5f,originSafetyEpsilon=.00002f;
    bool collectProbeCoverage=true; // optional diagnostic; coverage view always collects it
    std::uint32_t reconstructionDebug=0; // indirect: real / constant / coarse-fine / actual-position affine / selected impulse
    // Keep cached rectangle-light depth shadows available when diffuse GI is off.
    // Requires the opt-in opaque PBR Surface Cache area-shadow shader variant.
    bool areaLightShadows=false;
    AreaLightShadowSettings areaShadows;
    std::uint32_t view=0,raysPerPixel=1; // total, cache, indirect, coverage, atlas, history length, probe coverage
    float maxDistance=30,intensity=1,rayBias=.01f;
    std::shared_ptr<const SurfaceCacheScene> cache;
    std::shared_ptr<const ProbeSdfScene> scene;
    SurfaceCacheTemporalSettings temporal;
    SurfaceCacheSpatialSettings spatial;
    SurfaceCacheContactSettings contact;
};
inline bool ValidateSurfaceCacheGiSettings(const SurfaceCacheGiSettings& s) {
    if(s.ueLumenHigh&&!s.nativeScreenProbeTransport)return false;
    if(s.nativeReconstructionComparison>3||((s.fuseNativeReconstruction||s.nativeReconstructionComparison)&&!s.nativeScreenProbeTransport))return false;
    if(s.nativeAdaptiveFreshSampling&&(!s.nativeScreenProbeTransport||s.contact.raysPerPixel!=8))return false;
    if(s.nativeForceRasterCore&&!s.nativeScreenProbeTransport)return false;
    if(s.nativeSyntheticReceiverFilterSupport&&!s.nativeScreenProbeTransport)return false;
    if(s.nativeReceiverFilterRiskWeights&&!s.nativeScreenProbeTransport)return false;
    if(s.nativeReceiverFilterSupportHalo&&!s.nativeScreenProbeTransport)return false;
    if(s.nativeTemporalContactMetadata&&!s.nativeScreenProbeTransport)return false;
    if(s.nativeScreenProbeTransport&&(!s.splitReceiverGather||!s.coherentReceiverTracing||!s.deferredReceiverMix||
        !s.safeProbeOrigins||!s.useScreenProbes||(!s.contact.enabled&&!s.ueLumenHigh)))return false;
    if(s.deferredReceiverMix&&(!s.splitReceiverGather||!s.coherentReceiverTracing))return false;
    for(unsigned c=0;c<4;++c)if(!std::isfinite(s.diagnosticReceiverClipPlane[c]))return false;
    return (!s.compactReceiverTracing||(s.splitReceiverGather&&s.coherentReceiverTracing&&s.sparseReceiverSampling))&&s.receiverLatticeMax>=1&&s.receiverLatticeMax<=4&&(s.transportTexelStride==1||s.transportTexelStride==2||s.transportTexelStride==4)&&s.transportRays>=4&&s.transportRays<=64&&s.transportUpdatePeriod>=1&&s.transportUpdatePeriod<=8&&std::isfinite(s.receiverRiskDistance)&&s.receiverRiskDistance>=.03f&&s.receiverRiskDistance<=.5f&&(s.cachedAreaSamplesPerAxis==0||s.cachedAreaSamplesPerAxis==4||s.cachedAreaSamplesPerAxis==8)&&s.view<=6&&s.reconstructionDebug<=4&&s.raysPerPixel>=1&&s.raysPerPixel<=32&&ValidateSurfaceCacheTemporalSettings(s.temporal)&&ValidateSurfaceCacheSpatialSettings(s.spatial)&&ValidateSurfaceCacheContactSettings(s.contact)&&
        ValidateAreaLightShadowSettings(s.areaShadows)&&s.areaShadows.nearPlane<s.maxDistance&&
        std::isfinite(s.maxDistance)&&s.maxDistance>=.1f&&s.maxDistance<=100&&
        std::isfinite(s.intensity)&&s.intensity>=0&&s.intensity<=10&&
        std::isfinite(s.rayBias)&&s.rayBias>=.0001f&&s.rayBias<=.2f&&
        std::isfinite(s.originSafetyFraction)&&s.originSafetyFraction>=.05f&&s.originSafetyFraction<=.95f&&
        std::isfinite(s.originSafetyEpsilon)&&s.originSafetyEpsilon>=.000001f&&s.originSafetyEpsilon<=.001f&&
        (!(s.enabled||s.areaLightShadows)||(s.cache&&s.scene));
}
struct SurfaceCacheGiConstants {
    glm::mat4 inverseView{1};
    glm::ivec4 atlas{1024,1024,0,0}; // width, plane stride, texel count, cards
    glm::ivec4 geometry{0,1024,0,0}; // triangles offset, width, nodes, triangles
    glm::ivec4 screen{1,1,1,0};
    glm::vec4 parameters{30,.01f,1,0};
    glm::vec4 sunDirectionIntensity{0,-1,0,0},sunColor{1};
    SpotLightConstants spot;
};
static_assert(sizeof(SurfaceCacheGiConstants)==208);
inline constexpr std::uint32_t SurfaceCacheGiBinding=12;
struct SurfaceCacheOriginSafetyConstants {
    glm::vec4 safety{.5f,.00002f,0,0}; // clearance fraction, numeric clearance, directly rasterized source positions, reserved
};
static_assert(sizeof(SurfaceCacheOriginSafetyConstants)==16);
inline constexpr std::uint32_t SurfaceCacheOriginSafetyBinding=15;
struct SurfaceCacheTemporalConstants {
    glm::mat4 currentInverseView{1},previousViewProjection{1},previousView{1};
    glm::ivec4 screen{1,1,0,10}; // width, height, history/motion flags and display fade, frame limit
    glm::vec4 reject{.01f,.70710678f,8,3}; // distance, normal cosine, moving frame limit, motion pixels
    glm::vec4 clamp{6,2,.002f,2}; // sigma, clamp mode (0 off / 1 fast / 2 moving), distance floor, edge cap (0 disables)
    glm::vec4 recovery{.75f,3,4,.08f}; // young-history strength, pixel radius, frame limit, albedo tolerance
    glm::vec4 dynamic{0,4,2,0}; // moving lighting, reactive/stop history cap, clamp sigma, coherent coverage confidence
};
static_assert(sizeof(SurfaceCacheTemporalConstants)==272);
inline constexpr std::uint32_t SurfaceCacheTemporalBinding=13;
struct SurfaceCacheSpatialConstants {
    glm::ivec4 options{0,1,1,0}; // pass, radius, visibility test, safe origins available
    glm::vec4 filter{1000,.174532925f,.8660254f,1}; // depth scale, hit radians, normal cosine, blend
    glm::vec4 parameters{.01f,.01f,0,1}; // ray bias, relative plane tolerance, numeric endpoint clearance, local adaptive neighbors
};
static_assert(sizeof(SurfaceCacheSpatialConstants)==48);
inline constexpr std::uint32_t SurfaceCacheSpatialBinding=14;
struct SurfaceCacheContactConstants {
    glm::vec4 parameters{.1f,.17632698f,.36397023f,.05f}; // near-hit limit, tan(parallax start/end), risky weight fraction
    glm::ivec4 options{8,1,1,0}; // actual receiver rays, local-filter pixel radius, parallel-layer validation, reserved
    glm::vec4 filter{1,.08f,.9659258263f,.00002f}; // strength, albedo tolerance, normal cosine, numeric clearance
    glm::uvec4 sampling{0}; // base seed, committed GI sequence, independent contact directions, continuous confidence
    glm::vec4 guide{.17632698f,4,1,1}; // tan(hit angle), maximum radius, receiver screen detection, coherent probe risk
};
static_assert(sizeof(SurfaceCacheContactConstants)==80);
inline constexpr std::uint32_t SurfaceCacheContactBinding=14; // spatial pass has finished before this block is bound
}
