#pragma once

#include "Model/Public/skeletal_asset.h"
#include <limits>
#include <span>

namespace Model::detail {

// All query positions, distances and corrections are in meters. Skeletal
// geometry and poses retain source units; conversion happens once in Update.
struct HairBodyNearest {
    bool found = false, inside = false, backfacing = false;
    glm::dvec3 point{0}, normal{0,1,0};
    double distance = std::numeric_limits<double>::infinity();
    double signedDistance = std::numeric_limits<double>::infinity();
    std::uint32_t triangle = UINT32_MAX, mesh = UINT32_MAX;
};
struct HairBodyContact {
    bool hit = false, swept = false;
    glm::dvec3 normal{0,1,0}, correction{0}, closestPoint{0};
    double signedGap = std::numeric_limits<double>::infinity();
    double outsideClearance = 0; // endpoint's true outside distance; never a contact exemption
    std::uint32_t triangle = UINT32_MAX;
};
struct HairBodyQueryStats {
    std::uint64_t nearestQueries = 0, sweepQueries = 0;
    std::uint64_t insideOnlyQueries = 0;
    std::uint64_t nodeTests = 0, triangleTests = 0, sweepTriangleTests = 0;
    std::uint64_t insideQueries = 0, rayQueries = 0, rayTriangleTests = 0;
    std::uint64_t componentNodeRejects = 0, nearestHintTests = 0;
    std::uint64_t rayNodeTests = 0, sharedRayTraversals = 0;
    std::uint64_t broadphaseQueries = 0, broadphaseNodeTests = 0, broadphaseTriangleTests = 0;
    std::uint64_t surfaceNearestQueries = 0;
    std::uint64_t nearbyBackfaceQueries = 0, nearbyBackfaceNodeTests = 0, nearbyBackfaceTriangleTests = 0;
    std::uint64_t componentBoundTests = 0, componentBvhNodeTests = 0;
    std::uint64_t componentDopTests = 0, componentDopRejects = 0;
    std::uint64_t rayGridQueries = 0, rayGridCandidateTests = 0, rayGridFallbacks = 0;
    std::uint64_t fastRayTests = 0, fastRayPlaneRejects = 0;
    // Enabled only by MODEL_HAIR_RAY_AUDIT=1 for reference-path tests.
    std::uint64_t rayResultHash = 14695981039346656037ull;
};

// The immutable BVH topology is built once. Update skins only retained body
// vertices and refits current/swept bounds without rebuilding or touching hair.
// Queries are const except for diagnostic counters; use one instance per solver.
class HairBodyCollision {
public:
    HairBodyCollision();
    ~HairBodyCollision();
    HairBodyCollision(HairBodyCollision&&) noexcept;
    HairBodyCollision& operator=(HairBodyCollision&&) noexcept;
    HairBodyCollision(const HairBodyCollision&) = delete;
    HairBodyCollision& operator=(const HairBodyCollision&) = delete;
    bool Initialize(const SkeletalAssetPackage& target,
        std::span<const std::uint32_t> excludedHairMeshes, double unitsPerMeter,
        std::string* error = nullptr);
    bool Update(const SkeletalPose& pose, std::string* error = nullptr);
    // Suppresses sweep across a timeline seek/reset while retaining the current
    // body pose. Normal subsequent Update calls retain the previous body pose.
    void ResetMotion();
    // Negative distance requires finite enclosure: two of three independent
    // bidirectional ray pairs must exit the SAME edge-connected component.
    // For nested body/clothing, distance refers to the deepest enclosing actual
    // component. Open garments can be recognized away from neck/sleeve holes;
    // arbitrary open/nonmanifold surfaces have no mathematically unique solid.
    // Three current-pose 2D triangle grids accelerate the same fixed rays;
    // MODEL_HAIR_RAY_GRID=0 retains the BVH traversal reference. Oversized or
    // numerically invalid grids automatically fall back to that reference.
    // MODEL_HAIR_RAY_GRID_SIDE accepts 64, 128 or 256 (default 256).
    // MODEL_HAIR_FAST_RAYS: 0 retains original grid Moller, 1 (default) caches
    // fixed-ray coefficients, 2 also enables conservative far-plane screening.
    HairBodyNearest Nearest(const glm::dvec3& pointMeters,
        double maxDistanceMeters = std::numeric_limits<double>::infinity(),
        std::uint32_t* triangleHint = nullptr) const;
    // Exact inside-only result. A genuine interior hit has every field equal
    // to unbounded Nearest; exterior and its <=1e-10-meter boundary exception
    // return a default not-found result with no usable outside clearance.
    // MODEL_HAIR_INSIDE_ONLY=0 retains full Nearest followed by that conversion.
    // nearestQueries counts this query once; insideOnlyQueries identifies it.
    HairBodyNearest NearestInside(const glm::dvec3& pointMeters,
        std::uint32_t* triangleHint = nullptr) const;
    // Unsigned exact nearest TRIANGLE SURFACE only: no inside/ray query.
    // signedDistance == distance >= 0, inside=false; normal is the triangle's
    // oriented face normal and backfacing is dot(point-closest,normal)<0.
    // A close backface is only a candidate for Nearest/Contact verification,
    // never proof of solid penetration. No hit does not prove solid outside.
    HairBodyNearest NearestSurface(const glm::dvec3& pointMeters,
        double maxDistanceMeters = .03, std::uint32_t* triangleHint = nullptr) const;
    // Candidate-only query of each nearby component's closest finite face,
    // including outer garments hidden behind a closer inner skin surface.
    // True requires distance <= radius and backface gap < -1e-12 meters;
    // it is not a solid-inside test and performs no ray/inside queries.
    // MODEL_HAIR_COMPONENT_BACKFACE_GATE=0 retains the broader reference query
    // over every nearby finite backface; default/1 use component closest faces.
    // MODEL_HAIR_COMPONENT_BVH=0 retains the linear component-bounds reference;
    // default/1 use refitted bounds and visit candidates in the same ID order.
    bool HasNearbyBackFacingSurface(const glm::dvec3& pointMeters,
        double maxDistanceMeters = .03, std::uint32_t* triangleHint = nullptr) const;
    // Geometrically verified finite enclosure handles reset embedding. A thin
    // open face's back side needs sweep evidence, never a negative half-space.
    // Sweeps solve
    // point versus linearly moving triangles between the two Update poses;
    // they are not a ray test against the final body pose.
    HairBodyContact Contact(const glm::dvec3& previousMeters,
        const glm::dvec3& currentMeters, double marginMeters,
        double maxInsideDistanceMeters = .06, bool sweep = true,
        std::uint32_t* triangleHint = nullptr) const;
    // Only finite triangle crossings; no nearest/inside query or reset push.
    // Local render-surface passes use movingBody=false after the full animated
    // body interval has already been consumed by the first simulation pass.
    HairBodyContact SweepContact(const glm::dvec3& previousMeters,
        const glm::dvec3& currentMeters, double marginMeters,
        bool movingBody = true) const;
    // Conservative triangle-surface broad phase. Query bounds must contain
    // both endpoints of every tested trajectory; margin is added here. False
    // means no finite triangle AABB overlaps, NOT that a point is outside a solid.
    bool MayIntersectSweptBounds(const glm::dvec3& minimumMeters,
        const glm::dvec3& maximumMeters, double marginMeters,
        bool movingBody = true) const;
    // Current-pose component bounds, including open/unreliable components.
    // Point screening additionally tests 49 fixed projection intervals, an
    // outer convex bound. MODEL_HAIR_COMPONENT_DOP=0 retains AABB-only reference
    // screening. Box overlap remains AABB-only and conservative for clusters.
    // False rules out the component-bound necessary condition used by Inside;
    // true is a conservative candidate and does not prove solid enclosure.
    // Points/boxes touching bounds are included. Bounds are normalized per
    // axis; empty bodies and nonfinite inputs return false. Units are meters.
    // These queries never visit triangles or run nearest/inside/ray queries.
    bool MayBeInsideComponentBounds(const glm::dvec3& pointMeters) const;
    bool MayOverlapComponentBounds(const glm::dvec3& minimumMeters,
        const glm::dvec3& maximumMeters) const;
    std::size_t TriangleCount() const;
    std::size_t VertexCount() const;
    std::size_t ComponentCount() const;
    // Object-lifetime revision, initially 1; successful reinitialization also
    // advances it. Update advances only when actual retained body coordinates
    // change. ResetMotion, source hair, unused bones/morphs and identical poses
    // do not advance it. Wrap skips 0; compare only within the same object.
    std::uint64_t GeometryRevision() const;
    std::size_t OpenComponentCount() const;
    // Mean effective normalized skin weight across the actual collision
    // triangle's three vertices. Invalid indices and unweighted fallback
    // vertices contribute zero; duplicate influences on one bone are summed.
    double TriangleBoneWeight(std::uint32_t triangle, std::uint32_t bone) const;
    double LastUpdateMilliseconds() const;
    const HairBodyQueryStats& QueryStats() const;
    void ResetQueryStats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Model::detail
