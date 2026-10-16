#include "Render/Public/Pipeline/probe_sdf_clipmap.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

namespace {
using namespace Render;
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class Function> void Reject(Function function, const char* message) {
    bool rejected = false; try { function(); } catch (const std::exception&) { rejected = true; } Check(rejected, message);
}
std::vector<ProbeTriangle> Cube() {
    std::vector<ProbeTriangle> triangles;
    for (int axis = 0; axis != 3; ++axis) for (int side = 0; side != 2; ++side) {
        const int u = (axis + 1) % 3, v = (axis + 2) % 3;
        const auto point = [&](int x, int y) { glm::vec3 p(0); p[axis] = side ? .5f : -.5f; p[u] = x - .5f; p[v] = y - .5f; return p; };
        const auto a = point(0, 0), b = point(1, 0), c = point(1, 1), d = point(0, 1);
        triangles.push_back({a, b, c}); triangles.push_back({a, c, d});
    }
    return triangles;
}
std::size_t At(glm::uvec3 p, glm::uvec3 n) { return (std::size_t(p.z) * n.y + p.y) * n.x + p.x; }
const std::shared_ptr<const ProbeMeshSdf>& Source(const ProbeSdfInstance& instance, std::span<const ProbeSdfClipmapMeshes> fields, std::size_t object, std::uint32_t level) {
    if (!fields.empty() && fields[object][level]) return fields[object][level];
    return instance.globalMesh ? instance.globalMesh : instance.mesh;
}
// An unbinned reference, deliberately independent of clipmap dirty bounds,
// ring addresses, and cached layers. Uses the same immutable source samples.
float ReferenceDistance(const ProbeSdfInstance& instance, const ProbeMeshSdf& mesh, glm::vec3 p) {
    const auto inverse = glm::inverse(instance.localToWorld); const auto local = glm::vec3(inverse * glm::vec4(p, 1));
    const float scale = std::min({glm::length(glm::vec3(instance.localToWorld[0])), glm::length(glm::vec3(instance.localToWorld[1])), glm::length(glm::vec3(instance.localToWorld[2]))});
    const auto fieldEnd = mesh.Origin() + glm::vec3(mesh.Counts() - glm::uvec3(1)) * mesh.Spacing();
    if (glm::all(glm::greaterThanEqual(local, mesh.Origin())) && glm::all(glm::lessThanEqual(local, fieldEnd))) return (mesh.Sample(local) - mesh.SurfaceBias()) * scale;
    glm::vec3 lo(1e30f), hi(-1e30f);
    for (int z = 0; z != 2; ++z) for (int y = 0; y != 2; ++y) for (int x = 0; x != 2; ++x) {
        const auto a = mesh.BoundsMin(), b = mesh.BoundsMax();
        const auto q = glm::vec3(instance.localToWorld * glm::vec4(x ? b.x : a.x, y ? b.y : a.y, z ? b.z : a.z, 1));
        lo = glm::min(lo, q); hi = glm::max(hi, q);
    }
    const auto outside = glm::max(glm::max(lo - p, p - hi), glm::vec3(0));
    return std::max(std::max(0.f, mesh.ConservativeDistance(local) - mesh.SurfaceBias()) * scale,
                    std::max(0.f, glm::length(outside) - mesh.SurfaceBias() * scale));
}
glm::vec4 Reference(std::span<const ProbeSdfInstance> instances, std::span<const ProbeSdfClipmapMeshes> fields, std::uint32_t level, glm::vec3 p, float band) {
    glm::vec2 fixed(band, 0), moving(band, 0);
    for (std::size_t object = 0; object < instances.size(); ++object) {
        const float distance = ReferenceDistance(instances[object], *Source(instances[object], fields, object, level), p);
        auto& layer = instances[object].movable ? moving : fixed;
        if (distance < layer.x) layer = {distance, float(object + 1)};
    }
    const auto nearest = fixed.x <= moving.x ? fixed : moving;
    return {nearest.x, nearest.y, fixed.x, moving.x};
}
void Near(glm::vec4 a, glm::vec4 b, const char* message) { Check(glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec4(3e-6f))), message); }
void CheckReference(const ProbeSdfClipmapVolume& volume, std::span<const ProbeSdfInstance> instances, std::span<const ProbeSdfClipmapMeshes> fields = {}) {
    for (std::uint32_t i = 0; i < volume.Levels().size(); ++i) {
        const auto& level = volume.Levels()[i];
        for (std::uint32_t z = 0; z < level.counts.z; ++z) for (std::uint32_t y = 0; y < level.counts.y; ++y) for (std::uint32_t x = 0; x < level.counts.x; ++x) {
            const glm::uvec3 logical{x, y, z}; const auto p = level.origin + glm::vec3(logical) * level.spacing;
            Near(volume.SampleLogical(logical, i), Reference(instances, fields, i, p, level.narrowBand), "Clipmap disagrees with independent unbinned full union");
        }
    }
}
void CheckFull(const ProbeSdfClipmapVolume& volume, std::span<const ProbeSdfInstance> instances, glm::vec3 camera, std::span<const ProbeSdfClipmapMeshes> fields = {}) {
    ProbeSdfClipmapVolume full(volume.Settings()); full.Update(instances, camera, fields);
    for (std::uint32_t i = 0; i < volume.Levels().size(); ++i) {
        const auto& level = volume.Levels()[i]; Check(level.origin == full.Levels()[i].origin, "Full reference origin mismatch");
        for (std::uint32_t z = 0; z < level.counts.z; ++z) for (std::uint32_t y = 0; y < level.counts.y; ++y) for (std::uint32_t x = 0; x < level.counts.x; ++x)
            Near(volume.SampleLogical({x, y, z}, i), full.SampleLogical({x, y, z}, i), "Ring cache differs from fresh full recapture");
    }
}
void CheckHierarchy(const ProbeSdfClipmapVolume& volume, const std::vector<glm::vec4>& previous = {}) {
    std::vector<bool> changedNodes(volume.Pixels().size()), changedRanges(volume.RangePixels().size());
    for (const auto range : volume.DirtyRanges())
        for (std::uint32_t i = 0; i < range.count; ++i) changedNodes[range.offset + i] = true;
    std::uint64_t end = 0;
    for (const auto range : volume.RangeDirtyRanges()) {
        Check(range.count > 0 && range.offset >= end && std::uint64_t(range.offset) + range.count <= changedRanges.size(), "Invalid hierarchy dirty range");
        end = std::uint64_t(range.offset) + range.count;
        for (std::uint32_t i = 0; i < range.count; ++i) changedRanges[range.offset + i] = true;
    }
    const auto brick = volume.Settings().brickSize;
    for (const auto& level : volume.Levels()) {
        Check(level.rangeAtlasOffset % volume.Width() == 0 && level.rangeCounts == level.counts / brick, "Hierarchy layout disagrees with physical bricks");
        for (std::uint32_t z = 0; z < level.rangeCounts.z; ++z) for (std::uint32_t y = 0; y < level.rangeCounts.y; ++y) for (std::uint32_t x = 0; x < level.rangeCounts.x; ++x) {
            const glm::uvec3 first = glm::uvec3(x, y, z) * brick;
            float low = std::numeric_limits<float>::max(), high = -low; bool intersectsUpdate = false;
            // Independent exhaustive reduction includes all 125 corners for
            // a 4^3-cell brick, including ring wrap and shared positive faces.
            for (std::uint32_t dz = 0; dz <= brick; ++dz) for (std::uint32_t dy = 0; dy <= brick; ++dy) for (std::uint32_t dx = 0; dx <= brick; ++dx) {
                const auto node = (first + glm::uvec3(dx, dy, dz)) % level.counts;
                const auto sampleAt = level.atlasOffset + At(node, level.counts);
                const float value = volume.Pixels()[sampleAt].x;
                low = std::min(low, value); high = std::max(high, value); intersectsUpdate = intersectsUpdate || changedNodes[sampleAt];
            }
            const auto at = level.rangeAtlasOffset + At({x, y, z}, level.rangeCounts);
            Check(volume.RangePixels()[at] == glm::vec4(low, high, 0, 0), "Brick min/max misses a shared interpolation corner or retains stale data");
            Check(changedRanges[at] == intersectsUpdate, "Hierarchy dirty cover misses a shared-corner neighbor or contains an unaffected brick");
        }
    }
    if (!previous.empty()) for (std::size_t i = 0; i < changedRanges.size(); ++i)
        if (!changedRanges[i]) Check(previous[i] == volume.RangePixels()[i], "Unreported physical hierarchy texel changed");
}
void CheckParallelAgainstSerialTiles(const ProbeSdfClipmapVolume& volume, std::span<const ProbeSdfInstance> instances) {
    const auto& level = volume.Levels()[0];
    auto settings = volume.Settings(); settings.counts = glm::uvec3(16);
    // Eight separately captured 16^3 tiles stay below the parallel threshold.
    // Their world sample positions exactly cover the 32^3 parallel volume.
    for (std::uint32_t z = 0; z != 2; ++z) for (std::uint32_t y = 0; y != 2; ++y) for (std::uint32_t x = 0; x != 2; ++x) {
        const glm::uvec3 first = glm::uvec3(x, y, z) * 16u;
        const auto camera = level.origin + glm::vec3(first + glm::uvec3(8)) * level.spacing;
        ProbeSdfClipmapVolume serial(settings); serial.Update(instances, camera);
        for (std::uint32_t dz = 0; dz < 16; ++dz) for (std::uint32_t dy = 0; dy < 16; ++dy) for (std::uint32_t dx = 0; dx < 16; ++dx) {
            const glm::uvec3 node{dx, dy, dz};
            Check(volume.SampleLogical(first + node, 0) == serial.SampleLogical(node, 0), "Parallel brick update differs bit-for-bit from independent serial captures");
        }
    }
    CheckHierarchy(volume);
}
void CheckRanges(const ProbeSdfClipmapVolume& volume, const std::vector<glm::vec4>& previous) {
    std::vector<bool> dirty(volume.Pixels().size()); std::uint64_t voxels = 0, end = 0;
    for (const auto range : volume.DirtyRanges()) {
        Check(range.count > 0 && range.offset >= end && std::uint64_t(range.offset) + range.count <= dirty.size(), "Invalid dirty physical range");
        end = std::uint64_t(range.offset) + range.count; voxels += range.count;
        for (std::uint32_t i = 0; i < range.count; ++i) dirty[range.offset + i] = true;
    }
    Check(voxels == volume.Statistics().updatedVoxels, "Physical dirty ranges are not an exact nonoverlapping voxel cover");
    if (!previous.empty()) for (std::size_t i = 0; i < dirty.size(); ++i) if (!dirty[i]) Check(previous[i] == volume.Pixels()[i], "An unreported physical texel changed");
    CheckHierarchy(volume);
}
void CheckOverlap(const ProbeSdfClipmapVolume& volume, const std::vector<ProbeSdfClipmapLevel>& oldLevels, const std::vector<glm::vec4>& oldPixels) {
    for (std::uint32_t i = 0; i < volume.Levels().size(); ++i) {
        const auto& level = volume.Levels()[i]; const auto& old = oldLevels[i];
        const auto delta = glm::ivec3(glm::round((level.origin - old.origin) / level.spacing));
        std::uint64_t overlap = 0;
        for (std::uint32_t z = 0; z < level.counts.z; ++z) for (std::uint32_t y = 0; y < level.counts.y; ++y) for (std::uint32_t x = 0; x < level.counts.x; ++x) {
            const glm::uvec3 logical{x, y, z}; const auto previous = glm::ivec3(logical) + delta;
            if (glm::any(glm::lessThan(previous, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(previous, glm::ivec3(level.counts)))) continue;
            const auto physical = (glm::uvec3(previous) + old.scroll) % old.counts;
            Check(volume.SampleLogical(logical, i) == oldPixels[old.atlasOffset + At(physical, old.counts)], "Scrolling changed an overlapping world voxel"); ++overlap;
        }
        Check(level.statistics.enteringVoxels == level.statistics.totalVoxels - overlap, "Entering slabs include overlap or miss a diagonal slab");
    }
}
void CheckInterpolation(const ProbeSdfClipmapVolume& volume) {
    for (std::uint32_t i = 0; i < volume.Levels().size(); ++i) {
        const auto& level = volume.Levels()[i];
        for (const glm::vec3 coordinate : {glm::vec3(4.7f, 3.2f, 2.6f), glm::vec3(level.counts - glm::uvec3(1)), glm::vec3(level.counts) - glm::vec3(1.01f), glm::vec3(0)}) {
            const auto first = glm::uvec3(glm::floor(coordinate)), last = glm::min(first + glm::uvec3(1), level.counts - glm::uvec3(1)); const auto f = coordinate - glm::vec3(first);
            glm::vec4 expected(0);
            for (std::uint32_t z = 0; z != 2; ++z) for (std::uint32_t y = 0; y != 2; ++y) for (std::uint32_t x = 0; x != 2; ++x)
                expected += volume.SampleLogical({x ? last.x : first.x, y ? last.y : first.y, z ? last.z : first.z}, i) * (x ? f.x : 1 - f.x) * (y ? f.y : 1 - f.y) * (z ? f.z : 1 - f.z);
            expected.y = volume.SampleLogical(glm::min(glm::uvec3(glm::floor(coordinate + glm::vec3(.5f))), level.counts - glm::uvec3(1)), i).y;
            Near(volume.SampleWorld(level.origin + coordinate * level.spacing, i), expected, "Trilinear ring sampling wrapped a logical boundary neighbor");
        }
        Reject([&] { volume.SampleWorld(level.origin - level.spacing * .01f, i); }, "Outside clipmap sample silently wrapped");
    }
}
void CheckSceneApi(const std::shared_ptr<const ProbeMeshSdf>& mesh, const std::shared_ptr<const ProbeMeshSdf>& high) {
    ProbeGlobalSdfSettings settings; settings.counts = {16, 12, 16}; settings.spacing = glm::vec3(.125f); settings.maxDistance = .3f;
    settings.clipmapLevels = 3; settings.clipmapSpacingScale = 2; settings.clipmapCamera = {.1f, .1f, .2f};
    std::vector<ProbeSdfInstance> instances{
        {mesh, glm::translate(glm::mat4(1), glm::vec3(0, -.4f, 0)) * glm::scale(glm::mat4(1), glm::vec3(2, .1f, 1.7f)), false, 71, mesh, mesh},
        {mesh, glm::translate(glm::mat4(1), glm::vec3(.45f, .3f, -.1f)) * glm::scale(glm::mat4(1), glm::vec3(.4f)), true, 72, mesh, mesh}};
    std::vector<ProbeSdfClipmapMeshes> sources(instances.size()); sources[0][0] = sources[1][0] = high;
    auto scene = BuildProbeSdfScene(instances, settings);
    Check(scene->Clipmaps() && scene->GlobalGeneration() == 0 && scene->Clipmaps()->Generation() == 0
        && scene->Statistics().updatedVoxels == 0 && scene->DirtyRanges().empty(), "Scene Build did not defer its first clipmap capture");
    Check(scene->Statistics().totalVoxels == 3 * 16 * 12 * 16, "Deferred Scene has incorrect aggregate clipmap footprint");
    for (const auto& level : scene->Clipmaps()->Levels()) Check(level.generation == 0, "Build performed a throwaway per-level capture");
    const auto originalGeometry = scene->PackedGeometry(); const auto geometryGeneration = scene->GeometryGeneration(), originalMeshGeneration = scene->MeshGeneration();
    instances[1].mesh = high;
    Check(scene->UpdateInstances(instances) && scene->MeshGeneration() == originalMeshGeneration + 1
        && scene->GeometryGeneration() == geometryGeneration && scene->GlobalGeneration() == 0, "Preparing a local LOD before capture changed resident world geometry");
    const auto preparedSceneGeneration = scene->Statistics().generation;
    glm::vec3 camera = settings.clipmapCamera;
    Check(scene->UpdateGlobalClipmaps(camera, sources) && scene->GlobalGeneration() == 1 && scene->Statistics().generation == preparedSceneGeneration + 1,
        "Scene explicit first capture did not synchronize generation");
    Check(&scene->GlobalPixels() == &scene->Clipmaps()->Pixels() && &scene->DirtyRanges() == &scene->Clipmaps()->DirtyRanges(), "Scene Global data/ranges are not forwarded to clipmap storage");
    Check(scene->Statistics().updatedVoxels == scene->Statistics().totalVoxels && scene->Statistics().fullRebuild, "Scene first capture statistics are incomplete");
    CheckReference(*scene->Clipmaps(), instances, sources);
    float maximumError = 0;
    for (std::uint32_t level = 0; level < settings.clipmapLevels; ++level) {
        const float expectedError = std::max(Source(instances[0], sources, 0, level)->InterpolationError() * .1f,
                                              Source(instances[1], sources, 1, level)->InterpolationError() * .4f);
        Check(std::abs(scene->Clipmaps()->Levels()[level].sourceInterpolationError - expectedError) < 1e-7f,
            "Nonuniform table source error does not match minimum-scale distance units");
        maximumError = std::max(maximumError, expectedError);
    }
    Check(std::abs(scene->MaximumWorldMeshError() - maximumError) < 1e-7f, "Scene maximum source error differs from actual per-level sources");

    // Object atlas offsets use the Scene generation, independently of the
    // Global volume generation. A local-only quality switch must invalidate
    // GPU metadata even when every resident Global source stays unchanged.
    const auto beforeLocalSceneGeneration = scene->Statistics().generation, beforeLocalGlobalGeneration = scene->GlobalGeneration();
    const auto beforeLocalPixels = scene->GlobalPixels(); const auto beforeLocalOffset = scene->ObjectPixels()[ProbeSdfObjectTexels + 4].w;
    instances[1].mesh = mesh;
    Check(scene->UpdateInstances(instances) && scene->Statistics().generation == beforeLocalSceneGeneration + 1
        && scene->GlobalGeneration() == beforeLocalGlobalGeneration && scene->GlobalPixels() == beforeLocalPixels,
        "Local-only Mesh quality switch did not advance Scene metadata independently of Global generation");
    Check(beforeLocalOffset > 0 && scene->ObjectPixels()[ProbeSdfObjectTexels + 4].w == 0, "Local-only Mesh switch did not exercise a changed GPU object atlas offset");
    const auto localSceneGeneration = scene->Statistics().generation;
    Check(!scene->UpdateGlobalClipmaps(camera, sources) && scene->Statistics().generation == localSceneGeneration
        && scene->GlobalGeneration() == beforeLocalGlobalGeneration && scene->DirtyRanges().empty(),
        "Global no-op overwrote the Scene generation after local-only Mesh quality change");
    const auto meshGeneration = scene->MeshGeneration();

    const auto beforeCameraPixels = scene->GlobalPixels(); const auto beforeCameraLevels = scene->Clipmaps()->Levels(); camera = {.6f, .1f, .2f};
    Check(scene->UpdateGlobalClipmaps(camera, sources), "Scene camera scrolling failed");
    Check(scene->MeshGeneration() == meshGeneration && scene->GeometryGeneration() == geometryGeneration && scene->PackedGeometry() == originalGeometry,
        "Camera scrolling rebuilt Mesh atlas or source triangle geometry");
    Check(scene->Settings().clipmapCamera == camera && scene->Statistics().updatedVoxels == 4 * 12 * 16 && !scene->Statistics().fullRebuild,
        "Scene camera metadata/slab statistics are wrong");
    CheckOverlap(*scene->Clipmaps(), beforeCameraLevels, beforeCameraPixels); CheckRanges(*scene->Clipmaps(), beforeCameraPixels);
    const auto cameraGeneration = scene->GlobalGeneration();
    Check(!scene->UpdateInstances(instances) && !scene->DirtyRanges().empty(), "Metadata-only no-op discarded unconsumed clipmap dirty ranges");
    Check(!scene->UpdateGlobalClipmaps(camera, sources) && scene->GlobalGeneration() == cameraGeneration && scene->DirtyRanges().empty()
        && scene->Statistics().updatedVoxels == 0 && scene->Statistics().updatedBricks == 0 && scene->Statistics().objectDistanceQueries == 0
        && scene->Statistics().staticVoxelUpdates == 0 && scene->Statistics().dynamicVoxelUpdates == 0 && !scene->Statistics().fullRebuild,
        "Scene explicit global no-op retained stale work or changed generation");

    const auto beforeMovePixels = scene->GlobalPixels();
    instances[1].localToWorld = glm::translate(glm::mat4(1), glm::vec3(.1f, .3f, .2f)) * glm::scale(glm::mat4(1), glm::vec3(.4f));
    Check(scene->UpdateInstances(instances) && scene->GlobalGeneration() == cameraGeneration && scene->GlobalPixels() == beforeMovePixels,
        "UpdateInstances performed an implicit clipmap merge");
    Check(scene->MeshGeneration() == meshGeneration && scene->GeometryGeneration() == geometryGeneration + 1
        && scene->PackedGeometry()->TopologyKey() == originalGeometry->TopologyKey(), "Mover metadata rebaked a Mesh or replaced reference topology");
    Check(scene->UpdateGlobalClipmaps(camera, sources) && scene->GlobalGeneration() == cameraGeneration + 1
        && scene->Statistics().staticVoxelUpdates == 0 && scene->Statistics().dynamicVoxelUpdates == scene->Statistics().updatedVoxels,
        "Scene explicit mover update lost resident static-layer reuse");
    for (std::size_t i = 0; i < beforeMovePixels.size(); ++i) Check(scene->GlobalPixels()[i].z == beforeMovePixels[i].z, "Scene mover changed static physical cache");
    CheckReference(*scene->Clipmaps(), instances, sources); CheckRanges(*scene->Clipmaps(), beforeMovePixels);
    auto fresh = BuildProbeSdfScene(instances, settings); fresh->UpdateGlobalClipmaps(camera, sources);
    for (std::uint32_t level = 0; level < settings.clipmapLevels; ++level)
        for (std::uint32_t z = 0; z < settings.counts.z; ++z) for (std::uint32_t y = 0; y < settings.counts.y; ++y) for (std::uint32_t x = 0; x < settings.counts.x; ++x) {
            Near(scene->Clipmaps()->SampleLogical({x, y, z}, level), fresh->Clipmaps()->SampleLogical({x, y, z}, level), "Scene moved ring differs from fresh Scene full capture");
            if (level == 0) Near(scene->SampleGlobal({x, y, z}), fresh->SampleGlobal({x, y, z}), "Scene SampleGlobal used a physical rather than logical level-zero address");
        }
    const auto beforeSourceLevels = scene->Clipmaps()->Levels(); const auto beforeSourceGeometry = scene->GeometryGeneration();
    sources[1][1] = high;
    Check(!scene->UpdateInstances(instances) && scene->UpdateGlobalClipmaps(camera, sources), "Scene ignored explicit per-level source changes");
    Check(scene->Clipmaps()->Levels()[0].generation == beforeSourceLevels[0].generation && scene->Clipmaps()->Levels()[2].generation == beforeSourceLevels[2].generation
        && scene->Clipmaps()->Levels()[1].generation > beforeSourceLevels[1].generation && scene->MeshGeneration() == meshGeneration
        && scene->GeometryGeneration() == beforeSourceGeometry, "Scene per-level source change affected unrelated assets/levels");
    CheckReference(*scene->Clipmaps(), instances, sources);

    settings.clipmapLevels = 0; settings.counts = {9, 9, 9}; auto fixed = BuildProbeSdfScene(instances, settings);
    const auto fixedGeneration = fixed->GlobalGeneration(); const auto fixedPixels = fixed->GlobalPixels();
    Check(!fixed->Clipmaps() && !fixed->UpdateGlobalClipmaps(camera, sources) && fixed->GlobalGeneration() == fixedGeneration && fixed->GlobalPixels() == fixedPixels,
        "Clipmap API changed the disabled legacy fixed-volume path");
}
} // namespace

