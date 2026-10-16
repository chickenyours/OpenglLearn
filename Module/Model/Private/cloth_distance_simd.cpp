#include "cloth_distance_simd.h"
#include "cloth_simd.h"
#include <algorithm>
#include <cmath>

#if (defined(__clang__) || defined(__GNUC__)) && (defined(_M_X64) || defined(__x86_64__))
#define MODEL_CLOTH_DISTANCE_AVX2_IMPLEMENTED 1
#define MODEL_CLOTH_DISTANCE_AVX2 __attribute__((target("avx2,no-fma")))
#define MODEL_CLOTH_DISTANCE_AVX2_INLINE __attribute__((target("avx2,no-fma"),always_inline)) inline
#include <immintrin.h>
#else
#define MODEL_CLOTH_DISTANCE_AVX2_IMPLEMENTED 0
#endif

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace Model::detail {
namespace {
constexpr double Epsilon=1e-12;

void SolveScalar(const ClothDistanceConstraint& constraint,const ClothDistancePrepared& prepared,
        std::size_t index,glm::dvec3* positions,const double* inverseMass,double* lambda,double alpha){
    if(prepared.massSum<=0)return;
    const auto d=positions[constraint.a]-positions[constraint.b];
    const double length=std::sqrt(std::max(0.,glm::dot(d,d)));
    if(length<=Epsilon)return;
    const double residual=length-prepared.rest;
    if(residual==0&&lambda[index]==0)return;
    const double dl=(-residual-alpha*lambda[index])*prepared.inverseDenominator;
    lambda[index]+=dl;
    const auto correction=d*(dl/length);
    positions[constraint.a]+=correction*inverseMass[constraint.a];
    positions[constraint.b]-=correction*inverseMass[constraint.b];
}

#if MODEL_CLOTH_DISTANCE_AVX2_IMPLEMENTED
struct Packet3 {__m256d x,y,z;};
MODEL_CLOTH_DISTANCE_AVX2_INLINE __m256d Set(double value){return _mm256_set1_pd(value);}
MODEL_CLOTH_DISTANCE_AVX2_INLINE __m256d Neg(__m256d value){return _mm256_xor_pd(value,Set(-0.));}
MODEL_CLOTH_DISTANCE_AVX2_INLINE Packet3 GatherPositions(const glm::dvec3* positions,__m128i ids){
    const auto offsets=_mm_mullo_epi32(ids,_mm_set1_epi32(sizeof(glm::dvec3)));
    return {_mm256_i32gather_pd(&positions[0].x,offsets,1),_mm256_i32gather_pd(&positions[0].y,offsets,1),
        _mm256_i32gather_pd(&positions[0].z,offsets,1)};
}
MODEL_CLOTH_DISTANCE_AVX2_INLINE Packet3 Sub(Packet3 a,Packet3 b){
    return {_mm256_sub_pd(a.x,b.x),_mm256_sub_pd(a.y,b.y),_mm256_sub_pd(a.z,b.z)};
}
MODEL_CLOTH_DISTANCE_AVX2_INLINE Packet3 Add(Packet3 a,Packet3 b){
    return {_mm256_add_pd(a.x,b.x),_mm256_add_pd(a.y,b.y),_mm256_add_pd(a.z,b.z)};
}
MODEL_CLOTH_DISTANCE_AVX2_INLINE Packet3 Mul(Packet3 a,__m256d value){
    return {_mm256_mul_pd(a.x,value),_mm256_mul_pd(a.y,value),_mm256_mul_pd(a.z,value)};
}
MODEL_CLOTH_DISTANCE_AVX2_INLINE __m256d Dot(Packet3 value){
    return _mm256_add_pd(_mm256_add_pd(_mm256_mul_pd(value.x,value.x),_mm256_mul_pd(value.y,value.y)),
        _mm256_mul_pd(value.z,value.z));
}
MODEL_CLOTH_DISTANCE_AVX2_INLINE void ScatterPositions(glm::dvec3* positions,__m128i ids,Packet3 value,int active){
    alignas(16) std::uint32_t indices[4];alignas(32) double x[4],y[4],z[4];
    _mm_store_si128(reinterpret_cast<__m128i*>(indices),ids);
    _mm256_store_pd(x,value.x);_mm256_store_pd(y,value.y);_mm256_store_pd(z,value.z);
    for(int lane=0;lane<4;++lane)if(active&(1<<lane))positions[indices[lane]]={x[lane],y[lane],z[lane]};
}
MODEL_CLOTH_DISTANCE_AVX2_INLINE void SolvePacket(const ClothDistanceConstraint* constraints,const ClothDistancePrepared* prepared,
        const std::uint32_t* indices,glm::dvec3* positions,const double* inverseMass,double* lambda,double alpha){
    const auto k=_mm_loadu_si128(reinterpret_cast<const __m128i*>(indices));
    const auto a=_mm_setr_epi32(constraints[indices[0]].a,constraints[indices[1]].a,
        constraints[indices[2]].a,constraints[indices[3]].a);
    const auto b=_mm_setr_epi32(constraints[indices[0]].b,constraints[indices[1]].b,
        constraints[indices[2]].b,constraints[indices[3]].b);
    const auto preparedOffsets=_mm_mullo_epi32(k,_mm_set1_epi32(sizeof(ClothDistancePrepared)));
    const auto massSum=_mm256_i32gather_pd(&prepared[0].massSum,preparedOffsets,1);
    const auto positiveMass=_mm256_cmp_pd(massSum,Set(0),_CMP_NLE_UQ);
    if(_mm256_movemask_pd(positiveMass)==0)return;
    const auto pa=GatherPositions(positions,a),pb=GatherPositions(positions,b),d=Sub(pa,pb);
    // max(dot,0) also matches std::max(0,dot)'s zero result for NaN dot.
    const auto length=_mm256_sqrt_pd(_mm256_max_pd(Dot(d),Set(0)));
    const auto rest=_mm256_i32gather_pd(&prepared[0].rest,preparedOffsets,1);
    const auto multiplier=_mm256_i32gather_pd(lambda,k,8);
    const auto residual=_mm256_sub_pd(length,rest);
    const auto nonzero=_mm256_or_pd(_mm256_cmp_pd(residual,Set(0),_CMP_NEQ_UQ),
        _mm256_cmp_pd(multiplier,Set(0),_CMP_NEQ_UQ));
    const auto activeMask=_mm256_and_pd(_mm256_and_pd(positiveMass,
        _mm256_cmp_pd(length,Set(Epsilon),_CMP_NLE_UQ)),nonzero);
    const int active=_mm256_movemask_pd(activeMask);
    if(!active)return;
    const auto inverseDenominator=_mm256_i32gather_pd(&prepared[0].inverseDenominator,preparedOffsets,1);
    const auto dl=_mm256_mul_pd(_mm256_sub_pd(Neg(residual),_mm256_mul_pd(Set(alpha),multiplier)),inverseDenominator);
    // Exact hardware sqrt/div and the scalar multiplication association are
    // deliberate: correction=(a-b)*(dl/length), then correction*inverseMass.
    const auto safeLength=_mm256_blendv_pd(Set(1),length,activeMask);
    const auto correction=Mul(d,_mm256_div_pd(dl,safeLength));
    const auto massA=_mm256_i32gather_pd(inverseMass,a,8),massB=_mm256_i32gather_pd(inverseMass,b,8);
    ScatterPositions(positions,a,Add(pa,Mul(correction,massA)),active);
    ScatterPositions(positions,b,Sub(pb,Mul(correction,massB)),active);
    alignas(32) double updated[4];_mm256_store_pd(updated,_mm256_add_pd(multiplier,dl));
    for(int lane=0;lane<4;++lane)if(active&(1<<lane))lambda[indices[lane]]=updated[lane];
}
MODEL_CLOTH_DISTANCE_AVX2 void SolveAvx2(const ClothDistanceConstraint* constraints,const ClothDistancePrepared* prepared,
        const std::uint32_t* indices,std::size_t count,glm::dvec3* positions,const double* inverseMass,double* lambda,double alpha){
    std::size_t offset=0;
    for(;offset+4<=count;offset+=4){
        const std::uint32_t contiguous[]={static_cast<std::uint32_t>(offset),static_cast<std::uint32_t>(offset+1),
            static_cast<std::uint32_t>(offset+2),static_cast<std::uint32_t>(offset+3)};
        SolvePacket(constraints,prepared,indices?indices+offset:contiguous,positions,inverseMass,lambda,alpha);
    }
    for(;offset<count;++offset){const auto index=indices?indices[offset]:static_cast<std::uint32_t>(offset);
        SolveScalar(constraints[index],prepared[index],index,positions,inverseMass,lambda,alpha);}
}
#endif
} // namespace

void SolveClothDistances(const ClothDistanceConstraint* constraints,const ClothDistancePrepared* prepared,
        const std::uint32_t* indices,std::size_t count,glm::dvec3* positions,const double* inverseMass,double* lambda,double alpha){
#if MODEL_CLOTH_DISTANCE_AVX2_IMPLEMENTED
    if(ClothSimdAvailable()){SolveAvx2(constraints,prepared,indices,count,positions,inverseMass,lambda,alpha);return;}
#endif
    for(std::size_t offset=0;offset<count;++offset){const auto index=indices?indices[offset]:static_cast<std::uint32_t>(offset);
        SolveScalar(constraints[index],prepared[index],index,positions,inverseMass,lambda,alpha);}
}

} // namespace Model::detail
