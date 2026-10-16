#pragma once
#include "Render/Public/Pipeline/probe_sdf_scene.h"
#include <array>
#include <chrono>
#include <memory>

namespace Render {
// These are distance-volume quality tiers, using the SAME triangle geometry.
// They do not alter the raster mesh or introduce mesh geometry LODs.
enum class ProbeSdfQuality : std::uint32_t { Low = 0, Medium = 1, High = 2 };
inline constexpr std::array<std::uint32_t, 3> ProbeSdfQualityResolutions{24, 40, 64};
std::uint32_t ProbeSdfQualityResolution(ProbeSdfQuality quality);
const char* ProbeSdfQualityName(ProbeSdfQuality quality);
struct ProbeSdfDistanceLodSettings {
    float highDistance = 3, lowDistance = 7, hysteresis = .6f;
};
// Current tier determines a Schmitt-trigger transition. A distant jump may
// skip the medium tier; motion within the threshold bands retains current.
ProbeSdfQuality SelectProbeSdfDistanceQuality(float distance, ProbeSdfQuality current,
                                             const ProbeSdfDistanceLodSettings& settings = {});
struct ProbeSdfLodResult {
    std::shared_ptr<const ProbeMeshSdf> mesh;
    ProbeSdfQuality requestedQuality = ProbeSdfQuality::Medium;
    ProbeSdfQuality activeQuality = ProbeSdfQuality::Medium;
    bool pending = false, changed = false, failed = false;
    std::uint64_t generation = 0;
};
struct ProbeSdfLodStatistics {
    std::uint64_t requests = 0, cacheHits = 0, deduplicatedRequests = 0, generation = 0;
    std::uint64_t bakedVoxels = 0, triangleDistanceTests = 0, distanceBytes = 0;
    std::array<std::uint32_t, 3> completedBakes{}, readyTiers{};
    std::uint32_t assets = 0, seededAssets = 0, queued = 0, working = 0, failedBakes = 0;
    double bakeMilliseconds = 0;
};

// One sequential background worker bakes immutable tiers on demand. Cached
// switches never rebake. Resolve retains the caller's previous ready mesh
// while a requested tier is pending; late completion only populates the cache
// and cannot change a caller's active selection.
class ProbeSdfLodCache {
  public:
    explicit ProbeSdfLodCache(std::array<std::uint32_t,3> resolutions = ProbeSdfQualityResolutions,float surfaceExpansionVoxels = 0);
    ~ProbeSdfLodCache();
    ProbeSdfLodCache(const ProbeSdfLodCache&) = delete;
    ProbeSdfLodCache& operator=(const ProbeSdfLodCache&) = delete;
    // Identical ordered geometry/sign requests share the same asset ID. An
    // optional existing configured-medium bake avoids any startup rebake. Without
    // a seed, registration queues the initial medium tier asynchronously.
    std::uint32_t RegisterMesh(std::span<const ProbeTriangle> triangles, bool closedMesh,
                              std::shared_ptr<const ProbeMeshSdf> seedMedium = {});
    void Request(std::uint32_t asset, ProbeSdfQuality quality);
    std::shared_ptr<const ProbeMeshSdf> Ready(std::uint32_t asset, ProbeSdfQuality quality) const;
    ProbeSdfLodResult Resolve(std::uint32_t asset, ProbeSdfQuality desired,
                             std::shared_ptr<const ProbeMeshSdf> previous = {});
    ProbeSdfLodStatistics Statistics() const;
    std::string Error(std::uint32_t asset, ProbeSdfQuality quality) const;
    // Intended for explicit preload or verification, never per-frame drawing.
    bool WaitIdle(std::chrono::milliseconds timeout = std::chrono::seconds(60)) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace Render
