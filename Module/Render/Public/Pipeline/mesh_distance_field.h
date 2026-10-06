#pragma once
#include "Render/Public/Pipeline/realtime_gi.h"
namespace Render {
// Immutable world-space distance volume packed into a 2D atlas, usable through
// the existing RHI. Open meshes use unsigned distance; closed meshes may enable
// even/odd signed distance. Tracing uses conservative absolute-distance bounds
// and exact triangle refinement, so thin surfaces are retained.
class MeshDistanceField {
  public:
    const std::vector<glm::vec4> &Pixels() const { return pixels_; }
    glm::vec3 Origin() const { return origin_; }
    glm::vec3 Spacing() const { return spacing_; }
    glm::uvec3 Counts() const { return counts_; }
    uint32_t Width() const { return 1024; }
    uint32_t Height() const { return uint32_t(pixels_.size() / Width()); }
    float ConservativeDistance(glm::vec3 position) const;
    float Sample(glm::uvec3 cell) const;

  private:
    std::vector<glm::vec4> pixels_;
    glm::vec3 origin_{0}, spacing_{1};
    glm::uvec3 counts_{1};
    friend std::shared_ptr<const MeshDistanceField> BuildMeshDistanceField(const RealtimeGiScene &, uint32_t,
                                                                           bool);
};
std::shared_ptr<const MeshDistanceField>
BuildMeshDistanceField(const RealtimeGiScene &, uint32_t longestAxis = 48, bool closedMesh = false);
} // namespace Render
