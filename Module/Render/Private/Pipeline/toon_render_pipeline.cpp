#include "Render/Public/Pipeline/toon_render_pipeline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>
#include "Render/Public/Material/toon_material.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"

namespace Render {
namespace {
void Require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
template<class T> bool Finite(const T& vector) {
    for (int i = 0; i < vector.length(); ++i) if (!std::isfinite(vector[i])) return false;
    return true;
}
bool FiniteMatrix(const glm::mat4& matrix) {
    for (int column = 0; column < 4; ++column) if (!Finite(matrix[column])) return false;
    return true;
}
bool Range(float value, float minimum, float maximum) {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}
template<class T> bool Color(const T& value) {
    for (int i = 0; i < value.length(); ++i) if (!Range(value[i], 0, 1)) return false;
    return true;
}
struct Target {
    RenderResourceHandle<RenderTargetSpec> framebuffer;
    RenderResourceHandle<RHITextureSpec> color, depth;
    std::uint32_t width = 0, height = 0;
};
Target MakeTarget(PipelineDetail::ResourceBuilder& builder, std::uint32_t width,
    std::uint32_t height, bool color, std::uint32_t samples = 1) {
    Target result;
    result.width = width; result.height = height;
    if (color) result.color = builder.Texture(width, height, RHITextureFormat::RGBA8, samples);
    result.depth = builder.Texture(width, height, RHITextureFormat::Depth32F, samples);
    result.framebuffer = builder.Target(result.color, result.depth);
    return result;
}
void BindTarget(RHIFrameEncoder& encoder, const Target& target, std::uint8_t clear, const glm::vec4& color = glm::vec4(1)) {
    RHICommand::SetRenderTarget command;
    command.target = target.framebuffer;
    command.width = target.width; command.height = target.height;
    command.clearFlags = clear; command.clearColor = color;
    Require(encoder.SetRenderTarget(command), "Cannot record toon render target");
}
void Upload(RHIFrameEncoder& encoder, RenderResourceHandle<UniformBufferSpec> handle,
    const void* data, std::uint32_t bytes, std::uint32_t binding) {
    auto* input = static_cast<const std::byte*>(data);
    for (std::uint32_t offset = 0; offset < bytes; offset += RHICommand::MaxInlineUniformBytes)
        Require(encoder.UpdateUniformBufferBytes(handle, input + offset,
            std::min(std::uint32_t(RHICommand::MaxInlineUniformBytes), bytes - offset), offset), "Cannot record toon uniform data");
    Require(encoder.BindUniformBuffer(handle, binding), "Cannot bind toon uniform data");
}
const char* CopyVertex() {
    return R"GLSL(#version 450 core
layout(location=0) in vec2 position;
layout(location=1) in vec2 texcoord;
out vec2 uv;
void main(){ uv=texcoord; gl_Position=vec4(position,0,1); }
)GLSL";
}
const char* CopyFragment() {
    return R"GLSL(#version 450 core
layout(binding=0) uniform sampler2D colorTexture;
in vec2 uv;
layout(location=0) out vec4 color;
void main(){ color=texture(colorTexture,uv); }
)GLSL";
}
} // namespace

bool ValidateToonPipelineSettings(const ToonPipelineSettings& settings, std::string* error) {
    const auto fail = [&](const char* message) { if (error) *error = message; return false; };
    if (!Color(settings.backgroundColor) || !Color(settings.lightColor) ||
        !Color(settings.shadowTint) || !Color(settings.outlineColor)) return fail("Toon colors must be finite and between 0 and 1");
    if (!Finite(settings.lightDirection) || glm::dot(settings.lightDirection, settings.lightDirection) < 1e-10f ||
        !std::isfinite(glm::dot(settings.lightDirection, settings.lightDirection))) return fail("Toon light direction must be finite and nonzero");
    if (!Range(settings.exposure, 0, 8)) return fail("Toon exposure must be between 0 and 8");
    if (!Range(settings.bandThreshold, -1, 1) || !Range(settings.bandSoftness, 0.001f, 0.5f) ||
        !Range(settings.normalWrap, 0, 1)) return fail("Invalid toon band threshold, softness or normal wrap");
    if (!Range(settings.bandStrength, 0, 1) || !Range(settings.highlightStrength, 0, 1) ||
        !Range(settings.highlightThreshold, 0, 1) || !Range(settings.selfShadowStrength, 0, 1) ||
        !Range(settings.groundShadowStrength, 0, 1)) return fail("Toon lighting strengths must be between 0 and 1");
    if (!Range(settings.outlineWidth, 0, 8)) return fail("Toon outline width must be between 0 and 8 pixels");
    if (!Range(settings.shadowDepthBias, 0, 0.01f) || !Range(settings.shadowNormalBias, 0, 1)) return fail("Invalid toon shadow bias");
    if (!Finite(settings.sceneCenter) || !std::isfinite(settings.groundHeight) ||
        !std::isfinite(settings.groundExtent) || settings.groundExtent <= 0 ||
        !std::isfinite(settings.shadowRadius) || settings.shadowRadius <= 0 ||
        !std::isfinite(settings.shadowDistance) || settings.shadowDistance <= settings.shadowRadius)
        return fail("Invalid toon ground or shadow volume");
    if (error) error->clear();
    return true;
}

