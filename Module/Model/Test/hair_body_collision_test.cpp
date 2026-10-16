#include "Model/Private/hair_body_collision.h"
#include "Model/Public/vmd_animation.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <bit>
#include <chrono>
#include <stdexcept>

namespace {
using namespace Model;
using namespace Model::detail;
int failures=0;
void Expect(bool condition,const char* text){std::printf("%-105s %s\n",text,condition?"ok":"FAIL");if(!condition)++failures;}
bool Near(glm::dvec3 a,glm::dvec3 b,double epsilon=1e-8){return glm::length(a-b)<=epsilon;}
struct ScopedOption {
    std::string name,previous;bool existed=false;
    ScopedOption(const char* key,const char* value):name(key){if(const auto* old=std::getenv(key)){existed=true;previous=old;}
#ifdef _WIN32
        _putenv_s(key,value?value:"");
#else
        if(value)setenv(key,value,1);else unsetenv(key);
#endif
    }
    ~ScopedOption(){
#ifdef _WIN32
        _putenv_s(name.c_str(),existed?previous.c_str():"");
#else
        if(existed)setenv(name.c_str(),previous.c_str(),1);else unsetenv(name.c_str());
#endif
    }
};
bool Bits(double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
bool Bits(glm::dvec3 a,glm::dvec3 b){return Bits(a.x,b.x)&&Bits(a.y,b.y)&&Bits(a.z,b.z);}
bool Same(const HairBodyNearest& a,const HairBodyNearest& b){return a.found==b.found&&a.inside==b.inside&&a.backfacing==b.backfacing&&a.triangle==b.triangle&&a.mesh==b.mesh&&Bits(a.point,b.point)&&Bits(a.normal,b.normal)&&Bits(a.distance,b.distance)&&Bits(a.signedDistance,b.signedDistance);}
HairBodyNearest InsideResult(const HairBodyNearest& nearest){return nearest.found&&nearest.inside?nearest:HairBodyNearest{};}
bool Same(const HairBodyContact& a,const HairBodyContact& b){return a.hit==b.hit&&a.swept==b.swept&&a.triangle==b.triangle&&Bits(a.normal,b.normal)&&Bits(a.correction,b.correction)&&Bits(a.closestPoint,b.closestPoint)&&Bits(a.signedGap,b.signedGap)&&Bits(a.outsideClearance,b.outsideClearance);}
SkeletalAssetPackage Package(double units=1){
    SkeletalAssetPackage p;p.geometry=std::make_shared<StaticModel>();p.skeleton.bones.resize(3);
    p.skeleton.bones[0].name="root";p.skeleton.bones[1].name="head";p.skeleton.bones[1].parent=0;p.skeleton.bones[2].name="arm";p.skeleton.bones[2].parent=0;
    p.geometry->materials.push_back({});p.geometry->materials.back().name="SKIN";return p;
}
std::uint32_t AddBox(SkeletalAssetPackage& p,glm::dvec3 center,glm::dvec3 half,std::uint32_t bone=0,const char* name="Body"){
    const auto meshIndex=static_cast<std::uint32_t>(p.geometry->meshes.size());StaticMesh mesh;mesh.name=name;
    const std::array<glm::dvec3,8> corners{{{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}}};
    for(const auto& corner:corners){StaticVertex vertex;vertex.position=center+corner*half;vertex.normal=glm::vec3(glm::normalize(corner));mesh.vertices.push_back(vertex);}
    mesh.indices={0,2,1,0,3,2,4,5,6,4,6,7,1,2,6,1,6,5,0,4,7,0,7,3,3,7,6,3,6,2,0,1,5,0,5,4};
    p.geometry->meshes.push_back(mesh);MeshSkin skin;skin.vertices.resize(mesh.vertices.size());for(auto& vertex:skin.vertices){vertex.joints[0]=bone;vertex.weights={1,0,0,0};}p.skins.push_back(skin);p.geometry->instances.push_back({{},meshIndex,glm::dmat4(1)});return meshIndex;
}
std::uint32_t AddPlane(SkeletalAssetPackage& p){
    const auto meshIndex=static_cast<std::uint32_t>(p.geometry->meshes.size());StaticMesh mesh;mesh.name="ThinClothing";
    for(auto position:std::array<glm::dvec3,4>{{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}}}){StaticVertex vertex;vertex.position=position;vertex.normal={0,0,1};mesh.vertices.push_back(vertex);}mesh.indices={0,1,2,0,2,3};
    p.geometry->meshes.push_back(mesh);MeshSkin skin;skin.vertices.resize(4);p.skins.push_back(skin);p.geometry->instances.push_back({{},meshIndex,glm::dmat4(1)});return meshIndex;
}
void AddSphere(SkeletalAssetPackage& p,bool openEye=false){
    const auto meshIndex=static_cast<std::uint32_t>(p.geometry->meshes.size());StaticMesh mesh;mesh.name=openEye?"EyeWhite":"SphericalHead";
    auto vertex=[&](glm::dvec3 direction){StaticVertex v;v.position=direction*.1;v.normal=glm::vec3(direction);mesh.vertices.push_back(v);};
    constexpr int segments=16,rings=8;vertex({0,1,0});
    for(int ring=1;ring<rings;++ring)for(int segment=0;segment<segments;++segment){const double latitude=glm::pi<double>()*ring/rings,longitude=glm::two_pi<double>()*segment/segments;vertex({std::sin(latitude)*std::cos(longitude),std::cos(latitude),std::sin(latitude)*std::sin(longitude)});}
    const auto south=static_cast<std::uint32_t>(mesh.vertices.size());vertex({0,-1,0});
    auto triangle=[&](std::uint32_t a,std::uint32_t b,std::uint32_t c){if(openEye&&(mesh.vertices[a].position.z< -1e-10||mesh.vertices[b].position.z< -1e-10||mesh.vertices[c].position.z< -1e-10))return;mesh.indices.insert(mesh.indices.end(),{a,b,c});};
    for(int segment=0;segment<segments;++segment){const int next=(segment+1)%segments;triangle(0,1+segment,1+next);triangle(south,1+(rings-2)*segments+next,1+(rings-2)*segments+segment);}
    for(int ring=0;ring<rings-2;++ring)for(int segment=0;segment<segments;++segment){const int next=(segment+1)%segments;const std::uint32_t a=1+ring*segments+segment,b=1+ring*segments+next,c=a+segments,d=b+segments;triangle(a,c,b);triangle(b,c,d);}
    p.geometry->meshes.push_back(mesh);MeshSkin skin;skin.vertices.resize(mesh.vertices.size());p.skins.push_back(skin);p.geometry->instances.push_back({{},meshIndex,glm::dmat4(1)});
}
void OptionalRealComparison(int argc,char** argv){
    if(argc==1)return;
    try{std::filesystem::path bodyFile,animationFile;double units=1;std::size_t samples=128;
        for(int i=1;i<argc;++i){const std::string option=argv[i];if(i+1>=argc)throw std::runtime_error("Expected a value after "+option);const std::string value=argv[++i];
            if(option=="--body")bodyFile=std::filesystem::u8path(value);else if(option=="--animation")animationFile=std::filesystem::u8path(value);else if(option=="--units")units=std::stod(value);else if(option=="--samples")samples=std::stoul(value);else throw std::runtime_error("Unknown option: "+option);}
        if(bodyFile.empty()||!std::isfinite(units)||units<=0||samples<1||samples>256)throw std::runtime_error("Use --body FILE [--animation FILE] --units N [--samples 1..256]");
        const auto start=std::chrono::steady_clock::now();std::string error;auto asset=LoadSkeletalAssetPackage(bodyFile,&error);if(!asset)throw std::runtime_error("Real body load: "+error);
        std::optional<AnimationSequenceAsset> animation;if(!animationFile.empty()){animation=LoadAnimationSequenceAsset(animationFile,*asset,&error);if(!animation)throw std::runtime_error("Real animation load: "+error);}
        HairBodyCollision reference,optimized;
        {ScopedOption fast("MODEL_HAIR_FAST_RAYS","0"),inside("MODEL_HAIR_INSIDE_ONLY","0"),grid("MODEL_HAIR_RAY_GRID","1"),paired("MODEL_HAIR_PAIRED_RAYS","1"),audit("MODEL_HAIR_RAY_AUDIT","1");if(!reference.Initialize(*asset,{},units,&error))throw std::runtime_error(error);}
        {ScopedOption fast("MODEL_HAIR_FAST_RAYS","2"),inside("MODEL_HAIR_INSIDE_ONLY","1"),grid("MODEL_HAIR_RAY_GRID","1"),paired("MODEL_HAIR_PAIRED_RAYS","1"),audit("MODEL_HAIR_RAY_AUDIT","1");if(!optimized.Initialize(*asset,{},units,&error))throw std::runtime_error(error);}
        bool identical=true;double maxGapDifference=0,referenceMs=0,optimizedMs=0,insideReferenceMs=0,insideOptimizedMs=0;std::size_t observations=0;std::uint64_t randomState=0x0c43e71abf91d2ull;
        auto random=[&](){randomState=randomState*6364136223846793005ull+1442695040888963407ull;return static_cast<std::uint32_t>(randomState>>32);};
        auto scalar=[&](){return double(random())/double(UINT32_MAX)*2-1;};
        for(int poseIndex=0;poseIndex<6;++poseIndex){const double time=animation?animation->duration*poseIndex/5.:0.;const auto pose=EvaluateSkeletalPose(*asset,animation?&*animation:nullptr,time,false);if(!reference.Update(pose,&error)||!optimized.Update(pose,&error))throw std::runtime_error(error);
            StaticModel rendered;if(!SkinSkeletalMesh(*asset,pose,rendered,&error))throw std::runtime_error(error);if(rendered.meshes.empty())throw std::runtime_error("Real comparison has no meshes");
            for(std::size_t i=0;i<samples;++i){const auto meshIndex=random()%rendered.meshes.size();const auto& mesh=rendered.meshes[meshIndex];if(mesh.vertices.empty())continue;auto point=mesh.vertices[random()%mesh.vertices.size()].position;
                for(const auto& instance:rendered.instances)if(instance.meshIndex==meshIndex){point=glm::dvec3(instance.transform*glm::dvec4(point,1));break;}point=point/units+glm::dvec3(scalar(),scalar(),scalar())*.02;
                const auto before=std::chrono::steady_clock::now();const auto expected=reference.Nearest(point);const auto middle=std::chrono::steady_clock::now();const auto actual=optimized.Nearest(point);const auto after=std::chrono::steady_clock::now();referenceMs+=std::chrono::duration<double,std::milli>(middle-before).count();optimizedMs+=std::chrono::duration<double,std::milli>(after-middle).count();
                identical&=Same(expected,actual);if(expected.found&&actual.found)maxGapDifference=std::max(maxGapDifference,std::abs(expected.signedDistance-actual.signedDistance));++observations;
                const auto beforeInside=std::chrono::steady_clock::now();const auto insideReference=reference.NearestInside(point);const auto middleInside=std::chrono::steady_clock::now();const auto insideActual=optimized.NearestInside(point);const auto afterInside=std::chrono::steady_clock::now();insideReferenceMs+=std::chrono::duration<double,std::milli>(middleInside-beforeInside).count();insideOptimizedMs+=std::chrono::duration<double,std::milli>(afterInside-middleInside).count();identical&=Same(InsideResult(expected),insideReference)&&Same(insideReference,insideActual);
                if(i%17==0)identical&=Same(reference.Contact(point,point,.0015,.08,false),optimized.Contact(point,point,.0015,.08,false));
                if(std::chrono::duration<double>(after-start).count()>110)throw std::runtime_error("Real comparison exceeded its 110-second wall budget");}
        }
        const auto fast=optimized.QueryStats();std::printf("Real fixed-ray A/B: poses=6 points=%zu maxGapDifference=%.17g referenceMs=%.3f optimizedMs=%.3f insideReferenceMs=%.3f insideOptimizedMs=%.3f fastTests=%llu planeRejects=%llu\n",observations,maxGapDifference,referenceMs,optimizedMs,insideReferenceMs,insideOptimizedMs,static_cast<unsigned long long>(fast.fastRayTests),static_cast<unsigned long long>(fast.fastRayPlaneRejects));
        Expect(observations>0&&fast.fastRayTests>0&&identical&&maxGapDifference<=1e-10&&reference.QueryStats().rayResultHash==fast.rayResultHash,"Optional real body six-pose random surface-neighborhood A/B preserves nearest, inside, contact and every first-hit bit");
    }catch(const std::exception& exception){std::printf("Real fixed-ray comparison error: %s\n",exception.what());Expect(false,"Optional real fixed-ray comparison completed within its declared inputs and wall budget");}
}
}

