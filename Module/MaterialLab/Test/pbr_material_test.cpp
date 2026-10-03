#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "MaterialLab/Public/pbr_material.h"

namespace {

using namespace MaterialLab;
using namespace Render::Material;

template <typename T>
T Read(const ParameterBlock& block, std::string_view name, std::size_t component = 0) {
    const auto* layout = block.GetTemplate()->FindLayout(name);
    assert(layout && component * sizeof(T) + sizeof(T) <= layout->size);
    T value{};
    std::memcpy(&value, block.Bytes().data() + layout->offset + component * sizeof(T), sizeof(T));
    return value;
}

void CheckSchemaAndDefaults() {
    const auto schema = MakePbrTemplate();
    assert(schema && schema->Desc().domain == Domain::Surface);
    assert(schema->ByteSize() == 112 && schema->ByteSize() % 16 == 0);
    const char* names[] = {
        "baseColor", "metallic", "roughness", "ao", "normalScale", "useNormalMap",
        "useMetallicMap", "useRoughnessMap", "useAoMap", "baseColorIsSRGB", "uvTransform",
        "emissiveColor", "reflectionStrength", "alphaCutoff"
    };
    const std::size_t offsets[] = {0, 16, 20, 24, 28, 32, 36, 40, 44, 48, 64, 80, 96, 100};
    const ParameterType types[] = {
        ParameterType::Vec4, ParameterType::Float, ParameterType::Float, ParameterType::Float,
        ParameterType::Float, ParameterType::Bool, ParameterType::Bool, ParameterType::Bool,
        ParameterType::Bool, ParameterType::Bool, ParameterType::Vec4,
        ParameterType::Vec4, ParameterType::Float, ParameterType::Float
    };
    assert(schema->Desc().parameters.size() == std::size(names));
    for (std::size_t i = 0; i < std::size(names); ++i) {
        assert(schema->FindParameter(names[i]));
        assert(schema->FindParameter(names[i])->type == types[i]);
        assert(schema->FindLayout(names[i])->offset == offsets[i]);
    }

    ParameterBlock defaults(schema);
    for (std::size_t i = 0; i < 4; ++i) assert(Read<float>(defaults, "baseColor", i) == 1.0f);
    assert(Read<float>(defaults, "metallic") == 0.0f);
    assert(Read<float>(defaults, "roughness") == 0.5f);
    assert(Read<float>(defaults, "ao") == 1.0f);
    assert(Read<float>(defaults, "normalScale") == 1.0f);
    for (const auto* name : {"useNormalMap", "useMetallicMap", "useRoughnessMap", "useAoMap"}) {
        assert(Read<std::uint32_t>(defaults, name) == 0);
        assert(!schema->FindParameter(name)->perObject);
    }
    assert(Read<std::uint32_t>(defaults, "baseColorIsSRGB") == 1);
    assert(!schema->FindParameter("baseColorIsSRGB")->perObject);
    assert(Read<float>(defaults, "uvTransform", 0) == 1.0f);
    assert(Read<float>(defaults, "uvTransform", 1) == 1.0f);
    assert(Read<float>(defaults, "uvTransform", 2) == 0.0f);
    assert(Read<float>(defaults, "uvTransform", 3) == 0.0f);
    assert(Read<float>(defaults, "emissiveColor", 0) == 0.0f);
    assert(Read<float>(defaults, "reflectionStrength") == 0.0f);
    assert(Read<float>(defaults, "alphaCutoff") == 0.0f);
    assert(defaults.Set("emissiveColor", glm::vec4(10, 4, 1, 0)));
    for (std::size_t i = 52; i < 64; ++i) assert(defaults.Bytes()[i] == std::byte{0});

    const char* textureNames[] = {"albedoMap", "normalMap", "metallicMap", "roughnessMap", "aoMap"};
    assert(schema->Desc().textures.size() == 5);
    for (std::size_t i = 0; i < 5; ++i) {
        const auto& slot = schema->Desc().textures[i];
        assert(slot.name == textureNames[i] && slot.slot == i && slot.required);
    }
    // Lights/camera/exposure belong to pass/view buffers, not each material.
    assert(!schema->FindParameter("exposure"));
    assert(!schema->FindParameter("lightPositions"));
}

void CheckParameterValidation() {
    const auto schema = MakePbrTemplate();
    ParameterBlock block(schema);
    std::string error;
    for (const auto* name : {"metallic", "roughness", "ao"}) {
        const auto* desc = schema->FindParameter(name);
        assert(desc->min == 0.0f && desc->max == 1.0f && desc->perObject);
        assert(block.Set(name, 0.0f, &error));
        assert(block.Set(name, 1.0f, &error) && error.empty());
        const auto previous = block.Bytes();
        assert(!block.Set(name, -0.01f, &error) && !error.empty());
        assert(!block.Set(name, 1.01f, &error));
        assert(!block.Set(name, std::numeric_limits<float>::quiet_NaN(), &error));
        assert(!block.Set(name, std::int32_t(1), &error));
        assert(block.Bytes() == previous);
    }
    assert(block.Set("normalScale", 0.0f));
    assert(block.Set("normalScale", 4.0f));
    assert(!block.Set("normalScale", 4.01f, &error));
    assert(!block.Set("normalScale", -1.0f, &error));
    assert(!block.Set("baseColor", glm::vec4(1, 1, 1, -0.1f), &error));
    assert(!block.Set("baseColor", glm::vec4(1.1f, 0, 0, 1), &error));
    assert(block.Set("baseColor", glm::vec4(0.2f, 0.4f, 0.6f, 1), &error));
    assert(block.Set("uvTransform", glm::vec4(3, 2, -1, 0.5f), &error));
    assert(!block.Set("uvTransform", glm::vec4(std::numeric_limits<float>::infinity()), &error));
    assert(!block.Set("useNormalMap", 1.0f, &error));
    assert(block.Set("useNormalMap", true, &error));
    assert(Read<std::uint32_t>(block, "useNormalMap") == 1);
    assert(!block.Set("unknown", 1.0f, &error));
}

void CheckLegacyAssetAndInstanceEdits() {
    const auto schema = MakePbrTemplate();
    std::string error;
    // Corresponds to the existing tile material: scalar metallic, normal/rough/AO maps.
    const auto asset = MaterialAsset::Create(schema, {
        {"metallic", 0.6f}, {"roughness", 0.5f}, {"ao", 1.0f},
        {"useNormalMap", true}, {"useMetallicMap", false},
        {"useRoughnessMap", true}, {"useAoMap", true}
    }, &error);
    assert(asset && error.empty() && asset->Parameters().GetTemplate() == schema);
    assert(Read<float>(asset->Parameters(), "metallic") == 0.6f);
    assert(Read<std::uint32_t>(asset->Parameters(), "useNormalMap") == 1);
    assert(Read<std::uint32_t>(asset->Parameters(), "useMetallicMap") == 0);
    assert(Read<std::uint32_t>(asset->Parameters(), "useRoughnessMap") == 1);
    assert(Read<std::uint32_t>(asset->Parameters(), "useAoMap") == 1);
    assert(Read<float>(ParameterBlock(schema), "metallic") == 0.0f);

    assert(!MaterialAsset::Create(schema, {{"roughness", 1.5f}}, &error) && !error.empty());
    assert(!MaterialAsset::Create(schema, {{"useAoMap", 1.0f}}, &error));
    assert(!MaterialAsset::Create(schema, {{"metallic", 0.2f}, {"metallic", 0.9f}}, &error));

    MaterialInstance edited(asset), untouched(asset);
    const auto revision = edited.Revision();
    assert(edited.Set("roughness", 0.1f, &error));
    assert(edited.Set("useNormalMap", false, &error));
    assert(edited.Revision() == revision + 2);
    assert(Read<float>(edited.Parameters(), "roughness") == 0.1f);
    assert(Read<std::uint32_t>(edited.Parameters(), "useNormalMap") == 0);
    assert(Read<float>(untouched.Parameters(), "roughness") == 0.5f);
    assert(Read<std::uint32_t>(untouched.Parameters(), "useNormalMap") == 1);
    assert(Read<float>(asset->Parameters(), "roughness") == 0.5f);
    assert(!edited.Set("useNormalMap", std::int32_t(1), &error));
    assert(edited.Revision() == revision + 2);

    ParameterBlock draw;
    assert(edited.BuildDrawParameters({{"metallic", 0.9f}, {"normalScale", 2.0f},
        {"baseColor", glm::vec4(0.3f, 0.4f, 0.8f, 1.0f)}}, draw, &error));
    assert(Read<float>(draw, "metallic") == 0.9f);
    assert(Read<float>(draw, "normalScale") == 2.0f);
    assert(Read<float>(edited.Parameters(), "metallic") == 0.6f);
    assert(edited.Revision() == revision + 2);
    const auto unchanged = draw.Bytes();
    assert(!edited.BuildDrawParameters({{"metallic", 0.1f}, {"useNormalMap", true}}, draw, &error));
    assert(!edited.BuildDrawParameters({{"baseColorIsSRGB", false}}, draw, &error));
    assert(draw.Bytes() == unchanged);
}

void CheckPassAndShaderInterface() {
    static_assert(std::is_trivially_copyable_v<PbrPassConstants>);
    static_assert(sizeof(PbrPassConstants) == 160);
    static_assert(alignof(PbrPassConstants) == 16);
    static_assert(offsetof(PbrPassConstants, lightPositions) == 0);
    static_assert(offsetof(PbrPassConstants, lightColors) == 64);
    static_assert(offsetof(PbrPassConstants, ambientAndExposure) == 128);
    static_assert(offsetof(PbrPassConstants, outputOptions) == 144);
    static_assert(sizeof(PbrPassConstants) <= 256);
    static_assert(PbrPassBinding == 3 && MaterialParameterBinding == 2);
    const PbrPassConstants pass;
    for (std::size_t i = 0; i < 4; ++i) {
        assert(pass.lightPositions[i].w == 1.0f);
        assert(pass.lightColors[i].x == 100.0f && pass.lightColors[i].y == 100.0f && pass.lightColors[i].z == 100.0f);
    }
    assert(pass.ambientAndExposure.x == 0.03f && pass.ambientAndExposure.w == 1.0f);
    assert(pass.outputOptions.x == 0.2f && pass.outputOptions.y == 2.2f && pass.outputOptions.z == 1.0f);

    const auto declaration = MakePbrTemplate()->GenerateGLSLUniformBlock();
    assert(declaration.find("uint useNormalMap;") != std::string::npos);
    assert(declaration.find("uint baseColorIsSRGB;") != std::string::npos);
    assert(PbrVertexShader().find(declaration) != std::string::npos);
    assert(PbrFragmentShader().find(declaration) != std::string::npos);
    const Render::RenderResourceHandle<Render::ShaderProgramSpec> program{9, 2};
    const auto pipeline = PbrPipelineSpec(program);
    assert(pipeline.shaderProgram == program && pipeline.depthTest && pipeline.depthWrite);
    assert(!pipeline.blendEnable && pipeline.blendMode == Render::BlendMode::Opaque);
    assert((pipeline.expectVertexLayout.typeSlots == std::vector<Render::VertexFieldType>{
        Render::VertexFieldType::Vec3, Render::VertexFieldType::Vec3,
        Render::VertexFieldType::Vec2, Render::VertexFieldType::Vec4}));
}

} // namespace

int main() {
    CheckSchemaAndDefaults();
    CheckParameterValidation();
    CheckLegacyAssetAndInstanceEdits();
    CheckPassAndShaderInterface();
    std::cout << "MaterialLab PBR CPU tests passed\n";
}
