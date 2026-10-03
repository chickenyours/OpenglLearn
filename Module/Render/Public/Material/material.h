#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

namespace Render::Material {

inline constexpr std::uint32_t MaterialParameterBinding = 2;
inline constexpr std::uint32_t MaxMaterialTextureSlots = 16;

static_assert(sizeof(float) == 4 && sizeof(std::int32_t) == 4,
              "Material parameter storage requires 32-bit scalars");

enum class Domain : std::uint8_t { Surface, Sprite };
enum class ParameterType : std::uint8_t { Float, Int, Bool, Vec2, Vec3, Vec4, Mat4 };
using ParameterValue = std::variant<float, std::int32_t, bool, glm::vec2, glm::vec3, glm::vec4, glm::mat4>;

struct ParameterDesc {
    std::string name;
    ParameterType type = ParameterType::Float;
    ParameterValue defaultValue = 0.0f;
    std::string displayName{};
    std::string group{};
    std::optional<float> min = std::nullopt;
    std::optional<float> max = std::nullopt;
    bool perObject = false;
};

struct TextureSlotDesc {
    std::string name;
    std::uint32_t slot = 0;
    bool required = true;
};

struct MaterialTemplateDesc {
    std::string name;
    Domain domain = Domain::Surface;
    std::vector<ParameterDesc> parameters;
    std::vector<TextureSlotDesc> textures;
};

struct ParameterLayout {
    std::size_t offset = 0;
    std::size_t size = 0;
    std::size_t alignment = 0;
};

struct ParameterOverride {
    std::string name;
    ParameterValue value = 0.0f;
};

namespace Detail {

inline bool Fail(std::string* error, std::string message) {
    if (error) *error = std::move(message);
    return false;
}

inline void ClearError(std::string* error) {
    if (error) error->clear();
}

inline bool IsIdentifier(std::string_view name) {
    const auto letter = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    };
    if (name.empty() || !letter(name.front())) return false;
    return std::all_of(name.begin() + 1, name.end(), [&](char c) {
        return letter(c) || (c >= '0' && c <= '9');
    });
}

inline constexpr std::string_view PaddingPrefix = "materialPadding_";

inline bool IsGLSLIdentifier(std::string_view name) {
    if (!IsIdentifier(name) || name.starts_with("gl_") ||
        name.find("__") != std::string_view::npos || name.starts_with(PaddingPrefix)) return false;
    // GLSL 4.50 keywords, reserved words and opaque type names must not become
    // uniform members. This is validation, not shader source escaping.
    constexpr std::string_view reserved[] = {
        "attribute", "const", "uniform", "varying", "buffer", "shared", "coherent", "volatile",
        "restrict", "readonly", "writeonly", "atomic_uint", "layout", "centroid", "flat", "smooth",
        "noperspective", "patch", "sample", "break", "continue", "do", "for", "while", "switch",
        "case", "default", "if", "else", "subroutine", "in", "out", "inout", "float", "double",
        "int", "void", "bool", "true", "false", "invariant", "precise", "discard", "return",
        "mat2", "mat3", "mat4", "dmat2", "dmat3", "dmat4", "mat2x2", "mat2x3", "mat2x4",
        "mat3x2", "mat3x3", "mat3x4", "mat4x2", "mat4x3", "mat4x4", "dmat2x2", "dmat2x3",
        "dmat2x4", "dmat3x2", "dmat3x3", "dmat3x4", "dmat4x2", "dmat4x3", "dmat4x4",
        "vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4", "bvec2", "bvec3", "bvec4",
        "dvec2", "dvec3", "dvec4", "uint", "uvec2", "uvec3", "uvec4", "lowp", "mediump",
        "highp", "precision", "struct", "common", "partition", "active", "asm", "class", "union",
        "enum", "typedef", "template", "this", "resource", "goto", "inline", "noinline", "public",
        "static", "extern", "external", "interface", "long", "short", "half", "fixed", "unsigned",
        "superp", "input", "output", "hvec2", "hvec3", "hvec4", "fvec2", "fvec3", "fvec4",
        "sampler3DRect", "filter", "sizeof", "cast", "namespace", "using", "main"
    };
    for (const auto keyword : reserved)
        if (name == keyword) return false;
    constexpr std::string_view prefixes[] = {"sampler", "isampler", "usampler", "image", "iimage", "uimage"};
    constexpr std::string_view suffixes[] = {
        "1D", "2D", "3D", "Cube", "2DRect", "1DArray", "2DArray", "Buffer", "2DMS", "2DMSArray",
        "CubeArray", "1DShadow", "2DShadow", "CubeShadow", "2DRectShadow", "1DArrayShadow",
        "2DArrayShadow", "CubeArrayShadow"
    };
    for (const auto prefix : prefixes) {
        if (!name.starts_with(prefix)) continue;
        for (const auto suffix : suffixes)
            if (name.substr(prefix.size()) == suffix) return false;
    }
    return true;
}

inline std::string_view GLSLType(ParameterType type) {
    switch (type) {
    case ParameterType::Float: return "float";
    case ParameterType::Int: return "int";
    case ParameterType::Bool: return "uint";
    case ParameterType::Vec2: return "vec2";
    case ParameterType::Vec3: return "vec3";
    case ParameterType::Vec4: return "vec4";
    case ParameterType::Mat4: return "mat4";
    }
    return {};
}

inline ParameterLayout TypeLayout(ParameterType type) {
    switch (type) {
    case ParameterType::Float:
    case ParameterType::Int:
    case ParameterType::Bool: return {0, 4, 4};
    case ParameterType::Vec2: return {0, 8, 8};
    // GenerateGLSLUniformBlock emits a padding member for the fourth component.
    case ParameterType::Vec3:
    case ParameterType::Vec4: return {0, 16, 16};
    case ParameterType::Mat4: return {0, 64, 16};
    }
    return {};
}

inline bool ValidateValue(const ParameterDesc& desc, const ParameterValue& value,
                          std::string* error) {
    if (value.index() != static_cast<std::size_t>(desc.type))
        return Fail(error, "Parameter '" + desc.name + "' has the wrong value type");

    const auto scalarValid = [&](double scalar) {
        return std::isfinite(scalar) && (!desc.min || scalar >= *desc.min) &&
               (!desc.max || scalar <= *desc.max);
    };
    bool valid = true;
    std::visit([&](const auto& typed) {
        using T = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<T, bool>) {
            valid = true;
        } else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, std::int32_t>) {
            valid = scalarValid(static_cast<double>(typed));
        } else if constexpr (std::is_same_v<T, glm::mat4>) {
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    valid = valid && std::isfinite(typed[column][row]);
        } else {
            for (glm::length_t i = 0; i < typed.length(); ++i)
                valid = valid && scalarValid(typed[i]);
        }
    }, value);
    return valid || Fail(error, "Parameter '" + desc.name + "' is non-finite or outside its range");
}

