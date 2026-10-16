#include "Render/Public/Pipeline/probe_sdf_scene.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

namespace {
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
std::vector<Render::ProbeTriangle> Cube(std::uint32_t subdivisions) {
    std::vector<Render::ProbeTriangle> triangles;
    for (int axis = 0; axis != 3; ++axis) for (int side = 0; side != 2; ++side) {
        const int u = (axis + 1) % 3, v = (axis + 2) % 3;
        const auto point = [&](std::uint32_t x, std::uint32_t y) {
            glm::vec3 p(0); p[axis] = side ? .5f : -.5f; p[u] = float(x) / subdivisions - .5f; p[v] = float(y) / subdivisions - .5f; return p;
        };
        for (std::uint32_t y = 0; y < subdivisions; ++y) for (std::uint32_t x = 0; x < subdivisions; ++x) {
            const auto a = point(x, y), b = point(x + 1, y), c = point(x + 1, y + 1), d = point(x, y + 1);
            triangles.push_back({a, b, c}); triangles.push_back({a, c, d});
        }
    }
    return triangles;
}
float BoxSdf(glm::vec3 p) {
    const auto q = glm::abs(p) - glm::vec3(.5f);
    return glm::length(glm::max(q, glm::vec3(0))) + std::min(std::max({q.x, q.y, q.z}), 0.f);
}
void EqualFields(const Render::ProbeSdfScene& a, const Render::ProbeSdfScene& b) {
    Check(a.GlobalPixels().size() == b.GlobalPixels().size(), "Global atlas size mismatch");
    for (std::size_t i = 0; i < a.GlobalPixels().size(); ++i)
        Check(glm::all(glm::lessThanEqual(glm::abs(a.GlobalPixels()[i] - b.GlobalPixels()[i]), glm::vec4(2e-6f))), "Dirty brick update differs from full union rebuild");
}
} // namespace
int main() {
    try {
        const auto cube = Cube(8); const auto mesh = Render::BakeProbeMeshSdf(cube, 28, true);
        Check(mesh->Closed(), "Watertight subdivided cube was not signed");
        Check(mesh->Sample({0, 0, 0}) < -.45f, "Closed mesh interior must be negative");
        Check(mesh->Sample({.6f, 0, 0}) > .09f, "Closed mesh exterior must be positive");
        const auto& bake = mesh->Statistics();
        const auto bruteForceTests = bake.voxels * cube.size();
        Check(bake.distanceTriangleTests < bruteForceTests / 3, "Triangle BVH did not reduce nearest-distance triangle tests");
        for (int z = -8; z <= 8; ++z) for (int y = -8; y <= 8; ++y) for (int x = -8; x <= 8; ++x) {
            const glm::vec3 p = glm::vec3(x, y, z) * .1f;
            const float bound = mesh->ConservativeDistance(p);
            Check(std::isfinite(bound) && bound >= 0 && bound <= std::abs(BoxSdf(p)) + 2e-5f, "Mesh distance advance is not conservative");
        }
        const std::array<Render::ProbeTriangle, 2> plane{{{{-.5f, 0, -.5f}, {.5f, 0, -.5f}, {.5f, 0, .5f}}, {{-.5f, 0, -.5f}, {.5f, 0, .5f}, {-.5f, 0, .5f}}}};
        const auto open = Render::BakeProbeMeshSdf(plane, 20, true);
        Check(!open->Closed(), "Open plane must fall back to unsigned distance");
        Check(open->Sample({0, .1f, 0}) > .09f && open->Sample({0, -.1f, 0}) > .09f, "Unsigned plane distance must preserve both sides");

        Render::ProbeGlobalSdfSettings settings; settings.origin = {-5, -2, -5}; settings.spacing = {.2f, .2f, .22f}; settings.counts = {51, 31, 49}; settings.brickSize = 4; settings.maxDistance = .6f;
        std::vector<Render::ProbeSdfInstance> instances{{mesh, glm::translate(glm::mat4(1), glm::vec3(-2, 0, 0)), false, 17}, {mesh, glm::translate(glm::mat4(1), glm::vec3(1, 0, 0)), true, 23}};
        auto scene = Render::BuildProbeSdfScene(instances, settings);
        Check(scene->Statistics().meshAssets == 1 && scene->Statistics().bakedVoxels == bake.voxels, "Instanced meshes did not share one bake");
        Check(scene->ObjectDistance(1, {1, 0, 0}) < -.45f, "Object transform did not preserve signed interior");
        const auto initial = scene->GlobalPixels(); const auto generation = scene->Statistics().generation, meshGeneration = scene->MeshGeneration();
        const auto geometryTopology = scene->PackedGeometry()->TopologyKey();
        Check(!scene->UpdateInstances(instances) && scene->Statistics().generation == generation, "No-op SDF update changed generation");
        Check(scene->DirtyBricks().empty() && scene->DirtyRanges().empty(), "No-op update reports dirty data");
        instances[1].localToWorld = glm::translate(glm::mat4(1), glm::vec3(2.3f, 0, .5f));
        Check(scene->UpdateInstances(instances), "Moved instance did not dirty global SDF");
        const auto movedStats = scene->Statistics();
        Check(!movedStats.fullRebuild && movedStats.updatedVoxels > 0 && movedStats.updatedVoxels < movedStats.totalVoxels / 2, "Movement unnecessarily rebuilt the entire global SDF");
        Check(movedStats.staticVoxelUpdates == 0 && movedStats.dynamicVoxelUpdates == movedStats.updatedVoxels, "Movement touched the cached static layer");
        Check(scene->MeshGeneration() == meshGeneration && scene->PackedGeometry()->TopologyKey() == geometryTopology, "Movement rebaked a mesh or rebuilt BVH topology");
        for (std::size_t i = 0; i < initial.size(); ++i) Check(initial[i].z == scene->GlobalPixels()[i].z, "Static cache changed after dynamic motion");
        EqualFields(*scene, *Render::BuildProbeSdfScene(instances, settings));
        std::uint64_t dirtyRangeVoxels = 0; for (auto range : scene->DirtyRanges()) dirtyRangeVoxels += range.count;
        Check(dirtyRangeVoxels == movedStats.updatedVoxels, "Partial upload ranges do not cover exactly the changed voxels");

        // Rotated, strongly nonuniform scale tests both atlas-domain padding
        // and the world-bounds guard used by dirty-region composition.
        instances[1].localToWorld = glm::translate(glm::mat4(1), glm::vec3(1.7f, .4f, -.3f)) * glm::rotate(glm::mat4(1), .43f, glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1), glm::vec3(2.1f, .15f, .7f));
        scene->UpdateInstances(instances); EqualFields(*scene, *Render::BuildProbeSdfScene(instances, settings));
        Check(scene->ObjectDistance(1, {1.7f, .4f, -.3f}) < 0, "Nonuniform scale lost negative interior");
        const float expectedError = mesh->InterpolationError() * .15f;
        Check(std::abs(scene->ObjectPixels()[Render::ProbeSdfObjectTexels + 9].y - expectedError) < 1e-6f, "Nonuniform mesh error must match min-scale distance units");
        const auto beforeStaticMove = scene->GlobalPixels(); instances[0].localToWorld = glm::translate(glm::mat4(1), glm::vec3(-2, .5f, 0));
        scene->UpdateInstances(instances); EqualFields(*scene, *Render::BuildProbeSdfScene(instances, settings));
        Check(scene->Statistics().dynamicVoxelUpdates == 0, "Static change touched movable cache");
        for (std::size_t i = 0; i < beforeStaticMove.size(); ++i) Check(beforeStaticMove[i].w == scene->GlobalPixels()[i].w, "Dynamic cache changed after static motion");
        std::cout << "Mesh SDF signed/open and conservative samples passed. BVH triangle tests " << bake.distanceTriangleTests << "/" << bruteForceTests
                  << ". Dynamic update " << movedStats.updatedBricks << "/" << movedStats.totalBricks << " bricks, " << movedStats.updatedVoxels << "/" << movedStats.totalVoxels
                  << " voxels in " << movedStats.updateMilliseconds << " ms. Shared bake, static cache, partial uploads, odd volume, nonuniform scale and full-union equivalence passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
