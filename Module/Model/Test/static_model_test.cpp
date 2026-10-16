#include "Model/Public/static_model.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

#ifndef MODEL_SOURCE_DIR
#error MODEL_SOURCE_DIR must identify the repository root for the real-asset regression.
#endif

namespace {
namespace fs = std::filesystem;
using Model::StaticModel;
using Model::StaticModelLoader;

int failures = 0;

bool Expect(bool condition, const char* description) {
    std::printf("  %-76s %s\n", description, condition ? "ok" : "FAIL");
    failures += !condition;
    return condition;
}

bool Near(double actual, double expected, double tolerance = 1e-5) {
    return std::abs(actual - expected) <= tolerance;
}

bool Near(const glm::dvec3& actual, const glm::dvec3& expected, double tolerance = 1e-5) {
    return Near(actual.x, expected.x, tolerance) && Near(actual.y, expected.y, tolerance)
        && Near(actual.z, expected.z, tolerance);
}

void Write(const fs::path& file, const std::string& contents) {
    fs::create_directories(file.parent_path());
    std::ofstream stream(file, std::ios::binary);
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!stream) throw std::runtime_error("Could not write a model test fixture.");
}

// A 2x2 top-origin TGA with deliberately different rows and columns. This
// catches accidental global stb flips and texture orientation regressions.
void WriteTexture(const fs::path& file) {
    std::string bytes(18, '\0');
    bytes[2] = 2; // uncompressed true color
    bytes[12] = bytes[14] = 2;
    bytes[16] = 32;
    bytes[17] = 0x28; // top row first, eight alpha bits
    constexpr std::array<std::array<unsigned char, 4>, 4> pixels{{
        {240, 30, 10, 255}, {30, 220, 50, 255},
        {10, 50, 220, 255}, {190, 120, 70, 255}}};
    for (const auto& rgba : pixels) {
        bytes.push_back(static_cast<char>(rgba[2]));
        bytes.push_back(static_cast<char>(rgba[1]));
        bytes.push_back(static_cast<char>(rgba[0]));
        bytes.push_back(static_cast<char>(rgba[3]));
    }
    Write(file, bytes);
}

void WriteObj(const fs::path& directory, const fs::path& filename = "surface.obj") {
    Write(directory / filename,
        "mtllib surface.mtl\n"
        "o DetailedSurface\n"
        "v 0 0 0\nv 2 0 0\nv 2 3 0\nv 0 3 0\nv -1 1 2\n"
        "vt 0.17 0.26\nvt 0.83 0.21\nvt 0.92 0.91\nvt 0.08 0.87\nvt 0.31 0.64\n"
        "vn 0 0.6 0.8\nvn -0.8 0 0.6\n"
        "usemtl Paint\nf 1/1/1 2/2/1 3/3/1 4/4/1\n"
        "usemtl Glass\nf 1/1/2 4/4/2 5/5/2\n");
    Write(directory / "surface.mtl",
        "newmtl Paint\nKd 0.8 0.5 0.25\nNs 98\nd 1\nmap_Kd albedo.tga\n"
        "newmtl Glass\nKd 0.2 0.7 0.9\nNs 6\nd 0.4\n");
    WriteTexture(directory / "albedo.tga");
}

std::shared_ptr<const StaticModel> Load(const fs::path& file,
    const Model::LoadOptions& options = {}) {
    std::string error;
    auto result = StaticModelLoader::Load(file, options, &error);
    if (!result) std::printf("    Import error: %s\n", error.c_str());
    return result;
}

bool ValidData(const StaticModel& model) {
    if (model.meshes.empty() || model.instances.empty() || model.materials.empty()
        || !model.bounds.valid) return false;
    for (const auto& mesh : model.meshes) {
        if (mesh.vertices.empty() || mesh.indices.empty() || mesh.indices.size() % 3
            || mesh.materialIndex >= model.materials.size() || !mesh.bounds.valid) return false;
        for (const auto index : mesh.indices) if (index >= mesh.vertices.size()) return false;
        for (const auto& vertex : mesh.vertices) {
            for (int i = 0; i < 3; ++i)
                if (!std::isfinite(vertex.position[i]) || !std::isfinite(vertex.normal[i])) return false;
            for (int i = 0; i < 2; ++i) if (!std::isfinite(vertex.uv[i])) return false;
            for (int i = 0; i < 4; ++i) if (!std::isfinite(vertex.tangent[i])) return false;
        }
    }
    for (const auto& instance : model.instances) {
        if (instance.meshIndex >= model.meshes.size()) return false;
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                if (!std::isfinite(instance.transform[column][row])) return false;
    }
    for (const auto& material : model.materials)
        for (const auto texture : material.textures)
            if (texture < -1 || (texture >= 0 && static_cast<std::size_t>(texture) >= model.textures.size()))
                return false;
    for (const auto& texture : model.textures) {
        const auto count = static_cast<std::size_t>(texture.width) * texture.height * 4;
        if (!count || !((texture.rgba.size() == count && texture.rgba32f.empty())
            || (texture.rgba32f.size() == count && texture.rgba.empty()))) return false;
    }
    return true;
}

