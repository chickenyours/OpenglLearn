#include "MaterialLab/Public/sdf_model_scene.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

namespace MaterialLab {
namespace {
bool Finite(glm::dvec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
bool Finite(const glm::dmat4& m) { for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) if (!std::isfinite(m[c][r])) return false; return true; }
void RequireAffine(const glm::dmat4& m) {
    if (!Finite(m) || std::abs(m[0].w) > 1e-10 || std::abs(m[1].w) > 1e-10 || std::abs(m[2].w) > 1e-10 || std::abs(m[3].w - 1) > 1e-10 || std::abs(glm::determinant(glm::dmat3(m))) < 1e-15)
        throw std::invalid_argument("Imported SDF model needs a finite, nonsingular affine transform");
}
std::vector<Model::MeshInstance> Instances(const Model::StaticModel& model) {
    if (!model.instances.empty()) return model.instances;
    std::vector<Model::MeshInstance> result;
    for (std::uint32_t i = 0; i < model.meshes.size(); ++i) result.push_back({model.meshes[i].name, i, glm::dmat4(1)});
    return result;
}
Model::Bounds GeometryBounds(const Model::StaticModel& model, const glm::dmat4& basis) {
    RequireAffine(basis); Model::Bounds bounds;
    for (const auto& instance : Instances(model)) {
        if (instance.meshIndex >= model.meshes.size()) throw std::invalid_argument("Imported SDF node references an invalid mesh");
        RequireAffine(instance.transform); const auto matrix = basis * instance.transform;
        const auto& mesh = model.meshes[instance.meshIndex];
        if (mesh.indices.size() % 3) throw std::invalid_argument("Imported SDF mesh contains nontriangle indices");
        for (const auto index : mesh.indices) {
            if (index >= mesh.vertices.size()) throw std::invalid_argument("Imported SDF triangle has an invalid vertex index");
            const auto point = glm::dvec3(matrix * glm::dvec4(mesh.vertices[index].position, 1));
            if (!Finite(point)) throw std::invalid_argument("Imported SDF vertex is nonfinite");
            bounds.Include(point);
        }
    }
    if (!bounds.valid) throw std::invalid_argument("Imported SDF model has no triangle geometry");
    return bounds;
}
SdfModelTopology Topology(std::span<const Render::ProbeTriangle> triangles, const Model::Bounds& bounds) {
    using Vertex = std::array<std::int64_t, 3>; using Edge = std::pair<Vertex, Vertex>;
    SdfModelTopology result; std::map<Vertex, std::uint32_t> vertices; std::map<Edge, std::uint32_t> edges;
    const double epsilon = std::max(glm::length(bounds.Extent()) * 1e-6, 1e-9);
    const auto key = [&](glm::vec3 point) -> Vertex {
        const auto p = (glm::dvec3(point) - bounds.minimum) / epsilon;
        return {std::llround(p.x), std::llround(p.y), std::llround(p.z)};
    };
    for (const auto& triangle : triangles) {
        const std::array<Vertex, 3> corners{key(triangle.a), key(triangle.b), key(triangle.c)};
        for (const auto& vertex : corners) vertices.try_emplace(vertex, std::uint32_t(vertices.size()));
        for (int i = 0; i < 3; ++i) {
            auto a = corners[i], b = corners[(i + 1) % 3];
            if (a == b) { ++result.nonManifoldEdges; continue; }
            if (b < a) std::swap(a, b); ++edges[{a, b}];
        }
    }
    for (const auto& edge : edges) { if (edge.second == 1) ++result.boundaryEdges; else if (edge.second != 2) ++result.nonManifoldEdges; }
    result.weldedVertices = vertices.size();
    result.closed = !triangles.empty() && result.boundaryEdges == 0 && result.nonManifoldEdges == 0;
    return result;
}
} // namespace

std::shared_ptr<const SdfModelGeometry> FlattenSdfModel(std::shared_ptr<const Model::StaticModel> model,
    const glm::dmat4& normalization, bool excludeBlended, bool requireClosed) {
    if (!model || model->meshes.empty()) throw std::invalid_argument("Cannot flatten an empty imported SDF model");
    RequireAffine(normalization); auto result = std::make_shared<SdfModelGeometry>();
    result->sourceModel = std::move(model); result->normalization = normalization;
    for (const auto& instance : Instances(*result->sourceModel)) {
        if (instance.meshIndex >= result->sourceModel->meshes.size()) throw std::invalid_argument("Imported SDF node references an invalid mesh");
        RequireAffine(instance.transform); const auto matrix = normalization * instance.transform;
        const bool mirrored = glm::determinant(glm::dmat3(matrix)) < 0;
        const auto& mesh = result->sourceModel->meshes[instance.meshIndex];
        if (mesh.indices.size() % 3) throw std::invalid_argument("Imported SDF mesh contains nontriangle indices");
        const Model::PbrMaterial fallback{};
        const auto& material = mesh.materialIndex < result->sourceModel->materials.size() ? result->sourceModel->materials[mesh.materialIndex] : fallback;
        result->sourceTriangles += mesh.indices.size() / 3;
        if (excludeBlended && material.alphaMode == Model::AlphaMode::Blend) { result->transparentTriangles += mesh.indices.size() / 3; continue; }
        for (std::size_t triangle = 0; triangle < mesh.indices.size(); triangle += 3) {
            std::array<glm::vec3, 3> points;
            for (int corner = 0; corner < 3; ++corner) {
                const auto index = mesh.indices[triangle + corner];
                if (index >= mesh.vertices.size()) throw std::invalid_argument("Imported SDF triangle has an invalid vertex index");
                // Apply the accumulated node transform only here; neither the
                // source bounds nor normalization contain its second copy.
                const auto point = glm::dvec3(matrix * glm::dvec4(mesh.vertices[index].position, 1));
                if (!Finite(point) || glm::any(glm::greaterThan(glm::abs(point), glm::dvec3(100000)))) throw std::invalid_argument("Normalized imported SDF vertex exceeds the finite trace range");
                points[corner] = glm::vec3(point);
            }
            if (glm::length(glm::cross(points[1] - points[0], points[2] - points[0])) <= 1e-7f) { ++result->degenerateTriangles; continue; }
            if (mirrored) std::swap(points[1], points[2]);
            Render::ProbeTriangle geometry{points[0], points[1], points[2]};
            const float metallic = std::clamp(material.metallic, 0.f, 1.f);
            const auto base = glm::clamp(glm::vec3(material.baseColor), glm::vec3(0), glm::vec3(1));
            geometry.diffuseReflectance = base * (1 - metallic); geometry.specularF0 = glm::mix(glm::vec3(.04f), base, metallic);
            geometry.roughness = std::clamp(material.roughness, 0.f, 1.f); geometry.emission = glm::clamp(material.emissive, glm::vec3(0), glm::vec3(60000));
            geometry.twoSidedEmission = material.twoSided; result->triangles.push_back(geometry);
            for (auto point : points) result->localBounds.Include(glm::dvec3(point));
        }
    }
    if (result->triangles.empty()) throw std::invalid_argument("Imported SDF model has no valid opaque triangles");
    result->topology = Topology(result->triangles, result->localBounds);
    if (requireClosed && !result->topology.closed) throw std::invalid_argument("Imported SDF model is open or nonmanifold; use unsigned distance or repair its boundary");
    return result;
}
std::shared_ptr<const SdfModelVariants> LoadSdfModelVariants(const std::array<std::filesystem::path, 3>& paths,
    const SdfModelLoadOptions& options, std::string* error) {
    try {
        if (!std::isfinite(options.targetHeight) || options.targetHeight < .05 || options.targetHeight > 100) throw std::invalid_argument("Imported SDF normalization height must be .05..100");
        RequireAffine(options.sourceBasis); std::array<std::shared_ptr<const Model::StaticModel>, 3> models;
        for (std::size_t i = 0; i < models.size(); ++i) {
            std::string loadError; models[i] = Model::StaticModelLoader::Load(paths[i], options.importer, &loadError);
            if (!models[i]) throw std::runtime_error("Cannot load SDF model tier " + std::to_string(i) + ": " + loadError);
        }
        auto result = std::make_shared<SdfModelVariants>(); result->referenceBounds = GeometryBounds(*models[2], options.sourceBasis);
        const auto extent = result->referenceBounds.Extent();
        const double height = extent.y > 1e-8 ? extent.y : std::max({extent.x, extent.y, extent.z});
        if (height <= 1e-8) throw std::invalid_argument("Imported SDF model normalization has zero extent");
        const auto center = result->referenceBounds.Center(); const glm::dvec3 origin(center.x, result->referenceBounds.minimum.y, center.z);
        result->normalization = glm::scale(glm::dmat4(1), glm::dvec3(options.targetHeight / height)) * glm::translate(glm::dmat4(1), -origin) * options.sourceBasis;
        for (std::size_t i = 0; i < models.size(); ++i) result->variants[i] = FlattenSdfModel(models[i], result->normalization, options.excludeBlended, options.requireClosed);
        if (error) error->clear(); return result;
    } catch (const std::exception& failure) { if (error) *error = failure.what(); return {}; }
}
std::array<std::filesystem::path, 3> DefaultSdfModelVariantPaths(const std::filesystem::path& root) {
    const auto directory = root / "Asset" / "Model" / "SdfBanquet";
    return {directory / "teapot_low.obj", directory / "teapot_medium.obj", directory / "teapot_high.obj"};
}
const std::vector<SdfModelObject>& SdfModelBanquetObjects() {
    static const auto objects = [] {
        std::vector<SdfModelObject> result;
        const std::array<float, 3> depths{4, -1, -7}; const std::array<float, 4> columns{-8.25f, -2.75f, 2.75f, 8.25f};
        const std::array<const char*, 3> rows{"Near", "Middle", "Far"};
        for (std::size_t row = 0; row < depths.size(); ++row) for (std::size_t column = 0; column < columns.size(); ++column)
            result.push_back({std::string(rows[row]) + " teapot " + std::to_string(column + 1), {columns[column], .85f, depths[row]}, glm::vec3(1), float(column % 2 ? -.2 : .2), row == 0 && column == 1});
        return result;
    }(); return objects;
}
glm::mat4 SdfModelObjectTransform(const SdfModelObject& object, const SdfModelSceneSettings& settings) {
    if (!std::isfinite(settings.movableOffset)) throw std::invalid_argument("Nonfinite imported SDF model offset");
    auto position = object.position; if (object.movable) position.x += settings.movableOffset;
    return glm::translate(glm::mat4(1), position) * glm::rotate(glm::mat4(1), object.yaw, glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1), object.scale);
}
Render::PipelineCamera SdfModelCameraPreset(unsigned preset, float aspect) {
    if (!std::isfinite(aspect) || aspect <= 0) throw std::invalid_argument("Imported SDF camera needs a positive aspect");
    const std::array<glm::vec3, 3> positions{{{0, 3.6f, 7.8f}, {-3.6f, 2.1f, 6.5f}, {0, 6.5f, 15}}};
    const std::array<glm::vec3, 3> targets{{{0, 1.3f, -1.5f}, {-2.75f, 1.75f, 4}, {0, 1.2f, -3}}};
    Render::PipelineCamera result; result.position = positions[preset % 3]; result.nearPlane = .05f; result.farPlane = 65;
    result.view = glm::lookAt(result.position, targets[preset % 3], glm::vec3(0, 1, 0));
    result.projection = glm::perspective(glm::radians(55.f), aspect, result.nearPlane, result.farPlane); return result;
}
Render::ProbeGlobalSdfSettings SdfModelGlobalSettings() {
    Render::ProbeGlobalSdfSettings result; result.origin = {-12, -.25f, -12}; result.spacing = glm::vec3(.125f); result.counts = {193, 45, 193}; result.brickSize = 4; result.maxDistance = 1; return result;
}
} // namespace MaterialLab
