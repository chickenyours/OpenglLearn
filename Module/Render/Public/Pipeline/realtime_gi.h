#pragma once
#include <vector>
#include "Render/Public/Pipeline/diffuse_probe_volume.h"
#include "Render/Public/Pipeline/realtime_gi_query.h"

namespace Render {
inline constexpr std::uint32_t RealtimeGiBinding=15, RealtimeGiCacheTextureSlot=15;
inline constexpr std::uint32_t RealtimeGiCacheRows=26, RealtimeGiMaxRays=128;
// Immutable CPU extraction asset. GPU software tracing consumes this packed
// BVH directly; changing lights does not recreate geometry or bake radiance.
class RealtimeGiScene {
public:
    const std::vector<glm::vec4>& Pixels() const { return pixels_; }
    std::uint32_t Width() const { return 1024; }
    std::uint32_t Height() const { return std::uint32_t(pixels_.size()/Width()); }
    std::uint32_t NodeCount() const { return nodes_; }
    std::uint32_t TriangleCount() const { return triangles_; }
    bool HasEmission() const { return hasEmission_; }
    // Refits preserve triangle IDs and cache layout. The caller must retain
    // input triangle ordering and replace the asset when topology changes.
    const void* TopologyKey() const { return topology_.get(); }
private:
    std::vector<glm::vec4> pixels_;
    std::vector<std::uint32_t> inputOrder_;
    std::shared_ptr<const void> topology_;
    std::uint32_t nodes_=0,triangles_=0;
    bool hasEmission_=false;
    friend std::shared_ptr<const RealtimeGiScene> BuildRealtimeGiScene(std::span<const ProbeTriangle>);
    friend std::shared_ptr<const RealtimeGiScene> RefitRealtimeGiScene(const RealtimeGiScene&,std::span<const ProbeTriangle>);
};
std::shared_ptr<const RealtimeGiScene> BuildRealtimeGiScene(std::span<const ProbeTriangle>);
std::shared_ptr<const RealtimeGiScene> RefitRealtimeGiScene(const RealtimeGiScene&,std::span<const ProbeTriangle>);
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
