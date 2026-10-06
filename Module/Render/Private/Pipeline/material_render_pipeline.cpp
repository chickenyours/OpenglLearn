#include "Render/Public/Pipeline/material_render_pipeline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <glm/gtc/matrix_inverse.hpp>
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Private/Pipeline/post_process_shaders.h"
#include "Render/Private/Pipeline/temporal_effects_shaders.h"
#include "Render/Private/Pipeline/indirect_lighting_shaders.h"
#include "Render/Private/Pipeline/realtime_gi_shaders.h"
#include "Render/Private/Pipeline/lumen_gi_shaders.h"
#include "Render/Public/Pipeline/surface_pass.h"
#include "Render/Public/Pipeline/view_frustum.h"

namespace Render {
namespace {
void Require(bool result, const char* message) { if (!result) throw std::runtime_error(message); }
bool Finite(const glm::mat4& matrix) {
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
        if (!std::isfinite(matrix[c][r])) return false;
    return true;
}
bool Finite(const glm::vec3& value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
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
    Target indirectPositions;
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
        bool valid = false, taaValid = false;
    } completed, pending;
    std::uint64_t nextToken = 0, pendingToken = 0;
    RenderResourceHandle<RHITextureSpec> lastBloom;
    PipelineStatistics statistics;
    ForwardRenderPipeline forward;

