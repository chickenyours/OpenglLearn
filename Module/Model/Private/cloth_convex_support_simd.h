#pragma once

#include <glm/glm.hpp>
#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace Model::detail {

// A snapshot of the complete support cloud, packed once for all refit planes.
// Prepare owns its copy and retains vector capacity for later refits. Maximum
// is read-only and can be shared between threads after preparation. The caller
// validates finite source points and normals; no support point is discarded.
//
// SIMD availability is captured by Prepare, using ClothConvexSimdAvailable's
// CPU/OS gate and MODEL_CLOTH_SIMD / MODEL_CLOTH_CONVEX_SIMD overrides. Changing
// an override takes effect on the next Prepare, as with convex query packets.
class PreparedClothSupportCloud {
public:
    void Prepare(std::span<const glm::dvec3> points);
    // Exact max of GLM's ((nx*x + ny*y) + nz*z), initialized to -infinity.
    // Empty clouds return -infinity. Equal zero maxima retain the first
    // source point's sign, matching an ordered scalar std::max reduction.
    double Maximum(glm::dvec3 normal)const;
    std::size_t Size()const noexcept{return count_;}
    bool UsesSimd()const noexcept{return simd_;}

private:
    struct alignas(32) Packet {std::array<double,4> x,y,z;};
    std::vector<Packet> packets_;
    std::size_t count_=0;
    bool simd_=false;
    double MaximumScalar(glm::dvec3 normal)const;
    double MaximumAvx2(glm::dvec3 normal)const;
};

} // namespace Model::detail
