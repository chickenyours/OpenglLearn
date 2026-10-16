#include "cloth_area_simd.h"
#include "cloth_simd.h"
#include <algorithm>
#include <cmath>
#include <limits>

#if (defined(__clang__) || defined(__GNUC__)) && (defined(_M_X64) || defined(__x86_64__))
#define MODEL_CLOTH_AREA_AVX2_IMPLEMENTED 1
#define MODEL_CLOTH_AREA_AVX2 __attribute__((target("avx2,no-fma")))
#define MODEL_CLOTH_AREA_AVX2_INLINE __attribute__((target("avx2,no-fma"),always_inline)) inline
#include <immintrin.h>
#else
#define MODEL_CLOTH_AREA_AVX2_IMPLEMENTED 0
#endif

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace Model::detail {
namespace {
constexpr double Epsilon=1e-12;

void SolveScalar(const std::array<std::uint32_t,3>& t,std::size_t index,
    const double* restAreas,const double* restAreaScales,
    glm::dvec3* positions,const double* inverseMass,double* lambda,double alpha){
    const double rest=restAreas[index],scale=restAreaScales[index];
    if(!std::isfinite(rest)||rest<=Epsilon||!std::isfinite(scale)||scale<=0||!std::isfinite(lambda[index]))return;
    const auto a=positions[t[0]],b=positions[t[1]],c=positions[t[2]],cross=glm::cross(b-a,c-a);
    const double area2=glm::dot(cross,cross);if(!std::isfinite(area2))return;
    const double twiceArea=std::sqrt(std::max(0.0,area2));if(twiceArea<=Epsilon)return;
    if(twiceArea*.5==rest&&lambda[index]==0)return;
    const auto normal=cross/twiceArea;
    const glm::dvec3 gradients[3]={glm::cross(b-c,normal)*(.5*scale),glm::cross(c-a,normal)*(.5*scale),glm::cross(a-b,normal)*(.5*scale)};
    double denominator=alpha;for(int j=0;j<3;++j)denominator+=inverseMass[t[j]]*glm::dot(gradients[j],gradients[j]);
    if(!std::isfinite(denominator)||denominator<=Epsilon)return;
    const double dl=(-((twiceArea*.5-rest)*scale)-alpha*lambda[index])/denominator;
    if(!std::isfinite(dl)||!std::isfinite(lambda[index]+dl))return;
    lambda[index]+=dl;for(int j=0;j<3;++j)positions[t[j]]+=gradients[j]*(inverseMass[t[j]]*dl);
}

#if MODEL_CLOTH_AREA_AVX2_IMPLEMENTED
struct Packet3 {__m256d x,y,z;};
MODEL_CLOTH_AREA_AVX2_INLINE __m256d Set(double v){return _mm256_set1_pd(v);}
MODEL_CLOTH_AREA_AVX2_INLINE __m256d Neg(__m256d v){return _mm256_xor_pd(v,Set(-0.0));}
MODEL_CLOTH_AREA_AVX2_INLINE __m256d Blend(__m256d a,__m256d b,__m256d mask){return _mm256_blendv_pd(a,b,mask);}
MODEL_CLOTH_AREA_AVX2_INLINE __m256d Finite(__m256d v){return _mm256_cmp_pd(_mm256_andnot_pd(Set(-0.0),v),Set(std::numeric_limits<double>::max()),_CMP_LE_OQ);}
MODEL_CLOTH_AREA_AVX2_INLINE Packet3 Add(Packet3 a,Packet3 b){return {_mm256_add_pd(a.x,b.x),_mm256_add_pd(a.y,b.y),_mm256_add_pd(a.z,b.z)};}
MODEL_CLOTH_AREA_AVX2_INLINE Packet3 Sub(Packet3 a,Packet3 b){return {_mm256_sub_pd(a.x,b.x),_mm256_sub_pd(a.y,b.y),_mm256_sub_pd(a.z,b.z)};}
MODEL_CLOTH_AREA_AVX2_INLINE Packet3 Mul(Packet3 a,__m256d b){return {_mm256_mul_pd(a.x,b),_mm256_mul_pd(a.y,b),_mm256_mul_pd(a.z,b)};}
MODEL_CLOTH_AREA_AVX2_INLINE Packet3 Div(Packet3 a,__m256d b){return {_mm256_div_pd(a.x,b),_mm256_div_pd(a.y,b),_mm256_div_pd(a.z,b)};}
MODEL_CLOTH_AREA_AVX2_INLINE __m256d Dot(Packet3 a,Packet3 b){return _mm256_add_pd(_mm256_add_pd(_mm256_mul_pd(a.x,b.x),_mm256_mul_pd(a.y,b.y)),_mm256_mul_pd(a.z,b.z));}
MODEL_CLOTH_AREA_AVX2_INLINE Packet3 Cross(Packet3 a,Packet3 b){return {
    _mm256_sub_pd(_mm256_mul_pd(a.y,b.z),_mm256_mul_pd(b.y,a.z)),
    _mm256_sub_pd(_mm256_mul_pd(a.z,b.x),_mm256_mul_pd(b.z,a.x)),
    _mm256_sub_pd(_mm256_mul_pd(a.x,b.y),_mm256_mul_pd(b.x,a.y))};}
MODEL_CLOTH_AREA_AVX2_INLINE Packet3 Gather(const glm::dvec3* positions,__m128i ids){
    const auto offsets=_mm_mullo_epi32(ids,_mm_set1_epi32(sizeof(glm::dvec3)));
    return {_mm256_i32gather_pd(&positions[0].x,offsets,1),_mm256_i32gather_pd(&positions[0].y,offsets,1),_mm256_i32gather_pd(&positions[0].z,offsets,1)};
}
MODEL_CLOTH_AREA_AVX2_INLINE void Scatter(glm::dvec3* positions,__m128i ids,Packet3 values){
    alignas(16) std::uint32_t indices[4];alignas(32) double x[4],y[4],z[4];
    _mm_store_si128(reinterpret_cast<__m128i*>(indices),ids);_mm256_store_pd(x,values.x);_mm256_store_pd(y,values.y);_mm256_store_pd(z,values.z);
    for(int lane=0;lane<4;++lane)positions[indices[lane]]={x[lane],y[lane],z[lane]};
}

MODEL_CLOTH_AREA_AVX2_INLINE void SolvePacket(const std::array<std::uint32_t,3>* triangles,
    const double* restAreas,const double* restAreaScales,const std::uint32_t* ids,
    glm::dvec3* positions,const double* inverseMass,double* lambda,double alpha){
    const auto triangleIndices=_mm_setr_epi32(ids[0],ids[1],ids[2],ids[3]);
    const auto rest=_mm256_i32gather_pd(restAreas,triangleIndices,8),scale=_mm256_i32gather_pd(restAreaScales,triangleIndices,8),previousLambda=_mm256_i32gather_pd(lambda,triangleIndices,8);
    auto active=_mm256_and_pd(_mm256_and_pd(Finite(rest),_mm256_cmp_pd(rest,Set(Epsilon),_CMP_GT_OQ)),
        _mm256_and_pd(_mm256_and_pd(Finite(scale),_mm256_cmp_pd(scale,Set(0),_CMP_GT_OQ)),Finite(previousLambda)));
    if(_mm256_movemask_pd(active)==0)return;
    __m128i vertices[3];Packet3 points[3];__m256d masses[3];
    for(int j=0;j<3;++j){vertices[j]=_mm_setr_epi32(triangles[ids[0]][j],triangles[ids[1]][j],triangles[ids[2]][j],triangles[ids[3]][j]);
        points[j]=Gather(positions,vertices[j]);masses[j]=_mm256_i32gather_pd(inverseMass,vertices[j],8);}
    const auto cross=Cross(Sub(points[1],points[0]),Sub(points[2],points[0]));const auto area2=Dot(cross,cross);
    active=_mm256_and_pd(active,Finite(area2));
    const auto twiceArea=_mm256_sqrt_pd(Blend(Set(1),_mm256_max_pd(Set(0),area2),active));
    // Keep the scalar threshold after sqrt, including immediately adjacent
    // double values around the degeneracy boundary.
    active=_mm256_and_pd(active,_mm256_cmp_pd(twiceArea,Set(Epsilon),_CMP_GT_OQ));
    const auto atRest=_mm256_and_pd(_mm256_cmp_pd(_mm256_mul_pd(twiceArea,Set(.5)),rest,_CMP_EQ_OQ),_mm256_cmp_pd(previousLambda,Set(0),_CMP_EQ_OQ));
    active=_mm256_andnot_pd(atRest,active);if(_mm256_movemask_pd(active)==0)return;
    const auto normal=Div(cross,Blend(Set(1),twiceArea,active));const auto gradientScale=_mm256_mul_pd(Set(.5),scale);
    const Packet3 gradients[3]={Mul(Cross(Sub(points[1],points[2]),normal),gradientScale),Mul(Cross(Sub(points[2],points[0]),normal),gradientScale),Mul(Cross(Sub(points[0],points[1]),normal),gradientScale)};
    auto denominator=Set(alpha);for(int j=0;j<3;++j)denominator=_mm256_add_pd(denominator,_mm256_mul_pd(masses[j],Dot(gradients[j],gradients[j])));
    active=_mm256_and_pd(active,_mm256_and_pd(Finite(denominator),_mm256_cmp_pd(denominator,Set(Epsilon),_CMP_GT_OQ)));
    if(_mm256_movemask_pd(active)==0)return;
    const auto constraint=_mm256_mul_pd(_mm256_sub_pd(_mm256_mul_pd(twiceArea,Set(.5)),rest),scale);
    const auto dl=_mm256_div_pd(_mm256_sub_pd(Neg(constraint),_mm256_mul_pd(Set(alpha),previousLambda)),Blend(Set(1),denominator,active));
    const auto updatedLambda=_mm256_add_pd(previousLambda,dl);active=_mm256_and_pd(active,_mm256_and_pd(Finite(dl),Finite(updatedLambda)));
    if(_mm256_movemask_pd(active)==0)return;
    alignas(32) double lambdaValues[4];_mm256_store_pd(lambdaValues,Blend(previousLambda,updatedLambda,active));for(int lane=0;lane<4;++lane)lambda[ids[lane]]=lambdaValues[lane];
    for(int j=0;j<3;++j){const auto next=Add(points[j],Mul(gradients[j],_mm256_mul_pd(masses[j],dl)));
        Scatter(positions,vertices[j],{Blend(points[j].x,next.x,active),Blend(points[j].y,next.y,active),Blend(points[j].z,next.z,active)});}
}

MODEL_CLOTH_AREA_AVX2 void SolveAvx2(const std::array<std::uint32_t,3>* triangles,
    const double* restAreas,const double* restAreaScales,const std::uint32_t* indices,std::size_t count,
    glm::dvec3* positions,const double* inverseMass,double* lambda,double alpha){
    std::size_t offset=0;for(;offset+4<=count;offset+=4){const std::uint32_t ids[4]={indices?indices[offset]:std::uint32_t(offset),indices?indices[offset+1]:std::uint32_t(offset+1),indices?indices[offset+2]:std::uint32_t(offset+2),indices?indices[offset+3]:std::uint32_t(offset+3)};
        SolvePacket(triangles,restAreas,restAreaScales,ids,positions,inverseMass,lambda,alpha);}
    for(;offset<count;++offset){const auto k=indices?indices[offset]:std::uint32_t(offset);SolveScalar(triangles[k],k,restAreas,restAreaScales,positions,inverseMass,lambda,alpha);}
}
#endif
} // namespace

void SolveClothAreas(const std::array<std::uint32_t,3>* triangles,
    const double* restAreas,const double* restAreaScales,const std::uint32_t* indices,std::size_t count,
    glm::dvec3* positions,const double* inverseMass,double* lambda,double alpha){
    if(count==0||!std::isfinite(alpha)||alpha<0)return;
#if MODEL_CLOTH_AREA_AVX2_IMPLEMENTED
    if(ClothSimdAvailable()){SolveAvx2(triangles,restAreas,restAreaScales,indices,count,positions,inverseMass,lambda,alpha);return;}
#endif
    for(std::size_t offset=0;offset<count;++offset){const auto k=indices?indices[offset]:std::uint32_t(offset);SolveScalar(triangles[k],k,restAreas,restAreaScales,positions,inverseMass,lambda,alpha);}
}

} // namespace Model::detail
