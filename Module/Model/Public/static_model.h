#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace Model {

enum class AlphaMode : std::uint8_t { Opaque, Mask, Blend };

struct Bounds {
    glm::dvec3 minimum{0}, maximum{0};
    bool valid = false;
    glm::dvec3 Center() const { return (minimum + maximum) * 0.5; }
    glm::dvec3 Extent() const { return maximum - minimum; }
    void Include(const glm::dvec3& point);
};

// No vertex quantization or mesh reduction. Double CPU positions/transforms
// preserve imported precision; source formats/Assimp builds may use float data.
struct StaticVertex {
    glm::dvec3 position{0};
    glm::vec3 normal{0, 1, 0};
    glm::vec2 uv{0};
    glm::vec4 tangent{1, 0, 0, 1};
};

struct StaticMesh {
    std::string name;
    std::vector<StaticVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::uint32_t materialIndex = 0;
    Bounds bounds;
};

struct TextureImage {
    std::string name;
    std::uint32_t width = 0, height = 0;
    // Top row first. Exactly one array is populated. HDR/16-bit images can
    // retain their precision in linear or encoded float RGBA, as appropriate.
    std::vector<std::uint8_t> rgba;
    std::vector<float> rgba32f;
    bool hasAlpha = false;
    bool linearColor = false; // HDR color is linear; PNG/JPEG color is sRGB encoded
};

struct PbrMaterial {
    std::string name;
    glm::vec4 baseColor{1}; // linear factor; albedo texture is sRGB
    glm::vec3 emissive{0};
    float metallic = 0, roughness = 0.6f, ao = 1, normalScale = 1;
    glm::vec4 uvTransform{1, 1, 0, 0};
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool twoSided = false;
    // Material PBR slots: albedo, normal, metallic, roughness, AO, distortion,
    // flow, emissive. Scalar maps must have the selected channel in red and
    // include their scalar factor (the current PBR shader selects map OR factor).
    // Emission maps are linear. -1 means use the renderer's neutral fallback.
    std::array<std::int32_t, 8> textures{-1, -1, -1, -1, -1, -1, -1, -1};
};

struct MeshInstance {
    std::string nodeName;
    std::uint32_t meshIndex = 0;
    glm::dmat4 transform{1}; // accumulated scene-node transform
};

struct StaticModel {
    std::filesystem::path sourcePath;
    std::vector<StaticMesh> meshes;
    std::vector<MeshInstance> instances;
    std::vector<PbrMaterial> materials;
    std::vector<TextureImage> textures;
    Bounds bounds; // scene-node transforms applied, source units retained
    std::vector<std::string> warnings;
    std::uint64_t VertexCount() const;
    std::uint64_t TriangleCount() const;
};

struct LoadOptions {
    bool flipUVs = true; // top-row-first image upload; PMX uses its native UVs
    bool strictTextures = false; // missing/unsupported maps warn, or fail
    float defaultRoughness = 0.6f;
    std::vector<std::filesystem::path> textureSearchPaths;
    // .blend is converted by a separate Blender process into an interchange
    // copy. Empty executable discovers BLENDER_EXECUTABLE or installed Blender.
    std::filesystem::path blenderExecutable;
    std::filesystem::path blendCacheDirectory;
};

// CPU-only and independent of a graphics context. A directory selects its
// model deterministically; ambiguous directories report an actionable error.
class StaticModelLoader {
public:
    static std::shared_ptr<const StaticModel> Load(const std::filesystem::path& path,
        const LoadOptions& options = {}, std::string* error = nullptr);
    static bool SupportsExtension(std::string extension);
    static std::string SupportedExtensions();
};

} // namespace Model