int main(int argc,char** argv){
    std::string error;
    {
        HairBodyCollision empty;Expect(Same(empty.NearestInside({0,0,0}),{})&&Same(empty.NearestInside({NAN,0,0}),{}),"Inside-only queries return canonical not-found for empty bodies and invalid inputs");
        auto p=Package();AddBox(p,{0,0,0},{.08,.08,.08});const auto shirt=AddBox(p,{0,0,0},{.1,.1,.1},0,"OpenShirt");auto& indices=p.geometry->meshes[shirt].indices;indices.erase(indices.begin()+24,indices.begin()+30);HairBodyCollision reference,optimized;
        {ScopedOption inside("MODEL_HAIR_INSIDE_ONLY","0");reference.Initialize(p,{},1,&error);}{ScopedOption inside("MODEL_HAIR_INSIDE_ONLY","1");optimized.Initialize(p,{},1,&error);}
        bool identical=true;for(const auto point:std::array<glm::dvec3,8>{{{0,0,0},{.085,0,0},{.105,0,0},{.095,.095,.095},{.08,0,0},{.1,0,0},{0,.099,0},{4,4,4}}}){const auto full=reference.Nearest(point);identical&=Same(InsideResult(full),reference.NearestInside(point))&&Same(InsideResult(full),optimized.NearestInside(point));}
        for(const double side:std::array<double,2>{{-.1,.1}})for(const double offset:std::array<double,5>{{-2e-10,-1e-10,0,1e-10,2e-10}})for(int ulp=-1;ulp<=1;++ulp){double x=side+offset;if(ulp)x=std::nextafter(x,ulp>0?std::numeric_limits<double>::infinity():-std::numeric_limits<double>::infinity());const auto full=reference.Nearest({x,0,0});identical&=Same(InsideResult(full),optimized.NearestInside({x,0,0}));}
        Expect(identical,"Inside-only mode and full reference preserve nested/open body fields and nextafter boundary-exception results bit for bit");
        ScopedOption optimizedMode("MODEL_HAIR_INSIDE_ONLY","1");auto tied=Package();AddBox(tied,{-.04,0,0},{.1,.1,.1});AddBox(tied,{.04,0,0},{.1,.1,.1});optimized.Initialize(tied,{},1,&error);const auto full=optimized.Nearest({0,0,0});Expect(full.inside&&Same(full,optimized.NearestInside({0,0,0})),"Inside-only preserves equally deep component and lowest-triangle tie selection");
        AddPlane(tied);optimized.Initialize(tied,{},1,&error);Expect(!optimized.Nearest({0,0,0}).inside&&!optimized.NearestInside({0,0,0}).found,"Global internal surface touching still overrides another component's deep enclosure exactly as Nearest");
        reference.ResetQueryStats();optimized.Initialize(p,{},1,&error);optimized.ResetQueryStats();std::uint32_t hint=UINT32_MAX;
        for(int i=0;i<200;++i){const glm::dvec3 point(1+i*.01,2,3);reference.NearestInside(point);optimized.NearestInside(point,&hint);}
        Expect(hint==UINT32_MAX&&optimized.QueryStats().insideOnlyQueries==200&&optimized.QueryStats().nearestQueries==200&&optimized.QueryStats().nodeTests<reference.QueryStats().nodeTests&&optimized.QueryStats().triangleTests<reference.QueryStats().triangleTests,"Inside-only outside proof leaves hint unchanged, counts actual queries honestly and reduces real geometric BVH work");
    }
    {
        auto p=Package();AddBox(p,{0,0,0},{.1,.1,.1});const auto hair=AddBox(p,{0,.2,0},{.05,.05,.05},1,"HAIR_Back");p.morphs.resize(3);p.morphs[0].deltas.push_back({hair,0,{.01,0,0},{0,0,0}});p.morphs[1].deltas.push_back({0,0,{.01,0,0},{0,0,0}});
        HairBodyCollision body;Expect(body.Initialize(p,{},1,&error)&&body.GeometryRevision()==1,"Actual body geometry revision starts at one");auto pose=EvaluateSkeletalPose(p,nullptr,0,false);body.Update(pose,&error);body.ResetMotion();Expect(body.GeometryRevision()==1,"Identical pose and ResetMotion do not advance actual body revision");
        pose.local[1].translation={.2,.1,.3};pose.local[2].translation={.1,.2,.3};pose.global=GlobalBoneTransforms(p.skeleton,pose.local);pose.morphWeights={1,0,1};body.Update(pose,&error);Expect(body.GeometryRevision()==1,"Hair-only bones/morphs and an unused morph leave retained body revision unchanged");
        pose.morphWeights[1]=.5f;body.Update(pose,&error);const auto changed=body.GeometryRevision();body.Update(pose,&error);body.ResetMotion();Expect(changed==2&&body.GeometryRevision()==changed,"Actual body morph changes advance once while repeated geometry is stable");
        pose.local[0].translation.x=1e-12;pose.global=GlobalBoneTransforms(p.skeleton,pose.local);body.Update(pose,&error);Expect(body.GeometryRevision()==changed+1,"Even a representable picometer body-coordinate change advances the exact geometry revision");
        const auto before=body.GeometryRevision();const auto nearest=body.Nearest({.105,0,0});auto invalid=p;invalid.geometry=std::make_shared<StaticModel>(*p.geometry);invalid.geometry->meshes[0].vertices[0].position.x=NAN;
        Expect(!body.Initialize(invalid,{},1,&error)&&body.GeometryRevision()==before&&Same(nearest,body.Nearest({.105,0,0})),"A failed late reinitialization preserves the previous body state and geometry revision");
        Expect(body.Initialize(p,{},1,&error)&&body.GeometryRevision()==before+1&&body.Initialize(p,{},1,&error)&&body.GeometryRevision()==before+2,"Successful reinitialization advances the same object's revision even when importing identical rest geometry");
    }
    {
        HairBodyCollision empty;Expect(!empty.MayBeInsideComponentBounds({0,0,0})&&!empty.MayOverlapComponentBounds({-1,-1,-1},{1,1,1}),"Empty component-bound broad phases return false");
        auto p=Package();AddBox(p,{0,0,0},{.1,.1,.1});HairBodyCollision body;body.Initialize(p,{},1,&error);
        Expect(body.MayBeInsideComponentBounds({0,0,0})&&body.MayBeInsideComponentBounds({.1,.1,.1})&&!body.MayBeInsideComponentBounds({.10001,0,0}),"Component-bound point candidates include enclosure and exact boundary touches");
        Expect(body.MayOverlapComponentBounds({.1,-.02,-.02},{.2,.02,.02})&&body.MayOverlapComponentBounds({.2,.02,.02},{.1,-.02,-.02})&&!body.MayOverlapComponentBounds({.10001,-.02,-.02},{.2,.02,.02}),"Component-bound overlap is inclusive, normalizes reversed inputs and rejects disjoint boxes");
        Expect(!body.MayBeInsideComponentBounds({NAN,0,0})&&!body.MayBeInsideComponentBounds({INFINITY,0,0})&&!body.MayOverlapComponentBounds({0,0,0},{INFINITY,0,0})&&!body.MayOverlapComponentBounds({NAN,0,0},{0,0,0}),"Component-bound broad phases reject every nonfinite input");
        Expect(body.QueryStats().nearestQueries==0&&body.QueryStats().surfaceNearestQueries==0&&body.QueryStats().insideQueries==0&&body.QueryStats().rayQueries==0&&body.QueryStats().triangleTests==0&&body.QueryStats().sweepTriangleTests==0,"Component-bound broad phases visit no triangles and perform no nearest, inside or ray queries");
        auto separated=Package();AddBox(separated,{-1,0,0},{.1,.1,.1});AddBox(separated,{1,0,0},{.1,.1,.1});HairBodyCollision gap;gap.Initialize(separated,{},1,&error);
        Expect(!gap.MayBeInsideComponentBounds({0,0,0})&&!gap.MayOverlapComponentBounds({-.2,-.02,-.02},{.2,.02,.02}),"An empty cavity between distinct components is rejected despite overlap with the aggregate root bounds");
        auto open=Package();AddPlane(open);HairBodyCollision sheet;sheet.Initialize(open,{},1,&error);
        Expect(sheet.OpenComponentCount()==1&&sheet.MayBeInsideComponentBounds({0,0,0})&&sheet.MayOverlapComponentBounds({-.1,-.1,-.01},{.1,.1,.01})&&!sheet.MayBeInsideComponentBounds({0,0,.01}),"Open components remain conservative candidates on their real bounds without pretending to define a solid");
    }
    {
        auto p=Package();AddBox(p,{0,0,0},{.1,.1,.1});HairBodyCollision body;Expect(body.Initialize(p,{},1,&error),"Builds unsigned-surface query fixture");
        const auto before=body.QueryStats();std::uint32_t hint=UINT32_MAX;
        const auto insideCandidate=body.NearestSurface({.095,0,0},.03,&hint),outsideCandidate=body.NearestSurface({.105,0,0},.03,&hint);
        Expect(insideCandidate.found&&!insideCandidate.inside&&insideCandidate.backfacing&&Near(insideCandidate.point,{.1,0,0})&&Near(insideCandidate.normal,{1,0,0})&&std::abs(insideCandidate.distance-.005)<1e-9&&insideCandidate.signedDistance==insideCandidate.distance,"Unsigned nearest returns an actual finite body face, oriented normal and positive distance for an interior candidate");
        Expect(outsideCandidate.found&&!outsideCandidate.inside&&!outsideCandidate.backfacing&&Near(outsideCandidate.point,{.1,0,0})&&Near(outsideCandidate.normal,{1,0,0}),"Unsigned nearest keeps the same oriented triangle normal on its outside side");
        Expect(!body.NearestSurface({.105,0,0},.004).found&&body.NearestSurface({.105,0,0},.006).found,"Unsigned nearest honors the finite search radius without silently assuming outside");
        const auto deep=body.NearestSurface({0,0,0},.2);Expect(deep.found&&!deep.inside&&deep.backfacing&&std::abs(deep.distance-.1)<1e-9&&deep.signedDistance>0,"Unsigned surface query does not perform or pretend to perform a solid-inside classification");
        Expect(!body.NearestSurface({NAN,0,0}).found&&!body.NearestSurface({0,0,0},NAN).found&&!body.NearestSurface({0,0,0},-1).found,"Unsigned nearest safely rejects nonfinite positions and invalid finite ranges");
        Expect(body.QueryStats().insideQueries==before.insideQueries&&body.QueryStats().rayQueries==before.rayQueries&&body.QueryStats().nearestQueries==before.nearestQueries&&body.QueryStats().surfaceNearestQueries>0,"Cheap endpoint surface gates perform no geometric-inside, ray or full-Nearest queries");
        const auto actualInside=body.Nearest({.095,0,0});body.NearestSurface({-.102,.015,.025});const auto stillInside=body.Nearest({.095,0,0});Expect(Same(actualInside,stillInside)&&actualInside.inside&&actualInside.signedDistance<0,"Adding unsigned surface queries does not change the existing verified Nearest result");
    }
    {
        auto p=Package();AddPlane(p);for(auto& vertex:p.geometry->meshes[0].vertices)vertex.normal={0,0,-1};HairBodyCollision body;body.Initialize(p,{},1,&error);
        const auto front=body.NearestSurface({0,0,-.01}),back=body.NearestSurface({0,0,.01});Expect(front.found&&back.found&&Near(front.normal,{0,0,-1})&&Near(back.normal,{0,0,-1})&&!front.backfacing&&back.backfacing,"Unsigned nearest follows a reversed authored open-surface orientation without turning its back half-space into a solid");
    }
    {
        auto p=Package();AddBox(p,{0,1.5,0},{.1,.12,.09},1,"Head");AddBox(p,{0,1.1,0},{.15,.2,.08});AddBox(p,{.3,1.25,0},{.05,.18,.05},2,"Arm");
        const auto hair=AddBox(p,{0,1.5,0},{.13,.15,.12},1,"StyledHair");AddBox(p,{0,1.5,0},{.14,.16,.13},1,"HAIR_Back");
        HairBodyCollision body;const std::array<std::uint32_t,1> excluded{hair};Expect(body.Initialize(p,excluded,1,&error),"Builds body BVH while retaining head, chest and arm");
        Expect(body.TriangleCount()==36&&body.VertexCount()==24,"Explicit hair ids and HAIR names are excluded before compact body skin");
        Expect(body.TriangleBoneWeight(0,1)==1&&body.TriangleBoneWeight(0,0)==0,"Triangle bone provenance uses the actual head triangle's normalized skin weights");
        const auto nearest=body.Nearest({.101,1.5,0},.01);Expect(nearest.found&&std::abs(nearest.signedDistance-.001)<1e-9&&Near(nearest.normal,{1,0,0}),"Head contact uses true triangle surface with a 1 mm gap, not enlarged capsule");
        const auto nearOutside=body.Contact({.101,1.5,0},{.101,1.5,0},.0015,.06,false);Expect(nearOutside.hit&&std::abs(nearOutside.outsideClearance-.001)<1e-9,"Outside clearance reports the actual endpoint surface distance even within the contact margin");
        const auto root=body.Contact({.095,1.5,0},{.095,1.5,0},.0015);Expect(root.hit&&!root.swept&&std::abs(root.correction.x-.0065)<1e-9,"Finite inside-reset correction removes a 5 mm embedding plus actual margin");
        Expect(root.outsideClearance==0,"Inside endpoints never receive an outside-clearance certificate");
        const auto insideSweep=body.SweepContact({.095,1.5,0},{.095,1.5,0},.0015,false);Expect(!insideSweep.hit,"Pure static sweep does not replace endpoint inside-reset contact handling");
        const auto far=body.Contact({0,1.5,0},{0,1.5,0},.0015,.06);Expect(!far.hit,"Bounded inside treatment does not apply an infinite negative half-space");
        Expect(far.outsideClearance==0,"An endpoint with no nearest surface within the query radius retains zero clearance");
        const auto chest=body.Contact({0,1.1,.085},{0,1.1,.078},.0015);Expect(chest.hit&&chest.normal.z>.99,"Actual chest surface blocks hair probes");
        const auto arm=body.Contact({.36,1.25,0},{.345,1.25,0},.0015);Expect(arm.hit&&arm.normal.x>.99,"Actual arm surface blocks hair probes");
        auto pose=EvaluateSkeletalPose(p,nullptr,0,false);pose.local[2].translation={.4,0,0};pose.global=GlobalBoneTransforms(p.skeleton,pose.local);Expect(body.Update(pose,&error),"Refits a moved arm without rebuilding topology");
        Expect(!body.Nearest({.35,1.25,0},.02).found&&body.Nearest({.75,1.25,0},.02).found,"Refit updates body bounds and removes the previous arm's static surface");
        const auto swept=body.Contact({.5,1.25,0},{.5,1.25,0},.0015);Expect(swept.hit&&swept.swept&&swept.correction.x>.2,"Stationary hair detects a moving body triangle crossing and follows its end pose");
        Expect(!body.Contact({.5,1.25,0},{.5,1.25,0},.0015,.06,false).hit,"Endpoint-only iterations do not repeat an already consumed body sweep");
        body.ResetMotion();Expect(!body.Contact({.5,1.25,0},{.5,1.25,0},.0015).hit,"ResetMotion suppresses a spurious sweep across a timeline seek");
        Expect(body.QueryStats().triangleTests<1000&&body.QueryStats().sweepTriangleTests<1000,"Queries traverse BVH candidates instead of scanning every triangle per probe");
    }
    {
        auto p=Package();AddPlane(p);HairBodyCollision body;Expect(body.Initialize(p,{},1,&error),"Builds an open, zero-thickness clothing surface");
        const auto behind=body.Nearest({.2,.1,-.01});Expect(behind.found&&!behind.inside&&behind.signedDistance>0,"An open face's back half-space is outside, not solid body penetration");
        Expect(!body.Contact({0,0,-.0005},{0,0,-.0005},.0015,.06,false).hit,"A stationary probe just behind a thin open face is not pushed across it without CCD evidence");
        const auto contact=body.Contact({0,0,.25},{0,0,-.25},.0015,.02);Expect(contact.hit&&contact.swept&&Near(contact.correction,{0,0,.2515}),"Continuous crossing catches a thin face even when both endpoints are far from it");
        const auto sweptClearance=body.Contact({0,0,.025},{0,0,-.025},.0015,.06);Expect(sweptClearance.hit&&sweptClearance.swept&&std::abs(sweptClearance.outsideClearance-.025)<1e-9,"A swept hit preserves the endpoint's separately measured outside clearance diagnostic");
        const auto outside=body.Contact({2,0,.25},{2,0,-.25},.0015,.02);Expect(!outside.hit,"CCD checks finite triangle barycentrics, not an unbounded collision plane");
        auto pose=EvaluateSkeletalPose(p,nullptr,0,false);pose.local[0].translation={0,0,.4};pose.global=GlobalBoneTransforms(p.skeleton,pose.local);body.Update(pose,&error);
        const auto moving=body.Contact({.1,.1,.2},{.1,.1,.2},.0015,.02);Expect(moving.hit&&moving.swept&&Near(moving.correction,{0,0,.2015}),"Point versus moving thin triangles uses previous and current body positions");
        const auto beforeQueries=body.QueryStats();const auto pureMoving=body.SweepContact({.1,.1,.2},{.1,.1,.2},.0015,true);const auto pureStatic=body.SweepContact({.1,.1,.2},{.1,.1,.2},.0015,false);
        Expect(pureMoving.hit&&Near(pureMoving.correction,{0,0,.2015})&&!pureStatic.hit,"Pure sweeps can consume animated body motion once while static-body passes do not replay it");
        Expect(body.QueryStats().nearestQueries==beforeQueries.nearestQueries&&body.QueryStats().insideQueries==beforeQueries.insideQueries,"SweepContact performs no global nearest or geometric-inside queries");
        const auto staticCross=body.SweepContact({.1,.1,.6},{.1,.1,.2},.0015,false);Expect(staticCross.hit&&Near(staticCross.correction,{0,0,.2015}),"Current-body-only sweep catches a new local surface correction crossing the final body face");
        Expect(!body.SweepContact({2,0,.6},{2,0,.2},.0015,false).hit&&!body.SweepContact({0,0,.2},{0,0,.6},.0015,false).hit,"Pure sweep rejects finite-face misses and outward-only crossings");
        Expect(body.MayIntersectSweptBounds({.1,.1,.2},{.1,.1,.2},.0015,true)&&!body.MayIntersectSweptBounds({.1,.1,.2},{.1,.1,.2},.0015,false),"Broad phase selects previous/current or current-only body bounds consistently");
        Expect(body.MayIntersectSweptBounds({0,0,.4012},{0,0,.4012},.0015,false)&&!body.MayIntersectSweptBounds({0,0,.4012},{0,0,.4012},0,false),"Broad phase conservatively includes the requested contact margin");
        bool conservative=true,legacyIdentical=true;std::size_t hits=0;
        for(int sample=0;sample<300;++sample){const glm::dvec3 start(std::sin(sample*.13)*1.2,std::cos(sample*.31)*1.2,.6),end(std::sin(sample*.17)*1.2,std::cos(sample*.23)*1.2,.2);
            for(bool animatedBody:{false,true}){const auto swept=body.SweepContact(start,end,.0015,animatedBody);if(swept.hit){++hits;conservative&=body.MayIntersectSweptBounds(glm::min(start,end),glm::max(start,end),.0015,animatedBody);}}
            const auto endpoint=body.Contact(start,end,.0015,.06,false),swept=body.SweepContact(start,end,.0015,true),combined=body.Contact(start,end,.0015,.06,true);auto expected=swept.hit?swept:endpoint;if(swept.hit)expected.outsideClearance=endpoint.outsideClearance;legacyIdentical&=Same(expected,combined);
        }
        Expect(hits>0&&conservative,"Every actual static/moving finite-triangle CCD hit survives leaf-triangle AABB broad phase");
        Expect(legacyIdentical,"Original Contact endpoint/swept merge, correction and clearance remain bitwise unchanged");
        Expect(!body.SweepContact({NAN,0,0},{0,0,0},.0015).hit&&!body.MayIntersectSweptBounds({0,0,0},{0,0,0},-1),"New swept APIs safely reject nonfinite positions and invalid margins");
    }
    {
        auto p=Package();AddBox(p,{-1,0,0},{.1,.1,.1});AddBox(p,{1,0,0},{.1,.1,.1});HairBodyCollision body;body.Initialize(p,{},1,&error);
        Expect(!body.MayIntersectSweptBounds({-.2,-.02,-.02},{.2,.02,.02},.0015,true),"Broad phase rejects the empty gap inside the root AABB by testing actual leaf triangle bounds");
        Expect(body.MayIntersectSweptBounds({.89,-.02,-.02},{.91,.02,.02},.0015,false),"Broad phase keeps a tight query that overlaps a real body triangle surface");
    }
    {
        auto p=Package();AddSphere(p);HairBodyCollision body;Expect(body.Initialize(p,{},1,&error),"Builds a geometrically closed UV sphere with welded pole/seam topology");
        const auto inside=body.Nearest({.095,0,0});const auto outside=body.Nearest({.105,0,0});
        Expect(body.ComponentCount()==1&&body.OpenComponentCount()==0&&inside.inside&&inside.signedDistance< -.004,"True spherical-head interior is negative and retains millimeter penetration depth");
        Expect(!outside.inside&&outside.signedDistance>0,"Spherical-head exterior remains positive");
        // Reverse one patch's supplied normals. Enclosure uses two independent
        // ray intervals, rather than trusting whichever normal is nearest.
        for(auto& v:p.geometry->meshes[0].vertices)if(v.position.x>.08)v.normal=-v.normal;
        HairBodyCollision imperfect;Expect(imperfect.Initialize(p,{},1,&error)&&imperfect.Nearest({.095,0,0}).inside,"Enclosure tolerates a locally inconsistent nearest patch normal");
    }
    {
        auto p=Package();AddSphere(p,true);AddBox(p,{0,-.5,0},{.2,.2,.2});HairBodyCollision body;Expect(body.Initialize(p,{},1,&error),"Retains an open EyeWhite component and an adjacent real body without name-based eye exclusion");
        const auto oldFalsePositive=body.Nearest({0,.04,-.01});Expect(oldFalsePositive.found&&!oldFalsePositive.inside&&oldFalsePositive.signedDistance>=0,"A point beside/behind an open eye shell is not reported inside by a nearest-face negative sign");
        Expect(!body.Contact({0,.04,-.01},{0,.04,-.01},.0015,.2,false).hit,"Eye-shell back-side diagnostics cannot push distant hair away from the actual head");
    }
    {
        ScopedOption referenceMode("MODEL_HAIR_COMPONENT_BACKFACE_GATE","0");auto p=Package();AddBox(p,{0,0,0},{.08,.08,.08});const auto shirt=AddBox(p,{0,0,0},{.1,.1,.1},0,"OpenShirt");auto& indices=p.geometry->meshes[shirt].indices;indices.erase(indices.begin()+24,indices.begin()+30);
        HairBodyCollision body;Expect(body.Initialize(p,{},1,&error)&&body.OpenComponentCount()==1,"Builds an open-neck shirt around a separate closed skin body");
        const auto gap=body.Nearest({.085,0,0});Expect(gap.inside&&gap.mesh==shirt&&std::abs(gap.distance-.015)<1e-8&&gap.normal.x>.99,"Inside an open shirt uses the garment's actual enclosing wall, not the nearer internal skin face");
        Expect(!body.Nearest({.105,0,0}).inside,"Outside the open shirt is not mistaken for body volume");
        const auto hidden=body.NearestSurface({.085,0,0},.03);std::uint32_t hint=hidden.triangle;const auto before=body.QueryStats();
        Expect(hidden.found&&hidden.mesh==0&&!hidden.backfacing&&body.HasNearbyBackFacingSurface({.085,0,0},.03,&hint),"Any-nearby-backface query sees the outer shirt even when closer inner skin is front-facing and supplied as hint");
        Expect(!body.HasNearbyBackFacingSurface({.085,0,0},.014)&&body.HasNearbyBackFacingSurface({.085,0,0},.016),"Nearby-backface query retains exact finite closest-point radius bounds for nested surfaces");
        Expect(body.QueryStats().insideQueries==before.insideQueries&&body.QueryStats().rayQueries==before.rayQueries&&body.QueryStats().nearestQueries==before.nearestQueries,"Nested endpoint candidate screening runs no solid-inside, ray or full-nearest queries");
    }
    {
        ScopedOption referenceMode("MODEL_HAIR_COMPONENT_BACKFACE_GATE","0");auto p=Package();AddPlane(p);HairBodyCollision body;body.Initialize(p,{},1,&error);
        Expect(body.HasNearbyBackFacingSurface({0,0,-.005},.01)&&!body.HasNearbyBackFacingSurface({0,0,.005},.01),"Nearby-backface candidates use the actual oriented side of an open finite surface");
        Expect(!body.HasNearbyBackFacingSurface({1.1,0,-.001},.02)&&body.HasNearbyBackFacingSurface({1.01,0,-.001},.02),"Finite triangle boundary distance rejects an infinite-plane backface false candidate beyond the requested sphere");
        Expect(!body.HasNearbyBackFacingSurface({0,0,-5e-13},.01)&&body.HasNearbyBackFacingSurface({0,0,-2e-12},.01),"Backface candidate epsilon is explicit in meters and does not treat a surface touch as penetration");
        const auto trueGeometry=body.Nearest({0,0,-.005});Expect(!trueGeometry.inside&&trueGeometry.signedDistance>=0,"A positive nearby-backface candidate remains distinct from a real geometric-inside verdict");
        Expect(!body.HasNearbyBackFacingSurface({4,4,4})&&!body.HasNearbyBackFacingSurface({NAN,0,0})&&!body.HasNearbyBackFacingSurface({0,0,0},-1),"Far points and invalid nearby-backface queries return false without body rays");
    }
    {
        auto p=Package();AddSphere(p);HairBodyCollision standard,explicitReference,experiment;
        {ScopedOption option("MODEL_HAIR_COMPONENT_BACKFACE_GATE",nullptr);standard.Initialize(p,{},1,&error);}
        {ScopedOption option("MODEL_HAIR_COMPONENT_BACKFACE_GATE","0");explicitReference.Initialize(p,{},1,&error);}
        {ScopedOption option("MODEL_HAIR_COMPONENT_BACKFACE_GATE","1");experiment.Initialize(p,{},1,&error);}
        bool identical=true;std::uint32_t standardHint=UINT32_MAX,componentHint=UINT32_MAX;
        for(int i=0;i<500;++i){const glm::dvec3 point(std::sin(i*.31)*.14,std::cos(i*.23)*.14,std::sin(i*.17)*.14);const bool a=standard.HasNearbyBackFacingSurface(point,.03,&standardHint),b=experiment.HasNearbyBackFacingSurface(point,.03,&componentHint);identical&=a==b&&standardHint==componentHint;}
        Expect(identical,"Default and explicit-one backface gates retain identical per-component candidates and hint results");
        const auto before=experiment.QueryStats();std::uint32_t nonNearestBackface=UINT32_MAX;
        const bool exactAny=explicitReference.HasNearbyBackFacingSurface({.1001,0,0},.06,&nonNearestBackface);
        const bool reduced=experiment.HasNearbyBackFacingSurface({.1001,0,0},.06,&nonNearestBackface);
        Expect(exactAny&&!reduced,"Explicit-zero exact-any reference retains curved-surface backfaces that the default component gate rejects even when supplied as hint");
        Expect(experiment.HasNearbyBackFacingSurface({.095,0,0},.03)&&!experiment.HasNearbyBackFacingSurface({4,4,4},.03),"Component gate retains an actual near interior face and rejects far-away components");
        Expect(experiment.QueryStats().insideQueries==before.insideQueries&&experiment.QueryStats().rayQueries==before.rayQueries&&experiment.QueryStats().nearestQueries==before.nearestQueries,"Component candidate gate performs no full-Nearest, inside or ray queries");
        auto nested=Package();AddBox(nested,{0,0,0},{.08,.08,.08});const auto shirt=AddBox(nested,{0,0,0},{.1,.1,.1});auto& indices=nested.geometry->meshes[shirt].indices;indices.erase(indices.begin()+24,indices.begin()+30);
        HairBodyCollision nestedExperiment;{ScopedOption option("MODEL_HAIR_COMPONENT_BACKFACE_GATE","1");nestedExperiment.Initialize(nested,{},1,&error);}
        auto closest=nestedExperiment.NearestSurface({.085,0,0});std::uint32_t innerHint=closest.triangle;
        Expect(closest.mesh==0&&!closest.backfacing&&nestedExperiment.HasNearbyBackFacingSurface({.085,0,0},.03,&innerHint),"Component gate independently detects the outer shirt while the globally closest inner skin is front-facing");
        Expect(!nestedExperiment.HasNearbyBackFacingSurface({.085,0,0},.014)&&nestedExperiment.HasNearbyBackFacingSurface({.085,0,0},.016),"Component gate preserves the finite search radius across distinct nested components");
    }
    {
        auto p=Package();AddSphere(p);const auto shirt=AddBox(p,{0,0,0},{.12,.12,.12},1,"OpenShirt");auto& indices=p.geometry->meshes[shirt].indices;indices.erase(indices.begin()+24,indices.begin()+30);
        for(int i=0;i<72;++i)AddBox(p,{.4+(i%12)*.28,(i%3)*.015,(i/12)*.28},{.04,.04,.04},i%3);
        HairBodyCollision reference,optimized;
        {ScopedOption mode("MODEL_HAIR_COMPONENT_BACKFACE_GATE","1"),bvh("MODEL_HAIR_COMPONENT_BVH","0");Expect(reference.Initialize(p,{},1,&error),"Builds linear component-AABB reference for the exact nearby candidate gate");}
        {ScopedOption mode("MODEL_HAIR_COMPONENT_BACKFACE_GATE","1"),bvh("MODEL_HAIR_COMPONENT_BVH",nullptr);Expect(optimized.Initialize(p,{},1,&error),"Builds default component-AABB BVH with preallocated sorted-candidate scratch");}
        bool identical=true;std::uint32_t referenceHint=UINT32_MAX,optimizedHint=UINT32_MAX;
        for(int poseIndex=0;poseIndex<3;++poseIndex){auto pose=EvaluateSkeletalPose(p,nullptr,0,false);pose.local[0].translation={poseIndex*.23,-poseIndex*.11,poseIndex*.19};pose.local[0].rotation=glm::angleAxis(poseIndex*.61,glm::normalize(glm::dvec3(1,2,3)));pose.local[1].translation={poseIndex*.025,poseIndex*.01,-poseIndex*.015};pose.local[1].rotation=glm::angleAxis(poseIndex*.22,glm::dvec3(0,1,0));pose.local[2].translation={-poseIndex*.03,poseIndex*.04,poseIndex*.02};pose.global=GlobalBoneTransforms(p.skeleton,pose.local);
            identical&=reference.Update(pose,&error)&&optimized.Update(pose,&error);if(poseIndex==2){reference.ResetMotion();optimized.ResetMotion();}
            for(int i=0;i<400;++i){glm::dvec3 local(std::sin(i*.31)*.16,std::cos(i*.23)*.16,std::sin(i*.17)*.16);if(i>=240){const int cell=(i-240)%72;local={.4+(cell%12)*.28+std::sin(i*.73)*.08,(cell%3)*.015+std::cos(i*.29)*.07,(cell/12)*.28+std::cos(i*.47)*.08};}
                const auto point=glm::dvec3(pose.global[0]*glm::dvec4(local,1));const double radius=i%13==0?.14:(i%9+1)*.005;
                const bool expected=reference.HasNearbyBackFacingSurface(point,radius,&referenceHint),actual=optimized.HasNearbyBackFacingSurface(point,radius,&optimizedHint);identical&=expected==actual&&referenceHint==optimizedHint;
                identical&=reference.MayBeInsideComponentBounds(point)==optimized.MayBeInsideComponentBounds(point);const auto extent=glm::dvec3(radius,.007,.013);identical&=reference.MayOverlapComponentBounds(point+extent,point-extent)==optimized.MayOverlapComponentBounds(point+extent,point-extent);}
        }
        Expect(identical&&optimized.ComponentCount()==74,"Refitted component BVH preserves backface candidates, hint IDs and conservative point/box candidates for 1,200 queries over moving components");
        for(const auto& point:std::array<glm::dvec3,3>{{{4,4,4},{0,0,0},{NAN,0,0}}})for(double radius:std::array<double,4>{{0,.001,.03,-1}}){const bool expected=reference.HasNearbyBackFacingSurface(point,radius,&referenceHint),actual=optimized.HasNearbyBackFacingSurface(point,radius,&optimizedHint);identical&=expected==actual&&referenceHint==optimizedHint;}
        Expect(identical,"Component BVH retains finite range, far-point and invalid-query reference behavior");
        Expect(optimized.QueryStats().componentBvhNodeTests>0&&reference.QueryStats().componentBvhNodeTests==0&&optimized.QueryStats().componentBoundTests<reference.QueryStats().componentBoundTests,"Component BVH visits actual tree nodes and reduces exact component-bound tests");
        Expect(optimized.QueryStats().nearestQueries==0&&optimized.QueryStats().insideQueries==0&&optimized.QueryStats().rayQueries==0,"Component broad phase introduces no full-nearest, inside or ray queries");
    }
    {
        auto p=Package();AddPlane(p);AddPlane(p);for(auto& v:p.geometry->meshes[0].vertices){v.position.z=-.01;v.normal={0,0,-1};}for(auto& v:p.geometry->meshes[1].vertices)v.position.z=.01;
        HairBodyCollision body;Expect(body.Initialize(p,{},1,&error)&&body.ComponentCount()==2,"Two separated thin surfaces remain independent connected components");
        const auto gap=body.Nearest({0,0,0});Expect(!gap.inside&&gap.signedDistance>=0&&!body.Contact({0,0,0},{0,0,0},.0015,.06,false).hit,"Opposing open sheets cannot manufacture a fake solid inside the air gap");
    }
    {
        auto p=Package();AddBox(p,{0,0,0},{1,1,1});
        // Deliberately nontrivial mesh offsets; the runtime's mesh-specific
        // boneFromMesh must take priority over SkeletonBone.inverseBind.
        p.skins[0].bindTransform=glm::translate(glm::dmat4(1),glm::dvec3(2,0,0));p.geometry->instances[0].transform=p.skins[0].bindTransform;
        p.skins[0].boneFromMesh.assign(3,glm::translate(glm::dmat4(1),glm::dvec3(.3,0,0)));
        p.skeleton.bones[0].inverseBind=glm::translate(glm::dmat4(1),glm::dvec3(-7,0,0));
        p.morphs.push_back({});p.morphs[0].deltas.push_back({0,6,{.1,.2,0},{0,0,0}});
        HairBodyCollision body;Expect(body.Initialize(p,{},10,&error),"Builds a source-unit body with nonidentity bind and explicit mesh offset");
        auto pose=EvaluateSkeletalPose(p,nullptr,0,false);pose.local[0].translation={2,3,-1};pose.local[0].rotation=glm::angleAxis(1.2,glm::normalize(glm::dvec3(1,2,3)));pose.global=GlobalBoneTransforms(p.skeleton,pose.local);pose.morphWeights={.75f};body.Update(pose,&error);body.ResetMotion();
        StaticModel rendered;Expect(SkinSkeletalMesh(p,pose,rendered,&error),"Reference CPU mesh skin succeeds");
        double maximum=0;for(const auto& vertex:rendered.meshes[0].vertices){const auto world=glm::dvec3(p.geometry->instances[0].transform*glm::dvec4(vertex.position,1))/10.;const auto nearest=body.Nearest(world,.01);if(!nearest.found)maximum=1;else maximum=std::max(maximum,nearest.distance);}
        Expect(maximum<1e-10,"Collision vertices exactly match CPU-rendered morph/skin/instance positions in meters after large rotation");
        Expect(body.TriangleCount()==12&&body.VertexCount()==8,"Animation and large rotations retain immutable BVH topology");
        const auto invalid=body.Contact({0,0,0},{NAN,0,0},.0015);Expect(!invalid.hit,"Nonfinite query input produces a finite no-contact result");
    }
    {
        auto p=Package();AddSphere(p);HairBodyCollision reference,optimized;
        {ScopedOption dop("MODEL_HAIR_COMPONENT_DOP","0");reference.Initialize(p,{},1,&error);}
        {ScopedOption dop("MODEL_HAIR_COMPONENT_DOP",nullptr);optimized.Initialize(p,{},1,&error);}
        Expect(reference.MayBeInsideComponentBounds({.09,.09,.09})&&!optimized.MayBeInsideComponentBounds({.09,.09,.09}),"Projection convex bounds reject an exterior sphere corner that still lies inside the component AABB");
        bool boundary=true;for(const auto& vertex:p.geometry->meshes[0].vertices)boundary&=optimized.MayBeInsideComponentBounds(vertex.position);
        Expect(boundary&&optimized.MayBeInsideComponentBounds({.095,0,0}),"All authored sphere boundary vertices and genuine interior points remain conservative DOP candidates");
        Expect(Same(reference.Nearest({.09,.09,.09}),optimized.Nearest({.09,.09,.09}))&&Same(reference.Nearest({.095,0,0}),optimized.Nearest({.095,0,0})),"Projection screening preserves true exterior/interior nearest results exactly");
        Expect(optimized.QueryStats().componentDopRejects>0&&optimized.QueryStats().rayQueries<reference.QueryStats().rayQueries,"Projection screening actually removes exterior-component ray work");
    }
    {
        auto p=Package();AddSphere(p);for(auto& vertex:p.skins.back().vertices){vertex.joints[0]=0;vertex.weights={1,0,0,0};}
        const auto shirt=AddBox(p,{0,0,0},{.12,.12,.12},1,"OpenShirt");auto& indices=p.geometry->meshes[shirt].indices;indices.erase(indices.begin()+24,indices.begin()+30);
        AddSphere(p,true);for(auto& vertex:p.skins.back().vertices){vertex.joints[0]=2;vertex.weights={1,0,0,0};}p.geometry->instances.back().transform=glm::translate(glm::dmat4(1),glm::dvec3(.45,0,0));
        HairBodyCollision reference,optimized;
        {ScopedOption dop("MODEL_HAIR_COMPONENT_DOP","0");reference.Initialize(p,{},1,&error);}
        {ScopedOption dop("MODEL_HAIR_COMPONENT_DOP",nullptr);optimized.Initialize(p,{},1,&error);}
        bool identical=true,boundaryConservative=true;std::uint32_t referenceHint=UINT32_MAX,optimizedHint=UINT32_MAX;
        for(int poseIndex=0;poseIndex<3;++poseIndex){auto pose=EvaluateSkeletalPose(p,nullptr,0,false);pose.local[0].translation={poseIndex==2?10000.:poseIndex*.07,poseIndex*.02,-poseIndex*.03};pose.local[0].rotation=glm::angleAxis(poseIndex*.43,glm::normalize(glm::dvec3(1,2,3)));pose.local[1].rotation=glm::angleAxis(poseIndex*.17,glm::dvec3(0,1,0));pose.local[2].translation={0,poseIndex*.03,0};pose.global=GlobalBoneTransforms(p.skeleton,pose.local);identical&=reference.Update(pose,&error)&&optimized.Update(pose,&error);
            for(int i=0;i<400;++i){glm::dvec3 local((i%25-12)*.014,std::sin(i*.37)*.16,std::cos(i*.23)*.16);if(i>=300)local.x+=.45;const auto point=glm::dvec3(pose.global[0]*glm::dvec4(local,1));const auto expected=reference.Nearest(point,std::numeric_limits<double>::infinity(),&referenceHint),actual=optimized.Nearest(point,std::numeric_limits<double>::infinity(),&optimizedHint);identical&=Same(expected,actual)&&referenceHint==optimizedHint;
                if(expected.inside)boundaryConservative&=optimized.MayBeInsideComponentBounds(point);
                if(i%19==0){identical&=Same(reference.Nearest(point,.03),optimized.Nearest(point,.03));identical&=Same(reference.Contact(point,point,.0015,.06,false),optimized.Contact(point,point,.0015,.06,false));}}
            for(const auto& vertex:p.geometry->meshes[0].vertices){const auto world=glm::dvec3(pose.global[0]*glm::dvec4(vertex.position,1));boundaryConservative&=optimized.MayBeInsideComponentBounds(world);}
        }
        Expect(identical,"DOP-zero and default preserve every nearest/inside/contact bit across 1,200 moving closed/open/nested and large-coordinate queries");
        Expect(boundaryConservative,"Projection tolerance retains all reference inside points and all moving authored boundary vertices");
        Expect(optimized.QueryStats().componentDopTests>0&&optimized.QueryStats().componentDopRejects>0&&reference.QueryStats().componentDopTests==0&&optimized.QueryStats().rayQueries<reference.QueryStats().rayQueries,"Default DOP rejects convex-bound exterior components and reduces rays while explicit zero retains the unmodified reference");
    }
    {
        auto p=Package();AddSphere(p);for(auto& vertex:p.skins.back().vertices){vertex.joints[0]=1;vertex.weights={1,0,0,0};}
        const auto shirt=AddBox(p,{0,0,0},{.12,.12,.12},0,"OpenShirt");auto& indices=p.geometry->meshes[shirt].indices;indices.erase(indices.begin()+24,indices.begin()+30);
        AddSphere(p,true);for(auto& vertex:p.skins.back().vertices){vertex.joints[0]=2;vertex.weights={1,0,0,0};}p.geometry->instances.back().transform=glm::translate(glm::dmat4(1),glm::dvec3(.45,0,0));
        HairBodyCollision reference,slowGrid,optimized;
        {ScopedOption grid("MODEL_HAIR_RAY_GRID","0"),dop("MODEL_HAIR_COMPONENT_DOP","0"),audit("MODEL_HAIR_RAY_AUDIT","1"),paired("MODEL_HAIR_PAIRED_RAYS","1");Expect(reference.Initialize(p,{},1,&error),"Builds original shared-ray BVH reference without projection grids");}
        {ScopedOption grid("MODEL_HAIR_RAY_GRID","1"),fast("MODEL_HAIR_FAST_RAYS","0"),dop("MODEL_HAIR_COMPONENT_DOP","0"),audit("MODEL_HAIR_RAY_AUDIT","1"),paired("MODEL_HAIR_PAIRED_RAYS","1");Expect(slowGrid.Initialize(p,{},1,&error),"Builds original grid Moller reference with fixed-ray caching disabled");}
        {ScopedOption grid("MODEL_HAIR_RAY_GRID","1"),fast("MODEL_HAIR_FAST_RAYS","2"),dop("MODEL_HAIR_COMPONENT_DOP","0"),audit("MODEL_HAIR_RAY_AUDIT","1"),paired("MODEL_HAIR_PAIRED_RAYS","1");Expect(optimized.Initialize(p,{},1,&error),"Builds projected CSR grids, cached original fixed-ray coefficients and conservative far-plane screening");}
        bool identical=true;std::uint32_t referenceHint=UINT32_MAX,optimizedHint=UINT32_MAX;
        for(int poseIndex=0;poseIndex<3;++poseIndex){auto pose=EvaluateSkeletalPose(p,nullptr,0,false);pose.local[0].translation={poseIndex==2?10000.:poseIndex*.07,poseIndex*.02,-poseIndex*.03};pose.local[0].rotation=glm::angleAxis(poseIndex*.43,glm::normalize(glm::dvec3(1,2,3)));pose.local[1].rotation=glm::angleAxis(poseIndex*.17,glm::dvec3(0,1,0));pose.local[2].translation={0,poseIndex*.03,0};pose.global=GlobalBoneTransforms(p.skeleton,pose.local);identical&=reference.Update(pose,&error)&&slowGrid.Update(pose,&error)&&optimized.Update(pose,&error);if(poseIndex==2){reference.ResetMotion();slowGrid.ResetMotion();optimized.ResetMotion();}
            for(int i=0;i<400;++i){glm::dvec3 local((i%25-12)*.014,std::sin(i*.37)*.16,std::cos(i*.23)*.16);if(i>=300)local.x+=.45;const auto point=glm::dvec3(pose.global[0]*glm::dvec4(local,1));const auto expected=reference.Nearest(point,std::numeric_limits<double>::infinity(),&referenceHint),slow=slowGrid.Nearest(point),actual=optimized.Nearest(point,std::numeric_limits<double>::infinity(),&optimizedHint);identical&=Same(expected,actual)&&Same(slow,actual)&&referenceHint==optimizedHint;
                if(i%19==0){const auto bounded=reference.Nearest(point,.03);identical&=Same(bounded,slowGrid.Nearest(point,.03))&&Same(bounded,optimized.Nearest(point,.03));const auto contact=reference.Contact(point,point,.0015,.06,false);identical&=Same(contact,slowGrid.Contact(point,point,.0015,.06,false))&&Same(contact,optimized.Contact(point,point,.0015,.06,false));}}
        }
        Expect(identical,"Projected grids retain every nearest/inside/contact bit for 1,200 moving open/nested/closed and large-coordinate queries");
        Expect(optimized.QueryStats().rayResultHash==reference.QueryStats().rayResultHash&&optimized.QueryStats().rayResultHash==slowGrid.QueryStats().rayResultHash,"Fast-rays-zero, original BVH and cached fixed-ray grid preserve each audited finite first-hit bit in reference order");
        Expect(optimized.QueryStats().rayGridQueries>0&&optimized.QueryStats().rayGridCandidateTests>0&&reference.QueryStats().rayGridQueries==0&&optimized.QueryStats().rayNodeTests<reference.QueryStats().rayNodeTests,"Projected grids visit real local-cell candidates and eliminate reference ray BVH node walks");
        Expect(optimized.QueryStats().fastRayTests>0&&slowGrid.QueryStats().fastRayTests==0,"Fixed-ray acceleration actually uses its cache while explicit zero retains the reference Moller path");
    }
    {
        auto p=Package();AddBox(p,{0,0,0},{.1,.1,.1});for(int i=0;i<21;++i)AddBox(p,{.4+i*.3,0,0},{.04,.04,.04});HairBodyCollision reference,optimized;
        {ScopedOption grid("MODEL_HAIR_RAY_GRID","0"),dop("MODEL_HAIR_COMPONENT_DOP","0"),audit("MODEL_HAIR_RAY_AUDIT","1"),paired("MODEL_HAIR_PAIRED_RAYS","1");reference.Initialize(p,{},1,&error);}
        {ScopedOption grid("MODEL_HAIR_RAY_GRID","1"),dop("MODEL_HAIR_COMPONENT_DOP","0"),audit("MODEL_HAIR_RAY_AUDIT","1"),paired("MODEL_HAIR_PAIRED_RAYS","1");optimized.Initialize(p,{},1,&error);}
        const auto direction=glm::normalize(glm::dvec3(1,.371,.529));bool identical=true;
        for(int sample=1;sample<=120;++sample){const auto point=glm::dvec3(.1)-direction*(sample*.001);for(int perturb=-1;perturb<=1;++perturb){auto probe=point;if(perturb)probe.x=std::nextafter(probe.x,perturb>0?std::numeric_limits<double>::infinity():-std::numeric_limits<double>::infinity());identical&=Same(reference.Nearest(probe),optimized.Nearest(probe));}}
        Expect(identical&&reference.QueryStats().rayResultHash==optimized.QueryStats().rayResultHash,"Projected grid padding and original triangle order preserve one-ULP corner/edge ray ties and cell boundaries");
        for(const auto& axis:std::array<glm::dvec3,3>{{{1,.371,.529},{.217,1,.413},{.319,.271,1}}}){auto pose=EvaluateSkeletalPose(p,nullptr,0,false);pose.local[0].translation=axis*1e12;pose.global=GlobalBoneTransforms(p.skeleton,pose.local);reference.Update(pose,&error);optimized.Update(pose,&error);
            for(int i=0;i<32;++i){const auto point=pose.local[0].translation+glm::dvec3(std::sin(i*.37)*.08,std::cos(i*.23)*.08,std::sin(i*.11)*.08);identical&=Same(reference.Nearest(point),optimized.Nearest(point));}}
        Expect(identical&&reference.QueryStats().rayResultHash==optimized.QueryStats().rayResultHash&&optimized.QueryStats().rayGridFallbacks>0,"Ill-conditioned projection after trillion-unit translation along every ray direction falls back without losing hit/tie bits");
    }
    {
        auto p=Package();AddSphere(p);for(int i=0;i<4;++i)AddBox(p,{.4+i*.3,0,0},{.04,.04,.04});HairBodyCollision reference;std::array<HairBodyCollision,3> grids;
        {ScopedOption grid("MODEL_HAIR_RAY_GRID","0"),dop("MODEL_HAIR_COMPONENT_DOP","0"),audit("MODEL_HAIR_RAY_AUDIT","1"),paired("MODEL_HAIR_PAIRED_RAYS","1");reference.Initialize(p,{},1,&error);}
        const std::array<const char*,3> sides{{"64","128","256"}};for(std::size_t i=0;i<grids.size();++i){ScopedOption grid("MODEL_HAIR_RAY_GRID","1"),side("MODEL_HAIR_RAY_GRID_SIDE",sides[i]),dop("MODEL_HAIR_COMPONENT_DOP","0"),audit("MODEL_HAIR_RAY_AUDIT","1"),paired("MODEL_HAIR_PAIRED_RAYS","1");grids[i].Initialize(p,{},1,&error);}
        bool identical=true;for(int i=0;i<240;++i){const glm::dvec3 point(std::sin(i*.31)*.12,std::cos(i*.23)*.12,std::sin(i*.17)*.12);const auto expected=reference.Nearest(point);for(auto& grid:grids)identical&=Same(expected,grid.Nearest(point));}
        bool allActive=true;for(auto& grid:grids){identical&=grid.QueryStats().rayResultHash==reference.QueryStats().rayResultHash;allActive&=grid.QueryStats().rayGridQueries>0;}
        Expect(identical&&allActive,"64, 128 and 256 projected-grid resolutions preserve every nearest and audited first-hit bit while using real grids");
    }
    {
        auto p=Package();AddBox(p,{0,0,0},{1,1,1});for(auto& vertex:p.skins[0].vertices)vertex.weights={0,0,0,0};p.skins[0].bindTransform=glm::translate(glm::dmat4(1),glm::dvec3(2,0,0));p.geometry->instances[0].transform=glm::translate(glm::dmat4(1),glm::dvec3(4,0,0));
        HairBodyCollision body;Expect(body.Initialize(p,{},1,&error)&&body.Nearest({5,0,0},.001).found,"Unweighted fallback vertices use the actual mesh instance transform");
        Expect(body.TriangleBoneWeight(0,0)==0,"Unweighted fallback geometry contributes zero bone weight");
    }
    {
        auto p=Package();AddBox(p,{0,0,0},{1,1,1});
        auto& skin=p.skins[0].vertices;skin[0].joints={1,1,2,999};skin[0].weights={.2f,.3f,.5f,2.f};skin[2].joints={1,0,0,0};skin[2].weights={.3f,.7f,0,0};skin[1].weights={0,0,0,0};
        HairBodyCollision body;Expect(body.Initialize(p,{},1,&error),"Builds a mixed-weight triangle with duplicate joints and an invalid source influence");
        const double sum0=double(.2f)+double(.3f)+double(.5f),sum2=double(.3f)+double(.7f);
        const double expected=((double(.2f)+double(.3f))/sum0+double(.3f)/sum2)/3.;
        Expect(std::abs(body.TriangleBoneWeight(0,1)-expected)<1e-15,"Triangle bone weight averages three actual vertex weights with effective normalization and duplicate sums");
        Expect(body.TriangleBoneWeight(UINT32_MAX,1)==0&&body.TriangleBoneWeight(0,UINT32_MAX)==0,"Invalid triangle or bone provenance queries safely return zero");
    }
    {
        auto p=Package();for(int i=0;i<512;++i)AddBox(p,{(i%32)*.4,0,(i/32)*.4},{.05,.05,.05});
        HairBodyCollision body;Expect(body.Initialize(p,{},1,&error),"Builds a many-component body with 6,144 actual triangles");body.ResetQueryStats();
        const auto nearest=body.Nearest({.051,0,0},.02);
        Expect(nearest.found&&std::abs(nearest.distance-.001)<1e-9,"Large-body BVH query retains exact nearest surface distance");
        Expect(body.QueryStats().triangleTests<128&&body.QueryStats().nodeTests<256,"Spatial query visits a small local subset of a 6,144-triangle body");
    }
    {
        auto p=Package();AddSphere(p);const auto shirt=AddBox(p,{0,0,0},{.12,.12,.12},0,"OpenShirt");auto& indices=p.geometry->meshes[shirt].indices;indices.erase(indices.begin()+24,indices.begin()+30);
        // More than 64 components also protects the mask's collision policy:
        // hash-bit collisions may cost visits but must never skip true geometry.
        for(int i=0;i<72;++i)AddBox(p,{.4+(i%12)*.3,0,(i/12)*.3},{.04,.04,.04});
        HairBodyCollision reference,pairedReference,optimized;
        {ScopedOption mask("MODEL_HAIR_COMPONENT_MASK","0"),hints("MODEL_HAIR_QUERY_HINT","0"),paired("MODEL_HAIR_PAIRED_RAYS","0"),audit("MODEL_HAIR_RAY_AUDIT","1"),bvh("MODEL_HAIR_COMPONENT_BVH","0"),dop("MODEL_HAIR_COMPONENT_DOP","0");Expect(reference.Initialize(p,{},1,&error),"Builds unchanged reference with component masks, hints, paired rays, component BVH and DOP disabled");}
        {ScopedOption mask("MODEL_HAIR_COMPONENT_MASK","1"),hints("MODEL_HAIR_QUERY_HINT","1"),paired("MODEL_HAIR_PAIRED_RAYS","0"),audit("MODEL_HAIR_RAY_AUDIT","1"),bvh("MODEL_HAIR_COMPONENT_BVH","0"),dop("MODEL_HAIR_COMPONENT_DOP","0");Expect(pairedReference.Initialize(p,{},1,&error),"Builds isolated separate-ray and linear-component reference with the same exact masks and hints");}
        {ScopedOption mask("MODEL_HAIR_COMPONENT_MASK","1"),hints("MODEL_HAIR_QUERY_HINT","1"),paired("MODEL_HAIR_PAIRED_RAYS","1"),audit("MODEL_HAIR_RAY_AUDIT","1"),bvh("MODEL_HAIR_COMPONENT_BVH",nullptr),dop("MODEL_HAIR_COMPONENT_DOP","0");Expect(optimized.Initialize(p,{},1,&error),"Builds exact component-mask, triangle-hint, shared-ray and default component-BVH path with original ray audit coverage");}
        bool identical=true;std::uint32_t hint=UINT32_MAX,surfaceHint=UINT32_MAX;
        for(int poseIndex=0;poseIndex<3;++poseIndex){auto pose=EvaluateSkeletalPose(p,nullptr,0,false);pose.local[0].translation={poseIndex*.03,poseIndex*.02,-poseIndex*.04};pose.local[0].rotation=glm::angleAxis(poseIndex*.4,glm::normalize(glm::dvec3(1,2,3)));pose.global=GlobalBoneTransforms(p.skeleton,pose.local);reference.Update(pose,&error);pairedReference.Update(pose,&error);optimized.Update(pose,&error);
            for(int i=0;i<400;++i){const glm::dvec3 point((i%25-12)*.012,std::sin(i*.37)*.13,std::cos(i*.23)*.13);const auto expected=reference.Nearest(point),separate=pairedReference.Nearest(point),actual=optimized.Nearest(point,std::numeric_limits<double>::infinity(),&hint);identical&=Same(expected,actual)&&Same(separate,actual);const auto surface=reference.NearestSurface(point,.03),fastSurface=optimized.NearestSurface(point,.03,&surfaceHint);identical&=Same(surface,fastSurface);
                if(i%41==0){const auto bounded=reference.Nearest(point,.03),boundedSeparate=pairedReference.Nearest(point,.03),boundedFast=optimized.Nearest(point,.03);identical&=Same(bounded,boundedSeparate)&&Same(bounded,boundedFast);const auto contact=reference.Contact(point,point,.0015,.06,false),contactSeparate=pairedReference.Contact(point,point,.0015,.06,false),contactFast=optimized.Contact(point,point,.0015,.06,false);identical&=Same(contact,contactSeparate)&&Same(contact,contactFast);}}
        }
        Expect(identical,"Default component BVH and BVH-zero preserve all nearest/inside, bounded-nearest and endpoint-contact result bits across moving poses");
        Expect(optimized.QueryStats().componentBvhNodeTests>0&&reference.QueryStats().componentBvhNodeTests==0&&optimized.QueryStats().componentBoundTests<reference.QueryStats().componentBoundTests,"Inside component BVH reduces exact component-bound checks without changing ray votes");
        Expect(optimized.QueryStats().componentNodeRejects>0&&optimized.QueryStats().nearestHintTests>0,"Exact optimization actually rejects component-free branches and uses verified triangle bounds");
        Expect(optimized.QueryStats().nodeTests<reference.QueryStats().nodeTests,"Exact hints and component pruning reduce nearest BVH node work");
        Expect(optimized.QueryStats().rayResultHash==reference.QueryStats().rayResultHash&&optimized.QueryStats().rayResultHash==pairedReference.QueryStats().rayResultHash,"Shared ray traversal preserves every first-hit distance, facing and component bit for bit in audited order");
        Expect(optimized.QueryStats().sharedRayTraversals>0&&pairedReference.QueryStats().sharedRayTraversals==0&&optimized.QueryStats().rayNodeTests<pairedReference.QueryStats().rayNodeTests,"Shared bidirectional rays reduce actual ray BVH node visits without reducing directions or votes");
    }
    {
        auto p=Package();AddBox(p,{0,0,0},{.1,.1,.1});HairBodyCollision separate,paired;
        {ScopedOption mode("MODEL_HAIR_PAIRED_RAYS","0"),audit("MODEL_HAIR_RAY_AUDIT","1");separate.Initialize(p,{},1,&error);}
        {ScopedOption mode("MODEL_HAIR_PAIRED_RAYS","1"),audit("MODEL_HAIR_RAY_AUDIT","1");paired.Initialize(p,{},1,&error);}
        const auto direction=glm::normalize(glm::dvec3(1,.371,.529));bool identical=true;
        for(int sample=1;sample<=120;++sample){const auto corner=glm::dvec3(.1);auto point=corner-direction*(sample*.001);
            for(int perturb=-1;perturb<=1;++perturb){auto probe=point;if(perturb)probe.x=std::nextafter(probe.x,perturb>0?std::numeric_limits<double>::infinity():-std::numeric_limits<double>::infinity());identical&=Same(separate.Nearest(probe),paired.Nearest(probe));}}
        Expect(identical&&separate.QueryStats().rayResultHash==paired.QueryStats().rayResultHash,"Shared rays retain reference edge/corner nearest-hit tie order including one-ULP ray-origin perturbations");
    }
    OptionalRealComparison(argc,argv);std::printf("Hair body collision: %d failure(s)\n",failures);return failures?1:0;
}