struct ToonRenderPipeline::Impl {
    RHIDevice& device;
    std::string error;
    std::shared_ptr<const void> assetsOwner, targetsOwner;
    std::shared_ptr<const Material::MaterialTemplate> schema = Material::MakePbrTemplate();
    std::array<RenderResourceHandle<PipelineSpec>, 2> surface{}, transparent{}, outline{};
    RenderResourceHandle<PipelineSpec> shadow, ground, display;
    RenderResourceHandle<VertexBufferSpec> fullscreen, groundMesh;
    RenderResourceHandle<UniformBufferSpec> viewBuffer, objectBuffer, passBuffer;
    Target scene, resolved, shadows;
    std::uint32_t width = 0, height = 0, shadowResolution = 0, samples = 1;
    std::uint64_t nextToken = 0, pendingToken = 0;
    ToonPipelineStatistics statistics;

    explicit Impl(RHIDevice& value) : device(value) {}

    void Initialize() {
        Require(device.IsRunning(), "Start RHIDevice before initializing the toon pipeline");
        if (assetsOwner) return;
        PipelineDetail::ResourceBuilder builder(device);
        const auto program = [&](const std::string& vertex, const std::string& fragment) {
            CreateGraphicShaderDesc desc;
            desc.vertexShaderSource = builder.Source(ShaderSourceType::Vertex, vertex);
            desc.fragmentShaderSource = builder.Source(ShaderSourceType::Fragment, fragment);
            return builder.Create<ShaderProgramSpec>([&](auto cb) { device.async_CreateGraphicShader(desc, cb); });
        };
        const auto surfaceProgram = program(Material::ToonVertexShader(), Material::ToonFragmentShader());
        const auto outlineProgram = program(Material::ToonOutlineVertexShader(), Material::ToonOutlineFragmentShader());
        const auto shadowProgram = program(Material::ToonShadowVertexShader(), Material::ToonShadowFragmentShader());
        const auto groundProgram = program(Material::ToonGroundVertexShader(), Material::ToonGroundFragmentShader());
        const auto create = [&](const PipelineSpec& state) {
            return builder.Create<PipelineSpec>([&](auto cb) { device.async_CreatePipeline({state}, cb); });
        };
        PipelineSpec spec;
        spec.expectVertexLayout = Material::PbrVertexLayout();
        spec.cullMode = CullMode::None;
        for (int mirrored = 0; mirrored < 2; ++mirrored) {
            spec.shaderProgram = surfaceProgram;
            spec.frontFaceCounterClockwise = !mirrored;
            spec.cullMode = CullMode::None;
            spec.depthWrite = true; spec.blendEnable = false; spec.blendMode = BlendMode::Opaque;
            surface[mirrored] = create(spec);
            spec.depthWrite = false; spec.blendEnable = true; spec.blendMode = BlendMode::Alpha;
            transparent[mirrored] = create(spec);
            spec.shaderProgram = outlineProgram;
            spec.cullMode = CullMode::Front;
            spec.blendEnable = false; spec.blendMode = BlendMode::Opaque;
            outline[mirrored] = create(spec);
        }
        spec.shaderProgram = shadowProgram;
        spec.frontFaceCounterClockwise = true;
        spec.cullMode = CullMode::None; spec.depthWrite = true;
        shadow = create(spec);
        spec.shaderProgram = groundProgram;
        ground = create(spec);
        display = builder.FullscreenPipeline(CopyVertex(), CopyFragment());
        fullscreen = builder.FullscreenMesh();
        // Unit XZ plane with the exact imported-model vertex layout.
        const std::array<float, 48> plane{
            -1,0,-1, 0,1,0, 0,0, 1,0,0,1,
            -1,0, 1, 0,1,0, 0,1, 1,0,0,1,
             1,0, 1, 0,1,0, 1,1, 1,0,0,1,
             1,0,-1, 0,1,0, 1,0, 1,0,0,1};
        const std::array<std::uint32_t, 6> indices{0,1,2,0,2,3};
        CreateMeshBufferDesc mesh;
        mesh.vertexLayout = Material::PbrVertexLayout(); mesh.vertexCount = 4;
        mesh.vertexData = plane.data(); mesh.vertexByteSize = sizeof(plane);
        mesh.indexCount = 6; mesh.indexData = indices.data(); mesh.indexByteSize = sizeof(indices);
        groundMesh = builder.Create<VertexBufferSpec>([&](auto cb) { device.async_CreateMeshBuffer(mesh, cb); });
        viewBuffer = builder.Uniform(sizeof(ViewConstants));
        objectBuffer = builder.Uniform(sizeof(ObjectConstants));
        passBuffer = builder.Uniform(sizeof(Material::ToonPassConstants));
        assetsOwner = builder.Finish();
    }

