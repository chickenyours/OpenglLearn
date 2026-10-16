#include "cloth_convex_support_simd.h"
#include "cloth_convex.h"
#include <algorithm>
#include <limits>

#if (defined(__clang__) || defined(__GNUC__)) && (defined(_M_X64) || defined(__x86_64__))
#define MODEL_CLOTH_SUPPORT_AVX2_IMPLEMENTED 1
#define MODEL_CLOTH_SUPPORT_AVX2 __attribute__((target("avx2,no-fma")))
#include <immintrin.h>
#else
#define MODEL_CLOTH_SUPPORT_AVX2_IMPLEMENTED 0
#define MODEL_CLOTH_SUPPORT_AVX2
#endif

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace Model::detail {

void PreparedClothSupportCloud::Prepare(std::span<const glm::dvec3> points){
    count_=points.size();packets_.resize(count_/4+(count_%4!=0));
    for(std::size_t i=0;i<count_;++i){auto& packet=packets_[i/4];const auto lane=i%4;
        packet.x[lane]=points[i].x;packet.y[lane]=points[i].y;packet.z[lane]=points[i].z;}
    // Unused tail lanes are never queried. In particular, padding with zeros
    // must not change the maximum of an entirely negative support cloud.
#if MODEL_CLOTH_SUPPORT_AVX2_IMPLEMENTED
    simd_=ClothConvexSimdAvailable();
#else
    simd_=false;
#endif
}

double PreparedClothSupportCloud::MaximumScalar(glm::dvec3 normal)const{
    double maximum=-std::numeric_limits<double>::infinity();
    for(std::size_t i=0;i<count_;++i){const auto& packet=packets_[i/4];const auto lane=i%4;
        const double value=(normal.x*packet.x[lane]+normal.y*packet.y[lane])+normal.z*packet.z[lane];
        maximum=std::max(maximum,value);}
    return maximum;
}

MODEL_CLOTH_SUPPORT_AVX2 double PreparedClothSupportCloud::MaximumAvx2(glm::dvec3 normal)const{
#if MODEL_CLOTH_SUPPORT_AVX2_IMPLEMENTED
    const auto nx=_mm256_set1_pd(normal.x),ny=_mm256_set1_pd(normal.y),nz=_mm256_set1_pd(normal.z);
    auto maximum=_mm256_set1_pd(-std::numeric_limits<double>::infinity());
    const std::size_t complete=count_/4;
    // One ISA dispatch covers every packet for this plane. No AoS gathers or
    // repeated conversion are needed as the plane normal changes.
    for(std::size_t i=0;i<complete;++i){const auto& packet=packets_[i];
        const auto value=_mm256_add_pd(_mm256_add_pd(
            _mm256_mul_pd(nx,_mm256_load_pd(packet.x.data())),
            _mm256_mul_pd(ny,_mm256_load_pd(packet.y.data()))),
            _mm256_mul_pd(nz,_mm256_load_pd(packet.z.data())));
        // MAXPD returns its second operand for an unordered comparison. Keep
        // the accumulated numeric value there, matching std::max's treatment
        // of a later NaN even if finite inputs overflow during their dot.
        maximum=_mm256_max_pd(value,maximum);
    }
    alignas(32) double lanes[4];_mm256_store_pd(lanes,maximum);
    double result=-std::numeric_limits<double>::infinity();
    for(double lane:lanes)result=std::max(result,lane);
    for(std::size_t i=complete*4;i<count_;++i){const auto& packet=packets_[i/4];const auto lane=i%4;
        const double value=(normal.x*packet.x[lane]+normal.y*packet.y[lane])+normal.z*packet.z[lane];
        result=std::max(result,value);}
    // A lane-wise reduction can reorder +0/-0 ties. Only a zero maximum needs
    // the original source order; ordinary nonzero maxima are bit-identical.
    return result==0?MaximumScalar(normal):result;
#else
    return MaximumScalar(normal);
#endif
}

double PreparedClothSupportCloud::Maximum(glm::dvec3 normal)const{
    return simd_?MaximumAvx2(normal):MaximumScalar(normal);
}

} // namespace Model::detail
