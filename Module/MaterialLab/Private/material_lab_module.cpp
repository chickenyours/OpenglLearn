#include "MaterialLab/Public/material_lab_module.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <utility>

#include <glad/glad.h>
#include <glfw/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "MaterialLab/Public/lab_geometry.h"
#include "Render/Public/Pipeline/material_render_pipeline.h"
#include "Render/Private/Material/rhi_material_resources.h"

namespace MaterialLab {
using namespace Render;
using namespace Render::Material;
using namespace std::chrono_literals;

namespace {
constexpr float GalleryFloorHeight = -3.25f;
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
template <class T> struct Completion { T value{}; bool done = false; };
struct MeshLease {
    RHIDevice& device;
    std::vector<RenderResourceHandle<VertexBufferSpec>> handles;
    ~MeshLease() { for (auto handle : handles) device.async_DeleteVertexBuffer(handle); }
};
template <class T> T ReadValue(const ParameterBlock& parameters, const char* name) {
    T result{};
    const auto* layout = parameters.GetTemplate()->FindLayout(name);
    Require(layout != nullptr && sizeof(T) <= layout->size, "Invalid parameter read");
    std::memcpy(&result, parameters.Bytes().data() + layout->offset, sizeof(T));
    return result;
}
TexturePixels LoadTexture(const std::filesystem::path& path) {
    int width = 0, height = 0, channels = 0;
    const auto file = path.string();
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load(file.c_str(), &width, &height, &channels, 4), stbi_image_free);
    Require(pixels != nullptr, "Cannot load legacy texture: " + file);
    Require(width > 0 && height > 0 && width <= 8192 && height <= 8192, "Invalid texture dimensions");
    TexturePixels result;
    result.width = static_cast<std::uint32_t>(width);
    result.height = static_cast<std::uint32_t>(height);
    result.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    // Keep all maps in the same bottom-left UV convention without stb globals.
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
    for (int y = 0; y < height; ++y)
        std::memcpy(result.rgba.data() + y * rowBytes,
                    pixels.get() + (height - 1 - y) * rowBytes, rowBytes);
    return result;
}
} // namespace

struct MaterialLabModule::Impl {
    // First member, destroyed last: all resource leases depend on this device.
    RHIDevice device;
    MaterialService materials;
    std::shared_ptr<const MaterialTemplate> schema;
    OwnedMaterialResources owned;
    std::shared_ptr<MeshLease> meshes;
    std::size_t pendingCreates = 0;
    std::shared_ptr<const void> owner;
    MaterialPassResources common;
    struct Entry {
        std::string name;
        MaterialHandle handle;
        std::shared_ptr<const MaterialAsset> asset;
        MaterialPassResources pass;
        glm::mat4 model{1};
    };
    std::vector<Entry> entries;
    RenderResourceHandle<VertexBufferSpec> sphere, plane, box;
    std::uint32_t sphereIndices = 0, planeIndices = 0, boxIndices = 0;
    MaterialHandle floorMaterial;
    MaterialHandle glowMaterial;
    MaterialHandle studyMaterial, studyFloorMaterial;
    std::unique_ptr<MaterialRenderPipeline> pipeline;
    std::uint64_t frameIndex = 0;
    std::uint32_t lastWidth = 0, lastHeight = 0;
    bool lastPresented = true;

