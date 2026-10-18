#include "Model/Public/skeletal_asset.h"
#include "Model/Private/blend_importer.h"
#include "Model/Private/blend_metadata.h"
#include "Model/Private/garment_recognition.h"
#include "Model/Private/max_metadata.h"

#include <assimp/Importer.hpp>
#include <assimp/IOStream.hpp>
#include <assimp/IOSystem.hpp>
#include <assimp/cimport.h>
#include <assimp/config.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <sstream>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace Model {
namespace {

std::string Utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {value.begin(), value.end()};
}

std::filesystem::path NativePath(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if(!stream) throw std::runtime_error("Cannot open skeletal source: " + Utf8(path));
    const auto length = stream.tellg();
    if(length < 0 || static_cast<std::uintmax_t>(length) > std::numeric_limits<std::size_t>::max() ||
       static_cast<std::uintmax_t>(length) > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
        throw std::runtime_error("Skeletal source is too large");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    stream.seekg(0);
    if(!bytes.empty() && !stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("Cannot read skeletal source: " + Utf8(path));
    return bytes;
}

std::string ContentId(const std::vector<std::uint8_t>& bytes) {
    std::uint64_t hash = 14695981039346656037ull;
    for(const auto byte : bytes) { hash ^= byte; hash *= 1099511628211ull; }
    std::ostringstream text;
    text << "model-" << std::hex << std::setfill('0') << std::setw(16) << hash;
    return text.str();
}

bool Finite(const glm::dvec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

glm::vec3 SafeNormal(glm::vec3 value, glm::vec3 fallback = {0,1,0}) {
    const float length = glm::dot(value, value);
    return std::isfinite(length) && length > 1e-20f ? value / std::sqrt(length) : fallback;
}

void BuildTangents(StaticMesh& mesh) {
    std::vector<glm::dvec3> tangents(mesh.vertices.size(), glm::dvec3(0));
    std::vector<glm::dvec3> bitangents(mesh.vertices.size(), glm::dvec3(0));
    for(std::size_t i = 0; i < mesh.indices.size(); i += 3) {
        const auto a = mesh.indices[i], b = mesh.indices[i+1], c = mesh.indices[i+2];
        const auto edge1 = mesh.vertices[b].position - mesh.vertices[a].position;
        const auto edge2 = mesh.vertices[c].position - mesh.vertices[a].position;
        const auto uv1 = glm::dvec2(mesh.vertices[b].uv - mesh.vertices[a].uv);
        const auto uv2 = glm::dvec2(mesh.vertices[c].uv - mesh.vertices[a].uv);
        const double determinant = uv1.x * uv2.y - uv1.y * uv2.x;
        if(std::abs(determinant) < 1e-20) continue;
        const double area = glm::length(glm::cross(edge1, edge2));
        const auto tangent = (edge1 * uv2.y - edge2 * uv1.y) / determinant;
        const auto bitangent = (edge2 * uv1.x - edge1 * uv2.x) / determinant;
        if(!Finite(tangent) || !Finite(bitangent) || !std::isfinite(area)) continue;
        for(const auto index : {a,b,c}) {
            tangents[index] += tangent * area;
            bitangents[index] += bitangent * area;
        }
    }
    for(std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        auto& vertex = mesh.vertices[i];
        const auto n = glm::dvec3(vertex.normal);
        const auto orthogonal = tangents[i] - n * glm::dot(n, tangents[i]);
        const auto axis = std::abs(vertex.normal.z) < .999f ? glm::vec3(0,0,1) : glm::vec3(0,1,0);
        const auto tangent = SafeNormal(glm::vec3(orthogonal), SafeNormal(glm::cross(axis, vertex.normal), {1,0,0}));
        vertex.tangent = glm::vec4(tangent, glm::dot(glm::cross(n, glm::dvec3(tangent)), bitangents[i]) < 0 ? -1.f : 1.f);
    }
}

// The native reader deliberately retains source vertex IDs. Assimp's PMX
// importer expands material meshes, and loses morph IDs and native IK records.
class PmxReader {
public:
    explicit PmxReader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

    template<class T> T Read() {
        static_assert(std::endian::native == std::endian::little, "PMX reader requires little-endian scalars");
        Require(sizeof(T));
        T result;
        std::memcpy(&result, bytes_.data() + cursor_, sizeof(T));
        cursor_ += sizeof(T);
        return result;
    }
    float Float() {
        const float value = Read<float>();
        if(!std::isfinite(value)) Fail("Non-finite PMX number");
        return value;
    }
    glm::dvec3 Vector() {
        glm::dvec3 value;
        for(int i = 0; i < 3; ++i) value[i] = Float();
        return value;
    }
    glm::dvec3 Position() { auto value = Vector(); value.z = -value.z; return value; }
    void Skip(std::size_t size) { Require(size); cursor_ += size; }
    std::uint32_t Count(std::size_t minimumRecordBytes = 1) {
        const auto value = Read<std::int32_t>();
        if(value < 0 || static_cast<std::size_t>(value) > Remaining() / minimumRecordBytes)
            Fail("Invalid PMX count");
        return static_cast<std::uint32_t>(value);
    }
    std::int32_t Index(unsigned size) {
        if(size == 1) return Read<std::int8_t>();
        if(size == 2) return Read<std::int16_t>();
        if(size == 4) return Read<std::int32_t>();
        Fail("PMX index size must be 1, 2, or 4");
    }
    std::uint32_t VertexIndex(unsigned size) {
        if(size == 1) return Read<std::uint8_t>();
        if(size == 2) return Read<std::uint16_t>();
        if(size == 4) return Read<std::uint32_t>();
        Fail("PMX vertex index size must be 1, 2, or 4");
    }
    std::string Text() {
        const auto length = Count();
        Require(length);
        const auto start = cursor_;
        cursor_ += length;
        if(utf8) return {reinterpret_cast<const char*>(bytes_.data() + start), length};
        if(length % 2) Fail("PMX UTF-16 text has an odd byte count");
        std::string result;
        for(std::size_t i = start; i < start + length; i += 2) {
            std::uint32_t code = bytes_[i] | (static_cast<std::uint32_t>(bytes_[i+1]) << 8);
            if(code >= 0xd800 && code <= 0xdbff) {
                if(i + 3 >= start + length) Fail("Incomplete PMX UTF-16 surrogate");
                const auto next = bytes_[i+2] | (static_cast<std::uint32_t>(bytes_[i+3]) << 8);
                if(next < 0xdc00 || next > 0xdfff) Fail("Invalid PMX UTF-16 surrogate");
                code = 0x10000 + ((code - 0xd800) << 10) + (next - 0xdc00);
                i += 2;
            } else if(code >= 0xdc00 && code <= 0xdfff) Fail("Invalid PMX UTF-16 surrogate");
            if(code < 0x80) result.push_back(static_cast<char>(code));
            else if(code < 0x800) {
                result.push_back(static_cast<char>(0xc0 | (code >> 6)));
                result.push_back(static_cast<char>(0x80 | (code & 63)));
            } else if(code < 0x10000) {
                result.push_back(static_cast<char>(0xe0 | (code >> 12)));
                result.push_back(static_cast<char>(0x80 | ((code >> 6) & 63)));
                result.push_back(static_cast<char>(0x80 | (code & 63)));
            } else {
                result.push_back(static_cast<char>(0xf0 | (code >> 18)));
                result.push_back(static_cast<char>(0x80 | ((code >> 12) & 63)));
                result.push_back(static_cast<char>(0x80 | ((code >> 6) & 63)));
                result.push_back(static_cast<char>(0x80 | (code & 63)));
            }
        }
        return result;
    }
    std::size_t Remaining() const { return bytes_.size() - cursor_; }
    [[noreturn]] void Fail(const std::string& message) const {
        throw std::runtime_error(message + " at PMX byte " + std::to_string(cursor_));
    }
    bool utf8 = false;
private:
    void Require(std::size_t size) const {
        if(size > Remaining()) Fail("Truncated PMX record");
    }
    const std::vector<std::uint8_t>& bytes_;
    std::size_t cursor_ = 0;
};

struct PmxVertex {
    StaticVertex vertex;
    VertexSkin skin;
    std::array<std::int32_t,4> joints{-1,-1,-1,-1};
};

struct PmxMaterial { std::string name; std::uint32_t count = 0; };

void ReadPmx(SkeletalAssetPackage& asset, const std::vector<std::uint8_t>& bytes) {
    PmxReader reader(bytes);
    if(reader.Read<std::uint32_t>() != 0x20584d50u) reader.Fail("Invalid PMX signature");
    const float version = reader.Float();
    if(std::abs(version - 2.0f) > .001f && std::abs(version - 2.1f) > .001f)
        reader.Fail("Only PMX 2.0 and 2.1 are supported");
    const auto headerSize = reader.Read<std::uint8_t>();
    if(headerSize < 8) reader.Fail("Incomplete PMX global header");
    const auto encoding = reader.Read<std::uint8_t>();
    if(encoding > 1) reader.Fail("Unknown PMX text encoding");
    reader.utf8 = encoding == 1;
    const auto extraUv = reader.Read<std::uint8_t>();
    if(extraUv > 4) reader.Fail("PMX supports at most four additional UV sets");
    const auto vertexSize = reader.Read<std::uint8_t>();
    const auto textureSize = reader.Read<std::uint8_t>();
    const auto materialSize = reader.Read<std::uint8_t>();
    const auto boneSize = reader.Read<std::uint8_t>();
    const auto morphSize = reader.Read<std::uint8_t>();
    const auto rigidSize = reader.Read<std::uint8_t>();
    for(const auto size : {vertexSize,textureSize,materialSize,boneSize,morphSize,rigidSize})
        if(size != 1 && size != 2 && size != 4) reader.Fail("Invalid PMX index width");
    reader.Skip(headerSize - 8);
    asset.name = reader.Text();
    reader.Text(); // English model name; bone and morph names retained below.
    reader.Text(); reader.Text(); // Source comments are data, never instructions.

    std::vector<PmxVertex> vertices(reader.Count(37));
    bool hasSdef = false, hasQdef = false;
    for(auto& vertex : vertices) {
        vertex.vertex.position = reader.Position();
        vertex.vertex.normal = SafeNormal(glm::vec3(reader.Position()));
        vertex.vertex.uv.x = reader.Float(); vertex.vertex.uv.y = reader.Float();
        reader.Skip(static_cast<std::size_t>(extraUv) * 16);
        const auto mode = reader.Read<std::uint8_t>();
        if(mode > 4) reader.Fail("Unknown PMX skinning mode");
        vertex.skin.mode = static_cast<SkinningMode>(mode);
        vertex.skin.weights = glm::vec4(0);
        if(mode == 0) {
            vertex.joints[0] = reader.Index(boneSize);
            vertex.skin.weights[0] = 1;
        } else if(mode == 1 || mode == 3) {
            vertex.joints[0] = reader.Index(boneSize); vertex.joints[1] = reader.Index(boneSize);
            const float weight = reader.Float();
            vertex.skin.weights = {weight,1-weight,0,0};
            if(mode == 3) {
                vertex.skin.sdefC = glm::vec3(reader.Position());
                vertex.skin.sdefR0 = glm::vec3(reader.Position());
                vertex.skin.sdefR1 = glm::vec3(reader.Position());
                hasSdef = true;
            }
        } else {
            for(auto& joint : vertex.joints) joint = reader.Index(boneSize);
            for(int i = 0; i < 4; ++i) vertex.skin.weights[i] = reader.Float();
            hasQdef |= mode == 4;
        }
        reader.Float(); // Edge scale, retained visually through the Toon material.
    }
    const auto indexCount = reader.Count(vertexSize);
    if(indexCount % 3) reader.Fail("PMX surface index count is not divisible by three");
    std::vector<std::uint32_t> indices(indexCount);
    for(auto& index : indices) {
        index = reader.VertexIndex(vertexSize);
        if(index >= vertices.size()) reader.Fail("PMX surface has an invalid vertex index");
    }
    const auto textureCount = reader.Count(4);
    for(std::uint32_t i = 0; i < textureCount; ++i) reader.Text();
    std::vector<PmxMaterial> materials(reader.Count(80));
    std::uint64_t totalMaterialIndices = 0;
    for(auto& material : materials) {
        material.name = reader.Text(); reader.Text();
        reader.Skip(65); // Material shading is converted by StaticModelLoader.
        reader.Index(textureSize); reader.Index(textureSize);
        reader.Read<std::uint8_t>();
        const auto sharedToon = reader.Read<std::uint8_t>();
        if(sharedToon > 1) reader.Fail("Invalid PMX shared toon flag");
        if(sharedToon) reader.Read<std::uint8_t>(); else reader.Index(textureSize);
        reader.Text();
        material.count = reader.Count(vertexSize);
        if(material.count % 3) reader.Fail("PMX material index count is not divisible by three");
        totalMaterialIndices += material.count;
    }
    if(totalMaterialIndices != indices.size()) reader.Fail("PMX material ranges do not cover the surface indices");
    if(asset.geometry->materials.empty()) {
        for(const auto& source : materials) {
            PbrMaterial material;
            material.name = source.name;
            asset.geometry->materials.push_back(std::move(material));
        }
    } else if(materials.size() != asset.geometry->materials.size())
        reader.Fail("Assimp material order does not match native PMX materials");

    auto& bones = asset.skeleton.bones;
    bones.resize(reader.Count(24));
    if(bones.empty()) reader.Fail("PMX has no skeleton");
    std::vector<glm::dvec3> bonePositions(bones.size());
    for(std::size_t i = 0; i < bones.size(); ++i) {
        auto& bone = bones[i];
        bone.name = reader.Text(); bone.englishName = reader.Text();
        bonePositions[i] = reader.Position();
        bone.parent = reader.Index(boneSize);
        if(bone.parent < -1 || bone.parent >= static_cast<std::int32_t>(bones.size()) || bone.parent == static_cast<std::int32_t>(i))
            reader.Fail("PMX bone parent index is invalid");
        reader.Read<std::int32_t>(); // Native transform layer.
        const auto flags = reader.Read<std::uint16_t>();
        bone.sourceFlags = flags;
        // Tail offsets affect PMX editor bone display only. Some Unity PMX
        // exporters emit NaN here even for an otherwise valid skeleton; these
        // unused display values must not invalidate the finite bind position.
        if(flags & 1) reader.Index(boneSize); else reader.Skip(12);
        if(flags & 0x300) {
            bone.inheritParent = reader.Index(boneSize);
            bone.inheritWeight = reader.Float();
            if(bone.inheritParent < -1 || bone.inheritParent >= static_cast<std::int32_t>(bones.size()))
                reader.Fail("PMX append-transform parent index is invalid");
        }
        if(flags & 0x400) bone.fixedAxis = reader.Position();
        if(flags & 0x800) { bone.localAxisX = reader.Position(); bone.localAxisZ = reader.Position(); }
        if(flags & 0x2000) reader.Read<std::int32_t>();
        if(flags & 0x20) {
            IkChain chain;
            chain.name = bone.name;
            chain.native = true;
            chain.targetBone = static_cast<std::int32_t>(i);
            chain.effector = reader.Index(boneSize);
            chain.iterations = reader.Count();
            chain.angleLimit = reader.Float();
            chain.target = bonePositions[i];
            chain.links.resize(reader.Count(boneSize + 1));
            if(chain.effector < 0 || chain.effector >= static_cast<std::int32_t>(bones.size()))
                reader.Fail("PMX IK effector index is invalid");
            for(auto& link : chain.links) {
                const auto index = reader.Index(boneSize);
                if(index < 0 || index >= static_cast<std::int32_t>(bones.size())) reader.Fail("PMX IK link index is invalid");
                link.bone = static_cast<std::uint32_t>(index);
                const auto limited = reader.Read<std::uint8_t>();
                if(limited > 1) reader.Fail("Invalid PMX IK angle-limit flag");
                link.limited = limited != 0;
                if(link.limited) {
                    const auto minimum = reader.Vector(), maximum = reader.Vector();
                    // Reflection flips rotation axes X/Y (axial vectors), not Z.
                    link.minimum = {-maximum.x,-maximum.y,minimum.z};
                    link.maximum = {-minimum.x,-minimum.y,maximum.z};
                }
            }
            chain.root = chain.links.empty() ? chain.effector : static_cast<std::int32_t>(chain.links.back().bone);
            asset.ikRig.chains.push_back(std::move(chain));
        }
    }
    for(std::size_t i = 0; i < bones.size(); ++i) {
        auto& bone = bones[i];
        bone.restLocal.translation = bonePositions[i] - (bone.parent < 0 ? glm::dvec3(0) : bonePositions[bone.parent]);
        bone.inverseBind = glm::translate(glm::dmat4(1), -bonePositions[i]);
    }
    if(std::any_of(bones.begin(),bones.end(),[](const auto& bone) { return (bone.sourceFlags & 0x300) != 0; }))
        asset.warnings.push_back("PMX append/grant-transform metadata is retained; the current preview does not evaluate inherited PMX bone transforms.");
    if(std::any_of(bones.begin(),bones.end(),[](const auto& bone) { return (bone.sourceFlags & 0xc00) != 0; }))
        asset.warnings.push_back("PMX fixed/local bone axes are retained as metadata; only explicit IK-link Euler limits are enforced by the preview solver.");
    // Detect cycles before handing the source to Assimp; runtime inspection is
    // deliberately tolerant of broken trees, while imported assets must be valid.
    std::vector<std::uint8_t> visited(bones.size(),0);
    for(std::size_t i = 0; i < bones.size(); ++i) {
        auto cursor = static_cast<std::int32_t>(i);
        while(cursor >= 0 && visited[cursor] == 0) { visited[cursor] = 1; cursor = bones[cursor].parent; }
        if(cursor >= 0 && visited[cursor] == 1) reader.Fail("Cycle in PMX bone hierarchy");
        cursor = static_cast<std::int32_t>(i);
        while(cursor >= 0 && visited[cursor] == 1) { visited[cursor] = 2; cursor = bones[cursor].parent; }
    }
    for(auto& vertex : vertices) {
        double sum = 0;
        for(int i = 0; i < 4; ++i) {
            const auto joint = vertex.joints[i];
            const auto weight = vertex.skin.weights[i];
            if(weight < -1e-5f || weight > 1.00001f) reader.Fail("PMX skin weight is outside [0,1]");
            if(weight > 0 && (joint < 0 || joint >= static_cast<std::int32_t>(bones.size())))
                reader.Fail("PMX weighted joint index is invalid");
            vertex.skin.joints[i] = joint < 0 || joint >= static_cast<std::int32_t>(bones.size()) ? 0u : static_cast<std::uint32_t>(joint);
            vertex.skin.weights[i] = std::max(weight,0.f);
            sum += vertex.skin.weights[i];
        }
        if(sum < 1e-8) reader.Fail("PMX vertex has no nonzero skin weight");
        vertex.skin.weights /= static_cast<float>(sum);
    }

    // Build material meshes while preserving a reversible source-index mapping.
    asset.geometry->meshes.clear(); asset.geometry->instances.clear(); asset.geometry->bounds = {};
    std::vector<std::vector<std::pair<std::uint32_t,std::uint32_t>>> sourceDestinations(vertices.size());
    std::size_t indexOffset = 0;
    for(std::uint32_t materialIndex = 0; materialIndex < materials.size(); ++materialIndex) {
        const auto& sourceMaterial = materials[materialIndex];
        if(!sourceMaterial.count) continue;
        StaticMesh mesh;
        MeshSkin skin;
        mesh.name = sourceMaterial.name;
        mesh.materialIndex = materialIndex;
        mesh.indices.reserve(sourceMaterial.count);
        std::unordered_map<std::uint32_t,std::uint32_t> mapping;
        const auto meshIndex = static_cast<std::uint32_t>(asset.geometry->meshes.size());
        for(std::uint32_t j = 0; j < sourceMaterial.count; j += 3) {
            for(const auto corner : {0u,2u,1u}) { // PMX LH -> engine RH winding.
                const auto sourceIndex = indices[indexOffset + j + corner];
                const auto [entry, inserted] = mapping.try_emplace(sourceIndex, static_cast<std::uint32_t>(mesh.vertices.size()));
                const auto destinationIndex = entry->second;
                if(inserted) {
                    const auto& value = vertices[sourceIndex];
                    mesh.vertices.push_back(value.vertex);
                    mesh.bounds.Include(value.vertex.position);
                    asset.geometry->bounds.Include(value.vertex.position);
                    skin.vertices.push_back(value.skin);
                    skin.sourceVertexIds.push_back(sourceIndex);
                    sourceDestinations[sourceIndex].emplace_back(meshIndex,destinationIndex);
                }
                mesh.indices.push_back(destinationIndex);
            }
        }
        indexOffset += sourceMaterial.count;
        BuildTangents(mesh);
        asset.geometry->instances.push_back({asset.name,meshIndex,glm::dmat4(1)});
        asset.geometry->meshes.push_back(std::move(mesh));
        asset.skins.push_back(std::move(skin));
    }
    asset.morphs.resize(reader.Count(14));
    std::size_t unsupportedMorphs = 0;
    for(auto& morph : asset.morphs) {
        morph.name = reader.Text(); morph.englishName = reader.Text();
        morph.panel = reader.Read<std::uint8_t>(); morph.sourceType = reader.Read<std::uint8_t>();
        const auto count = reader.Count();
        for(std::uint32_t i = 0; i < count; ++i) {
            const auto kind = morph.sourceType;
            if(kind == 1) {
                const auto sourceIndex = reader.VertexIndex(vertexSize);
                if(sourceIndex >= sourceDestinations.size()) reader.Fail("PMX morph vertex index is invalid");
                const auto delta = reader.Position();
                for(const auto [mesh,vertex] : sourceDestinations[sourceIndex])
                    morph.deltas.push_back({mesh,vertex,delta,glm::vec3(0)});
            } else if(kind == 0 || kind == 9) { reader.Index(morphSize); reader.Float(); }
            else if(kind == 2) { reader.Index(boneSize); reader.Skip(28); }
            else if(kind >= 3 && kind <= 7) { reader.VertexIndex(vertexSize); reader.Skip(16); }
            else if(kind == 8) { reader.Index(materialSize); reader.Skip(113); }
            else if(kind == 10) { reader.Index(rigidSize); reader.Skip(25); }
            else reader.Fail("Unknown PMX morph type");
        }
        if(morph.sourceType != 1) ++unsupportedMorphs;
    }
    const auto displayCount = reader.Count(13);
    for(std::uint32_t i = 0; i < displayCount; ++i) {
        reader.Text(); reader.Text(); reader.Read<std::uint8_t>();
        const auto count = reader.Count();
        for(std::uint32_t j = 0; j < count; ++j) {
            const auto morph = reader.Read<std::uint8_t>();
            if(morph > 1) reader.Fail("Invalid PMX display-frame element type");
            reader.Index(morph ? morphSize : boneSize);
        }
    }
    asset.sourceRigidBodies = reader.Count(70);
    asset.rigidBodies.resize(asset.sourceRigidBodies);
    for(auto& body : asset.rigidBodies) {
        body.name=reader.Text();body.englishName=reader.Text();body.bone=reader.Index(boneSize);
        if(body.bone < -1 || body.bone >= static_cast<std::int32_t>(bones.size())) reader.Fail("PMX rigid body bone is invalid");
        body.collisionGroup=reader.Read<std::uint8_t>();body.collisionMask=reader.Read<std::uint16_t>();
        body.shape=reader.Read<std::uint8_t>();body.dimensions=reader.Vector();body.position=reader.Position();
        const auto euler=reader.Vector();body.rotationEuler={-euler.x,-euler.y,euler.z};
        body.mass=reader.Float();body.linearDamping=reader.Float();body.angularDamping=reader.Float();
        body.restitution=reader.Float();body.friction=reader.Float();body.mode=reader.Read<std::uint8_t>();
        if(body.collisionGroup>15||body.shape>2||body.mode>2||glm::any(glm::lessThan(body.dimensions,glm::dvec3(0)))||body.mass<0||body.linearDamping<0||body.angularDamping<0||body.restitution<0||body.friction<0)
            reader.Fail("Invalid PMX rigid body physical parameters");
    }
    asset.sourceJoints = reader.Count(105);
    asset.physicsJoints.resize(asset.sourceJoints);
    for(auto& joint : asset.physicsJoints) {
        joint.name=reader.Text();joint.englishName=reader.Text();joint.type=reader.Read<std::uint8_t>();
        joint.rigidBodyA=reader.Index(rigidSize);joint.rigidBodyB=reader.Index(rigidSize);
        const auto validRigid=[&](std::int32_t i){return i>=-1&&(i<0||static_cast<std::uint32_t>(i)<asset.sourceRigidBodies);};
        if(joint.type>5||!validRigid(joint.rigidBodyA)||!validRigid(joint.rigidBodyB)) reader.Fail("Invalid PMX joint body/type");
        joint.position=reader.Position();const auto euler=reader.Vector();joint.rotationEuler={-euler.x,-euler.y,euler.z};
        const auto lo=reader.Vector(),hi=reader.Vector();joint.translationMinimum={lo.x,lo.y,-hi.z};joint.translationMaximum={hi.x,hi.y,-lo.z};
        const auto rlo=reader.Vector(),rhi=reader.Vector();joint.rotationMinimum={-rhi.x,-rhi.y,rlo.z};joint.rotationMaximum={-rlo.x,-rlo.y,rhi.z};
        joint.translationSpring=reader.Vector();joint.rotationSpring=reader.Vector();
        if(glm::any(glm::greaterThan(joint.translationMinimum,joint.translationMaximum))||glm::any(glm::greaterThan(joint.rotationMinimum,joint.rotationMaximum))) reader.Fail("Inverted PMX joint limits");
    }
    if(reader.Remaining() == 4 && version < 2.05f) {
        if(reader.Read<std::uint32_t>() != 0) reader.Fail("Unexpected PMX 2.0 trailing bytes");
    } else if(reader.Remaining() && version < 2.05f) reader.Fail("Unexpected PMX 2.0 trailing bytes");
    else if(reader.Remaining()) asset.warnings.push_back("PMX 2.1 soft-body payload is not evaluated or exported in this asset version.");
    if(extraUv) asset.warnings.push_back("Additional PMX UV sets are not used by the current Material system.");
    if(hasSdef || hasQdef) asset.warnings.push_back("SDEF/QDEF source modes and parameters are preserved; the preview currently uses normalized linear blend skinning.");
    if(unsupportedMorphs) asset.warnings.push_back(std::to_string(unsupportedMorphs) +
        " non-vertex PMX morphs retain names/type only; group, bone, UV, material and impulse offsets are not evaluated.");
    if(asset.sourceRigidBodies || asset.sourceJoints)
        asset.warnings.push_back("PMX rigid-body/joint physical metadata is preserved; runtime secondary motion requires a generated simulation asset.");
    asset.warnings.push_back("PMX native coordinate system converted to right-handed Y-up by reflecting Z; source units retained.");
}

class SourceStream final : public Assimp::IOStream {
public:
    explicit SourceStream(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}
    std::size_t Read(void* destination, std::size_t size, std::size_t count) override {
        if(!size || !count) return 0;
        const auto actual = std::min(count,(bytes_.size()-cursor_)/size);
        if(actual) std::memcpy(destination,bytes_.data()+cursor_,size*actual);
        cursor_ += size*actual;
        return actual;
    }
    std::size_t Write(const void*,std::size_t,std::size_t) override { return 0; }
    aiReturn Seek(std::size_t offset, aiOrigin origin) override {
        std::size_t next = 0;
        if(origin == aiOrigin_SET) next = offset;
        else if(origin == aiOrigin_CUR) {
            if(offset > bytes_.size()-cursor_) return aiReturn_FAILURE;
            next = cursor_+offset;
        } else if(origin == aiOrigin_END) {
            if(offset > bytes_.size()) return aiReturn_FAILURE;
            next = bytes_.size()-offset;
        } else return aiReturn_FAILURE;
        if(next > bytes_.size()) return aiReturn_FAILURE;
        cursor_ = next; return aiReturn_SUCCESS;
    }
    std::size_t Tell() const override { return cursor_; }
    std::size_t FileSize() const override { return bytes_.size(); }
    void Flush() override {}
private:
    std::vector<std::uint8_t> bytes_;
    std::size_t cursor_ = 0;
};

class SourceIO final : public Assimp::IOSystem {
public:
    explicit SourceIO(std::filesystem::path directory) : directory_(std::move(directory)) {}
    bool Exists(const char* file) const override {
        std::error_code error;
        return std::filesystem::is_regular_file(Resolve(file),error);
    }
    char getOsSeparator() const override { return '/'; }
    Assimp::IOStream* Open(const char* file,const char* mode) override {
        if(!file || !mode || std::strchr(mode,'w') || std::strchr(mode,'+')) return nullptr;
        try { return new SourceStream(ReadFile(Resolve(file))); } catch(const std::exception&) { return nullptr; }
    }
    void Close(Assimp::IOStream* stream) override { delete stream; }
private:
    std::filesystem::path Resolve(const char* file) const {
        if(!file) return {};
        auto path = NativePath(file);
        if(path.is_absolute()) return path;
        std::error_code error;
        if(std::filesystem::is_regular_file(directory_/path,error)) return directory_/path;
        if(std::filesystem::is_regular_file(path,error)) return path;
        return directory_/path;
    }
    std::filesystem::path directory_;
};

glm::dmat4 Matrix(const aiMatrix4x4& value) {
    glm::dmat4 output(1);
    for(int row = 0; row < 4; ++row) for(int column = 0; column < 4; ++column) {
        const double component = value[row][column];
        if(!std::isfinite(component)) throw std::runtime_error("Non-finite skeletal node transform");
        output[column][row] = component;
    }
    return output;
}

BoneTransform Transform(const aiMatrix4x4& matrix) {
    aiVector3D scale,translation;
    aiQuaternion rotation;
    matrix.Decompose(scale,rotation,translation);
    BoneTransform result;
    result.translation = {translation.x,translation.y,translation.z};
    result.scale = {scale.x,scale.y,scale.z};
    result.rotation = glm::normalize(glm::dquat(rotation.w,rotation.x,rotation.y,rotation.z));
    if(!Finite(result.translation) || !Finite(result.scale) || !std::isfinite(result.rotation.w) ||
       !std::isfinite(result.rotation.x) || !std::isfinite(result.rotation.y) || !std::isfinite(result.rotation.z))
        throw std::runtime_error("Cannot decompose skeletal node transform");
    const auto original = Matrix(matrix), reconstructed = TransformMatrix(result);
    double difference = 0, magnitude = 1;
    for(int c = 0; c < 4; ++c) for(int r = 0; r < 4; ++r) {
        difference = std::max(difference,std::abs(original[c][r]-reconstructed[c][r]));
        magnitude = std::max(magnitude,std::abs(original[c][r]));
    }
    if(difference > magnitude * 1e-5)
        throw std::runtime_error("Source skeleton contains shear not representable by bone TRS; bake node transforms before import");
    return result;
}

std::vector<detail::SourceMeshSemantics> ReadAssimp(SkeletalAssetPackage& asset,const LoadOptions& options,const std::filesystem::path& importPath) {
    Assimp::Importer importer;
    importer.SetIOHandler(new SourceIO(importPath.parent_path()));
    importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE,aiPrimitiveType_POINT | aiPrimitiveType_LINE);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS,true);
    unsigned flags = aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace |
        aiProcess_SortByPType | aiProcess_ValidateDataStructure;
    if(options.flipUVs) flags |= aiProcess_FlipUVs;
    const auto* scene = importer.ReadFile(Utf8(importPath),flags);
    if(!scene || !scene->mRootNode) throw std::runtime_error("Assimp skeletal import: " + std::string(importer.GetErrorString()));

    std::unordered_map<std::string,std::uint32_t> names;
    std::vector<glm::dmat4> global;
    std::unordered_map<const aiNode*,std::uint32_t> nodeIndices;
    std::function<void(const aiNode&,std::int32_t,const glm::dmat4&,unsigned)> visit;
    visit = [&](const aiNode& node,std::int32_t parent,const glm::dmat4& parentGlobal,unsigned depth) {
        if(depth > 4096) throw std::runtime_error("Skeleton hierarchy is too deep");
        const auto index = static_cast<std::uint32_t>(asset.skeleton.bones.size());
        SkeletonBone bone;
        bone.name = node.mName.C_Str(); bone.parent = parent;
        if(bone.name.empty()) bone.name = "Node_" + std::to_string(index);
        const auto transform = parentGlobal * Matrix(node.mTransformation);
        if(std::abs(glm::determinant(transform)) < 1e-20) throw std::runtime_error("Skeleton has a singular rest transform");
        bone.restLocal = Transform(node.mTransformation);
        bone.inverseBind = glm::inverse(transform);
        if(!names.emplace(bone.name,index).second)
            asset.warnings.push_back("Duplicate node name: " + bone.name + "; animation channels resolve to the first occurrence.");
        nodeIndices.emplace(&node,index);
        asset.skeleton.bones.push_back(std::move(bone)); global.push_back(transform);
        for(unsigned i = 0; i < node.mNumChildren; ++i) visit(*node.mChildren[i],static_cast<std::int32_t>(index),transform,depth+1);
    };
    visit(*scene->mRootNode,-1,glm::dmat4(1),0);

    // Both imports use identical flags, so tangent splits and mesh order match.
    std::vector<std::uint32_t> meshMap(scene->mNumMeshes,std::numeric_limits<std::uint32_t>::max());
    std::uint32_t destinationMesh = 0;
    for(unsigned i = 0; i < scene->mNumMeshes; ++i) {
        const auto& mesh = *scene->mMeshes[i];
        if(!mesh.mNumVertices || !mesh.mNumFaces || !mesh.HasPositions()) continue;
        if(destinationMesh >= asset.geometry->meshes.size() || asset.geometry->meshes[destinationMesh].vertices.size() != mesh.mNumVertices)
            throw std::runtime_error("Skeletal/static source imports disagree on mesh topology");
        meshMap[i] = destinationMesh++;
    }
    if(destinationMesh != asset.geometry->meshes.size()) throw std::runtime_error("Skeletal/static source imports disagree on mesh count");
    asset.skins.resize(asset.geometry->meshes.size());
    std::vector<detail::SourceMeshSemantics> semantics(asset.geometry->meshes.size());
    for(std::uint32_t mesh=0;mesh<semantics.size();++mesh)semantics[mesh].mesh=mesh;
    std::vector<std::int32_t> meshNodes(asset.skins.size(),-1);
    std::function<void(const aiNode&)> assignMeshNodes;
    assignMeshNodes = [&](const aiNode& node) {
        std::set<unsigned> materials;
        for(unsigned i=0;i<node.mNumMeshes;++i) {
            if(node.mMeshes[i]>=scene->mNumMeshes)throw std::runtime_error("Node has an invalid source mesh reference");
            materials.insert(scene->mMeshes[node.mMeshes[i]]->mMaterialIndex);
        }
        for(unsigned i = 0; i < node.mNumMeshes; ++i) {
            if(node.mMeshes[i] >= meshMap.size()) throw std::runtime_error("Node has an invalid mesh reference");
            const auto mesh = meshMap[node.mMeshes[i]];
            if(mesh == std::numeric_limits<std::uint32_t>::max()) continue;
            const auto bone = static_cast<std::int32_t>(nodeIndices.at(&node));
            if(meshNodes[mesh] >= 0 && meshNodes[mesh] != bone)
                throw std::runtime_error("Skeletal mesh has multiple node instances with distinct bind transforms; expand instances before import");
            meshNodes[mesh] = bone;
            auto& input=semantics[mesh];input.mesh=mesh;input.sourceObject=node.mName.C_Str();
            input.sourceMesh=asset.geometry->meshes[mesh].name;
            const auto material=asset.geometry->meshes[mesh].materialIndex;
            if(material<asset.geometry->materials.size())input.sourceMaterial=asset.geometry->materials[material].name;
            input.sourceObjectMaterialCount=std::max(1u,static_cast<unsigned>(materials.size()));
            // Preserve original hierarchy names as weak object-level evidence.
            // Material names remain section-specific and take precedence.
            for(auto* parent=node.mParent;parent&&parent->mParent;parent=parent->mParent)
                input.collections.emplace_back(parent->mName.C_Str());
        }
        for(unsigned i = 0; i < node.mNumChildren; ++i) assignMeshNodes(*node.mChildren[i]);
    };
    assignMeshNodes(*scene->mRootNode);
    std::size_t extendedWeightVertices = 0;
    std::vector<std::vector<std::uint32_t>> meshMorphMap(scene->mNumMeshes);
    for(unsigned sourceIndex = 0; sourceIndex < scene->mNumMeshes; ++sourceIndex) {
        const auto meshIndex = meshMap[sourceIndex];
        if(meshIndex == std::numeric_limits<std::uint32_t>::max()) continue;
        const auto& source = *scene->mMeshes[sourceIndex];
        auto& skin = asset.skins[meshIndex];
        skin.vertices.resize(source.mNumVertices);
        skin.sourceVertexIds.resize(source.mNumVertices);
        const auto meshBone = meshNodes[meshIndex] < 0 ? 0u : static_cast<std::uint32_t>(meshNodes[meshIndex]);
        const auto extension=Lower(Utf8(importPath.extension()));
        const bool gltfSkin=(extension==".gltf"||extension==".glb")&&source.HasBones();
        // glTF ignores the skinned mesh node transform; the renderer instance
        // is identity too. Preserve original joint TRS and IBM without adding
        // a second mesh-node transform to the bind reference.
        skin.bindTransform = gltfSkin?glm::dmat4(1):global[meshBone];
        skin.boneFromMesh.reserve(asset.skeleton.bones.size());
        for(const auto& bone : asset.skeleton.bones) skin.boneFromMesh.push_back(bone.inverseBind * skin.bindTransform);
        std::vector<std::vector<std::pair<std::uint32_t,float>>> influences(source.mNumVertices);
        for(unsigned i = 0; i < source.mNumBones; ++i) {
            const auto& sourceBone = *source.mBones[i];
            const auto found = names.find(sourceBone.mName.C_Str());
            if(found == names.end()) throw std::runtime_error("Weighted bone has no matching scene node: " + std::string(sourceBone.mName.C_Str()));
            const auto boneIndex = found->second;
            skin.boneFromMesh[boneIndex] = Matrix(sourceBone.mOffsetMatrix);
            for(unsigned j = 0; j < sourceBone.mNumWeights; ++j) {
                const auto& weight = sourceBone.mWeights[j];
                if(weight.mVertexId >= influences.size()) throw std::runtime_error("Assimp skin has invalid vertex ID");
                if(!std::isfinite(weight.mWeight) || weight.mWeight < 0) throw std::runtime_error("Assimp skin has invalid weight");
                if(weight.mWeight > 0) influences[weight.mVertexId].emplace_back(boneIndex,static_cast<float>(weight.mWeight));
            }
        }
        bool needsFullInfluences=false;
        for(auto& weights:influences) {
            std::sort(weights.begin(),weights.end(),[](const auto& a,const auto& b){return a.first<b.first;});
            std::vector<std::pair<std::uint32_t,float>> combined;
            for(const auto& weight:weights) {
                if(!combined.empty()&&combined.back().first==weight.first)combined.back().second+=weight.second;
                else combined.push_back(weight);
            }
            weights=std::move(combined);needsFullInfluences|=weights.size()>4;
        }
        if(needsFullInfluences){skin.influenceOffsets.reserve(source.mNumVertices+1ull);skin.influenceOffsets.push_back(0);}
        for(unsigned i = 0; i < source.mNumVertices; ++i) {
            skin.sourceVertexIds[i] = i;
            auto& value = skin.vertices[i];
            auto& sourceWeights = influences[i];
            value.joints = {meshBone,0,0,0}; value.weights = {1,0,0,0};
            if(sourceWeights.empty()) {
                if(needsFullInfluences){skin.influences.push_back({meshBone,1});skin.influenceOffsets.push_back(static_cast<std::uint32_t>(skin.influences.size()));}
                continue;
            }
            std::sort(sourceWeights.begin(),sourceWeights.end(),[](const auto& a,const auto& b) { return a.second > b.second; });
            if(sourceWeights.size() > 4) ++extendedWeightVertices;
            value.weights = glm::vec4(0);
            double sum = 0;
            for(std::size_t j = 0; j < std::min<std::size_t>(4,sourceWeights.size()); ++j) {
                value.joints[j] = sourceWeights[j].first; value.weights[static_cast<int>(j)] = sourceWeights[j].second;
                sum += sourceWeights[j].second;
            }
            if(sum < 1e-12) throw std::runtime_error("Assimp vertex skin weights sum to zero");
            value.weights /= static_cast<float>(sum);
            if(needsFullInfluences) {
                double fullSum=0;for(const auto& weight:sourceWeights)fullSum+=weight.second;
                if(!std::isfinite(fullSum)||fullSum<1e-12||skin.influences.size()+sourceWeights.size()>std::numeric_limits<std::uint32_t>::max())
                    throw std::runtime_error("Full skin influence buffer exceeds its bounds or has invalid weights");
                for(const auto& [joint,weight]:sourceWeights)skin.influences.push_back({joint,static_cast<float>(weight/fullSum)});
                skin.influenceOffsets.push_back(static_cast<std::uint32_t>(skin.influences.size()));
            }
            value.mode = sourceWeights.size() == 1 ? SkinningMode::Bdef1 :
                sourceWeights.size() == 2 ? SkinningMode::Bdef2 : SkinningMode::Bdef4;
        }
        for(unsigned morphIndex = 0; morphIndex < source.mNumAnimMeshes; ++morphIndex) {
            const auto& sourceMorph = *source.mAnimMeshes[morphIndex];
            if(sourceMorph.mNumVertices != source.mNumVertices) throw std::runtime_error("Assimp morph topology does not match base mesh");
            MorphTarget morph;
            morph.name = sourceMorph.mName.length ? sourceMorph.mName.C_Str() : "Mesh_" + std::to_string(meshIndex) + "_Morph_" + std::to_string(morphIndex);
            for(unsigned v = 0; v < source.mNumVertices; ++v) {
                glm::dvec3 position(0); glm::vec3 normal(0);
                if(sourceMorph.mVertices) position = glm::dvec3(sourceMorph.mVertices[v].x-source.mVertices[v].x,
                    sourceMorph.mVertices[v].y-source.mVertices[v].y,sourceMorph.mVertices[v].z-source.mVertices[v].z);
                if(sourceMorph.mNormals && source.mNormals) normal = glm::vec3(sourceMorph.mNormals[v].x-source.mNormals[v].x,
                    sourceMorph.mNormals[v].y-source.mNormals[v].y,sourceMorph.mNormals[v].z-source.mNormals[v].z);
                if(glm::dot(position,position) > 0 || glm::dot(normal,normal) > 0)
                    morph.deltas.push_back({meshIndex,v,position,normal});
            }
            meshMorphMap[sourceIndex].push_back(static_cast<std::uint32_t>(asset.morphs.size()));
            asset.morphs.push_back(std::move(morph));
        }
    }
    if(extendedWeightVertices) asset.warnings.push_back(std::to_string(extendedWeightVertices) + " vertices retain complete skin influences; four-weight data is a compatibility summary only.");
    for(unsigned animationIndex = 0; animationIndex < scene->mNumAnimations; ++animationIndex) {
        const auto& source = *scene->mAnimations[animationIndex];
        AnimationSequenceAsset sequence;
        sequence.id = asset.id + "/animation-" + std::to_string(animationIndex);
        sequence.skeletonId = asset.skeleton.id;
        sequence.name = source.mName.length ? source.mName.C_Str() : "Animation_" + std::to_string(animationIndex);
        sequence.source = Utf8(asset.sourcePath.filename());
        const double ticksPerSecond = source.mTicksPerSecond > 0 ? static_cast<double>(source.mTicksPerSecond) : 25.0;
        if(!std::isfinite(ticksPerSecond) || !std::isfinite(source.mDuration)) throw std::runtime_error("Non-finite animation timing");
        // glTF's 1000 ticks/second is a time unit, not an authored sample rate.
        // The sequence stores times in seconds and exposes a practical preview
        // sampling rate when the source has no conventional frame-rate value.
        sequence.framesPerSecond = ticksPerSecond <= 240 ? ticksPerSecond : 30;
        sequence.duration = std::max(0.0,static_cast<double>(source.mDuration)/ticksPerSecond);
        for(unsigned channelIndex = 0; channelIndex < source.mNumChannels; ++channelIndex) {
            const auto& channel = *source.mChannels[channelIndex];
            const auto bone = names.find(channel.mNodeName.C_Str());
            if(bone == names.end()) {
                asset.warnings.push_back("Animation channel has no matching node: " + std::string(channel.mNodeName.C_Str()));
                continue;
            }
            BoneAnimationTrack track; track.bone = bone->second;
            for(unsigned k = 0; k < channel.mNumPositionKeys; ++k) {
                const auto& key = channel.mPositionKeys[k];
                const auto time = static_cast<double>(key.mTime)/ticksPerSecond;
                track.translations.push_back({time,{key.mValue.x,key.mValue.y,key.mValue.z}});
                sequence.duration = std::max(sequence.duration,time);
            }
            for(unsigned k = 0; k < channel.mNumRotationKeys; ++k) {
                const auto& key = channel.mRotationKeys[k];
                const auto time = static_cast<double>(key.mTime)/ticksPerSecond;
                track.rotations.push_back({time,glm::normalize(glm::dquat(key.mValue.w,key.mValue.x,key.mValue.y,key.mValue.z))});
                sequence.duration = std::max(sequence.duration,time);
            }
            for(unsigned k = 0; k < channel.mNumScalingKeys; ++k) {
                const auto& key = channel.mScalingKeys[k];
                const auto time = static_cast<double>(key.mTime)/ticksPerSecond;
                track.scales.push_back({time,{key.mValue.x,key.mValue.y,key.mValue.z}});
                sequence.duration = std::max(sequence.duration,time);
            }
            sequence.boneTracks.push_back(std::move(track));
        }
        auto channelMeshes = [&](const std::string& name) {
            std::vector<unsigned> result;
            for(unsigned mesh = 0; mesh < scene->mNumMeshes; ++mesh) {
                if(meshMap[mesh] == std::numeric_limits<std::uint32_t>::max() || meshMorphMap[mesh].empty()) continue;
                const auto node = meshNodes[meshMap[mesh]];
                if(name == scene->mMeshes[mesh]->mName.C_Str() ||
                   (node >= 0 && name == asset.skeleton.bones[node].name)) result.push_back(mesh);
            }
            return result;
        };
        for(unsigned channelIndex = 0; channelIndex < source.mNumMorphMeshChannels; ++channelIndex) {
            const auto& channel = *source.mMorphMeshChannels[channelIndex];
            const auto matches = channelMeshes(channel.mName.C_Str());
            if(matches.empty()) asset.warnings.push_back("Morph animation has no matching mesh: " + std::string(channel.mName.C_Str()));
            for(const auto mesh : matches) {
                std::vector<MorphAnimationTrack> tracks(meshMorphMap[mesh].size());
                for(std::size_t i = 0; i < tracks.size(); ++i) tracks[i].morph = meshMorphMap[mesh][i];
                for(unsigned k = 0; k < channel.mNumKeys; ++k) {
                    const auto& key = channel.mKeys[k];
                    const double time = key.mTime/ticksPerSecond;
                    std::vector<double> weights(tracks.size(),0);
                    for(unsigned w = 0; w < key.mNumValuesAndWeights; ++w) {
                        if(key.mValues[w] >= weights.size()) throw std::runtime_error("Morph animation references an invalid target");
                        weights[key.mValues[w]] = key.mWeights[w];
                    }
                    for(std::size_t i = 0; i < tracks.size(); ++i) tracks[i].keys.push_back({time,weights[i]});
                    sequence.duration = std::max(sequence.duration,time);
                }
                for(auto& track : tracks) sequence.morphTracks.push_back(std::move(track));
            }
        }
        if(source.mNumMeshChannels) asset.warnings.push_back("Animation " + sequence.name +
            " includes discrete mesh-swap channels; this asset version retains bone and blend-shape weight tracks only.");
        double earliest = 0;
        auto inspectTimes = [&](const auto& keys) { for(const auto& key : keys) earliest = std::min(earliest,key.time); };
        for(const auto& track : sequence.boneTracks) { inspectTimes(track.translations); inspectTimes(track.rotations); inspectTimes(track.scales); }
        for(const auto& track : sequence.morphTracks) inspectTimes(track.keys);
        if(earliest < 0) {
            auto shiftTimes = [&](auto& keys) { for(auto& key : keys) key.time -= earliest; };
            for(auto& track : sequence.boneTracks) { shiftTimes(track.translations); shiftTimes(track.rotations); shiftTimes(track.scales); }
            for(auto& track : sequence.morphTracks) shiftTimes(track.keys);
            sequence.duration -= earliest;
            asset.warnings.push_back("Animation " + sequence.name + " had negative source key times; clip origin was shifted to retain the complete source timeline.");
        }
        asset.animations.push_back(std::move(sequence));
    }
    if(asset.skeleton.bones.empty()) throw std::runtime_error("Source has no scene nodes for a skeleton");
    asset.warnings.push_back("Assimp skeleton retains all scene nodes to preserve hierarchy and animation channels; source units retained.");
    std::erase_if(semantics,[&](const auto& input){return input.mesh>=meshNodes.size()||meshNodes[input.mesh]<0;});
    return semantics;
}

} // namespace

