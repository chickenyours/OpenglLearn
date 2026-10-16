#pragma once
#include "Render/Public/Pipeline/realtime_gi.h"
#include <array>
#include <span>

namespace Render {
class ProbeSdfClipmapVolume;
inline constexpr std::uint32_t ProbeSdfAtlasWidth = 1024;
inline constexpr std::uint32_t ProbeSdfObjectTexels = 12;

struct ProbeSdfBakeStatistics {
    std::uint64_t voxels = 0, distanceTriangleTests = 0, signTriangleTests = 0, nodeVisits = 0;
    double milliseconds = 0;
};

// Reusable local-space asset. Geometry supplies bake/reference acceleration;
// runtime marching consumes its distance samples, rather than tracing the BVH.
class ProbeMeshSdf {
  public:
    const std::vector<glm::vec4>& Pixels() const { return pixels_; }
    const std::vector<ProbeTriangle>& Triangles() const { return triangles_; }
    const std::shared_ptr<const RealtimeGiScene>& Geometry() const { return geometry_; }
    glm::vec3 Origin() const { return origin_; }
    glm::vec3 Spacing() const { return spacing_; }
    glm::uvec3 Counts() const { return counts_; }
    glm::vec3 BoundsMin() const { return boundsMin_; }
    glm::vec3 BoundsMax() const { return boundsMax_; }
    bool Closed() const { return closed_; }
    std::uint32_t Resolution() const { return resolution_; }
    float InterpolationError() const { return interpolationError_; }
    float SurfaceBias() const { return surfaceBias_; } // explicit thin-surface iso expansion, local units
    const ProbeSdfBakeStatistics& Statistics() const { return statistics_; }
    float Sample(glm::vec3 localPosition) const;
    float ConservativeDistance(glm::vec3 localPosition) const;