    explicit Impl(const OpenglBackendContext& context) {
        meshes = std::shared_ptr<MeshLease>(new MeshLease{device, {}});
        owned.dependencies.push_back(meshes);
        device.Run(BackendType::Opengl, context);
    }
    ~Impl() {
        // A startup timeout can leave a successful creation's callback pending.
        // Join these completions while the device still accepts deletions, and
        // register ownership inside the callback even when Create() has unwound.
        while (pendingCreates != 0) {
            device.returnSystem.DrainCallbacks();
            if (pendingCreates != 0) device.returnSystem.WaitForCallbacks(2ms);
        }
        pipeline.reset();
        materials.Clear();
        entries.clear();
        common = {};
        meshes.reset();
        owner.reset();
        RetainMaterialResources(device, std::exchange(owned, {})).reset();
        device.StopAndRelease();
        while (!device.returnSystem.callbacks.empty()) device.returnSystem.DrainCallbacks();
    }
    template <class Predicate> void Wait(Predicate complete, const char* operation) {
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!complete()) {
            device.returnSystem.DrainCallbacks();
            if (complete()) break;
            Require(std::chrono::steady_clock::now() < deadline, std::string("Timed out: ") + operation);
            glfwPollEvents();
            device.returnSystem.WaitForCallbacks(2ms);
        }
    }
    template <class Spec, class Submit>
    RenderResourceHandle<Spec> Create(Submit submit, const char* operation) {
        auto result = std::make_shared<Completion<RenderResourceHandle<Spec>>>();
        ++pendingCreates;
        try {
            submit([this, result](auto handle) {
                if (handle.IsValid()) {
                    if constexpr (std::is_same_v<Spec, ShaderSourceSpec>) owned.sources.push_back(handle);
                    else if constexpr (std::is_same_v<Spec, ShaderProgramSpec>) owned.programs.push_back(handle);
                    else if constexpr (std::is_same_v<Spec, PipelineSpec>) owned.pipelines.push_back(handle);
                    else if constexpr (std::is_same_v<Spec, UniformBufferSpec>) owned.uniformBuffers.push_back(handle);
                    else if constexpr (std::is_same_v<Spec, RHITextureSpec>) owned.textures.push_back(handle);
                    else if constexpr (std::is_same_v<Spec, VertexBufferSpec>) meshes->handles.push_back(handle);
                }
                result->value = handle; result->done = true; --pendingCreates;
            });
        } catch (...) { --pendingCreates; throw; }
        Wait([result] { return result->done; }, operation);
        Require(result->value.IsValid(), std::string("RHI creation failed: ") + operation);
        return result->value;
    }
    RenderResourceHandle<UniformBufferSpec> Uniform(std::uint32_t bytes) {
        CreateUniformBufferDesc desc;
        desc.byteSize = bytes;
        auto handle = Create<UniformBufferSpec>([&](auto cb) { device.async_CreateUniformBuffer(desc, cb); }, "uniform");
        return handle;
    }
    RenderResourceHandle<RHITextureSpec> Texture(const TexturePixels& pixels) {
        CreateRHITextureSpec desc;
        desc.width = pixels.width; desc.height = pixels.height; desc.data = pixels.rgba.data();
        desc.filterMode = RHIFilterMode::Linear;
        desc.mipmapMode = RHIMipmapMode::Linear;
        auto handle = Create<RHITextureSpec>([&](auto cb) { device.async_CreateTexture(desc, cb); }, "texture");
        return handle;
    }
    RenderResourceHandle<ShaderSourceSpec> Shader(ShaderSourceType type, const std::string& text) {
        CreateShaderSourceDesc desc{type, text.data(), text.size()};
        auto handle = Create<ShaderSourceSpec>([&](auto cb) { device.async_CreateShaderSource(desc, cb); }, "PBR source");
        return handle;
    }
    RenderResourceHandle<VertexBufferSpec> Mesh(const MeshData& mesh) {
        CreateMeshBufferDesc desc;
        desc.vertexLayout = PbrVertexLayout();
        desc.vertexCount = static_cast<std::uint32_t>(mesh.vertices.size());
        desc.vertexData = mesh.vertices.data();
        desc.vertexByteSize = static_cast<std::uint32_t>(mesh.vertices.size() * sizeof(PbrVertex));
        desc.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
        desc.indexData = mesh.indices.data();
        desc.indexByteSize = static_cast<std::uint32_t>(mesh.indices.size() * sizeof(std::uint32_t));
        return Create<VertexBufferSpec>([&](auto cb) { device.async_CreateMeshBuffer(desc, cb); }, "mesh");
    }
    // Lab-only reflection verifies CPU-generated std140 declarations on the GPU.
    // Rendering itself uses only the normal RHI path.
    void InspectShader(RenderResourceHandle<ShaderProgramSpec> shader) {
        auto result = std::make_shared<Completion<std::string>>();
        device.async_ExecuteCode([this, shader, result] {
            const auto* program = device.GetResourcePool().shaderProgramTable.Get(shader);
            if (!program) { result->value = "Missing PBR shader"; return; }
            const auto checkBlock = [&](const char* name, GLint bytes, GLint binding) {
                GLuint block = glGetUniformBlockIndex(program->rhi_id, name);
                if (block == GL_INVALID_INDEX) { result->value = std::string("Missing block: ") + name; return; }
                GLint actualBytes = 0, actualBinding = -1;
                glGetActiveUniformBlockiv(program->rhi_id, block, GL_UNIFORM_BLOCK_DATA_SIZE, &actualBytes);
                glGetActiveUniformBlockiv(program->rhi_id, block, GL_UNIFORM_BLOCK_BINDING, &actualBinding);
                if (actualBytes != bytes || actualBinding != binding)
                    result->value = std::string("Incorrect block size/binding: ") + name;
            };
            checkBlock("MaterialData", static_cast<GLint>(schema->ByteSize()), 2);
            checkBlock("PbrPassData", sizeof(PbrPassConstants), PbrPassBinding);
            checkBlock("SceneEffectsData", sizeof(SceneEffectsConstants), SceneEffectsBinding);
            for (const auto& parameter : schema->Desc().parameters) {
                const char* name = parameter.name.c_str();
                GLuint index = GL_INVALID_INDEX;
                glGetUniformIndices(program->rhi_id, 1, &name, &index);
                if (index == GL_INVALID_INDEX) { result->value = "Missing uniform: " + parameter.name; break; }
                GLint offset = -1;
                glGetActiveUniformsiv(program->rhi_id, 1, &index, GL_UNIFORM_OFFSET, &offset);
                if (offset != static_cast<GLint>(schema->FindLayout(name)->offset))
                    result->value = "Incorrect std140 offset: " + parameter.name;
            }
            if (glGetError() != GL_NO_ERROR) result->value = "GL error in PBR reflection";
        }, [result] { result->done = true; });
        Wait([result] { return result->done; }, "PBR reflection");
        Require(result->value.empty(), result->value);
    }
    MaterialHandle Register(const std::shared_ptr<const MaterialAsset>& asset, const MaterialPassResources& pass) {
        std::string error;
        const auto handle = materials.Register(asset, pass, &error);
        Require(handle.IsValid(), "Register PBR: " + error);
        return handle;
    }
    void Initialize(const std::string& legacyDirectory) {
        schema = MakePbrTemplate();
        Require(schema != nullptr, "Invalid PBR schema");
        CreateGraphicShaderDesc shaders;
        shaders.vertexShaderSource = Shader(ShaderSourceType::Vertex, PbrVertexShader());
        shaders.fragmentShaderSource = Shader(ShaderSourceType::Fragment, PbrFragmentShader());
        const auto program = Create<ShaderProgramSpec>([&](auto cb) { device.async_CreateGraphicShader(shaders, cb); }, "PBR program");
        InspectShader(program);
        CreatePipelineDesc desc{PbrPipelineSpec(program)};
        common.pipeline = Create<PipelineSpec>([&](auto cb) { device.async_CreatePipeline(desc, cb); }, "PBR pipeline");
        CreateGraphicShaderDesc shadowShaders;
        shadowShaders.vertexShaderSource = Shader(ShaderSourceType::Vertex, PbrShadowVertexShader());
        shadowShaders.fragmentShaderSource = Shader(ShaderSourceType::Fragment, PbrShadowFragmentShader());
        const auto shadowProgram = Create<ShaderProgramSpec>([&](auto cb) { device.async_CreateGraphicShader(shadowShaders, cb); }, "PBR shadow program");
        CreatePipelineDesc shadowDesc{PbrShadowPipelineSpec(shadowProgram)};
        common.shadowPipeline = Create<PipelineSpec>([&](auto cb) { device.async_CreatePipeline(shadowDesc, cb); }, "PBR shadow pipeline");
        common.expectedTemplate = schema;
        common.parameterBufferBytes = static_cast<std::uint32_t>(schema->ByteSize());
        common.parameterBuffer = Uniform(common.parameterBufferBytes);

        const auto white = Texture(MakeTexture(TexturePattern::White, 1));
        const auto normal = Texture(MakeTexture(TexturePattern::FlatNormal, 1));
        const auto copper = Texture(MakeTexture(TexturePattern::CopperAlbedo));
        const auto checker = Texture(MakeTexture(TexturePattern::CheckerAlbedo));
        const auto bump = Texture(MakeTexture(TexturePattern::BumpedNormal));
        const auto metal = Texture(MakeTexture(TexturePattern::Metallic));
        const auto rough = Texture(MakeTexture(TexturePattern::Roughness));
        const auto ao = Texture(MakeTexture(TexturePattern::AmbientOcclusion));
        common.textures = {{0, white}, {1, normal}, {2, white}, {3, white}, {4, white}};
        std::array<RenderResourceHandle<RHITextureSpec>, 4> legacy{checker, bump, rough, ao};
        if (!legacyDirectory.empty()) {
            const std::filesystem::path root(legacyDirectory);
            legacy = {Texture(LoadTexture(root / "square_tiles_03_diff_1k.png")),
                      Texture(LoadTexture(root / "square_tiles_03_nor_gl_1k.png")),
                      Texture(LoadTexture(root / "square_tiles_03_rough_1k.png")),
                      Texture(LoadTexture(root / "square_tiles_03_ao_1k.png"))};
        }
        // Meshes are pinned with the same frame owner as shaders/textures. Even
        // a timed-out submission cannot race eager mesh deletion during shutdown.
        auto sphereData = MakeSphere();
        sphereIndices = static_cast<std::uint32_t>(sphereData.indices.size());
        sphere = Mesh(sphereData);
        auto planeData = MakePlane();
        planeIndices = static_cast<std::uint32_t>(planeData.indices.size());
        plane = Mesh(planeData);
        auto boxData = MakeBox();
        boxIndices = static_cast<std::uint32_t>(boxData.indices.size());
        box = Mesh(boxData);
        owner = RetainMaterialResources(device, std::exchange(owned, {}));
        common.lifetime = owner;
        const auto add = [&](std::string name, std::vector<ParameterOverride> defaults,
                             MaterialPassResources pass, glm::vec3 position) {
            std::string error;
            auto asset = MaterialAsset::Create(schema, std::move(defaults), &error);
            Require(asset != nullptr, error);
            entries.push_back({std::move(name), Register(asset, pass), asset, std::move(pass),
                               glm::translate(glm::mat4(1), position) * glm::scale(glm::mat4(1), glm::vec3(0.87f))});
        };
        const std::array<float, 5> roughness{0.08f, 0.25f, 0.5f, 0.75f, 1.0f};
        for (int row = 0; row < 3; ++row) for (int column = 0; column < 5; ++column) {
            const float metallic = row * 0.5f;
            add("Grid M=" + std::to_string(metallic).substr(0, 3) + " R=" + std::to_string(roughness[column]).substr(0, 4),
                {{"baseColor", glm::vec4(0.72f, 0.29f, 0.10f, 1)}, {"metallic", metallic},
                 {"roughness", roughness[column]}}, common, {(column - 2) * 2.3f, 4.4f - row * 2.2f, 0});
        }
        auto copperPass = common;
        copperPass.textures = {{0, copper}, {1, bump}, {2, metal}, {3, rough}, {4, ao}};
        add("Textured copper", {{"metallic", 1.0f}, {"roughness", 0.3f}, {"useNormalMap", true}}, copperPass, {-3, GalleryFloorHeight + 0.96f, 0});
        entries.back().model = glm::translate(glm::mat4(1), glm::vec3(-3, GalleryFloorHeight + 0.96f, 0)) *
                               glm::scale(glm::mat4(1), glm::vec3(-0.87f, 0.96f, 0.73f));
        auto checkerPass = copperPass;
        checkerPass.textures[0].texture = checker;
        add("All five maps", {{"metallic", 0.0f}, {"roughness", 0.45f}, {"useNormalMap", true},
            {"useMetallicMap", true}, {"useRoughnessMap", true}, {"useAoMap", true}}, checkerPass, {0, GalleryFloorHeight + 0.87f, 0});
        auto legacyPass = common;
        legacyPass.textures = {{0, legacy[0]}, {1, legacy[1]}, {2, white}, {3, legacy[2]}, {4, legacy[3]}};
        add(legacyDirectory.empty() ? "Legacy tile preset (procedural maps)" : "Legacy tile (original maps)",
            {{"metallic", 0.6f}, {"roughness", 0.5f}, {"ao", 1.0f}, {"useNormalMap", true},
             {"useMetallicMap", false}, {"useRoughnessMap", true}, {"useAoMap", true}}, legacyPass, {3, GalleryFloorHeight + 0.87f, 0});
        auto floorAsset = MaterialAsset::Create(schema, {{"baseColor", glm::vec4(0.065f, 0.075f, 0.10f, 1)},
            {"roughness", 0.24f}, {"metallic", 0.35f}, {"reflectionStrength", 0.85f}});
        Require(floorAsset != nullptr, "Invalid floor asset");
        floorMaterial = Register(floorAsset, common);
        auto glowAsset = MaterialAsset::Create(schema, {{"baseColor", glm::vec4(0.8f, 0.25f, 0.05f, 1)},
            {"emissiveColor", glm::vec4(9.0f, 2.5f, 0.5f, 0)}, {"roughness", 0.4f}});
        Require(glowAsset != nullptr, "Invalid emissive material");
        glowMaterial = Register(glowAsset, common);
        auto studyAsset = MaterialAsset::Create(schema, {{"baseColor", glm::vec4(0.65f,0.47f,0.24f,1)},
            {"roughness", 0.82f}, {"metallic", 0.0f}});
        auto studyFloorAsset = MaterialAsset::Create(schema, {{"baseColor", glm::vec4(0.32f,0.36f,0.42f,1)},
            {"roughness", 0.9f}, {"metallic", 0.0f}});
        Require(studyAsset && studyFloorAsset, "Invalid shadow study material");
        studyMaterial = Register(studyAsset, common); studyFloorMaterial = Register(studyFloorAsset, common);
        pipeline = std::make_unique<MaterialRenderPipeline>(device);
        Require(pipeline->Initialize(), pipeline->LastError());
    }
    std::shared_ptr<const MaterialSnapshot> Capture(MaterialHandle handle) {
        std::string error;
        auto snapshot = materials.Capture(handle, {}, &error);
        Require(snapshot != nullptr, error);
        return snapshot;
    }
    void Draw(const FrameSettings& settings) {
        lastWidth = lastHeight = 0;
        lastPresented = true;
        Require(settings.width > 0 && settings.height > 0 && settings.width <= 8192 && settings.height <= 8192,
                "Invalid framebuffer dimensions");
        Require(std::isfinite(settings.exposure) && settings.exposure > 0 && std::isfinite(settings.lightTime) &&
                std::isfinite(settings.cameraYaw) && std::isfinite(settings.cameraPitch) &&
                std::isfinite(settings.cameraDistance) && settings.cameraDistance >= 3.0f,
                "Invalid camera/exposure");
        const bool isolated = !settings.shadowStudy && settings.isolatedMaterial >= 0;
        Require(!isolated || static_cast<std::size_t>(settings.isolatedMaterial) < entries.size(), "Invalid material index");
        const glm::vec3 target = settings.shadowStudy ? glm::vec3(0, 0.5f, -0.8f) : isolated ? glm::vec3(0) : glm::vec3(0, 0.7f, 0);
        const float distance = settings.shadowStudy ? settings.cameraDistance * 0.65f : isolated ? std::max(2.4f, settings.cameraDistance / 4.5f) : settings.cameraDistance;
        const float pitch = glm::clamp(settings.cameraPitch, -1.3f, 1.3f);
        const glm::vec3 eye = target + distance * glm::vec3(std::sin(settings.cameraYaw) * std::cos(pitch),
            std::sin(pitch), std::cos(settings.cameraYaw) * std::cos(pitch));
        RenderFrame frame;
        frame.frameIndex = ++frameIndex;
        const auto item = [&](MaterialHandle material, auto mesh, std::uint32_t indices, glm::mat4 model) {
            RenderItem result;
            result.mesh = mesh; result.draw.indexCount = indices; result.model = model;
            result.material = Capture(material);
            return result;
        };
        if (settings.shadowStudy) {
            frame.items.push_back(item(studyFloorMaterial, plane, planeIndices, glm::scale(glm::mat4(1), glm::vec3(7,1,7))));
            frame.items.push_back(item(studyMaterial, sphere, sphereIndices,
                glm::translate(glm::mat4(1), glm::vec3(-2,1,0))));
            frame.items.push_back(item(studyMaterial, box, boxIndices,
                glm::translate(glm::mat4(1), glm::vec3(1.2f,0.9f,0)) * glm::scale(glm::mat4(1),glm::vec3(0.9f))));
            frame.items.push_back(item(studyMaterial, box, boxIndices,
                glm::translate(glm::mat4(1), glm::vec3(-0.3f,0.7f,2)) * glm::scale(glm::mat4(1),glm::vec3(0.06f,0.7f,0.8f))));
            const float slope = glm::radians(15.0f);
            frame.items.push_back(item(studyFloorMaterial, plane, planeIndices,
                glm::translate(glm::mat4(1), glm::vec3(0,2.5f*std::sin(slope),-3.5f)) *
                glm::rotate(glm::mat4(1), slope, glm::vec3(1,0,0)) * glm::scale(glm::mat4(1),glm::vec3(3.8f,1,2.5f))));
        } else if (isolated) frame.items.push_back(item(entries[settings.isolatedMaterial].handle, sphere, sphereIndices, glm::mat4(1)));
        else {
            for (const auto& entry : entries) frame.items.push_back(item(entry.handle, sphere, sphereIndices, entry.model));
            frame.items.push_back(item(floorMaterial, plane, planeIndices,
                glm::translate(glm::mat4(1), glm::vec3(0, GalleryFloorHeight, 0)) * glm::scale(glm::mat4(1), glm::vec3(9, 1, 6))));
            frame.items.back().castsShadows = false;
            frame.items.back().visibleInReflections = false;
            for (float x : {-5.8f, 5.8f}) {
                frame.items.push_back(item(glowMaterial, sphere, sphereIndices,
                    glm::translate(glm::mat4(1), glm::vec3(x, -1.7f, 0)) * glm::scale(glm::mat4(1), glm::vec3(0.25f))));
                frame.items.back().castsShadows = false;
            }
        }
        PbrPassConstants lighting;
        const std::array<glm::vec3, 4> positions{{{-5, 7, 6}, {5, 4, 5}, {-3, -2, 4}, {4, 1, -4}}};
        const std::array<glm::vec3, 4> colors{{{100, 92, 80}, {75, 88, 100}, {32, 38, 50}, {100, 80, 62}}};
        const float angle = settings.lightTime * 0.5f;
        for (std::size_t i = 0; i < positions.size(); ++i) {
            const auto& p = positions[i];
            lighting.lightPositions[i] = glm::vec4(p.x * std::cos(angle) - p.z * std::sin(angle), p.y,
                                                 p.x * std::sin(angle) + p.z * std::cos(angle), 1);
            lighting.lightColors[i] = glm::vec4(colors[i], 1);
        }
        auto effects = settings.effects;
        effects.lighting = lighting;
        effects.lighting.specularAA = settings.effects.lighting.specularAA;
        effects.post.exposure = settings.exposure;
        effects.time = settings.lightTime;
        if (isolated) effects.reflection.enabled = false;
        if (settings.shadowStudy) {
            effects.reflection.enabled = false;
            effects.lighting.ambientAndExposure = glm::vec4(0.10f,0.10f,0.10f,1);
            for (auto& color : effects.lighting.lightColors) color = glm::vec4(0);
        }
        PipelineCamera camera;
        camera.position = eye;
        camera.view = glm::lookAt(eye, target, glm::vec3(0, 1, 0));
        camera.projection = glm::perspective(glm::radians(42.0f), float(settings.width) / settings.height, 0.1f, 100.0f);
        Require(pipeline->Resize(settings.width, settings.height, effects.shadows.atlasResolution,
            effects.temporal.msaaSamples, effects.reflection.resolutionScale), pipeline->LastError());
        RHICommand::BeginFrame begin;
        begin.frameIndex = frameIndex;
        begin.framebufferWidth = settings.width; begin.framebufferHeight = settings.height;
        begin.clearColor = glm::vec4(0.027f, 0.035f, 0.052f, 1);
        auto encoder = device.BeginFrame(begin);
        if (!encoder.KeepAlive(owner) || !pipeline->Record(encoder, frame, camera, effects) || !encoder.End(settings.present)) {
            encoder.Cancel();
            if (pipeline->RecordedFrameToken()) pipeline->DiscardFrame(pipeline->RecordedFrameToken());
            throw std::runtime_error("PBR frame recording failed: " + pipeline->LastError());
        }
        auto completion = std::make_shared<Completion<bool>>();
        if (!device.async_SubmitFrameCommands(encoder.GetCommandBuffer(), [completion] { completion->done = true; })) {
            encoder.Cancel();
            pipeline->DiscardFrame(pipeline->RecordedFrameToken());
            throw std::runtime_error("PBR frame submission failed");
        }
        Wait([completion] { return completion->done; }, "PBR frame completion");
        Require(pipeline->CompleteFrame(pipeline->RecordedFrameToken()), pipeline->LastError());
        lastWidth = settings.width; lastHeight = settings.height; lastPresented = settings.present;
    }
    FramePixels ReadPixels() {
        Require(lastWidth != 0 && !lastPresented, "Readback requires a completed Render(present=false)");
        struct Result { FramePixels pixels; GLenum error = GL_NO_ERROR; };
        auto result = std::make_shared<Completion<Result>>();
        result->value.pixels = {lastWidth, lastHeight, std::vector<std::uint8_t>(static_cast<std::size_t>(lastWidth) * lastHeight * 4)};
        device.async_ExecuteCode([result] {
            auto& pixels = result->value.pixels;
            glReadPixels(0, 0, pixels.width, pixels.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.rgba.data());
            result->value.error = glGetError();
        }, [result] { result->done = true; });
        Wait([result] { return result->done; }, "PBR frame readback");
        Require(result->value.error == GL_NO_ERROR, "OpenGL error during PBR rendering/readback");
        return std::move(result->value.pixels);
    }
    glm::vec3 InspectBuffers() {
        Require(lastWidth != 0, "Draw a frame before inspecting pass buffers");
        auto result = std::make_shared<Completion<glm::vec3>>();
        auto error = std::make_shared<GLenum>(GL_NO_ERROR);
        const auto textures = pipeline->DebugTextures();
        device.async_ExecuteCode([this, result, error, textures] {
            const auto extrema = [&](RenderResourceHandle<RHITextureSpec> handle, bool depth, bool minimum) {
                const auto* texture = device.GetResourcePool().TextureTable.Get(handle);
                if (!texture) return -1.0f;
                std::vector<float> data(static_cast<std::size_t>(texture->width) * texture->height * (depth ? 1 : 4));
                glGetTextureImage(texture->rhi_id, 0, depth ? GL_DEPTH_COMPONENT : GL_RGBA, GL_FLOAT,
                                  static_cast<GLsizei>(data.size() * sizeof(float)), data.data());
                float value = minimum ? 1e10f : -1e10f;
                for (std::size_t i = 0; i < data.size(); ++i) {
                    if (!depth && i % 4 == 3) continue;
                    if (!std::isfinite(data[i])) { value = -1; break; }
                    value = minimum ? std::min(value, data[i]) : std::max(value, data[i]);
                }
                return value;
            };
            result->value.x = extrema(textures.shadowAtlas, true, true);
            result->value.y = extrema(textures.ambientOcclusion, false, true);
            result->value.z = extrema(textures.sceneColor, false, false);
            *error = glGetError();
        }, [result] { result->done = true; });
        Wait([result] { return result->done; }, "pipeline attachment inspection");
        Require(*error == GL_NO_ERROR, "GL error inspecting HDR/depth/AO attachments");
        return result->value;
    }
};

