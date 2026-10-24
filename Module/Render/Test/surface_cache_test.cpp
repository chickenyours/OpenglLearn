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
void CheckDirtyCardCoverage(const SurfaceCacheScene& cache,const std::vector<glm::vec4>& oldAttributes,
                          const std::vector<glm::vec4>& oldMetadata) {
    Check(oldAttributes.size()==cache.Attributes().size()&&oldMetadata.size()==cache.Metadata().size(),"Dirty coverage fixture changed atlas size");
    std::vector<bool> dirty(cache.CardCount(),false);
    for(const auto card:cache.DirtyCards()){Check(card<dirty.size(),"Dirty card index is outside the resident atlas");dirty[card]=true;}
    const auto resolution=cache.Settings().pageResolution,pageTexels=resolution*resolution;
    // Independent full-array diff: every changed attribute must belong to a
    // reported card, including normal/card IDs and valid-to-uncovered texels.
    for(std::size_t texel=0;texel<oldAttributes.size();++texel)if(oldAttributes[texel]!=cache.Attributes()[texel]) {
        const auto card=(texel%cache.PlaneTexels())/pageTexels;
        Check(card<dirty.size()&&dirty[card],"Dirty cards omitted a changed attribute texel");
    }
    for(std::size_t texel=0;texel<oldMetadata.size();++texel)if(oldMetadata[texel]!=cache.Metadata()[texel]) {
        const auto card=texel/SurfaceCacheCardTexels;
        Check(card<dirty.size()&&dirty[card],"A stable-topology update changed unreported card or owner metadata");
    }
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
    const auto generation=cache->Generation();const auto attributes=cache->Attributes(),metadata=cache->Metadata();
    Check(!cache->Update(*scene,materials)&&cache->Generation()==generation&&cache->Statistics().updatedCards==0,"Static cache needlessly updated");
    Check(cache->Attributes()==attributes,"Static no-op modified card attributes");
    const auto alternate=BakeProbeMeshSdf(triangles,24,true);instances[0].mesh=alternate;scene->UpdateInstances(instances);
    Check(!cache->Update(*scene,materials)&&cache->Generation()==generation,"MDF quality switch invalidated stable geometry capture");
    instances[1].localToWorld=glm::translate(glm::mat4(1),glm::vec3(3,0,0))*glm::scale(glm::mat4(1),glm::vec3(1,2,.5f));
    scene->UpdateInstances(instances);Check(cache->Update(*scene,materials),"Moved object did not invalidate cards");
    Check(cache->Statistics().updatedCards==6&&cache->Statistics().captureRays==0,"Movement recaptured geometry or updated unrelated cards");
    CheckDirtyCardCoverage(*cache,attributes,metadata);
    const auto moved=cache->Lookup({3.5f,0,0},{1,0,0},1);
    Check(moved.valid&&moved.albedo==materials[1].albedo&&glm::dot(moved.normal,glm::vec3(1,0,0))>.999f,"Transformed cache lookup or material failed");
    Check(!cache->Lookup({2.5f,0,0},{1,0,0},1).valid,"Movement retained stale cache surface");
    const auto movedAttributes=cache->Attributes(),movedMetadata=cache->Metadata();
    materials[1].albedo={.8f,.2f,.1f};materials[1].emission={2,1,.5f};
    Check(cache->Update(*scene,materials)&&cache->Statistics().updatedCards==6&&cache->Statistics().captureRays==0,"Material update recaptured geometry");
    CheckDirtyCardCoverage(*cache,movedAttributes,movedMetadata);
    const auto material=cache->Lookup({3.5f,0,0},{1,0,0},1);
    Check(material.valid&&material.albedo==materials[1].albedo&&material.emission==materials[1].emission,"Material/emission generation remained stale");
    const auto stable=cache->Generation();materials[0].albedo.x=-1;
    Reject([&]{cache->Update(*scene,materials);},"Negative material accepted");Check(cache->Generation()==stable,"Rejected update changed generation");
    Reject([&]{SurfaceCacheScene invalid({17});},"Non-power-of-two card resolution accepted");
}
void TestDirtyCardLifetime() {
    const auto field=BakeProbeMeshSdf(Cube(),16,true);std::vector<ProbeSdfInstance> instances(2);
    instances[0].mesh=instances[1].mesh=field;instances[1].movable=true;
    const auto scene=BuildProbeSdfScene(instances,GlobalSettings());
    const auto cache=BuildSurfaceCache(*scene,{}, {16});const auto uploadedGeneration=cache->Generation();
    const auto uploadedAttributes=cache->Attributes(),uploadedMetadata=cache->Metadata();
    const auto uploadedGeometry=scene->PackedGeometry();
    instances[1].localToWorld=glm::translate(glm::mat4(1),glm::vec3(.4f,0,0));scene->UpdateInstances(instances);
    Check(cache->Update(*scene)&&cache->Generation()==uploadedGeneration+1,"Dirty lifetime fixture missed its motion generation");
    Check(uploadedGeometry->TopologyKey()==scene->PackedGeometry()->TopologyKey(),"Rigid refit unexpectedly replaced owner-map topology");
    Check(cache->OwnerGeometry()->TopologyKey()==scene->PackedGeometry()->TopologyKey(),"Moved cache did not retain the trace geometry owner-map identity");
    CheckDirtyCardCoverage(*cache,uploadedAttributes,uploadedMetadata);
    const auto movedGeneration=cache->Generation();
    Check(!cache->Update(*scene)&&cache->DirtyCards().empty()&&cache->Generation()==movedGeneration,
        "A no-op update did not clear transient dirty cards; upload fallback fixture is no longer valid");
    std::vector<SurfaceCacheMaterial> materials(2);materials[0].albedo={.2f,.3f,.4f};
    Check(cache->Update(*scene,materials)&&cache->Generation()==uploadedGeneration+2,"Missed-generation fixture did not advance twice");
    for(const auto card:cache->DirtyCards())Check(card<6,"Latest dirty cards unexpectedly retain a previous object's motion");
    Check(cache->Attributes()!=uploadedAttributes,"Missed-generation fixture lost earlier attribute changes");
    // Same triangle count and texture dimensions still need a full metadata
    // upload when packed triangle ownership receives a new topology identity.
    instances[0].mesh=BakeProbeMeshSdf(Cube(.6f),16,true);scene->UpdateInstances(instances);
    Check(scene->PackedGeometry()->TriangleCount()==uploadedGeometry->TriangleCount()&&
        scene->PackedGeometry()->TopologyKey()!=uploadedGeometry->TopologyKey(),"Topology fallback fixture did not preserve dimensions while replacing topology");
    Check(cache->OwnerGeometry()->TopologyKey()!=scene->PackedGeometry()->TopologyKey(),"Pending topology edit was hidden from the upload compatibility guard");
    Check(cache->Update(*scene)&&cache->OwnerGeometry()->TopologyKey()==scene->PackedGeometry()->TopologyKey(),"Topology replacement did not invalidate card metadata");
}
void TestLocalCaptureTransforms() {
    const auto field=BakeProbeMeshSdf(Cube(),16,true);
    std::vector<ProbeSdfInstance> instances(2);instances[0].mesh=instances[1].mesh=field;instances[1].movable=true;
    instances[1].localToWorld=glm::translate(glm::mat4(1),glm::vec3(2,0,0));
    const auto scene=BuildProbeSdfScene(instances,GlobalSettings());
    std::vector<SurfaceCacheMaterial> materials(2);materials[1].albedo={.2f,.5f,.1f};
    const auto reference=BuildSurfaceCache(*scene,materials,{16});
    const auto local=BuildSurfaceCache(*scene,materials,{16,true});
    const auto checkWorld=[&] {
        Check(local->Attributes()==reference->Attributes(),"Local capture public world attributes differ from the retained path");
        const auto& raw=local->UploadAttributes();const auto& metadata=local->Metadata();const auto& world=reference->Attributes();
        const auto plane=local->PlaneTexels(),page=16u*16u;
        for(std::uint32_t texel=0;texel<local->TexelCount();++texel)if(raw[texel].w>.5f) {
            const auto card=texel/page,first=(card/6)*6,meta=card*SurfaceCacheCardTexels;
            const glm::mat4 forward(metadata[(first+0)*SurfaceCacheCardTexels+11],metadata[(first+1)*SurfaceCacheCardTexels+11],
                metadata[(first+2)*SurfaceCacheCardTexels+11],metadata[(first+3)*SurfaceCacheCardTexels+11]);
            const glm::mat3 inverse{glm::vec3(metadata[meta+0]),glm::vec3(metadata[meta+1]),glm::vec3(metadata[meta+2])};
            const auto position=glm::vec4(glm::vec3(forward*raw[texel]),raw[texel].w);
            const auto normal=glm::vec4(glm::normalize(glm::transpose(inverse)*glm::vec3(raw[plane+texel])),raw[plane+texel].w);
            Check(position==world[texel]&&normal==world[plane+texel],"GPU card metadata cannot restore world position/normal or owner");
        }
        // The topology case replaces Cube(.5) with Cube(.6); query its
        // current outer +X face, rather than the old face now in the interior.
        const auto mesh=instances[1].geometryMesh?instances[1].geometryMesh:instances[1].mesh;
        const auto low=mesh->BoundsMin(),high=mesh->BoundsMax();
        const auto localPoint=glm::vec3(high.x,(low.y+high.y)*.5f,(low.z+high.z)*.5f);
        const auto point=glm::vec3(instances[1].localToWorld*glm::vec4(localPoint,1));
        const auto normal=glm::normalize(glm::transpose(glm::mat3(glm::inverse(instances[1].localToWorld)))*glm::vec3(1,0,0));
        const auto a=local->Lookup(point,normal,1),b=reference->Lookup(point,normal,1);
        Check(a.valid&&b.valid&&a.texel==b.texel&&a.position==b.position&&a.normal==b.normal&&a.albedo==b.albedo,
            "Local capture CPU Lookup lost its world-space contract");
    };
    checkWorld();const auto captures=local->Statistics().captureRays;
    Check(captures==6*16*16&&local->AttributeDirtyCards().size()==12,"Local capture initialization lost shared capture/attribute work");
    const auto initialUpload=local->UploadAttributes();
    for(unsigned pose=0;pose<3;++pose) {
        const auto scale=pose==2?glm::vec3(-1.2f,.7f,1.6f):glm::vec3(1.f+.2f*pose,2.f,.5f);
        instances[1].localToWorld=glm::translate(glm::mat4(1),glm::vec3(2.f+.1f*pose,.1f,0))*
            glm::rotate(glm::mat4(1),.3f*float(pose+1),glm::normalize(glm::vec3(1,2,3)))*glm::scale(glm::mat4(1),scale);
        scene->UpdateInstances(instances,true,true);
        Check(reference->Update(*scene,materials)&&local->Update(*scene,materials),"Rigid local capture did not update current metadata");
        Check(local->DirtyCards().size()==6&&local->AttributeDirtyCards().empty()&&local->Statistics().updatedTexels==0&&
            local->Statistics().captureRays==0,"Rigid local capture rewrote attributes or recaptured mesh data");
        for(const auto card:local->DirtyCards())Check(card>=6&&card<12,"Rigid local capture dirtied an untouched object");
        Check(local->UploadAttributes()==initialUpload,"Rigid motion modified persistent local capture/albedo storage");
        checkWorld();
    }
    Check(!local->Update(*scene,materials)&&local->DirtyCards().empty()&&local->AttributeDirtyCards().empty(),
        "Local capture no-op retained transient dirty records");
    materials[1].albedo={.7f,.3f,.1f};materials[1].emission={2,1,.5f};
    Check(reference->Update(*scene,materials)&&local->Update(*scene,materials)&&local->AttributeDirtyCards().size()==6&&
        local->Statistics().captureRays==0,"Local capture material invalidation did not update its attribute pages");
    checkWorld();const auto materialUpload=local->UploadAttributes();
    instances[1].mesh=BakeProbeMeshSdf(Cube(.6f),16,true);scene->UpdateInstances(instances,true,true);
    Check(reference->Update(*scene,materials)&&local->Update(*scene,materials)&&local->AttributeDirtyCards().size()==6&&
        local->Statistics().captureRays==6*16*16,"Local capture topology replacement did not rebuild its capture");
    Check(local->UploadAttributes()!=materialUpload,"Local capture topology replacement retained stale local positions");
    checkWorld();
    const auto oldFace=glm::vec3(instances[1].localToWorld*glm::vec4(.5f,0,0,1));
    const auto oldNormal=glm::normalize(glm::transpose(glm::mat3(glm::inverse(instances[1].localToWorld)))*glm::vec3(1,0,0));
    Check(!local->Lookup(oldFace,oldNormal,1).valid&&!reference->Lookup(oldFace,oldNormal,1).valid,
        "Topology replacement allowed the previous outer face to borrow the new exterior capture");
}
void TestUncoveredConcavity() {
    auto triangles=Cube();const auto inner=Cube(.45f,true);triangles.insert(triangles.end(),inner.begin(),inner.end());
    const auto shell=BakeProbeMeshSdf(triangles,16,true);ProbeSdfInstance instance;instance.mesh=shell;
    const auto scene=BuildProbeSdfScene(std::span(&instance,1),GlobalSettings());
    for(const bool localMode:{false,true}) {
        const auto cache=BuildSurfaceCache(*scene,{}, {32,localMode});
        Check(cache->Lookup({.5f,0,0},{1,0,0}).valid,"Outer shell absent from single-depth cards");
        Check(!cache->Lookup({.45f,0,0},{-1,0,0}).valid,"Hidden inner shell borrowed outer surface lighting");
        Check(!cache->Lookup({.45f,0,0},{0,0,0}).valid,"Invalid normal accepted");
        Check(!cache->Lookup({100,100,100},{1,0,0}).valid,"Out-of-bounds lookup accepted");
    }
}
void TestMotionSpatialReuseSettings() {
    const SurfaceCacheTemporalSettings defaults;
    Check(defaults.staticConvergence&&!defaults.motionConsistentSpatialReuse&&!defaults.localCameraHistory,
        "Motion spatial reuse and local camera history must remain opt-in experiments");
    // The option may be retained while temporal/static convergence is disabled
    // or maxFrames is one; those legal dormant states are handled by the pipeline.
    for(const bool temporalEnabled:{false,true})for(const bool stationaryEnabled:{false,true})
        for(const bool consistentReuse:{false,true})for(const bool localHistory:{false,true})for(const std::uint32_t frames:{1u,10u,64u}) {
            SurfaceCacheGiSettings gi;auto& settings=gi.temporal;
            settings.enabled=temporalEnabled;settings.staticConvergence=stationaryEnabled;
            settings.motionConsistentSpatialReuse=consistentReuse;settings.localCameraHistory=localHistory;settings.maxFrames=frames;
            Check(ValidateSurfaceCacheTemporalSettings(settings),
                "A legal active or dormant motion-spatial reuse setting was rejected");
            Check(ValidateSurfaceCacheGiSettings(gi),
                "Parent GI validation rejected a legal motion-spatial reuse setting");
        }
    for(const bool consistentReuse:{false,true})for(const bool stationaryEnabled:{false,true})for(const bool localHistory:{false,true}) {
        SurfaceCacheGiSettings gi;auto& settings=gi.temporal;
        settings.motionConsistentSpatialReuse=consistentReuse;settings.staticConvergence=stationaryEnabled;settings.localCameraHistory=localHistory;
        settings.maxFrames=0;
        Check(!ValidateSurfaceCacheTemporalSettings(settings)&&!ValidateSurfaceCacheGiSettings(gi),
            "Motion-spatial reuse hid an invalid history cap");
        settings.maxFrames=10;settings.directionPhases=65;
        Check(!ValidateSurfaceCacheTemporalSettings(settings)&&!ValidateSurfaceCacheGiSettings(gi),
            "Motion-spatial reuse hid an invalid retained direction-phase setting");
        settings.directionPhases=8;settings.dynamicMaxFrames=17;
        Check(!ValidateSurfaceCacheTemporalSettings(settings)&&!ValidateSurfaceCacheGiSettings(gi),
            "Motion-spatial reuse hid an invalid dynamic response cap");
    }
}
void TestContactSettings() {
    Check(ValidateSurfaceCacheContactSettings({}),"Default receiver-contact settings are invalid");
    Check(ValidateSurfaceCacheGiSettings({}),"Default disabled GI settings are invalid");
    for(const std::uint32_t multiplier:{0u,1u,2u,3u,4u,5u,std::numeric_limits<std::uint32_t>::max()}) {
        SurfaceCacheGiSettings gi;gi.contact.disocclusionSampling=true;gi.contact.disocclusionRayMultiplier=multiplier;
        Check(ValidateSurfaceCacheGiSettings(gi)==(multiplier>=1&&multiplier<=4),"Fresh receiver sampling multiplier bounds were not validated");
    }
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
    try {TestSharedAndDynamic();TestDirtyCardLifetime();TestLocalCaptureTransforms();TestUncoveredConcavity();TestMotionSpatialReuseSettings();TestContactSettings();std::cout<<"Surface cache tests passed: shared captures, black validity, stable MDF switch, dirty-card coverage/lifetime, dynamic/material invalidation, hidden shell rejection, motion-spatial reuse and receiver-contact settings validation\n";return 0;}
    catch(const std::exception& e){std::cerr<<"Surface cache test failed: "<<e.what()<<'\n';return 1;}
}
