#include "Model/Private/cloth_simd.h"
#include "Model/Private/cloth_math.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>

namespace {
using Model::detail::ClothHinge;
constexpr double Pi=3.141592653589793238462643383279502884;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void Reference(const ClothHinge& hinge,std::size_t k,glm::dvec3* p,const double* mass,double* lambda,double alpha){
    const auto& t=hinge.vertices;const auto a=p[t[0]],b=p[t[1]],c=p[t[2]],d=p[t[3]],edge=b-a;
    const auto first=glm::cross(edge,c-a),second=glm::cross(d-a,edge);
    const double edge2=glm::dot(edge,edge),area1=glm::dot(first,first),area2=glm::dot(second,second);
    if(edge2<=1e-24||area1<=1e-24||area2<=1e-24)return;
    const double length=std::sqrt(edge2),u=glm::dot(c-a,edge)/edge2,v=glm::dot(d-a,edge)/edge2;
    glm::dvec3 g[4];g[2]=first*(-length/area1);g[3]=second*(-length/area2);
    g[0]=-(1-u)*g[2]-(1-v)*g[3];g[1]=-u*g[2]-v*g[3];
    const double angle=Model::detail::FastAtan2(-length*glm::dot(first,d-a),glm::dot(first,second));
    double constraint=angle-hinge.angle;if(constraint>Pi)constraint-=2*Pi;else if(constraint< -Pi)constraint+=2*Pi;
    double denominator=alpha;for(int j=0;j<4;++j)denominator+=mass[t[j]]*glm::dot(g[j],g[j]);
    if(denominator<=1e-12)return;
    const double dl=(-constraint-alpha*lambda[k])/denominator;lambda[k]+=dl;
    for(int j=0;j<4;++j)p[t[j]]+=g[j]*(mass[t[j]]*dl);
}
void ZeroResidualPackets(){
    constexpr std::size_t count=8;
    std::vector<ClothHinge> hinges(count);std::vector<glm::dvec3> positions(count*4);std::vector<double> masses(count*4,1),lambda(count,0);
    const std::uint32_t order[count]={7,1,4,0,6,2,5,3};
    for(std::size_t k=0;k<count;++k){const auto offset=std::uint32_t(k*4);hinges[k]={{offset,offset+1,offset+2,offset+3},0};
        const glm::dvec3 origin(3*double(k)+1,2,3);positions[offset]=origin;positions[offset+1]=origin+glm::dvec3(1,0,0);positions[offset+2]=origin+glm::dvec3(0,1,0);positions[offset+3]=origin+glm::dvec3(0,-1,0);}
    const auto original=positions;const auto originalLambda=lambda;
    auto unchanged=[&]{for(std::size_t i=0;i<positions.size();++i)for(int j=0;j<3;++j)Check(std::bit_cast<std::uint64_t>(positions[i][j])==std::bit_cast<std::uint64_t>(original[i][j]),"A zero-residual packet changed a position bit");
        for(std::size_t k=0;k<count;++k)Check(std::bit_cast<std::uint64_t>(lambda[k])==std::bit_cast<std::uint64_t>(originalLambda[k]),"A zero-residual packet changed a multiplier bit");};
    // Two complete packets, rather than scalar tails: all four lanes in each
    // packet have constraint==0 and lambda==0 and must take the no-write path.
    Model::detail::SolveClothHinges(hinges.data(),order,count,positions.data(),masses.data(),lambda.data(),.0035);unchanged();
    Model::detail::SolveClothHinges(hinges.data(),nullptr,count,positions.data(),masses.data(),lambda.data(),0);unchanged();
}
void AxisPacket(){
    constexpr std::size_t count=4;std::vector<ClothHinge> hinges(count);std::vector<glm::dvec3> positions(count*4);std::vector<double> masses(count*4,1),lambda(count,0);
    const double expectedAngles[count]={Pi/2,-Pi/2,-Pi,Pi};
    for(std::size_t k=0;k<count;++k){const auto offset=std::uint32_t(k*4);hinges[k]={{offset,offset+1,offset+2,offset+3},0};
        positions[offset]={0,0,0};positions[offset+1]={1,0,0};positions[offset+2]={0,1,0};
        positions[offset+3]=k==0?glm::dvec3(0,0,-1):(k==1?glm::dvec3(0,0,1):glm::dvec3(0,1,0));}
    // All three terms in dot(first,opposite) are negative zero, producing
    // atan2(+0,-1)==+pi instead of the -pi case in the neighboring lane.
    positions[13]={1,0,-0.};positions[14]={1,1,0};positions[15]={-0.,1,-0.};
    for(std::size_t k=0;k<count;++k){const auto& t=hinges[k].vertices;const auto a=positions[t[0]],edge=positions[t[1]]-a,first=glm::cross(edge,positions[t[2]]-a),opposite=positions[t[3]]-a,second=glm::cross(opposite,edge);
        const double angle=std::atan2(-std::sqrt(glm::dot(edge,edge))*glm::dot(first,opposite),glm::dot(first,second));
        Check(angle==expectedAngles[k]&&std::signbit(angle)==std::signbit(expectedAngles[k]),"Axis fixture did not produce its intended signed quadrant boundary");}
    auto expected=positions;auto expectedLambda=lambda;
    for(std::size_t k=0;k<count;++k)Reference(hinges[k],k,expected.data(),masses.data(),expectedLambda.data(),.05);
    Model::detail::SolveClothHinges(hinges.data(),nullptr,count,positions.data(),masses.data(),lambda.data(),.05);
    for(std::size_t i=0;i<positions.size();++i)for(int j=0;j<3;++j)Check(std::isfinite(positions[i][j])&&std::abs(positions[i][j]-expected[i][j])<=1e-9*(1+std::abs(expected[i][j])),"SIMD axis-boundary position differs from scalar reference");
    for(std::size_t k=0;k<count;++k)Check(std::isfinite(lambda[k])&&std::abs(lambda[k]-expectedLambda[k])<=1e-9*(1+std::abs(expectedLambda[k])),"SIMD axis-boundary multiplier differs from scalar reference");
}
void Run(){
    ZeroResidualPackets();AxisPacket();
    std::mt19937_64 random(0x27ee9853);
    std::uniform_real_distribution<double> value(-1,1),unit(0,1);
    double maxPositionError=0,maxLambdaError=0;
    for(std::size_t batch=0;batch<600;++batch){
        const std::size_t count=batch%131;const double scale=std::pow(10.,double(int(batch%5)-2));
        const double alpha=batch%11==0?0:std::pow(10.,double(int(batch%12)-8));
        std::vector<ClothHinge> hinges(count);std::vector<std::uint32_t> order(count);
        std::vector<glm::dvec3> positions(count*4);std::vector<double> masses(count*4),lambda(count);
        for(std::size_t k=0;k<count;++k){
            hinges[k]={{std::uint32_t(4*k),std::uint32_t(4*k+1),std::uint32_t(4*k+2),std::uint32_t(4*k+3)},value(random)*Pi};
            for(int j=0;j<4;++j){positions[4*k+j]=glm::dvec3(value(random),value(random),value(random))*scale;masses[4*k+j]=(k+j)%9==0?0:unit(random)*2;}
            lambda[k]=batch%7?value(random)*.001:0;
            // Mix invalid hinges with ordinary lanes and exact angle-axis cases.
            if(k%17==0)positions[4*k+1]=positions[4*k];
            if(k%19==0)positions[4*k+2]=positions[4*k]+(positions[4*k+1]-positions[4*k])*2.;
            if(k%23==0){positions[4*k]={0,0,0};positions[4*k+1]={scale,0,0};positions[4*k+2]={0,scale,0};positions[4*k+3]={0,-scale,0};hinges[k].angle=k%2?Pi:-Pi;}
            if(k%29==0)for(int j=0;j<4;++j)masses[4*k+j]=0;
        }
        std::iota(order.begin(),order.end(),0);std::shuffle(order.begin(),order.end(),random);
        auto expected=positions;auto expectedLambda=lambda;
        for(auto k:order)Reference(hinges[k],k,expected.data(),masses.data(),expectedLambda.data(),alpha);
        Model::detail::SolveClothHinges(hinges.data(),order.data(),count,positions.data(),masses.data(),lambda.data(),alpha);
        for(std::size_t i=0;i<positions.size();++i)for(int j=0;j<3;++j){
            const double error=std::abs(positions[i][j]-expected[i][j]);maxPositionError=std::max(maxPositionError,error);
            Check(std::isfinite(positions[i][j])&&error<=1e-9*(1+std::abs(expected[i][j])),"SIMD hinge position differs from scalar reference");
        }
        for(std::size_t k=0;k<count;++k){const double error=std::abs(lambda[k]-expectedLambda[k]);maxLambdaError=std::max(maxLambdaError,error);Check(std::isfinite(lambda[k])&&error<=1e-9*(1+std::abs(expectedLambda[k])),"SIMD hinge multiplier differs from scalar reference");}
        // A null index pointer provides the same complete contiguous batch.
        auto contiguous=expected;auto contiguousLambda=expectedLambda;auto contiguousExpected=expected;auto contiguousExpectedLambda=expectedLambda;
        for(std::size_t k=0;k<count;++k)Reference(hinges[k],k,contiguousExpected.data(),masses.data(),contiguousExpectedLambda.data(),alpha);
        Model::detail::SolveClothHinges(hinges.data(),nullptr,count,contiguous.data(),masses.data(),contiguousLambda.data(),alpha);
        for(std::size_t i=0;i<contiguous.size();++i)for(int j=0;j<3;++j)Check(std::isfinite(contiguous[i][j])&&std::abs(contiguous[i][j]-contiguousExpected[i][j])<=1e-9*(1+std::abs(contiguousExpected[i][j])),"Contiguous SIMD batch differs from scalar reference");
    }
    std::cout<<"SIMD available="<<Model::detail::ClothSimdAvailable()<<", 600 varying batches; max position error="<<maxPositionError<<", max lambda error="<<maxLambdaError<<'\n';
}
}
int main(){try{Run();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
