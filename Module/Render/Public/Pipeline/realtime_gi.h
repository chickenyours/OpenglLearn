#pragma once
#include <vector>
#include <mutex>
#include <deque>
#include "Render/Public/Pipeline/diffuse_probe_volume.h"
#include "Render/Public/Pipeline/realtime_gi_query.h"

namespace Render {
inline constexpr std::uint32_t RealtimeGiBinding=15, RealtimeGiCacheTextureSlot=15;
inline constexpr std::uint32_t RealtimeGiCacheRows=26, RealtimeGiMaxRays=128;
// Immutable CPU extraction asset. GPU software tracing consumes this packed
// BVH directly; changing lights does not recreate geometry or bake radiance.
class RealtimeGiScene {
public:
    struct DirtyRange {std::uint32_t first=0,count=0;};
    enum class SnapshotCopyFallback : std::uint8_t {
        None,Disabled,PoolEmpty,IncompatibleStorage,MissingJournal,NotAncestor,FullRefitBarrier,JournalFailure
    };
    static constexpr std::size_t SnapshotJournalCapacity=32;
    struct RefitStatistics {
        double snapshotMilliseconds=0,refitMilliseconds=0;
        bool snapshotStorageReused=false,snapshotCatchupUsed=false;
        std::uint64_t snapshotCopiedBytes=0,snapshotPatchedRows=0;
        std::uint32_t snapshotCatchupVersions=0;
        SnapshotCopyFallback snapshotCopyFallback=SnapshotCopyFallback::Disabled;
    };
    const std::vector<glm::vec4>& Pixels() const { return pixels_; }
    std::uint32_t Width() const { return 1024; }
    std::uint32_t Height() const { return std::uint32_t(pixels_.size()/Width()); }
    std::uint32_t NodeCount() const { return nodes_; }
    std::uint32_t TriangleCount() const { return triangles_; }
    // Packed BVH triangle ID -> original extraction order. Consumers such as
    // card caches use this to retain exact instance ownership after BVH sorting.
    const std::vector<std::uint32_t>& InputTriangleIndices() const { return topology_->inputOrder; }
    bool HasEmission() const { return hasEmission_; }
    // Refits preserve triangle IDs and cache layout. The caller must retain
    // input triangle ordering and replace the asset when topology changes.
    const void* TopologyKey() const { return topology_.get(); }
    bool IncrementalUpdate() const {return incremental_;}
    const std::vector<DirtyRange>& DirtyRanges() const {return dirtyRanges_;}
    const RefitStatistics& RefitCpuStatistics() const {return refitStatistics_;}
private:
    std::vector<glm::vec4> pixels_;
    struct SnapshotStoragePool {
        struct Retired {std::vector<glm::vec4> pixels;std::uint64_t version=0;};
        struct Delta {
            std::uint64_t version=0,parent=0;
            bool fullRefitBarrier=false;
            std::vector<DirtyRange> ranges;
        };
        std::mutex mutex;
        std::vector<Retired> available;
        std::deque<Delta> journal;
        std::uint64_t nextVersion=1;
        SnapshotStoragePool(){available.reserve(3);}
    };
    struct Topology {
        std::vector<std::uint32_t> inputOrder,packedForInput,leafForTriangle,parents;
        std::shared_ptr<SnapshotStoragePool> snapshotStorage=std::make_shared<SnapshotStoragePool>();
    };
    std::vector<DirtyRange> dirtyRanges_;
    bool incremental_=false;
    // A refit creates a new immutable pixel snapshot, but never changes these
    // maps. Sharing them also keeps TopologyKey stable without copying every
    // triangle index and parent link during rigid instance movement.
    std::shared_ptr<const Topology> topology_;
    std::uint32_t nodes_=0,triangles_=0;
    std::uint32_t emissiveTriangles_=0;
    bool hasEmission_=false;
    RefitStatistics refitStatistics_;
    std::uint64_t snapshotVersion_=0;
    void PublishSnapshotVersion(std::uint64_t parent,bool fullRefitBarrier,bool recordDelta) noexcept;
    friend std::shared_ptr<const RealtimeGiScene> BuildRealtimeGiScene(std::span<const ProbeTriangle>);
    friend std::shared_ptr<const RealtimeGiScene> RefitRealtimeGiScene(const RealtimeGiScene&,std::span<const ProbeTriangle>);
    friend std::shared_ptr<const RealtimeGiScene> RefitRealtimeGiSceneChanged(const RealtimeGiScene&,std::span<const ProbeTriangle>,std::span<const std::uint32_t>,bool);
};
std::shared_ptr<const RealtimeGiScene> BuildRealtimeGiScene(std::span<const ProbeTriangle>);
std::shared_ptr<const RealtimeGiScene> RefitRealtimeGiScene(const RealtimeGiScene&,std::span<const ProbeTriangle>);
// Certified extraction path: every changed input triangle must be listed.
// Unlisted triangles/materials must be identical to the previous immutable asset.
std::shared_ptr<const RealtimeGiScene> RefitRealtimeGiSceneChanged(const RealtimeGiScene&,std::span<const ProbeTriangle>,std::span<const std::uint32_t>,bool snapshotRowCatchup=false);
struct RealtimeGiSettings {
    bool enabled=false, reset=false;
    glm::vec3 origin{-2.5f,.4f,-2.5f}, spacing{1.666667f,1.066667f,1.666667f};
    glm::uvec3 counts{4,4,4};
    // Fixed world-space Fibonacci quadrature; light changes still retrace.
    // Raise raysPerProbe for small-source/spatial accuracy, not temporal AA.
    std::uint32_t raysPerProbe=64, probesPerFrame=16;
    float maxDistance=30, rayBias=.005f, normalBias=.12f, visibilityBias=.15f;
    float historyWeight=.85f, bounceFeedback=.9f, intensity=1;
    std::shared_ptr<const RealtimeGiScene> scene;
};
bool ValidateRealtimeGiSettings(const RealtimeGiSettings&,std::string* error=nullptr);
struct alignas(16) RealtimeGiConstants {
    glm::vec4 origin{0},spacing{1},counts{0},trace{30,.005f,.85f,.9f};
    glm::ivec4 options{64,0,16,0},geometry{0,1024,0,0};
    glm::vec4 sunDirectionIntensity{0,-1,0,0},sunColor{1},runtime{1,.12f,.15f,0};
    SpotLightConstants spotLight;
};
static_assert(offsetof(RealtimeGiConstants,spotLight)==144);
static_assert(sizeof(RealtimeGiConstants)==192);

} // namespace Render