    void Resize(std::uint32_t w, std::uint32_t h, std::uint32_t resolution, std::uint32_t sampleCount) {
        Require(assetsOwner && device.IsRunning(), "Initialize the toon pipeline before Resize");
        Require(!pendingToken, "Complete or discard the recorded toon frame before Resize");
        Require(w && h && w <= 8192 && h <= 8192, "Toon dimensions must be between 1 and 8192");
        Require(resolution >= 128 && resolution <= 8192, "Toon shadow resolution must be between 128 and 8192");
        Require(IsValidTextureSampleCount(sampleCount), "Toon MSAA supports 1, 2, 4 or 8 samples");
        if (targetsOwner && w == width && h == height && resolution == shadowResolution && samples == sampleCount) return;
        PipelineDetail::ResourceBuilder builder(device);
        auto newScene = MakeTarget(builder, w, h, true, sampleCount);
        auto newResolved = sampleCount > 1 ? MakeTarget(builder, w, h, true) : newScene;
        auto newShadows = MakeTarget(builder, resolution, resolution, false);
        auto owner = builder.Finish();
        scene = newScene; resolved = newResolved; shadows = newShadows;
        targetsOwner = std::move(owner);
        width = w; height = h; shadowResolution = resolution; samples = sampleCount;
    }

    bool Compatible(const Material::MaterialSnapshot& material) const {
        const auto& actual = material.Parameters().GetTemplate();
        if (!actual || actual->ByteSize() != schema->ByteSize() || actual->Layouts().size() != schema->Layouts().size()) return false;
        if (actual->Desc().textures.size() != schema->Desc().textures.size()) return false;
        for (const auto& slot : schema->Desc().textures) {
            const auto found = std::find_if(actual->Desc().textures.begin(), actual->Desc().textures.end(), [&](const auto& candidate) {
                return candidate.name == slot.name && candidate.slot == slot.slot && candidate.required == slot.required;
            });
            if (found == actual->Desc().textures.end()) return false;
        }
        const auto& expected = schema->Desc().parameters;
        for (std::size_t index = 0; index < expected.size(); ++index) {
            const auto& parameter = actual->Desc().parameters[index];
            if (parameter.name != expected[index].name || parameter.type != expected[index].type ||
                actual->Layouts()[index].offset != schema->Layouts()[index].offset) return false;
        }
        return material.GetDomain() == Material::Domain::Surface || material.GetDomain() == Material::Domain::Translucent;
    }

    void BindMaterial(RHIFrameEncoder& encoder, const RenderItem& item) {
        const auto& material = *item.material;
        const auto& bytes = material.Parameters().Bytes();
        Upload(encoder, material.Resources().parameterBuffer, bytes.data(), std::uint32_t(bytes.size()), Material::MaterialParameterBinding);
        for (const auto& texture : material.Resources().textures)
            Require(encoder.BindTexture(texture.texture, texture.slot), "Cannot bind toon material texture");
        Upload(encoder, objectBuffer, &item.model, sizeof(item.model), 1);
        Require(encoder.BindMesh(item.mesh) && encoder.DrawIndexed(item.draw), "Cannot record toon mesh draw");
    }

