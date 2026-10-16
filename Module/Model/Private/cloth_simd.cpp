#include "cloth_simd.h"
#include "cloth_math.h"
#include <cmath>
#include <cstdlib>
#include <limits>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if (defined(__clang__) || defined(__GNUC__)) && (defined(_M_X64) || defined(__x86_64__))
#define MODEL_CLOTH_AVX2_IMPLEMENTED 1
#define MODEL_CLOTH_AVX2 __attribute__((target("avx2,no-fma")))
#define MODEL_CLOTH_AVX2_INLINE __attribute__((target("avx2,no-fma"),always_inline)) inline
#include <immintrin.h>
#else
#define MODEL_CLOTH_AVX2_IMPLEMENTED 0
#endif

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace Model::detail {
namespace {
constexpr double Epsilon = 1e-12;
constexpr double Pi = 3.141592653589793238462643383279502884;

void SolveScalar(const ClothHinge& hinge,std::size_t index,glm::dvec3* positions,
        const double* inverseMass,double* lambda,double alpha) {
    const auto& t=hinge.vertices;
    const auto a=positions[t[0]],b=positions[t[1]],c=positions[t[2]],d=positions[t[3]],edge=b-a;
    const double edge2=glm::dot(edge,edge);
    const auto first=glm::cross(edge,c-a),second=glm::cross(d-a,edge);
    const double area1Squared=glm::dot(first,first),area2Squared=glm::dot(second,second);
    if(edge2<=Epsilon*Epsilon||area1Squared<=Epsilon*Epsilon||area2Squared<=Epsilon*Epsilon)return;
    const double edgeLength=std::sqrt(edge2);
    glm::dvec3 gradients[4];
    gradients[2]=first*(-edgeLength/area1Squared);
    gradients[3]=second*(-edgeLength/area2Squared);
    const double u=glm::dot(c-a,edge)/edge2,v=glm::dot(d-a,edge)/edge2;
    gradients[0]=-(1-u)*gradients[2]-(1-v)*gradients[3];
    gradients[1]=-u*gradients[2]-v*gradients[3];
    const double angle=FastAtan2(-edgeLength*glm::dot(first,d-a),glm::dot(first,second));
    const double difference=angle-hinge.angle;
    const double constraint=difference>Pi?difference-2*Pi:(difference< -Pi?difference+2*Pi:difference);
    double denominator=alpha;
    for(int j=0;j<4;++j)denominator+=inverseMass[t[j]]*glm::dot(gradients[j],gradients[j]);
    if(denominator<=Epsilon)return;
    const double dl=(-constraint-alpha*lambda[index])/denominator;
    lambda[index]+=dl;
    for(int j=0;j<4;++j)positions[t[j]]+=gradients[j]*(inverseMass[t[j]]*dl);
}

bool Requested() {
#ifdef _WIN32
    char* value=nullptr;std::size_t length=0;
    const bool disabled=_dupenv_s(&value,&length,"MODEL_CLOTH_SIMD")==0&&value&&value[0]=='0'&&value[1]=='\0';
    std::free(value);return !disabled;
#else
    const char* value=std::getenv("MODEL_CLOTH_SIMD");
    return !(value&&value[0]=='0'&&value[1]=='\0');
#endif
}

#if MODEL_CLOTH_AVX2_IMPLEMENTED
bool CpuAndOsAvailable() {
#ifdef _WIN32
    // Windows checks the CPU capability together with enabled AVX register
    // state; a CPU-only CPUID bit is insufficient when XCR0 lacks YMM support.
#ifdef PF_AVX2_INSTRUCTIONS_AVAILABLE
    return IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE)!=FALSE;
#else
    return false;
#endif
#else
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2");
#endif
}

struct Packet3 { __m256d x,y,z; };
MODEL_CLOTH_AVX2_INLINE __m256d Set(double value){return _mm256_set1_pd(value);}
MODEL_CLOTH_AVX2_INLINE __m256d Neg(__m256d value){return _mm256_xor_pd(value,Set(-0.0));}
MODEL_CLOTH_AVX2_INLINE __m256d Abs(__m256d value){return _mm256_andnot_pd(Set(-0.0),value);}
MODEL_CLOTH_AVX2_INLINE __m256d Blend(__m256d a,__m256d b,__m256d mask){return _mm256_blendv_pd(a,b,mask);}
MODEL_CLOTH_AVX2_INLINE __m256d Not(__m256d value){return _mm256_xor_pd(value,_mm256_castsi256_pd(_mm256_set1_epi64x(-1)));}
MODEL_CLOTH_AVX2_INLINE __m256d CopySign(__m256d magnitude,__m256d sign){
    return _mm256_or_pd(Abs(magnitude),_mm256_and_pd(sign,Set(-0.0)));
}
MODEL_CLOTH_AVX2_INLINE Packet3 Add(Packet3 a,Packet3 b){return {_mm256_add_pd(a.x,b.x),_mm256_add_pd(a.y,b.y),_mm256_add_pd(a.z,b.z)};}
MODEL_CLOTH_AVX2_INLINE Packet3 Sub(Packet3 a,Packet3 b){return {_mm256_sub_pd(a.x,b.x),_mm256_sub_pd(a.y,b.y),_mm256_sub_pd(a.z,b.z)};}
MODEL_CLOTH_AVX2_INLINE Packet3 Mul(Packet3 a,__m256d b){return {_mm256_mul_pd(a.x,b),_mm256_mul_pd(a.y,b),_mm256_mul_pd(a.z,b)};}
MODEL_CLOTH_AVX2_INLINE __m256d Dot(Packet3 a,Packet3 b){
    // Match GLM's (x*x + y*y) + z*z association; never contract to FMA.
    return _mm256_add_pd(_mm256_add_pd(_mm256_mul_pd(a.x,b.x),_mm256_mul_pd(a.y,b.y)),_mm256_mul_pd(a.z,b.z));
}
MODEL_CLOTH_AVX2_INLINE Packet3 Cross(Packet3 a,Packet3 b){
    return {_mm256_sub_pd(_mm256_mul_pd(a.y,b.z),_mm256_mul_pd(b.y,a.z)),
        _mm256_sub_pd(_mm256_mul_pd(a.z,b.x),_mm256_mul_pd(b.z,a.x)),
        _mm256_sub_pd(_mm256_mul_pd(a.x,b.y),_mm256_mul_pd(b.x,a.y))};
}
MODEL_CLOTH_AVX2_INLINE Packet3 GatherPositions(const glm::dvec3* positions,__m128i ids){
    const auto offsets=_mm_mullo_epi32(ids,_mm_set1_epi32(sizeof(glm::dvec3)));
    return {_mm256_i32gather_pd(&positions[0].x,offsets,1),
        _mm256_i32gather_pd(&positions[0].y,offsets,1),
        _mm256_i32gather_pd(&positions[0].z,offsets,1)};
}
MODEL_CLOTH_AVX2_INLINE void ScatterPositions(glm::dvec3* positions,__m128i ids,Packet3 values){
    alignas(16) std::uint32_t indices[4];
    alignas(32) double x[4],y[4],z[4];
    _mm_store_si128(reinterpret_cast<__m128i*>(indices),ids);
    _mm256_store_pd(x,values.x);_mm256_store_pd(y,values.y);_mm256_store_pd(z,values.z);
    for(int lane=0;lane<4;++lane)positions[indices[lane]]={x[lane],y[lane],z[lane]};
}

MODEL_CLOTH_AVX2_INLINE __m256d Atan2Packet(__m256d y,__m256d x){
    const auto zero=Set(0),one=Set(1),ax=Abs(x),ay=Abs(y);
    const auto finite=_mm256_and_pd(_mm256_cmp_pd(ax,Set(std::numeric_limits<double>::max()),_CMP_LE_OQ),
        _mm256_cmp_pd(ay,Set(std::numeric_limits<double>::max()),_CMP_LE_OQ));
    const auto xZero=_mm256_cmp_pd(x,zero,_CMP_EQ_OQ),yZero=_mm256_cmp_pd(y,zero,_CMP_EQ_OQ);
    const auto normal=_mm256_and_pd(finite,Not(_mm256_or_pd(xZero,yZero)));
    const auto xNegative=_mm256_castsi256_pd(_mm256_cmpgt_epi64(_mm256_setzero_si256(),_mm256_castpd_si256(x)));
    const auto axisAngle=Blend(CopySign(Set(Pi/2),y),Blend(y,CopySign(Set(Pi),y),xNegative),yZero);
    if(_mm256_movemask_pd(normal)==0&&_mm256_movemask_pd(finite)==15)return axisAngle;
    const auto ratio=_mm256_div_pd(Blend(zero,_mm256_min_pd(ax,ay),normal),Blend(one,_mm256_max_pd(ax,ay),normal));
    const auto reduced=_mm256_cmp_pd(ratio,Set(.414213562373095048801688724209698079),_CMP_GT_OQ);
    const auto z=Blend(ratio,_mm256_div_pd(_mm256_sub_pd(ratio,one),_mm256_add_pd(ratio,one)),reduced);
    const auto z2=_mm256_mul_pd(z,z);
    auto polynomial=Set(1.0/21);
    polynomial=_mm256_add_pd(Set(-1.0/19),_mm256_mul_pd(z2,polynomial));
    polynomial=_mm256_add_pd(Set(1.0/17),_mm256_mul_pd(z2,polynomial));
    polynomial=_mm256_add_pd(Set(-1.0/15),_mm256_mul_pd(z2,polynomial));
    polynomial=_mm256_add_pd(Set(1.0/13),_mm256_mul_pd(z2,polynomial));
    polynomial=_mm256_add_pd(Set(-1.0/11),_mm256_mul_pd(z2,polynomial));
    polynomial=_mm256_add_pd(Set(1.0/9),_mm256_mul_pd(z2,polynomial));
    polynomial=_mm256_add_pd(Set(-1.0/7),_mm256_mul_pd(z2,polynomial));
    polynomial=_mm256_add_pd(Set(1.0/5),_mm256_mul_pd(z2,polynomial));
    polynomial=_mm256_add_pd(Set(-1.0/3),_mm256_mul_pd(z2,polynomial));
    auto angle=_mm256_add_pd(Blend(zero,Set(Pi/4),reduced),
        _mm256_mul_pd(z,_mm256_add_pd(one,_mm256_mul_pd(z2,polynomial))));
    angle=Blend(angle,_mm256_sub_pd(Set(Pi/2),angle),_mm256_cmp_pd(ay,ax,_CMP_GT_OQ));
    angle=Blend(angle,_mm256_sub_pd(Set(Pi),angle),_mm256_cmp_pd(x,zero,_CMP_LT_OQ));
    angle=CopySign(angle,y);
    angle=Blend(angle,CopySign(Set(Pi/2),y),xZero);
    angle=Blend(angle,Blend(y,CopySign(Set(Pi),y),xNegative),yZero);
    const auto fallback=_mm256_or_pd(Not(finite),_mm256_and_pd(normal,
        _mm256_cmp_pd(ratio,Set(std::numeric_limits<double>::min()),_CMP_LT_OQ)));
    const int fallbackBits=_mm256_movemask_pd(fallback);
    if(fallbackBits){
        alignas(32) double values[4],xs[4],ys[4];
        _mm256_store_pd(values,angle);_mm256_store_pd(xs,x);_mm256_store_pd(ys,y);
        for(int lane=0;lane<4;++lane)if(fallbackBits&(1<<lane))values[lane]=std::atan2(ys[lane],xs[lane]);
        angle=_mm256_load_pd(values);
    }
    return angle;
}

MODEL_CLOTH_AVX2_INLINE void SolvePacket(const ClothHinge* hinges,const std::uint32_t* ids,
        glm::dvec3* positions,const double* inverseMass,double* lambda,double alpha){
    __m128i vertices[4];Packet3 points[4];__m256d masses[4];
    for(int corner=0;corner<4;++corner){
        vertices[corner]=_mm_setr_epi32(hinges[ids[0]].vertices[corner],hinges[ids[1]].vertices[corner],
            hinges[ids[2]].vertices[corner],hinges[ids[3]].vertices[corner]);
        points[corner]=GatherPositions(positions,vertices[corner]);
        masses[corner]=_mm256_i32gather_pd(inverseMass,vertices[corner],8);
    }
    const auto edge=Sub(points[1],points[0]),opposite1=Sub(points[2],points[0]),opposite2=Sub(points[3],points[0]);
    const auto first=Cross(edge,opposite1),second=Cross(opposite2,edge);
    const auto edge2=Dot(edge,edge),area1=Dot(first,first),area2=Dot(second,second);
    auto invalid=_mm256_or_pd(_mm256_cmp_pd(edge2,Set(Epsilon*Epsilon),_CMP_LE_OQ),
        _mm256_or_pd(_mm256_cmp_pd(area1,Set(Epsilon*Epsilon),_CMP_LE_OQ),_mm256_cmp_pd(area2,Set(Epsilon*Epsilon),_CMP_LE_OQ)));
    auto active=Not(invalid);
    if(_mm256_movemask_pd(active)==0)return;
    const auto safeEdge2=Blend(Set(1),edge2,active),edgeLength=_mm256_sqrt_pd(safeEdge2);
    const auto angle=Atan2Packet(_mm256_mul_pd(Neg(edgeLength),Dot(first,opposite2)),Dot(first,second));
    const auto restAngle=_mm256_setr_pd(hinges[ids[0]].angle,hinges[ids[1]].angle,hinges[ids[2]].angle,hinges[ids[3]].angle);
    const auto difference=_mm256_sub_pd(angle,restAngle);
    auto constraint=Blend(difference,_mm256_sub_pd(difference,Set(2*Pi)),_mm256_cmp_pd(difference,Set(Pi),_CMP_GT_OQ));
    constraint=Blend(constraint,_mm256_add_pd(difference,Set(2*Pi)),_mm256_cmp_pd(difference,Set(-Pi),_CMP_LT_OQ));
    const auto hingeIndices=_mm_setr_epi32(ids[0],ids[1],ids[2],ids[3]);
    const auto previousLambda=_mm256_i32gather_pd(lambda,hingeIndices,8);
    const auto atRest=_mm256_and_pd(_mm256_cmp_pd(constraint,Set(0),_CMP_EQ_OQ),_mm256_cmp_pd(previousLambda,Set(0),_CMP_EQ_OQ));
    active=_mm256_andnot_pd(atRest,active);
    if(_mm256_movemask_pd(active)==0)return;
    Packet3 gradients[4];
    gradients[2]=Mul(first,_mm256_div_pd(Neg(edgeLength),Blend(Set(1),area1,active)));
    gradients[3]=Mul(second,_mm256_div_pd(Neg(edgeLength),Blend(Set(1),area2,active)));
    const auto u=_mm256_div_pd(Dot(opposite1,edge),safeEdge2),v=_mm256_div_pd(Dot(opposite2,edge),safeEdge2);
    gradients[0]=Sub(Mul(gradients[2],Neg(_mm256_sub_pd(Set(1),u))),Mul(gradients[3],_mm256_sub_pd(Set(1),v)));
    gradients[1]=Sub(Mul(gradients[2],Neg(u)),Mul(gradients[3],v));
    auto denominator=Set(alpha);
    for(int corner=0;corner<4;++corner)denominator=_mm256_add_pd(denominator,
        _mm256_mul_pd(masses[corner],Dot(gradients[corner],gradients[corner])));
    active=_mm256_and_pd(active,Not(_mm256_cmp_pd(denominator,Set(Epsilon),_CMP_LE_OQ)));
    const auto dl=_mm256_div_pd(_mm256_sub_pd(Neg(constraint),_mm256_mul_pd(Set(alpha),previousLambda)),Blend(Set(1),denominator,active));
    const auto nextLambda=Blend(previousLambda,_mm256_add_pd(previousLambda,dl),active);
    alignas(32) double lambdaValues[4];_mm256_store_pd(lambdaValues,nextLambda);
    for(int lane=0;lane<4;++lane)lambda[ids[lane]]=lambdaValues[lane];
    for(int corner=0;corner<4;++corner){
        auto next=Add(points[corner],Mul(gradients[corner],_mm256_mul_pd(masses[corner],dl)));
        next={Blend(points[corner].x,next.x,active),Blend(points[corner].y,next.y,active),Blend(points[corner].z,next.z,active)};
        ScatterPositions(positions,vertices[corner],next);
    }
}

MODEL_CLOTH_AVX2 void SolveAvx2(const ClothHinge* hinges,const std::uint32_t* indices,std::size_t count,
        glm::dvec3* positions,const double* inverseMass,double* lambda,double alpha){
    std::size_t offset=0;
    for(;offset+4<=count;offset+=4){
        const std::uint32_t packet[4]={indices?indices[offset]:static_cast<std::uint32_t>(offset),
            indices?indices[offset+1]:static_cast<std::uint32_t>(offset+1),
            indices?indices[offset+2]:static_cast<std::uint32_t>(offset+2),
            indices?indices[offset+3]:static_cast<std::uint32_t>(offset+3)};
        SolvePacket(hinges,packet,positions,inverseMass,lambda,alpha);
    }
    for(;offset<count;++offset){const auto index=indices?indices[offset]:static_cast<std::uint32_t>(offset);
        SolveScalar(hinges[index],index,positions,inverseMass,lambda,alpha);}
}
#endif
} // namespace

bool ClothSimdAvailable(){
#if MODEL_CLOTH_AVX2_IMPLEMENTED
    static const bool available=Requested()&&CpuAndOsAvailable();
    return available;
#else
    return false;
#endif
}

void SolveClothHinges(const ClothHinge* hinges,const std::uint32_t* indices,std::size_t count,
        glm::dvec3* positions,const double* inverseMass,double* lambda,double alpha){
#if MODEL_CLOTH_AVX2_IMPLEMENTED
    if(ClothSimdAvailable()){SolveAvx2(hinges,indices,count,positions,inverseMass,lambda,alpha);return;}
#endif
    for(std::size_t offset=0;offset<count;++offset){const auto index=indices?indices[offset]:static_cast<std::uint32_t>(offset);
        SolveScalar(hinges[index],index,positions,inverseMass,lambda,alpha);}
}

} // namespace Model::detail
