#include "MaterialLab/Public/material_lab_module.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <future>
#include <map>
#include <stdexcept>
#include <utility>

#include <glad/glad.h>
#include <glfw/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "MaterialLab/Public/lab_geometry.h"
#include "MaterialLab/Private/showcase_hud.h"
#include "Render/Public/Pipeline/material_render_pipeline.h"
#include "Render/Private/Material/rhi_material_resources.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"

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
    MaterialPassResources translucent;
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
    MeshData probeSphere,probePlane,probeBox;
    MeshData showcaseProbeSphere = MakeSphere(24, 32);
    std::array<MaterialHandle, std::size_t(ShowcaseMaterial::Count)> showcaseMaterials{};
    bool showcaseMaterialsReady=false,showcaseEmission=true;
    std::array<MaterialHandle,std::size_t(GiRoomMaterial::Count)> giRoomMaterials{};
    bool giRoomMaterialsReady=false;
    std::shared_ptr<const LumenGiScene> giRoomScene;
    std::uint64_t giRoomHash=0;
    struct ShowcaseBatch {
        ShowcaseMaterial material;
        RenderResourceHandle<VertexBufferSpec> mesh;
        std::uint32_t indices;
        bool castsShadows;
        MeshData giGeometry;
    };
    std::vector<ShowcaseBatch> showcaseBatches;
    RenderResourceHandle<PipelineSpec> showcaseHudPipeline;
    RenderResourceHandle<RHITextureSpec> showcaseHudTexture;
    std::shared_ptr<const void> showcaseHudOwner;
    std::string showcaseHudText;
    std::shared_ptr<const LumenGiScene> showcaseGiScene;
    RenderResourceHandle<RHITextureSpec> showcaseWaterNormal,showcaseWaterFlow;
    std::uint64_t showcaseGiHash=0,showcaseRequestedHash=0;
    struct TextureAverage { glm::vec3 raw{1},linear{1}; };
    std::map<std::uint32_t,TextureAverage> textureAverages;
    std::shared_ptr<const DiffuseProbeVolume> cachedProbes;
    std::uint64_t cachedProbeHash=0,pendingProbeHash=0;
    std::uint64_t cachedRealtimeHash=0;
    std::shared_ptr<const RealtimeGiScene> cachedRealtimeScene;
    std::shared_ptr<const LumenGiScene> cachedLumenScene;
    std::uint64_t cachedLumenHash=0;
    std::future<std::shared_ptr<const DiffuseProbeVolume>> probeBakeJob;
    MaterialHandle floorMaterial;
    MaterialHandle glowMaterial;
    MaterialHandle studyMaterial, studyFloorMaterial;
    MaterialHandle bounceWhite, bounceRed;
    std::unique_ptr<MaterialRenderPipeline> pipeline;
    std::uint64_t frameIndex = 0;
    std::uint32_t lastWidth = 0, lastHeight = 0;
    bool lastPresented = true;

    template<class Context> explicit Impl(const Context& context) {
        meshes = std::shared_ptr<MeshLease>(new MeshLease{device, {}});
        owned.dependencies.push_back(meshes);
        if constexpr(std::is_same_v<Context,VulkanBackendContext>) {
            Require(RegisterVulkanBackend(),"Vulkan backend requires the Vulkan SDK and shaderc at build time");
            device.Run(BackendType::Vulkan,context);
        } else device.Run(BackendType::Opengl, context);
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
        showcaseHudOwner.reset();
        materials.Clear();
        entries.clear();
        common = {};
        translucent = {};
        meshes.reset();
        owner.reset();
        RetainMaterialResources(device, std::exchange(owned, {})).reset();
        device.StopAndRelease();
        while (!device.returnSystem.callbacks.empty()) device.returnSystem.DrainCallbacks();
    }
    template <class Predicate> void Wait(Predicate complete, const char* operation,std::chrono::seconds timeout=10s) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
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
        glm::dvec3 raw(0),linear(0);
        for(std::size_t i=0;i<pixels.rgba.size();i+=4) {
            for(int c=0;c<3;++c) {
                const double value=pixels.rgba[i+c]/255.0; raw[c]+=value;
                linear[c]+=value<=.04045?value/12.92:std::pow((value+.055)/1.055,2.4);
            }
        }
        const double count=double(pixels.width)*pixels.height;
        textureAverages[handle.id]={glm::vec3(raw/count),glm::vec3(linear/count)};
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
            if(device.GetBackendType()==BackendType::Vulkan) {
                for(auto [name,bytes,binding]:{std::tuple{"MaterialData",uint32_t(schema->ByteSize()),2u},std::tuple{"PbrPassData",uint32_t(sizeof(PbrPassConstants)),PbrPassBinding},std::tuple{"SceneEffectsData",uint32_t(sizeof(SceneEffectsConstants)),SceneEffectsBinding}}) {
                    auto block=device.BackendDiagnostics()->ReflectUniformBlock(shader,name);
                    if(!block||block->bytes!=bytes||block->binding!=binding)result->value=std::string("Incorrect Vulkan std140 block: ")+name;
                    if(block&&std::string(name)=="MaterialData")for(const auto& parameter:schema->Desc().parameters) {
                        auto member=block->offsets.find(parameter.name);if(member!=block->offsets.end()&&member->second!=schema->FindLayout(parameter.name)->offset)result->value="Incorrect Vulkan std140 offset: "+parameter.name;
                    }
                }
                return;
            }
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
                // Domain/output variants may optimize unrelated parameters
                // away; active members must still match the shared schema.
                if (index == GL_INVALID_INDEX) continue;
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
        const auto variant = [&](Domain domain, PbrOutput output) {
            CreateGraphicShaderDesc sources;
            sources.vertexShaderSource = Shader(ShaderSourceType::Vertex, PbrVertexShader(domain));
            sources.fragmentShaderSource = Shader(ShaderSourceType::Fragment, PbrFragmentShader(domain, output));
            const auto program = Create<ShaderProgramSpec>([&](auto cb) { device.async_CreateGraphicShader(sources, cb); }, "advanced PBR program");
            auto state = PbrPipelineSpec(program, domain);
            if (output != PbrOutput::Surface) state.depthWrite = false;
            return Create<PipelineSpec>([&](auto cb) { device.async_CreatePipeline({state}, cb); }, "advanced PBR variant");
        };
        common.diffuseRadiancePipeline = variant(Domain::Surface, PbrOutput::DiffuseRadiance);
        common.normalPipeline = variant(Domain::Surface, PbrOutput::WorldNormal);
        translucent.expectedTemplate = MakePbrTemplate(Domain::Translucent);
        translucent.pipeline = variant(Domain::Translucent, PbrOutput::Surface);
        translucent.parameterBufferBytes = static_cast<std::uint32_t>(translucent.expectedTemplate->ByteSize());
        translucent.parameterBuffer = Uniform(translucent.parameterBufferBytes);

        const auto white = Texture(MakeTexture(TexturePattern::White, 1));
        const auto normal = Texture(MakeTexture(TexturePattern::FlatNormal, 1));
        const auto copper = Texture(MakeTexture(TexturePattern::CopperAlbedo));
        const auto checker = Texture(MakeTexture(TexturePattern::CheckerAlbedo));
        const auto bump = Texture(MakeTexture(TexturePattern::BumpedNormal));
        const auto metal = Texture(MakeTexture(TexturePattern::Metallic));
        const auto rough = Texture(MakeTexture(TexturePattern::Roughness));
        const auto ao = Texture(MakeTexture(TexturePattern::AmbientOcclusion));
        showcaseWaterNormal=Texture(MakeTexture(TexturePattern::WaterNormal));
        showcaseWaterFlow=Texture(MakeTexture(TexturePattern::WaterFlow));
        common.textures = {{0, white}, {1, normal}, {2, white}, {3, white}, {4, white}};
        translucent.textures = {{0, white}, {1, bump}, {2, white}, {3, white}, {4, ao}, {5, bump}, {6, bump}, {7, checker}};
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
        probeSphere=std::move(sphereData); probePlane=std::move(planeData); probeBox=std::move(boxData);
        owner = RetainMaterialResources(device, std::exchange(owned, {}));
        common.lifetime = owner;
        translucent.lifetime = owner;
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
        auto glassAsset = MaterialAsset::Create(translucent.expectedTemplate, {
            {"baseColor", glm::vec4(.74f,.92f,1,1)}, {"roughness", .12f}, {"opacity", .9f},
            {"transmission", .94f}, {"ior", 1.45f}, {"thickness", .35f}, {"distortionStrength", 12.0f},
            {"absorptionColor", glm::vec4(.45f,.82f,.96f,1)}});
        auto waterAsset = MaterialAsset::Create(translucent.expectedTemplate, {
            {"baseColor", glm::vec4(.3f,.8f,.88f,1)}, {"roughness", .18f}, {"opacity", .85f},
            {"transmission", .88f}, {"ior", 1.333f}, {"thickness", .2f}, {"distortionStrength", 18.0f},
            {"useNormalMap", true}, {"useDistortionMap", true}, {"useFlowMap", true}, {"flowStrength", .15f},
            {"uvFlow", glm::vec4(.08f,.035f,-.04f,.06f)}, {"useAoMap", true}, {"twoSided", true}});
        auto emissionPass = common; emissionPass.textures.push_back({7, checker});
        auto emissionAsset = MaterialAsset::Create(schema, {{"baseColor", glm::vec4(.12f,.04f,.01f,1)},
            {"emissiveColor", glm::vec4(7,1.5f,.25f,0)}, {"emissiveIntensity", 1.0f}, {"useEmissiveMap", true}});
        Require(glassAsset && waterAsset && emissionAsset, "Invalid transmission/emission study material");
        entries.push_back({"Tinted glass", Register(glassAsset, translucent), glassAsset, translucent});
        entries.push_back({"Flowing refractive water", Register(waterAsset, translucent), waterAsset, translucent});
        entries.push_back({"Textured emissive panel", Register(emissionAsset, emissionPass), emissionAsset, emissionPass});
        auto whiteAsset = MaterialAsset::Create(schema, {{"baseColor", glm::vec4(.65f,.65f,.65f,1)}, {"roughness", .85f}});
        auto redAsset = MaterialAsset::Create(schema, {{"baseColor", glm::vec4(.72f,.035f,.015f,1)}, {"roughness", .85f}});
        bounceWhite = Register(whiteAsset, common); bounceRed = Register(redAsset, common);
        pipeline = std::make_unique<MaterialRenderPipeline>(device);
        Require(pipeline->Initialize(), pipeline->LastError());
    }
    std::shared_ptr<const MaterialSnapshot> Capture(MaterialHandle handle) {
        std::string error;
        auto snapshot = materials.Capture(handle, {}, &error);
        Require(snapshot != nullptr, error);
        return snapshot;
    }
    void InitializeShowcase() {
        if(showcaseMaterialsReady)return;
        using M=ShowcaseMaterial;
        const auto add=[&](M slot,std::vector<ParameterOverride> defaults,MaterialPassResources pass) {
            auto asset=MaterialAsset::Create(pass.expectedTemplate,std::move(defaults));
            Require(bool(asset),"Invalid showcase material");showcaseMaterials[std::size_t(slot)]=Register(asset,pass);
        };
        const auto solid=[&](M slot,glm::vec3 color,float roughness,float metallic=0) {
            add(slot,{{"baseColor",glm::vec4(color,1)},{"roughness",roughness},{"metallic",metallic}},common);
        };
        solid(M::Stone,{.32f,.36f,.4f},.84f);solid(M::Plaster,{.72f,.69f,.61f},.86f);
        solid(M::Terracotta,{.56f,.09f,.035f},.78f);solid(M::Wood,{.20f,.085f,.028f},.66f);
        solid(M::Leaves,{.065f,.24f,.045f},.9f);solid(M::Gold,{.88f,.59f,.19f},.22f,1);
        solid(M::Chrome,{.86f,.9f,.94f},.09f,1);solid(M::Ceramic,{.10f,.3f,.35f},.32f);
        auto tilePass=entries[17].pass;
        add(M::Tiles,{{"baseColor",glm::vec4(.65f,.67f,.70f,1)},{"roughness",.5f},{"useNormalMap",true},
            {"useRoughnessMap",true},{"useAoMap",true},{"uvTransform",glm::vec4(4,3,0,0)}},tilePass);
        add(M::Glass,{{"baseColor",glm::vec4(.74f,.92f,1,1)},{"roughness",.10f},{"opacity",.9f},
            {"transmission",.94f},{"ior",1.45f},{"thickness",.35f},{"distortionStrength",9.f},
            {"absorptionColor",glm::vec4(.45f,.82f,.96f,1)}},translucent);
        auto waterPass=translucent;
        for(auto& binding:waterPass.textures) {
            if(binding.slot==1||binding.slot==5)binding.texture=showcaseWaterNormal;
            if(binding.slot==6)binding.texture=showcaseWaterFlow;
        }
        add(M::Water,{{"baseColor",glm::vec4(.14f,.49f,.57f,1)},{"roughness",.12f},{"opacity",.86f},
            {"transmission",.86f},{"ior",1.333f},{"thickness",.22f},{"distortionStrength",12.f},
            {"useNormalMap",true},{"useDistortionMap",true},{"useFlowMap",true},{"flowStrength",.12f},
            {"uvFlow",glm::vec4(.04f,.02f,-.02f,.035f)},{"uvTransform",glm::vec4(2,3,0,0)},
            {"reflectionStrength",.75f},{"twoSided",true}},waterPass);
        add(M::WarmLamp,{{"baseColor",glm::vec4(.85f,.65f,.35f,1)},{"emissiveColor",glm::vec4(5,3.5f,1.8f,0)},
            {"roughness",.4f},{"twoSided",true}},common);
        add(M::CeilingLamp,{{"baseColor",glm::vec4(.85f,.65f,.35f,1)},{"emissiveColor",glm::vec4(5,3.5f,1.8f,0)},
            {"roughness",.4f}},common);
        add(M::CoolLamp,{{"baseColor",glm::vec4(.22f,.55f,.85f,1)},{"emissiveColor",glm::vec4(1,3,7,0)},
            {"roughness",.4f}},common);
        add(M::EmissivePanel,{{"baseColor",glm::vec4(.5f,.12f,.025f,1)},{"emissiveColor",glm::vec4(4,1.1f,.28f,0)},
            {"roughness",.5f},{"twoSided",true}},common);
        // Static construction has many small parts. Merge equal-material parts
        // in world space; movable props retain individual model matrices. This
        // is an upper-layer mesh build, with no RHI instancing/API changes.
        struct BuildBatch { MeshData visible,gi; };
        std::map<std::pair<M,bool>,BuildBatch> batches;
        const auto append=[](MeshData& destination,const MeshData& source,const glm::mat4& model) {
            const auto base=std::uint32_t(destination.vertices.size());
            const auto normalMatrix=glm::transpose(glm::inverse(glm::mat3(model)));
            for(auto v:source.vertices) {
                v.position=glm::vec3(model*glm::vec4(v.position,1));v.normal=glm::normalize(normalMatrix*v.normal);
                v.tangent=glm::vec4(glm::normalize(glm::mat3(model)*glm::vec3(v.tangent)),v.tangent.w);
                destination.vertices.push_back(v);
            }
            for(auto index:source.indices)destination.indices.push_back(base+index);
        };
        for(const auto& o:ShowcaseObjects())if(o.movable<0&&o.material!=M::Water) {
            auto& batch=batches[{o.material,o.castsShadows}];const auto model=ShowcaseTransform(o,{});
            const auto& visible=o.mesh==ShowcaseMesh::Sphere?probeSphere:o.mesh==ShowcaseMesh::Plane?probePlane:probeBox;
            const auto& gi=o.mesh==ShowcaseMesh::Sphere?showcaseProbeSphere:visible;
            append(batch.visible,visible,model);append(batch.gi,gi,model);
        }
        for(auto& [key,data]:batches)showcaseBatches.push_back({key.first,Mesh(data.visible),
            std::uint32_t(data.visible.indices.size()),key.second,std::move(data.gi)});
        showcaseMaterialsReady=true;
    }
    void InitializeGiRoom() {
        if(giRoomMaterialsReady)return;
        const std::array<glm::vec3,std::size_t(GiRoomMaterial::Count)> colors{{
            {.72f,.72f,.72f},{.62f,.035f,.022f},{.035f,.48f,.065f},{.55f,.55f,.55f},{.64f,.64f,.64f}}};
        for(std::size_t i=0;i<colors.size();++i) {
            auto asset=MaterialAsset::Create(common.expectedTemplate,{{"baseColor",glm::vec4(colors[i],1)},
                {"roughness",.95f},{"metallic",0.f}});
            Require(bool(asset),"Invalid GI room material");giRoomMaterials[i]=Register(asset,common);
        }
        giRoomMaterialsReady=true;
    }
    void AddShowcaseHud(RenderFrame& frame,const FrameSettings& settings) {
        const bool updating=showcaseRequestedHash!=showcaseGiHash;
        const auto output=settings.OutputExtent();
        AddLabHud(frame,settings,ShowcaseHudText(settings.showcaseState,settings.effects.lumenGi.enabled,updating,
                                               {settings.width,settings.height},output));
    }
    void AddGiRoomHud(RenderFrame& frame,const FrameSettings& settings) {
        const auto& state=settings.giRoomState;
        const auto mode=settings.effects.lumenGi.lightingView;
        const char* view=mode==LumenLightingView::IndirectOnly?"BOUNCE ONLY":mode==LumenLightingView::DirectOnly?"FIRST DIRECT":"TOTAL GI";
        const auto output=settings.OutputExtent();
        std::string text="SEALED GI ROOM / "+std::string(view)+"\n";
        text+="POINT "+std::to_string(state.pointLight)+"  FLASHLIGHT "+std::to_string(state.flashlight)+"  WORLD CACHE\n";
        text+="SELECTED "+std::to_string(state.selected+1)+": "+GiRoomMovable(state.selected).name+"\n";
        if(state.help) {
            text+="RMB LOOK  WASD MOVE  Q/E DOWN/UP  SHIFT FAST\n";
            text+="F FLASHLIGHT  F6 POINT  G LIGHTING VIEW\n";
            text+="1 ROOM  2 BLOCKER  3 CEILING  P RESOLUTION\n";
            text+="CLICK/TAB SELECT  ARROWS MOVE X/Z  PGUP/PGDN Y\n";
            text+="Z/X ROTATE  R RESET PROP  BACKSPACE RESET ALL\n";
            text+="F1 HELP  NO SKY / AO / BLOOM / REFLECTIONS\n";
        }
        text+="RENDER "+std::to_string(settings.width)+"X"+std::to_string(settings.height)+"  OUTPUT "+
            std::to_string(output.x)+"X"+std::to_string(output.y);
        if(std::isfinite(settings.displayFps)&&settings.displayFps>0)text+="  FPS "+std::to_string(int(std::lround(settings.displayFps)));
        AddLabHud(frame,settings,text);
    }
    void AddLabHud(RenderFrame& frame,const FrameSettings& settings,const std::string& text) {
        const auto output=settings.OutputExtent();
        if(!showcaseHudOwner) {
            PipelineDetail::ResourceBuilder b(device);
            CreateGraphicShaderDesc shaders;
            shaders.vertexShaderSource=b.Source(ShaderSourceType::Vertex,R"GLSL(#version 450 core
layout(location=0) in vec3 aPosition;
layout(location=2) in vec2 aTexCoord;
layout(std140,binding=1) uniform ObjectData { mat4 uModel; };
out vec2 vUV;
void main(){vUV=aTexCoord;gl_Position=uModel*vec4(aTexCoord,0,1);}
)GLSL");
            shaders.fragmentShaderSource=b.Source(ShaderSourceType::Fragment,R"GLSL(#version 450 core
layout(binding=0) uniform sampler2D hud;
in vec2 vUV;layout(location=0) out vec4 outColor;
void main(){outColor=texture(hud,vUV);}
)GLSL");
            const auto program=b.Create<ShaderProgramSpec>([&](auto cb){device.async_CreateGraphicShader(shaders,cb);});
            PipelineSpec state;state.shaderProgram=program;state.expectVertexLayout=PbrVertexLayout();
            state.depthTest=state.depthWrite=false;state.cullMode=CullMode::None;state.blendEnable=true;state.blendMode=BlendMode::Alpha;
            showcaseHudPipeline=b.Create<PipelineSpec>([&](auto cb){device.async_CreatePipeline({state},cb);});
            auto image=RasterizeShowcaseHud(text);CreateRHITextureSpec desc;
            desc.width=image.width;desc.height=image.height;desc.data=image.rgba.data();desc.mipmaps=false;desc.filterMode=RHIFilterMode::Nearest;
            showcaseHudTexture=b.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(desc,cb);});
            showcaseHudOwner=b.Finish();showcaseHudText=text;
        } else if(text!=showcaseHudText) {
            auto image=RasterizeShowcaseHud(text);UpdateRHITextureDesc desc;
            desc.width=image.width;desc.height=image.height;desc.data=image.rgba.data();desc.byteSize=image.rgba.size();
            auto done=std::make_shared<Completion<bool>>();
            device.async_UpdateTexture(showcaseHudTexture,desc,[done](bool ok){done->value=ok;done->done=true;});
            Wait([done]{return done->done;},"showcase HUD upload");Require(done->value,"Cannot update showcase HUD");showcaseHudText=text;
        }
        const float scale=std::min(1.f,float(output.x)/720.f);
        const float w=688*scale,h=206*scale;
        RenderItem hud;hud.mesh=plane;hud.draw.indexCount=planeIndices;hud.pipeline=showcaseHudPipeline;
        hud.texture=showcaseHudTexture;hud.layer=RenderLayer::Overlay;hud.castsShadows=hud.visibleInReflections=false;
        hud.model=glm::translate(glm::mat4(1),glm::vec3(-1+16.f/output.x,1-(16+h)*2/output.y,0))*
            glm::scale(glm::mat4(1),glm::vec3(w*2/output.x,h*2/output.y,1));
        frame.items.push_back(hud);
    }
    std::vector<ProbeTriangle> ExtractStudyTriangles(const RenderFrame& frame,bool showcase=false) {
        std::vector<ProbeTriangle> triangles;
        for(const auto& item:frame.items) {
            if(!item.material||item.EffectiveLayer()==RenderLayer::Transparent||item.EffectiveLayer()==RenderLayer::Overlay) continue;
            const MeshData* geometry=item.mesh==sphere?(showcase?&showcaseProbeSphere:&probeSphere):item.mesh==plane?&probePlane:item.mesh==box?&probeBox:nullptr;
            if(showcase&&!geometry)for(const auto& batch:showcaseBatches)if(batch.mesh==item.mesh){geometry=&batch.giGeometry;break;}
            if(!geometry) continue;
            const auto& p=item.material->Parameters();
            auto albedo=glm::vec3(ReadValue<glm::vec4>(p,"baseColor"));
            auto emission=glm::vec3(ReadValue<glm::vec4>(p,"emissiveColor"))*ReadValue<float>(p,"emissiveIntensity");
            float metallic=ReadValue<float>(p,"metallic");
            float roughness=ReadValue<float>(p,"roughness");
            for(const auto& binding:item.material->Resources().textures) {
                const auto found=textureAverages.find(binding.texture.id); if(found==textureAverages.end()) continue;
                const auto average=found->second;
                if(binding.slot==0) albedo*=ReadValue<std::uint32_t>(p,"baseColorIsSRGB")?average.linear:average.raw;
                if(binding.slot==2&&ReadValue<std::uint32_t>(p,"useMetallicMap")) metallic=average.raw.r;
                if(binding.slot==3&&ReadValue<std::uint32_t>(p,"useRoughnessMap")) roughness=average.raw.r;
                if(binding.slot==7&&ReadValue<std::uint32_t>(p,"useEmissiveMap")) emission*=average.raw;
            }
            const auto reflectance=glm::clamp(albedo*(.95f*(1-std::clamp(metallic,0.0f,1.0f))),glm::vec3(0),glm::vec3(1));
            emission=glm::clamp(emission,glm::vec3(0),glm::vec3(60000));
            for(std::size_t i=0;i<geometry->indices.size();i+=3) {
                const auto vertex=[&](std::size_t n) { return glm::vec3(item.model*glm::vec4(geometry->vertices[geometry->indices[i+n]].position,1)); };
                auto a=vertex(0),b=vertex(1),c=vertex(2);
                if(glm::length(glm::cross(b-a,c-a))<1e-7f) continue;
                if(glm::determinant(glm::mat3(item.model))<0) std::swap(b,c);
                triangles.push_back({a,b,c,reflectance,emission,ReadValue<std::uint32_t>(p,"twoSided")!=0,
                    glm::mix(glm::vec3(.04f),glm::clamp(albedo,glm::vec3(0),glm::vec3(1)),std::clamp(metallic,0.f,1.f)),std::clamp(roughness,.045f,1.f),item.analyticEmission});
            }
        }
        return triangles;
    }
    void PrepareRealtimeStudy(const RenderFrame& frame,MaterialPipelineSettings& effects,bool showcase=false,bool room=false) {
        effects.probes.enabled=false;effects.indirect.enabled=false;
        const bool lumen=effects.lumenGi.enabled;
        if(lumen)effects.realtimeGi.enabled=false;
        if(lumen?bool(effects.lumenGi.scene):bool(effects.realtimeGi.scene))return;
        std::uint64_t hash=14695981039346656037ull;
        const auto bytes=[&](const void* data,std::size_t size){const auto* p=static_cast<const unsigned char*>(data);for(std::size_t i=0;i<size;++i){hash^=p[i];hash*=1099511628211ull;}};
        const auto add=[&](const auto& value){bytes(&value,sizeof(value));};
        for(const auto& item:frame.items) {
            if(!item.material||item.EffectiveLayer()==RenderLayer::Transparent)continue;
            add(item.mesh);add(item.model);add(item.analyticEmission);const auto& parameters=item.material->Parameters().Bytes();bytes(parameters.data(),parameters.size());
            for(const auto& binding:item.material->Resources().textures){add(binding.slot);add(binding.texture);}
        }
        if(room&&lumen) {
            if(!giRoomScene)giRoomScene=BuildLumenGiScene(ExtractStudyTriangles(frame),.2f,32);
            else if(hash!=giRoomHash) {
                auto triangles=ExtractStudyTriangles(frame);
                giRoomScene=triangles.size()==giRoomScene->Geometry()->TriangleCount()?
                    RefitLumenGiScene(*giRoomScene,triangles):BuildLumenGiScene(triangles,.2f,32);
            }
            giRoomHash=hash;effects.lumenGi.scene=giRoomScene;
            cachedLumenScene=giRoomScene;cachedLumenHash=hash;return;
        }
        if(showcase&&lumen) {
            showcaseRequestedHash=hash;
            if(!showcaseGiScene) {
                showcaseGiScene=BuildLumenGiScene(ExtractStudyTriangles(frame,true),.5f);showcaseGiHash=hash;
            } else if(hash!=showcaseGiHash) {
                auto triangles=ExtractStudyTriangles(frame,true);
                showcaseGiScene=triangles.size()==showcaseGiScene->Geometry()->TriangleCount()?
                    RefitLumenGiScene(*showcaseGiScene,triangles):BuildLumenGiScene(triangles,.5f);
                showcaseGiHash=hash;
            }
            effects.lumenGi.scene=showcaseGiScene;cachedLumenScene=showcaseGiScene;cachedLumenHash=showcaseGiHash;return;
        }
        if(lumen) {
            if(!cachedLumenScene||hash!=cachedLumenHash){cachedLumenScene=BuildLumenGiScene(ExtractStudyTriangles(frame));cachedLumenHash=hash;}
            effects.lumenGi.scene=cachedLumenScene;
        } else {
            if(!cachedRealtimeScene||hash!=cachedRealtimeHash){cachedRealtimeScene=BuildRealtimeGiScene(ExtractStudyTriangles(frame,showcase));cachedRealtimeHash=hash;}
            effects.realtimeGi.scene=cachedRealtimeScene;
        }
    }
    void PrepareStudyProbes(const RenderFrame& frame,MaterialPipelineSettings& effects,bool waitForBake) {
        if(!effects.probes.enabled || effects.probes.volume) return;
        std::uint64_t hash=1469598103934665603ull;
        const auto bytes=[&](const void* p,std::size_t size) {
            const auto* b=static_cast<const unsigned char*>(p); for(std::size_t i=0;i<size;++i) { hash^=b[i]; hash*=1099511628211ull; }
        };
        const auto add=[&](const auto& value) { bytes(&value,sizeof(value)); };
        for(const auto& item:frame.items) {
            if(!item.material||item.EffectiveLayer()==RenderLayer::Transparent) continue;
            add(item.mesh); add(item.model); const auto& parameters=item.material->Parameters().Bytes(); bytes(parameters.data(),parameters.size());
            for(const auto& binding:item.material->Resources().textures) { add(binding.slot); add(binding.texture); }
        }
        add(effects.sky.enabled); add(effects.sky.intensity); add(effects.sky.diffuseStrength); add(effects.sky.rotation); add(effects.sky.environment.get());
        add(effects.shadows.sunDirection); add(effects.shadows.sunColor); add(effects.shadows.sunIntensity);
        add(effects.lighting.lightPositions); add(effects.lighting.lightColors);
        const auto area=MakeAreaLightsConstants(effects.areaLights); add(area);
        const auto spot=MakeSpotLightConstants(effects.spotLight);add(spot);
        if(probeBakeJob.valid()&&(waitForBake||probeBakeJob.wait_for(0ms)==std::future_status::ready)) {
            cachedProbes=probeBakeJob.get(); cachedProbeHash=pendingProbeHash;
        }
        if(!cachedProbes || (hash!=cachedProbeHash&&!probeBakeJob.valid())) {
            auto triangles=ExtractStudyTriangles(frame);
            ProbeBakeLighting lighting; lighting.sky=effects.sky; lighting.sunDirection=effects.shadows.sunDirection;
            lighting.sunColor=effects.shadows.sunColor; lighting.sunIntensity=effects.shadows.sunIntensity;
            lighting.pointPositions=effects.lighting.lightPositions; lighting.pointColors=effects.lighting.lightColors; lighting.areaLights=effects.areaLights;
            lighting.spotLight=effects.spotLight;
            const auto bake=[triangles=std::move(triangles),lighting] { return BakeDiffuseProbeVolume(ProbeBakeSettings{},triangles,lighting); };
            if(!cachedProbes||waitForBake) { cachedProbes=bake(); cachedProbeHash=hash; }
            else { pendingProbeHash=hash; probeBakeJob=std::async(std::launch::async,bake); }
        }
        effects.probes.volume=cachedProbes;
        // This volume covers the study room. Screen GI remains available when
        // explicitly selected, but is not traced redundantly for this preset.
        effects.indirect.enabled=false;
    }
    void Draw(const FrameSettings& settings, double* gpuMilliseconds = nullptr) {
        lastWidth = lastHeight = 0;
        lastPresented = true;
        Require(settings.width > 0 && settings.height > 0 && settings.width <= 8192 && settings.height <= 8192,
                "Invalid framebuffer dimensions");
        if(settings.showcase) {
            Require(settings.showcaseState.selected>=0&&settings.showcaseState.selected<int(ShowcaseMovableCount),"Invalid showcase selection");
            for(std::size_t i=0;i<ShowcaseMovableCount;++i) {
                const auto p=settings.showcaseState.offsets[i];
                Require(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::isfinite(settings.showcaseState.rotations[i]),"Invalid showcase transform");
            }
        }
        if(settings.giRoom) {
            std::string error;Require(ValidateGiRoomSettings(settings.giRoomState,&error),error);
            Require(!settings.showcase&&!settings.lightingStudy&&!settings.shadowStudy,"GI room is an independent scene");
            Require(settings.effects.lumenGi.lightingView!=LumenLightingView::Material,"GI room requires a Surface Cache lighting view");
        }
        Require(std::isfinite(settings.exposure) && settings.exposure > 0 && std::isfinite(settings.lightTime) &&
                std::isfinite(settings.cameraYaw) && std::isfinite(settings.cameraPitch) &&
                std::isfinite(settings.cameraDistance) && settings.cameraDistance >= 3.0f,
                "Invalid camera/exposure");
        const bool isolated = !settings.giRoom && !settings.showcase && !settings.shadowStudy && !settings.lightingStudy && settings.isolatedMaterial >= 0;
        Require(!isolated || static_cast<std::size_t>(settings.isolatedMaterial) < entries.size(), "Invalid material index");
        const glm::vec3 target = settings.lightingStudy ? glm::vec3(0,1,-.6f) : settings.shadowStudy ? glm::vec3(0, 0.5f, -0.8f) : isolated ? glm::vec3(0) : glm::vec3(0, 0.7f, 0);
        const float distance = settings.shadowStudy || settings.lightingStudy ? settings.cameraDistance * 0.65f : isolated ? std::max(2.4f, settings.cameraDistance / 4.5f) : settings.cameraDistance;
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
        if(settings.giRoom) {
            InitializeGiRoom();
            for(const auto& object:GiRoomObjects()) {
                const bool isPlane=object.mesh==ShowcaseMesh::Plane;
                frame.items.push_back(item(giRoomMaterials[std::size_t(object.material)],isPlane?plane:box,
                    isPlane?planeIndices:boxIndices,GiRoomTransform(object,settings.giRoomState)));
                frame.items.back().castsShadows=object.castsShadows;
            }
        } else if(settings.showcase) {
            InitializeShowcase();
            if(showcaseEmission!=settings.showcaseState.emission) {
                for(auto slot:{ShowcaseMaterial::WarmLamp,ShowcaseMaterial::CoolLamp,ShowcaseMaterial::EmissivePanel,ShowcaseMaterial::CeilingLamp}) {
                    std::string error;
                    Require(materials.SetParameter(showcaseMaterials[std::size_t(slot)],"emissiveIntensity",
                        settings.showcaseState.emission?1.f:0.f,&error),error);
                }
                showcaseEmission=settings.showcaseState.emission;
            }
            for(const auto& batch:showcaseBatches) {
                frame.items.push_back(item(showcaseMaterials[std::size_t(batch.material)],batch.mesh,batch.indices,glm::mat4(1)));
                frame.items.back().castsShadows=batch.castsShadows;
                const bool areaFixture=batch.material==ShowcaseMaterial::CeilingLamp;
                const bool pointFixture=batch.material==ShowcaseMaterial::WarmLamp||
                    (batch.material==ShowcaseMaterial::CoolLamp&&!settings.showcaseState.animateLights);
                frame.items.back().analyticEmission=(areaFixture&&settings.showcaseState.areaLights)||
                    (pointFixture&&settings.showcaseState.pointLights);
            }
            for(const auto& o:ShowcaseObjects())if(o.movable>=0||o.material==ShowcaseMaterial::Water) {
                const auto mesh=o.mesh==ShowcaseMesh::Sphere?sphere:o.mesh==ShowcaseMesh::Plane?plane:box;
                const auto indices=o.mesh==ShowcaseMesh::Sphere?sphereIndices:o.mesh==ShowcaseMesh::Plane?planeIndices:boxIndices;
                frame.items.push_back(item(showcaseMaterials[std::size_t(o.material)],mesh,indices,ShowcaseTransform(o,settings.showcaseState)));
                frame.items.back().castsShadows=o.castsShadows;
                frame.items.back().analyticEmission=o.material==ShowcaseMaterial::EmissivePanel&&settings.showcaseState.areaLights;
                if(o.material==ShowcaseMaterial::Water) {
                    frame.items.back().visibleInReflections=false;
                    frame.items.back().castsShadows=false;
                }
            }
        } else if (settings.lightingStudy) {
            const auto transform = [](glm::vec3 p, glm::vec3 scale, float angle=0, glm::vec3 axis=glm::vec3(1,0,0)) {
                return glm::translate(glm::mat4(1),p) * glm::rotate(glm::mat4(1),angle,axis) * glm::scale(glm::mat4(1),scale);
            };
            frame.items.push_back(item(bounceWhite,plane,planeIndices,transform({0,0,0},{3,1,3})));
            frame.items.push_back(item(bounceRed,plane,planeIndices,transform({-3,2,0},{2,1,3},glm::radians(-90.0f),{0,0,1})));
            frame.items.push_back(item(bounceWhite,plane,planeIndices,transform({0,2,-3},{3,1,2},glm::radians(90.0f))));
            frame.items.push_back(item(studyFloorMaterial,box,boxIndices,transform({-.4f,.55f,-.5f},{.55f,.55f,.55f})));
            frame.items.push_back(item(entries[16].handle,sphere,sphereIndices,transform({1.3f,.65f,-.4f},{.65f,.65f,.65f})));
            frame.items.push_back(item(entries[20].handle,plane,planeIndices,transform({1.9f,.65f,-1.6f},{.75f,1,.5f},glm::radians(90.0f))));
            frame.items.push_back(item(entries[18].handle,sphere,sphereIndices,transform({-1.25f,1.1f,1.2f},{.8f,.8f,.8f})));
            frame.items.push_back(item(entries[19].handle,plane,planeIndices,transform({1.3f,1.1f,1.4f},{.85f,1,.95f},glm::radians(90.0f))));
            if(settings.closedLightingStudy) {
                frame.items.push_back(item(bounceWhite,plane,planeIndices,transform({3,2,0},{2,1,3},glm::radians(90.0f),{0,0,1})));
                frame.items.push_back(item(bounceWhite,plane,planeIndices,transform({0,2,3},{3,1,2},glm::radians(-90.0f))));
                frame.items.push_back(item(bounceWhite,plane,planeIndices,transform({0,4,0},{3,1,3},glm::radians(180.0f),{0,0,1})));
            }
        } else if (settings.shadowStudy) {
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
            for (std::size_t i=0; i<18; ++i) {
                const auto& entry = entries[i];
                frame.items.push_back(item(entry.handle, sphere, sphereIndices, entry.model));
            }
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
        if(settings.showcase)ConfigureShowcaseLighting(effects,settings.showcaseState,settings.lightTime);
        if(settings.giRoom)ConfigureGiRoomLighting(effects,settings.giRoomState);
        if (isolated) effects.reflection.enabled = false;
        if (settings.shadowStudy) {
            effects.reflection.enabled = false;
            effects.lighting.ambientAndExposure = glm::vec4(0.10f,0.10f,0.10f,1);
            for (auto& color : effects.lighting.lightColors) color = glm::vec4(0);
        }
        if (settings.lightingStudy) {
            effects.shadows.sunIntensity = 0;
            effects.lighting.ambientAndExposure = glm::vec4(.006f,.006f,.006f,1);
            for (auto& color : effects.lighting.lightColors) color = glm::vec4(0);
        }
        if (effects.sky.enabled||effects.probes.enabled||effects.realtimeGi.enabled||effects.lumenGi.enabled) effects.lighting.ambientAndExposure = glm::vec4(0,0,0,1);
        if(settings.lightingStudy||settings.showcase||settings.giRoom) {
            if(effects.realtimeGi.enabled||effects.lumenGi.enabled) PrepareRealtimeStudy(frame,effects,settings.showcase,settings.giRoom);
            else PrepareStudyProbes(frame,effects,settings.waitForProbeBake);
        }
        PipelineCamera camera;
        camera.position = eye;
        camera.view = glm::lookAt(eye, target, glm::vec3(0, 1, 0));
        camera.projection = glm::perspective(glm::radians(42.0f), settings.OutputAspect(), 0.1f, 100.0f);
        if(settings.camera)camera=*settings.camera;
        else if(settings.giRoom)camera=GiRoomCameraPreset(0,settings.OutputAspect());
        else if(settings.showcase)camera=ShowcaseCameraPreset(0,settings.OutputAspect());
        if(settings.giRoom) {
            const auto forward=glm::normalize(-glm::vec3(glm::inverse(camera.view)[2]));
            effects.spotLight.enabled=settings.giRoomState.flashlight;
            effects.spotLight.position=ClampGiRoomCamera(camera.position+forward*.08f,std::max(.12f,effects.spotLight.sourceRadius+.02f));
            effects.spotLight.direction=forward;
        }
        if(settings.showcase) {
            // Water is the showcase's planar-reflection receiver. Its bounds
            // let the pipeline skip the entire mirror scene when offscreen.
            effects.reflection.receiverBounds.reset();
            for(const auto& object:ShowcaseObjects())if(object.material==ShowcaseMaterial::Water) {
                const auto model=ShowcaseTransform(object,settings.showcaseState);
                for(int x:{-1,1})for(int y:{-1,1})for(int z:{-1,1}) {
                    const auto point=glm::vec3(model*glm::vec4(float(x),float(y)*.005f,float(z),1));
                    if(!effects.reflection.receiverBounds)
                        effects.reflection.receiverBounds=ReflectionReceiverBounds{point,point};
                    else {
                        auto& bounds=*effects.reflection.receiverBounds;
                        bounds.minimum=glm::min(bounds.minimum,point);bounds.maximum=glm::max(bounds.maximum,point);
                    }
                }
            }
        }
        if(settings.showcase&&settings.showcaseHud)AddShowcaseHud(frame,settings);
        if(settings.giRoom&&settings.giRoomHud)AddGiRoomHud(frame,settings);
        Require(pipeline->Resize(settings.width, settings.height, effects.shadows.atlasResolution,
            effects.temporal.msaaSamples, effects.reflection.resolutionScale), pipeline->LastError());
        RHICommand::BeginFrame begin;
        begin.frameIndex = frameIndex;
        const auto output=settings.OutputExtent();
        begin.framebufferWidth = output.x; begin.framebufferHeight = output.y;
        begin.clearColor = glm::vec4(0.027f, 0.035f, 0.052f, 1);
        auto encoder = device.BeginFrame(begin);
        if((settings.showcase&&settings.showcaseHud)||(settings.giRoom&&settings.giRoomHud))
            Require(encoder.KeepAlive(showcaseHudOwner),"Cannot retain lab HUD");
        if (!encoder.KeepAlive(owner) || !pipeline->Record(encoder, frame, camera, effects) || !encoder.End(settings.present)) {
            encoder.Cancel();
            if (pipeline->RecordedFrameToken()) pipeline->DiscardFrame(pipeline->RecordedFrameToken());
            throw std::runtime_error("PBR frame recording failed: " + pipeline->LastError());
        }
        struct FrameCompletion { bool frameDone=false,timingDone=false; double gpuMilliseconds=0; };
        auto completion = std::make_shared<FrameCompletion>();
        completion->timingDone=gpuMilliseconds==nullptr;
        if (!device.async_SubmitFrameCommands(encoder.GetCommandBuffer(), [completion] { completion->frameDone = true; })) {
            encoder.Cancel();
            pipeline->DiscardFrame(pipeline->RecordedFrameToken());
            throw std::runtime_error("PBR frame submission failed");
        }
        if(gpuMilliseconds) {
            // The reliable frame queue runs before diagnostic work. Enqueue
            // the timestamp read before waiting, so Measure does not add a
            // second application/render-thread round trip to every frame.
            // Backend diagnostics remain confined to the render thread.
            device.async_ExecuteCode([this,completion] {
                completion->gpuMilliseconds=device.BackendDiagnostics()->LastGpuMilliseconds();
            },[completion] { completion->timingDone=true; });
        }
        // Drivers may compile pipeline variants lazily on the first draw.
        // Keep ordinary frame waits bounded without treating cold startup as
        // a failed render; this does not affect measured/warm frame timings.
        Wait([completion] { return completion->frameDone&&completion->timingDone; }, "PBR frame completion",frameIndex==1?30s:10s);
        Require(pipeline->CompleteFrame(pipeline->RecordedFrameToken()), pipeline->LastError());
        if(gpuMilliseconds)*gpuMilliseconds=completion->gpuMilliseconds;
        lastWidth = output.x; lastHeight = output.y; lastPresented = settings.present;
    }
    FramePixels ReadPixels() {
        Require(lastWidth != 0 && !lastPresented, "Readback requires a completed Render(present=false)");
        struct Result { FramePixels pixels; GLenum error = GL_NO_ERROR; };
        auto result = std::make_shared<Completion<Result>>();
        result->value.pixels = {lastWidth, lastHeight, std::vector<std::uint8_t>(static_cast<std::size_t>(lastWidth) * lastHeight * 4)};
        device.async_ExecuteCode([this,result] {
            auto& pixels = result->value.pixels;
            if(device.GetBackendType()==BackendType::Vulkan){pixels.rgba=device.BackendDiagnostics()->ReadWindow();if(device.BackendDiagnostics()->ValidationErrorCount())result->value.error=GL_INVALID_OPERATION;return;}
            glReadPixels(0, 0, pixels.width, pixels.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.rgba.data());
            result->value.error = glGetError();
        }, [result] { result->done = true; });
        Wait([result] { return result->done; }, "PBR frame readback");
        Require(result->value.error == GL_NO_ERROR&&result->value.pixels.rgba.size()==std::size_t(lastWidth)*lastHeight*4,
                "Backend error or incorrect output extent during PBR rendering/readback");
        return std::move(result->value.pixels);
    }
    std::vector<glm::vec4> ReadRealtimeCache() {
        const auto handle=pipeline->DebugTextures().realtimeGiCache;
        Require(handle.IsValid(),"Draw realtime GI before reading its cache");
        struct Result {std::vector<glm::vec4> pixels;GLenum error=GL_NO_ERROR;};
        auto result=std::make_shared<Completion<Result>>();result->value.pixels.resize(MaxDiffuseProbes*RealtimeGiCacheRows);
        device.async_ExecuteCode([this,result,handle]{
            const auto* texture=device.GetResourcePool().TextureTable.Get(handle);
            if(!texture){result->value.error=GL_INVALID_OPERATION;return;}
            if(device.GetBackendType()==BackendType::Vulkan){auto data=device.BackendDiagnostics()->ReadTexture(handle);if(data.size()!=result->value.pixels.size()*4){result->value.error=GL_INVALID_OPERATION;return;}std::memcpy(result->value.pixels.data(),data.data(),data.size()*4);return;}
            glGetTextureImage(texture->rhi_id,0,GL_RGBA,GL_FLOAT,GLsizei(result->value.pixels.size()*sizeof(glm::vec4)),result->value.pixels.data());
            result->value.error=glGetError();
        },[result]{result->done=true;});
        Wait([result]{return result->done;},"realtime GI cache readback");
        Require(result->value.error==GL_NO_ERROR,"GL error reading realtime GI cache");
        return std::move(result->value.pixels);
    }
    std::vector<glm::vec4> ReadFloatTexture(RenderResourceHandle<RHITextureSpec> handle) {
        Require(handle.IsValid(),"Draw hybrid GI before reading its lighting texture");
        struct Result {std::vector<glm::vec4> pixels;GLenum error=GL_NO_ERROR;};
        auto result=std::make_shared<Completion<Result>>();
        device.async_ExecuteCode([this,result,handle]{
            const auto* t=device.GetResourcePool().TextureTable.Get(handle);if(!t){result->value.error=GL_INVALID_OPERATION;return;}
            auto& pixels=result->value.pixels;pixels.resize(std::size_t(t->width)*t->height);
            if(device.GetBackendType()==BackendType::Vulkan){auto data=device.BackendDiagnostics()->ReadTexture(handle);if(data.size()!=pixels.size()*4){result->value.error=GL_INVALID_OPERATION;return;}std::memcpy(pixels.data(),data.data(),data.size()*4);return;}
            glGetTextureImage(t->rhi_id,0,GL_RGBA,GL_FLOAT,GLsizei(pixels.size()*sizeof(glm::vec4)),pixels.data());result->value.error=glGetError();
        },[result]{result->done=true;});
        Wait([result]{return result->done;},"Hybrid lighting texture readback");Require(result->value.error==GL_NO_ERROR,"Error reading hybrid lighting texture");return std::move(result->value.pixels);
    }
    std::vector<glm::vec4> ReadLumenCache(bool direct=false) {
        const auto debug=pipeline->DebugTextures();return ReadFloatTexture(direct?debug.lumenDirectLighting:debug.lumenSurfaceCache);
    }
    std::vector<glm::vec4> ReadLumenGather() {
        return ReadFloatTexture(pipeline->DebugTextures().indirectIrradiance);
    }
    FrameTiming Measure(const FrameSettings& settings, std::uint32_t count,float timeStep) {
        Require(count > 0 && count <= 120, "Timing requires 1..120 frames");
        Require(std::isfinite(timeStep)&&timeStep>=0&&timeStep<=1,"Invalid animation step for timing");
        auto frame=settings;
        if(device.GetBackendType()==BackendType::Vulkan) {
            const auto begin=std::chrono::steady_clock::now();double sum=0;
            for(uint32_t i=0;i<count;++i) {
                frame.lightTime=settings.lightTime+i*timeStep;double gpuMilliseconds=0;
                Draw(frame,&gpuMilliseconds);
                Require(std::isfinite(gpuMilliseconds)&&gpuMilliseconds>0,"Invalid Vulkan frame timestamp");
                sum+=gpuMilliseconds;
            }
            return {sum/count,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/count};
        }
        auto started = std::make_shared<Completion<GLuint>>();
        device.async_ExecuteCode([started] {
            glFinish(); // Exclude queued warmup work from the measured batch.
            glGenQueries(1, &started->value);
            glBeginQuery(GL_TIME_ELAPSED, started->value);
        }, [started] { started->done = true; });
        Wait([started] { return started->done; }, "GPU timing start");
        const auto begin = std::chrono::steady_clock::now();
        try {
            for (std::uint32_t i = 0; i < count; ++i){frame.lightTime=settings.lightTime+i*timeStep;Draw(frame);}
        } catch (...) {
            device.async_ExecuteCode([started] { glEndQuery(GL_TIME_ELAPSED); glDeleteQueries(1, &started->value); });
            throw;
        }
        auto finished = std::make_shared<Completion<GLuint64>>();
        device.async_ExecuteCode([started, finished] {
            glEndQuery(GL_TIME_ELAPSED);
            glGetQueryObjectui64v(started->value, GL_QUERY_RESULT, &finished->value);
            glDeleteQueries(1, &started->value);
        }, [finished] { finished->done = true; });
        Wait([finished] { return finished->done; }, "GPU timing result");
        return {double(finished->value) / 1000000.0 / count,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count() / count};
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
                if(device.GetBackendType()==BackendType::Vulkan)data=device.BackendDiagnostics()->ReadTexture(handle);
                else glGetTextureImage(texture->rhi_id, 0, depth ? GL_DEPTH_COMPONENT : GL_RGBA, GL_FLOAT,
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
            if(device.GetBackendType()==BackendType::Opengl)*error = glGetError();
        }, [result] { result->done = true; });
        Wait([result] { return result->done; }, "pipeline attachment inspection");
        Require(*error == GL_NO_ERROR, "GL error inspecting HDR/depth/AO attachments");
        return result->value;
    }
};