void TestObj(const fs::path& directory) {
    std::puts("OBJ / MTL: topology, source attributes and PBR conversion");
    WriteObj(directory);
    const auto model = Load(directory / "surface.obj");
    if (!Expect(model != nullptr, "textured OBJ imports")) return;
    Expect(ValidData(*model), "all indices, materials, instances and image buffers are valid");
    Expect(model->TriangleCount() == 3, "quad triangulation and second material retain all three triangles");
    Expect(Near(model->bounds.minimum, {-1, 0, 0}) && Near(model->bounds.maximum, {2, 3, 2}),
        "scene bounds include every material group");

    const Model::PbrMaterial* paint = nullptr;
    const Model::PbrMaterial* glass = nullptr;
    std::size_t paintTriangles = 0, glassTriangles = 0;
    bool preservedAttributes = false;
    bool validTangents = true;
    bool correctHandedness = true;
    for (const auto& mesh : model->meshes) {
        const auto& material = model->materials.at(mesh.materialIndex);
        if (material.name == "Paint") { paint = &material; paintTriangles += mesh.indices.size() / 3; }
        if (material.name == "Glass") { glass = &material; glassTriangles += mesh.indices.size() / 3; }
        for (const auto& vertex : mesh.vertices) {
            const auto tangent = glm::vec3(vertex.tangent);
            validTangents &= Near(glm::length(tangent), 1.0, 1e-3)
                && std::abs(glm::dot(vertex.normal, tangent)) < 1e-3
                && Near(std::abs(vertex.tangent.w), 1.0);
            if (material.name == "Paint" && Near(vertex.position, {0, 0, 0}))
                preservedAttributes = Near(vertex.uv.x, .17) && Near(vertex.uv.y, .74)
                    && Near(vertex.normal.y, .6) && Near(vertex.normal.z, .8);
        }
        // Independently derive dP/dV from each face's positions and UVs. Unit
        // tangents alone cannot catch a lost/mirrored bitangent sign.
        for (std::size_t face = 0; face < mesh.indices.size(); face += 3) {
            const auto& a = mesh.vertices[mesh.indices[face]];
            const auto& b = mesh.vertices[mesh.indices[face + 1]];
            const auto& c = mesh.vertices[mesh.indices[face + 2]];
            const auto uv1 = b.uv - a.uv, uv2 = c.uv - a.uv;
            const double determinant = uv1.x * uv2.y - uv1.y * uv2.x;
            if (std::abs(determinant) > 1e-8) {
                const auto bitangent = ((c.position - a.position) * double(uv1.x)
                    - (b.position - a.position) * double(uv2.x)) / determinant;
                const auto reconstructed = glm::dvec3(glm::cross(a.normal, glm::vec3(a.tangent)) * a.tangent.w);
                const double agreement = glm::dot(reconstructed, bitangent);
                if (agreement <= 0)
                    std::printf("    %s face %zu: tangent=(%.5f,%.5f,%.5f,%.0f), derivative dot=%.6f\n",
                        material.name.c_str(), face / 3, a.tangent.x, a.tangent.y, a.tangent.z, a.tangent.w, agreement);
                correctHandedness &= agreement > 0;
            }
        }
    }
    Expect(paint && glass && paintTriangles == 2 && glassTriangles == 1,
        "distinct source materials remain attached to the correct faces");
    Expect(preservedAttributes, "asymmetric UVs and authored normals survive (default V flip)");
    Expect(validTangents, "generated tangents are unit, orthogonal, and retain handedness");
    Expect(correctHandedness, "bitangent signs agree with independent asymmetric-UV face derivatives");
    if (paint) {
        Expect(Near(paint->baseColor.r, .8) && Near(paint->baseColor.g, .5)
            && Near(paint->baseColor.b, .25) && Near(paint->metallic, 0)
            && Near(paint->roughness, std::sqrt(2.0 / 100.0), 1e-4),
            "legacy diffuse and shininess convert to dielectric PBR factors");
        const auto index = paint->textures[0];
        if (Expect(index >= 0, "diffuse texture populates the Material albedo slot")) {
            const auto& image = model->textures.at(static_cast<std::size_t>(index));
            Expect(image.width == 2 && image.height == 2 && image.rgba.size() == 16
                && image.rgba[0] == 240 && image.rgba[1] == 30 && image.rgba[2] == 10
                && image.rgba[8] == 10 && image.rgba[9] == 50 && image.rgba[10] == 220,
                "decoded RGBA pixels retain asymmetric top-row-first image order");
        }
    }
    if (glass) Expect(Near(glass->baseColor.a, .4) && glass->alphaMode == Model::AlphaMode::Blend,
        "source opacity selects a blended PBR material");

    Model::LoadOptions nativeUvs;
    nativeUvs.flipUVs = false;
    const auto unflipped = Load(directory / "surface.obj", nativeUvs);
    bool sourceUvs = false;
    if (unflipped) for (const auto& mesh : unflipped->meshes) for (const auto& vertex : mesh.vertices)
        sourceUvs |= Near(vertex.position, {0, 0, 0}) && Near(vertex.uv.x, .17) && Near(vertex.uv.y, .26);
    Expect(sourceUvs, "flipUVs=false retains source V coordinates");
}