inline void WriteValue(std::vector<std::byte>& bytes, const ParameterLayout& layout,
                       const ParameterValue& value) {
    auto* destination = bytes.data() + layout.offset;
    std::fill(destination, destination + layout.size, std::byte{0});
    std::visit([&](const auto& typed) {
        using T = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<T, bool>) {
            const std::uint32_t encoded = typed ? 1u : 0u;
            std::memcpy(destination, &encoded, sizeof(encoded));
        } else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, std::int32_t>) {
            std::memcpy(destination, &typed, sizeof(typed));
        } else if constexpr (std::is_same_v<T, glm::mat4>) {
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    std::memcpy(destination + column * 16 + row * 4, &typed[column][row], 4);
        } else {
            for (glm::length_t i = 0; i < typed.length(); ++i)
                std::memcpy(destination + i * 4, &typed[i], 4);
        }
    }, value);
}

} // namespace Detail

// Immutable CPU metadata. No graphics API objects, globals or resource-manager
// ownership are required to create templates and edit their parameter blocks.
class MaterialTemplate {
public:
    static constexpr std::size_t MaxParameterBytes = 256;

    static std::shared_ptr<const MaterialTemplate> Create(MaterialTemplateDesc desc,
                                                         std::string* error = nullptr) {
        Detail::ClearError(error);
        if (desc.name.empty()) {
            Detail::Fail(error, "Material template name cannot be empty");
            return nullptr;
        }
        if (desc.domain != Domain::Surface && desc.domain != Domain::Sprite) {
            Detail::Fail(error, "Unknown material domain");
            return nullptr;
        }
        auto result = std::shared_ptr<MaterialTemplate>(new MaterialTemplate(std::move(desc)));
        std::unordered_set<std::string> names;
        std::size_t end = 0;
        for (const auto& parameter : result->desc_.parameters) {
            if (!Detail::IsGLSLIdentifier(parameter.name) || !names.insert(parameter.name).second) {
                Detail::Fail(error, "Invalid or duplicate parameter name: '" + parameter.name + "'");
                return nullptr;
            }
            auto layout = Detail::TypeLayout(parameter.type);
            if (layout.size == 0) {
                Detail::Fail(error, "Unknown parameter type: '" + parameter.name + "'");
                return nullptr;
            }
            if ((parameter.min && !std::isfinite(*parameter.min)) ||
                (parameter.max && !std::isfinite(*parameter.max)) ||
                (parameter.min && parameter.max && *parameter.min > *parameter.max) ||
                ((parameter.min || parameter.max) &&
                 (parameter.type == ParameterType::Bool || parameter.type == ParameterType::Mat4))) {
                Detail::Fail(error, "Invalid range for parameter '" + parameter.name + "'");
                return nullptr;
            }
            if (!Detail::ValidateValue(parameter, parameter.defaultValue, error)) return nullptr;
            layout.offset = (end + layout.alignment - 1) / layout.alignment * layout.alignment;
            end = layout.offset + layout.size;
            if (end > MaxParameterBytes) {
                Detail::Fail(error, "Material parameter block exceeds 256 bytes");
                return nullptr;
            }
            result->layouts_.push_back(layout);
        }
        std::unordered_set<std::uint32_t> slots;
        for (const auto& texture : result->desc_.textures) {
            if (texture.slot >= MaxMaterialTextureSlots) {
                Detail::Fail(error, "Texture slot must be below " + std::to_string(MaxMaterialTextureSlots));
                return nullptr;
            }
            if (!Detail::IsGLSLIdentifier(texture.name) || !names.insert(texture.name).second ||
                !slots.insert(texture.slot).second) {
                Detail::Fail(error, "Invalid or duplicate texture name/slot: '" + texture.name + "'");
                return nullptr;
            }
        }
        result->defaults_.resize((end + 15) / 16 * 16, std::byte{0});
        for (std::size_t i = 0; i < result->layouts_.size(); ++i)
            Detail::WriteValue(result->defaults_, result->layouts_[i], result->desc_.parameters[i].defaultValue);
        return result;
    }

