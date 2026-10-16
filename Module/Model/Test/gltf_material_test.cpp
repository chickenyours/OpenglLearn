#include "Model/Public/static_model.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef MODEL_SOURCE_DIR
#error MODEL_SOURCE_DIR must identify the repository root.
#endif

namespace {
int failures = 0;

void Expect(bool value, const char* description) {
    std::cout << "  " << description << (value ? " : ok\n" : " : FAIL\n");
    failures += !value;
}

bool Near(float actual, float expected, float tolerance = 1e-5f) {
    return std::abs(actual - expected) < tolerance;
}

std::string Base64(const std::vector<std::uint8_t>& bytes) {
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::uint32_t a = bytes[i], b = i + 1 < bytes.size() ? bytes[i + 1] : 0,
            c = i + 2 < bytes.size() ? bytes[i + 2] : 0;
        const auto value = a << 16 | b << 8 | c;
        result.push_back(alphabet[(value >> 18) & 63]);
        result.push_back(alphabet[(value >> 12) & 63]);
        result.push_back(i + 1 < bytes.size() ? alphabet[(value >> 6) & 63] : '=');
        result.push_back(i + 2 < bytes.size() ? alphabet[value & 63] : '=');
    }
    return result;
}

void AppendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

std::vector<std::uint8_t> Geometry() {
    std::vector<std::uint8_t> bytes;
    for (const float value : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f,
                            0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f,
                            0.f, 0.f, 1.f, 0.f, 0.f, 1.f})
        AppendU32(bytes, std::bit_cast<std::uint32_t>(value));
    for (const std::uint16_t index : {0, 1, 2}) {
        bytes.push_back(static_cast<std::uint8_t>(index));
        bytes.push_back(static_cast<std::uint8_t>(index >> 8));
    }
    return bytes;
}

void Write(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary);
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!output) throw std::runtime_error("Cannot write glTF regression fixture");
}

