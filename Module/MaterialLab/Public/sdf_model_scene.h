#pragma once
#include "Model/Public/static_model.h"
#include "Render/Public/Pipeline/probe_sdf_scene.h"
#include "Render/Public/Pipeline/material_render_pipeline.h"
#include <array>

namespace MaterialLab {
struct SdfModelTopology {
    std::uint64_t boundaryEdges = 0, nonManifoldEdges = 0, weldedVertices = 0;
    bool closed = false;
};
struct SdfModelGeometry {
    // Keep the imported model immutable and shared between scene instances.
    // Raster placement must be worldPlacement * normalization; its original
    // node transforms are applied by ModelResource::AppendTo exactly once.
    std::shared_ptr<const Model::StaticModel> sourceModel;
    glm::dmat4 normalization{1};
    std::vector<Render::ProbeTriangle> triangles;
    Model::Bounds localBounds;
    SdfModelTopology topology;
    std::uint64_t sourceTriangles = 0, degenerateTriangles = 0, transparentTriangles = 0;
};
struct SdfModelLoadOptions {
    Model::LoadOptions importer;
    glm::dmat4 sourceBasis{1}; // optional source-axis conversion, applied once
    double targetHeight = 2.2;
    bool excludeBlended = true, requireClosed = false;
};
struct SdfModelVariants {
    // Low / medium / high IMPORTED geometry, distinct from 24/40/64 MDF quality.
    std::array<std::shared_ptr<const SdfModelGeometry>, 3> variants;
    glm::dmat4 normalization{1}; // shared high-model reference transform
    Model::Bounds referenceBounds;
};
// CPU-only extraction applies accumulated double node transforms once, then
// the shared normalization, before converting small local triangles to float.
std::shared_ptr<const SdfModelGeometry> FlattenSdfModel(
    std::shared_ptr<const Model::StaticModel> model, const glm::dmat4& normalization,
    bool excludeBlended = true, bool requireClosed = false);
std::shared_ptr<const SdfModelVariants> LoadSdfModelVariants(
    const std::array<std::filesystem::path, 3>& paths, const SdfModelLoadOptions& options = {},
    std::string* error = nullptr);
std::array<std::filesystem::path, 3> DefaultSdfModelVariantPaths(const std::filesystem::path& repositoryRoot);

struct SdfModelObject {
    std::string name;
    glm::vec3 position{0}, scale{1};
    float yaw = 0;
    bool movable = false;
};
struct SdfModelSceneSettings { float movableOffset = 0; };
// Twelve shared-model instances arranged into near/middle/far rows.
const std::vector<SdfModelObject>& SdfModelBanquetObjects();
glm::mat4 SdfModelObjectTransform(const SdfModelObject&, const SdfModelSceneSettings& = {});
Render::PipelineCamera SdfModelCameraPreset(unsigned preset, float aspect);
Render::ProbeGlobalSdfSettings SdfModelGlobalSettings();
} // namespace MaterialLab