    const MaterialTemplateDesc& Desc() const noexcept { return desc_; }
    std::size_t ByteSize() const noexcept { return defaults_.size(); }
    const std::vector<std::byte>& DefaultBytes() const noexcept { return defaults_; }
    const std::vector<ParameterLayout>& Layouts() const noexcept { return layouts_; }

    const ParameterDesc* FindParameter(std::string_view name) const noexcept {
        for (const auto& parameter : desc_.parameters)
            if (parameter.name == name) return &parameter;
        return nullptr;
    }

    const ParameterLayout* FindLayout(std::string_view name) const noexcept {
        for (std::size_t i = 0; i < desc_.parameters.size(); ++i)
            if (desc_.parameters[i].name == name) return &layouts_[i];
        return nullptr;
    }

    // The returned declaration matches Bytes() without caller-managed padding.
    // Bool parameters are uint in GLSL; read them with `parameter != 0u`.
    // A texture-only template returns an empty declaration without an error.
    std::string GenerateGLSLUniformBlock(std::string_view blockName = "MaterialData",
                                         std::uint32_t binding = MaterialParameterBinding,
                                         std::string* error = nullptr) const {
        Detail::ClearError(error);
        if (binding != MaterialParameterBinding) {
            Detail::Fail(error, "Material parameter blocks must use binding " + std::to_string(MaterialParameterBinding));
            return {};
        }
        if (!Detail::IsGLSLIdentifier(blockName) || FindParameter(blockName)) {
            Detail::Fail(error, "Invalid or conflicting GLSL block name: '" + std::string(blockName) + "'");
            return {};
        }
        for (const auto& texture : desc_.textures) {
            if (texture.name == blockName) {
                Detail::Fail(error, "GLSL block name conflicts with texture: '" + std::string(blockName) + "'");
                return {};
            }
        }
        if (desc_.parameters.empty()) return {};
        std::string source = "layout(std140, binding = " + std::to_string(binding) + ") uniform " +
                             std::string(blockName) + " {\n";
        for (std::size_t i = 0; i < desc_.parameters.size(); ++i) {
            const auto& parameter = desc_.parameters[i];
            source += "    " + std::string(Detail::GLSLType(parameter.type)) + " " + parameter.name + ";";
            if (parameter.type == ParameterType::Bool) source += " // bool: 0u=false, 1u=true";
            source += "\n";
            if (parameter.type == ParameterType::Vec3)
                source += "    float " + std::string(Detail::PaddingPrefix) + std::to_string(i) + ";\n";
        }
        source += "};\n";
        return source;
    }

private:
    explicit MaterialTemplate(MaterialTemplateDesc desc) : desc_(std::move(desc)) {}
    MaterialTemplateDesc desc_;
    std::vector<ParameterLayout> layouts_;
    std::vector<std::byte> defaults_;
};