    void Record(RHIFrameEncoder& encoder, const RenderFrame& frame, const PipelineCamera& camera, const ToonPipelineSettings& settings) {
        Require(assetsOwner && targetsOwner && device.IsRunning(), "Initialize and Resize the toon pipeline before Record");
        Require(!pendingToken, "Complete or discard the previous toon frame before Record");
        Require(encoder.IsRecording(), "Toon pipeline requires an active frame encoder");
        std::string validation;
        Require(ValidateToonPipelineSettings(settings, &validation), validation);
        Require(FiniteMatrix(camera.view) && FiniteMatrix(camera.projection) && Finite(camera.position), "Non-finite toon camera");
        for (const auto& item : frame.items)
            Require(item.mesh.IsValid() && item.material && Compatible(*item.material) && FiniteMatrix(item.model),
                "Toon pipeline requires finite indexed meshes with imported PBR-compatible Material snapshots");
        Require(encoder.KeepAlive(assetsOwner) && encoder.KeepAlive(targetsOwner), "Cannot pin toon pipeline resources");
        for (const auto& item : frame.items) Require(encoder.KeepAlive(item.material), "Cannot pin toon model material");
        const ViewConstants view{camera.projection * camera.view, glm::vec4(camera.position, 1)};
        Require(FiniteMatrix(view.viewProjection), "Toon view-projection matrix overflow");
        Upload(encoder, viewBuffer, &view, sizeof(view), 0);
        const auto lightDirection = glm::normalize(settings.lightDirection);
        const glm::vec3 up = std::abs(lightDirection.y) > 0.98f ? glm::vec3(0,0,1) : glm::vec3(0,1,0);
        Material::ToonPassConstants pass;
        pass.lightViewProjection = glm::ortho(-settings.shadowRadius, settings.shadowRadius,
            -settings.shadowRadius, settings.shadowRadius, 0.05f, settings.shadowDistance + settings.shadowRadius * 3) *
            glm::lookAt(settings.sceneCenter + lightDirection * settings.shadowDistance, settings.sceneCenter, up);
        Require(FiniteMatrix(pass.lightViewProjection), "Toon shadow camera exceeds the supported coordinate range");
        pass.lightDirectionAndShadow = glm::vec4(lightDirection, settings.shadowsEnabled ? 1 : 0);
        pass.shadowTintAndStrength = glm::vec4(settings.shadowTint, settings.bandStrength);
        pass.shading = {settings.bandThreshold, settings.bandSoftness, settings.normalWrap, settings.highlightStrength};
        pass.outlineColorAndWidth = glm::vec4(settings.outlineColor, settings.outlinesEnabled ? settings.outlineWidth : 0);
        pass.viewportAndBias = {float(width), float(height), settings.shadowDepthBias, settings.shadowNormalBias};
        pass.options = {settings.selfShadowStrength, settings.groundShadows ? settings.groundShadowStrength : 0, settings.highlightThreshold, 0};
        pass.lightColorAndExposure = glm::vec4(settings.lightColor, settings.exposure);
        Upload(encoder, passBuffer, &pass, sizeof(pass), Material::ToonPassBinding);

        ToonPipelineStatistics result;
        result.msaaSamples = samples;
        if (settings.shadowsEnabled) {
            BindTarget(encoder, shadows, RHICommand::ClearDepth);
            Require(encoder.BindPipeline(shadow), "Cannot bind toon shadow pipeline");
            for (const auto& item : frame.items)
                if (item.castsShadows && item.EffectiveLayer() != RenderLayer::Transparent) BindMaterial(encoder, item);
            result.shadowPasses = 1;
        }
        BindTarget(encoder, scene, RHICommand::ClearColor | RHICommand::ClearDepth, settings.backgroundColor);
        Require(encoder.BindTexture(shadows.depth, Material::ToonShadowTextureBinding), "Cannot bind toon shadow map");
        if (settings.groundEnabled) {
            const auto model = glm::translate(glm::mat4(1), glm::vec3(settings.sceneCenter.x, settings.groundHeight, settings.sceneCenter.z)) *
                glm::scale(glm::mat4(1), glm::vec3(settings.groundExtent, 1, settings.groundExtent));
            Upload(encoder, objectBuffer, &model, sizeof(model), 1);
            Require(encoder.BindPipeline(ground) && encoder.BindMesh(groundMesh) &&
                encoder.DrawIndexed({6}), "Cannot draw toon ground receiver");
            result.groundPasses = 1;
        }
        std::vector<const RenderItem*> opaque, translucent;
        for (const auto& item : frame.items) {
            if (item.EffectiveLayer() == RenderLayer::Transparent) translucent.push_back(&item);
            else opaque.push_back(&item);
        }
        const auto mirrored = [](const RenderItem& item) { return glm::determinant(glm::mat3(item.model)) < 0 ? 1 : 0; };
        for (auto* item : opaque) {
            Require(encoder.BindPipeline(surface[mirrored(*item)]), "Cannot bind toon surface pipeline");
            BindMaterial(encoder, *item);
        }
        result.scenePasses = 1;
        if (settings.outlinesEnabled && settings.outlineWidth > 0 && !opaque.empty()) {
            for (auto* item : opaque) {
                Require(encoder.BindPipeline(outline[mirrored(*item)]), "Cannot bind toon outline pipeline");
                BindMaterial(encoder, *item);
            }
            result.outlinePasses = 1;
        }
        std::stable_sort(translucent.begin(), translucent.end(), [](auto* a, auto* b) { return a->viewDepth > b->viewDepth; });
        for (auto* item : translucent) {
            Require(encoder.BindPipeline(transparent[mirrored(*item)]), "Cannot bind translucent toon pipeline");
            BindMaterial(encoder, *item);
        }
        result.transparentPasses = translucent.empty() ? 0 : 1;
        if (samples > 1) {
            Require(encoder.ResolveRenderTarget({scene.framebuffer, resolved.framebuffer, true, true}), "Cannot resolve toon MSAA target");
            result.resolvePasses = 1;
        }
        Target screen; screen.width = width; screen.height = height;
        BindTarget(encoder, screen, RHICommand::ClearNone);
        Require(encoder.BindPipeline(display) && encoder.BindTexture(resolved.color, 0) &&
            encoder.BindMesh(fullscreen) && encoder.DrawIndexed({3}), "Cannot display toon frame");
        result.postPasses = 1;
        statistics = result;
        pendingToken = ++nextToken;
    }
};