  private:
    std::vector<glm::vec4> pixels_;
    std::vector<ProbeTriangle> triangles_;
    std::shared_ptr<const RealtimeGiScene> geometry_;
    glm::vec3 origin_{0}, spacing_{1}, boundsMin_{0}, boundsMax_{0};
    glm::uvec3 counts_{1};
    bool closed_ = false;
    std::uint32_t resolution_=40;
    float interpolationError_ = 0,surfaceBias_ = 0;
    ProbeSdfBakeStatistics statistics_;
    friend std::shared_ptr<const ProbeMeshSdf> BakeProbeMeshSdf(std::span<const ProbeTriangle>, std::uint32_t, bool,float);
};
std::shared_ptr<const ProbeMeshSdf> BakeProbeMeshSdf(std::span<const ProbeTriangle> triangles,
                                                  std::uint32_t longestAxis = 40, bool closedMesh = true,float surfaceExpansionVoxels = 0);

struct ProbeSdfInstance {
    std::shared_ptr<const ProbeMeshSdf> mesh;
    glm::mat4 localToWorld{1};
    bool movable = false;
    std::uint32_t userId = 0;
    // Optional resident coarse field and source geometry. A camera-driven
    // local-field change then needs neither a Global rebuild nor a BVH refit.
    std::shared_ptr<const ProbeMeshSdf> globalMesh, geometryMesh;
};
struct ProbeGlobalSdfSettings {
    glm::vec3 origin{-6, -.5f, -6};
    glm::vec3 spacing{.125f};
    glm::uvec3 counts{97, 49, 97};
    std::uint32_t brickSize = 4;
    float maxDistance = 1;
    // Zero preserves the fixed-volume path. Enabled clipmaps use counts per
    // level and spacing.x as the finest cubic cell size; origin is unused.
    std::uint32_t clipmapLevels = 0;
    float clipmapSpacingScale = 4;
    glm::vec3 clipmapCamera{0};
};
struct ProbeSdfAtlasRange { std::uint32_t offset = 0, count = 0; };
struct ProbeSdfStatistics {
    std::uint64_t generation = 0, updatedVoxels = 0, updatedBricks = 0, totalVoxels = 0, totalBricks = 0;
    std::uint64_t objectDistanceQueries = 0, staticVoxelUpdates = 0, dynamicVoxelUpdates = 0;
    std::uint64_t bakedVoxels = 0, distanceTriangleTests = 0, signTriangleTests = 0;
    std::uint32_t meshAssets = 0, instances = 0;
    double updateMilliseconds = 0;
    bool fullRebuild = false;
};

// Fixed-volume or camera-centered clipmap union. Static and movable fields
// are separately cached; movement dirties old/new expanded object bounds.
class ProbeSdfScene {
  public:
    const ProbeGlobalSdfSettings& Settings() const { return settings_; }
    const std::vector<ProbeSdfInstance>& Instances() const { return instances_; }
    // All atlases use x-fast 3D voxel indexing, flattened into width 1024.
    // Mesh texel = (local signed/unsigned distance, nearest triangle ID, 0, 0).
    const std::vector<glm::vec4>& MeshPixels() const { return meshPixels_; }
    // Twelve texels per instance: inverse matrix columns [0..3], field origin
    // and voxel offset [4], spacing and closed flag [5], counts and min scale
    // [6], local geometry min and asset index [7], local geometry max and max
    // scale [8], local/world interpolation error + movable/user ID [9], world
    // geometry min/max [10..11], local surface bias in [10].w and actual
    // resolution in [11].w. World error uses min scale, matching distance.
    const std::vector<glm::vec4>& ObjectPixels() const { return objectPixels_; }
    // (union distance, nearest object index + 1, static distance, movable
    // distance). Zero owner means no object within the truncated narrow band.
    const std::vector<glm::vec4>& GlobalPixels() const;
    const std::shared_ptr<ProbeSdfClipmapVolume>& Clipmaps() const { return clipmaps_; }
    // Logical brick coordinates for the legacy fixed volume only. Clipmap
    // updates expose physical atlas ranges and per-level brick statistics.
    const std::vector<glm::uvec3>& DirtyBricks() const { return dirtyBricks_; }
    // Linear atlas ranges actually changed by the latest update. Split these
    // at Width() boundaries to issue partial texture-row uploads.
    const std::vector<ProbeSdfAtlasRange>& DirtyRanges() const;
    std::uint64_t MeshGeneration() const { return meshGeneration_; }
    std::uint64_t GlobalGeneration() const { return globalGeneration_; }
    std::uint64_t GeometryGeneration() const { return geometryGeneration_; }
    const ProbeSdfStatistics& Statistics() const { return statistics_; }
    const std::shared_ptr<const RealtimeGiScene>& PackedGeometry() const { return packedGeometry_; }
    std::uint32_t Width() const { return ProbeSdfAtlasWidth; }
    float MaximumWorldMeshError() const { return maximumWorldMeshError_; }
    glm::uvec3 BrickCounts() const;
    float ObjectDistance(std::uint32_t objectIndex, glm::vec3 worldPosition) const;
    glm::vec4 SampleGlobal(glm::uvec3 voxel) const;
    // Returns false when nothing changed. No-op updates preserve generation.
    // In clipmap mode this prepares metadata only; explicitly call the global
    // update once after all source/instance changes for the frame are ready.
    bool UpdateInstances(std::span<const ProbeSdfInstance> instances);
    // Build defers the first clipmap capture to this call. Optional arrays
    // provide one source per level and instance (null uses globalMesh/mesh).
    // Camera scrolling never changes the source triangle BVH generation.
    bool UpdateGlobalClipmaps(glm::vec3 camera,
        std::span<const std::array<std::shared_ptr<const ProbeMeshSdf>, 4>> levelMeshes = {});

  private:
    ProbeGlobalSdfSettings settings_;
    std::vector<ProbeSdfInstance> instances_;
    std::vector<glm::vec4> meshPixels_, objectPixels_, globalPixels_, staticPixels_, dynamicPixels_;
    std::vector<glm::vec3> globalBoundsMin_,globalBoundsMax_;
    std::vector<glm::uvec3> dirtyBricks_;
    std::vector<ProbeSdfAtlasRange> dirtyRanges_;
    std::shared_ptr<const RealtimeGiScene> packedGeometry_;
    std::shared_ptr<ProbeSdfClipmapVolume> clipmaps_;
    ProbeSdfStatistics statistics_;
    float maximumWorldMeshError_ = 0;
    std::uint64_t meshGeneration_ = 0;
    std::uint64_t globalGeneration_ = 0,geometryGeneration_ = 0;
    void RepackAssets(bool rebuildMeshAtlas = true);
    void UpdateGeometry(bool topologyChanged);
    void RebuildBricks(std::span<const std::uint8_t> staticDirty, std::span<const std::uint8_t> dynamicDirty);
    friend std::shared_ptr<ProbeSdfScene> BuildProbeSdfScene(std::span<const ProbeSdfInstance>, const ProbeGlobalSdfSettings&);
};
std::shared_ptr<ProbeSdfScene> BuildProbeSdfScene(std::span<const ProbeSdfInstance> instances,
                                              const ProbeGlobalSdfSettings& settings = {});
} // namespace Render