class ParameterBlock {
public:
    ParameterBlock() = default;
    explicit ParameterBlock(std::shared_ptr<const MaterialTemplate> materialTemplate)
        : template_(std::move(materialTemplate)) {
        if (template_) bytes_ = template_->DefaultBytes();
    }

    bool Valid() const noexcept { return template_ != nullptr; }
    std::shared_ptr<const MaterialTemplate> GetTemplate() const noexcept { return template_; }
    const std::vector<std::byte>& Bytes() const noexcept { return bytes_; }

    bool Set(std::string_view name, const ParameterValue& value, std::string* error = nullptr) {
        Detail::ClearError(error);
        if (!template_) return Detail::Fail(error, "Parameter block has no material template");
        const auto* parameter = template_->FindParameter(name);
        if (!parameter) return Detail::Fail(error, "Unknown material parameter: '" + std::string(name) + "'");
        if (!Detail::ValidateValue(*parameter, value, error)) return false;
        Detail::WriteValue(bytes_, *template_->FindLayout(name), value);
        return true;
    }

private:
    std::shared_ptr<const MaterialTemplate> template_;
    std::vector<std::byte> bytes_;
};

class MaterialAsset {
public:
    static std::shared_ptr<const MaterialAsset> Create(
        std::shared_ptr<const MaterialTemplate> materialTemplate,
        std::vector<ParameterOverride> defaults = {}, std::string* error = nullptr) {
        Detail::ClearError(error);
        if (!materialTemplate) {
            Detail::Fail(error, "Material asset has no material template");
            return nullptr;
        }
        auto result = std::shared_ptr<MaterialAsset>(new MaterialAsset(std::move(materialTemplate)));
        std::unordered_set<std::string> names;
        for (const auto& override : defaults) {
            if (!names.insert(override.name).second) {
                Detail::Fail(error, "Duplicate asset override: '" + override.name + "'");
                return nullptr;
            }
            if (!result->parameters_.Set(override.name, override.value, error)) return nullptr;
        }
        return result;
    }

    const ParameterBlock& Parameters() const noexcept { return parameters_; }

private:
    explicit MaterialAsset(std::shared_ptr<const MaterialTemplate> materialTemplate)
        : parameters_(std::move(materialTemplate)) {}
    ParameterBlock parameters_;
};

// Instance edits are owned by the calling/main thread. Copies have independent
// parameter storage. Publish a copied block to the renderer at a frame boundary.
class MaterialInstance {
public:
    explicit MaterialInstance(std::shared_ptr<const MaterialAsset> asset)
        : asset_(std::move(asset)) {
        if (asset_) {
            parameters_ = asset_->Parameters();
            revision_ = 1;
        }
    }

    bool Set(std::string_view name, const ParameterValue& value, std::string* error = nullptr) {
        if (!parameters_.Set(name, value, error)) return false;
        ++revision_;
        return true;
    }

    const ParameterBlock& Parameters() const noexcept { return parameters_; }
    std::uint64_t Revision() const noexcept { return revision_; }

    bool BuildDrawParameters(const std::vector<ParameterOverride>& overrides,
                             ParameterBlock& output, std::string* error = nullptr) const {
        Detail::ClearError(error);
        if (!parameters_.Valid()) return Detail::Fail(error, "Material instance has no asset");
        ParameterBlock candidate = parameters_;
        std::unordered_set<std::string> names;
        for (const auto& override : overrides) {
            const auto* parameter = candidate.GetTemplate()->FindParameter(override.name);
            if (!parameter || !parameter->perObject)
                return Detail::Fail(error, "Parameter does not allow per-object overrides: '" + override.name + "'");
            if (!names.insert(override.name).second)
                return Detail::Fail(error, "Duplicate draw override: '" + override.name + "'");
            if (!candidate.Set(override.name, override.value, error)) return false;
        }
        output = std::move(candidate);
        return true;
    }

private:
    std::shared_ptr<const MaterialAsset> asset_;
    ParameterBlock parameters_;
    std::uint64_t revision_ = 0;
};

} // namespace Render::Material