int main() {
    try {
        const auto mesh = BakeProbeMeshSdf(Cube(), 28, true), high = BakeProbeMeshSdf(Cube(), 40, true);
        const std::array<ProbeTriangle, 2> plane{{{{-.5f, 0, -.5f}, {.5f, 0, -.5f}, {.5f, 0, .5f}}, {{-.5f, 0, -.5f}, {.5f, 0, .5f}, {-.5f, 0, .5f}}}};
        const auto sheet = BakeProbeMeshSdf(plane, 24, false);
        ProbeSdfClipmapSettings settings; settings.levelCount = 3; settings.counts = {24, 20, 16}; settings.baseSpacing = .125f; settings.spacingScale = 2;
        std::vector<ProbeSdfInstance> instances{{mesh, glm::translate(glm::mat4(1), glm::vec3(-.5f, 0, 0)), false, 10},
            {mesh, glm::translate(glm::mat4(1), glm::vec3(.75f, .1f, .25f)), true, 20},
            {sheet, glm::translate(glm::mat4(1), glm::vec3(0, -.7f, 0)), false, 30}};
        std::vector<ProbeSdfClipmapMeshes> sources(instances.size()); sources[0][0] = high;
        ProbeSdfClipmapVolume volume(settings); Reject([&] { volume.SampleWorld({}, 0); }, "Uninitialized field was readable");
        glm::vec3 camera(.1f, .2f, .3f); Check(volume.Update(instances, camera, sources), "Initial clipmap capture failed");
        Check(volume.Statistics().fullLevels == 3 && volume.Generation() == 1, "Initial capture metadata is wrong"); CheckReference(volume, instances, sources); CheckRanges(volume, {}); CheckInterpolation(volume);
        Check(std::abs(volume.ObjectDistance(0, {-.3f, .05f, .05f}, 0) + .3f) < 1e-5f, "Translated analytic box signed distance changed");
        Check(std::abs(volume.ObjectDistance(2, {0, -.65f, 0}, 0) - .05f) < 1e-5f, "Translated unsigned plane distance changed");

        const auto initialPixels = volume.Pixels(), initialRanges = volume.RangePixels(); const auto generation = volume.Generation(); camera = {.2f, .3f, .4f};
        Check(!volume.Update(instances, camera, sources) && volume.Generation() == generation && volume.DirtyRanges().empty() && volume.RangeDirtyRanges().empty(), "Sub-brick camera movement was not a no-op");
        Check(volume.Statistics().updatedVoxels == 0 && initialPixels == volume.Pixels() && initialRanges == volume.RangePixels(), "No-op touched physical storage or hierarchy"); CheckHierarchy(volume, initialRanges);
        std::uint64_t firstSlabVoxels = 0;
        for (const glm::vec3 next : {glm::vec3(.55f, .2f, .3f), glm::vec3(-.55f), glm::vec3(.55f), glm::vec3(1.1f, .55f, .55f), glm::vec3(1.6f, .55f, .55f), glm::vec3(2.1f, .55f, .55f), glm::vec3(2.6f, .55f, .55f), glm::vec3(3.1f, .55f, .55f)}) {
            const auto previous = volume.Pixels(), previousRanges = volume.RangePixels(); const auto oldLevels = volume.Levels(); camera = next;
            Check(volume.Update(instances, camera, sources), "Camera slab update reported no change");
            Check(volume.Statistics().fullLevels == 0, "Small camera step triggered full capture");
            CheckOverlap(volume, oldLevels, previous); CheckRanges(volume, previous); CheckReference(volume, instances, sources); CheckFull(volume, instances, camera, sources); CheckInterpolation(volume);
            CheckHierarchy(volume, previousRanges);
            if (!firstSlabVoxels) { firstSlabVoxels = volume.Statistics().updatedVoxels; Check(firstSlabVoxels == 4 * 20 * 16, "One-axis slab updated more than newly entering fine voxels"); }
        }
        camera = {40, 0, 0}; Check(volume.Update(instances, camera, sources) && volume.Statistics().fullLevels == 3, "Teleport did not recapture all affected levels");
        for (const auto& level : volume.Levels()) Check(level.statistics.fullReason == ProbeSdfClipmapFullReason::Teleport && level.scroll == glm::uvec3(0), "Teleport did not reset its physical ring");
        CheckReference(volume, instances, sources); CheckRanges(volume, {});
        camera = {}; volume.Update(instances, camera, sources);

        auto previous = volume.Pixels(); const auto beforeDynamicRanges = volume.RangePixels(); instances[1].localToWorld = glm::translate(glm::mat4(1), glm::vec3(-.2f, .1f, .25f));
        volume.Update(instances, camera, sources); const auto dynamic = volume.Statistics();
        Check(dynamic.staticVoxelUpdates == 0 && dynamic.dynamicVoxelUpdates == dynamic.updatedVoxels && dynamic.updatedVoxels < dynamic.totalVoxels, "Dynamic object update lost static reuse/locality");
        for (std::size_t i = 0; i < previous.size(); ++i) Check(previous[i].z == volume.Pixels()[i].z, "Dynamic motion changed a static cache texel");
        CheckRanges(volume, previous); CheckReference(volume, instances, sources); CheckFull(volume, instances, camera, sources);
        CheckHierarchy(volume, beforeDynamicRanges);
        previous = volume.Pixels(); instances[0].localToWorld = glm::translate(glm::mat4(1), glm::vec3(-.75f, .1f, -.3f)) * glm::rotate(glm::mat4(1), .3f, glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1), glm::vec3(1.2f, .6f, .8f));
        volume.Update(instances, camera, sources); Check(volume.Statistics().dynamicVoxelUpdates == 0, "Static object update touched movable layer");
        for (std::size_t i = 0; i < previous.size(); ++i) Check(previous[i].w == volume.Pixels()[i].w, "Static motion changed a movable cache texel");
        CheckRanges(volume, previous); CheckReference(volume, instances, sources); CheckFull(volume, instances, camera, sources);

        // Raster/local changes preserve explicit per-level resident sources.
        for (std::size_t object = 0; object < instances.size(); ++object) instances[object].globalMesh = instances[object].mesh;
        volume.Update(instances, camera, sources); const auto beforeLocal = volume.Generation(); instances[1].mesh = high;
        Check(!volume.Update(instances, camera, sources) && volume.Generation() == beforeLocal, "Local MDF LOD dirtied an unchanged resident Global field");
        const auto beforeLevels = volume.Levels(); sources[1][1] = high; volume.Update(instances, camera, sources);
        Check(volume.Levels()[0].generation == beforeLevels[0].generation && volume.Levels()[2].generation == beforeLevels[2].generation
            && volume.Levels()[1].generation > beforeLevels[1].generation, "Per-level source change dirtied unrelated levels");
        Check(volume.Statistics().staticVoxelUpdates == 0, "Per-level movable source touched static cache"); CheckReference(volume, instances, sources); CheckFull(volume, instances, camera, sources);

        previous = volume.Pixels(); instances.erase(instances.begin()); sources.erase(sources.begin()); volume.Update(instances, camera, sources);
        Check(volume.Statistics().fullLevels == 0, "Removal forced full recapture"); CheckReference(volume, instances, sources); CheckFull(volume, instances, camera, sources); CheckRanges(volume, previous);
        previous = volume.Pixels(); instances.clear(); sources.clear(); volume.Update(instances, camera, sources);
        Check(volume.Statistics().fullLevels == 0, "Removing all objects forced full recapture"); CheckReference(volume, instances); CheckRanges(volume, previous);
        Check(!volume.Update(instances, camera) && volume.DirtyRanges().empty() && volume.RangeDirtyRanges().empty(), "Empty-world no-op changed generation");

        const auto stableGeneration = volume.Generation(); const auto stablePixels = volume.Pixels(), stableRanges = volume.RangePixels();
        Reject([&] { volume.Update(instances, {std::numeric_limits<float>::quiet_NaN(), 0, 0}); }, "NaN camera accepted");
        Reject([&] { volume.Update(instances, {std::numeric_limits<float>::max(), 0, 0}); }, "Unresolvable camera grid accepted");
        instances.push_back({mesh}); instances[0].localToWorld[0][1] = .1f;
        Reject([&] { volume.Update(instances, {1, 0, 0}); }, "Sheared instance accepted");
        instances[0].localToWorld = glm::mat4(1); instances[0].localToWorld[3].x = std::numeric_limits<float>::infinity();
        Reject([&] { volume.Update(instances, {1, 0, 0}); }, "Infinite transform accepted");
        instances[0].localToWorld = glm::mat4(1); instances[0].mesh.reset(); Reject([&] { volume.Update(instances, {}); }, "Null source accepted");
        Check(volume.Generation() == stableGeneration && volume.Pixels() == stablePixels && volume.RangePixels() == stableRanges, "Rejected update mutated live field or hierarchy");
        auto invalid = settings; invalid.levelCount = 5; Reject([&] { ProbeSdfClipmapVolume bad(invalid); }, "Excessive clipmap levels accepted");
        invalid = settings; invalid.counts.x = 23; Reject([&] { ProbeSdfClipmapVolume bad(invalid); }, "Non-brick-aligned ring dimensions accepted");

        // This sample lies beyond bounds + band + interpolation/voxel halo,
        // but within the explicitly expanded field's band. Independent direct
        // distance catches a bin/dirty radius that accidentally omits bias.
        const auto biased = BakeProbeMeshSdf(Cube(), 24, true, 2);
        ProbeSdfClipmapSettings biasSettings; biasSettings.levelCount = 1; biasSettings.counts = {128, 24, 24}; biasSettings.baseSpacing = .01f; biasSettings.baseMaxDistance = .6f;
        ProbeSdfClipmapVolume biasVolume(biasSettings); std::vector<ProbeSdfInstance> biasedObjects{{biased, glm::mat4(1), true, 1}};
        const glm::vec3 biasCamera(.8f, 0, 0); biasVolume.Update(biasedObjects, biasCamera);
        const auto& biasLevel = biasVolume.Levels()[0]; const auto biasLogical = glm::uvec3(glm::round((glm::vec3(1.2f, 0, 0) - biasLevel.origin) / biasLevel.spacing));
        const auto biasPoint = biasLevel.origin + glm::vec3(biasLogical) * biasLevel.spacing;
        const auto expectedBias = Reference(biasedObjects, {}, 0, biasPoint, biasLevel.narrowBand);
        Check(expectedBias.y == 1 && expectedBias.x < biasLevel.narrowBand, "Bias binning fixture has no independent represented sample");
        Near(biasVolume.SampleLogical(biasLogical, 0), expectedBias, "Expanded narrow band was excluded from clipmap object bins");
        CheckReference(biasVolume, biasedObjects); CheckHierarchy(biasVolume);
        biasedObjects[0].localToWorld = glm::translate(glm::mat4(1), glm::vec3(-.2f, 0, 0)); biasVolume.Update(biasedObjects, biasCamera);
        Check(biasVolume.SampleLogical(biasLogical, 0).y == 0, "Movement did not clear the old expanded narrow band"); CheckReference(biasVolume, biasedObjects);
        biasedObjects[0].localToWorld = glm::mat4(1); biasVolume.Update(biasedObjects, biasCamera);
        Near(biasVolume.SampleLogical(biasLogical, 0), expectedBias, "Movement did not restore the new expanded narrow band"); CheckReference(biasVolume, biasedObjects);

        CheckSceneApi(mesh, high);

        // Default footprint/spacing and all four levels, including the largest
        // ring, are exercised without a costly model bake.
        ProbeSdfClipmapVolume defaults; defaults.Update({}, {});
        Check(defaults.Levels().size() == 4 && defaults.Statistics().totalVoxels == 4 * 96 * 96 * 96, "Default clipmap allocation differs from profile");
        for (std::uint32_t i = 0; i < 4; ++i) Check(defaults.Levels()[i].spacing == glm::vec3(.03125f * std::pow(4.f, float(i))), "Default level spacing is wrong");
        defaults.Update({}, {.13f, 0, 0}); Check(defaults.Statistics().scrolledLevels == 1 && defaults.Statistics().updatedVoxels == 4 * 96 * 96, "Default camera update lost fine-level slab locality");
        CheckHierarchy(defaults);

        ProbeSdfClipmapSettings parallelSettings; parallelSettings.levelCount = 1; parallelSettings.counts = glm::uvec3(32); parallelSettings.baseSpacing = .125f;
        ProbeSdfClipmapVolume parallel(parallelSettings);
        std::vector<ProbeSdfInstance> threadedObjects{{mesh, glm::scale(glm::mat4(1), glm::vec3(2)), false, 41},
            {high, glm::translate(glm::mat4(1), glm::vec3(.6f, .25f, -.4f)) * glm::scale(glm::mat4(1), glm::vec3(1.5f)), true, 42}};
        parallel.Update(threadedObjects, {}); CheckParallelAgainstSerialTiles(parallel, threadedObjects);
        for (const glm::vec3 threadedCamera : {glm::vec3(.55f), glm::vec3(-.55f), glm::vec3(.55f)}) {
            parallel.Update(threadedObjects, threadedCamera);
            Check(parallel.Statistics().updatedVoxels >= 8192, "Parallel camera fixture does not reach the worker threshold");
            CheckParallelAgainstSerialTiles(parallel, threadedObjects);
        }
        threadedObjects[1].localToWorld[3].x += .375f; parallel.Update(threadedObjects, glm::vec3(.55f));
        Check(parallel.Statistics().updatedVoxels >= 8192 && parallel.Statistics().staticVoxelUpdates == 0, "Parallel dynamic fixture lost movable-only update work");
        CheckParallelAgainstSerialTiles(parallel, threadedObjects);
        const auto parallelGeneration = parallel.Generation(); parallel.Update(threadedObjects, glm::vec3(.55f));
        Check(parallel.Generation() == parallelGeneration && parallel.DirtyRanges().empty() && parallel.RangeDirtyRanges().empty(), "Idle persistent workers changed a no-op field");
        std::cout << "Clipmap signed box/unsigned plane, independent full union, negative/positive XYZ scroll, exact physical overlap/ranges, conservative shared-corner brick min/max hierarchy, trilinear boundaries, teleport, static/dynamic reuse, removal/owner shifts, per-level resident sources, parallel/serial bit determinism and invalid-input atomicity passed. Fine slab "
                  << firstSlabVoxels << "/" << dynamic.totalVoxels << " voxels; dynamic " << dynamic.updatedVoxels << "/" << dynamic.totalVoxels << ".\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
