#include "Render/Public/Pipeline/lumen_gi.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <map>
#include <array>
#include <cstdint>
#include <set>
#include <iterator>
#include <tuple>
namespace Render {
namespace {
glm::vec3 TriangleCachePoint(glm::vec3 a,glm::vec3 b,glm::vec3 c,unsigned x,unsigned y,unsigned r) {
    if(x+y>=r){x=r-1-x;y=r-1-y;}
    // One-third offsets center the lattice inside its triangular cells. Even
    // the diagonal and the r=1 centroid stay away from shared mesh edges.
    const float u=(float(x)+1.f/3)/r,v=(float(y)+1.f/3)/r;
    return a+(b-a)*u+(c-a)*v;
}
std::uint32_t AppendEmissiveTriangles(std::vector<glm::vec4>& attributes,const RealtimeGiScene& geometry,std::uint32_t surfels) {
    const auto& pixels=geometry.Pixels();const auto offset=geometry.NodeCount()*2;
    std::vector<std::pair<unsigned,double>> emitters;double total=0;
    for(unsigned i=0;i<geometry.TriangleCount();++i) {
        const auto e=pixels[offset+i*5+4];
        if((unsigned(e.w)&2u)||!glm::any(glm::greaterThan(glm::vec3(e),glm::vec3(0))))continue;
        const auto a=glm::vec3(pixels[offset+i*5]),b=glm::vec3(pixels[offset+i*5+1]),c=glm::vec3(pixels[offset+i*5+2]);
        const double importance=double(glm::length(glm::cross(b-a,c-a))*.5f)*double(glm::dot(glm::vec3(e),glm::vec3(.2126f,.7152f,.0722f)));
        if(importance>0){emitters.emplace_back(i,importance);total+=importance;}
    }
    const unsigned begin=geometry.TriangleCount()+surfels*2;
    attributes.resize(std::max(1024u,((begin+unsigned(emitters.size())+1023)/1024)*1024),glm::vec4(0));
    double sum=0;for(unsigned i=0;i<emitters.size();++i) {
        const auto [triangle,weight]=emitters[i];sum+=weight;
        attributes[begin+i]={float(triangle),float(sum/total),float(weight/total),0};
    }
    return unsigned(emitters.size());
}
void AppendCoplanarAdjacency(std::vector<glm::vec4>& attributes,const RealtimeGiScene& geometry,unsigned begin) {
    const auto count=geometry.TriangleCount(),offset=geometry.NodeCount()*2;const auto& pixels=geometry.Pixels();
    // Refit can move emitter/list offsets and lose old neighbors. Truncate the
    // appended portion before rebuilding, retaining immutable surface texels.
    attributes.resize(begin+count,glm::vec4(0));
    for(unsigned i=0;i<count;++i)attributes[begin+i]={-1,-1,-1,0};
    constexpr double tolerance=.0002;
    struct Edge {unsigned triangle,side;glm::dvec3 a,b,normal,direction;double length;};
    using LineKey=std::array<std::int64_t,9>;
    const auto quantize=[](double value,double scale) {return std::int64_t(std::llround(std::clamp(value*scale,-9e18,9e18)));};
    std::map<LineKey,std::vector<Edge>> lines;
    for(unsigned i=0;i<count;++i)for(unsigned side=0;side<3;++side) {
        const auto a=glm::dvec3(pixels[offset+i*5+side]),b=glm::dvec3(pixels[offset+i*5+(side+1)%3]);
        const auto c=glm::dvec3(pixels[offset+i*5+(side+2)%3]);
        const double length=glm::length(b-a);if(length<=1e-12)continue;
        auto direction=(b-a)/length;
        const unsigned axis=std::abs(direction.x)>=std::abs(direction.y)&&std::abs(direction.x)>=std::abs(direction.z)?0:
                            (std::abs(direction.y)>=std::abs(direction.z)?1:2);
        if(direction[axis]<0)direction=-direction;
        const auto normal=glm::normalize(glm::cross(b-a,c-a)),moment=glm::cross(a,direction);
        LineKey key{};
        for(unsigned j=0;j<3;++j) {
            key[j]=quantize(direction[j],1e6);key[3+j]=quantize(normal[j],1e5);key[6+j]=quantize(moment[j],1e4);
        }
        lines[key].push_back({i,side,a,b,normal,direction,length});
    }
    struct Partial {unsigned neighbor,edge;float first,last;};
    std::vector<std::vector<Partial>> partials(count);
    for(const auto& [key,edges]:lines) {
        if(edges.size()<2)continue;
        struct Event {double at;unsigned edge;bool enter;};
        std::vector<Event> events;events.reserve(edges.size()*2);
        const auto direction=edges.front().direction;
        for(unsigned i=0;i<edges.size();++i) {
            const double a=glm::dot(edges[i].a,direction),b=glm::dot(edges[i].b,direction);
            events.push_back({std::min(a,b),i,true});events.push_back({std::max(a,b),i,false});
        }
        std::sort(events.begin(),events.end(),[](const auto& x,const auto& y){return x.at<y.at;});
        std::set<unsigned> active;
        for(std::size_t i=0;i<events.size();) {
            const double first=events[i].at;
            do {if(events[i].enter)active.insert(events[i].edge);else active.erase(events[i].edge);++i;}
            while(i<events.size()&&events[i].at==first);
            if(i==events.size()||active.size()!=2||events[i].at-first<=tolerance)continue;
            const auto& x=edges[*active.begin()];const auto& y=edges[*std::next(active.begin())];
            // Equal plane/line hashes are only candidates. A reversed winding
            // places the interiors on opposite sides; coincident/same-side
            // faces and non-manifold intervals never acquire a neighbor.
            if(x.triangle==y.triangle||glm::dot(x.b-x.a,y.b-y.a)>=0||glm::dot(x.normal,y.normal)<.9995)continue;
            if(glm::length(glm::cross(y.a-x.a,x.direction))>tolerance||
               glm::length(glm::cross(y.b-x.a,x.direction))>tolerance)continue;
            bool coplanar=true;
            for(unsigned v=0;v<3;++v)coplanar&=std::abs(glm::dot(glm::dvec3(pixels[offset+y.triangle*5+v])-x.a,x.normal))<=tolerance;
            if(!coplanar)continue;
            const double last=events[i].at;
            const auto project=[&](const Edge& edge,unsigned neighbor) {
                const double a=glm::dot(edge.a,direction),b=glm::dot(edge.b,direction);
                const double t0=(first-a)/(b-a),t1=(last-a)/(b-a);
                return Partial{neighbor,edge.side,float(std::clamp(std::min(t0,t1),0.0,1.0)),
                                                  float(std::clamp(std::max(t0,t1),0.0,1.0))};
            };
            const auto toY=project(x,y.triangle),toX=project(y,x.triangle);
            if(toY.first<toY.last&&toX.first<toX.last) {
                partials[x.triangle].push_back(toY);partials[y.triangle].push_back(toX);
            }
        }
    }
    for(unsigned i=0;i<count;++i) {
        auto& entries=partials[i];
        std::sort(entries.begin(),entries.end(),[](const auto& x,const auto& y) {
            return std::tie(x.edge,x.neighbor,x.first)<std::tie(y.edge,y.neighbor,y.first);
        });
        std::vector<Partial> merged;merged.reserve(entries.size());
        for(const auto& entry:entries) {
            if(!merged.empty()&&merged.back().edge==entry.edge&&merged.back().neighbor==entry.neighbor&&entry.first<=merged.back().last)
                merged.back().last=std::max(merged.back().last,entry.last);
            else merged.push_back(entry);
        }
        entries=std::move(merged);
        for(const auto& entry:entries) {
            const auto a=glm::dvec3(pixels[offset+i*5+entry.edge]),b=glm::dvec3(pixels[offset+i*5+(entry.edge+1)%3]);
            if(entry.first*glm::length(b-a)>tolerance||(1-entry.last)*glm::length(b-a)>tolerance)continue;
            for(unsigned edge=0;edge<3;++edge) {
                const auto c=glm::dvec3(pixels[offset+entry.neighbor*5+edge]),d=glm::dvec3(pixels[offset+entry.neighbor*5+(edge+1)%3]);
                if(glm::length(a-d)<=tolerance&&glm::length(b-c)<=tolerance) {
                    attributes[begin+i][entry.edge]=float(entry.neighbor);break;
                }
            }
        }
        entries.erase(std::remove_if(entries.begin(),entries.end(),[&](const auto& entry) {
            return attributes[begin+i][entry.edge]==float(entry.neighbor);
        }),entries.end());
    }
    std::vector<bool> overflow(count,false);
    for(unsigned i=0;i<count;++i)overflow[i]=partials[i].size()>MaxLumenPartialNeighbours;
    for(unsigned i=0;i<count;++i) {
        auto& entries=partials[i];
        if(overflow[i])continue;
        entries.erase(std::remove_if(entries.begin(),entries.end(),[&](const auto& entry){return overflow[entry.neighbor];}),entries.end());
        std::sort(entries.begin(),entries.end(),[](const auto& x,const auto& y) {
            return std::tie(x.edge,x.first,x.neighbor)<std::tie(y.edge,y.first,y.neighbor);
        });
        if(entries.empty())continue;
        attributes[begin+i].w=float(attributes.size());
        attributes.push_back({float(entries.size()),0,0,0});
        for(const auto& entry:entries)attributes.push_back({float(entry.neighbor),float(entry.edge),entry.first,entry.last});
    }
    attributes.resize(std::max<std::size_t>(LumenSurfaceWidth,((attributes.size()+LumenSurfaceWidth-1)/LumenSurfaceWidth)*LumenSurfaceWidth),glm::vec4(0));
}
}
std::shared_ptr<const LumenGiScene> BuildLumenGiScene(std::span<const ProbeTriangle> triangles,float size,std::uint32_t maxTriangleResolution) {
    if(!std::isfinite(size)||size<.05f||size>10)throw std::invalid_argument("Surface texel size must be .05..10 world units");
    if(maxTriangleResolution<1||maxTriangleResolution>32)throw std::invalid_argument("Surface triangle resolution must be 1..32");
    auto result=std::make_shared<LumenGiScene>();result->geometry_=BuildRealtimeGiScene(triangles);
    result->distanceField_=BuildMeshDistanceField(*result->geometry_);
    const auto& geometry=*result->geometry_;const auto& data=geometry.Pixels();
    const auto count=geometry.TriangleCount(),offset=geometry.NodeCount()*2;
    std::vector<unsigned> resolutions(count,1);std::uint32_t total=count;
    // Always cover every triangle. Spend remaining texels on larger surfaces;
    // globally bounded memory even for a high-density input mesh.
    for(unsigned i=0;i<count;++i) {
        const auto a=glm::vec3(data[offset+i*5]),b=glm::vec3(data[offset+i*5+1]),c=glm::vec3(data[offset+i*5+2]);
        const double desired=std::ceil(std::sqrt(double(glm::length(glm::cross(b-a,c-a))*.5f))/(double(size)));
        unsigned r=unsigned(std::clamp(desired,1.0,double(maxTriangleResolution)));
        while(r>1&&total+r*r-1>MaxLumenSurfels)--r;
        resolutions[i]=r;total+=r*r-1;
    }
    result->surfels_=total;const auto pixels=count+total*2;
    result->attributes_.resize(std::max(1024u,((pixels+1023)/1024)*1024),glm::vec4(0));
    unsigned first=0;
    for(unsigned i=0;i<count;++i) {
        const auto a=glm::vec3(data[offset+i*5]),b=glm::vec3(data[offset+i*5+1]),c=glm::vec3(data[offset+i*5+2]);
        const auto N=glm::normalize(glm::cross(b-a,c-a));const auto r=resolutions[i];
        const float cell=std::sqrt(glm::length(glm::cross(b-a,c-a))*.5f)/r;
        const float footprint=std::max({glm::length(b-a),glm::length(c-b),glm::length(a-c)})/r;
        result->attributes_[i]={float(first),float(r),cell,footprint};
        for(unsigned y=0;y<r;++y)for(unsigned x=0;x<r;++x) {
            // Uniform barycentric lattice. Mirror the square's unused half
            // into the triangle; duplicate texels preserve the existing r*r
            // allocation and permit a regular bilinear lookup at every edge.
            // The previous sqrt/ratio map stretched noisy texels into fans.
            const auto p=TriangleCachePoint(a,b,c,x,y,r);
            result->attributes_[count+(first+y*r+x)*2]=glm::vec4(p,float(i));
            result->attributes_[count+(first+y*r+x)*2+1]=glm::vec4(N,0);
        }
        first+=r*r;
    }
    result->emissiveTriangles_=AppendEmissiveTriangles(result->attributes_,geometry,total);
    AppendCoplanarAdjacency(result->attributes_,geometry,result->AdjacencyOffset());
    return result;
}
std::shared_ptr<const LumenGiScene> RefitLumenGiScene(const LumenGiScene& previous,std::span<const ProbeTriangle> triangles) {
    auto result=std::make_shared<LumenGiScene>(previous);result->geometry_=RefitRealtimeGiScene(*previous.Geometry(),triangles);
    const auto& geometry=*result->geometry_;const auto& data=geometry.Pixels();
    const unsigned count=geometry.TriangleCount(),offset=geometry.NodeCount()*2;
    for(unsigned i=0;i<count;++i) {
        const auto a=glm::vec3(data[offset+i*5]),b=glm::vec3(data[offset+i*5+1]),c=glm::vec3(data[offset+i*5+2]);
        const auto N=glm::normalize(glm::cross(b-a,c-a));auto meta=result->attributes_[i];unsigned first=unsigned(meta.x),r=unsigned(meta.y);
        result->attributes_[i].z=std::sqrt(glm::length(glm::cross(b-a,c-a))*.5f)/r;
        result->attributes_[i].w=std::max({glm::length(b-a),glm::length(c-b),glm::length(a-c)})/r;
        for(unsigned y=0;y<r;++y)for(unsigned x=0;x<r;++x) {
            result->attributes_[count+(first+y*r+x)*2]=glm::vec4(TriangleCachePoint(a,b,c,x,y,r),float(i));
            result->attributes_[count+(first+y*r+x)*2+1]=glm::vec4(N,0);
        }
    }
    // A stale field must never skip moved geometry. Exact BVH/native queries
    // remain active until a new topology asset supplies a freshly built field.
    result->distanceFieldCurrent_=previous.DistanceFieldCurrent()&&std::equal(data.begin(),data.end(),previous.Geometry()->Pixels().begin(),
        [](const auto& a,const auto& b){return glm::vec3(a)==glm::vec3(b);});
    result->emissiveTriangles_=AppendEmissiveTriangles(result->attributes_,geometry,result->surfels_);
    AppendCoplanarAdjacency(result->attributes_,geometry,result->AdjacencyOffset());
    return result;
}
bool ValidateLumenGiSettings(const LumenGiSettings& s,std::string* error) {
    const auto range=[](float v,float lo,float hi){return std::isfinite(v)&&v>=lo&&v<=hi;};
    RealtimeGiSettings grid;grid.origin=s.origin;grid.spacing=s.spacing;grid.counts=s.counts;
    const bool valid=ValidateRealtimeGiSettings(grid)&&range(s.intensity,0,16)&&range(s.maxDistance,.1f,1000)&&
        range(s.rayBias,.0001f,.5f)&&range(s.thickness,.0001f,s.maxDistance)&&range(s.surfaceHistory,0,.98f)&&
        range(s.gatherHistory,0,.98f)&&range(s.bounceFeedback,0,.95f)&&s.surfaceUpdatesPerFrame>=1&&
        s.surfaceUpdatesPerFrame<=MaxLumenSurfels&&s.surfaceRays>=1&&s.surfaceRays<=32&&s.gatherRays>=1&&s.gatherRays<=32&&
        s.probeSpacing>=4&&s.probeSpacing<=32&&s.screenSteps>=4&&s.screenSteps<=64&&s.reflectionRays>=1&&s.reflectionRays<=32&&
        s.reflectionTraceMaxDimension>=128&&s.reflectionTraceMaxDimension<=512&&range(s.reflectionResolutionScale,.5f,1)&&
        range(s.reflectionSourceResolutionScale,.25f,1)&&
        range(s.maxReflectionRoughness,0,1)&&(!s.enabled||bool(s.scene))&&
        static_cast<std::uint32_t>(s.lightingView)<=static_cast<std::uint32_t>(LumenLightingView::DirectOnly);
    if(error)*error=valid?"":"Invalid hybrid Lumen GI scene, grid, budgets or history";return valid;
}
} // namespace Render
