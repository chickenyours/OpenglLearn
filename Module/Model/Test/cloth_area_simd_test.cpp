#include "Model/Private/cloth_area_simd.h"
#include "Model/Private/cloth_simd.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>

namespace {
using Triangle=std::array<std::uint32_t,3>;
constexpr double Epsilon=1e-12;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool BitsEqual(double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
// Independent scalar XPBD reference, retaining the original area kernel's
// component operations, summation association, units and degeneracy threshold.
void Reference(const Triangle& t,std::size_t k,const double* rest,const double* scales,
    glm::dvec3* p,const double* mass,double* lambda,double alpha){
    if(!std::isfinite(rest[k])||rest[k]<=Epsilon||!std::isfinite(scales[k])||scales[k]<=0||!std::isfinite(lambda[k]))return;
    const auto a=p[t[0]],b=p[t[1]],c=p[t[2]],cross=glm::cross(b-a,c-a);const double area2=glm::dot(cross,cross);
    if(!std::isfinite(area2))return;const double twiceArea=std::sqrt(std::max(0.,area2));if(twiceArea<=Epsilon)return;
    if(twiceArea*.5==rest[k]&&lambda[k]==0)return;
    const auto normal=cross/twiceArea;const double scale=scales[k];
    const glm::dvec3 gradient[3]={glm::cross(b-c,normal)*(.5*scale),glm::cross(c-a,normal)*(.5*scale),glm::cross(a-b,normal)*(.5*scale)};
    double denominator=alpha;for(int j=0;j<3;++j)denominator+=mass[t[j]]*glm::dot(gradient[j],gradient[j]);
    if(!std::isfinite(denominator)||denominator<=Epsilon)return;
    const double dl=(-((twiceArea*.5-rest[k])*scale)-alpha*lambda[k])/denominator;
    if(!std::isfinite(dl)||!std::isfinite(lambda[k]+dl))return;
    lambda[k]+=dl;for(int j=0;j<3;++j)p[t[j]]+=gradient[j]*(mass[t[j]]*dl);
}
void Compare(const std::vector<glm::dvec3>& actual,const std::vector<glm::dvec3>& expected,
    const std::vector<double>& actualLambda,const std::vector<double>& expectedLambda,double& maxPositionError,double& maxLambdaError){
    for(std::size_t i=0;i<actual.size();++i)for(int j=0;j<3;++j){const double error=std::abs(actual[i][j]-expected[i][j]);maxPositionError=std::max(maxPositionError,error);
        Check(std::isfinite(actual[i][j])&&error<=1e-10*(1+std::abs(expected[i][j])),"Area SIMD position differs from scalar reference");}
    for(std::size_t k=0;k<actualLambda.size();++k){const double error=std::abs(actualLambda[k]-expectedLambda[k]);maxLambdaError=std::max(maxLambdaError,error);
        Check(std::isfinite(actualLambda[k])&&error<=1e-10*(1+std::abs(expectedLambda[k])),"Area SIMD multiplier differs from scalar reference");}
}
void ZeroAndInvalidPackets(){
    constexpr std::size_t count=8;std::vector<Triangle> triangles(count);std::vector<glm::dvec3> positions(count*3);
    std::vector<double> rest(count,.5),scale(count,1/std::sqrt(.5)),mass(count*3,1),lambda(count,0);
    const std::uint32_t order[count]={1,4,0,2,5,7,3,6};
    for(std::size_t k=0;k<count;++k){const auto i=std::uint32_t(k*3);triangles[k]={i,i+1,i+2};const glm::dvec3 origin(3*double(k)+1,2,3);positions[i]=origin;positions[i+1]=origin+glm::dvec3(1,0,0);positions[i+2]=origin+glm::dvec3(0,1,0);}
    const auto original=positions;const auto originalLambda=lambda;
    auto unchanged=[&]{for(std::size_t i=0;i<positions.size();++i)for(int j=0;j<3;++j)Check(BitsEqual(positions[i][j],original[i][j]),"A zero area packet changed a position bit");
        for(std::size_t k=0;k<count;++k)Check(BitsEqual(lambda[k],originalLambda[k]),"A zero area packet changed a multiplier bit");};
    Model::detail::SolveClothAreas(triangles.data(),rest.data(),scale.data(),order,count,positions.data(),mass.data(),lambda.data(),.0035);unchanged();
    Model::detail::SolveClothAreas(triangles.data(),rest.data(),scale.data(),nullptr,count,positions.data(),mass.data(),lambda.data(),0);unchanged();
    // A valid lane shares each packet with malformed or degenerate lanes.
    // The latter must retain their exact original values, including NaN bits.
    positions[2].y+=1;positions[5]=positions[4];rest[2]=0;scale[3]=std::numeric_limits<double>::quiet_NaN();lambda[4]=std::numeric_limits<double>::quiet_NaN();
    positions[15].z=std::numeric_limits<double>::quiet_NaN();for(int j=0;j<3;++j)mass[18+j]=0;lambda[6]=.001;rest[7]=std::numeric_limits<double>::infinity();
    const auto before=positions;const auto beforeLambda=lambda;auto expected=positions;auto expectedLambda=lambda;
    Reference(triangles[0],0,rest.data(),scale.data(),expected.data(),mass.data(),expectedLambda.data(),0);
    Model::detail::SolveClothAreas(triangles.data(),rest.data(),scale.data(),order,count,positions.data(),mass.data(),lambda.data(),0);
    for(std::size_t k=0;k<count;++k){for(auto i:triangles[k])for(int j=0;j<3;++j){if(k==0)Check(std::isfinite(positions[i][j])&&std::abs(positions[i][j]-expected[i][j])<=1e-10*(1+std::abs(expected[i][j])),"A mixed area packet corrupted its valid lane");else Check(BitsEqual(positions[i][j],before[i][j]),"An invalid area lane changed a position bit");}
        if(k==0)Check(std::abs(lambda[k]-expectedLambda[k])<=1e-10*(1+std::abs(expectedLambda[k])),"A mixed area packet corrupted its valid multiplier");else Check(BitsEqual(lambda[k],beforeLambda[k]),"An invalid area lane changed a multiplier bit");}
    Model::detail::SolveClothAreas(nullptr,nullptr,nullptr,nullptr,0,nullptr,nullptr,nullptr,0);
}
void Run(){
    ZeroAndInvalidPackets();std::mt19937_64 random(0x0ccdd44892e1ull);std::uniform_real_distribution<double> value(-1,1),unit(0,1);double maxPositionError=0,maxLambdaError=0;
    for(std::size_t batch=0;batch<600;++batch){const std::size_t count=batch%131;const double lengthScale=std::pow(10.,double(int(batch%6)-3)),alpha=batch%11==0?0:std::pow(10.,double(int(batch%12)-8));
        std::vector<Triangle> triangles(count);std::vector<std::uint32_t> order(count);std::vector<glm::dvec3> positions(count*3);std::vector<double> rest(count),scales(count),mass(count*3),lambda(count);
        for(std::size_t k=0;k<count;++k){const auto offset=std::uint32_t(3*k);triangles[k]={offset,offset+1,offset+2};for(int j=0;j<3;++j){positions[offset+j]=glm::dvec3(value(random),value(random),value(random))*lengthScale;mass[offset+j]=(k+j)%9==0?0:unit(random)*2;}
            const auto cross=glm::cross(positions[offset+1]-positions[offset],positions[offset+2]-positions[offset]);const double area=std::sqrt(std::max(0.,glm::dot(cross,cross)))*.5;
            rest[k]=area*(.5+unit(random));lambda[k]=batch%7?value(random)*.001:0;
            if(k%17==0)rest[k]=0;if(k%19==0)positions[offset+2]=positions[offset+1];if(k%23==0)for(int j=0;j<3;++j)mass[offset+j]=0;
            if(k%29==0){rest[k]=area;lambda[k]=0;}
            scales[k]=rest[k]>Epsilon?1/std::sqrt(rest[k]):0;
        }
        std::iota(order.begin(),order.end(),0);std::shuffle(order.begin(),order.end(),random);auto expected=positions;auto expectedLambda=lambda;
        for(auto k:order)Reference(triangles[k],k,rest.data(),scales.data(),expected.data(),mass.data(),expectedLambda.data(),alpha);
        Model::detail::SolveClothAreas(triangles.data(),rest.data(),scales.data(),order.data(),count,positions.data(),mass.data(),lambda.data(),alpha);
        Compare(positions,expected,lambda,expectedLambda,maxPositionError,maxLambdaError);
        auto contiguousExpected=positions;auto contiguousExpectedLambda=lambda;
        for(std::size_t k=0;k<count;++k)Reference(triangles[k],k,rest.data(),scales.data(),contiguousExpected.data(),mass.data(),contiguousExpectedLambda.data(),alpha);
        Model::detail::SolveClothAreas(triangles.data(),rest.data(),scales.data(),nullptr,count,positions.data(),mass.data(),lambda.data(),alpha);
        Compare(positions,contiguousExpected,lambda,contiguousExpectedLambda,maxPositionError,maxLambdaError);
    }
    std::cout<<"Area SIMD available="<<Model::detail::ClothSimdAvailable()<<", 600 varying batches; max position error="<<maxPositionError<<", max lambda error="<<maxLambdaError<<'\n';
}
}
int main(){try{Run();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
