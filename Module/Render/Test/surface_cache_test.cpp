#include "Render/Public/Pipeline/surface_cache.h"
#include "Render/Public/Pipeline/surface_cache_gi.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

namespace {
using namespace Render;
void Check(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
template<class F>void Reject(F function,const char* message) {
    bool rejected=false;try{function();}catch(const std::exception&){rejected=true;}Check(rejected,message);
}
std::vector<ProbeTriangle> Cube(float half=.5f,bool inward=false) {
    std::vector<ProbeTriangle> triangles;
    for(int axis=0;axis<3;++axis)for(int side=0;side<2;++side) {
        const int u=(axis+1)%3,v=(axis+2)%3;
        const auto point=[&](int x,int y){glm::vec3 p(0);p[axis]=side?half:-half;p[u]=(x?1.f:-1.f)*half;p[v]=(y?1.f:-1.f)*half;return p;};
        auto a=point(0,0),b=point(1,0),c=point(1,1),d=point(0,1);
        if(bool(side)==inward){std::swap(b,d);}
        triangles.push_back({a,b,c});triangles.push_back({a,c,d});
    }
    return triangles;
}
ProbeGlobalSdfSettings GlobalSettings() {
    ProbeGlobalSdfSettings s;s.origin={-2,-2,-2};s.spacing={.5f,.5f,.5f};s.counts={13,9,9};return s;
}
void TestSharedAndDynamic() {
    const auto triangles=Cube();const auto field=BakeProbeMeshSdf(triangles,16,true);
    std::vector<ProbeSdfInstance> instances(2);instances[0].mesh=instances[1].mesh=field;
    instances[0].geometryMesh=instances[1].geometryMesh=field;
    instances[1].localToWorld=glm::translate(glm::mat4(1),glm::vec3(2,0,0));instances[1].movable=true;
    const auto scene=BuildProbeSdfScene(instances,GlobalSettings());
    std::vector<SurfaceCacheMaterial> materials(2);materials[0].albedo={0,0,0};materials[1].albedo={.1f,.6f,.2f};
    const auto cache=BuildSurfaceCache(*scene,materials,{16});
    Check(cache->CardCount()==12&&cache->Statistics().localCaptures==1,"Repeated geometry failed to share local card capture");
    Check(cache->Statistics().captureRays==6*16*16,"Shared capture ray budget incorrect");
    Check(cache->Statistics().validTexels==12*16*16,"Cube capture has holes");
    Check(cache->Attributes().size()==cache->PlaneTexels()*3&&cache->Metadata().size()%cache->Width()==0,"GPU atlas layout invalid");
    const auto& geometry=*scene->PackedGeometry();
    for(std::uint32_t triangle=0;triangle<geometry.TriangleCount();++triangle) {
        const auto packed=geometry.NodeCount()*2+triangle*5;
        const float centerX=(geometry.Pixels()[packed].x+geometry.Pixels()[packed+1].x+geometry.Pixels()[packed+2].x)/3;
        const float owner=cache->Metadata()[cache->TriangleOwnerOffset()+triangle/4][triangle%4];
        Check(owner==(centerX>1?1.f:0.f),"BVH-sorted triangle resolved to the wrong object's cache");
    }
    const auto black=cache->Lookup({.5f,0,0},{1,0,0},0);
    Check(black.valid&&black.albedo==glm::vec3(0),"Valid black surface was confused with uncovered cache");
    Check(!cache->Lookup({0,0,0},{1,0,0},0).valid,"Interior borrowed exterior card radiance");
    Check(!cache->Lookup({.5f,0,0},{-1,0,0},0).valid,"Opposite shell normal accepted exterior cache");
    const auto generation=cache->Generation();const auto attributes=cache->Attributes();
    Check(!cache->Update(*scene,materials)&&cache->Generation()==generation&&cache->Statistics().updatedCards==0,"Static cache needlessly updated");
    Check(cache->Attributes()==attributes,"Static no-op modified card attributes");
    const auto alternate=BakeProbeMeshSdf(triangles,24,true);instances[0].mesh=alternate;scene->UpdateInstances(instances);
    Check(!cache->Update(*scene,materials)&&cache->Generation()==generation,"MDF quality switch invalidated stable geometry capture");
    instances[1].localToWorld=glm::translate(glm::mat4(1),glm::vec3(3,0,0))*glm::scale(glm::mat4(1),glm::vec3(1,2,.5f));
    scene->UpdateInstances(instances);Check(cache->Update(*scene,materials),"Moved object did not invalidate cards");
    Check(cache->Statistics().updatedCards==6&&cache->Statistics().captureRays==0,"Movement recaptured geometry or updated unrelated cards");
    const auto moved=cache->Lookup({3.5f,0,0},{1,0,0},1);
    Check(moved.valid&&moved.albedo==materials[1].albedo&&glm::dot(moved.normal,glm::vec3(1,0,0))>.999f,"Transformed cache lookup or material failed");
    Check(!cache->Lookup({2.5f,0,0},{1,0,0},1).valid,"Movement retained stale cache surface");
    materials[1].albedo={.8f,.2f,.1f};materials[1].emission={2,1,.5f};
    Check(cache->Update(*scene,materials)&&cache->Statistics().updatedCards==6&&cache->Statistics().captureRays==0,"Material update recaptured geometry");
    const auto material=cache->Lookup({3.5f,0,0},{1,0,0},1);
    Check(material.valid&&material.albedo==materials[1].albedo&&material.emission==materials[1].emission,"Material/emission generation remained stale");
    const auto stable=cache->Generation();materials[0].albedo.x=-1;
    Reject([&]{cache->Update(*scene,materials);},"Negative material accepted");Check(cache->Generation()==stable,"Rejected update changed generation");
    Reject([&]{SurfaceCacheScene invalid({17});},"Non-power-of-two card resolution accepted");
}
void TestUncoveredConcavity() {
    auto triangles=Cube();const auto inner=Cube(.45f,true);triangles.insert(triangles.end(),inner.begin(),inner.end());
    const auto shell=BakeProbeMeshSdf(triangles,16,true);ProbeSdfInstance instance;instance.mesh=shell;
    const auto scene=BuildProbeSdfScene(std::span(&instance,1),GlobalSettings());const auto cache=BuildSurfaceCache(*scene,{}, {32});
    Check(cache->Lookup({.5f,0,0},{1,0,0}).valid,"Outer shell absent from single-depth cards");
    Check(!cache->Lookup({.45f,0,0},{-1,0,0}).valid,"Hidden inner shell borrowed outer surface lighting");
    Check(!cache->Lookup({.45f,0,0},{0,0,0}).valid,"Invalid normal accepted");
    Check(!cache->Lookup({100,100,100},{1,0,0}).valid,"Out-of-bounds lookup accepted");
}
void TestContactSettings() {
    Check(ValidateSurfaceCacheContactSettings({}),"Default receiver-contact settings are invalid");
    Check(ValidateSurfaceCacheGiSettings({}),"Default disabled GI settings are invalid");
    for(const std::uint32_t rays:{8u,16u,17u,32u}) {
        SurfaceCacheContactSettings settings;settings.raysPerPixel=rays;
        Check(ValidateSurfaceCacheContactSettings(settings),"A supported receiver-contact ray budget was rejected");
    }
    for(const std::uint32_t rays:{0u,7u,33u,std::numeric_limits<std::uint32_t>::max()}) {
        SurfaceCacheContactSettings settings;settings.raysPerPixel=rays;
        Check(!ValidateSurfaceCacheContactSettings(settings),"An out-of-range receiver-contact ray budget was accepted");
    }
    for(const std::uint32_t radius:{1u,2u}) {
        SurfaceCacheContactSettings settings;settings.filterRadius=radius;
        Check(ValidateSurfaceCacheContactSettings(settings),"A supported receiver-contact filter radius was rejected");
    }
    for(const std::uint32_t radius:{0u,3u,std::numeric_limits<std::uint32_t>::max()}) {
        SurfaceCacheContactSettings settings;settings.filterRadius=radius;
        Check(!ValidateSurfaceCacheContactSettings(settings),"An out-of-range receiver-contact filter radius was accepted");
        SurfaceCacheGiSettings gi;gi.contact=settings;
        Check(!ValidateSurfaceCacheGiSettings(gi),"Parent GI validation ignored an invalid receiver-contact filter radius");
    }
    for(const bool enabled:{false,true})for(const bool layers:{false,true}) {
        SurfaceCacheContactSettings settings;settings.enabled=enabled;settings.layerValidation=layers;
        Check(ValidateSurfaceCacheContactSettings(settings),"A receiver-contact feature toggle invalidated legal settings");
        SurfaceCacheGiSettings gi;gi.contact=settings;
        Check(ValidateSurfaceCacheGiSettings(gi),"Parent GI validation rejected legal receiver-contact feature toggles");
    }
    for(const std::uint32_t passes:{0u,1u,2u,3u,4u,std::numeric_limits<std::uint32_t>::max()}) {
        SurfaceCacheGiSettings gi;gi.contact.filterPasses=passes;
        Check(ValidateSurfaceCacheGiSettings(gi)==(passes>=1&&passes<=3),"Contact filter-pass bounds were not validated");
    }
    for(const std::uint32_t radius:{0u,1u,4u,5u,std::numeric_limits<std::uint32_t>::max()}) {
        SurfaceCacheGiSettings gi;gi.contact.guideMaxRadius=radius;
        Check(ValidateSurfaceCacheGiSettings(gi)==(radius>=1&&radius<=4),"Guided contact filter radius bounds were not validated");
    }
    for(const bool directions:{false,true})for(const bool confidence:{false,true}) {
        SurfaceCacheGiSettings gi;gi.contact.temporalDirections=directions;gi.contact.coverageWeightedFilter=confidence;
        Check(ValidateSurfaceCacheGiSettings(gi),"Legal contact-denoising switches were rejected");
    }
    struct Range {float SurfaceCacheContactSettings::*member;float minimum,maximum;const char* message;};
    const Range ranges[]{
        {&SurfaceCacheContactSettings::maxDistance,.01f,2.f,"Invalid receiver-contact distance validation"},
        {&SurfaceCacheContactSettings::parallaxAngleDegrees,1.f,45.f,"Invalid receiver-contact angle validation"},
        {&SurfaceCacheContactSettings::weightThreshold,.001f,.5f,"Invalid receiver-contact weight validation"},
        {&SurfaceCacheContactSettings::filterStrength,0.f,1.f,"Invalid receiver-contact filter strength validation"},
        {&SurfaceCacheContactSettings::guideHitAngleDegrees,1.f,45.f,"Invalid receiver-contact filter hit-angle validation"}};
    for(const auto& range:ranges) {
        for(const float value:{range.minimum,range.maximum}) {
            SurfaceCacheContactSettings settings;settings.*range.member=value;
            Check(ValidateSurfaceCacheContactSettings(settings),range.message);
        }
        for(const float value:{std::nextafter(range.minimum,-std::numeric_limits<float>::infinity()),
            std::nextafter(range.maximum,std::numeric_limits<float>::infinity()),
            std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity()}) {
            SurfaceCacheContactSettings settings;settings.*range.member=value;
            Check(!ValidateSurfaceCacheContactSettings(settings),range.message);
            SurfaceCacheGiSettings gi;gi.contact=settings;
            Check(!ValidateSurfaceCacheGiSettings(gi),"Parent GI validation ignored invalid receiver-contact settings");
            gi.contact.enabled=false;
            Check(!ValidateSurfaceCacheGiSettings(gi),"Disabling receiver-contact filtering hid invalid retained settings");
        }
    }
}
}
int main() {
    try {TestSharedAndDynamic();TestUncoveredConcavity();TestContactSettings();std::cout<<"Surface cache tests passed: shared captures, black validity, stable MDF switch, dynamic/material invalidation, hidden shell rejection and receiver-contact settings validation\n";return 0;}
    catch(const std::exception& e){std::cerr<<"Surface cache test failed: "<<e.what()<<'\n';return 1;}
}
