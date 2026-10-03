#include "Render/Public/Pipeline/material_render_pipeline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <glm/gtc/matrix_inverse.hpp>
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Private/Pipeline/post_process_shaders.h"

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
Target MakeTarget(PipelineDetail::ResourceBuilder& builder, std::uint32_t w, std::uint32_t h, bool color, bool depth) {
    Target result; result.width = w; result.height = h;
    if (color) result.color = builder.Texture(w, h, RHITextureFormat::RGBA16F);
    if (depth) result.depth = builder.Texture(w, h, RHITextureFormat::Depth32F);
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
    RenderResourceHandle<UniformBufferSpec> viewBuffer, objectBuffer, lightingBuffer, effectsBuffer, postBuffer;
    RenderResourceHandle<PipelineSpec> extract, blur, upsample, ssao, ssaoBlur, composite, filter;
    Target scene, reflection, shadows, aoRaw, aoBlurred, display;
    std::array<Target, 6> bloomDown, bloomWork;
    std::uint32_t width = 0, height = 0, atlasResolution = 0;
    RenderResourceHandle<RHITextureSpec> lastBloom;
    PipelineStatistics statistics;
    ForwardRenderPipeline forward;

    explicit Impl(RHIDevice& device) : device(device) {}
    void Initialize() {
        if (assetsOwner) return;
        PipelineDetail::ResourceBuilder builder(device);
        const auto vertex = FullscreenVertexShader();
        extract = builder.FullscreenPipeline(vertex, BloomExtractShader());
        blur = builder.FullscreenPipeline(vertex, GaussianBlurShader());
        upsample = builder.FullscreenPipeline(vertex, BloomUpsampleShader());
        ssao = builder.FullscreenPipeline(vertex, SsaoShader());
        ssaoBlur = builder.FullscreenPipeline(vertex, SsaoBlurShader());
        composite = builder.FullscreenPipeline(vertex, CompositeShader());
        filter = builder.FullscreenPipeline(vertex, FilterShader());
        fullscreen = builder.FullscreenMesh();
        viewBuffer = builder.Uniform(sizeof(ViewConstants));
        objectBuffer = builder.Uniform(sizeof(ObjectConstants));
        lightingBuffer = builder.Uniform(sizeof(Material::PbrPassConstants));
        effectsBuffer = builder.Uniform(sizeof(SceneEffectsConstants));
        postBuffer = builder.Uniform(sizeof(PostProcessConstants));
        assetsOwner = builder.Finish();
    }
    void Resize(std::uint32_t w, std::uint32_t h, std::uint32_t resolution) {
        Require(assetsOwner != nullptr, "Initialize pipeline before Resize");
        Require(w > 0 && h > 0 && w <= 8192 && h <= 8192, "Invalid viewport dimensions");
        Require(resolution >= 128 && resolution <= 8192 && resolution % 2 == 0, "CSM atlas must be even, 128..8192");
        if (w == width && h == height && atlasResolution == resolution) return;
        // Build a complete replacement. Failure leaves the previous targets usable.
        PipelineDetail::ResourceBuilder builder(device);
        auto newScene = MakeTarget(builder, w, h, true, true);
        const auto halfW = std::max(1u, (w + 1) / 2), halfH = std::max(1u, (h + 1) / 2);
        auto newReflection = MakeTarget(builder, halfW, halfH, true, true);
        auto newShadows = MakeTarget(builder, resolution, resolution, false, true);
        auto newAoRaw = MakeTarget(builder, halfW, halfH, true, false);
        auto newAoBlurred = MakeTarget(builder, halfW, halfH, true, false);
        auto newDisplay = MakeTarget(builder, w, h, true, false);
        std::array<Target, 6> newDown, newWork;
        auto levelW = halfW, levelH = halfH;
        for (std::size_t i = 0; i < newDown.size(); ++i) {
            newDown[i] = MakeTarget(builder, levelW, levelH, true, false);
            newWork[i] = MakeTarget(builder, levelW, levelH, true, false);
            levelW = std::max(1u, (levelW + 1) / 2); levelH = std::max(1u, (levelH + 1) / 2);
        }
        auto newOwner = builder.Finish();
        scene = newScene; reflection = newReflection; shadows = newShadows;
        aoRaw = newAoRaw; aoBlurred = newAoBlurred; display = newDisplay;
        bloomDown = newDown; bloomWork = newWork;
        targetsOwner = std::move(newOwner);
        width = w; height = h; atlasResolution = resolution; lastBloom = {};
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
    void Validate(const RenderFrame& frame, const PipelineCamera& camera, const MaterialPipelineSettings& settings) {
        Require(assetsOwner && targetsOwner, "Pipeline requires Initialize and Resize");
        Require(Finite(camera.view) && Finite(camera.projection) && Finite(camera.position) &&
                std::abs(glm::determinant(camera.projection)) > 1e-10f &&
                std::abs(glm::determinant(camera.view)) > 1e-10f, "Invalid camera matrices");
        Require(std::isfinite(camera.nearPlane) && std::isfinite(camera.farPlane) &&
                camera.nearPlane > 0 && camera.farPlane > camera.nearPlane, "Invalid camera clip range");
        Require(ValidatePostProcessSettings(settings.post, &error), "Invalid post-process settings");
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
        Require(nonnegativeRadiance(glm::vec3(settings.lighting.ambientAndExposure)), "Invalid ambient radiance");
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
        effects.screenAndAo = glm::vec4(1.0f / width, 1.0f / height, 0, 0);
        const auto cascades = BuildCascades(camera.view, camera.projection, camera.nearPlane, camera.farPlane,
            settings.shadows.sunDirection, settings.shadows.distance, settings.shadows.splitLambda,
            atlasResolution, 30.0f, settings.shadows.cascadeBlend);
        effects.lightViewProjection = cascades.lightViewProjection;
        effects.cascadeSplits = cascades.cascadeSplits;
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
        BindTarget(encoder, reflection, RHICommand::ClearColor | RHICommand::ClearDepth, glm::vec4(0.006f, 0.009f, 0.015f, 1));
        Require(encoder.BindTexture(shadows.depth, 12), "Cannot bind CSM atlas");
        if (settings.reflection.enabled) {
            const auto reflected = ReflectCamera(camera.view, camera.projection, settings.reflection.plane);
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
        Effects(encoder, effects); // Restore clip state before the main-view depth prepass.

        PostProcessConstants post{};
        post.projection = camera.projection; post.inverseProjection = glm::inverse(camera.projection);
        post.bloom = glm::vec4(settings.post.bloomThreshold, settings.post.bloomStrength, 0, 0);
        post.ssao = glm::vec4(settings.post.ssaoRadius, settings.post.ssaoBias, settings.post.ssaoPower, settings.post.ssaoEnabled ? 1 : 0);
        post.output = glm::vec4(settings.post.exposure, settings.post.gamma, float(settings.post.toneMap), float(settings.post.filter));
        post.screen = glm::vec4(1.0f / width, 1.0f / height, 0, settings.post.bloomEnabled ? 1 : 0);
        post.grading = glm::vec4(settings.post.saturation, settings.post.contrast, settings.post.vignette, settings.post.sharpenStrength);
        post.misc = glm::vec4(camera.nearPlane, camera.farPlane, settings.time, settings.post.ssaoEnabled ? 1 : 0);
        if (settings.post.ssaoEnabled) {
            BindTarget(encoder, scene, RHICommand::ClearDepth);
            Depth(encoder, frame, camera.projection * camera.view, false);
            ++statistics.depthPasses;
            Fullscreen(encoder, aoRaw, ssao, {}, {}, scene.depth, post);
            auto blurPost = post;
            blurPost.screen.x = 1.0f / aoRaw.width; blurPost.screen.y = 1.0f / aoRaw.height;
            Fullscreen(encoder, aoBlurred, ssaoBlur, aoRaw.color, {}, scene.depth, blurPost);
            effects.screenAndAo.z = 1;
        } else {
            BindTarget(encoder, aoBlurred, RHICommand::ClearColor, glm::vec4(1));
        }
        BindTarget(encoder, scene, RHICommand::ClearColor | RHICommand::ClearDepth, glm::vec4(0.006f, 0.009f, 0.015f, 1));
        Require(encoder.BindTexture(reflection.color, 13) && encoder.BindTexture(aoBlurred.color, 14), "Cannot bind reflection/SSAO");
        Effects(encoder, effects);
        Scene(encoder, frame, camera, false);
        ++statistics.scenePasses;

        lastBloom = {};
        if (settings.post.bloomEnabled) {
            auto levels = std::min<std::uint32_t>(settings.post.bloomLevels, static_cast<std::uint32_t>(bloomDown.size()));
            for (std::uint32_t i = 1; i < levels; ++i) {
                if (bloomDown[i - 1].width == 1 && bloomDown[i - 1].height == 1) { levels = i; break; }
            }
            Fullscreen(encoder, bloomDown[0], extract, scene.color, {}, {}, post);
            for (std::uint32_t level = 0; level < levels; ++level) {
                const auto& source = bloomDown[level == 0 ? 0 : level - 1];
                auto blurPost = post;
                blurPost.screen.x = 1.0f / source.width; blurPost.screen.y = 1.0f / source.height;
                blurPost.bloom.z = 1; blurPost.bloom.w = 0;
                Fullscreen(encoder, bloomWork[level], blur, source.color, {}, {}, blurPost);
                blurPost.screen.x = 1.0f / bloomWork[level].width; blurPost.screen.y = 1.0f / bloomWork[level].height;
                blurPost.bloom.z = 0; blurPost.bloom.w = 1;
                Fullscreen(encoder, bloomDown[level], blur, bloomWork[level].color, {}, {}, blurPost);
            }
            lastBloom = bloomDown[levels - 1].color;
            for (int level = static_cast<int>(levels) - 2; level >= 0; --level) {
                auto upPost = post;
                upPost.screen.x = 1.0f / bloomDown[level + 1].width;
                upPost.screen.y = 1.0f / bloomDown[level + 1].height;
                Fullscreen(encoder, bloomWork[level], upsample, lastBloom, bloomDown[level].color, {}, upPost);
                lastBloom = bloomWork[level].color;
            }
        }
        Fullscreen(encoder, display, composite, scene.color, lastBloom, {}, post);
        Target screenTarget; screenTarget.width = width; screenTarget.height = height;
        Fullscreen(encoder, screenTarget, filter, display.color, {}, {}, post);
        Scene(encoder, frame, camera, false, true); // Overlay bypasses HDR/post effects.
    }
};

MaterialRenderPipeline::MaterialRenderPipeline(RHIDevice& device) : impl_(std::make_unique<Impl>(device)) {}
MaterialRenderPipeline::~MaterialRenderPipeline() { Shutdown(); }
bool MaterialRenderPipeline::Initialize() {
    impl_->error.clear();
    try { impl_->Initialize(); return true; }
    catch (const std::exception& error) { impl_->error = error.what(); return false; }
}
bool MaterialRenderPipeline::Resize(std::uint32_t width, std::uint32_t height, std::uint32_t resolution) {
    impl_->error.clear();
    try { impl_->Resize(width, height, resolution); return true; }
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
}
const std::string& MaterialRenderPipeline::LastError() const noexcept { return impl_->error; }
PipelineStatistics MaterialRenderPipeline::Statistics() const { return impl_->statistics; }
PipelineDebugTextures MaterialRenderPipeline::DebugTextures() const {
    if (!impl_->targetsOwner) return {};
    return {impl_->scene.color, impl_->scene.depth, impl_->shadows.depth,
            impl_->reflection.color, impl_->aoBlurred.color, impl_->lastBloom};
}
} // namespace Render
