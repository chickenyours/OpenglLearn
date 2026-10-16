#pragma once

#include "Model/Public/cloth_asset.h"
#include <cstddef>

namespace Model::detail {

struct ClothDistancePrepared {
    double rest=0,massSum=0,inverseDenominator=0;
};

// All constraints in a call must be particle-disjoint (one graph color).
// indices==nullptr selects [0,count). Prepared values and lambda are always
// indexed by the original constraint index. Uses ClothSimdAvailable() and
// transparently falls back to scalar when AVX2 is unavailable or disabled.
void SolveClothDistances(const ClothDistanceConstraint* constraints,const ClothDistancePrepared* prepared,
    const std::uint32_t* indices,std::size_t count,glm::dvec3* positions,
    const double* inverseMass,double* lambda,double alpha);

} // namespace Model::detail
