#include "MaterialLab/Public/sdf_model_scene.h"
#include "Render/Public/Pipeline/probe_sdf_lod.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

#ifndef SDF_MODEL_SOURCE_DIR
#error SDF_MODEL_SOURCE_DIR must name the repository containing the imported study models.
#endif
namespace {
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
std::shared_ptr<Model::StaticModel> Tetrahedron(double coordinate = 0) {
    auto model = std::make_shared<Model::StaticModel>(); model->materials.emplace_back();
    Model::StaticMesh mesh; mesh.name = "Precision tetrahedron";
    for (auto p : {glm::dvec3(.125, .25, .5), glm::dvec3(1, .25, .5), glm::dvec3(.125, 1, .5), glm::dvec3(.125, .25, 1)}) {
        Model::StaticVertex vertex; vertex.position = p + glm::dvec3(coordinate); mesh.vertices.push_back(vertex);
    }
    mesh.indices = {0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3}; model->meshes.push_back(mesh); model->instances.push_back({"Root", 0, glm::dmat4(1)}); return model;
}
} // namespace
int main() {
    try {
        // Large source coordinates cancel in double precision before the
        // normalized triangle conversion. A float intermediate loses them.
        auto large = Tetrahedron(1000000000.0);
        large->instances[0].transform = glm::translate(glm::dmat4(1), glm::dvec3(-2e9, -3e9, -4e9)) * glm::scale(glm::dmat4(1), glm::dvec3(2, 3, 4));
        const auto normalization = glm::scale(glm::dmat4(1), glm::dvec3(.5)) * glm::translate(glm::dmat4(1), glm::dvec3(-1, -.5, -2));
        const auto flattened = MaterialLab::FlattenSdfModel(large, normalization);
        Check(flattened->triangles.size() == 4 && flattened->topology.closed && flattened->topology.boundaryEdges == 0, "Closed imported tetrahedron lost its signed eligibility");
        const auto expected = glm::vec3(-.375f, .125f, 0);
        Check(glm::length(flattened->triangles[0].a - expected) < 1e-6f, "Node transform was applied twice or source precision was lost before normalization");
        Check(flattened->sourceModel == large && flattened->normalization == normalization, "CPU extraction did not preserve the shared original model and raster placement transform");
        auto mirrored = Tetrahedron(); mirrored->instances[0].transform = glm::scale(glm::dmat4(1), glm::dvec3(-1, 1, 1));
        const auto mirror = MaterialLab::FlattenSdfModel(mirrored, glm::dmat4(1));
        const auto original = MaterialLab::FlattenSdfModel(Tetrahedron(), glm::dmat4(1));
        const auto sourceNormal = glm::normalize(glm::cross(original->triangles[0].b - original->triangles[0].a, original->triangles[0].c - original->triangles[0].a));
        const auto mirrorNormal = glm::normalize(glm::cross(mirror->triangles[0].b - mirror->triangles[0].a, mirror->triangles[0].c - mirror->triangles[0].a));
        Check(glm::length(mirrorNormal - glm::vec3(-sourceNormal.x, sourceNormal.y, sourceNormal.z)) < 1e-6f, "Mirrored node produced an inconsistent refinement normal");
        auto opened = Tetrahedron(); opened->meshes[0].indices.resize(9);
        const auto open = MaterialLab::FlattenSdfModel(opened, glm::dmat4(1));
        Check(!open->topology.closed && open->topology.boundaryEdges == 3, "Open model was incorrectly declared signed");
        bool rejectedOpen = false; try { MaterialLab::FlattenSdfModel(opened, glm::dmat4(1), true, true); } catch (const std::invalid_argument&) { rejectedOpen = true; }
        Check(rejectedOpen, "Strict closed-model mode silently accepted a hole");
        auto duplicate = Tetrahedron(); duplicate->meshes[0].indices.insert(duplicate->meshes[0].indices.end(), {0, 2, 1});
        const auto nonmanifold = MaterialLab::FlattenSdfModel(duplicate, glm::dmat4(1));
        Check(!nonmanifold->topology.closed && nonmanifold->topology.nonManifoldEdges == 3, "Duplicate faces did not report nonmanifold topology");
        auto skipped = Tetrahedron(); skipped->meshes[0].indices.insert(skipped->meshes[0].indices.end(), {0, 0, 0});
        Model::PbrMaterial glass; glass.alphaMode = Model::AlphaMode::Blend; skipped->materials.push_back(glass);
        auto glassMesh = skipped->meshes[0]; glassMesh.materialIndex = 1; glassMesh.indices = {0, 2, 1}; skipped->meshes.push_back(glassMesh); skipped->instances.push_back({"Transparent decoration", 1, glm::dmat4(1)});
        const auto filtered = MaterialLab::FlattenSdfModel(skipped, glm::dmat4(1));
        Check(filtered->topology.closed && filtered->degenerateTriangles == 1 && filtered->transparentTriangles == 1 && filtered->triangles.size() == 4, "Degenerate or blended triangles polluted distance geometry");

        // Custom imported-study resolutions reuse a resident global field and
        // reference geometry while local MDF quality changes independently.
        const auto resident = Render::BakeProbeMeshSdf(original->triangles, 48, true);
        Render::ProbeSdfLodCache custom({24, 48, 128}); const auto customAsset = custom.RegisterMesh(original->triangles, true, resident);
        custom.Request(customAsset, Render::ProbeSdfQuality::Low); custom.Request(customAsset, Render::ProbeSdfQuality::High);
        Check(custom.WaitIdle(), "Custom imported quality cache did not finish");
        const auto lowField = custom.Ready(customAsset, Render::ProbeSdfQuality::Low), highField = custom.Ready(customAsset, Render::ProbeSdfQuality::High);
        Check(lowField && highField && lowField->Resolution() == 24 && resident->Resolution() == 48 && highField->Resolution() == 128,
            "Imported custom 24/48/128 resolution tiers were not baked accurately");
        Check(custom.Statistics().completedBakes[1] == 0 && custom.Ready(customAsset, Render::ProbeSdfQuality::Medium) == resident,
            "Custom seeded medium field was rebaked");
        Render::ProbeGlobalSdfSettings small; small.origin = {-3, -1, -3}; small.spacing = glm::vec3(.2f); small.counts = {31, 21, 31}; small.maxDistance = .6f;
        std::vector<Render::ProbeSdfInstance> instances{
            {lowField, glm::translate(glm::mat4(1), glm::vec3(-1, 0, 0)), false, 0, resident, resident},
            {lowField, glm::translate(glm::mat4(1), glm::vec3(1, 0, 0)), true, 1, resident, resident}};
        const auto scene = Render::BuildProbeSdfScene(instances, small); const auto globalBeforeLod = scene->GlobalPixels();
        const auto globalGeneration = scene->GlobalGeneration(), geometryGeneration = scene->GeometryGeneration(), meshGeneration = scene->MeshGeneration();
        const auto packedGeometry = scene->PackedGeometry(); const auto geometryTopology = packedGeometry->TopologyKey();
        instances[1].mesh = highField; Check(scene->UpdateInstances(instances), "Local quality switch did not update object metadata");
        Check(scene->MeshGeneration() > meshGeneration && scene->GlobalGeneration() == globalGeneration && scene->GeometryGeneration() == geometryGeneration && scene->PackedGeometry() == packedGeometry,
            "Local quality switch rebuilt the resident Global field or world geometry");
        Check(scene->GlobalPixels() == globalBeforeLod && scene->DirtyBricks().empty() && scene->Statistics().updatedVoxels == 0,
            "Local quality switch modified cached global voxels");
        Check(scene->ObjectPixels()[Render::ProbeSdfObjectTexels + 11].w == 128, "Local high quality did not update GPU resolution metadata");
        instances[1].localToWorld = glm::translate(glm::mat4(1), glm::vec3(1.35f, .15f, -.2f));
        Check(scene->UpdateInstances(instances) && scene->GlobalGeneration() > globalGeneration && scene->GeometryGeneration() > geometryGeneration,
            "Movable instance did not update resident global/reference geometry generations");
        Check(scene->PackedGeometry()->TopologyKey() == geometryTopology && scene->Statistics().staticVoxelUpdates == 0 && scene->Statistics().dynamicVoxelUpdates > 0,
            "Resident movable field update rebuilt BVH topology or touched static cached distances");
        const auto full = Render::BuildProbeSdfScene(instances, small);
        Check(full->GlobalPixels().size() == scene->GlobalPixels().size(), "Resident global update changed the atlas layout");
        for (std::size_t voxel = 0; voxel < full->GlobalPixels().size(); ++voxel)
            Check(glm::all(glm::lessThanEqual(glm::abs(full->GlobalPixels()[voxel] - scene->GlobalPixels()[voxel]), glm::vec4(2e-6f))),
                "Resident global dirty update differs from a fresh full composition");
        const auto movedGlobal = scene->GlobalPixels(); const auto movedGlobalGeneration = scene->GlobalGeneration(), movedGeometryGeneration = scene->GeometryGeneration();
        instances[1].mesh = resident; scene->UpdateInstances(instances);
        Check(scene->GlobalPixels() == movedGlobal && scene->GlobalGeneration() == movedGlobalGeneration && scene->GeometryGeneration() == movedGeometryGeneration,
            "Returning to a cached local quality invalidated the moved resident global/reference asset");

        // Thin-surface expansion is explicit metadata. It changes the chosen
        // iso surface, never the saved true signed samples or source triangles.
        constexpr float expansion = .8660254f;
        const auto expandedLow = Render::BakeProbeMeshSdf(original->triangles, 24, true, expansion);
        const auto expandedMedium = Render::BakeProbeMeshSdf(original->triangles, 48, true, expansion);
        Check(expandedLow->Pixels() == lowField->Pixels() && expandedMedium->Pixels() == resident->Pixels(),
            "Thin-surface bias altered the saved raw signed distance samples");
        Check(lowField->SurfaceBias() == 0 && resident->SurfaceBias() == 0 && highField->SurfaceBias() == 0,
            "Default distance assets unexpectedly enabled thin-surface expansion");
        Check(expandedLow->SurfaceBias() > expandedMedium->SurfaceBias() && expandedMedium->SurfaceBias() > 0 &&
            std::abs(expandedMedium->SurfaceBias() - expandedMedium->Spacing().x * expansion) < 1e-7f,
            "Explicit expansion did not scale with the actual local voxel size");
        Render::ProbeSdfLodCache expandedCache({24, 48, 128}, expansion);
        bool rejectedUnbiasedSeed = false;
        try { expandedCache.RegisterMesh(original->triangles, true, resident); } catch (const std::invalid_argument&) { rejectedUnbiasedSeed = true; }
        Check(rejectedUnbiasedSeed, "Expanded cache accepted a seed with incompatible surface-bias metadata");
        const auto expandedAsset = expandedCache.RegisterMesh(original->triangles, true, expandedMedium);
        expandedCache.Request(expandedAsset, Render::ProbeSdfQuality::Low);
        Check(expandedCache.WaitIdle() && expandedCache.Ready(expandedAsset, Render::ProbeSdfQuality::Low)->SurfaceBias() == expandedLow->SurfaceBias(),
            "Asynchronous quality baking discarded the configured thin-surface expansion");
        Check(expandedCache.Ready(expandedAsset, Render::ProbeSdfQuality::Low)->Pixels() == lowField->Pixels(),
            "Configured cache changed true signed sample values instead of carrying explicit bias");

        // A wide allowed expansion extends the narrow-band influence beyond
        // the unexpanded AABB. Test the expected voxel directly; comparing two
        // builds alone could hide identical mistakes in their object binning.
        const auto wide = Render::BakeProbeMeshSdf(original->triangles, 24, true, 2);
        Render::ProbeGlobalSdfSettings fine; fine.origin = {-.4f, -.1f, -.2f}; fine.spacing = glm::vec3(.02f);
        fine.counts = {111, 61, 81}; fine.brickSize = 2; fine.maxDistance = .6f;
        const std::vector<Render::ProbeSdfInstance> wideObjects{{wide, glm::mat4(1), true, 0}};
        const auto wideScene = Render::BuildProbeSdfScene(wideObjects, fine);
        const glm::uvec3 wideVoxel{104, 30, 40}; const auto widePoint = fine.origin + glm::vec3(wideVoxel) * fine.spacing;
        const float expectedWideDistance = widePoint.x - wide->BoundsMax().x - wide->SurfaceBias();
        Check(expectedWideDistance > 0 && expectedWideDistance < fine.maxDistance,
            "Wide-expansion binning fixture did not fall inside the expanded narrow band");
        Check(std::abs(wideScene->ObjectDistance(0, widePoint) - expectedWideDistance) < 2e-6f,
            "Wide-expansion object distance disagrees with its independent AABB lower-bound reference");
        const auto wideSample = wideScene->SampleGlobal(wideVoxel);
        Check(wideSample.y == 1 && std::abs(wideSample.x - expectedWideDistance) < 2e-6f,
            "Global object binning excluded an expanded narrow-band voxel beyond the unexpanded influence radius");
        auto wideMoved = wideObjects; wideMoved[0].localToWorld = glm::translate(glm::mat4(1), glm::vec3(-.2f, 0, 0));
        wideScene->UpdateInstances(wideMoved);
        Check(wideScene->SampleGlobal(wideVoxel).y == 0 && std::abs(wideScene->SampleGlobal(wideVoxel).x - fine.maxDistance) < 2e-6f,
            "Expanded dirty bounds left an old-position narrow-band distance after motion");
        wideScene->UpdateInstances(wideObjects);
        Check(wideScene->SampleGlobal(wideVoxel).y == 1 && std::abs(wideScene->SampleGlobal(wideVoxel).x - expectedWideDistance) < 2e-6f,
            "Expanded dirty bounds omitted the restored narrow-band voxel after reverse motion");

        const auto paths = MaterialLab::DefaultSdfModelVariantPaths(SDF_MODEL_SOURCE_DIR); std::string error;
        const auto models = MaterialLab::LoadSdfModelVariants(paths, {}, &error);
        if (!models) throw std::runtime_error(error);
        Check(models->variants[0]->triangles.size() < models->variants[1]->triangles.size() && models->variants[1]->triangles.size() < models->variants[2]->triangles.size(), "Imported geometry variants do not contain independently reduced triangle levels");
        for (const auto& variant : models->variants) {
            Check(variant && variant->sourceModel && variant->topology.closed && variant->topology.boundaryEdges == 0 && variant->topology.nonManifoldEdges == 0, "Imported teapot has an unverified or open triangle surface");
            Check(variant->normalization == models->normalization, "Geometry variants independently normalized and will jump in size during switching");
            Check(variant->degenerateTriangles == 0 && variant->transparentTriangles == 0, "Classic study model discarded source surfaces unexpectedly");
        }
        const auto& high = *models->variants[2];
        Check(std::abs(high.localBounds.minimum.y) < 1e-5 && std::abs(high.localBounds.Extent().y - 2.2) < 1e-5, "Shared imported normalization did not put the high mesh on its base at the requested height");
        Render::ProbeSdfLodCache cache;
        const auto shared = cache.RegisterMesh(high.triangles, high.topology.closed);
        Check(cache.RegisterMesh(high.triangles, high.topology.closed) == shared, "Imported instances generated duplicate SDF asset registrations");
        Check(cache.WaitIdle(), "Imported mesh SDF initial medium bake did not finish");
        const auto mediumField = cache.Ready(shared, Render::ProbeSdfQuality::Medium);
        Check(mediumField && mediumField->Closed() && mediumField->Geometry()->TriangleCount() == high.triangles.size(), "Imported mesh bake did not preserve the original closed triangle acceleration asset");

        const auto& objects = MaterialLab::SdfModelBanquetObjects(); const auto grid = MaterialLab::SdfModelGlobalSettings();
        Check(objects.size() == 12 && grid.spacing.x <= .125f && std::uint64_t(grid.counts.x) * grid.counts.y * grid.counts.z <= 4 * 1024 * 1024, "Imported study layout exceeded its bounded global volume");
        const auto globalMax = grid.origin + glm::vec3(grid.counts - glm::uvec3(1)) * grid.spacing;
        unsigned movable = 0;
        for (const auto& object : objects) {
            const auto transform = MaterialLab::SdfModelObjectTransform(object);
            for (const auto& triangle : high.triangles) for (auto p : {triangle.a, triangle.b, triangle.c}) {
                const auto world = glm::vec3(transform * glm::vec4(p, 1));
                Check(glm::all(glm::greaterThanEqual(world, grid.origin)) && glm::all(glm::lessThanEqual(world, globalMax)), "Imported banquet geometry exceeds the global SDF volume");
            }
            const auto moved = MaterialLab::SdfModelObjectTransform(object, {.movableOffset = .5f});
            Check(std::abs(moved[3].x - transform[3].x - (object.movable ? .5f : 0.f)) < 1e-6f, "Imported movable instance changed another object's placement"); movable += object.movable;
        }
        Check(movable == 1, "Imported study requires one independently movable shared-model instance");
        const auto contained = [&](glm::vec3 point) { return glm::all(glm::greaterThanEqual(point, grid.origin)) && glm::all(glm::lessThanEqual(point, globalMax)); };
        for (const float x : {-11.7f, 11.7f}) for (const float z : {-11.7f, 11.7f})
            Check(contained({x, 0, z}), "Banquet floor front or back edge lies outside the resident Global volume");
        for (const float depth : {4.f, -1.f, -7.f}) for (const float x : {-10.8f, 10.8f}) for (const float y : {-.05f, .85f}) for (const float z : {-1.8f, 1.8f})
            Check(contained({x, y, depth + z}), "A banquet supporting table lies outside the resident Global volume");
        for (unsigned preset = 0; preset < 3; ++preset) {
            const auto camera = MaterialLab::SdfModelCameraPreset(preset, 1.6f);
            Check(std::isfinite(camera.position.x) && camera.nearPlane > 0 && camera.farPlane > camera.nearPlane && std::abs(glm::determinant(camera.view)) > .9f, "Imported camera preset is invalid");
        }
        std::cout << "Imported SDF helper passed: double node flattening, mirror winding, closed/open topology, shared high normalization and 12 reusable banquet instances. Teapot triangles "
                  << models->variants[0]->triangles.size() << '/' << models->variants[1]->triangles.size() << '/' << models->variants[2]->triangles.size()
                  << "; shared imported Mesh SDF bake, explicit thin-surface bias, and independent 24/48/128 local / resident Global / reference geometry generations verified.\n";
        return 0;
    } catch (const std::exception& failure) { std::cerr << failure.what() << '\n'; return 1; }
}
