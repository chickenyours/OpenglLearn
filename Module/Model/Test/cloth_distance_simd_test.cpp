#include "Model/Private/cloth_distance_simd.h"
#include "Model/Private/cloth_simd.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <random>
#include <stdexcept>

namespace {
using namespace Model;
using namespace Model::detail;
void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void Reference(const ClothDistanceConstraint& c,const ClothDistancePrepared& cached,std::size_t k,
        glm::dvec3* positions,const double* inverseMass,double* lambda,double alpha){
    if(cached.massSum<=0)return;
    const auto d=positions[c.a]-positions[c.b];const double length=std::sqrt(std::max(0.,glm::dot(d,d)));
    if(length<=1e-12)return;
    const double constraint=length-cached.rest;if(constraint==0&&lambda[k]==0)return;
    const double dl=(-constraint-alpha*lambda[k])*cached.inverseDenominator;lambda[k]+=dl;
    const auto correction=d*(dl/length);
    positions[c.a]+=correction*inverseMass[c.a];positions[c.b]-=correction*inverseMass[c.b];
}
double maxPositionError=0,maxLambdaError=0;
void Compare(const std::vector<glm::dvec3>& actual,const std::vector<glm::dvec3>& expected,
        const std::vector<double>& actualLambda,const std::vector<double>& expectedLambda){
    Check(actual.size()==expected.size()&&actualLambda.size()==expectedLambda.size(),"Distance output layout changed");
    for(std::size_t i=0;i<actual.size();++i)for(int axis=0;axis<3;++axis){
        const double error=std::abs(actual[i][axis]-expected[i][axis]);maxPositionError=std::max(maxPositionError,error);
        Check(std::isfinite(actual[i][axis])&&error<=1e-9,"Distance SIMD position exceeds the 1e-9 scalar tolerance");
    }
    for(std::size_t i=0;i<actualLambda.size();++i){
        const double error=std::abs(actualLambda[i]-expectedLambda[i]);maxLambdaError=std::max(maxLambdaError,error);
        Check(std::isfinite(actualLambda[i])&&error<=1e-9,"Distance SIMD lambda exceeds the 1e-9 scalar tolerance");
    }
}
void UnchangedPackets(){
    std::vector<ClothDistanceConstraint> constraints(8);std::vector<ClothDistancePrepared> prepared(8);
    std::vector<glm::dvec3> positions(16);std::vector<double> mass(16,1),lambda(8,-0.);
    const std::uint32_t order[]={7,0,6,2,5,3,1,4};
    for(std::uint32_t k=0;k<8;++k){
        constraints[k]={2*k,2*k+1,1};positions[2*k]={-0.,double(k),-0.};positions[2*k+1]={1,double(k),-0.};
        prepared[k]={1,2,.5};
    }
    const auto original=positions;const auto originalLambda=lambda;
    for(const auto* selected:{order,static_cast<const std::uint32_t*>(nullptr)}){
        SolveClothDistances(constraints.data(),prepared.data(),selected,8,positions.data(),mass.data(),lambda.data(),0);
        for(std::size_t i=0;i<positions.size();++i)for(int axis=0;axis<3;++axis)
            Check(std::bit_cast<std::uint64_t>(positions[i][axis])==std::bit_cast<std::uint64_t>(original[i][axis]),
                "Zero-residual complete packets changed a position bit");
        for(std::size_t k=0;k<lambda.size();++k)Check(std::bit_cast<std::uint64_t>(lambda[k])==
            std::bit_cast<std::uint64_t>(originalLambda[k]),"Zero-residual complete packets changed a signed-zero lambda");
    }
}
void ThresholdsAndPins(){
    constexpr std::uint32_t count=8;
    std::vector<ClothDistanceConstraint> constraints(count);std::vector<ClothDistancePrepared> prepared(count);
    std::vector<glm::dvec3> positions(count*2);std::vector<double> mass(count*2,1),lambda(count,.0125);
    const double lengths[]={0,1e-13,1e-12,2e-12,1,1,1,1};
    for(std::uint32_t k=0;k<count;++k){
        constraints[k]={2*k,2*k+1,.5};positions[2*k]={0,0,0};positions[2*k+1]={lengths[k],0,0};
        if(k==4||k==6)mass[2*k]=0;
        if(k==5||k==6)mass[2*k+1]=0;
        if(k==7)mass[2*k]=mass[2*k+1]=5e-21;
        const double sum=mass[2*k]+mass[2*k+1];prepared[k]={.5,sum,sum>0?1/sum:0};
    }
    const auto original=positions;auto expected=positions;auto expectedLambda=lambda;
    for(std::size_t k=0;k<count;++k)Reference(constraints[k],prepared[k],k,expected.data(),mass.data(),expectedLambda.data(),0);
    SolveClothDistances(constraints.data(),prepared.data(),nullptr,count,positions.data(),mass.data(),lambda.data(),0);
    Compare(positions,expected,lambda,expectedLambda);
    Check(positions[0]==original[0]&&positions[1]==original[1]&&positions[4]==original[4]&&positions[5]==original[5],
        "Zero or threshold-length constraints should be skipped");
    Check(positions[8]==original[8]&&positions[11]==original[11]&&positions[12]==original[12]&&positions[13]==original[13],
        "Pinned endpoints or a zero total mass should remain fixed");
    Check(glm::length(positions[14]-original[14])>.1,
        "A positive mass sum below epsilon must still solve; the scalar threshold is zero");
}
void RandomBatches(){
    std::mt19937_64 random(0x41a9835ee7);
    std::uniform_real_distribution<double> signedValue(-1,1),unit(0,1);
    for(std::size_t batch=0;batch<600;++batch){
        const std::size_t count=batch%133;const double scale=std::pow(10.,double(int(batch%7)-3));
        const double alpha=batch%9==0?0:std::pow(10.,double(int(batch%13)-8));
        std::vector<ClothDistanceConstraint> constraints(count);std::vector<ClothDistancePrepared> prepared(count);
        std::vector<std::uint32_t> order(count);std::iota(order.begin(),order.end(),0);std::shuffle(order.begin(),order.end(),random);
        std::vector<glm::dvec3> positions(count*2+2,glm::dvec3(123,-456,789));
        std::vector<double> mass(count*2+2,1),lambda(count+1,321.125);
        for(std::size_t k=0;k<count;++k){
            constraints[k]={std::uint32_t(2*k),std::uint32_t(2*k+1),unit(random)*scale*2};
            for(int j=0;j<2;++j){
                positions[2*k+j]=glm::dvec3(signedValue(random),signedValue(random),signedValue(random))*scale;
                mass[2*k+j]=(k+j)%7==0?0:.0001+unit(random)*5;
            }
            if(k%13==0)positions[2*k+1]=positions[2*k];
            if(k%17==0)mass[2*k]=mass[2*k+1]=0;
            lambda[k]=batch%5==0?0:signedValue(random)*.005;
            if(k%19==0){constraints[k].restLength=glm::length(positions[2*k]-positions[2*k+1]);lambda[k]=0;}
            const double sum=mass[2*k]+mass[2*k+1];
            prepared[k]={constraints[k].restLength,sum,sum>0?1/(sum+alpha):0};
        }
        // Partial colors leave unselected constraints and sentinel storage intact;
        // varying lengths also exercise every possible four-lane scalar tail.
        if(batch%3==0)order.resize((count+1)/2);
        auto expected=positions;auto expectedLambda=lambda;
        for(int pass=0;pass<5;++pass){
            for(const auto k:order)Reference(constraints[k],prepared[k],k,expected.data(),mass.data(),expectedLambda.data(),alpha);
            SolveClothDistances(constraints.data(),prepared.data(),order.data(),order.size(),positions.data(),mass.data(),lambda.data(),alpha);
            Compare(positions,expected,lambda,expectedLambda);
        }
        for(std::size_t k=0;k<count;++k)Reference(constraints[k],prepared[k],k,expected.data(),mass.data(),expectedLambda.data(),alpha);
        SolveClothDistances(constraints.data(),prepared.data(),nullptr,count,positions.data(),mass.data(),lambda.data(),alpha);
        Compare(positions,expected,lambda,expectedLambda);
    }
}
} // namespace

int main(){
    try {
        UnchangedPackets();ThresholdsAndPins();RandomBatches();
        std::printf("Distance SIMD available=%d; 600 randomized partial/contiguous batches; max position error=%.12g, lambda error=%.12g\n",
            int(ClothSimdAvailable()),maxPositionError,maxLambdaError);
        return 0;
    } catch(const std::exception& error){std::fprintf(stderr,"Distance SIMD test: %s\n",error.what());return 1;}
}