std::filesystem::path Fixture(const std::filesystem::path& directory) {
    // A two-pixel RGBA PNG: [64,128,192,128], [255,32,16,255]. All image
    // data and triangle buffers live inside the glTF; no sidecar files exist.
    constexpr auto png = "iVBORw0KGgoAAAANSUhEUgAAAAIAAAABCAYAAAD0In+KAAAAEUlEQVR4nGNwaDjQ8F9B4D8AEgQEL4L2foYAAAAASUVORK5CYII=";
    const auto buffer = Geometry();
    std::string json = R"JSON({
      "asset":{"version":"2.0"},
      "scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],
      "buffers":[{"byteLength":102,"uri":"data:application/octet-stream;base64,)JSON" + Base64(buffer) + R"JSON("}],
      "bufferViews":[
        {"buffer":0,"byteOffset":0,"byteLength":36,"target":34962},
        {"buffer":0,"byteOffset":36,"byteLength":36,"target":34962},
        {"buffer":0,"byteOffset":72,"byteLength":24,"target":34962},
        {"buffer":0,"byteOffset":96,"byteLength":6,"target":34963}],
      "accessors":[
        {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
        {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
        {"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
        {"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}],
      "images":[{"name":"EmbeddedTwoPixels","uri":"data:image/png;base64,)JSON" + png + R"JSON("}],
      "textures":[{"source":0}],
      "materials":[
        {"name":"BlendEmbedded","alphaMode":"BLEND","doubleSided":true,
         "pbrMetallicRoughness":{"baseColorFactor":[0.8,0.6,0.4,0.5],"baseColorTexture":{"index":0},
           "metallicRoughnessTexture":{"index":0},"metallicFactor":0.8,"roughnessFactor":0.6},
         "normalTexture":{"index":0,"scale":0.7},"occlusionTexture":{"index":0,"strength":0.5},
         "emissiveTexture":{"index":0},"emissiveFactor":[0.2,0.4,0.8]},
        {"name":"MaskFactor","alphaMode":"MASK","alphaCutoff":0.4,
         "pbrMetallicRoughness":{"baseColorFactor":[1,1,1,0.75]}},
        {"name":"AoDisabled","occlusionTexture":{"index":0,"strength":0}},
        {"name":"EmissionDefaultZero","emissiveTexture":{"index":0}},
        {"name":"OrmDefaultFactors","pbrMetallicRoughness":{"metallicRoughnessTexture":{"index":0}}},
        {"name":"OcclusionDefault","occlusionTexture":{"index":0}}
      ],"meshes":[{"primitives":[)JSON";
    for (int material = 0; material < 6; ++material) {
        if (material) json += ',';
        json += "{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2},\"indices\":3,\"material\":" +
            std::to_string(material) + '}';
    }
    json += "]}]}";
    const auto path = directory / "embedded_materials.gltf";
    Write(path, json);
    return path;
}

const Model::PbrMaterial& Material(const Model::StaticModel& model, const char* name) {
    for (const auto& material : model.materials) if (material.name == name) return material;
    throw std::runtime_error(std::string("glTF material was lost: ") + name);
}

const Model::TextureImage& Texture(const Model::StaticModel& model, const Model::PbrMaterial& material, int slot) {
    const auto index = material.textures[slot];
    if (index < 0 || std::size_t(index) >= model.textures.size())
        throw std::runtime_error("Expected imported texture slot is missing");
    return model.textures[index];
}

float Sample(const Model::TextureImage& image, std::size_t pixel, std::size_t channel = 0) {
    if (image.width != 2 || image.height != 1) throw std::runtime_error("Embedded texture dimensions changed");
    return image.rgba.empty() ? image.rgba32f.at(pixel * 4 + channel) : image.rgba.at(pixel * 4 + channel) / 255.0f;
}

float SrgbToLinear(float value) { return std::pow((value + 0.055f) / 1.055f, 2.4f); }

void Test(const std::filesystem::path& directory) {
    const auto path = Fixture(directory);
    Model::LoadOptions options;
    options.strictTextures = true;
    std::string error;
    const auto model = Model::StaticModelLoader::Load(path, options, &error);
    if (!model) throw std::runtime_error("Embedded glTF import failed: " + error);
    Expect(model->TriangleCount() == 6, "data-URI geometry buffer preserves all six material primitives");

    const auto& blend = Material(*model, "BlendEmbedded");
    Expect(blend.alphaMode == Model::AlphaMode::Blend && Near(blend.baseColor.a, 0.5f),
        "BLEND baseColor alpha factor is applied exactly once");
    Expect(Near(blend.baseColor.r, .8f) && Near(blend.baseColor.g, .6f) && Near(blend.baseColor.b, .4f),
        "linear baseColor factors remain separate from the sRGB image");
    Expect(blend.twoSided && Near(blend.normalScale, .7f), "double-sided and normal scale reach the PBR material");
    const auto& albedo = Texture(*model, blend, 0);
    Expect(Near(Sample(albedo, 0, 0), 64 / 255.f) && Near(Sample(albedo, 0, 1), 128 / 255.f) &&
        Near(Sample(albedo, 0, 2), 192 / 255.f) && Near(Sample(albedo, 0, 3), 128 / 255.f) &&
        Near(Sample(albedo, 1, 0), 1) && Near(Sample(albedo, 1, 3), 1),
        "embedded compressed PNG keeps RGB and linear alpha without sidecar files");

    const auto& metallic = Texture(*model, blend, 2);
    const auto& roughness = Texture(*model, blend, 3);
    Expect(Near(Sample(metallic, 0), .8f * 192 / 255.f) && Near(Sample(metallic, 1), .8f * 16 / 255.f),
        "packed metallic uses blue and includes the material factor once");
    Expect(Near(Sample(roughness, 0), .6f * 128 / 255.f) && Near(Sample(roughness, 1), .6f * 32 / 255.f),
        "packed roughness uses green and includes the material factor once");
    const auto& occlusion = Texture(*model, blend, 4);
    Expect(Near(Sample(occlusion, 0), 1 + .5f * (64 / 255.f - 1)) && Near(Sample(occlusion, 1), 1),
        "occlusion strength blends sampled red toward one instead of multiplying AO");

    const auto& mask = Material(*model, "MaskFactor");
    Expect(mask.alphaMode == Model::AlphaMode::Mask && Near(mask.baseColor.a, .75f) && Near(mask.alphaCutoff, .4f),
        "MASK preserves its alpha factor and alpha cutoff");
    const auto& aoDisabled = Texture(*model, Material(*model, "AoDisabled"), 4);
    Expect(Near(Sample(aoDisabled, 0), 1) && Near(Sample(aoDisabled, 1), 1),
        "zero occlusion strength gives unoccluded white for every source texel");
    const auto& aoDefault = Texture(*model, Material(*model, "OcclusionDefault"), 4);
    Expect(Near(Sample(aoDefault, 0), 64 / 255.f) && Near(Sample(aoDefault, 1), 1),
        "omitted occlusion strength defaults to the red sample");

    const auto& defaultOrm = Material(*model, "OrmDefaultFactors");
    Expect(Near(Sample(Texture(*model, defaultOrm, 2), 0), 192 / 255.f) &&
        Near(Sample(Texture(*model, defaultOrm, 3), 0), 128 / 255.f),
        "omitted metallic and roughness factors both default to one");
    const auto& emission = Texture(*model, blend, 7);
    Expect(Near(blend.emissive.r, .2f) && Near(blend.emissive.g, .4f) && Near(blend.emissive.b, .8f) &&
        Near(Sample(emission, 0), SrgbToLinear(64 / 255.f)) && Near(Sample(emission, 1), 1),
        "emissive image is decoded to linear while its nonzero factor stays intact");
    const auto& defaultEmission = Material(*model, "EmissionDefaultZero");
    Expect(defaultEmission.emissive == glm::vec3(0) && defaultEmission.textures[7] >= 0,
        "an emissive image with default zero factor does not emit light");
}
} // namespace

int main() {
    try {
        const auto run = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        const auto directory = std::filesystem::path(MODEL_SOURCE_DIR) / "build/model_gltf_fixtures" / std::to_string(run);
        std::filesystem::create_directories(directory);
        std::cout << "glTF PBR material regression\n";
        Test(directory);
    } catch (const std::exception& error) {
        std::cerr << "glTF material regression failed: " << error.what() << '\n';
        ++failures;
    }
    std::cout << "glTF PBR material regression: " << (failures ? "FAILED" : "PASSED") << " (" << failures << " failures)\n";
    return failures ? 1 : 0;
}
