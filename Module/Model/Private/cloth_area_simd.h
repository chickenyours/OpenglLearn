#pragma once

#include <glm/glm.hpp>
#include <array>
#include <cstddef>
#include <cstdint>

namespace Model::detail {

// All triangles in a call must be particle-disjoint (one graph color). Indices
// are validated by the caller. Null indices selects triangles [0,count).
// Rest data and lambdas use the original triangle index, including indexed calls.
// alpha is areaCompliance / (substepDuration * substepDuration).
// AVX2 availability and MODEL_CLOTH_SIMD=0 follow ClothSimdAvailable(); otherwise
// the same double-precision scalar projection is used transparently.
void SolveClothAreas(const std::array<std::uint32_t,3>* triangles,
    const double* restAreas,const double* restAreaScales,
    const std::uint32_t* indices,std::size_t count,
    glm::dvec3* positions,const double* inverseMass,double* lambda,double alpha);

} // namespace Model::detail