MaterialLabModule::MaterialLabModule(OpenglBackendContext context, std::string legacyTextureDirectory)
    : context_(std::move(context)), legacyTextureDirectory_(std::move(legacyTextureDirectory)) {}
MaterialLabModule::MaterialLabModule(VulkanBackendContext context, std::string legacyTextureDirectory)
    : vulkanContext_(context),legacyTextureDirectory_(std::move(legacyTextureDirectory)) {}
MaterialLabModule::~MaterialLabModule() { Shutdown(); }
bool MaterialLabModule::Startup() {
    if (impl_) return true;
    error_.clear();
    try {
        if(vulkanContext_)impl_=std::make_unique<Impl>(*vulkanContext_);
        else impl_ = std::make_unique<Impl>(context_);
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
std::shared_ptr<const Render::DiffuseProbeVolume> MaterialLabModule::PreparedDiffuseProbes() const {
    return impl_?impl_->cachedProbes:nullptr;
}
std::shared_ptr<const Render::RealtimeGiScene> MaterialLabModule::PreparedRealtimeGiScene() const {
    return impl_?impl_->cachedRealtimeScene:nullptr;
}
std::vector<glm::vec4> MaterialLabModule::ReadbackRealtimeGiCache() {
    error_.clear();if(!impl_){error_="MaterialLab is not started";return {};}
    try{return impl_->ReadRealtimeCache();}catch(const std::exception& e){error_=e.what();return {};}
}
std::shared_ptr<const Render::LumenGiScene> MaterialLabModule::PreparedLumenGiScene() const {
    return impl_?impl_->cachedLumenScene:nullptr;
}
bool MaterialLabModule::ShowcaseGiUpdating() const {
    return impl_&&impl_->showcaseRequestedHash!=impl_->showcaseGiHash;
}
std::vector<glm::vec4> MaterialLabModule::ReadbackLumenSurfaceCache(bool directLighting) {
    error_.clear();if(!impl_){error_="MaterialLab is not started";return {};}
    try{return impl_->ReadLumenCache(directLighting);}catch(const std::exception& e){error_=e.what();return {};}
}
std::vector<glm::vec4> MaterialLabModule::ReadbackLumenGather() {
    error_.clear();if(!impl_){error_="MaterialLab is not started";return {};}
    try{return impl_->ReadLumenGather();}catch(const std::exception& e){error_=e.what();return {};}
}
glm::vec3 MaterialLabModule::InspectPipelineBuffers() {
    error_.clear();
    if (!impl_) { error_ = "MaterialLab is not started"; return glm::vec3(-1); }
    try { return impl_->InspectBuffers(); }
    catch (const std::exception& error) { error_ = error.what(); return glm::vec3(-1); }
}

FrameTiming MaterialLabModule::MeasureFrames(const FrameSettings& settings, std::uint32_t count,float timeStep) {
    error_.clear();
    if (!impl_) { error_ = "MaterialLab is not started"; return {}; }
    try { return impl_->Measure(settings, count,timeStep); }
    catch (const std::exception& error) { error_ = error.what(); return {}; }
}

} // namespace MaterialLab
