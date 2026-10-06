#pragma once
#include <algorithm>
#include "Render/Public/Pipeline/realtime_gi.h"
#include "Render/Public/Pipeline/mesh_distance_field.h"

namespace Render {
inline constexpr std::uint32_t LumenGiBinding=10, LumenSurfaceWidth=1024, MaxLumenSurfels=65536;
inline constexpr std::uint32_t MaxLumenPartialNeighbours=6;
// Project-specific hybrid GI, not Epic's Lumen implementation. Surface texels
// parameterize BVH triangles on regular, folded barycentric lattices.
class LumenGiScene {
public:
    const std::shared_ptr<const RealtimeGiScene>& Geometry() const {return geometry_;}
    const std::vector<glm::vec4>& Attributes() const {return attributes_;}
    const std::shared_ptr<const MeshDistanceField>& DistanceField() const {return distanceField_;}
    bool DistanceFieldCurrent() const {return distanceFieldCurrent_;}
    std::uint32_t SurfelCount() const {return surfels_;}
    std::uint32_t EmissiveTriangleCount() const {return emissiveTriangles_;}
    std::uint32_t AdjacencyOffset() const {return geometry_->TriangleCount()+surfels_*2+emissiveTriangles_;}
    // Adjacency.xyz retain complete-edge neighbors. W is zero or an absolute
    // attribute index whose header.x is a count <= MaxLumenPartialNeighbours;
    // subsequent vec4s store (neighbor triangle, receiver edge, edge t0, t1).
    // Edge t follows the original triangle's vertex order, normalized 0..1.
    std::uint32_t PartialAdjacencyOffset() const {return AdjacencyOffset()+geometry_->TriangleCount();}
    std::uint32_t AttributeHeight() const {return std::uint32_t(attributes_.size()/LumenSurfaceWidth);}
    std::uint32_t CacheHeight() const {return std::max(1u,(surfels_+LumenSurfaceWidth-1)/LumenSurfaceWidth);}
private:
    std::shared_ptr<const RealtimeGiScene> geometry_;
    std::shared_ptr<const MeshDistanceField> distanceField_;
    std::vector<glm::vec4> attributes_;
    std::uint32_t surfels_=0;
    std::uint32_t emissiveTriangles_=0;
    bool distanceFieldCurrent_=true;
    friend std::shared_ptr<const LumenGiScene> BuildLumenGiScene(std::span<const ProbeTriangle>,float,std::uint32_t);
    friend std::shared_ptr<const LumenGiScene> RefitLumenGiScene(const LumenGiScene&,std::span<const ProbeTriangle>);
};
// Existing scenes retain the sixteen-cell cap; isolated labs can request 32.
// The global surfel budget remains bounded independently of this cap.
std::shared_ptr<const LumenGiScene> BuildLumenGiScene(std::span<const ProbeTriangle>,float surfaceTexelSize=.35f,std::uint32_t maxTriangleResolution=16);
std::shared_ptr<const LumenGiScene> RefitLumenGiScene(const LumenGiScene&,std::span<const ProbeTriangle>);
// Diagnostic views share the traced diffuse transport solution. They do not
// change light sources or the persistent transport/cache scene.
enum class LumenLightingView : std::uint32_t { Material=0, SurfaceCache=1, IndirectOnly=2, DirectOnly=3 };
struct LumenGiSettings {
    bool enabled=false,reset=false,screenTraces=true,reflections=true;
    bool distanceFields=true;
    // Resolve analytic direct light at visible receivers in diagnostic views.
    // The surface cache still supplies indirect light and all bounce transport.
    // This avoids magnifying its coarse binary visibility / spotlight footprint.
    bool fullResolutionDirectLighting=false;
    // Evaluate analytic direct light at diffuse ray hits instead of magnifying
    // the coarse direct cache's visibility and narrow spotlight footprint.
    bool traceDirectLighting=false;
    uint32_t reflectionRays=4;
    uint32_t reflectionTraceMaxDimension=256; // longest tracing grid edge; 320/512 for finer reflection detail
    float reflectionResolutionScale=.5f;
    // HDR screen-hit color capture only; independent of the reflection resolve
    // and full internal depth/normal buffers. .25..1 relative to internal size.
    float reflectionSourceResolutionScale=.5f;
    float maxReflectionRoughness=.4f; // rougher surfaces reuse the irradiance cache; zero keeps dedicated rays
    float intensity=1,maxDistance=30,rayBias=.015f,thickness=.15f;
    float surfaceHistory=.65f,bounceFeedback=.9f,gatherHistory=.85f;
    std::uint32_t surfaceUpdatesPerFrame=512,surfaceRays=16;
    std::uint32_t probeSpacing=16,gatherRays=12,screenSteps=16;
    // Low-frequency radiance cache for transparency, mirrored views and
    // disoccluded pixels without matching screen probes; no CPU light bake.
    glm::vec3 origin{-2.5f,.4f,-2.5f},spacing{1.666667f,1.066667f,1.666667f};
    glm::uvec3 counts{4,4,4};
    std::shared_ptr<const LumenGiScene> scene;
    LumenLightingView lightingView=LumenLightingView::Material;
};
bool ValidateLumenGiSettings(const LumenGiSettings&,std::string* error=nullptr);
struct alignas(16) LumenGiConstants {
    glm::mat4 inverseView{1},previousVP{1};
    glm::vec4 trace{30,.015f,.9f,.65f};
    glm::vec4 settings{1,.15f,1,1}; // intensity, thickness, cache reset, screen tracing
    glm::ivec4 atlas{1024,0,0,0}; // width, surfels, emissive triangle count, surfel attribute offset
    glm::ivec4 budget{0,0,8,12}; // first/count, surface/gather ray count
    glm::ivec4 gather{0,0,16,0}; // grid size, screen steps, history valid / surface hit-direct flag
    glm::vec4 camera{0,0,0,.85f};
    glm::vec4 fieldOrigin{0},fieldSpacing{1,1,1,0}; // W: dedicated reflection roughness limit; zero for isolated ray tests
    glm::ivec4 fieldCounts{0};
    glm::vec4 reflection{4,0,1,0}; // rays, stage flag (live direct / reflection), resolve X scale, Fresnel coefficient
};
static_assert(sizeof(LumenGiConstants)==288);
} // namespace Render