void TestLargeMesh(const fs::path& directory) {
    std::puts("High-detail geometry: UInt32 indices");
    constexpr std::uint32_t triangles = 21846;
    constexpr std::uint32_t vertices = triangles * 3; // exceeds 65535
    std::string obj;
    obj.reserve(1900000);
    for (std::uint32_t triangle = 0; triangle < triangles; ++triangle) {
        const auto x = triangle * 2;
        obj += "v " + std::to_string(x) + " 0 0\nv " + std::to_string(x + 1)
            + " 0 0\nv " + std::to_string(x) + " 1 0\n";
    }
    for (std::uint32_t first = 1; first <= vertices; first += 3)
        obj += "f " + std::to_string(first) + " " + std::to_string(first + 1)
            + " " + std::to_string(first + 2) + "\n";
    Write(directory / "large.obj", obj);
    const auto model = Load(directory / "large.obj");
    if (!Expect(model != nullptr, "mesh with 65538 distinct vertices imports")) return;
    Expect(model->meshes.size() == 1 && model->VertexCount() == vertices
        && model->TriangleCount() == triangles, "high-detail mesh is retained without decimation or splitting");
    std::uint32_t largest = 0;
    for (const auto& mesh : model->meshes) for (const auto index : mesh.indices) largest = std::max(largest, index);
    Expect(largest == vertices - 1 && ValidData(*model), "indices above 65535 retain their full UInt32 value");
}

