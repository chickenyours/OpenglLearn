#include "Model/Private/cloth_convex_support_simd.h"
#include "Model/Private/cloth_convex.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace {
using Model::detail::PreparedClothSupportCloud;
void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
bool SameBits(double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}

class ConvexSimdOverride {
public:
    explicit ConvexSimdOverride(const char* value){
#ifdef _WIN32
        char* previous=nullptr;std::size_t length=0;
        if(_dupenv_s(&previous,&length,"MODEL_CLOTH_CONVEX_SIMD")==0&&previous){existed_=true;previous_=previous;}
        std::free(previous);_putenv_s("MODEL_CLOTH_CONVEX_SIMD",value);
#else
        if(const auto* previous=std::getenv("MODEL_CLOTH_CONVEX_SIMD")){existed_=true;previous_=previous;}
        setenv("MODEL_CLOTH_CONVEX_SIMD",value,1);
#endif
    }
    ~ConvexSimdOverride(){
#ifdef _WIN32
        _putenv_s("MODEL_CLOTH_CONVEX_SIMD",existed_?previous_.c_str():"");
#else
        if(existed_)setenv("MODEL_CLOTH_CONVEX_SIMD",previous_.c_str(),1);else unsetenv("MODEL_CLOTH_CONVEX_SIMD");
#endif
    }
    ConvexSimdOverride(const ConvexSimdOverride&)=delete;
    ConvexSimdOverride& operator=(const ConvexSimdOverride&)=delete;
private:
    bool existed_=false;
    std::string previous_;
};

double Reference(std::span<const glm::dvec3> points,glm::dvec3 normal){
    double maximum=-std::numeric_limits<double>::infinity();
    for(const auto& point:points)maximum=std::max(maximum,glm::dot(normal,point));
    return maximum;
}

struct Checks {
    std::uint64_t comparisons=0,simdClouds=0;
    PreparedClothSupportCloud selected,fallback;
    void Cloud(std::span<const glm::dvec3> points,std::span<const glm::dvec3> normals){
        selected.Prepare(points);simdClouds+=selected.UsesSimd();
        Check(selected.Size()==points.size(),"Preparation lost source support points");
        {ConvexSimdOverride disabled("0");fallback.Prepare(points);}
        Check(!fallback.UsesSimd(),"MODEL_CLOTH_CONVEX_SIMD=0 did not disable support SIMD");
        for(const auto normal:normals){const double expected=Reference(points,normal);
            Check(SameBits(selected.Maximum(normal),expected),"Selected support maximum differs from ordered scalar GLM bits");
            Check(SameBits(fallback.Maximum(normal),expected),"Forced fallback maximum differs from ordered scalar GLM bits");
            ++comparisons;}
    }
};

void Boundaries(Checks& checks){
    const std::vector<glm::dvec3> axes={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},{1,1,1},{-1,-1,-1},{0,0,0}};
    checks.Cloud({},axes);
    const double extreme=std::numeric_limits<double>::max()/8;
    for(std::size_t count=1;count<=19;++count){std::vector<glm::dvec3> points(count);
        for(std::size_t i=0;i<count;++i)points[i]={-extreme*(1+double(i%3)),-extreme*(1+double(i%2)),-extreme};
        checks.Cloud(points,axes);
        points.back()={extreme*3,extreme*2,extreme};checks.Cloud(points,axes);
    }
    // Source 1 wins the scalar zero tie, but lane 0 first sees source 4. This
    // requires the zero-result scalar redo rather than an arbitrary lane tie.
    std::vector<glm::dvec3> zeros(9,glm::dvec3(-1));zeros[1]=glm::dvec3(-0.);zeros[4]=glm::dvec3(0.);
    checks.Cloud(zeros,axes);std::reverse(zeros.begin(),zeros.end());checks.Cloud(zeros,axes);
    const std::vector<glm::dvec3> cancellation={{1e16,-1e16,1},{-1e16,1e16,-1},{1e150,-1e150,1e-150},
        {1e120,1e120,-1e120},{-1e120,-1e120,1e120}};
    checks.Cloud(cancellation,axes);
    // A prepared cloud owns a snapshot; later mutations and repeated Prepare
    // calls of different sizes must not expose source or stale tail elements.
    std::vector<glm::dvec3> source={{-1,-2,-3},{4,5,6},{7,8,9},{-10,-11,-12},{13,14,15}};
    PreparedClothSupportCloud snapshot;snapshot.Prepare(source);const double expected=Reference(source,{1,2,3});
    source.clear();Check(snapshot.Size()==5&&SameBits(snapshot.Maximum({1,2,3}),expected),"Prepared cloud aliases mutable source memory");
    source={{-7,-8,-9}};snapshot.Prepare(source);
    Check(snapshot.Size()==1&&SameBits(snapshot.Maximum({1,2,3}),Reference(source,{1,2,3})),"Shrinking a support cloud queried stale packet lanes");
    snapshot.Prepare({});Check(snapshot.Size()==0&&SameBits(snapshot.Maximum({1,2,3}),-std::numeric_limits<double>::infinity()),"Empty re-preparation retained old supports");
}

void RandomClouds(Checks& checks){
    std::mt19937_64 random(0xa6fc204d172893ULL);std::uniform_real_distribution<double> value(-1,1);
    const double scales[]={1e-120,1e-9,1,1e9,1e120};
    for(std::size_t count=1;count<=1024;++count){const double scale=scales[count%5];
        std::vector<glm::dvec3> points(count),normals={{1,0,0},{0,-1,0},{0,0,1},{1,1,1}};
        for(auto& point:points)point={value(random)*scale,value(random)*scale,value(random)*scale};
        // Move the maximum to every tail length, so no complete-packet-only
        // implementation or accidental zero padding can pass by chance.
        points.back().x=2*scale;
        for(int i=0;i<12;++i)normals.emplace_back(value(random),value(random),value(random));
        checks.Cloud(points,normals);
    }
}
}

int main(){try{
    Checks checks;Boundaries(checks);RandomClouds(checks);
    std::cout<<"Support SIMD available="<<Model::detail::ClothConvexSimdAvailable()
        <<", SIMD clouds="<<checks.simdClouds<<", bit-exact comparisons="<<checks.comparisons<<'\n';
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
