#pragma once

#include <glm/glm.hpp>
#include <array>
#include <cstddef>
#include <cstdint>

namespace Model::detail {

struct ClothHinge {
    std::array<std::uint32_t,4> vertices;
    double angle = 0;
};

// CPU and OS AVX2 support, with MODEL_CLOTH_SIMD=0 as a process-start override.
// No target-wide ISA option is required for the Clang/GCC implementation.
bool ClothSimdAvailable();

// Every hinge in this call must be particle-disjoint (one graph color).
// indices==nullptr selects hinges [0,count); otherwise it supplies that color's
// hinge indices. Lambdas are indexed by the original hinge index in either case.
// Unsupported/disabled SIMD transparently uses the scalar reference kernel.
void SolveClothHinges(const ClothHinge* hinges,const std::uint32_t* indices,std::size_t count,
    glm::dvec3* positions,const double* inverseMass,double* lambda,double alpha);

} // namespace Model::detail
