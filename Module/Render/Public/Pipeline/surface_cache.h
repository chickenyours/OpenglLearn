#pragma once
#include "Render/Public/Pipeline/probe_sdf_scene.h"
#include <limits>
#include <unordered_map>

namespace Render {
inline constexpr std::uint32_t SurfaceCacheAtlasWidth=1024, SurfaceCacheCardTexels=12;
struct SurfaceCacheMaterial {
    glm::vec3 albedo{.7f},emission{0};
    bool operator==(const SurfaceCacheMaterial&) const = default;
};
struct SurfaceCacheSettings { std::uint32_t pageResolution=32; };
struct SurfaceCacheStatistics {
    std::uint32_t cards=0,localCaptures=0,updatedCards=0;
    std::uint64_t validTexels=0,updatedTexels=0,captureRays=0;
    double updateMilliseconds=0;
};
struct SurfaceCacheSample {
    bool valid=false;
    std::uint32_t texel=0,card=0,object=0;
    glm::vec3 position{0},normal{0},albedo{0},emission{0};
};
// Minimal Lumen-style card cache: six single-depth orthographic cards per
// instance, fixed resident pages and no filtering/history. Captures are shared
// in mesh-local space; camera and MDF quality changes never invalidate them.
// Concavities invisible to all six captures deliberately remain uncovered.
class SurfaceCacheScene {
public:
    explicit SurfaceCacheScene(const SurfaceCacheSettings& settings={});
    bool Update(const ProbeSdfScene&,std::span<const SurfaceCacheMaterial> materials={});
    const SurfaceCacheSettings& Settings() const {return settings_;}
    // Three stacked width-1024 planes, each PlaneTexels() long:
    // world position/valid, world normal/card+1, linear albedo/valid.
    const std::vector<glm::vec4>& Attributes() const {return attributes_;}
    // Twelve vec4s per card: inverse transform[0..3]; local min/direction[4];
    // local max/first texel[5]; outward world normal/depth tolerance[6];
    // albedo/object[7]; emission/generation[8]; world min/resolution[9];
    // world max/0[10]; reserved[11]. Directions are +X,-X,+Y,-Y,+Z,-Z.
    const std::vector<glm::vec4>& Metadata() const {return metadata_;}
    const std::vector<std::uint32_t>& DirtyCards() const {return dirtyCards_;}
    std::uint32_t Width() const {return SurfaceCacheAtlasWidth;}
    std::uint32_t AtlasHeight() const {return std::uint32_t(attributes_.size()/Width());}
    std::uint32_t MetadataHeight() const {return std::uint32_t(metadata_.size()/Width());}
    std::uint32_t CardCount() const {return std::uint32_t(states_.size())*6;}
    // Packed BVH triangle owner IDs follow card records, four zero-based
    // instance IDs per vec4. Unused entries are -1.
    std::uint32_t TriangleOwnerOffset() const {return CardCount()*SurfaceCacheCardTexels;}
    std::uint32_t TriangleCount() const {return ownerGeometry_?ownerGeometry_->TriangleCount():0;}
    std::uint32_t TexelCount() const {return CardCount()*settings_.pageResolution*settings_.pageResolution;}
    std::uint32_t PlaneTexels() const {return planeTexels_;}
    std::uint64_t Generation() const {return generation_;}
    const SurfaceCacheStatistics& Statistics() const {return statistics_;}
    SurfaceCacheSample Lookup(glm::vec3 worldPosition,glm::vec3 worldNormal,
        std::uint32_t objectIndex=std::numeric_limits<std::uint32_t>::max()) const;
private:
    struct LocalCapture;
    struct InstanceState {
        std::shared_ptr<const ProbeMeshSdf> mesh;
        std::shared_ptr<const LocalCapture> capture;
        glm::mat4 transform{1};
        SurfaceCacheMaterial material;
    };
    SurfaceCacheSettings settings_;
    std::vector<InstanceState> states_;
    std::unordered_map<const ProbeMeshSdf*,std::shared_ptr<const LocalCapture>> captures_;
    std::shared_ptr<const RealtimeGiScene> ownerGeometry_;
    std::vector<glm::vec4> attributes_,metadata_;
    std::vector<std::uint32_t> dirtyCards_;
    SurfaceCacheStatistics statistics_;
    std::uint32_t planeTexels_=SurfaceCacheAtlasWidth;
    std::uint64_t generation_=0;
};
using SurfaceCache=SurfaceCacheScene;
std::shared_ptr<SurfaceCacheScene> BuildSurfaceCache(const ProbeSdfScene&,
    std::span<const SurfaceCacheMaterial> materials={},const SurfaceCacheSettings& settings={});
} // namespace Render
