#include "Model/Public/static_model.h"
#include "Model/Private/blend_importer.h"

#include <assimp/Importer.hpp>
#include <assimp/IOStream.hpp>
#include <assimp/IOSystem.hpp>
#include <assimp/GltfMaterial.h>
#include <assimp/cimport.h>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/version.h>
// Keep decoder state local to this translation unit: Render's image loaders
// use the global stb vertical-flip switch, which must not change model images.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace Model {
namespace {

std::string Utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::filesystem::path FromUtf8(std::string text) {
    std::replace(text.begin(), text.end(), '\\', '/');
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if(!file) throw std::runtime_error("Cannot open file: " + Utf8(path));
    const auto end = file.tellg();
    if(end < 0 || static_cast<std::uintmax_t>(end) > std::numeric_limits<std::size_t>::max() ||
       static_cast<std::uintmax_t>(end) > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
        throw std::runtime_error("File is too large: " + Utf8(path));
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    file.seekg(0);
    if(!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("Cannot read file: " + Utf8(path));
    return bytes;
}

// Assimp's default narrow fopen is not reliable for Chinese/Japanese Windows
// paths. Both the main model and auxiliary MTL/buffer files use native paths.
class FileStream final : public Assimp::IOStream {
public:
    explicit FileStream(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}
    std::size_t Read(void* destination, std::size_t size, std::size_t count) override {
        if(size == 0 || count == 0) return 0;
        const auto available = (bytes_.size() - cursor_) / size;
        const auto readCount = std::min(count, available);
        if(readCount) std::memcpy(destination, bytes_.data() + cursor_, size * readCount);
        cursor_ += size * readCount;
        return readCount;
    }
    std::size_t Write(const void*, std::size_t, std::size_t) override { return 0; }
    aiReturn Seek(std::size_t offset, aiOrigin origin) override {
        std::size_t next = 0;
        if(origin == aiOrigin_SET) next = offset;
        else if(origin == aiOrigin_CUR) {
            if(offset > bytes_.size() - cursor_) return aiReturn_FAILURE;
            next = cursor_ + offset;
        } else if(origin == aiOrigin_END) {
            if(offset > bytes_.size()) return aiReturn_FAILURE;
            next = bytes_.size() - offset;
        } else return aiReturn_FAILURE;
        if(next > bytes_.size()) return aiReturn_FAILURE;
        cursor_ = next;
        return aiReturn_SUCCESS;
    }
    std::size_t Tell() const override { return cursor_; }
    std::size_t FileSize() const override { return bytes_.size(); }
    void Flush() override {}
private:
    std::vector<std::uint8_t> bytes_;
    std::size_t cursor_ = 0;
};

class NativeIO final : public Assimp::IOSystem {
public:
    explicit NativeIO(std::filesystem::path directory) : directory_(std::move(directory)) {}
    bool Exists(const char* name) const override {
        std::error_code error;
        return std::filesystem::is_regular_file(Resolve(name), error);
    }
    char getOsSeparator() const override { return '/'; }
    Assimp::IOStream* Open(const char* name, const char* mode) override {
        if(!name || !mode || std::strchr(mode, 'w') || std::strchr(mode, '+')) return nullptr;
        try { return new FileStream(ReadBytes(Resolve(name))); }
        catch(const std::exception&) { return nullptr; }
    }
    void Close(Assimp::IOStream* stream) override { delete stream; }
    bool ComparePaths(const char* a, const char* b) const override {
        std::error_code error;
        const bool same = std::filesystem::equivalent(Resolve(a), Resolve(b), error);
        return !error ? same : Resolve(a).lexically_normal() == Resolve(b).lexically_normal();
    }
private:
    std::filesystem::path Resolve(const char* name) const {
        if(!name) return {};
        auto path = FromUtf8(name);
        if(path.is_absolute()) return path;
        std::error_code error;
        if(std::filesystem::is_regular_file(directory_ / path, error)) return directory_ / path;
        if(std::filesystem::is_regular_file(path, error)) return path;
        return directory_ / path;
    }
    std::filesystem::path directory_;
};

bool Finite(const glm::dvec3& point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

glm::dmat4 Matrix(const aiMatrix4x4& matrix) {
    // aiMatrix is row-major; GLM indexes columns first. Convert before doing
    // hierarchy multiplication so large accumulated translations stay double.
    glm::dmat4 value(1);
    for(int row = 0; row < 4; ++row)
        for(int column = 0; column < 4; ++column) {
            const double element = static_cast<double>(matrix[row][column]);
            if(!std::isfinite(element)) throw std::runtime_error("Model contains a non-finite node transform");
            value[column][row] = element;
        }
    return value;
}

glm::vec3 SafeNormal(glm::vec3 value, const glm::vec3& fallback) {
    const float length = glm::dot(value, value);
    return std::isfinite(length) && length > 1e-20f ? value / std::sqrt(length) : fallback;
}

float Saturate(float value, float fallback) {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : fallback;
}

float SrgbToLinear(float value) {
    value = std::max(0.0f, value);
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

// Assimp versions differ in whether the material scalar C API exposes float
// or ai_real. Deduce its output type rather than selecting a template Get<T>
// overload which only accepts raw aiPTI_Buffer values on some double builds.
template<class Real>
aiReturn ReadScalarTyped(const aiMaterial& material, const char* key, unsigned type,
    unsigned index, float& output,
    aiReturn (*reader)(const aiMaterial*, const char*, unsigned, unsigned, Real*, unsigned*)) {
    Real value = static_cast<Real>(output);
    const auto result = reader(&material, key, type, index, &value, nullptr);
    if(result == aiReturn_SUCCESS) output = static_cast<float>(value);
    return result;
}

aiReturn ReadScalar(const aiMaterial& material, const char* key, unsigned type,
    unsigned index, float& output) {
    return ReadScalarTyped(material, key, type, index, output, &aiGetMaterialFloatArray);
}

void RebuildTangentsFromFinalUVs(StaticMesh& mesh) {
    std::vector<glm::dvec3> tangents(mesh.vertices.size(), glm::dvec3(0));
    std::vector<glm::dvec3> bitangents(mesh.vertices.size(), glm::dvec3(0));
    for(std::size_t face = 0; face < mesh.indices.size(); face += 3) {
        const auto a = mesh.indices[face], b = mesh.indices[face + 1], c = mesh.indices[face + 2];
        const auto edge1 = mesh.vertices[b].position - mesh.vertices[a].position;
        const auto edge2 = mesh.vertices[c].position - mesh.vertices[a].position;
        const auto uv1 = glm::dvec2(mesh.vertices[b].uv) - glm::dvec2(mesh.vertices[a].uv);
        const auto uv2 = glm::dvec2(mesh.vertices[c].uv) - glm::dvec2(mesh.vertices[a].uv);
        const double determinant = uv1.x * uv2.y - uv1.y * uv2.x;
        if(!std::isfinite(determinant) || std::abs(determinant) < 1e-20) continue;
        const auto cross = glm::cross(edge1, edge2);
        const double area = glm::length(cross);
        if(!std::isfinite(area) || area < 1e-20) continue;
        const auto tangent = (edge1 * uv2.y - edge2 * uv1.y) / determinant;
        const auto bitangent = (edge2 * uv1.x - edge1 * uv2.x) / determinant;
        if(!Finite(tangent) || !Finite(bitangent)) continue;
        for(const auto index : {a, b, c}) {
            tangents[index] += tangent * area;
            bitangents[index] += bitangent * area;
        }
    }
    for(std::size_t index = 0; index < mesh.vertices.size(); ++index) {
        auto& vertex = mesh.vertices[index];
        const auto normal = glm::dvec3(vertex.normal);
        const auto orthogonal = tangents[index] - normal * glm::dot(normal, tangents[index]);
        const double squaredLength = glm::dot(orthogonal, orthogonal);
        if(!Finite(orthogonal) || !std::isfinite(squaredLength) || squaredLength < 1e-30) continue;
        const auto tangent = orthogonal / std::sqrt(squaredLength);
        const float sign = glm::dot(glm::cross(normal, tangent), bitangents[index]) < 0 ? -1.0f : 1.0f;
        vertex.tangent = glm::vec4(glm::vec3(tangent), sign);
    }
}

struct TextureReference {
    std::string name;
    aiTextureType type = aiTextureType_NONE;
    aiUVTransform transform;
    bool valid = false;
};

class ImportContext {
public:
    ImportContext(StaticModel& model, const aiScene& scene, const LoadOptions& options)
        : model_(model), scene_(scene), options_(options) {}

    void Warning(std::string message) { model_.warnings.push_back(std::move(message)); }
    void TextureIssue(const std::string& message) {
        if(options_.strictTextures) throw std::runtime_error(message);
        Warning(message);
    }

    void FinishMaterialTextures() {
        if(!usedMaxPhysicalCutout_)return;
        // Only after every material has been converted can a decoded source
        // image be identified as unused: another material may still reference
        // it directly while this one uses a derived opacity image. Move image
        // buffers in place, retaining old relative order and all eight slots.
        const auto oldCount=model_.textures.size();std::vector<bool> used(oldCount,false);
        for(const auto& material:model_.materials)for(const auto index:material.textures)if(index>=0) {
            if(static_cast<std::size_t>(index)>=oldCount)throw std::runtime_error("Invalid texture reference during FBX material compaction");
            used[index]=true;
        }
        std::vector<std::int32_t> remap(oldCount,-1);std::size_t write=0;
        for(std::size_t read=0;read<oldCount;++read)if(used[read]) {
            remap[read]=static_cast<std::int32_t>(write);
            if(write!=read)model_.textures[write]=std::move(model_.textures[read]);++write;
        }
        model_.textures.resize(write);
        for(auto& material:model_.materials)for(auto& index:material.textures)if(index>=0)index=remap[index];
        if(write<oldCount)Warning("Max Physical FBX retained "+std::to_string(write)+" runtime textures; released "+
            std::to_string(oldCount-write)+" unreferenced decoded source/derived images after all material slots were resolved.");
    }

    TextureReference FindTexture(const aiMaterial& material, std::initializer_list<aiTextureType> types) {
        for(const auto type : types) {
            if(!material.GetTextureCount(type)) continue;
            aiString path;
            aiTextureMapping mapping = aiTextureMapping_UV;
            unsigned uv = 0;
            if(material.GetTexture(type, 0, &path, &mapping, &uv) != aiReturn_SUCCESS) continue;
            if(mapping != aiTextureMapping_UV || uv != 0) {
                TextureIssue("Only UV channel 0 is supported; skipped texture: " + std::string(path.C_Str()));
                continue;
            }
            if(material.GetTextureCount(type) > 1)
                TextureIssue("Only the first texture layer is supported: " + std::string(path.C_Str()));
            TextureReference result;
            result.name = path.C_Str();
            result.type = type;
            result.valid = !result.name.empty();
            material.Get(AI_MATKEY_UVTRANSFORM(type, 0), result.transform);
            if(std::abs(result.transform.mRotation) > 1e-6)
                TextureIssue("Rotated texture UV transforms are unsupported: " + result.name);
            return result;
        }
        return {};
    }

    TextureReference FindMaxPhysicalCutout(const aiMaterial& material) {
        // Assimp retains every FBX texture connection in $raw even when it has
        // no standard semantic mapping. Physical Material cutout_map is absent
        // from its standard OPACITY table. Keep this exact FBX-only fallback
        // separate from glTF's UNKNOWN packed metallic/roughness texture.
        if(Lower(Utf8(model_.sourcePath.extension()))!=".fbx")return {};
        constexpr const char* prefix="$raw.3dsMax|Parameters|cutout_map";
        aiString path;
        if(material.Get((std::string(prefix)+"|file").c_str(),aiTextureType_UNKNOWN,0,path)!=aiReturn_SUCCESS||!path.length)return {};
        const aiMaterialProperty* enabledProperty=nullptr;
        if(aiGetMaterialProperty(&material,"$raw.3dsMax|Parameters|cutout_map_on",0,0,&enabledProperty)==aiReturn_SUCCESS) {
            int enabled=-1;
            if(!enabledProperty||enabledProperty->mType!=aiPTI_Integer||
                material.Get("$raw.3dsMax|Parameters|cutout_map_on",0,0,enabled)!=aiReturn_SUCCESS||
                (enabled!=0&&enabled!=1)) {
                Warning("Max Physical cutout_map has an unsupported enable flag; skipped its opacity connection: "+std::string(path.C_Str()));return {};
            }
            if(!enabled)return {};
        }else Warning("Max Physical cutout_map has no exported enable flag; using its connected texture: "+std::string(path.C_Str()));
        int uv=0;
        if(material.Get((std::string(prefix)+"|uvwsrc").c_str(),aiTextureType_UNKNOWN,0,uv)==aiReturn_SUCCESS&&uv!=0) {
            TextureIssue("Only UV channel 0 is supported; skipped Max Physical cutout texture: "+std::string(path.C_Str()));return {};
        }
        TextureReference result;result.name=path.C_Str();result.type=aiTextureType_OPACITY;result.valid=true;
        material.Get((std::string(prefix)+"|uvtrafo").c_str(),aiTextureType_UNKNOWN,0,result.transform);
        // Assimp FlipUVs adjusts only its standard $tex.uvtrafo keys. Apply
        // the identical change to this raw connection before comparing maps.
        if(options_.flipUVs){result.transform.mTranslation.y*=-1;result.transform.mRotation*=-1;}
        if(std::abs(result.transform.mRotation)>1e-6)
            TextureIssue("Rotated texture UV transforms are unsupported: "+result.name);
        Warning("Imported Max Physical cutout_map as opacity (white opaque, black transparent). Map amount/channel/output modes were not inferred; inspect the source material: "+result.name);
        usedMaxPhysicalCutout_=true;
        return result;
    }

    void MaxPhysicalScalarFactors(const aiMaterial& source,bool hasMetalMap,bool hasRoughMap,PbrMaterial& material) {
        if(Lower(Utf8(model_.sourcePath.extension()))!=".fbx")return;
        aiString signature;
        if(source.Get("$raw.3dsMax|ORIGINAL_MTL",0,0,signature)!=aiReturn_SUCCESS||
            std::string(signature.C_Str())!="PHYSICAL_MTL")return;
        // The FBX legacy Phong exponent supplies an approximate roughness even
        // when the authored Physical value survives in raw metadata. Prefer
        // that explicit scalar only for the exact source material signature.
        if(!hasMetalMap) {
            float metallic=0;
            if(ReadScalar(source,"$raw.3dsMax|Parameters|metalness",0,0,metallic)==aiReturn_SUCCESS) {
                if(std::isfinite(metallic)&&metallic>=0&&metallic<=1)material.metallic=metallic;
                else Warning("Invalid Max Physical metalness scalar; retained standard material factor: "+material.name);
            }
        }
        float roughness=0;
        if(ReadScalar(source,"$raw.3dsMax|Parameters|roughness",0,0,roughness)!=aiReturn_SUCCESS)return;
        if(hasRoughMap) {
            Warning("Max Physical roughness texture retains standard mapping; raw scalar/glossiness inversion was not inferred for this map: "+material.name);return;
        }
        const aiMaterialProperty* inversionProperty=nullptr;int inversion=-1;
        if(!std::isfinite(roughness)||roughness<0||roughness>1||
            aiGetMaterialProperty(&source,"$raw.3dsMax|Parameters|roughness_inv",0,0,&inversionProperty)!=aiReturn_SUCCESS||
            !inversionProperty||inversionProperty->mType!=aiPTI_Integer||
            source.Get("$raw.3dsMax|Parameters|roughness_inv",0,0,inversion)!=aiReturn_SUCCESS||
            (inversion!=0&&inversion!=1)) {
            Warning("Invalid/missing Max Physical roughness or roughness_inv flag; retained standard material factor: "+material.name);return;
        }
        material.roughness=inversion?1-roughness:roughness;
    }

    std::int32_t Image(const TextureReference& reference) {
        if(!reference.valid) return -1;
        const auto found = sourceCache_.find(reference.name);
        if(found != sourceCache_.end()) return found->second;
        try {
            TextureImage image;
            image.name = reference.name;
            if(const auto* embedded = scene_.GetEmbeddedTexture(reference.name.c_str())) {
                if(!embedded->pcData) throw std::runtime_error("Embedded texture has no pixel data");
                if(embedded->mHeight == 0) {
                    Decode(image, reinterpret_cast<const std::uint8_t*>(embedded->pcData), embedded->mWidth);
                } else {
                    image.width = embedded->mWidth;
                    image.height = embedded->mHeight;
                    const auto size = PixelBytes(image.width, image.height);
                    image.rgba.resize(size);
                    for(std::size_t pixel = 0; pixel < size / 4; ++pixel) {
                        const auto texel = embedded->pcData[pixel];
                        image.rgba[pixel * 4] = texel.r;
                        image.rgba[pixel * 4 + 1] = texel.g;
                        image.rgba[pixel * 4 + 2] = texel.b;
                        image.rgba[pixel * 4 + 3] = texel.a;
                        image.hasAlpha |= texel.a < 255;
                    }
                }
            } else {
                const auto path = ResolveTexture(reference.name);
                const auto bytes = ReadBytes(path);
                image.name = Utf8(path);
                Decode(image, bytes.data(), bytes.size());
            }
            const auto index = Append(std::move(image));
            sourceCache_[reference.name] = index;
            return index;
        } catch(const std::exception& exception) {
            TextureIssue("Texture '" + reference.name + "': " + exception.what());
            sourceCache_[reference.name] = -1;
            return -1;
        }
    }

    std::int32_t Scalar(std::int32_t source, int channel, float factor, float offset = 0) {
        if(source < 0) return -1;
        const std::string key = std::to_string(source) + ":scalar:" + std::to_string(channel) + ":" + std::to_string(factor) + ":" + std::to_string(offset);
        const auto found = derivedCache_.find(key);
        if(found != derivedCache_.end()) return found->second;
        const auto& input = model_.textures[source];
        TextureImage image;
        image.name = input.name + " [channel " + std::to_string(channel) + ", factor " + std::to_string(factor) + "]";
        image.width = input.width;
        image.height = input.height;
        image.linearColor = true;
        image.rgba32f.resize(PixelBytes(image.width, image.height));
        for(std::size_t pixel = 0; pixel < image.rgba32f.size() / 4; ++pixel) {
            const float sample = input.rgba.empty() ? input.rgba32f[pixel * 4 + channel]
                : static_cast<float>(input.rgba[pixel * 4 + channel]) / 255.0f;
            const float value = Saturate(sample * factor + offset, 0);
            image.rgba32f[pixel * 4] = image.rgba32f[pixel * 4 + 1] = image.rgba32f[pixel * 4 + 2] = value;
            image.rgba32f[pixel * 4 + 3] = 1;
        }
        const auto index = Append(std::move(image));
        derivedCache_[key] = index;
        return index;
    }

    std::int32_t Emission(std::int32_t source) {
        if(source < 0) return -1;
        const std::string key = std::to_string(source) + ":linear-emission";
        const auto found = derivedCache_.find(key);
        if(found != derivedCache_.end()) return found->second;
        const auto& input = model_.textures[source];
        // HDR pixels were decoded as linear by stb; all other emission maps
        // are color textures. The PBR shader expects linear emission samples.
        if(input.linearColor) return source;
        TextureImage image;
        image.name = input.name + " [linear emission]";
        image.width = input.width;
        image.height = input.height;
        image.linearColor = true;
        image.rgba32f.resize(PixelBytes(image.width, image.height));
        for(std::size_t pixel = 0; pixel < image.rgba32f.size() / 4; ++pixel) {
            for(int channel = 0; channel < 3; ++channel) {
                const float sample = input.rgba.empty() ? input.rgba32f[pixel * 4 + channel]
                    : static_cast<float>(input.rgba[pixel * 4 + channel]) / 255.0f;
                image.rgba32f[pixel * 4 + channel] = SrgbToLinear(sample);
            }
            image.rgba32f[pixel * 4 + 3] = 1;
        }
        const auto index = Append(std::move(image));
        derivedCache_[key] = index;
        return index;
    }

    std::int32_t Opacity(std::int32_t base, std::int32_t opacity) {
        if(opacity < 0) return base;
        const std::string key = std::to_string(base) + ":opacity:" + std::to_string(opacity);
        const auto found = derivedCache_.find(key);
        if(found != derivedCache_.end()) return found->second;
        const auto& mask = model_.textures[opacity];
        const auto* color = base < 0 ? nullptr : &model_.textures[base];
        TextureImage image;
        image.name = (color ? color->name : "white") + " [opacity " + mask.name + "]";
        image.width = color ? color->width : mask.width;
        image.height = color ? color->height : mask.height;
        image.linearColor = color && color->linearColor;
        // JPEG/opaque RGBA8 albedo + an 8-bit mask is exactly representable in
        // RGBA8: RGB is unchanged and 1 * mask.red becomes that very same byte.
        // Decide before allocating; partial alpha products and float/16-bit/HDR
        // sources retain the existing float path without any quantization.
        bool exactBytes=!mask.rgba.empty()&&mask.rgba32f.empty()&&
            (!color||(!color->rgba.empty()&&color->rgba32f.empty()));
        if(exactBytes&&color)for(std::size_t pixel=0;pixel<color->rgba.size()/4;++pixel)
            if(color->rgba[pixel*4+3]!=255){exactBytes=false;break;}
        if(exactBytes) {
            image.rgba.resize(PixelBytes(image.width,image.height));
            for(std::uint32_t y=0;y<image.height;++y)for(std::uint32_t x=0;x<image.width;++x) {
                const auto pixel=static_cast<std::size_t>(y)*image.width+x;
                for(unsigned channel=0;channel<3;++channel)image.rgba[pixel*4+channel]=color?color->rgba[pixel*4+channel]:255;
                const auto mx=static_cast<std::uint32_t>(static_cast<std::uint64_t>(x)*mask.width/image.width);
                const auto my=static_cast<std::uint32_t>(static_cast<std::uint64_t>(y)*mask.height/image.height);
                image.rgba[pixel*4+3]=mask.rgba[(static_cast<std::size_t>(my)*mask.width+mx)*4];
                image.hasAlpha|=image.rgba[pixel*4+3]!=255;
            }
            const auto index=Append(std::move(image));derivedCache_[key]=index;return index;
        }
        image.rgba32f.resize(PixelBytes(image.width, image.height));
        for(std::uint32_t y = 0; y < image.height; ++y)
            for(std::uint32_t x = 0; x < image.width; ++x) {
                const std::size_t pixel = static_cast<std::size_t>(y) * image.width + x;
                for(int channel = 0; channel < 4; ++channel)
                    image.rgba32f[pixel * 4 + channel] = color ? Sample(*color, pixel, channel) : 1;
                const auto mx = static_cast<std::uint32_t>(static_cast<std::uint64_t>(x) * mask.width / image.width);
                const auto my = static_cast<std::uint32_t>(static_cast<std::uint64_t>(y) * mask.height / image.height);
                image.rgba32f[pixel * 4 + 3] *= Sample(mask, static_cast<std::size_t>(my) * mask.width + mx, 0);
                image.hasAlpha |= image.rgba32f[pixel * 4 + 3] < 1;
            }
        const auto index = Append(std::move(image));
        derivedCache_[key] = index;
        return index;
    }

    PbrMaterial Material(const aiMaterial& source, std::uint32_t index) {
        PbrMaterial material;
        aiString name;
        source.Get(AI_MATKEY_NAME, name);
        material.name = name.length ? name.C_Str() : "Material_" + std::to_string(index);
        aiColor4D base(1, 1, 1, 1);
        const bool hasPbrColor = source.Get(AI_MATKEY_BASE_COLOR, base) == aiReturn_SUCCESS;
        if(!hasPbrColor)
            source.Get(AI_MATKEY_COLOR_DIFFUSE, base);
        material.baseColor = {Saturate(base.r, 1), Saturate(base.g, 1), Saturate(base.b, 1), Saturate(base.a, 1)};
        float opacity = 1;
        // glTF importers duplicate baseColor.a into opacity; multiplying both
        // would incorrectly square the source alpha factor.
        if(!hasPbrColor && ReadScalar(source, AI_MATKEY_OPACITY, opacity) == aiReturn_SUCCESS)
            material.baseColor.a *= Saturate(opacity, 1);
        aiColor3D emission(0, 0, 0);
        const bool hasEmissionFactor = source.Get(AI_MATKEY_COLOR_EMISSIVE, emission) == aiReturn_SUCCESS;
        material.emissive = glm::vec3(std::isfinite(emission.r) ? std::max(0.0f, static_cast<float>(emission.r)) : 0,
            std::isfinite(emission.g) ? std::max(0.0f, static_cast<float>(emission.g)) : 0,
            std::isfinite(emission.b) ? std::max(0.0f, static_cast<float>(emission.b)) : 0);
        float intensity = 1;
        if(ReadScalar(source, AI_MATKEY_EMISSIVE_INTENSITY, intensity) == aiReturn_SUCCESS && std::isfinite(intensity))
            material.emissive *= std::max(0.0f, intensity);
        int twoSided = 0;
        source.Get(AI_MATKEY_TWOSIDED, twoSided);
        material.twoSided = twoSided != 0;

        const auto albedo = FindTexture(source, {aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE});
        const auto normal = FindTexture(source, {aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA});
        const auto metal = FindTexture(source, {aiTextureType_METALNESS});
        const auto rough = FindTexture(source, {aiTextureType_DIFFUSE_ROUGHNESS});
        const auto ao = FindTexture(source, {aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP});
        const auto emissionMap = FindTexture(source, {aiTextureType_EMISSION_COLOR, aiTextureType_EMISSIVE});
        auto opacityMap = FindTexture(source, {aiTextureType_OPACITY});
        bool maxPhysicalCutout=false;
        if(!opacityMap.valid) {
            opacityMap=FindMaxPhysicalCutout(source);
            maxPhysicalCutout=opacityMap.valid;
        }
        const auto combined = FindTexture(source, {aiTextureType_UNKNOWN});

        material.metallic = metal.valid ? 1.0f : 0.0f;
        ReadScalar(source, AI_MATKEY_METALLIC_FACTOR, material.metallic);
        material.metallic = Saturate(material.metallic, 0);
        material.roughness = rough.valid ? 1.0f : options_.defaultRoughness;
        if(ReadScalar(source, AI_MATKEY_ROUGHNESS_FACTOR, material.roughness) != aiReturn_SUCCESS && !rough.valid) {
            float shininess = 0;
            if(ReadScalar(source, AI_MATKEY_SHININESS, shininess) == aiReturn_SUCCESS && std::isfinite(shininess) && shininess > 0)
                material.roughness = std::sqrt(2.0f / (shininess + 2.0f));
        }
        MaxPhysicalScalarFactors(source,metal.valid,rough.valid,material);
        material.roughness = std::clamp(Saturate(material.roughness, 0.6f), 0.045f, 1.0f);
        float normalScale = 1;
        if(normal.valid && ReadScalar(source, AI_MATKEY_GLTF_TEXTURE_SCALE(normal.type, 0), normalScale) != aiReturn_SUCCESS)
            ReadScalar(source, "$tex.file.scale", normal.type, 0, normalScale);
        material.normalScale = std::isfinite(normalScale) ? std::clamp(normalScale, 0.0f, 4.0f) : 1;
        float aoStrength = 1;
        if(ao.valid && ReadScalar(source, AI_MATKEY_GLTF_TEXTURE_STRENGTH(ao.type, 0), aoStrength) != aiReturn_SUCCESS)
            ReadScalar(source, "$tex.file.strength", ao.type, 0, aoStrength);
        aoStrength = Saturate(aoStrength, 1);

        const auto* uvReference = albedo.valid ? &albedo : normal.valid ? &normal : metal.valid ? &metal : rough.valid ? &rough : ao.valid ? &ao : emissionMap.valid ? &emissionMap : opacityMap.valid ? &opacityMap : nullptr;
        if(uvReference) {
            const auto& uv = uvReference->transform;
            material.uvTransform = {static_cast<float>(uv.mScaling.x), static_cast<float>(uv.mScaling.y),
                static_cast<float>(uv.mTranslation.x), static_cast<float>(uv.mTranslation.y)};
            for(const auto* map : std::array<const TextureReference*,7>{&albedo, &normal, &metal, &rough, &ao, &emissionMap, &opacityMap})
                if(map->valid && (std::abs(map->transform.mScaling.x - uv.mScaling.x) > 1e-5 ||
                    std::abs(map->transform.mScaling.y - uv.mScaling.y) > 1e-5 ||
                    std::abs(map->transform.mTranslation.x - uv.mTranslation.x) > 1e-5 ||
                    std::abs(map->transform.mTranslation.y - uv.mTranslation.y) > 1e-5))
                    TextureIssue("Material has different per-texture UV transforms; using the primary transform: " + material.name);
        }

        material.textures[0] = Opacity(Image(albedo), Image(opacityMap));
        material.textures[1] = Image(normal);
        // glTF's packed metallic/roughness map is B/G. Matching source paths
        // indicate packed data; dedicated scalar maps use red. UNKNOWN alone
        // is only treated as packed for a glTF asset, never arbitrary FBX maps.
        const bool isGltf = Lower(Utf8(model_.sourcePath.extension())) == ".gltf" || Lower(Utf8(model_.sourcePath.extension())) == ".glb";
        const bool packed = metal.valid && rough.valid && metal.name == rough.name;
        if(metal.valid) material.textures[2] = Scalar(Image(metal), packed || isGltf ? 2 : 0, material.metallic);
        else if(isGltf && combined.valid) material.textures[2] = Scalar(Image(combined), 2, material.metallic);
        if(rough.valid) material.textures[3] = Scalar(Image(rough), packed || isGltf ? 1 : 0, material.roughness);
        else if(isGltf && combined.valid) material.textures[3] = Scalar(Image(combined), 1, material.roughness);
        // AO strength affects the map only. Missing textures retain neutral AO.
        material.textures[4] = Scalar(Image(ao), 0, aoStrength, 1 - aoStrength);
        material.textures[7] = Emission(Image(emissionMap));
        if(emissionMap.valid && !hasEmissionFactor)
            material.emissive = glm::vec3(1);

        aiString alphaMode;
        const bool explicitAlpha = source.Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == aiReturn_SUCCESS;
        if(explicitAlpha) {
            const auto mode = std::string(alphaMode.C_Str());
            if(mode == "MASK") material.alphaMode = AlphaMode::Mask;
            else if(mode == "BLEND") material.alphaMode = AlphaMode::Blend;
        } else if(material.baseColor.a < 0.999f) {
            material.alphaMode = AlphaMode::Blend;
        } else if(material.textures[0] >= 0 && model_.textures[material.textures[0]].hasAlpha) {
            // Autodesk cutout grayscale denotes continuous opacity. Threshold
            // testing sparse hair/lace loses all coverage in minified mipmaps.
            // Preserve it through the existing blend pipeline only for this
            // exact Max Physical fallback; standard/glTF policies stay intact.
            material.alphaMode=maxPhysicalCutout?AlphaMode::Blend:AlphaMode::Mask;
        }
        ReadScalar(source, AI_MATKEY_GLTF_ALPHACUTOFF, material.alphaCutoff);
        material.alphaCutoff = Saturate(material.alphaCutoff, 0.5f);
        return material;
    }

private:
    static std::size_t PixelBytes(std::uint32_t width, std::uint32_t height) {
        if(width == 0 || height == 0 || static_cast<std::uint64_t>(width) * height > std::numeric_limits<std::size_t>::max() / (4 * sizeof(float)))
            throw std::runtime_error("Invalid or excessive texture dimensions");
        return static_cast<std::size_t>(width) * height * 4;
    }
    static float Sample(const TextureImage& image, std::size_t pixel, int channel) {
        return image.rgba.empty() ? image.rgba32f[pixel * 4 + channel]
            : static_cast<float>(image.rgba[pixel * 4 + channel]) / 255.0f;
    }
    std::int32_t Append(TextureImage image) {
        if(model_.textures.size() >= static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
            throw std::runtime_error("Too many model textures");
        const auto index = static_cast<std::int32_t>(model_.textures.size());
        model_.textures.push_back(std::move(image));
        return index;
    }
    void Decode(TextureImage& image, const std::uint8_t* bytes, std::size_t length) {
        if(length == 0 || length > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            throw std::runtime_error("Empty texture or encoded image larger than stb's 2 GB input limit");
        int width = 0, height = 0, channels = 0;
        const int size = static_cast<int>(length);
        if(!stbi_info_from_memory(bytes, size, &width, &height, &channels) || width <= 0 || height <= 0)
            throw std::runtime_error(std::string("Image decode failed: ") + (stbi_failure_reason() ? stbi_failure_reason() : "unsupported image"));
        image.width = static_cast<std::uint32_t>(width);
        image.height = static_cast<std::uint32_t>(height);
        const auto pixelBytes = PixelBytes(image.width, image.height);
        // Do not modify stb's global flip setting: import is safe alongside
        // existing image loaders. UV conversion happens in Assimp instead.
        if(stbi_is_hdr_from_memory(bytes, size)) {
            std::unique_ptr<float, decltype(&stbi_image_free)> pixels(stbi_loadf_from_memory(bytes, size, &width, &height, &channels, 4), stbi_image_free);
            if(!pixels) throw std::runtime_error("Cannot decode HDR texture");
            image.rgba32f.assign(pixels.get(), pixels.get() + pixelBytes);
            image.linearColor = true;
        } else if(stbi_is_16_bit_from_memory(bytes, size)) {
            std::unique_ptr<stbi_us, decltype(&stbi_image_free)> pixels(stbi_load_16_from_memory(bytes, size, &width, &height, &channels, 4), stbi_image_free);
            if(!pixels) throw std::runtime_error("Cannot decode 16-bit texture");
            image.rgba32f.resize(pixelBytes);
            std::transform(pixels.get(), pixels.get() + pixelBytes, image.rgba32f.begin(), [](stbi_us sample) { return static_cast<float>(sample) / 65535.0f; });
        } else {
            std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(stbi_load_from_memory(bytes, size, &width, &height, &channels, 4), stbi_image_free);
            if(!pixels) throw std::runtime_error("Cannot decode texture");
            image.rgba.assign(pixels.get(), pixels.get() + pixelBytes);
        }
        for(std::size_t pixel = 0; pixel < pixelBytes / 4; ++pixel)
            image.hasAlpha |= Sample(image, pixel, 3) < 0.99999f;
    }
    std::filesystem::path ResolveTexture(const std::string& name) const {
        const auto relative = FromUtf8(name);
        const auto directory = model_.sourcePath.parent_path();
        std::vector<std::filesystem::path> paths;
        if(relative.is_absolute()) paths.push_back(relative);
        paths.push_back(directory / relative);
        paths.push_back(directory / relative.filename());
        for(const auto& search : options_.textureSearchPaths) {
            const auto root = search.is_absolute() ? search : directory / search;
            paths.push_back(root / relative);
            paths.push_back(root / relative.filename());
        }
        for(const auto& path : paths) {
            std::error_code error;
            if(std::filesystem::is_regular_file(path, error)) return path.lexically_normal();
        }
        throw std::runtime_error("File not found (model directory and textureSearchPaths searched)");
    }
    StaticModel& model_;
    const aiScene& scene_;
    const LoadOptions& options_;
    std::unordered_map<std::string, std::int32_t> sourceCache_, derivedCache_;
    bool usedMaxPhysicalCutout_=false;
};

std::filesystem::path SelectModel(const std::filesystem::path& input) {
    std::error_code error;
    auto path = std::filesystem::absolute(input, error);
    if(error) throw std::runtime_error("Cannot resolve model path: " + Utf8(input));
    if(std::filesystem::is_directory(path)) {
        std::vector<std::filesystem::path> models;
        for(const auto& entry : std::filesystem::directory_iterator(path))
            if(entry.is_regular_file() && StaticModelLoader::SupportsExtension(Utf8(entry.path().extension())))
                models.push_back(entry.path());
        std::sort(models.begin(), models.end());
        if(models.empty()) throw std::runtime_error("Directory has no supported model: " + Utf8(path));
        if(models.size() != 1) {
            std::string message = "Directory contains multiple models; select a file explicitly:";
            for(const auto& model : models) message += " " + Utf8(model.filename());
            throw std::runtime_error(message);
        }
        path = models.front();
    }
    if(!std::filesystem::is_regular_file(path)) throw std::runtime_error("Model file not found: " + Utf8(path));
    if(!StaticModelLoader::SupportsExtension(Utf8(path.extension())))
        throw std::runtime_error("Unsupported model extension: " + Utf8(path.extension()));
    return path.lexically_normal();
}

} // namespace

void Bounds::Include(const glm::dvec3& point) {
    if(!Finite(point)) return;
    if(!valid) { minimum = maximum = point; valid = true; }
    else { minimum = glm::min(minimum, point); maximum = glm::max(maximum, point); }
}

std::uint64_t StaticModel::VertexCount() const {
    std::uint64_t count = 0;
    for(const auto& mesh : meshes) count += mesh.vertices.size();
    return count;
}

std::uint64_t StaticModel::TriangleCount() const {
    std::uint64_t count = 0;
    for(const auto& mesh : meshes) count += mesh.indices.size() / 3;
    return count;
}

bool StaticModelLoader::SupportsExtension(std::string extension) {
    extension = Lower(std::move(extension));
    if(extension.starts_with("*.")) extension.erase(0, 1);
    else if(!extension.empty() && extension.front() != '.') extension.insert(extension.begin(), '.');
    return extension == ".blend" || (!extension.empty() && aiIsExtensionSupported(extension.c_str()) != AI_FALSE);
}

std::string StaticModelLoader::SupportedExtensions() {
    aiString extensions;
    aiGetExtensionList(&extensions);
    return extensions.C_Str();
}

std::shared_ptr<const StaticModel> StaticModelLoader::Load(const std::filesystem::path& input,
    const LoadOptions& options, std::string* error) {
    if(error) error->clear();
    try {
        if(!std::isfinite(options.defaultRoughness) || options.defaultRoughness < 0 || options.defaultRoughness > 1)
            throw std::runtime_error("defaultRoughness must be finite and between 0 and 1");
#ifdef ASSIMP_DOUBLE_PRECISION
        constexpr bool expectsDouble = true;
#else
        constexpr bool expectsDouble = false;
#endif
        if(((aiGetCompileFlags() & ASSIMP_CFLAGS_DOUBLE_SUPPORT) != 0) != expectsDouble)
            throw std::runtime_error("Assimp header/library precision ABI mismatch; use matching headers and compile definitions");
        auto result = std::make_shared<StaticModel>();
        result->sourcePath = SelectModel(input);
        const auto extension = Lower(Utf8(result->sourcePath.extension()));
        if(extension == ".blend") {
            BlendImportOptions bridgeOptions;
            bridgeOptions.blenderExecutable = options.blenderExecutable;
            bridgeOptions.cacheDirectory = options.blendCacheDirectory;
            bridgeOptions.bakeStaticPose = true;
            std::string bridgeError;
            const auto prepared = PrepareBlendSource(result->sourcePath,bridgeOptions,&bridgeError);
            if(!prepared) throw std::runtime_error(bridgeError);
            const auto converted = Load(prepared->glbPath,options,&bridgeError);
            if(!converted) throw std::runtime_error(bridgeError);
            result = std::make_shared<StaticModel>(*converted);
            result->sourcePath = prepared->sourcePath;
            result->warnings.insert(result->warnings.end(),prepared->warnings.begin(),prepared->warnings.end());
            return result;
        }
        Assimp::Importer importer;
        importer.SetIOHandler(new NativeIO(result->sourcePath.parent_path()));
        importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_POINT | aiPrimitiveType_LINE);
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, true);
        // Retain the whole mesh and source units: no mesh optimization,
        // simplification, vertex compression, pretransform, or index narrowing.
        unsigned flags = aiProcess_Triangulate | aiProcess_GenSmoothNormals |
            aiProcess_CalcTangentSpace | aiProcess_SortByPType | aiProcess_ValidateDataStructure;
        // MMD/PMX imports convert native V to 1-V internally. Restore PMX's
        // original image-row UV convention; other formats follow the option.
        if(extension == ".pmx" || options.flipUVs) flags |= aiProcess_FlipUVs;
        const auto* scene = importer.ReadFile(Utf8(result->sourcePath), flags);
        if(!scene || !scene->mRootNode || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)
            throw std::runtime_error("Assimp could not load model: " + std::string(importer.GetErrorString()));
        ImportContext context(*result, *scene, options);
        if(!expectsDouble)
            context.Warning("Assimp uses float source data; CPU positions/node transforms are retained as double without quantization.");
        for(std::uint32_t index = 0; index < scene->mNumMaterials; ++index)
            result->materials.push_back(context.Material(*scene->mMaterials[index], index));
        if(result->materials.empty()) result->materials.push_back(PbrMaterial{});
        context.FinishMaterialTextures();
        std::vector<std::uint32_t> meshMap(scene->mNumMeshes, std::numeric_limits<std::uint32_t>::max());
        bool hasBones = false, hasMorphs = false;
        for(std::uint32_t index = 0; index < scene->mNumMeshes; ++index) {
            const auto& source = *scene->mMeshes[index];
            if(!source.mNumVertices || !source.mNumFaces || !source.HasPositions()) continue;
            StaticMesh mesh;
            mesh.name = source.mName.length ? source.mName.C_Str() : "Mesh_" + std::to_string(index);
            mesh.materialIndex = source.mMaterialIndex < result->materials.size() ? source.mMaterialIndex : 0;
            mesh.vertices.reserve(source.mNumVertices);
            mesh.indices.reserve(static_cast<std::size_t>(source.mNumFaces) * 3);
            hasBones |= source.HasBones();
            hasMorphs |= source.mNumAnimMeshes > 0;
            for(std::uint32_t vertex = 0; vertex < source.mNumVertices; ++vertex) {
                StaticVertex value;
                const auto& position = source.mVertices[vertex];
                value.position = {position.x, position.y, position.z};
                if(!Finite(value.position)) throw std::runtime_error("Mesh contains a non-finite position: " + mesh.name);
                if(source.HasNormals()) {
                    const auto& normal = source.mNormals[vertex];
                    value.normal = SafeNormal({static_cast<float>(normal.x), static_cast<float>(normal.y), static_cast<float>(normal.z)}, {0, 1, 0});
                }
                if(source.HasTextureCoords(0)) {
                    const auto& uv = source.mTextureCoords[0][vertex];
                    if(!std::isfinite(uv.x) || !std::isfinite(uv.y)) throw std::runtime_error("Mesh contains non-finite UVs: " + mesh.name);
                    value.uv = {static_cast<float>(uv.x), static_cast<float>(uv.y)};
                }
                const auto axis = std::abs(value.normal.z) < 0.999f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
                auto tangent = SafeNormal(glm::cross(axis, value.normal), {1, 0, 0});
                float handedness = 1;
                if(source.HasTangentsAndBitangents()) {
                    const auto& t = source.mTangents[vertex];
                    const auto& b = source.mBitangents[vertex];
                    const glm::vec3 importedTangent(t.x, t.y, t.z), bitangent(b.x, b.y, b.z);
                    tangent = SafeNormal(importedTangent - value.normal * glm::dot(value.normal, importedTangent), tangent);
                    handedness = glm::dot(glm::cross(value.normal, tangent), bitangent) < 0 ? -1.0f : 1.0f;
                }
                value.tangent = glm::vec4(tangent, handedness);
                mesh.bounds.Include(value.position);
                mesh.vertices.push_back(value);
            }
            for(std::uint32_t face = 0; face < source.mNumFaces; ++face) {
                const auto& triangle = source.mFaces[face];
                if(triangle.mNumIndices != 3) throw std::runtime_error("Assimp returned a non-triangle face: " + mesh.name);
                for(int corner = 0; corner < 3; ++corner) {
                    if(triangle.mIndices[corner] >= source.mNumVertices) throw std::runtime_error("Mesh contains an invalid index: " + mesh.name);
                    mesh.indices.push_back(triangle.mIndices[corner]);
                }
            }
            // Assimp versions can leave imported bitangents unchanged after
            // FlipUVs. Derive the final TBN from the actual uploaded UVs rather
            // than guessing a sign from importer/version/format behavior.
            // Assimp's tangent pass still supplies its useful vertex splits.
            if(source.HasTextureCoords(0)) RebuildTangentsFromFinalUVs(mesh);
            meshMap[index] = static_cast<std::uint32_t>(result->meshes.size());
            result->meshes.push_back(std::move(mesh));
        }
        if(result->meshes.empty()) throw std::runtime_error("Model has no triangle meshes");
        std::function<void(const aiNode&, const glm::dmat4&, unsigned)> visit;
        visit = [&](const aiNode& node, const glm::dmat4& parent, unsigned depth) {
            if(depth > 4096) throw std::runtime_error("Model node hierarchy is too deep");
            const auto transform = parent * Matrix(node.mTransformation);
            for(std::uint32_t index = 0; index < node.mNumMeshes; ++index) {
                const auto sceneIndex = node.mMeshes[index];
                if(sceneIndex >= meshMap.size()) throw std::runtime_error("Node references an invalid mesh");
                const auto meshIndex = meshMap[sceneIndex];
                if(meshIndex == std::numeric_limits<std::uint32_t>::max()) continue;
                // glTF skinned POSITION/IBM already define the skin's frame.
                // The specification ignores the mesh node transform. Keep an
                // identity instance while retaining joint/node TRS separately.
                const bool gltfSkin=(extension==".gltf"||extension==".glb")&&scene->mMeshes[sceneIndex]->HasBones();
                const auto meshTransform=gltfSkin?glm::dmat4(1):transform;
                result->instances.push_back({node.mName.C_Str(), meshIndex, meshTransform});
                for(const auto& vertex : result->meshes[meshIndex].vertices) {
                    const auto world = meshTransform * glm::dvec4(vertex.position, 1);
                    if(!std::isfinite(world.w) || std::abs(world.w) < 1e-20) throw std::runtime_error("Node transform produces an invalid homogeneous coordinate");
                    const auto point = glm::dvec3(world) / world.w;
                    if(!Finite(point)) throw std::runtime_error("Node transform produces non-finite bounds");
                    result->bounds.Include(point);
                }
            }
            for(std::uint32_t index = 0; index < node.mNumChildren; ++index)
                visit(*node.mChildren[index], transform, depth + 1);
        };
        visit(*scene->mRootNode, glm::dmat4(1), 0);
        if(result->instances.empty()) throw std::runtime_error("Model has no visible mesh instances");
        if(hasBones || hasMorphs || scene->mNumAnimations)
            context.Warning("Imported static rest/bind pose; skeletal animation and morph targets are not evaluated.");
        return result;
    } catch(const std::exception& exception) {
        if(error) *error = exception.what();
        return {};
    }
}

} // namespace Model