void TestFbx(const fs::path& directory) {
    std::puts("FBX: repeated geometry, nested transforms and scene bounds");
    // Self-contained ASCII FBX 7.4: two mesh nodes under a translated parent.
    // Both nodes connect to the same source Geometry, so transforms must be
    // represented as scene instances instead of discarded during import.
    Write(directory / "hierarchy.fbx", R"FBX(; FBX 7.4.0 project file
FBXHeaderExtension: {
    FBXHeaderVersion: 1003
    FBXVersion: 7400
    Creator: "OpenglLearn Model regression"
}
GlobalSettings: {
    Version: 1000
    Properties70: {
        P: "UpAxis", "int", "Integer", "",1
        P: "UpAxisSign", "int", "Integer", "",1
        P: "FrontAxis", "int", "Integer", "",2
        P: "FrontAxisSign", "int", "Integer", "",1
        P: "CoordAxis", "int", "Integer", "",0
        P: "CoordAxisSign", "int", "Integer", "",1
        P: "UnitScaleFactor", "double", "Number", "",1
    }
}
Definitions: {
    Version: 100
    Count: 5
    ObjectType: "Geometry" { Count: 1 }
    ObjectType: "Model" { Count: 3 }
    ObjectType: "Material" { Count: 1 }
}
Objects: {
    Geometry: 100, "Geometry::SharedTriangle", "Mesh" {
        GeometryVersion: 124
        Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 }
        PolygonVertexIndex: *3 { a: 0,1,-3 }
        LayerElementNormal: 0 {
            Version: 101
            Name: ""
            MappingInformationType: "ByPolygonVertex"
            ReferenceInformationType: "Direct"
            Normals: *9 { a: 0,0,1,0,0,1,0,0,1 }
        }
        LayerElementMaterial: 0 {
            Version: 101
            Name: ""
            MappingInformationType: "AllSame"
            ReferenceInformationType: "IndexToDirect"
            Materials: *1 { a: 0 }
        }
        Layer: 0 {
            Version: 100
            LayerElement: {
                Type: "LayerElementNormal"
                TypedIndex: 0
            }
            LayerElement: {
                Type: "LayerElementMaterial"
                TypedIndex: 0
            }
        }
    }
    Model: 200, "Model::Nested", "Mesh" {
        Version: 232
        Properties70: {
            P: "Lcl Translation", "Lcl Translation", "", "A",1,2,3
            P: "Lcl Rotation", "Lcl Rotation", "", "A",0,0,0
            P: "Lcl Scaling", "Lcl Scaling", "", "A",1,1,1
        }
        Shading: T
        Culling: "CullingOff"
    }
    Model: 201, "Model::Sibling", "Mesh" {
        Version: 232
        Properties70: {
            P: "Lcl Translation", "Lcl Translation", "", "A",-2,0,0
            P: "Lcl Rotation", "Lcl Rotation", "", "A",0,0,0
            P: "Lcl Scaling", "Lcl Scaling", "", "A",1,1,1
        }
        Shading: T
        Culling: "CullingOff"
    }
    Model: 202, "Model::Parent", "Null" {
        Version: 232
        Properties70: {
            P: "Lcl Translation", "Lcl Translation", "", "A",10,20,30
            P: "Lcl Rotation", "Lcl Rotation", "", "A",0,0,0
            P: "Lcl Scaling", "Lcl Scaling", "", "A",1,1,1
        }
    }
    Material: 300, "Material::Paint", "" {
        Version: 102
        ShadingModel: "phong"
        MultiLayer: 0
        Properties70: { P: "DiffuseColor", "Color", "", "A",0.8,0.5,0.25 }
    }
}
Connections: {
    C: "OO",100,200
    C: "OO",100,201
    C: "OO",200,202
    C: "OO",201,202
    C: "OO",202,0
    C: "OO",300,200
    C: "OO",300,201
}
)FBX");
    const auto model = Load(directory / "hierarchy.fbx");
    if (!Expect(model != nullptr, "ASCII FBX imports through the common model loader")) return;
    Expect(ValidData(*model) && model->instances.size() == 2,
        "both occurrences of shared source geometry become valid instances");
    bool nested = false, sibling = false;
    for (const auto& instance : model->instances) {
        const auto translation = glm::dvec3(instance.transform[3]);
        nested |= instance.nodeName == "Nested" && Near(translation, {11, 22, 33});
        sibling |= instance.nodeName == "Sibling" && Near(translation, {8, 20, 30});
    }
    Expect(nested && sibling, "instance transforms accumulate parent and child translations");
    Expect(Near(model->bounds.minimum, {8, 20, 30}) && Near(model->bounds.maximum, {12, 23, 33}),
        "world bounds include all transformed instances");
}

void TestErrorsAndPaths(const fs::path& directory) {
    std::puts("Errors, texture policy, directory selection and Unicode paths");
    std::string error;
    Expect(!StaticModelLoader::Load(directory / "does_not_exist.obj", {}, &error) && !error.empty(),
        "missing model returns an actionable error");
    Write(directory / "unknown.unsupported", "This is not a model.\n");
    error.clear();
    Expect(!StaticModelLoader::Load(directory / "unknown.unsupported", {}, &error) && !error.empty(),
        "unsupported model reports failure without a partial model");
    Expect(StaticModelLoader::SupportsExtension(".OBJ") && StaticModelLoader::SupportsExtension(".fbx")
        && StaticModelLoader::SupportsExtension(".pmx") && !StaticModelLoader::SupportsExtension(".unsupported"),
        "required formats and case-insensitive extensions are advertised");

    Write(directory / "missing.obj", "mtllib missing.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl Missing\nf 1 2 3\n");
    Write(directory / "missing.mtl", "newmtl Missing\nKd 1 1 1\nmap_Kd missing_image.tga\n");
    const auto relaxed = Load(directory / "missing.obj");
    bool warnedAboutImage = false;
    if (relaxed) for (const auto& warning : relaxed->warnings)
        warnedAboutImage |= warning.find("missing_image.tga") != std::string::npos;
    Expect(relaxed && warnedAboutImage && ValidData(*relaxed),
        "missing texture warns and retains a renderable fallback material");
    Model::LoadOptions strict;
    strict.strictTextures = true;
    error.clear();
    Expect(!StaticModelLoader::Load(directory / "missing.obj", strict, &error) && !error.empty(),
        "strictTextures rejects the same missing texture");
    WriteTexture(directory / "search" / "missing_image.tga");
    strict.textureSearchPaths.push_back(directory / "search");
    const auto searched = Load(directory / "missing.obj", strict);
    bool resolved = false;
    if (searched) for (const auto& material : searched->materials) resolved |= material.textures[0] >= 0;
    Expect(searched && resolved, "explicit search paths resolve textures in strict mode");

    const auto unicode = directory / fs::path(u8"模型_路径") / fs::path(u8"人物.obj");
    WriteObj(unicode.parent_path(), unicode.filename());
    const auto unicodeModel = Load(unicode);
    Expect(unicodeModel && unicodeModel->TriangleCount() == 3 && !unicodeModel->textures.empty(),
        "Unicode model directory and filename preserve dependent MTL and texture loading");
    const auto selected = Load(unicode.parent_path());
    Expect(selected && selected->TriangleCount() == 3, "directory with one model resolves deterministically");
    WriteObj(directory / "ambiguous", "first.obj");
    fs::copy_file(directory / "ambiguous" / "first.obj", directory / "ambiguous" / "second.obj");
    error.clear();
    Expect(!StaticModelLoader::Load(directory / "ambiguous", {}, &error) && !error.empty(),
        "ambiguous directory asks the caller to identify a model file");
}