ToonRenderPipeline::ToonRenderPipeline(RHIDevice& device) : impl_(std::make_unique<Impl>(device)) {}
ToonRenderPipeline::~ToonRenderPipeline() { Shutdown(); }
bool ToonRenderPipeline::Initialize() {
    impl_->error.clear();
    try { impl_->Initialize(); return true; }
    catch (const std::exception& error) { impl_->error = error.what(); return false; }
}
bool ToonRenderPipeline::Resize(std::uint32_t width, std::uint32_t height, std::uint32_t shadowResolution, std::uint32_t samples) {
    impl_->error.clear();
    try { impl_->Resize(width, height, shadowResolution, samples); return true; }
    catch (const std::exception& error) { impl_->error = error.what(); return false; }
}
bool ToonRenderPipeline::Record(RHIFrameEncoder& encoder, const RenderFrame& scene,
    const PipelineCamera& camera, const ToonPipelineSettings& settings) {
    impl_->error.clear();
    try { impl_->Record(encoder, scene, camera, settings); return true; }
    catch (const std::exception& error) { impl_->error = error.what(); return false; }
}
std::uint64_t ToonRenderPipeline::RecordedFrameToken() const noexcept { return impl_->pendingToken; }
bool ToonRenderPipeline::CompleteFrame(std::uint64_t token) {
    if (!token || token != impl_->pendingToken) { impl_->error = "Stale or absent toon frame completion token"; return false; }
    impl_->pendingToken = 0; return true;
}
bool ToonRenderPipeline::DiscardFrame(std::uint64_t token) { return CompleteFrame(token); }
void ToonRenderPipeline::Shutdown() {
    impl_->targetsOwner.reset(); impl_->assetsOwner.reset();
    impl_->scene = {}; impl_->resolved = {}; impl_->shadows = {};
    impl_->width = impl_->height = impl_->shadowResolution = 0;
    impl_->samples = 1; impl_->pendingToken = 0; impl_->statistics = {};
}
const std::string& ToonRenderPipeline::LastError() const noexcept { return impl_->error; }
ToonPipelineStatistics ToonRenderPipeline::Statistics() const { return impl_->statistics; }
ToonPipelineDebugTextures ToonRenderPipeline::DebugTextures() const {
    if (!impl_->targetsOwner) return {};
    return {impl_->resolved.color, impl_->resolved.depth, impl_->shadows.depth};
}
} // namespace Render
