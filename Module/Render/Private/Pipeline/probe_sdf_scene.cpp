#include "Render/Public/Pipeline/probe_sdf_scene.h"
#include "Render/Public/Pipeline/probe_sdf_clipmap.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>
#include <glm/gtc/matrix_inverse.hpp>

namespace Render {
namespace {
using Clock = std::chrono::steady_clock;
std::size_t VoxelCount(glm::uvec3 n) { return std::size_t(n.x) * n.y * n.z; }
std::size_t At(glm::uvec3 p, glm::uvec3 n) { return (std::size_t(p.z) * n.y + p.y) * n.x + p.x; }
std::size_t Padded(std::size_t n) { return std::max<std::size_t>(ProbeSdfAtlasWidth, (n + ProbeSdfAtlasWidth - 1) / ProbeSdfAtlasWidth * ProbeSdfAtlasWidth); }
bool Finite(glm::vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
float BoxDistance(glm::vec3 p, glm::vec3 lo, glm::vec3 hi) {
    return glm::length(glm::max(glm::max(lo - p, p - hi), glm::vec3(0)));
}
bool BoxRay(glm::vec3 p, glm::vec3 d, glm::vec3 lo, glm::vec3 hi) {
    float near = 0, far = 1e30f;
    for (int a = 0; a != 3; ++a) {
        if (std::abs(d[a]) < 1e-12f) { if (p[a] < lo[a] || p[a] > hi[a]) return false; continue; }
        float x = (lo[a] - p[a]) / d[a], y = (hi[a] - p[a]) / d[a];
        near = std::max(near, std::min(x, y)); far = std::min(far, std::max(x, y));
    }
    return near <= far;
}
float TriangleDistance(glm::vec3 p, glm::vec3 a, glm::vec3 b, glm::vec3 c) {
    const auto ab = b - a, ac = c - a, ap = p - a;
    const float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return glm::length(ap);
    const auto bp = p - b; const float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return glm::length(bp);
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return glm::length(p - (a + ab * (d1 / (d1 - d3))));
    const auto cp = p - c; const float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return glm::length(cp);
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return glm::length(p - (a + ac * (d2 / (d2 - d6))));
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0 && d4 >= d3 && d5 >= d6)
        return glm::length(p - (b + (c - b) * ((d4 - d3) / (d4 - d3 + d5 - d6))));
    return std::abs(glm::dot(ap, glm::normalize(glm::cross(ab, ac))));
}
bool TriangleRay(glm::vec3 p, glm::vec3 d, glm::vec3 a, glm::vec3 b, glm::vec3 c, float& t, bool& edge) {
    const auto ab = b - a, ac = c - a, q = glm::cross(d, ac);
    const float det = glm::dot(ab, q);
    if (std::abs(det) < 1e-9f) return false;
    const auto rel = p - a; const float u = glm::dot(rel, q) / det;
    if (u < -1e-6f || u > 1 + 1e-6f) return false;
    const auto r = glm::cross(rel, ab); const float v = glm::dot(d, r) / det;
    if (v < -1e-6f || u + v > 1 + 1e-6f) return false;
    t = glm::dot(ac, r) / det;
    edge |= u < 1e-5f || v < 1e-5f || 1 - u - v < 1e-5f;
    return t > 1e-6f;
}
struct DistanceQuery { float distance = 1e30f; int triangle = -1; };
DistanceQuery Nearest(const RealtimeGiScene& scene, glm::vec3 p, ProbeSdfBakeStatistics& statistics) {
    DistanceQuery result;
    const auto& data = scene.Pixels(); const auto triangleStart = scene.NodeCount() * 2;
    std::array<std::uint32_t, 64> stack{}; std::size_t size = 1;
    while (size) {
        const auto node = stack[--size]; ++statistics.nodeVisits;
        const auto lo = data[node * 2], hi = data[node * 2 + 1];
        if (BoxDistance(p, glm::vec3(lo), glm::vec3(hi)) > result.distance) continue;
        if (hi.w >= 0) {
            const auto left = std::uint32_t(lo.w), right = std::uint32_t(hi.w);
            const float ld = BoxDistance(p, glm::vec3(data[left * 2]), glm::vec3(data[left * 2 + 1]));
            const float rd = BoxDistance(p, glm::vec3(data[right * 2]), glm::vec3(data[right * 2 + 1]));
            // The near child is popped first, tightening the pruning radius.
            stack[size++] = ld < rd ? right : left; stack[size++] = ld < rd ? left : right;
        } else for (auto triangle = std::uint32_t(lo.w); triangle < std::uint32_t(lo.w - hi.w); ++triangle) {
            ++statistics.distanceTriangleTests;
            const auto at = triangleStart + triangle * 5;
            const float distance = TriangleDistance(p, glm::vec3(data[at]), glm::vec3(data[at + 1]), glm::vec3(data[at + 2]));
            if (distance < result.distance) { result.distance = distance; result.triangle = int(triangle); }
        }
    }
    return result;
}
bool Parity(const RealtimeGiScene& scene, glm::vec3 p, glm::vec3 direction, ProbeSdfBakeStatistics& statistics, bool& edge) {
    const auto& data = scene.Pixels(); const auto triangleStart = scene.NodeCount() * 2;
    std::array<std::uint32_t, 64> stack{}; std::size_t size = 1;
    std::vector<float> intersections; intersections.reserve(8);
    while (size) {
        const auto node = stack[--size]; ++statistics.nodeVisits;
        const auto lo = data[node * 2], hi = data[node * 2 + 1];
        if (!BoxRay(p, direction, glm::vec3(lo), glm::vec3(hi))) continue;
        if (hi.w >= 0) { stack[size++] = std::uint32_t(lo.w); stack[size++] = std::uint32_t(hi.w); }
        else for (auto triangle = std::uint32_t(lo.w); triangle < std::uint32_t(lo.w - hi.w); ++triangle) {
            ++statistics.signTriangleTests; const auto at = triangleStart + triangle * 5; float distance = 0;
            if (TriangleRay(p, direction, glm::vec3(data[at]), glm::vec3(data[at + 1]), glm::vec3(data[at + 2]), distance, edge))
                intersections.push_back(distance);
        }
    }
    std::sort(intersections.begin(), intersections.end());
    std::size_t distinct = 0; float previous = -1;
    // A ray crossing a triangulated face edge must count the shared crossing once.
    for (float distance : intersections) if (previous < 0 || distance - previous > 2e-5f * std::max(1.f, distance)) {
        previous = distance; ++distinct;
    }
    return (distinct & 1) != 0;
}
bool Watertight(std::span<const ProbeTriangle> triangles, glm::vec3 lo, glm::vec3 hi) {
    using Vertex = std::array<std::int64_t, 3>; using Edge = std::pair<Vertex, Vertex>;
    const double scale = 1000000.0 / std::max(double(glm::length(hi - lo)), 1e-5);
    const auto key = [&](glm::vec3 p) -> Vertex { return {std::llround((p.x - lo.x) * scale), std::llround((p.y - lo.y) * scale), std::llround((p.z - lo.z) * scale)}; };
    std::map<Edge, std::uint32_t> counts;
    for (const auto& t : triangles) {
        const std::array<Vertex, 3> vertices{key(t.a), key(t.b), key(t.c)};
        for (int i = 0; i != 3; ++i) {
            auto a = vertices[i], b = vertices[(i + 1) % 3];
            if (a == b) return false;
            if (b < a) std::swap(a, b);
            ++counts[{a, b}];
        }
    }
    return std::all_of(counts.begin(), counts.end(), [](const auto& edge) { return edge.second == 2; });
}
float Trilinear(const std::vector<glm::vec4>& pixels, glm::uvec3 counts, glm::vec3 coordinate) {
    coordinate = glm::clamp(coordinate, glm::vec3(0), glm::vec3(counts - glm::uvec3(1)));
    const auto lo = glm::uvec3(glm::floor(coordinate)); const auto hi = glm::min(lo + glm::uvec3(1), counts - glm::uvec3(1));
    const auto f = coordinate - glm::vec3(lo); float distance = 0;
    for (std::uint32_t z = 0; z != 2; ++z) for (std::uint32_t y = 0; y != 2; ++y) for (std::uint32_t x = 0; x != 2; ++x) {
        const float weight = (x ? f.x : 1 - f.x) * (y ? f.y : 1 - f.y) * (z ? f.z : 1 - f.z);
        distance += pixels[At({x ? hi.x : lo.x, y ? hi.y : lo.y, z ? hi.z : lo.z}, counts)].x * weight;
    }
    return distance;
}
struct InstanceBounds { glm::vec3 lo{1e30f}, hi{-1e30f}; };
InstanceBounds WorldBounds(const ProbeSdfInstance& instance) {
    InstanceBounds bounds;
    for (int z = 0; z != 2; ++z) for (int y = 0; y != 2; ++y) for (int x = 0; x != 2; ++x) {
        const auto lo = instance.mesh->BoundsMin(), hi = instance.mesh->BoundsMax();
        const auto p = glm::vec3(instance.localToWorld * glm::vec4(x ? hi.x : lo.x, y ? hi.y : lo.y, z ? hi.z : lo.z, 1));
        bounds.lo = glm::min(bounds.lo, p); bounds.hi = glm::max(bounds.hi, p);
    }
    return bounds;
}
glm::vec3 AxisScales(const ProbeSdfInstance& instance) {
    return {glm::length(glm::vec3(instance.localToWorld[0])), glm::length(glm::vec3(instance.localToWorld[1])), glm::length(glm::vec3(instance.localToWorld[2]))};
}
void ValidateInstance(const ProbeSdfInstance& instance) {
    if (!instance.mesh || !instance.mesh->Geometry() || !instance.mesh->Geometry()->TriangleCount())
        throw std::invalid_argument("SDF instance needs a nonempty baked mesh");
    for(const auto& field:{instance.globalMesh,instance.geometryMesh})
        if(field&&(!field->Geometry()||!field->Geometry()->TriangleCount()))throw std::invalid_argument("Invalid resident SDF source");
    for (int i = 0; i != 4; ++i) for (int j = 0; j != 4; ++j) if (!std::isfinite(instance.localToWorld[i][j]))
        throw std::invalid_argument("Nonfinite SDF transform");
    if (std::abs(instance.localToWorld[0].w) > 1e-6f || std::abs(instance.localToWorld[1].w) > 1e-6f || std::abs(instance.localToWorld[2].w) > 1e-6f || std::abs(instance.localToWorld[3].w - 1) > 1e-6f)
        throw std::invalid_argument("SDF transform must be affine");
    const auto scales = AxisScales(instance);
    if (glm::any(glm::lessThan(scales, glm::vec3(1e-4f)))) throw std::invalid_argument("Singular SDF transform");
    // Rotation and nonuniform scale are supported; a sheared matrix needs a
    // singular-value bound rather than the smallest column length.
    for (int i = 0; i != 3; ++i) for (int j = i + 1; j != 3; ++j)
        if (std::abs(glm::dot(glm::vec3(instance.localToWorld[i]), glm::vec3(instance.localToWorld[j]))) > scales[i] * scales[j] * 1e-4f)
            throw std::invalid_argument("Sheared SDF transforms are unsupported");
}
bool Same(const ProbeSdfInstance& a, const ProbeSdfInstance& b) {
    if (a.mesh != b.mesh || a.globalMesh != b.globalMesh || a.geometryMesh != b.geometryMesh || a.movable != b.movable || a.userId != b.userId) return false;
    for (int i = 0; i != 4; ++i) if (a.localToWorld[i] != b.localToWorld[i]) return false;
    return true;
}
ProbeSdfInstance GlobalInstance(ProbeSdfInstance value) {
    if(value.globalMesh)value.mesh=value.globalMesh;
    value.globalMesh.reset();value.geometryMesh.reset();return value;
}
const std::shared_ptr<const ProbeMeshSdf>& GeometryMesh(const ProbeSdfInstance& value) {
    return value.geometryMesh?value.geometryMesh:value.mesh;
}
void DirtyBounds(const ProbeSdfInstance& instance, const ProbeGlobalSdfSettings& settings, glm::uvec3 brickCounts, std::vector<std::uint8_t>& dirty) {
    auto bounds = WorldBounds(instance);
    const auto scales = AxisScales(instance); const float maximum = std::max({scales.x, scales.y, scales.z});
    // Outside the local atlas, ObjectDistance includes the world geometry
    // AABB lower bound. Its finite narrow band therefore has this tight bound
    // even for strongly stretched instances. Include an interpolation halo.
    const float fieldPadding = 4 * std::max({instance.mesh->Spacing().x, instance.mesh->Spacing().y, instance.mesh->Spacing().z}) * maximum;
    const float influence = std::max(settings.maxDistance+instance.mesh->SurfaceBias()*std::min({scales.x,scales.y,scales.z}), fieldPadding) + instance.mesh->InterpolationError() * std::min({scales.x, scales.y, scales.z}) + glm::length(settings.spacing);
    bounds.lo -= glm::vec3(influence); bounds.hi += glm::vec3(influence);
    auto lo = glm::ivec3(glm::floor((bounds.lo - settings.origin) / settings.spacing));
    auto hi = glm::ivec3(glm::ceil((bounds.hi - settings.origin) / settings.spacing));
    if (glm::any(glm::lessThan(hi, glm::ivec3(0))) || glm::any(glm::greaterThan(lo, glm::ivec3(settings.counts - glm::uvec3(1))))) return;
    lo = glm::clamp(lo, glm::ivec3(0), glm::ivec3(settings.counts - glm::uvec3(1))) / int(settings.brickSize);
    hi = glm::clamp(hi, glm::ivec3(0), glm::ivec3(settings.counts - glm::uvec3(1))) / int(settings.brickSize);
    for (int z = lo.z; z <= hi.z; ++z) for (int y = lo.y; y <= hi.y; ++y) for (int x = lo.x; x <= hi.x; ++x)
        dirty[At(glm::uvec3(x, y, z), brickCounts)] = 1;
}
} // namespace

std::shared_ptr<const ProbeMeshSdf> BakeProbeMeshSdf(std::span<const ProbeTriangle> triangles, std::uint32_t resolution, bool closed,float expansion,
    std::span<const std::array<glm::vec3,3>> shadingNormals) {
    if (resolution < 12 || resolution > 128 || triangles.empty()) throw std::invalid_argument("Mesh SDF needs geometry and resolution 12..128");
    if(!std::isfinite(expansion)||expansion<0||expansion>2)throw std::invalid_argument("Invalid Mesh SDF surface expansion");
    if(!shadingNormals.empty()&&shadingNormals.size()!=triangles.size())
        throw std::invalid_argument("Mesh shading-normal sidecar must match input triangle count");
    for(const auto& triangle:shadingNormals)for(const auto normal:triangle)
        if(!Finite(normal)||glm::dot(normal,normal)<1e-12f)
            throw std::invalid_argument("Invalid mesh shading-normal sidecar");
    const auto begin = Clock::now(); auto result = std::make_shared<ProbeMeshSdf>();
    result->shadingNormals_.assign(shadingNormals.begin(),shadingNormals.end());
    result->geometry_ = BuildRealtimeGiScene(triangles); result->triangles_.assign(triangles.begin(), triangles.end());
    const auto& data = result->geometry_->Pixels(); result->boundsMin_ = glm::vec3(data[0]); result->boundsMax_ = glm::vec3(data[1]);
    const auto extent = result->boundsMax_ - result->boundsMin_;
    const float spacing = std::max(std::max({extent.x, extent.y, extent.z}) / float(resolution - 7), .0001f);
    result->spacing_ = glm::vec3(spacing); result->origin_ = result->boundsMin_ - glm::vec3(3 * spacing);
    result->counts_ = glm::uvec3(glm::ceil(extent / spacing)) + glm::uvec3(7);
    result->interpolationError_ = .5f * glm::length(result->spacing_);
    result->surfaceBias_=expansion*spacing;
    result->resolution_=resolution;
    result->closed_ = closed && Watertight(triangles, result->boundsMin_, result->boundsMax_);
    result->pixels_.resize(Padded(VoxelCount(result->counts_)), glm::vec4(0));
    const std::array<glm::vec3, 3> directions{glm::normalize(glm::vec3(.719f, .331f, .607f)), glm::normalize(glm::vec3(.263f, .911f, .317f)), glm::normalize(glm::vec3(.431f, .283f, .857f))};
    for (std::uint32_t z = 0; z < result->counts_.z; ++z) for (std::uint32_t y = 0; y < result->counts_.y; ++y) for (std::uint32_t x = 0; x < result->counts_.x; ++x) {
        const auto p = result->origin_ + glm::vec3(x, y, z) * spacing;
        auto query = Nearest(*result->geometry_, p, result->statistics_); bool inside = false;
        if (result->closed_ && query.distance > spacing * 1e-4f && glm::all(glm::greaterThan(p, result->boundsMin_)) && glm::all(glm::lessThan(p, result->boundsMax_))) {
            bool edge = false; inside = Parity(*result->geometry_, p, directions[0], result->statistics_, edge);
            // Only edge-coincident queries need additional independent rays.
            if (edge) { bool edge1 = false, edge2 = false; int votes = int(inside);
                votes += Parity(*result->geometry_, p, directions[1], result->statistics_, edge1);
                votes += Parity(*result->geometry_, p, directions[2], result->statistics_, edge2); inside = votes >= 2;
            }
        }
        result->pixels_[At({x, y, z}, result->counts_)] = glm::vec4(inside ? -query.distance : query.distance, float(query.triangle), 0, 0);
        ++result->statistics_.voxels;
    }
    result->statistics_.milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
    return result;
}
float ProbeMeshSdf::Sample(glm::vec3 p) const { return Trilinear(pixels_, counts_, (p - origin_) / spacing_); }
float ProbeMeshSdf::ConservativeDistance(glm::vec3 p) const {
    const auto hi = origin_ + glm::vec3(counts_ - glm::uvec3(1)) * spacing_;
    const auto clamped = glm::clamp(p, origin_, hi); const float sample = Sample(clamped);
    if (p == clamped) return std::max(0.f, std::abs(sample) - interpolationError_);
    // A Lipschitz extension and the original geometry bounds are both lower
    // bounds. Extrapolating sample + outside length would overestimate corners.
    return std::max(BoxDistance(p, boundsMin_, boundsMax_), std::max(0.f, sample - interpolationError_ - glm::length(p - clamped)));
}
glm::uvec3 ProbeSdfScene::BrickCounts() const { return (settings_.counts + glm::uvec3(settings_.brickSize - 1)) / settings_.brickSize; }
void ProbeSdfScene::EnsureGlobalField() const {
    if(!globalFieldDeferred_)return;
    auto& scene=*const_cast<ProbeSdfScene*>(this);
    std::vector<std::uint8_t> all(scene.statistics_.totalBricks,1);
    scene.statistics_.fullRebuild=true;scene.RebuildBricks(all,all);scene.globalFieldDeferred_=false;
}
const std::vector<glm::vec4>& ProbeSdfScene::GlobalPixels() const { EnsureGlobalField();return clipmaps_ ? clipmaps_->Pixels() : globalPixels_; }
const std::vector<ProbeSdfAtlasRange>& ProbeSdfScene::DirtyRanges() const { EnsureGlobalField();return clipmaps_ ? clipmaps_->DirtyRanges() : dirtyRanges_; }
glm::vec4 ProbeSdfScene::SampleGlobal(glm::uvec3 voxel) const {
    EnsureGlobalField();
    voxel = glm::min(voxel, settings_.counts - glm::uvec3(1));
    return clipmaps_ ? clipmaps_->SampleLogical(voxel, 0) : globalPixels_[At(voxel, settings_.counts)];
}
float ProbeSdfScene::ObjectDistance(std::uint32_t index, glm::vec3 p) const {
    if (index >= instances_.size()) return settings_.maxDistance;
    const auto at = std::size_t(index) * ProbeSdfObjectTexels;
    const auto transform = glm::mat4(objectPixels_[at], objectPixels_[at + 1], objectPixels_[at + 2], objectPixels_[at + 3]);
    const auto local = glm::vec3(transform * glm::vec4(p, 1)); const auto& instance=instances_[index];
    const auto& mesh = *(instance.globalMesh?instance.globalMesh:instance.mesh);
    const auto fieldMax = mesh.Origin() + glm::vec3(mesh.Counts() - glm::uvec3(1)) * mesh.Spacing();
    const auto clamped = glm::clamp(local, mesh.Origin(), fieldMax); const float minimumScale = objectPixels_[at + 6].w;
    if (local == clamped) return (mesh.Sample(local)-mesh.SurfaceBias()) * minimumScale;
    return std::max(std::max(0.f,mesh.ConservativeDistance(local)-mesh.SurfaceBias()) * minimumScale,
        std::max(0.f,BoxDistance(p,globalBoundsMin_[index],globalBoundsMax_[index])-mesh.SurfaceBias()*minimumScale));
}
void ProbeSdfScene::RepackAssets(bool rebuildMeshAtlas,std::span<const std::uint32_t> changedObjects) {
    const auto metadataBegin=Clock::now();
    std::vector<std::shared_ptr<const ProbeMeshSdf>> meshes;
    std::vector<std::size_t> offsets;
    for (const auto& instance : instances_) if (std::find(meshes.begin(), meshes.end(), instance.mesh) == meshes.end()) meshes.push_back(instance.mesh);
    if (rebuildMeshAtlas) { meshPixels_.clear(); ++meshGeneration_; }
    maximumWorldMeshError_ = 0; std::size_t offset = 0;
    statistics_.bakedVoxels = statistics_.distanceTriangleTests = statistics_.signTriangleTests = 0;
    for (const auto& mesh : meshes) {
        offsets.push_back(offset); const auto count = VoxelCount(mesh->Counts()); offset += count;
        if (rebuildMeshAtlas) meshPixels_.insert(meshPixels_.end(), mesh->Pixels().begin(), mesh->Pixels().begin() + count);
        statistics_.bakedVoxels += mesh->Statistics().voxels; statistics_.distanceTriangleTests += mesh->Statistics().distanceTriangleTests; statistics_.signTriangleTests += mesh->Statistics().signTriangleTests;
    }
    if (rebuildMeshAtlas) meshPixels_.resize(Padded(meshPixels_.size()), glm::vec4(0));
    const bool partial=!rebuildMeshAtlas&&!changedObjects.empty()&&objectPixels_.size()==Padded(instances_.size()*ProbeSdfObjectTexels);
    if(!partial)objectPixels_.assign(Padded(instances_.size() * ProbeSdfObjectTexels), glm::vec4(0));
    globalBoundsMin_.resize(instances_.size());globalBoundsMax_.resize(instances_.size());
    for (std::size_t i = 0; i < instances_.size(); ++i) {
        const auto& instance = instances_[i]; const auto& mesh = *instance.mesh;
        const auto asset = std::size_t(std::find(meshes.begin(), meshes.end(), instance.mesh) - meshes.begin()); const auto at = i * ProbeSdfObjectTexels;
        if(partial&&!std::binary_search(changedObjects.begin(),changedObjects.end(),std::uint32_t(i))) {
            maximumWorldMeshError_=std::max(maximumWorldMeshError_,(instance.globalMesh?instance.globalMesh:instance.mesh)->InterpolationError()*objectPixels_[at+6].w);
            continue;
        }
        const auto inverse = glm::inverse(instance.localToWorld); for (int c = 0; c != 4; ++c) objectPixels_[at + c] = inverse[c];
        const auto scales = AxisScales(instance); const float minimum = std::min({scales.x, scales.y, scales.z}), maximum = std::max({scales.x, scales.y, scales.z});
        objectPixels_[at + 4] = glm::vec4(mesh.Origin(), float(offsets[asset]));
        objectPixels_[at + 5] = glm::vec4(mesh.Spacing(), mesh.Closed() ? 1.f : 0.f);
        objectPixels_[at + 6] = glm::vec4(glm::vec3(mesh.Counts()), minimum);
        objectPixels_[at + 7] = glm::vec4(mesh.BoundsMin(), float(asset));
        objectPixels_[at + 8] = glm::vec4(mesh.BoundsMax(), maximum);
        objectPixels_[at + 9] = glm::vec4(mesh.InterpolationError(), mesh.InterpolationError() * minimum, instance.movable ? 1.f : 0.f, float(instance.userId));
        const auto bounds = WorldBounds(instance);
        const auto globalBounds=WorldBounds(GlobalInstance(instance));globalBoundsMin_[i]=globalBounds.lo;globalBoundsMax_[i]=globalBounds.hi;
        objectPixels_[at + 10] = glm::vec4(bounds.lo, mesh.SurfaceBias()); objectPixels_[at + 11] = glm::vec4(bounds.hi, float(mesh.Resolution()));
        maximumWorldMeshError_ = std::max(maximumWorldMeshError_, (instance.globalMesh?instance.globalMesh:instance.mesh)->InterpolationError() * minimum);
    }
    statistics_.meshAssets = std::uint32_t(meshes.size()); statistics_.instances = std::uint32_t(instances_.size());
    statistics_.metadataMilliseconds=std::chrono::duration<double,std::milli>(Clock::now()-metadataBegin).count();
}
void ProbeSdfScene::UpdateGeometry(bool topologyChanged,bool incremental,bool snapshotRowCatchup) {
    ++geometryGeneration_;
    std::vector<ProbeTriangle> triangles;
    if(incremental){
        const auto transformBegin=Clock::now();
        std::size_t total=0;for(const auto& instance:instances_)total+=GeometryMesh(instance)->Triangles().size();
        if(topologyChanged||transformedTriangles_.size()!=total){transformedTriangles_.resize(total);geometryInputs_.clear();}
        auto& changed=changedGeometryTriangles_;changed.clear();std::size_t offset=0;
        for(std::size_t i=0;i<instances_.size();++i){const auto& instance=instances_[i];const auto& source=GeometryMesh(instance)->Triangles();
            bool same=i<geometryInputs_.size()&&GeometryMesh(instance)==GeometryMesh(geometryInputs_[i]);
            if(same)for(unsigned c=0;c<4;++c)same&=instance.localToWorld[c]==geometryInputs_[i].localToWorld[c];
            if(!same){changed.reserve(changed.size()+source.size());for(std::size_t local=0;local<source.size();++local){auto t=source[local];
                t.a=glm::vec3(instance.localToWorld*glm::vec4(t.a,1));t.b=glm::vec3(instance.localToWorld*glm::vec4(t.b,1));
                t.c=glm::vec3(instance.localToWorld*glm::vec4(t.c,1));transformedTriangles_[offset+local]=t;changed.push_back(std::uint32_t(offset+local));}}
            offset+=source.size();
        }geometryInputs_=instances_;
        statistics_.geometryTransformMilliseconds=std::chrono::duration<double,std::milli>(Clock::now()-transformBegin).count();
        const auto refitBegin=Clock::now();
        const bool refit=!topologyChanged&&packedGeometry_&&packedGeometry_->TriangleCount()==total;
        packedGeometry_=refit?
            RefitRealtimeGiSceneChanged(*packedGeometry_,transformedTriangles_,changed,snapshotRowCatchup):BuildRealtimeGiScene(transformedTriangles_);
        if(refit){statistics_.geometrySnapshotMilliseconds=packedGeometry_->RefitCpuStatistics().snapshotMilliseconds;
            statistics_.geometryRefitMilliseconds=packedGeometry_->RefitCpuStatistics().refitMilliseconds;}
        else statistics_.geometryRefitMilliseconds=std::chrono::duration<double,std::milli>(Clock::now()-refitBegin).count();
        return;
    } else {
    for (const auto& instance : instances_) for (auto triangle : GeometryMesh(instance)->Triangles()) {
        triangle.a = glm::vec3(instance.localToWorld * glm::vec4(triangle.a, 1));
        triangle.b = glm::vec3(instance.localToWorld * glm::vec4(triangle.b, 1));
        triangle.c = glm::vec3(instance.localToWorld * glm::vec4(triangle.c, 1));
        triangles.push_back(triangle);
    }
    }
    packedGeometry_ = !topologyChanged && packedGeometry_ && packedGeometry_->TriangleCount() == triangles.size()
        ? RefitRealtimeGiScene(*packedGeometry_, triangles) : BuildRealtimeGiScene(triangles);
    geometryInputs_=instances_;transformedTriangles_=std::move(triangles);
}
void ProbeSdfScene::RebuildBricks(std::span<const std::uint8_t> staticDirty, std::span<const std::uint8_t> dynamicDirty) {
    const auto begin = Clock::now(); const auto bricks = BrickCounts(); dirtyBricks_.clear(); dirtyRanges_.clear();
    statistics_.updatedVoxels = statistics_.updatedBricks = statistics_.objectDistanceQueries = statistics_.staticVoxelUpdates = statistics_.dynamicVoxelUpdates = 0;
    for (std::uint32_t z = 0; z < bricks.z; ++z) for (std::uint32_t y = 0; y < bricks.y; ++y) for (std::uint32_t x = 0; x < bricks.x; ++x) {
        const auto brick = glm::uvec3(x, y, z); const auto index = At(brick, bricks); if (!staticDirty[index] && !dynamicDirty[index]) continue;
        dirtyBricks_.push_back(brick); ++statistics_.updatedBricks;
        const auto first = brick * settings_.brickSize, end = glm::min(first + glm::uvec3(settings_.brickSize), settings_.counts);
        // Bin each affected brick to its relevant objects, avoiding a full
        // object-list scan for every voxel in larger scenes.
        std::vector<std::uint32_t> staticObjects, dynamicObjects;
        const auto brickLo = settings_.origin + glm::vec3(first) * settings_.spacing;
        const auto brickHi = settings_.origin + glm::vec3(end - glm::uvec3(1)) * settings_.spacing;
        for (std::uint32_t object = 0; object < instances_.size(); ++object) {
            const auto instance = GlobalInstance(instances_[object]); const auto bounds = WorldBounds(instance); const auto scales = AxisScales(instance);
            const float maximum = std::max({scales.x, scales.y, scales.z});
            const float fieldPadding = 4 * std::max({instance.mesh->Spacing().x, instance.mesh->Spacing().y, instance.mesh->Spacing().z}) * maximum;
            const float radius = std::max(settings_.maxDistance+instance.mesh->SurfaceBias()*std::min({scales.x,scales.y,scales.z}), fieldPadding) + instance.mesh->InterpolationError() * std::min({scales.x, scales.y, scales.z});
            if (glm::any(glm::greaterThan(bounds.lo - glm::vec3(radius), brickHi)) || glm::any(glm::lessThan(bounds.hi + glm::vec3(radius), brickLo))) continue;
            (instance.movable ? dynamicObjects : staticObjects).push_back(object);
        }
        for (std::uint32_t vz = first.z; vz < end.z; ++vz) for (std::uint32_t vy = first.y; vy < end.y; ++vy) for (std::uint32_t vx = first.x; vx < end.x; ++vx) {
            const auto at = At({vx, vy, vz}, settings_.counts); const auto p = settings_.origin + glm::vec3(vx, vy, vz) * settings_.spacing;
            const auto compose = [&](const std::vector<std::uint32_t>& objects) {
                glm::vec4 result(settings_.maxDistance, 0, 0, 0);
                for (const auto object : objects) { ++statistics_.objectDistanceQueries; const float distance = ObjectDistance(object, p);
                    if (distance < result.x) result = glm::vec4(distance, float(object + 1), 0, 0);
                }
                return result;
            };
            if (staticDirty[index]) { staticPixels_[at] = compose(staticObjects); ++statistics_.staticVoxelUpdates; }
            if (dynamicDirty[index]) { dynamicPixels_[at] = compose(dynamicObjects); ++statistics_.dynamicVoxelUpdates; }
            const auto a = staticPixels_[at], b = dynamicPixels_[at], nearest = a.x <= b.x ? a : b;
            globalPixels_[at] = glm::vec4(nearest.x, nearest.y, a.x, b.x); ++statistics_.updatedVoxels;
        }
        for (std::uint32_t vz = first.z; vz < end.z; ++vz) for (std::uint32_t vy = first.y; vy < end.y; ++vy)
            dirtyRanges_.push_back({std::uint32_t(At({first.x, vy, vz}, settings_.counts)), end.x - first.x});
    }
    std::sort(dirtyRanges_.begin(), dirtyRanges_.end(), [](auto a, auto b) { return a.offset < b.offset; });
    std::vector<ProbeSdfAtlasRange> merged;
    for (auto range : dirtyRanges_) {
        if (!merged.empty() && merged.back().offset + merged.back().count == range.offset) merged.back().count += range.count;
        else merged.push_back(range);
    }
    dirtyRanges_ = std::move(merged);
    ++statistics_.generation;
    if(!dirtyBricks_.empty())++globalGeneration_;
    statistics_.updateMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}
std::shared_ptr<ProbeSdfScene> BuildProbeSdfScene(std::span<const ProbeSdfInstance> instances, const ProbeGlobalSdfSettings& settings) {
    if (!Finite(settings.origin) || !Finite(settings.spacing) || glm::any(glm::lessThan(settings.spacing, glm::vec3(.001f))) ||
        glm::any(glm::lessThan(settings.counts, glm::uvec3(2))) || glm::any(glm::greaterThan(settings.counts, glm::uvec3(256))) ||
        VoxelCount(settings.counts) > 4 * 1024 * 1024 || settings.brickSize < 2 || settings.brickSize > 16 ||
        !std::isfinite(settings.maxDistance) || settings.maxDistance < .05f || settings.maxDistance > 10 || instances.size() > 256)
        throw std::invalid_argument("Invalid global SDF volume or instance budget");
    if (settings.clipmapLevels && (!Finite(settings.clipmapCamera) || settings.spacing.x != settings.spacing.y || settings.spacing.x != settings.spacing.z))
        throw std::invalid_argument("Clipmap Global SDF needs finite camera and cubic spacing");
    for (const auto& instance : instances) ValidateInstance(instance);
    auto result = std::make_shared<ProbeSdfScene>(); result->settings_ = settings; result->instances_.assign(instances.begin(), instances.end());
    if (settings.clipmapLevels) {
        ProbeSdfClipmapSettings clipSettings; clipSettings.levelCount = settings.clipmapLevels; clipSettings.counts = settings.counts;
        clipSettings.baseSpacing = settings.spacing.x; clipSettings.spacingScale = settings.clipmapSpacingScale;
        clipSettings.brickSize = settings.brickSize; clipSettings.baseMaxDistance = settings.maxDistance;
        result->clipmaps_ = std::make_shared<ProbeSdfClipmapVolume>(clipSettings);
        result->RepackAssets(); result->UpdateGeometry(true);
        result->statistics_.totalVoxels = VoxelCount(settings.counts) * settings.clipmapLevels;
        result->statistics_.totalBricks = VoxelCount(result->BrickCounts()) * settings.clipmapLevels;
        // The caller selects resident per-level sources before the one initial
        // capture, avoiding a throwaway bake of four fallback union volumes.
        return result;
    }
    result->RepackAssets(); result->UpdateGeometry(true);
    const auto pixels = Padded(VoxelCount(settings.counts));
    result->globalPixels_.resize(pixels, glm::vec4(settings.maxDistance, 0, settings.maxDistance, settings.maxDistance));
    result->staticPixels_.resize(pixels, glm::vec4(settings.maxDistance, 0, 0, 0)); result->dynamicPixels_ = result->staticPixels_;
    result->statistics_.totalVoxels = VoxelCount(settings.counts); result->statistics_.totalBricks = VoxelCount(result->BrickCounts()); result->statistics_.fullRebuild = true;
    std::vector<std::uint8_t> all(result->statistics_.totalBricks, 1); result->RebuildBricks(all, all); return result;
}
bool ProbeSdfScene::UpdateInstances(std::span<const ProbeSdfInstance> instances,bool deferGlobalField,bool incrementalGeometry,bool snapshotRowCatchup) {
    statistics_.geometryTransformMilliseconds=statistics_.geometrySnapshotMilliseconds=statistics_.geometryRefitMilliseconds=statistics_.metadataMilliseconds=0;
    if (instances.size() > 256) throw std::invalid_argument("Too many SDF instances");
    for (const auto& instance : instances) ValidateInstance(instance);
    bool changed = instances.size() != instances_.size(), topologyChanged = changed,geometryChanged=changed,geometryTopologyChanged=changed;
    std::vector<std::uint32_t> changedObjects;
    if(incrementalGeometry)changedObjects.reserve(instances.size());
    for (std::size_t i = 0; i < std::min(instances.size(), instances_.size()); ++i) {
        const bool objectChanged=!Same(instances[i],instances_[i]);
        if(incrementalGeometry&&objectChanged)changedObjects.push_back(std::uint32_t(i));
        changed |= objectChanged;topologyChanged |= instances[i].mesh != instances_[i].mesh;
        geometryTopologyChanged |= GeometryMesh(instances[i])!=GeometryMesh(instances_[i]);geometryChanged |= geometryTopologyChanged;
        for(int c=0;c<4;++c)geometryChanged |= instances[i].localToWorld[c]!=instances_[i].localToWorld[c];
    }
    // Stable mesh topology keeps all atlas offsets and object IDs. Refresh
    // inverse transforms/bounds only for changed instances, like Lumen's
    // pending primitive update list; the original full path stays available.
    const auto repack=[&](){RepackAssets(topologyChanged,
        incrementalGeometry&&!topologyChanged?std::span<const std::uint32_t>(changedObjects):std::span<const std::uint32_t>{});};
    if (clipmaps_) {
        if (!changed) return false;
        instances_.assign(instances.begin(), instances.end()); repack();
        if (geometryChanged) UpdateGeometry(geometryTopologyChanged,incrementalGeometry,snapshotRowCatchup);
        ++statistics_.generation;
        return true;
    }
    if(!changed&&globalFieldDeferred_&&!deferGlobalField){EnsureGlobalField();return true;}
    if(changed&&deferGlobalField) {
        instances_.assign(instances.begin(),instances.end());repack();
        if(geometryChanged)UpdateGeometry(geometryTopologyChanged,incrementalGeometry,snapshotRowCatchup);
        globalFieldDeferred_=true;++statistics_.generation;
        dirtyBricks_.clear();dirtyRanges_.clear();
        statistics_.updatedVoxels=statistics_.updatedBricks=statistics_.objectDistanceQueries=statistics_.staticVoxelUpdates=statistics_.dynamicVoxelUpdates=0;
        statistics_.updateMilliseconds=0;statistics_.fullRebuild=false;return true;
    }
    if (!changed) { dirtyBricks_.clear(); dirtyRanges_.clear(); statistics_.updatedVoxels = statistics_.updatedBricks = statistics_.objectDistanceQueries = statistics_.staticVoxelUpdates = statistics_.dynamicVoxelUpdates = 0; statistics_.updateMilliseconds = 0; statistics_.fullRebuild = false; return false; }
    const auto bricks = BrickCounts(); std::vector<std::uint8_t> staticDirty(VoxelCount(bricks),globalFieldDeferred_?1:0), dynamicDirty(staticDirty);
    for (std::size_t i = 0; i < std::max(instances.size(), instances_.size()); ++i) {
        const bool oldExists = i < instances_.size(), newExists = i < instances.size();
        if (oldExists && newExists && Same(GlobalInstance(instances_[i]),GlobalInstance(instances[i]))) continue;
        if (oldExists) DirtyBounds(GlobalInstance(instances_[i]), settings_, bricks, instances_[i].movable ? dynamicDirty : staticDirty);
        if (newExists) DirtyBounds(GlobalInstance(instances[i]), settings_, bricks, instances[i].movable ? dynamicDirty : staticDirty);
    }
    // A changed array index changes its owner ID, including untouched later
    // objects; invalidate both cached owner fields when topology is replaced.
    if (instances.size() != instances_.size()) { std::fill(staticDirty.begin(), staticDirty.end(), 1); std::fill(dynamicDirty.begin(), dynamicDirty.end(), 1); }
    instances_.assign(instances.begin(), instances.end()); repack();if(geometryChanged)UpdateGeometry(geometryTopologyChanged,incrementalGeometry,snapshotRowCatchup);
    statistics_.fullRebuild = instances.size() == 0 || (std::all_of(staticDirty.begin(), staticDirty.end(), [](auto x) { return x != 0; }) && std::all_of(dynamicDirty.begin(), dynamicDirty.end(), [](auto x) { return x != 0; }));
    RebuildBricks(staticDirty, dynamicDirty);globalFieldDeferred_=false; return true;
}
bool ProbeSdfScene::UpdateGlobalClipmaps(glm::vec3 camera,
    std::span<const std::array<std::shared_ptr<const ProbeMeshSdf>, 4>> levelMeshes) {
    if (!clipmaps_) return false;
    const bool changed = clipmaps_->Update(instances_, camera, levelMeshes);
    settings_.clipmapCamera = camera;
    const auto& source = clipmaps_->Statistics();
    globalGeneration_ = clipmaps_->Generation();
    if (changed) ++statistics_.generation;
    statistics_.updatedVoxels = source.updatedVoxels; statistics_.updatedBricks = source.updatedBricks;
    statistics_.totalVoxels = source.totalVoxels; statistics_.totalBricks = source.totalBricks;
    statistics_.objectDistanceQueries = source.objectDistanceQueries;
    statistics_.staticVoxelUpdates = source.staticVoxelUpdates; statistics_.dynamicVoxelUpdates = source.dynamicVoxelUpdates;
    statistics_.updateMilliseconds = source.updateMilliseconds; statistics_.fullRebuild = source.fullLevels != 0;
    maximumWorldMeshError_ = 0;
    for (const auto& level : clipmaps_->Levels()) maximumWorldMeshError_ = std::max(maximumWorldMeshError_, level.sourceInterpolationError);
    return changed;
}
} // namespace Render
