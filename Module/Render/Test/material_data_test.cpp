#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>

#include "Render/Public/Material/material.h"
#include "Render/Public/Material/unlit_material.h"

using namespace Render::Material;

namespace {

template <typename T>
T Read(const std::vector<std::byte>& bytes, std::size_t offset) {
    assert(offset + sizeof(T) <= bytes.size());
    T result{};
    std::memcpy(&result, bytes.data() + offset, sizeof(T));
    return result;
}

MaterialTemplateDesc BasicDesc() {
    MaterialTemplateDesc desc;
    desc.name = "TestSurface";
    desc.parameters = {
        {"amount", ParameterType::Float, 0.25f, "Amount", "Surface", 0.0f, 1.0f, true},
        {"uvScale", ParameterType::Vec2, glm::vec2(2.0f, 3.0f)},
        {"normal", ParameterType::Vec3, glm::vec3(4.0f, 5.0f, 6.0f)},
        {"threshold", ParameterType::Float, 0.75f},
        {"enabled", ParameterType::Bool, true},
        {"mode", ParameterType::Int, std::int32_t(-7)},
        {"tint", ParameterType::Vec4, glm::vec4(1.0f), "Tint", "Surface", 0.0f, 1.0f, true},
        {"matrix", ParameterType::Mat4, glm::mat4(1.0f)}
    };
    desc.textures = {{"baseColor", 0, true}, {"normalMap", 1, false}};
    return desc;
}

void CheckLayoutAndPacking() {
    std::string error = "stale";
    const auto materialTemplate = MaterialTemplate::Create(BasicDesc(), &error);
    assert(materialTemplate && error.empty());
    assert(materialTemplate->Desc().domain == Domain::Surface);
    assert(materialTemplate->ByteSize() == 128);
    assert(materialTemplate->Layouts().size() == 8);
    const std::size_t offsets[] = {0, 8, 16, 32, 36, 40, 48, 64};
    for (std::size_t i = 0; i < 8; ++i)
        assert(materialTemplate->Layouts()[i].offset == offsets[i]);
    assert(materialTemplate->FindLayout("normal")->size == 16);
    assert(materialTemplate->FindParameter("missing") == nullptr);
    assert(materialTemplate->FindLayout("missing") == nullptr);

    ParameterBlock block(materialTemplate);
    assert(block.Valid() && block.GetTemplate() == materialTemplate);
    assert(Read<float>(block.Bytes(), 0) == 0.25f);
    assert(Read<std::uint32_t>(block.Bytes(), 4) == 0); // vec2 alignment padding
    assert(Read<float>(block.Bytes(), 8) == 2.0f);
    assert(Read<float>(block.Bytes(), 12) == 3.0f);
    assert(Read<float>(block.Bytes(), 24) == 6.0f);
    assert(Read<std::uint32_t>(block.Bytes(), 28) == 0); // reserved vec3 fourth component
    assert(Read<float>(block.Bytes(), 32) == 0.75f);
    assert(Read<std::uint32_t>(block.Bytes(), 36) == 1);
    assert(Read<std::int32_t>(block.Bytes(), 40) == -7);
    assert(Read<std::uint32_t>(block.Bytes(), 44) == 0);
    assert(Read<float>(block.Bytes(), 124) == 1.0f);

    glm::mat4 matrix(0.0f);
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            matrix[column][row] = static_cast<float>(column * 4 + row + 1);
    assert(block.Set("matrix", matrix, &error));
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            assert(Read<float>(block.Bytes(), 64 + column * 16 + row * 4) == matrix[column][row]);
    assert(block.Set("enabled", false));
    assert(Read<std::uint32_t>(block.Bytes(), 36) == 0);
    assert(Read<std::uint32_t>(materialTemplate->DefaultBytes(), 36) == 1);

    const auto unchanged = block.Bytes();
    assert(!block.Set("missing", 1.0f, &error) && !error.empty());
    assert(!block.Set("amount", std::int32_t(1), &error));
    assert(!block.Set("amount", 1.01f, &error));
    assert(!block.Set("amount", std::numeric_limits<float>::quiet_NaN(), &error));
    assert(!block.Set("tint", glm::vec4(0.5f, 0.5f, -0.1f, 1.0f), &error));
    assert(block.Bytes() == unchanged);
    assert(block.Set("amount", 1.0f, &error) && error.empty());
}

void CheckSchemaRejection() {
    std::string error;
    const auto reject = [&](MaterialTemplateDesc desc) {
        assert(!MaterialTemplate::Create(std::move(desc), &error));
        assert(!error.empty());
    };
    auto desc = BasicDesc(); desc.name.clear(); reject(desc);
    desc = BasicDesc(); desc.domain = static_cast<Domain>(255); reject(desc);
    desc = BasicDesc(); desc.parameters[1].name = "amount"; reject(desc);
    desc = BasicDesc(); desc.parameters[1].name = "bad name"; reject(desc);
    desc = BasicDesc(); desc.parameters[1].name = "0bad"; reject(desc);
    desc = BasicDesc(); desc.parameters[0].defaultValue = true; reject(desc);
    desc = BasicDesc(); desc.parameters[0].type = static_cast<ParameterType>(255); reject(desc);
    desc = BasicDesc(); desc.parameters[0].min = 2.0f; reject(desc);
    desc = BasicDesc(); desc.parameters[0].max = 0.1f; reject(desc);
    desc = BasicDesc(); desc.parameters[0].max = std::numeric_limits<float>::infinity(); reject(desc);
    desc = BasicDesc(); desc.parameters[4].min = 0.0f; reject(desc);
    desc = BasicDesc(); desc.parameters[7].max = 1.0f; reject(desc);
    desc = BasicDesc(); desc.parameters[5].min = -6.0f; reject(desc);
    desc = BasicDesc(); desc.parameters[2].defaultValue = glm::vec3(std::numeric_limits<float>::infinity()); reject(desc);
    desc = BasicDesc(); desc.textures[1].slot = 0; reject(desc);
    desc = BasicDesc(); desc.textures[1].name = "baseColor"; reject(desc);
    desc = BasicDesc(); desc.textures[1].name = "amount"; reject(desc);
    desc = BasicDesc(); desc.textures[1].name.clear(); reject(desc);
    desc = BasicDesc(); desc.textures[1].slot = MaxMaterialTextureSlots; reject(desc);
    desc = BasicDesc(); desc.textures[1].slot = std::numeric_limits<std::uint32_t>::max(); reject(desc);
    desc = BasicDesc(); desc.textures[1].slot = MaxMaterialTextureSlots - 1;
    assert(MaterialTemplate::Create(desc, &error) && error.empty());

    desc = {};
    desc.name = "MaximumBlock";
    for (int i = 0; i < 4; ++i)
        desc.parameters.push_back({"matrix" + std::to_string(i), ParameterType::Mat4, glm::mat4(1.0f)});
    const auto maximum = MaterialTemplate::Create(desc, &error);
    assert(maximum && maximum->ByteSize() == 256 && error.empty());
    desc.parameters.push_back({"overflow", ParameterType::Float, 0.0f});
    reject(desc);

    desc = {};
    desc.name = "TextureOnlySprite";
    desc.domain = Domain::Sprite;
    desc.textures = {{"image", 0, true}};
    const auto empty = MaterialTemplate::Create(desc, &error);
    assert(empty && empty->ByteSize() == 0);
    assert(ParameterBlock(empty).Valid());
    ParameterBlock invalid;
    assert(!invalid.Valid() && invalid.Bytes().empty());
    assert(!invalid.Set("amount", 0.5f, &error));
}

void CheckAssetInstanceAndDrawIsolation() {
    const auto materialTemplate = MaterialTemplate::Create(BasicDesc());
    std::string error;
    assert(!MaterialAsset::Create(nullptr, {}, &error));
    assert(!MaterialAsset::Create(materialTemplate, {{"missing", 0.5f}}, &error));
    assert(!MaterialAsset::Create(materialTemplate, {{"amount", 0.5f}, {"amount", 0.7f}}, &error));
    const auto asset = MaterialAsset::Create(materialTemplate, {{"amount", 0.4f}}, &error);
    assert(asset && error.empty());
    MaterialInstance first(asset);
    MaterialInstance second(asset);
    const auto initialRevision = first.Revision();
    assert(initialRevision != 0 && second.Revision() == initialRevision);
    assert(first.Set("amount", 0.6f, &error));
    assert(first.Revision() == initialRevision + 1);
    assert(Read<float>(first.Parameters().Bytes(), 0) == 0.6f);
    assert(Read<float>(second.Parameters().Bytes(), 0) == 0.4f);
    assert(Read<float>(asset->Parameters().Bytes(), 0) == 0.4f);
    assert(Read<float>(materialTemplate->DefaultBytes(), 0) == 0.25f);
    assert(!first.Set("amount", 2.0f, &error));
    assert(first.Revision() == initialRevision + 1);

    auto copy = first;
    assert(copy.Set("amount", 0.9f));
    assert(Read<float>(first.Parameters().Bytes(), 0) == 0.6f);

    ParameterBlock draw;
    assert(first.BuildDrawParameters({{"amount", 0.8f}, {"tint", glm::vec4(0.5f)}}, draw, &error));
    assert(draw.Valid() && error.empty());
    assert(Read<float>(draw.Bytes(), 0) == 0.8f);
    assert(Read<float>(draw.Bytes(), 48) == 0.5f);
    assert(Read<float>(first.Parameters().Bytes(), 0) == 0.6f);
    assert(first.Revision() == initialRevision + 1);
    const auto unchanged = draw.Bytes();
    assert(!first.BuildDrawParameters({{"enabled", false}}, draw, &error));
    assert(!first.BuildDrawParameters({{"unknown", 0.5f}}, draw, &error));
    assert(!first.BuildDrawParameters({{"amount", 0.1f}, {"amount", 0.2f}}, draw, &error));
    assert(!first.BuildDrawParameters({{"amount", 0.1f}, {"tint", glm::vec4(2.0f)}}, draw, &error));
    assert(draw.Bytes() == unchanged); // Failed draw edits are transactional.
    assert(first.BuildDrawParameters({}, draw, &error));
    assert(draw.Bytes() == first.Parameters().Bytes());

    MaterialInstance invalid(nullptr);
    assert(invalid.Revision() == 0 && !invalid.Parameters().Valid());
    assert(!invalid.Set("amount", 0.5f, &error));
    assert(!invalid.BuildDrawParameters({}, draw, &error));
}

void CheckGeneratedGLSLContract() {
    const auto materialTemplate = MaterialTemplate::Create(BasicDesc());
    std::string error = "stale";
    const auto source = materialTemplate->GenerateGLSLUniformBlock("TestMaterial", MaterialParameterBinding, &error);
    const std::string expected =
        "layout(std140, binding = 2) uniform TestMaterial {\n"
        "    float amount;\n"
        "    vec2 uvScale;\n"
        "    vec3 normal;\n"
        "    float materialPadding_2;\n"
        "    float threshold;\n"
        "    uint enabled; // bool: 0u=false, 1u=true\n"
        "    int mode;\n"
        "    vec4 tint;\n"
        "    mat4 matrix;\n"
        "};\n";
    assert(source == expected && error.empty());
    assert(materialTemplate->GenerateGLSLUniformBlock().starts_with(
        "layout(std140, binding = 2) uniform MaterialData"));
    for (const std::uint32_t binding : {0u, 1u, 3u, 7u, std::numeric_limits<std::uint32_t>::max()}) {
        assert(materialTemplate->GenerateGLSLUniformBlock("TestMaterial", binding, &error).empty());
        assert(!error.empty());
    }

    const char* invalidNames[] = {
        "", "0value", "two words", "value; }; void main() {", "float", "layout", "gl_Position",
        "contains__reserved", "materialPadding_0", "sampler2D", "uimage2DMSArray", "main"
    };
    for (const auto* name : invalidNames) {
        assert(materialTemplate->GenerateGLSLUniformBlock(name, 2, &error).empty());
        assert(!error.empty());
        auto desc = BasicDesc();
        desc.parameters[0].name = name;
        assert(!MaterialTemplate::Create(desc, &error) && !error.empty());
        desc = BasicDesc();
        desc.textures[0].name = name;
        assert(!MaterialTemplate::Create(desc, &error) && !error.empty());
    }
    assert(materialTemplate->GenerateGLSLUniformBlock("amount", 2, &error).empty() && !error.empty());
    assert(materialTemplate->GenerateGLSLUniformBlock("baseColor", 2, &error).empty() && !error.empty());
    auto desc = BasicDesc();
    desc.parameters.clear();
    const auto textureOnly = MaterialTemplate::Create(desc);
    assert(textureOnly->GenerateGLSLUniformBlock("MaterialData", 2, &error).empty() && error.empty());
}

void CheckUnlitTemplates() {
    const auto surface = MakeUnlitTemplate(Domain::Surface);
    const auto sprite = MakeUnlitTemplate(Domain::Sprite);
    assert(surface && sprite);
    assert(surface->Desc().domain == Domain::Surface && sprite->Desc().domain == Domain::Sprite);
    assert(surface->ByteSize() == 48 && sprite->ByteSize() == 48);
    assert(surface->FindLayout("baseColor")->offset == 0);
    assert(surface->FindLayout("uvTransform")->offset == 16);
    assert(surface->FindLayout("alphaCutoff")->offset == 32);
    assert(surface->DefaultBytes() == sprite->DefaultBytes());
    assert(Read<float>(surface->DefaultBytes(), 0) == 1.0f);
    assert(Read<float>(surface->DefaultBytes(), 16) == 1.0f);
    assert(Read<float>(surface->DefaultBytes(), 20) == 1.0f);
    assert(Read<float>(surface->DefaultBytes(), 24) == 0.0f);
    assert(Read<float>(surface->DefaultBytes(), 28) == 0.0f);
    assert(Read<float>(surface->DefaultBytes(), 32) == 0.0f);
    assert(surface->FindParameter("baseColor")->perObject);
    assert(surface->FindParameter("uvTransform")->perObject);
    assert(!surface->FindParameter("alphaCutoff")->perObject);
    assert(surface->Desc().textures.size() == 1);
    assert(surface->Desc().textures[0].slot == 0 && surface->Desc().textures[0].required);

    const auto surfaceShader = UnlitVertexShader(Domain::Surface);
    const auto spriteShader = UnlitVertexShader(Domain::Sprite);
    const auto fragmentShader = UnlitFragmentShader();
    assert(surfaceShader.find("layout(location = 0) in vec3 aPosition;") != std::string::npos);
    assert(spriteShader.find("layout(location = 0) in vec2 aPosition;") != std::string::npos);
    assert(spriteShader.find("layout(location = 2) in vec4 aColor;") != std::string::npos);
    assert(surfaceShader.find("binding = 0) uniform ViewData") != std::string::npos);
    assert(surfaceShader.find("binding = 1) uniform ObjectData") != std::string::npos);
    assert(spriteShader.find("vec4(aPosition, 0.0, 1.0)") != std::string::npos);
    assert(fragmentShader.find(surface->GenerateGLSLUniformBlock()) != std::string::npos);
    assert(fragmentShader.find("layout(binding = 0) uniform sampler2D baseColorTexture;") != std::string::npos);
    assert(fragmentShader.find("* vColor * baseColor") != std::string::npos);
    assert(fragmentShader.find("if (color.a < alphaCutoff) discard;") != std::string::npos);

    const Render::RenderResourceHandle<Render::ShaderProgramSpec> program{7, 2};
    const auto surfacePipeline = UnlitPipelineSpec(program, Domain::Surface);
    const auto spritePipeline = UnlitPipelineSpec(program, Domain::Sprite);
    assert(surfacePipeline.shaderProgram == program && spritePipeline.shaderProgram == program);
    assert(surfacePipeline.depthTest && surfacePipeline.depthWrite && !surfacePipeline.blendEnable);
    assert(surfacePipeline.blendMode == Render::BlendMode::Opaque);
    assert(surfacePipeline.cullMode == Render::CullMode::Back);
    assert(!spritePipeline.depthTest && !spritePipeline.depthWrite && spritePipeline.blendEnable);
    assert(spritePipeline.cullMode == Render::CullMode::None && spritePipeline.blendMode == Render::BlendMode::Alpha);
    assert((surfacePipeline.expectVertexLayout.typeSlots == std::vector<Render::VertexFieldType>{
        Render::VertexFieldType::Vec3, Render::VertexFieldType::Vec2, Render::VertexFieldType::Vec4}));
    assert((spritePipeline.expectVertexLayout.typeSlots == std::vector<Render::VertexFieldType>{
        Render::VertexFieldType::Vec2, Render::VertexFieldType::Vec2, Render::VertexFieldType::Vec4}));
    assert(!MakeUnlitTemplate(static_cast<Domain>(255)));
    assert(UnlitVertexShader(static_cast<Domain>(255)).empty());
}

} // namespace

int main() {
    CheckLayoutAndPacking();
    CheckSchemaRejection();
    CheckAssetInstanceAndDrawIsolation();
    CheckGeneratedGLSLContract();
    CheckUnlitTemplates();
    std::cout << "Material CPU data tests passed\n";
}
