#pragma once
#include "Render/Public/Pipeline/strand_rendering.h"
#include "Render/Public/Material/pbr_material.h"
#include "Render/Public/Pipeline/strand_lighting.h"
#include "Render/Public/Pipeline/strand_direct_lighting.h"
#include "Render/Public/RHICommand/FrameCommand/rhi_frame_encoder.h"

namespace Render::PipelineDetail {
struct StrandPassTarget {
    RenderResourceHandle<RenderTargetSpec> framebuffer;
    RenderResourceHandle<RHITextureSpec> color, depth;
    std::uint32_t width = 0, height = 0, samples = 1;
};
struct StrandPassView {
    glm::mat4 viewProjection{1};
    glm::vec3 cameraPosition{0};
    glm::vec3 cameraForward{0,0,-1};
    // Scene display units per physical metre, shared with skin diffusion.
    float worldUnitsPerMetre=1.f;
    // Invalidate the screen-visible opaque blocker shell when opaque geometry
    // changes without a camera/depth resource change. Not a fiber density key.
    std::uint64_t opaqueContentRevision=0;
    // Optional stable blocker camera. TAA jitter must not rebuild the visible
    // shell, but real camera/depth-content changes still invalidate it.
    glm::mat4 blockerUnjitteredViewProjection{1};
    bool blockerStableViewValid=false;
    glm::vec3 sunDirection{0, -1, 0}, sunColor{1};
    float sunIntensity = 0;
    Material::PbrPassConstants lighting;
    // The same precomputed environment as opaque PBR surfaces. No GI gather.
    SkyLightConstants sky;
    StrandIndirectLightingSettings indirect;
    StrandSourceDirectSettings sourceDirect;
    SceneEffectsConstants effects;
    RenderResourceHandle<RHITextureSpec> shadowAtlas;
    // Configurable nearest depth groups plus a weighted tail. Equal-depth fragments
    // share a group with product coverage. The independent, resolved
    // opaque depth is sampled only by the single-sample layered path.
    bool frontLayerTransparency=false;
    std::uint32_t frontDepthGroups=2; // Supported: 2, 3, 7; not native packed MLAB.
    // Two additive MRT outputs replace separate color/reveal draws. Uses FP32
    // weighted-color scratch (+8 bytes/pixel) and hardware Equal/Greater depth
    // selection. Single-sample layered path only; false preserves legacy A/B.
    bool pairedLayerTransparency=false;
    bool nativeOit=false;
    std::uint32_t nativeOitLayers=4;
    // Optional diagnostic scratch-reference cap; zero uses the bounded auto budget.
    std::uint32_t nativeOitPairBudget=0;
    RenderResourceHandle<RHITextureSpec> opaqueDepth;
    // Independent single-sample outputs; opaqueDepth must not alias an active
    // attachment. The source authored high-coverage depth threshold is .95.
    bool outputTemporalData=false;
    float depthWriteThreshold=.95f;
    bool outputMotionData=false;
    bool motionHistoryValid=false;
    glm::mat4 previousViewProjection{1},currentUnjitteredViewProjection{1},previousUnjitteredViewProjection{1};
    glm::vec3 previousCameraPosition{0};
    // Optional host geometry shutter: 1=current, 0=last executed sample.
    // Width/LOD/topology provenance is carried by each item's validity flags.
    float shutterSample=1.f;
    // Resolve straight scene-linear RGBA without blending over the destination.
    // Used by independent time samples, before their premultiplied average.
    bool outputStraightColor=false;
    // Diagnostic scene-linear components use the same coverage/compositing as
    // Beauty. The caller isolates their temporal histories from normal color.
    StrandDebugMode debugMode=StrandDebugMode::Beauty;
};
struct StrandTemporalTextures {
    // nearestDepth: any surviving hair fragment, independent of threshold.
    // coverage.R: 1-product(T), G: high-coverage hair selected for depth.
    // compositeDepth: min(opaque, threshold-qualified nearest hair), raw grid.
    RenderResourceHandle<RHITextureSpec> nearestDepth,coverage,compositeDepth,motion,resolvedColor;
    std::uint32_t width=0,height=0;
};
struct StrandVolumeStatistics {
    std::uint32_t passes=0, resolution=0, voxelCount=0, updated=0;
    std::uint32_t blockerPasses=0,blockerUpdated=0;
    std::uint64_t segments=0;
    std::uint64_t nativeInputSegments=0,nativeInjectedSegments=0;
    float nativeInjectionFraction=0,nativeRasterWidthScale=0,nativeAverageWidth=0; // last native item
    float effectiveVoxelSize=0;
    float effectiveBiasMetres=0; // maximum-axis world scale, like effectiveVoxelSize
};
struct StrandVolumeDebugTextures {
    RenderResourceHandle<RHITextureSpec> density,paths,pathsExtra,pathsAmbient;
    RenderResourceHandle<RHITextureSpec> opaqueBlockers,directVisibility,extraVisibility;
    std::uint32_t width=0,height=0;
};
struct StrandSegmentShadingStatistics {
    std::uint32_t passes=0;
    std::uint32_t computePasses=0,rasterPasses=0;
    std::uint64_t points=0,outputPoints=0; // segment evaluations, endpoint slots
};
struct StrandSegmentShadingDebugTextures {
    RenderResourceHandle<RHITextureSpec> color;
    std::uint32_t width=0,height=0;
};
struct StrandCurveStatistics {
    std::uint64_t originalSegments=0,visibleSegments=0,curvePieces=0;
    std::uint32_t uploads=0;
};
struct StrandCurveDebugTextures {
    RenderResourceHandle<RHITextureSpec> atlas;
    glm::uvec4 layout{0},offsets{0};
    std::uint32_t pieces=0;
};
struct StrandNativeOitDebugBuffers;
class StrandRenderPass {
public:
    explicit StrandRenderPass(RHIDevice& device);
    ~StrandRenderPass();
    void Record(RHIFrameEncoder& encoder, std::span<const StrandRenderItem> items,
                const StrandPassTarget& target, const StrandPassView& view);
    void Shutdown();
    const StrandVolumeStatistics& VolumeStatistics() const;
    StrandVolumeDebugTextures VolumeDebugTextures() const;
    StrandSegmentShadingStatistics SegmentShadingStatistics() const;
    StrandSegmentShadingDebugTextures SegmentShadingDebugTextures() const;
    const StrandCurveStatistics& CurveStatistics() const;
    StrandCurveDebugTextures CurveDebugTextures() const;
    bool NativeOitActive() const;
    std::uint32_t NativeOitComputePasses() const;
    std::uint32_t NativeOitLayers() const;
    std::uint64_t NativeOitBytes() const;
    StrandNativeOitDebugBuffers NativeOitDebug() const;
    std::array<std::uint32_t,4> CaptureNativeOitCounters() const;
    // Transparency only: paired 3*groups+2, legacy 4*groups+3 (or weighted3).
    // Raster passes traverse the complete groom: paired 2*groups+1 versus
    // legacy 3*groups+2. Shading still occurs once per surviving fragment;
    // exact-depth ties retain weighted color and full product coverage.
    // Auxiliary temporal passes
    // are reported separately; no extra work unless outputTemporalData is set.
    std::uint32_t PassCount() const;
    std::uint32_t TransparencyRasterPassCount() const;
    bool PairedLayerTransparencyActive() const;
    std::uint32_t FrontDepthGroups() const;
    std::uint32_t TemporalPassCount() const;
    std::uint32_t MotionPassCount() const;
    std::uint32_t ColourResolvePassCount() const;
    StrandTemporalTextures TemporalTextures() const;
    bool TemporalDataMultisampleFallback() const;
    bool FrontLayerTransparencyActive() const;
    bool MultisampleTransparencyFallback() const;
    // Required if a caller discards/cancels recorded commands before GPU
    // submission: the cache includes work recorded by the last Record call.
    void InvalidateVolume();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace Render::PipelineDetail