    explicit Impl(RHIDevice& device) : device(device) {}
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
        scene = newScene; reflection = newReflection; shadows = newShadows;
        sceneMsaa = newSceneMsaa; reflectionMsaa = newReflectionMsaa;
        historyColor = newHistoryColor; historyDepth = newHistoryDepth;
        filtered = newFiltered; blurred = newBlurred;
        opaqueSnapshot = newOpaque; diffuseCapture = newDiffuse; normalCapture = newNormal;
        opaqueSnapshotDepth = newOpaqueDepth; reflectionSnapshot = newReflectionSnapshot; reflectionSnapshotDepth = newReflectionDepth;
        indirectHalf = newIndirectHalf; indirectFull = newIndirectFull;
        indirectWork = newIndirectWork;
        indirectPositions = newIndirectPositions;
        aoRaw = newAoRaw; aoBlurred = newAoBlurred; display = newDisplay;
        bloomDown = newDown; bloomWork = newWork;
        targetsOwner = std::move(newOwner);
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
            if ((item.EffectiveLayer() == RenderLayer::Overlay) != overlayOnly) continue;
            if (transparency >= 0 && (item.EffectiveLayer() == RenderLayer::Transparent) != (transparency == 1)) continue;
            if (reflectionOnly && !item.visibleInReflections) continue;
            frame.items.push_back(item);
            frame.items.back().viewDepth = -(camera.view * item.model * glm::vec4(0, 0, 0, 1)).z;
        }
        Require(forward.Record(encoder, frame, View(camera)), "Cannot record material scene");
    }
    void CaptureSurface(RHIFrameEncoder& encoder, const RenderFrame& frame, const PipelineCamera& camera, bool normals) {
        const ViewConstants view{camera.projection * camera.view, glm::vec4(camera.position, 1)};
        Upload(encoder, viewBuffer, &view, sizeof(view), 0);
        for (const auto& item : frame.items) {
            if (!item.material || (item.EffectiveLayer() != RenderLayer::Opaque && item.EffectiveLayer() != RenderLayer::Cutout)) continue;
            const auto& resources = item.material->Resources();
            const auto variant = normals ? resources.normalPipeline : resources.diffuseRadiancePipeline;
            if (!variant.IsValid()) continue;
            Require(encoder.KeepAlive(item.material) && encoder.BindPipeline(variant), "Cannot bind indirect source variant");
            for (const auto& binding : resources.textures)
                Require(encoder.BindTexture(binding.texture, binding.slot), "Cannot bind indirect source texture");
            const auto& bytes = item.material->Parameters().Bytes();
            if (!bytes.empty()) Upload(encoder, resources.parameterBuffer, bytes.data(), static_cast<std::uint32_t>(bytes.size()), 2);
            Upload(encoder, objectBuffer, &item.model, sizeof(item.model), 1);
            Require(encoder.BindMesh(item.mesh) && encoder.DrawIndexed(item.draw), "Cannot draw indirect source");
        }
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
                encoder.BindTexture(scene.depth, 2) && encoder.BindTexture(previousDepth, 3) &&
                encoder.BindTexture(constants.reactive.x > 0.5f ? opaqueSnapshot.color : RenderResourceHandle<RHITextureSpec>{}, 4) &&
                encoder.DrawIndexed(RHICommand::DrawIndexed{3}), "Cannot record temporal pass");
        ++statistics.postPasses;
    }
    // No object velocity buffer yet. Reset accumulation when geometry/material
    // content changes. Live texture uploads require the caller's ResetHistory().
    std::uint64_t ContentHash(const RenderFrame& frame, const MaterialPipelineSettings& settings) const {
        std::uint64_t hash = 14695981039346656037ull;
        const auto bytes = [&](const void* data, std::size_t count) {
            const auto* p = static_cast<const unsigned char*>(data);
            for (std::size_t i = 0; i < count; ++i) { hash ^= p[i]; hash *= 1099511628211ull; }
        };
        const auto add = [&](const auto& value) { bytes(&value, sizeof(value)); };
        for (const auto& item : frame.items) {
            if (item.layer == RenderLayer::Overlay) continue;
            add(item.mesh); add(item.EffectivePipeline()); add(item.texture); add(item.model);
            add(item.draw.indexCount); add(item.draw.firstIndex); add(item.draw.baseVertex);
            add(item.draw.instanceCount); add(item.draw.firstInstance);
            add(item.layer); add(item.castsShadows); add(item.visibleInReflections);
            if (item.material) {
                add(item.material->Resources().shadowPipeline);
                add(item.material->Resources().normalPipeline); add(item.material->Resources().diffuseRadiancePipeline);
                const auto& values = item.material->Parameters().Bytes(); bytes(values.data(), values.size());
                for (const auto& texture : item.material->Resources().textures) { add(texture.slot); add(texture.texture); }
            }
        }
        add(settings.shadows.enabled); add(settings.shadows.sunDirection); add(settings.shadows.sunColor);
        add(settings.shadows.sunIntensity); add(settings.shadows.distance); add(settings.shadows.splitLambda);
        add(settings.shadows.depthBias); add(settings.shadows.normalBias); add(settings.shadows.cascadeBlend);
        add(settings.shadows.depthBiasTexels); add(settings.shadows.normalBiasTexels);
        add(settings.shadows.distanceFade); add(settings.shadows.receiverPlaneClampTexels);
        add(settings.shadows.casterPadding); add(settings.shadows.debugView);
        add(settings.reflection.enabled); add(settings.reflection.plane); add(settings.reflection.strength);
        add(settings.reflection.receiverBounds.has_value());
        if(settings.reflection.receiverBounds) {
            add(settings.reflection.receiverBounds->minimum);add(settings.reflection.receiverBounds->maximum);
        }
        add(settings.post.ssaoEnabled); add(settings.post.ssaoRadius); add(settings.post.ssaoBias); add(settings.post.ssaoPower);
        add(settings.lighting.ambientAndExposure.x); add(settings.lighting.ambientAndExposure.y); add(settings.lighting.ambientAndExposure.z);
        add(settings.lighting.specularAA);
        const auto areas = MakeAreaLightsConstants(settings.areaLights); bytes(&areas, sizeof(areas));
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
        Require(ValidatePostProcessSettings(settings.post, &error), "Invalid post-process settings");
        Require(ValidateTemporalEffectsSettings(settings.temporal, &error), "Invalid temporal settings");
        Require(ValidateAreaLightsSettings(settings.areaLights, &error), "Invalid area light settings");
        Require(ValidateSpotLightSettings(settings.spotLight, &error), "Invalid spot light settings");
        Require(ValidateIndirectLightingSettings(settings.indirect, &error), "Invalid indirect lighting settings");
        Require(ValidateRealtimeGiSettings(settings.realtimeGi,&error),"Invalid realtime GI settings");
        Require(ValidateLumenGiSettings(settings.lumenGi,&error),"Invalid hybrid Lumen GI settings");
        const bool cacheView=settings.lumenGi.lightingView!=LumenLightingView::Material;
        Require(!cacheView||(settings.lumenGi.enabled&&!settings.lumenGi.reflections&&!settings.reflection.enabled),
                "Surface Cache views require Lumen GI with reflections disabled");
        if(cacheView)for(const auto& item:frame.items)
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
    #include "Render/Private/Pipeline/lumen_gi_pipeline.inl"
    void Record(RHIFrameEncoder& encoder, const RenderFrame& frame, const PipelineCamera& camera,
                const MaterialPipelineSettings& requested) {
        Validate(frame, camera, requested);
        auto settings=requested;
        if(settings.lumenGi.enabled) {
            const auto& lm=settings.lumenGi;auto& rt=settings.realtimeGi;
            rt.enabled=true;rt.scene=lm.scene->Geometry();rt.origin=lm.origin;rt.spacing=lm.spacing;rt.counts=lm.counts;
            rt.maxDistance=lm.maxDistance;rt.rayBias=lm.rayBias;rt.intensity=lm.intensity;rt.bounceFeedback=0;
            rt.reset=lm.reset;rt.historyWeight=.7f;
        }
        Require(encoder.IsRecording(), "Pipeline needs a recording frame encoder");
        auto skyAssets=settings.sky;if(settings.lumenGi.enabled&&settings.lumenGi.reflections)skyAssets.enabled=true;
        PrepareSky(skyAssets);
        PrepareProbes(settings.probes);
        PrepareRealtimeGi(settings.realtimeGi);realtimePending={};
        PrepareLumen(settings.lumenGi);lumenPending={};
        Require(encoder.KeepAlive(assetsOwner) && encoder.KeepAlive(targetsOwner), "Cannot pin pipeline resources");
        if (skyOwner) Require(encoder.KeepAlive(skyOwner), "Cannot pin sky environment resources");
        if (probeOwner) Require(encoder.KeepAlive(probeOwner), "Cannot pin diffuse probe resources");
        if(settings.realtimeGi.enabled) Require(encoder.KeepAlive(realtimeOwner)&&encoder.KeepAlive(realtimeGeometryOwner),"Cannot pin realtime GI resources");
        if(settings.lumenGi.enabled)Require(encoder.KeepAlive(lumenProgramsOwner)&&encoder.KeepAlive(lumenSceneOwner)&&encoder.KeepAlive(lumenCacheOwner)&&encoder.KeepAlive(lumenDistanceOwner)&&encoder.KeepAlive(lumenViewOwner),"Cannot pin hybrid GI resources");
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
        statistics = {};
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
        temporal.reactive.x = hasTransparent ? 1.0f : 0.0f;

        SceneEffectsConstants effects{};
        effects.mainView = camera.view;
        effects.sunDirectionIntensity = glm::vec4(glm::normalize(settings.shadows.sunDirection), settings.shadows.sunIntensity);
        effects.sunColor = glm::vec4(settings.shadows.sunColor, 1);
        effects.effects = glm::vec4(settings.shadows.enabled ? 1 : 0, 0, 0, float(atlasResolution));
        effects.shadowParams = glm::vec4(settings.shadows.depthBias, settings.shadows.normalBias,
                                         settings.shadows.cascadeBlend, settings.reflection.strength);
        effects.screenAndAo = glm::vec4(1.0f / width, 1.0f / height, 0, float(settings.shadows.debugView));
        const auto cascades = BuildCascades(camera.view, camera.projection, camera.nearPlane, camera.farPlane,
            settings.shadows.sunDirection, settings.shadows.distance, settings.shadows.splitLambda,
            atlasResolution, settings.shadows.casterPadding, settings.shadows.cascadeBlend);
        effects.lightViewProjection = cascades.lightViewProjection;
        effects.cascadeSplits = cascades.cascadeSplits;
        effects.cascadeWorldTexelSize = cascades.cascadeWorldTexelSize;
        effects.cascadeInverseDepthRange = cascades.cascadeInverseDepthRange;
        effects.shadowFilter = glm::vec4(settings.shadows.depthBiasTexels, settings.shadows.normalBiasTexels,
            settings.shadows.distanceFade, settings.shadows.receiverPlaneClampTexels);
        Effects(encoder, effects);

        Require(encoder.BindTexture({}, 12) && encoder.BindTexture({}, 13) && encoder.BindTexture({}, 14), "Cannot reset pass textures");
        Require(encoder.BindTexture({}, 8) && encoder.BindTexture({}, 9) && encoder.BindTexture({}, 15), "Cannot reset advanced pass textures");
        if(settings.lumenGi.enabled)RecordLumenSurface(encoder,settings);
        // Opaque cache diagnostics reconstruct a traced receiver directly;
        // no transparency/reflection or irradiance fallback consumes probes.
        if(!settings.lumenGi.enabled||settings.lumenGi.lightingView==LumenLightingView::Material) {
            RecordRealtimeGi(encoder,settings,settings.lumenGi.enabled?lumenWorldProbes:RenderResourceHandle<PipelineSpec>{});
            if(settings.lumenGi.enabled)statistics.lumenGiPasses+=2;
        }
        // Clear even when disabled: diagnostics never expose uninitialized depth.
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
        const bool mirrorEnabled=settings.reflection.enabled&&
            (!settings.reflection.receiverBounds||ViewFrustum(camera.projection*camera.view).Intersects(
                settings.reflection.receiverBounds->minimum,settings.reflection.receiverBounds->maximum));
        // Switching away from the shadow attachment must precede binding its
        // depth texture. The culled path initializes only the resolved mirror.
        BindTarget(encoder,mirrorEnabled?reflectionTarget:reflection,RHICommand::ClearColor|RHICommand::ClearDepth,
                   glm::vec4(0.006f,0.009f,0.015f,1));
        Require(encoder.BindTexture(shadows.depth, 12), "Cannot bind CSM atlas");
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
        if (hasTransparent) {
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

        auto hdrColor = scene.color;
        if (useTaa) {
            TemporalPass(encoder, historyColor[writeHistory], taa, hdrColor,
                historyValid ? historyColor[completed.image].color : RenderResourceHandle<RHITextureSpec>{},
                historyValid ? historyDepth[completed.image].depth : RenderResourceHandle<RHITextureSpec>{}, temporal);
            // The destination is depth-only; CompareAlways + gl_FragDepth keeps
            // exact Depth32F history for disocclusion rejection.
            TemporalPass(encoder, historyDepth[writeHistory], depthCopy, {}, {}, {}, temporal);
            hdrColor = historyColor[writeHistory].color;
            ++statistics.temporalPasses;
        }
        if (settings.temporal.motionBlurEnabled && !shadowDebug) {
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
        Fullscreen(encoder, display, composite, hdrColor, lastBloom, {}, post);
        Fullscreen(encoder, filtered, filter, display.color, {}, {}, post);
        Target screenTarget;
        screenTarget.width = settings.outputSize.x ? settings.outputSize.x : width;
        screenTarget.height = settings.outputSize.y ? settings.outputSize.y : height;
        const bool resample = screenTarget.width != width || screenTarget.height != height;
        if (resample) {
            auto presentation = filtered.color;
            if (settings.temporal.antialiasing == AntialiasingMode::FXAA) {
                // FXAA uses internal pixel neighborhoods. Reuse the now-dead
                // display target rather than adding a native-size intermediate.
                TemporalPass(encoder, display, fxaa, filtered.color, {}, {}, temporal);
                presentation = display.color;
            }
            Fullscreen(encoder, screenTarget, spatialUpsample, presentation, {}, {}, post);
        } else if (settings.temporal.antialiasing == AntialiasingMode::FXAA)
            TemporalPass(encoder, screenTarget, fxaa, filtered.color, {}, {}, temporal);
        else Fullscreen(encoder, screenTarget, copy, filtered.color, {}, {}, post);
        Scene(encoder, frame, camera, false, true); // Overlay bypasses HDR/post effects.
        pending = {};
        pending.camera = camera; pending.jitteredVP = renderCamera.projection * camera.view;
        pending.contentHash = contentHash; pending.sequence = sequence + 1;
        pending.image = writeHistory; pending.mode = settings.temporal.antialiasing;
        pending.valid = true; pending.taaValid = useTaa;
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
    catch (const std::exception& error) { impl_->error = error.what(); return false; }
}
void MaterialRenderPipeline::Shutdown() {
    impl_->targetsOwner.reset(); impl_->assetsOwner.reset();
    impl_->skyOwner.reset(); impl_->skyEnvironment.reset();
    impl_->probeOwner.reset(); impl_->probeVolume.reset();
    impl_->realtimeOwner.reset();impl_->realtimeGeometryOwner.reset();impl_->realtimeScene.reset();
    impl_->realtimeCompleted={};impl_->realtimePending={};
    impl_->lumenProgramsOwner.reset();impl_->lumenSceneOwner.reset();impl_->lumenCacheOwner.reset();impl_->lumenDistanceOwner.reset();impl_->lumenViewOwner.reset();impl_->lumenScene.reset();
    impl_->lumenCompleted={};impl_->lumenPending={};
    impl_->lumenReactiveFrames=0;impl_->lumenGeometryChanged=impl_->realtimeGeometryChanged=false;
    impl_->width = impl_->height = impl_->atlasResolution = 0;
    impl_->lastBloom = {};
    impl_->completed = {}; impl_->pending = {}; impl_->pendingToken = 0;
}
std::uint64_t MaterialRenderPipeline::RecordedFrameToken() const noexcept { return impl_->pendingToken; }
bool MaterialRenderPipeline::CompleteFrame(std::uint64_t token) {
    if (!token || token != impl_->pendingToken) { impl_->error = "Stale or absent frame completion token"; return false; }
    impl_->completed = impl_->pending; impl_->pending = {}; impl_->pendingToken = 0;
    impl_->realtimeCompleted=impl_->realtimePending;impl_->realtimePending={};
    impl_->lumenCompleted=impl_->lumenPending;impl_->lumenPending={};
    return true;
}
bool MaterialRenderPipeline::DiscardFrame(std::uint64_t token) {
    if (!token || token != impl_->pendingToken) { impl_->error = "Stale or absent discarded frame token"; return false; }
    impl_->pending = {}; impl_->pendingToken = 0;
    impl_->realtimePending={};
    impl_->lumenPending={};
    return true;
}
void MaterialRenderPipeline::ResetHistory() {
    impl_->completed.valid = impl_->completed.taaValid = false;
    impl_->pending.valid = impl_->pending.taaValid = false;
    impl_->lumenCompleted.viewValid=false;impl_->lumenPending.viewValid=false;
}
const std::string& MaterialRenderPipeline::LastError() const noexcept { return impl_->error; }
PipelineStatistics MaterialRenderPipeline::Statistics() const { return impl_->statistics; }
PipelineDebugTextures MaterialRenderPipeline::DebugTextures() const {
    if (!impl_->targetsOwner) return {};
    return {impl_->scene.color, impl_->scene.depth, impl_->shadows.depth,
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
}
} // namespace Render
