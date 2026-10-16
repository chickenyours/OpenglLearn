#pragma once
#include "Render/Public/Pipeline/probe_sdf_scene.h"
#include <array>

namespace Render {
inline constexpr std::uint32_t ProbeSdfClipmapMaximumLevels = 4;
using ProbeSdfClipmapMeshes = std::array<std::shared_ptr<const ProbeMeshSdf>, ProbeSdfClipmapMaximumLevels>;

struct ProbeSdfClipmapSettings {
    std::uint32_t levelCount = 4;
    glm::uvec3 counts{96};
    float baseSpacing = .03125f, spacingScale = 4;
    std::uint32_t brickSize = 4;
    // Each level truncates at max(baseMaxDistance, narrowBandVoxels * spacing).
    float baseMaxDistance = .25f, narrowBandVoxels = 4;
};
enum class ProbeSdfClipmapFullReason : std::uint8_t { None, Initial, Teleport };
struct ProbeSdfClipmapLevelStatistics {
    std::uint64_t updatedVoxels = 0, enteringVoxels = 0, updatedBricks = 0;
    std::uint64_t staticVoxelUpdates = 0, dynamicVoxelUpdates = 0, objectDistanceQueries = 0;
    std::uint64_t totalVoxels = 0, totalBricks = 0;
    ProbeSdfClipmapFullReason fullReason = ProbeSdfClipmapFullReason::None;
};
struct ProbeSdfClipmapLevel {
    glm::vec3 origin{0}, spacing{1};
    glm::uvec3 counts{0}, scroll{0};
    std::uint32_t atlasOffset = 0;
    glm::uvec3 rangeCounts{0};
    std::uint32_t rangeAtlasOffset = 0;
    float narrowBand = 0, sourceInterpolationError = 0;
    std::uint64_t generation = 0;
    ProbeSdfClipmapLevelStatistics statistics;
};
struct ProbeSdfClipmapStatistics {
    std::uint64_t generation = 0, updatedVoxels = 0, enteringVoxels = 0, updatedBricks = 0;
    std::uint64_t staticVoxelUpdates = 0, dynamicVoxelUpdates = 0, objectDistanceQueries = 0;
    std::uint64_t totalVoxels = 0, totalBricks = 0;
    std::uint32_t fullLevels = 0, scrolledLevels = 0;
    double updateMilliseconds = 0;
};

// Dense camera-centered clipmaps, with toroidal physical storage. No local
// asset baking or triangle BVH construction happens in this class. Source
// fields are immutable and may be shared between levels and instances.
class ProbeSdfClipmapVolume {
  public:
    explicit ProbeSdfClipmapVolume(const ProbeSdfClipmapSettings& settings = {});
    ~ProbeSdfClipmapVolume();
    ProbeSdfClipmapVolume(ProbeSdfClipmapVolume&&) noexcept;
    ProbeSdfClipmapVolume& operator=(ProbeSdfClipmapVolume&&) noexcept;
    ProbeSdfClipmapVolume(const ProbeSdfClipmapVolume&) = delete;
    ProbeSdfClipmapVolume& operator=(const ProbeSdfClipmapVolume&) = delete;

    // Empty per-level meshes use instance.globalMesh, then instance.mesh.
    // Otherwise provide one array per instance; null array entries use the
    // same fallback. Changes to a local mesh alone do not dirty resident fields.
    // Camera centers are floor-snapped to brickSize * spacing on each axis.
    // Throws invalid_argument before mutation for invalid inputs/transforms.
    bool Update(std::span<const ProbeSdfInstance> instances, glm::vec3 camera,
                std::span<const ProbeSdfClipmapMeshes> levelMeshes = {});
    const ProbeSdfClipmapSettings& Settings() const;
    const std::vector<ProbeSdfClipmapLevel>& Levels() const;
    // Each level starts at a 1024-texel-aligned offset. Physical texel is
    // atlasOffset + ((physical.z * counts.y + physical.y) * counts.x + physical.x).
    // physical = (logical + scroll) % counts; world = origin + logical * spacing.
    // RGBA = (union distance, closest instance index + 1, static, movable).
    // The owner is zero if no distance is strictly within the narrow band.
    const std::vector<glm::vec4>& Pixels() const;
    // Actual physical texel ranges rewritten by the latest Update. Ranges
    // may cross texture rows; GPU uploaders split/coalesce them at Width().
    const std::vector<ProbeSdfAtlasRange>& DirtyRanges() const;
    // One RGBA texel (minimum, maximum, 0, 0) per physical brick. Includes
    // brickSize + 1 nodes along each axis, including all shared cell corners.
    // Physical node wrap at the ring boundary only widens this conservative
    // range. Each level is Width()-aligned, with X-fast rangeCounts indexing.
    const std::vector<glm::vec4>& RangePixels() const;
    // Same Update/Generation as Pixels(); includes affected negative-axis
    // neighboring bricks because their cells share rewritten corner samples.
    const std::vector<ProbeSdfAtlasRange>& RangeDirtyRanges() const;
    const ProbeSdfClipmapStatistics& Statistics() const;
    std::uint64_t Generation() const;
    std::uint32_t Width() const { return ProbeSdfAtlasWidth; }
    // Trilinear distance/static/movable samples, nearest sample's owner.
    // Interpolation never wraps logical boundary neighbors to the other side.
    // Outside the inclusive sample extent, throws out_of_range.
    glm::vec4 SampleWorld(glm::vec3 worldPosition, std::uint32_t level) const;
    glm::vec4 SampleLogical(glm::uvec3 logicalVoxel, std::uint32_t level) const;
    // Reference merge input; useful for verifying updated grids independently.
    float ObjectDistance(std::uint32_t objectIndex, glm::vec3 worldPosition, std::uint32_t level) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace Render
