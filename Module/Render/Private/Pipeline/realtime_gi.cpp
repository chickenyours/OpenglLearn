#include "Render/Public/Pipeline/realtime_gi.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
namespace Render {
namespace {
bool Bounded(glm::vec3 p) {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&glm::all(glm::lessThanEqual(glm::abs(p),glm::vec3(100000)));
}
struct Node { glm::vec3 lo,hi; std::uint32_t left=0,right=0,begin=0,count=0; };
void ValidateTriangles(std::span<const ProbeTriangle> triangles) {
    if(triangles.size()>32768) throw std::invalid_argument("Realtime GI scene exceeds 32768 triangles");
    for(const auto& t:triangles) if(!Bounded(t.a)||!Bounded(t.b)||!Bounded(t.c)||!Bounded(t.diffuseReflectance)||!Bounded(t.emission)||
        glm::length(glm::cross(t.b-t.a,t.c-t.a))<1e-7f||
        glm::any(glm::lessThan(t.diffuseReflectance,glm::vec3(0)))||glm::any(glm::greaterThan(t.diffuseReflectance,glm::vec3(1)))||
        glm::any(glm::lessThan(t.emission,glm::vec3(0)))||glm::any(glm::greaterThan(t.emission,glm::vec3(60000)))||
        !Bounded(t.specularF0)||glm::any(glm::lessThan(t.specularF0,glm::vec3(0)))||glm::any(glm::greaterThan(t.specularF0,glm::vec3(1)))||!std::isfinite(t.roughness)||t.roughness<0||t.roughness>1)
        throw std::invalid_argument("Invalid realtime GI geometry/material");
}
}
std::shared_ptr<const RealtimeGiScene> BuildRealtimeGiScene(std::span<const ProbeTriangle> triangles) {
    ValidateTriangles(triangles);
    std::vector<std::uint32_t> indices(triangles.size()); std::iota(indices.begin(),indices.end(),0);
    std::vector<Node> nodes;
    const auto build=[&](auto&& self,std::uint32_t begin,std::uint32_t end)->std::uint32_t {
        Node n; n.lo=glm::vec3(1e30f); n.hi=glm::vec3(-1e30f); glm::vec3 clo=n.lo,chi=n.hi;
        for(auto i=begin;i<end;++i) {
            const auto& t=triangles[indices[i]]; const auto center=(t.a+t.b+t.c)/3.0f;
            n.lo=glm::min(n.lo,glm::min(t.a,glm::min(t.b,t.c))); n.hi=glm::max(n.hi,glm::max(t.a,glm::max(t.b,t.c)));
            clo=glm::min(clo,center); chi=glm::max(chi,center);
        }
        const auto index=std::uint32_t(nodes.size()); nodes.push_back(n);
        if(end-begin<=8) {nodes[index].begin=begin;nodes[index].count=end-begin;return index;}
        const auto extent=chi-clo; const int axis=extent.x>extent.y?(extent.x>extent.z?0:2):(extent.y>extent.z?1:2);
        auto middle=(begin+end)/2;
        std::nth_element(indices.begin()+begin,indices.begin()+middle,indices.begin()+end,[&](auto a,auto b){
            const auto& x=triangles[a];const auto& y=triangles[b];return x.a[axis]+x.b[axis]+x.c[axis]<y.a[axis]+y.b[axis]+y.c[axis];
        });
        auto left=self(self,begin,middle),right=self(self,middle,end);nodes[index].left=left;nodes[index].right=right;return index;
    };
    if(!triangles.empty()) build(build,0,std::uint32_t(triangles.size()));
    auto result=std::make_shared<RealtimeGiScene>(); result->nodes_=std::uint32_t(nodes.size());result->triangles_=std::uint32_t(triangles.size());
    result->inputOrder_=indices;result->topology_=std::make_shared<const int>(0);
    const auto size=nodes.size()*2+triangles.size()*5;
    result->pixels_.resize(std::max<std::size_t>(1024,((size+1023)/1024)*1024),glm::vec4(0));
    for(std::size_t i=0;i<nodes.size();++i) {
        const auto& n=nodes[i]; result->pixels_[i*2]=glm::vec4(n.lo,float(n.count?n.begin:n.left));
        result->pixels_[i*2+1]=glm::vec4(n.hi,n.count?-float(n.count):float(n.right));
    }
    for(std::size_t i=0;i<indices.size();++i) {
        const auto& t=triangles[indices[i]]; const auto offset=nodes.size()*2+i*5;
        result->hasEmission_|=glm::any(glm::greaterThan(t.emission,glm::vec3(0)));
        result->pixels_[offset]=glm::vec4(t.a,t.specularF0.r); result->pixels_[offset+1]=glm::vec4(t.b,t.specularF0.g);result->pixels_[offset+2]=glm::vec4(t.c,t.specularF0.b);
        result->pixels_[offset+3]=glm::vec4(t.diffuseReflectance,t.roughness);result->pixels_[offset+4]=glm::vec4(t.emission,float((t.twoSidedEmission?1:0)|(t.analyticEmission?2:0)));
    }
    return result;
}
std::shared_ptr<const RealtimeGiScene> RefitRealtimeGiScene(const RealtimeGiScene& previous,std::span<const ProbeTriangle> triangles) {
    ValidateTriangles(triangles);
    if(triangles.size()!=previous.TriangleCount())throw std::invalid_argument("GI refit requires identical triangle topology/order");
    auto result=std::make_shared<RealtimeGiScene>(previous);result->hasEmission_=false;
    for(unsigned i=0;i<result->triangles_;++i) {
        const auto& t=triangles[result->inputOrder_[i]];auto at=result->nodes_*2+i*5;
        result->pixels_[at]=glm::vec4(t.a,t.specularF0.r);result->pixels_[at+1]=glm::vec4(t.b,t.specularF0.g);result->pixels_[at+2]=glm::vec4(t.c,t.specularF0.b);
        result->pixels_[at+3]=glm::vec4(t.diffuseReflectance,t.roughness);result->pixels_[at+4]=glm::vec4(t.emission,float((t.twoSidedEmission?1:0)|(t.analyticEmission?2:0)));
        result->hasEmission_|=glm::any(glm::greaterThan(t.emission,glm::vec3(0)));
    }
    // Preorder allocation places children after parents; a reverse walk
    // updates every leaf and branch without sorting or tracing on the CPU.
    for(unsigned end=result->nodes_;end>0;--end) {
        const unsigned i=end-1;auto& a=result->pixels_[i*2];auto& b=result->pixels_[i*2+1];
        glm::vec3 lo(1e30f),hi(-1e30f);
        if(b.w<0)for(unsigned t=unsigned(a.w);t<unsigned(a.w-b.w);++t)for(unsigned v=0;v<3;++v) {
            auto p=glm::vec3(result->pixels_[result->nodes_*2+t*5+v]);lo=glm::min(lo,p);hi=glm::max(hi,p);
        } else {
            lo=glm::min(glm::vec3(result->pixels_[unsigned(a.w)*2]),glm::vec3(result->pixels_[unsigned(b.w)*2]));
            hi=glm::max(glm::vec3(result->pixels_[unsigned(a.w)*2+1]),glm::vec3(result->pixels_[unsigned(b.w)*2+1]));
        }
        a=glm::vec4(lo,a.w);b=glm::vec4(hi,b.w);
    }
    return result;
}
bool ValidateRealtimeGiSettings(const RealtimeGiSettings& s,std::string* error) {
    if(error) error->clear();
    const auto range=[](float v,float lo,float hi){return std::isfinite(v)&&v>=lo&&v<=hi;};
    const bool valid=Bounded(s.origin)&&Bounded(s.spacing)&&glm::all(glm::greaterThan(s.spacing,glm::vec3(.001f)))&&
        glm::all(glm::lessThanEqual(s.counts,glm::uvec3(64)))&&glm::all(glm::greaterThanEqual(s.counts,glm::uvec3(2)))&&
        std::uint64_t(s.counts.x)*s.counts.y*s.counts.z<=64&&s.raysPerProbe>=32&&s.raysPerProbe<=RealtimeGiMaxRays&&
        s.probesPerFrame>=1&&s.probesPerFrame<=64&&range(s.maxDistance,.1f,1000)&&range(s.rayBias,.0001f,.5f)&&
        range(s.normalBias,0,2)&&range(s.visibilityBias,0,2)&&range(s.historyWeight,0,.98f)&&range(s.bounceFeedback,0,.95f)&&
        range(s.intensity,0,16)&&(!s.enabled||(s.scene&&s.scene->Pixels().size()>=1024));
    if(!valid&&error) *error="Invalid realtime GI grid, budget, temporal settings or missing scene";
    return valid;
}
} // namespace Render
