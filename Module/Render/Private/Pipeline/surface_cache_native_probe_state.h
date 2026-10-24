#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include "pipeline_resource_builder.h"
#include "Render/Public/Compute/gpu_compute.h"
#include "surface_cache_native_screen_probe_shaders.h"
#include "surface_cache_probe_shaders.h"
#include "surface_cache_probe_coefficient_shaders.h"
#include "surface_cache_native_base_transport_shaders.h"
#include "surface_cache_native_spatial_shaders.h"
#include "surface_cache_native_reconstruction_shaders.h"
#include "ue_lumen_radiance_cache_shaders.h"

namespace Render::PipelineDetail {
// Target is the enclosing renderer's framebuffer/texture/extent value type.
// Keep the optional experiment's complete resource lifetime in one member:
// NativeProbeTransportState<Target> nativeProbeTransport;
template<class Target> struct NativeProbeTransportState {
    struct UeRadianceCacheState {
        std::shared_ptr<Compute::Device> computeOwner;
        std::array<Compute::PipelineHandle,8> updatePrograms;
        std::array<Compute::PipelineHandle,2> tracePrograms;
        Compute::PipelineHandle resolveProgram;
        Compute::PipelineHandle adaptiveResetProgram,adaptiveAllocateProgram;
        Compute::BufferHandle control,probeState,freeList,traceQueue,usedMarks,traceAudit;
        std::array<Compute::BufferHandle,2> indirection;
        std::shared_ptr<const void> targetsOwner,programsOwner;
        Target rawAtlas,rawDepth,finalAtlas,indirectionView;
        RenderResourceHandle<UniformBufferSpec> constantsBuffer;
        RenderResourceHandle<PipelineSpec> screenTraceProgram,screenTraceDiagnosticProgram,screenPackProgram;
        struct FrameState {bool valid=false;unsigned image=0;std::uint32_t frame=0;std::uint64_t lightingKey=0;
            std::array<glm::vec4,4> corners{};};
        FrameState completed,pending;
        bool recorded=false,queryDiagnostics=false;
    } ueRadianceCache;
    std::shared_ptr<const void> programsOwner,targetsOwner;
    RenderResourceHandle<PipelineSpec> traceProgram,traceDiagnosticProgram,packProgram;
    RenderResourceHandle<PipelineSpec> integrateProgram,activeMaskProgram,neighborsProgram,neighborsDiagnosticProgram;
    RenderResourceHandle<PipelineSpec> coefficientFilterProgram,extractProgram,reconstructProgram;
    RenderResourceHandle<PipelineSpec> receiverCoreProgram,receiverLowProgram,receiverFullProgram;
    RenderResourceHandle<PipelineSpec> receiverReconstructionProgram,receiverDiagnosticProgram;
    std::shared_ptr<const void> forceRasterCoreProgramsOwner;
    RenderResourceHandle<PipelineSpec> forceRasterCoreProgram,forceRasterLowProgram,forceRasterFullProgram;
    RenderResourceHandle<PipelineSpec> forceRasterReconstructionProgram,forceRasterDiagnosticProgram,forceRasterClassificationProgram;
    bool forceRasterCoreProgramsAdaptiveFresh=false;
    std::shared_ptr<Compute::Device> receiverHaloComputeOwner;
    Compute::PipelineHandle receiverHaloHorizontal,receiverHaloVertical;
    std::shared_ptr<const void> receiverHaloTargetsOwner,receiverHaloDiagnosticOwner;
    Target receiverHaloScratch;
    RenderResourceHandle<PipelineSpec> receiverHaloDiagnosticProgram;
    bool receiverHaloDiagnosticAdaptiveFresh=false,receiverHaloDiagnosticForceRasterCore=false;
    std::shared_ptr<const void> adaptiveProgramsOwner,adaptiveMaskProgramsOwner,adaptiveMaskOwner,receiverDiagnosticPagesOwner;
    RenderResourceHandle<PipelineSpec> adaptiveCoreProgram,adaptiveLowProgram,adaptiveFullProgram,adaptiveDiagnosticProgram,adaptiveReconstructionProgram;
    std::array<std::array<RenderResourceHandle<PipelineSpec>,2>,2> freshMaskPrograms;
    Target freshMaskTarget;
    RenderResourceHandle<RHITextureSpec> freshMaskSamplingIdentity;
    std::array<Target,4> receiverDiagnosticPages;
    RenderResourceHandle<UniformBufferSpec> spatialBuffer;
    std::array<Target,2> packedRays,coefficients;
    std::array<RenderResourceHandle<RHITextureSpec>,4> traceParts;
    Target traceOutputs,origins,activeMask,coefficientRaw,irradiance,neighbors,NativeBaseGI;
    // Contact risk uses raw geometric trace records, never filtered RGB.
    Target NativeProbeRisk,NativeSoftMask,NativeRawMask;
    std::shared_ptr<const void> contactRiskOwner,contactRiskProgramsOwner;
    RenderResourceHandle<PipelineSpec> contactRiskProgram,contactMaskProgram,contactMaskExpandProgram;
    RenderResourceHandle<UniformBufferSpec> contactRiskBuffer;
    std::shared_ptr<const void> reconstructionProgramsOwner,reconstructionTargetOwner,reconstructionComparisonOwner;
    RenderResourceHandle<PipelineSpec> reconstructionMaskProgram;
    Target reconstructionMaskTarget;
    RenderResourceHandle<RHITextureSpec> reconstructionRawIdentity;
    std::array<Target,4> reconstructionComparison; // old Base/raw, MRT Base/raw
    Target reconstructionComparisonMrt,reconstructionFailedOrigins;
    bool baseReconstructionDeferred=false,reconstructionComparisonRecorded=false;
    bool recorded=false,traceMetadataDiagnostics=false,targetsUseUe=false;
    bool compactPipelineUsesNativeBase=false;
    bool compactPipelineUsesAdaptiveFresh=false;
    bool compactPipelineUsesForceRasterCore=false;
    bool compactPipelineUsesSupportHalo=false;
    std::uint32_t rayImage=0,probeImage=0;
    // Capacity includes inactive reserved probes and packed-atlas padding.
    // Actual native queries are obtained only from diagnostic metadata.xy.
    std::uint64_t rayCapacity=0,logicalRayBudget=0,coefficientFilterTexels=0;
};
} // namespace Render::PipelineDetail
