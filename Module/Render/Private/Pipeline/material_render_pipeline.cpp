#include "Render/Public/Pipeline/material_render_pipeline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <glm/gtc/matrix_inverse.hpp>
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Private/Pipeline/post_process_shaders.h"
#include "Render/Private/Pipeline/temporal_effects_shaders.h"

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
    RenderResourceHandle<PipelineSpec> extract, downsample, upsample, ssao, ssaoBlur, composite, filter;
    RenderResourceHandle<PipelineSpec> copy, fxaa, taa, depthCopy, motionBlur;
    Target scene, reflection, sceneMsaa, reflectionMsaa, shadows, aoRaw, aoBlurred, display, filtered, blurred;
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
        fxaa = builder.FullscreenPipeline(vertex, FxaaShader());
        taa = builder.FullscreenPipeline(vertex, TaaShader());
        depthCopy = builder.FullscreenPipeline(vertex, DepthHistoryCopyShader(), true);
        motionBlur = builder.FullscreenPipeline(vertex, CameraMotionBlurShader());
        fullscreen = builder.FullscreenMesh();
        viewBuffer = builder.Uniform(sizeof(ViewConstants));
        objectBuffer = builder.Uniform(sizeof(ObjectConstants));
        lightingBuffer = builder.Uniform(sizeof(Material::PbrPassConstants));
        effectsBuffer = builder.Uniform(sizeof(SceneEffectsConstants));
        postBuffer = builder.Uniform(sizeof(PostProcessConstants));
        temporalBuffer = builder.Uniform(sizeof(TemporalConstants));
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
            if (item.layer != RenderLayer::Opaque && item.layer != RenderLayer::Cutout) continue;
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
               bool reflectionOnly, bool overlayOnly = false) {
        RenderFrame frame; frame.frameIndex = source.frameIndex;
        for (const auto& item : source.items) {
            if ((item.layer == RenderLayer::Overlay) != overlayOnly) continue;
            if (reflectionOnly && !item.visibleInReflections) continue;
            frame.items.push_back(item);
            frame.items.back().viewDepth = -(camera.view * item.model * glm::vec4(0, 0, 0, 1)).z;
        }
        Require(forward.Record(encoder, frame, View(camera)), "Cannot record material scene");
    }
    void Fullscreen(RHIFrameEncoder& encoder, const Target& target, RenderResourceHandle<PipelineSpec> pipeline,
                    RenderResourceHandle<RHITextureSpec> input, RenderResourceHandle<RHITextureSpec> second,
                    RenderResourceHandle<RHITextureSpec> depth, const PostProcessConstants& constants) {
        BindTarget(encoder, target);
        Upload(encoder, postBuffer, &constants, sizeof(constants), PostProcessBinding);
        Require(encoder.BindPipeline(pipeline) && encoder.BindMesh(fullscreen) &&
                encoder.BindTexture(input, 0) && encoder.BindTexture(second, 1) && encoder.BindTexture(depth, 2) &&
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
        add(settings.post.ssaoEnabled); add(settings.post.ssaoRadius); add(settings.post.ssaoBias); add(settings.post.ssaoPower);
        add(settings.lighting.ambientAndExposure.x); add(settings.lighting.ambientAndExposure.y); add(settings.lighting.ambientAndExposure.z);
        add(settings.lighting.specularAA);
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
        Require(Finite(camera.view) && Finite(camera.projection) && Finite(camera.position) &&
                std::abs(glm::determinant(camera.projection)) > 1e-10f &&
                std::abs(glm::determinant(camera.view)) > 1e-10f, "Invalid camera matrices");
        Require(std::isfinite(camera.nearPlane) && std::isfinite(camera.farPlane) &&
                camera.nearPlane > 0 && camera.farPlane > camera.nearPlane, "Invalid camera clip range");
        Require(ValidatePostProcessSettings(settings.post, &error), "Invalid post-process settings");
        Require(ValidateTemporalEffectsSettings(settings.temporal, &error), "Invalid temporal settings");
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
                    nonnegativeRadiance(glm::vec3(settings.lighting.lightColors[i])), "Invalid point light position/radiance");
        }
        Require(std::isfinite(settings.reflection.strength) && settings.reflection.strength >= 0 &&
                settings.reflection.strength <= 1 && std::isfinite(settings.time), "Invalid reflection/time settings");
        for (const auto& item : frame.items) {
            Require(item.mesh.IsValid() && item.EffectivePipeline().IsValid() && Finite(item.model), "Invalid render item");
            if (item.material) for (const auto& texture : item.material->Resources().textures)
                Require(texture.slot < 8, "MaterialRenderPipeline reserves texture slots 8..15 for passes");
        }
    }
    void Record(RHIFrameEncoder& encoder, const RenderFrame& frame, const PipelineCamera& camera,
                const MaterialPipelineSettings& settings) {
        Validate(frame, camera, settings);
        Require(encoder.IsRecording(), "Pipeline needs a recording frame encoder");
        Require(encoder.KeepAlive(assetsOwner) && encoder.KeepAlive(targetsOwner), "Cannot pin pipeline resources");
        statistics = {};
        statistics.reflectionWidth = reflection.width; statistics.reflectionHeight = reflection.height;
        statistics.msaaSamples = samples;
        const bool useTaa = settings.temporal.antialiasing == AntialiasingMode::TAA;
        const auto contentHash = ContentHash(frame, settings);
        const bool cut = CameraCut(camera, settings);
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
        // Clear even when disabled: diagnostics never expose uninitialized depth.
        BindTarget(encoder, shadows, RHICommand::ClearDepth);
        if (settings.shadows.enabled) {
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
        BindTarget(encoder, reflectionTarget, RHICommand::ClearColor | RHICommand::ClearDepth, glm::vec4(0.006f, 0.009f, 0.015f, 1));
        Require(encoder.BindTexture(shadows.depth, 12), "Cannot bind CSM atlas");
        if (settings.reflection.enabled) {
            const auto reflected = ReflectCamera(camera.view, renderCamera.projection, settings.reflection.plane);
            effects.reflectionViewProjection = reflected.viewProjection;
            effects.clipPlane = reflected.clipPlane;
            effects.effects.z = 1;
            Effects(encoder, effects);
            PipelineCamera reflectedCamera = camera;
            reflectedCamera.view = reflected.view; reflectedCamera.projection = reflected.projection;
            reflectedCamera.position = reflected.position;
            Scene(encoder, frame, reflectedCamera, true);
            ++statistics.reflectionPasses;
            effects.effects.z = 0;
            effects.effects.y = 1;
        }
        Resolve(encoder, reflectionTarget, reflection, true, false);
        Effects(encoder, effects); // Restore clip state before the main-view depth prepass.

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
        BindTarget(encoder, sceneTarget, RHICommand::ClearColor | RHICommand::ClearDepth, glm::vec4(0.006f, 0.009f, 0.015f, 1));
        Require(encoder.BindTexture(reflection.color, 13) && encoder.BindTexture(aoBlurred.color, 14), "Cannot bind reflection/SSAO");
        Effects(encoder, effects);
        Scene(encoder, frame, renderCamera, false);
        ++statistics.scenePasses;
        Resolve(encoder, sceneTarget, scene, true, true);

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
        Target screenTarget; screenTarget.width = width; screenTarget.height = height;
        if (settings.temporal.antialiasing == AntialiasingMode::FXAA)
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
    impl_->width = impl_->height = impl_->atlasResolution = 0;
    impl_->lastBloom = {};
    impl_->completed = {}; impl_->pending = {}; impl_->pendingToken = 0;
}
std::uint64_t MaterialRenderPipeline::RecordedFrameToken() const noexcept { return impl_->pendingToken; }
bool MaterialRenderPipeline::CompleteFrame(std::uint64_t token) {
    if (!token || token != impl_->pendingToken) { impl_->error = "Stale or absent frame completion token"; return false; }
    impl_->completed = impl_->pending; impl_->pending = {}; impl_->pendingToken = 0;
    return true;
}
bool MaterialRenderPipeline::DiscardFrame(std::uint64_t token) {
    if (!token || token != impl_->pendingToken) { impl_->error = "Stale or absent discarded frame token"; return false; }
    impl_->pending = {}; impl_->pendingToken = 0;
    return true;
}
void MaterialRenderPipeline::ResetHistory() {
    impl_->completed.valid = impl_->completed.taaValid = false;
    impl_->pending.valid = impl_->pending.taaValid = false;
}
const std::string& MaterialRenderPipeline::LastError() const noexcept { return impl_->error; }
PipelineStatistics MaterialRenderPipeline::Statistics() const { return impl_->statistics; }
PipelineDebugTextures MaterialRenderPipeline::DebugTextures() const {
    if (!impl_->targetsOwner) return {};
    return {impl_->scene.color, impl_->scene.depth, impl_->shadows.depth,
            impl_->reflection.color, impl_->aoBlurred.color, impl_->lastBloom,
            impl_->reflection.width, impl_->reflection.height, impl_->samples};
}
} // namespace Render