std::shared_ptr<SkeletalAssetPackage> ImportSkeletalSource(const std::filesystem::path& source,
    const LoadOptions& options,std::string* error) {
    if(error) error->clear();
    try {
        std::string loadError;
        auto result = std::make_shared<SkeletalAssetPackage>();
        result->sourcePath = std::filesystem::absolute(source).lexically_normal();
        if(std::filesystem::is_directory(result->sourcePath)) {
            std::vector<std::filesystem::path> candidates;
            for(const auto& entry : std::filesystem::directory_iterator(result->sourcePath))
                if(entry.is_regular_file() && StaticModelLoader::SupportsExtension(Utf8(entry.path().extension()))) candidates.push_back(entry.path());
            if(candidates.size() != 1) throw std::runtime_error("Choose an explicit source model; directory must contain exactly one supported model");
            result->sourcePath = candidates.front();
        }
        const auto bytes = ReadFile(result->sourcePath);
        result->id = ContentId(bytes);
        result->name = Utf8(result->sourcePath.stem());
        result->sourceFormat = Lower(Utf8(result->sourcePath.extension()));
        if(!result->sourceFormat.empty()) result->sourceFormat.erase(0,1);
        result->skeleton.id = result->id + "/skeleton";
        std::optional<PreparedBlendSource> blend;
        std::vector<detail::SourceMeshSemantics> nodeSemantics;
        auto importPath = result->sourcePath;
        if(result->sourceFormat == "blend") {
            BlendImportOptions bridgeOptions;
            bridgeOptions.blenderExecutable = options.blenderExecutable;
            bridgeOptions.cacheDirectory = options.blendCacheDirectory;
            blend = PrepareBlendSource(result->sourcePath,bridgeOptions,&loadError);
            if(!blend) throw std::runtime_error(loadError);
            importPath = blend->glbPath;
        }
        if(result->sourceFormat == "pmx") {
            // Validate and parse the binary before sending it to a third-party
            // importer. Only decoded Material-system resources are reused.
            result->geometry = std::make_shared<StaticModel>();
            result->geometry->sourcePath = result->sourcePath;
            ReadPmx(*result,bytes);
            const auto materials = StaticModelLoader::Load(result->sourcePath,options,&loadError);
            if(!materials) throw std::runtime_error(loadError);
            if(materials->materials.size() != result->geometry->materials.size())
                throw std::runtime_error("Assimp material order does not match native PMX materials");
            result->geometry->materials = materials->materials;
            result->geometry->textures = materials->textures;
            result->warnings.insert(result->warnings.end(),materials->warnings.begin(),materials->warnings.end());
        } else {
            {
                const auto model = StaticModelLoader::Load(importPath,options,&loadError);
                if(!model) throw std::runtime_error(loadError);
                result->geometry = std::make_shared<StaticModel>(*model);
                result->warnings = model->warnings;
            } // Release the temporary decoded textures before the skeletal pass.
            nodeSemantics=ReadAssimp(*result,options,importPath);
        }
        result->geometry->sourcePath = result->sourcePath;
        if(blend) {
            result->warnings.insert(result->warnings.end(),blend->warnings.begin(),blend->warnings.end());
            const auto semantics = detail::ReadBlendMeshSemantics(*result,blend->metadataPath);
            result->meshRegions = detail::RecognizeGarmentSections(*result,semantics);
            for(auto& region:result->meshRegions)if(std::none_of(semantics.begin(),semantics.end(),
                [&](const auto& input){return input.mesh==region.mesh;})) {
                region.requiresReview=true;
                region.evidence.push_back("Blender provenance mapping is missing or ambiguous; review this material section.");
            }
            detail::ImportBlendSourcePose(*result,blend->metadataPath);
        } else if(result->sourceFormat=="fbx") {
            auto metadataPath=result->sourcePath;metadataPath+=".max.json";
            detail::ApplyMaxSceneMetadata(*result,nodeSemantics,metadataPath,bytes);
        } else result->meshRegions = detail::RecognizeGarmentSections(*result,nodeSemantics);
        std::erase(result->warnings,"Imported static rest/bind pose; skeletal animation and morph targets are not evaluated.");
        result->skeleton.name = result->name + " Skeleton";
        auto nativeChains = std::move(result->ikRig.chains);
        result->ikRig = RecognizeHumanoidRig(result->skeleton);
        result->ikRig.id = result->id + "/ik-rig";
        result->ikRig.name = result->name + " IK Rig";
        result->ikRig.skeletonId = result->skeleton.id;
        result->ikRig.chains.insert(result->ikRig.chains.begin(),nativeChains.begin(),nativeChains.end());
        AddGeneratedPreviewSequences(*result);
        if(!ValidateSkeletalAsset(*result,&loadError)) throw std::runtime_error("Imported skeletal asset validation failed: " + loadError);
        return result;
    } catch(const std::exception& exception) {
        if(error) *error = exception.what();
        return {};
    }
}

} // namespace Model