void TestRikka(const fs::path& root) {
    std::puts("Repository PMX asset: Asset/Model/Rikka_Uniform");
    const auto folder = root / "Asset" / "Model" / "Rikka_Uniform";
    const auto model = Load(folder);
    if (!Expect(model != nullptr, "real Rikka_Uniform directory imports with its external PNG")) return;
    std::printf("    Rikka: %zu meshes, %zu instances, %llu vertices, %llu triangles, %zu materials, %zu textures\n",
        model->meshes.size(), model->instances.size(), static_cast<unsigned long long>(model->VertexCount()),
        static_cast<unsigned long long>(model->TriangleCount()), model->materials.size(), model->textures.size());
    Expect(ValidData(*model), "PMX indices, static geometry, materials and scene bounds are valid");
    // These counts come from the checked-in PMX (6992 source vertices and
    // 26235 source indices). Importers may split vertices at material seams.
    Expect(model->meshes.size() == 16 && model->materials.size() == 16
        && model->VertexCount() >= 6992 && model->TriangleCount() == 8745,
        "all 16 material groups and 8745 source triangles survive import");
    bool atlas = false, nativeUv = false, mapped = false;
    for (const auto& image : model->textures) atlas |= image.width == 2048 && image.height == 1024;
    for (const auto& material : model->materials) mapped |= material.textures[0] >= 0;
    for (const auto& mesh : model->meshes) for (const auto& vertex : mesh.vertices)
        if (Near(vertex.position.x, .8262472) && Near(vertex.position.y, 7.864573)
            && Near(std::abs(vertex.position.z), .9308547))
            nativeUv |= Near(vertex.uv.x, .6325455) && Near(vertex.uv.y, .780285);
    Expect(atlas && mapped, "2048x1024 PNG atlas reaches imported PBR albedo slots");
    Expect(nativeUv, "PMX source UV orientation is retained for the real character atlas");
}
} // namespace

int main() {
    static_assert(std::is_same_v<decltype(Model::StaticVertex::position), glm::dvec3>);
    static_assert(std::is_same_v<decltype(Model::StaticMesh::indices)::value_type, std::uint32_t>);
    try {
        const fs::path root = MODEL_SOURCE_DIR;
        const auto identifier = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        const auto fixtures = root / "build" / "model_test_fixtures" / std::to_string(identifier);
        fs::create_directories(fixtures);
        std::printf("Model CPU regression (fixtures retained in build/model_test_fixtures/%lld)\n",
            static_cast<long long>(identifier));
        Model::Bounds precise;
        precise.Include({1000000000000.25, 0, 0});
        precise.Include({1000000000000.5, 1, 1});
        Expect(precise.valid && Near(precise.Extent().x, .25),
            "CPU bounds retain double precision at large world coordinates");
        TestObj(fixtures / "obj");
        TestLargeMesh(fixtures / "large");
        TestFbx(fixtures / "fbx");
        TestErrorsAndPaths(fixtures / "paths");
        TestRikka(root);
    } catch (const std::exception& exception) {
        std::printf("FAIL: unexpected test exception: %s\n", exception.what());
        ++failures;
    }
    std::printf("Model CPU regression: %s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
