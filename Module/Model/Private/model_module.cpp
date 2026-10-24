#include "Model/Public/model_module.h"

#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>
#include "Render/Public/Material/pbr_material.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"

namespace Model {
namespace {
using namespace Render;
using namespace Render::Material;

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
template<class T> bool FiniteMatrix(const T& matrix) {
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
        if (!std::isfinite(matrix[c][r])) return false;
    return true;
}
template<class T> bool FiniteVector(const T& vector) {
    for (int i = 0; i < vector.length(); ++i) if (!std::isfinite(vector[i])) return false;
    return true;
}
// Explicit packed layout; independent of GLM aligned-gentype build options.
struct GpuVertex { float position[3], normal[3], uv[2], tangent[4]; };
static_assert(sizeof(GpuVertex) == 48);
std::vector<float> PackVertices(const StaticMesh& mesh,const glm::dvec3& origin,bool secondUV) {
    std::vector<float> packed;packed.reserve(mesh.vertices.size()*(secondUV?14:12));
    for(const auto& v:mesh.vertices){
        const glm::vec3 p(v.position-origin);
        Require(FiniteVector(p),"Mesh exceeds GPU float range");
        packed.insert(packed.end(),{p.x,p.y,p.z,v.normal.x,v.normal.y,v.normal.z,v.uv.x,v.uv.y,v.tangent.x,v.tangent.y,v.tangent.z,v.tangent.w});
        if(secondUV)packed.insert(packed.end(),{v.uv1.x,v.uv1.y});
    }return packed;
}

struct Pipelines {
    std::shared_ptr<const MaterialTemplate> schema;
    RenderResourceHandle<PipelineSpec> surface, shadow, diffuse, normal;
};
Pipelines BuildPipelines(RHIDevice& device, PipelineDetail::ResourceBuilder& builder, Domain domain,bool secondUV) {
    Pipelines result;
    result.schema = MakePbrTemplate(domain);
    const auto make = [&](bool shadow, PbrOutput output) {
        CreateGraphicShaderDesc desc;
        desc.vertexShaderSource = builder.Source(ShaderSourceType::Vertex,
            shadow ? PbrShadowVertexShader(domain) : PbrVertexShader(domain,secondUV));
        desc.fragmentShaderSource = builder.Source(ShaderSourceType::Fragment,
            shadow ? PbrShadowFragmentShader(domain) : PbrFragmentShader(domain, output));
        const auto program = builder.Create<ShaderProgramSpec>([&](auto cb) {
            device.async_CreateGraphicShader(desc, cb);
        });
        auto spec = shadow ? PbrShadowPipelineSpec(program, domain) : PbrPipelineSpec(program, domain);
        spec.expectVertexLayout=PbrVertexLayout(secondUV);
        if (!shadow && output != PbrOutput::Surface) spec.depthWrite = false;
        return builder.Create<PipelineSpec>([&](auto cb) { device.async_CreatePipeline({spec}, cb); });
    };
    result.surface = make(false, PbrOutput::Surface);
    if (domain == Domain::Surface) {
        result.shadow = make(true, PbrOutput::Surface);
        result.diffuse = make(false, PbrOutput::DiffuseRadiance);
        result.normal = make(false, PbrOutput::WorldNormal);
    }
    return result;
}

void Validate(const StaticModel& model) {
    constexpr auto max32 = std::numeric_limits<std::uint32_t>::max();
    Require(!model.meshes.empty() && !model.instances.empty() && !model.materials.empty(),
        "Model requires meshes, scene instances and materials");
    for (const auto& mesh : model.meshes) {
        Require(!mesh.vertices.empty() && !mesh.indices.empty() && mesh.indices.size() % 3 == 0,
            "Static mesh requires indexed triangles: " + mesh.name);
        Require(mesh.vertices.size() <= max32 / 56 && mesh.indices.size() <= max32 / sizeof(std::uint32_t),
            "Mesh exceeds the RHI buffer size limit: " + mesh.name);
        Require(mesh.materialIndex < model.materials.size(), "Mesh references an invalid material");
        for (auto index : mesh.indices) Require(index < mesh.vertices.size(), "Mesh index is out of range");
        for (const auto& vertex : mesh.vertices)
            Require(FiniteVector(vertex.position) && FiniteVector(vertex.normal) &&
                FiniteVector(vertex.uv) && FiniteVector(vertex.uv1) && FiniteVector(vertex.tangent), "Mesh contains non-finite vertex data");
    }
    for (const auto& instance : model.instances)
        Require(instance.meshIndex < model.meshes.size() && FiniteMatrix(instance.transform),
            "Invalid mesh instance or node transform");
    for (const auto& texture : model.textures) {
        const auto count = std::uint64_t(texture.width) * texture.height * 4;
        Require(texture.width && texture.height && count <= std::numeric_limits<std::size_t>::max(),
            "Invalid texture dimensions");
        Require((texture.rgba.size() == count && texture.rgba32f.empty()) ||
                (texture.rgba32f.size() == count && texture.rgba.empty()), "Invalid RGBA texture data");
        for (float value : texture.rgba32f) Require(std::isfinite(value), "Non-finite texture data");
    }
    for (const auto& material : model.materials)
        for (auto index : material.textures)
            Require(index == -1 || (index >= 0 && std::size_t(index) < model.textures.size()),
                "Material references an invalid texture");
}
} // namespace

struct ModelResource::Impl {
    RHIDevice* device = nullptr;
    std::shared_ptr<const StaticModel> source;
    std::shared_ptr<const void> owner;
    MaterialService materials;
    std::vector<MaterialHandle> materialHandles;
    std::vector<RenderResourceHandle<VertexBufferSpec>> meshes;
    std::vector<glm::dvec3> meshOrigins;
    std::vector<std::vector<float>> packedGeometry;
    std::uint64_t geometryRevision=0;
    bool secondUV=false;
};

ModelResource::ModelResource(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
ModelResource::~ModelResource() = default;
const StaticModel& ModelResource::Source() const { return *impl_->source; }
std::uint64_t ModelResource::GeometryRevision() const noexcept { return impl_->geometryRevision; }
MaterialService& ModelResource::Materials() { return impl_->materials; }
const std::vector<MaterialHandle>& ModelResource::MaterialHandles() const { return impl_->materialHandles; }
bool ModelResource::KeepAlive(RHIFrameEncoder& encoder) const { return encoder.KeepAlive(impl_->owner); }

bool ModelResource::UpdateGeometry(const StaticModel& deformed, std::string* error) {
    bool writesQueued=false;
    try {
        Require(impl_->device && impl_->device->IsRunning(), "Model deformation requires a running render device");
        Require(deformed.meshes.size() == impl_->meshes.size(), "Deformed mesh count differs from the uploaded mesh");
        // Validate the whole update before enqueueing any mutation.
        for (std::size_t i = 0; i < deformed.meshes.size(); ++i) {
            const auto& mesh = deformed.meshes[i];
            Require(mesh.vertices.size() == impl_->source->meshes[i].vertices.size() &&
                mesh.indices == impl_->source->meshes[i].indices, "Deformation must preserve vertex/index topology");
            for (const auto& vertex : mesh.vertices)
                Require(FiniteVector(vertex.position) && FiniteVector(vertex.normal) && FiniteVector(vertex.tangent) &&
                    FiniteVector(vertex.uv) && FiniteVector(vertex.uv1) && FiniteVector(glm::vec3(vertex.position - impl_->meshOrigins[i])),
                    "Deformation produced non-finite or out-of-range vertex data");
        }
        struct Completion { std::size_t pending = 0; bool succeeded = true; };
        auto completion = std::make_shared<Completion>();
        std::vector<std::vector<float>> pendingPacked;
        pendingPacked.reserve(deformed.meshes.size());
        bool packedChanged=impl_->packedGeometry.size()!=deformed.meshes.size();
        for (std::size_t i = 0; i < deformed.meshes.size(); ++i) {
            const auto& mesh = deformed.meshes[i];
            const auto& packed=pendingPacked.emplace_back(PackVertices(mesh,impl_->meshOrigins[i],impl_->secondUV));
            if(!packedChanged){
                const auto& previous=impl_->packedGeometry[i];
                packedChanged=previous.size()!=packed.size()||
                    std::memcmp(previous.data(),packed.data(),packed.size()*sizeof(float))!=0;
            }
            UpdateMeshBufferDesc desc;
            desc.vertexCount = std::uint32_t(mesh.vertices.size()); desc.vertexData = packed.data();
            desc.vertexByteSize = packed.size() * sizeof(float);
            desc.indexCount = std::uint32_t(mesh.indices.size()); desc.indexData = mesh.indices.data();
            desc.indexByteSize = mesh.indices.size() * sizeof(std::uint32_t);
            ++completion->pending;
            writesQueued=true;
            impl_->device->async_UpdateMeshBuffer(impl_->meshes[i], desc, [completion](bool ok) {
                completion->succeeded = completion->succeeded && ok;
                --completion->pending;
            });
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (completion->pending) {
            impl_->device->returnSystem.DrainCallbacks();
            if (!completion->pending) break;
            Require(std::chrono::steady_clock::now() < deadline, "Timed out updating skeletal preview geometry");
            impl_->device->returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));
        }
        Require(completion->succeeded, "Render backend rejected a skeletal mesh update");
        impl_->packedGeometry=std::move(pendingPacked);
        if(packedChanged)++impl_->geometryRevision;
        if (error) error->clear();
        return true;
    } catch (const std::exception& failure) {
        if(writesQueued){
            // Separate RHI mesh writes cannot be rolled back atomically. A
            // rejected/timed-out batch may have committed a subset; invalidate
            // conservatively and rebuild known contents on the next success.
            ++impl_->geometryRevision;impl_->packedGeometry.clear();
        }
        if (error) {
            *error = failure.what();
            if(writesQueued)*error+="; mesh contents may be partially updated and their geometry revision was invalidated";
        }
        return false;
    }
}

bool ModelResource::AppendTo(RenderFrame& frame, const glm::dmat4& placement,
    const glm::dvec3& renderOrigin, const glm::mat4& view, std::string* error) const {
    try {
        Require(FiniteMatrix(placement) && FiniteVector(renderOrigin) && FiniteMatrix(view),
            "Model placement, render origin and view must be finite");
        std::vector<RenderItem> items;
        items.reserve(impl_->source->instances.size());
        for (const auto& instance : impl_->source->instances) {
            const auto index = instance.meshIndex;
            const auto& mesh = impl_->source->meshes[index];
            auto transform = placement * instance.transform * glm::translate(glm::dmat4(1), impl_->meshOrigins[index]);
            transform[3] -= glm::dvec4(renderOrigin, 0);
            Require(FiniteMatrix(transform), "Model placement overflow");
            RenderItem item;
            item.model = glm::mat4(transform);
            Require(FiniteMatrix(item.model), "Model exceeds GPU float range after rebasing");
            item.mesh = impl_->meshes[index];
            item.material = impl_->materials.Capture(impl_->materialHandles[mesh.materialIndex], {}, error);
            Require(item.material != nullptr, error ? *error : "Model material was removed");
            const auto alpha = impl_->source->materials[mesh.materialIndex].alphaMode;
            item.layer = alpha == AlphaMode::Blend ? RenderLayer::Transparent :
                alpha == AlphaMode::Mask ? RenderLayer::Cutout : RenderLayer::Opaque;
            item.castsShadows = alpha != AlphaMode::Blend;
            item.draw.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
            item.viewDepth = -(view * item.model * glm::vec4(0, 0, 0, 1)).z;
            item.sortKey = mesh.materialIndex;
            items.push_back(std::move(item));
        }
        frame.items.insert(frame.items.end(), std::make_move_iterator(items.begin()), std::make_move_iterator(items.end()));
        if (error) error->clear();
        return true;
    } catch (const std::exception& failure) {
        if (error) *error = failure.what();
        return false;
    }
}

ModelModule::ModelModule(RHIDevice& device) : device_(device) {}
ModelModule::~ModelModule() { Shutdown(); }
bool ModelModule::Startup() {
    error_.clear();
    if (!device_.IsRunning()) { error_ = "Start Render::RHIDevice before the Model module"; return false; }
    started_ = true;
    return true;
}
void ModelModule::Shutdown() { started_ = false; }
bool ModelModule::IsStarted() const noexcept { return started_ && device_.IsRunning(); }
const std::string& ModelModule::LastError() const noexcept { return error_; }
std::shared_ptr<ModelResource> ModelModule::Load(const std::filesystem::path& path, const LoadOptions& options) {
    if (!IsStarted()) { error_ = "Start the Model module before loading GPU models"; return {}; }
    auto source = StaticModelLoader::Load(path, options, &error_);
    return source ? Upload(std::move(source)) : nullptr;
}
std::shared_ptr<ModelResource> ModelModule::Upload(std::shared_ptr<const StaticModel> model,const ModelUploadOptions& options) {
    error_.clear();
    try {
        Require(IsStarted(), "Start the Model module before uploading models");
        Require(model != nullptr, "Cannot upload a null model");
        Validate(*model);
        PipelineDetail::ResourceBuilder builder(device_);
        auto impl = std::make_unique<ModelResource::Impl>();
        impl->device = &device_;
        impl->secondUV=options.includeSecondUV;
        impl->source = std::move(model);
        const auto surface = BuildPipelines(device_, builder, Domain::Surface,options.includeSecondUV);
        Pipelines translucent;
        for (const auto& material : impl->source->materials)
            if (material.alphaMode == AlphaMode::Blend && !translucent.schema)
                translucent = BuildPipelines(device_, builder, Domain::Translucent,options.includeSecondUV);
        const auto uploadTexture = [&](const TextureImage& image) {
            CreateRHITextureSpec desc;
            desc.width = image.width; desc.height = image.height;
            desc.filterMode = RHIFilterMode::Linear; desc.mipmapMode = RHIMipmapMode::Linear;
            desc.textureDataStoreType = desc.textureUseType =
                image.rgba32f.empty() ? RHITextureFormat::RGBA8 : RHITextureFormat::RGBA32F;
            desc.data = image.rgba32f.empty() ? static_cast<const void*>(image.rgba.data()) : image.rgba32f.data();
            return builder.Create<RHITextureSpec>([&](auto cb) { device_.async_CreateTexture(desc, cb); });
        };
        TextureImage neutral;
        neutral.width = neutral.height = 1; neutral.rgba = {255, 255, 255, 255};
        const auto white = uploadTexture(neutral);
        neutral.rgba = {128, 128, 255, 255};
        const auto flatNormal = uploadTexture(neutral);
        std::vector<RenderResourceHandle<RHITextureSpec>> textures;
        for (const auto& image : impl->source->textures) textures.push_back(uploadTexture(image));
        for (const auto& mesh : impl->source->meshes) {
            Bounds bounds;
            for (const auto& vertex : mesh.vertices) bounds.Include(vertex.position);
            const auto origin = bounds.Center();
            impl->meshOrigins.push_back(origin);
            auto packed=PackVertices(mesh,origin,options.includeSecondUV);
            CreateMeshBufferDesc desc;
            desc.vertexLayout = PbrVertexLayout(options.includeSecondUV);
            desc.vertexCount = static_cast<std::uint32_t>(mesh.vertices.size());
            desc.vertexData = packed.data(); desc.vertexByteSize = packed.size() * sizeof(float);
            desc.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
            desc.indexData = mesh.indices.data(); desc.indexByteSize = mesh.indices.size() * sizeof(std::uint32_t);
            desc.indexType = IndexType::UInt32;
            impl->meshes.push_back(builder.Create<VertexBufferSpec>([&](auto cb) { device_.async_CreateMeshBuffer(desc, cb); }));
            impl->packedGeometry.push_back(std::move(packed));
        }
        std::vector<MaterialPassResources> passes;
        std::vector<std::shared_ptr<const MaterialAsset>> assets;
        for (const auto& material : impl->source->materials) {
            const auto& variants = material.alphaMode == AlphaMode::Blend ? translucent : surface;
            MaterialPassResources pass;
            pass.expectedTemplate = variants.schema;
            pass.pipeline = variants.surface; pass.shadowPipeline = variants.shadow;
            pass.diffuseRadiancePipeline = variants.diffuse; pass.normalPipeline = variants.normal;
            pass.parameterBufferBytes = static_cast<std::uint32_t>(variants.schema->ByteSize());
            pass.parameterBuffer = builder.Uniform(pass.parameterBufferBytes);
            for (std::uint32_t slot = 0; slot < material.textures.size(); ++slot) {
                const auto index = material.textures[slot];
                pass.textures.push_back({slot, index >= 0 ? textures[index] : slot == 1 ? flatNormal : white});
            }
            std::string error;
            auto asset = MaterialAsset::Create(variants.schema, {
                {"baseColor", material.baseColor}, {"metallic", material.metallic},
                {"roughness", material.roughness}, {"ao", material.ao}, {"normalScale", material.normalScale},
                {"uvTransform", material.uvTransform}, {"emissiveColor", glm::vec4(material.emissive, 0)},
                {"useNormalMap", material.textures[1] >= 0}, {"useMetallicMap", material.textures[2] >= 0},
                {"useRoughnessMap", material.textures[3] >= 0}, {"useAoMap", material.textures[4] >= 0},
                {"baseColorIsSRGB", material.textures[0] < 0 ||
                    !impl->source->textures[material.textures[0]].linearColor},
                {"useEmissiveMap", material.textures[7] >= 0}, {"twoSided", material.twoSided},
                {"alphaCutoff", material.alphaMode == AlphaMode::Mask ? material.alphaCutoff : 0.0f},
                {"transmission", 0.0f}}, &error);
            Require(asset != nullptr, "Invalid imported PBR material: " + error);
            assets.push_back(std::move(asset)); passes.push_back(std::move(pass));
        }
        impl->owner = builder.Finish();
        for (std::size_t i = 0; i < passes.size(); ++i) {
            passes[i].lifetime = impl->owner; // Includes meshes; material snapshots pin the complete model.
            std::string error;
            auto handle = impl->materials.Register(assets[i], passes[i], &error);
            Require(handle.IsValid(), "Cannot register imported PBR material: " + error);
            impl->materialHandles.push_back(handle);
        }
        impl->geometryRevision=1;
        return std::shared_ptr<ModelResource>(new ModelResource(std::move(impl)));
    } catch (const std::exception& failure) {
        error_ = failure.what();
        return {};
    }
}
} // namespace Model