MaterialLabModule::MaterialLabModule(OpenglBackendContext context, std::string legacyTextureDirectory)
    : context_(std::move(context)), legacyTextureDirectory_(std::move(legacyTextureDirectory)) {}
MaterialLabModule::~MaterialLabModule() { Shutdown(); }
bool MaterialLabModule::Startup() {
    if (impl_) return true;
    error_.clear();
    try {
        impl_ = std::make_unique<Impl>(context_);
        impl_->Initialize(legacyTextureDirectory_);
        return true;
    } catch (const std::exception& error) {
        error_ = error.what(); Shutdown(); return false;
    }
}
void MaterialLabModule::Shutdown() { impl_.reset(); }
bool MaterialLabModule::IsStarted() const noexcept { return impl_ != nullptr; }
std::size_t MaterialLabModule::MaterialCount() const { return impl_ ? impl_->entries.size() : 0; }
MaterialInfo MaterialLabModule::InspectMaterial(std::size_t index) const {
    if (!impl_ || index >= impl_->entries.size()) return {};
    auto& entry = impl_->entries[index];
    const auto snapshot = impl_->Capture(entry.handle);
    const auto& parameters = snapshot->Parameters();
    return {entry.name, ReadValue<float>(parameters, "metallic"), ReadValue<float>(parameters, "roughness"),
            ReadValue<std::uint32_t>(parameters, "useNormalMap") != 0,
            ReadValue<std::uint32_t>(parameters, "useMetallicMap") != 0,
            ReadValue<std::uint32_t>(parameters, "useRoughnessMap") != 0,
            ReadValue<std::uint32_t>(parameters, "useAoMap") != 0};
}
bool MaterialLabModule::SetMaterialParameter(std::size_t index, std::string_view name, const ParameterValue& value) {
    error_.clear();
    if (!impl_ || index >= impl_->entries.size()) { error_ = "Invalid material index"; return false; }
    return impl_->materials.SetParameter(impl_->entries[index].handle, name, value, &error_);
}
bool MaterialLabModule::ResetMaterial(std::size_t index) {
    error_.clear();
    if (!impl_ || index >= impl_->entries.size()) { error_ = "Invalid material index"; return false; }
    auto& entry = impl_->entries[index];
    return impl_->materials.Replace(entry.handle, entry.asset, entry.pass, &error_);
}
bool MaterialLabModule::Render(const FrameSettings& settings) {
    error_.clear();
    if (!impl_) { error_ = "MaterialLab is not started"; return false; }
    try { impl_->Draw(settings); return true; }
    catch (const std::exception& error) { error_ = error.what(); return false; }
}
FramePixels MaterialLabModule::Readback() {
    error_.clear();
    if (!impl_) { error_ = "MaterialLab is not started"; return {}; }
    try { return impl_->ReadPixels(); }
    catch (const std::exception& error) { error_ = error.what(); return {}; }
}
Render::PipelineStatistics MaterialLabModule::PipelineStatistics() const {
    return impl_ && impl_->pipeline ? impl_->pipeline->Statistics() : Render::PipelineStatistics{};
}
glm::vec3 MaterialLabModule::InspectPipelineBuffers() {
    error_.clear();
    if (!impl_) { error_ = "MaterialLab is not started"; return glm::vec3(-1); }
    try { return impl_->InspectBuffers(); }
    catch (const std::exception& error) { error_ = error.what(); return glm::vec3(-1); }
}

} // namespace MaterialLab
