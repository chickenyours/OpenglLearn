#include "Model/Public/cloth_asset.h"
#include <json/json.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace {
namespace fs=std::filesystem;
int failures=0;
bool Check(bool v,const char* label){std::printf("%-80s %s\n",label,v?"ok":"FAIL");failures+=!v;return v;}
Json::Value Read(const fs::path& p){std::ifstream s(p);Json::Value r;s>>r;return r;}
void Write(const fs::path& p,const Json::Value& r){std::ofstream s(p);s<<r;}
Model::SkeletalAssetPackage Mesh(const std::string& id) {
    using namespace Model;SkeletalAssetPackage a;a.id=id;a.name=id;a.sourceFormat="fixture";a.sourcePath="nonexistent-source.pmx";a.skeleton.id="cloth-skeleton";a.skeleton.name="Skeleton";
    SkeletonBone root;root.name="Hips";a.skeleton.bones={root};a.ikRig.id="fixture-ik";a.ikRig.name="IK";a.ikRig.skeletonId=a.skeleton.id;
    a.geometry=std::make_shared<StaticModel>();StaticMesh m;m.name="Panel";
    for(auto p:{glm::dvec3(-.5,1,0),glm::dvec3(.5,1,0),glm::dvec3(-.5,0,0),glm::dvec3(.5,0,0)}){StaticVertex v;v.position=p;m.vertices.push_back(v);m.bounds.Include(p);}
    m.indices={0,2,1,1,2,3};a.geometry->bounds=m.bounds;a.geometry->meshes.push_back(m);a.geometry->instances.push_back({"Panel",0,glm::dmat4(1)});a.geometry->materials.push_back(PbrMaterial{});
    MeshSkin skin;skin.vertices.resize(4);a.skins.push_back(skin);MorphTarget morph;morph.name="Blink";morph.englishName="blink";a.morphs.push_back(morph);return a;
}
Model::ClothAsset Cloth(const Model::SkeletalAssetPackage& g) {
    using namespace Model;ClothAsset c;c.id="cloth-panel";c.name="Panel cloth";c.skeletonId=g.skeleton.id;c.garmentId=g.id;c.topologyHash=ClothTopologyHash(g);
    for(std::size_t i=0;i<4;++i)c.particles.push_back({g.geometry->meshes[0].vertices[i].position,i<2?0.0:1.0,i<2?0.0:.7,i<2?1.0:.2});
    c.triangles={{0,2,1},{1,2,3}};
    for(auto pair:{std::array<std::uint32_t,2>{0,1},{0,2},{1,2},{1,3},{2,3}})c.stretch.push_back({pair[0],pair[1],glm::length(c.particles[pair[0]].restPosition-c.particles[pair[1]].restPosition)});
    c.bend.push_back({0,3,std::sqrt(2.0)});c.tethers={{2,0,1},{3,1,1}};for(std::uint32_t i=0;i<4;++i)c.renderBindings.push_back({0,i,i});
    c.capsules.push_back({"Hips",0,0,{0,.6,0},{0,1,0},.2});c.diagnostics={"Generated cloth fixture."};
    ClothConvexCollider hull;hull.name="Bone-local skin proxy";hull.bone=0;hull.referenceValid=true;hull.vertices={{-.1,.4,-.1},{.1,.4,-.1},{0,.4,.1},{0,.6,0}};
    const auto center=(hull.vertices[0]+hull.vertices[1]+hull.vertices[2]+hull.vertices[3])*.25;
    for(auto t:{std::array<std::uint32_t,3>{0,1,2},{0,3,1},{1,3,2},{2,3,0}}){const auto a=hull.vertices[t[0]];auto normal=glm::normalize(glm::cross(hull.vertices[t[1]]-a,hull.vertices[t[2]]-a));if(glm::dot(normal,center-a)>0){std::swap(t[1],t[2]);normal=-normal;}hull.triangles.push_back(t);hull.planes.push_back({normal,glm::dot(normal,a)});}hull.planes.back().collision=false;hull.vertices.push_back(center);for(const auto& vertex:hull.vertices)hull.skin.push_back({{0,vertex,1}});c.convexes.push_back(std::move(hull));return c;
}
}
int main() {
    const auto root=fs::path(MODEL_SOURCE_DIR)/"build/cloth_io_fixtures"/("run-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        auto garment=Mesh("garment"),body=Mesh("body");auto c=Cloth(garment);std::string e;c.motionBone=0;c.settings.areaCompliance=2e-7;c.settings.localDamping=1.7;c.settings.animationDamping=.3;c.capsules[0].referenceValid=true;c.capsules[0].restA={0,.6,0};c.capsules[0].restB={0,1,0};
        auto fullHashFixture=Mesh("complete-skin-hash");
        Model::SkeletonBone second;second.name="Second";fullHashFixture.skeleton.bones.push_back(second);
        auto& fullSkin=fullHashFixture.skins[0];fullSkin.influenceOffsets={0};
        for(std::size_t vertex=0;vertex<fullSkin.vertices.size();++vertex){fullSkin.influences.push_back({0,.6f});fullSkin.influences.push_back({1,.4f});fullSkin.influenceOffsets.push_back(std::uint32_t(fullSkin.influences.size()));}
        const auto completeHash=Model::ClothTopologyHash(fullHashFixture);
        fullSkin.influences[0].weight=.5f;fullSkin.influences[1].weight=.5f;
        Check(Model::ClothTopologyHash(fullHashFixture)!=completeHash,"Physical binding hash includes complete weights beyond the compatibility summary");
        Check(Model::ValidateClothAsset(c,garment,&e),"Validate anchored triangle cloth and complete render bindings");if(!e.empty())std::printf("%s\n",e.c_str());
        Check(Model::GarmentCompatible(body,garment,&e),"Independent garment geometry accepts an identical skeleton and morph layout");
        auto changed=garment;changed.skeleton.bones[0].name="Different";Check(!Model::GarmentCompatible(body,changed,&e),"Reject same-ID replacement with renamed bone");
        changed=garment;changed.skeleton.bones[0].restLocal.translation.x=.01;Check(!Model::GarmentCompatible(body,changed,&e),"Reject same-ID replacement with changed rest pose");
        changed=garment;changed.morphs[0].sourceType=3;Check(!Model::GarmentCompatible(body,changed,&e),"Reject same-count replacement with incompatible morph types");
        changed=garment;changed.skeleton.id="other-rig";changed.ikRig.skeletonId=changed.skeleton.id;Check(!Model::GarmentCompatible(body,changed,&e),"Never silently map a different skeleton ID");
        Check(Model::SaveClothAsset(c,garment,root/"Standalone.cloth.json",&e),"Save and verify independent cloth JSON before publish");if(!e.empty())std::printf("%s\n",e.c_str());
        auto loaded=Model::LoadClothAsset(root/"Standalone.cloth.json",garment,&e);
        Check(loaded&&loaded->particles.size()==4&&loaded->bend.size()==1&&loaded->tethers.size()==2&&loaded->capsules[0].radius==.2&&loaded->settings.substeps==3,"Cloth settings, masks, constraints, bindings and collision shapes roundtrip");
        Check(loaded&&loaded->version==3&&loaded->motionBone==0&&loaded->settings.areaCompliance==c.settings.areaCompliance&&loaded->settings.localDamping==1.7&&loaded->settings.animationDamping==.3,"Version 3 preserves the rigid motion frame and angular/area settings");
        Check(loaded&&loaded->convexes.size()==1&&loaded->convexes[0].name==c.convexes[0].name&&loaded->convexes[0].bone==0&&loaded->convexes[0].referenceValid&&loaded->convexes[0].vertices==c.convexes[0].vertices&&loaded->convexes[0].triangles==c.convexes[0].triangles&&loaded->convexes[0].planes.size()==4&&loaded->convexes[0].planes[0].normal==c.convexes[0].planes[0].normal&&loaded->convexes[0].planes[0].distance==c.convexes[0].planes[0].distance,"Bone-local convex vertices, ordered faces, planes and bind reference roundtrip");
        Check(loaded&&loaded->convexes[0].planes[0].collision&&!loaded->convexes[0].planes.back().collision,"Artificial split caps retain membership while their disabled push flag roundtrips");
        Check(loaded&&loaded->convexes[0].skin.size()==5&&loaded->convexes[0].skin.back().size()==1&&loaded->convexes[0].skin.back()[0].bone==0&&loaded->convexes[0].skin.back()[0].weightedPosition==c.convexes[0].vertices.back()&&loaded->convexes[0].skin.back()[0].weight==1,"All skin support points, including rest-interior points unused by faces, roundtrip");
        auto flagless=Read(root/"Standalone.cloth.json");for(auto& plane:flagless["convexes"][0]["planes"])plane.removeMember("collision");Write(root/"FlaglessV3.cloth.json",flagless);const auto flaglessLoaded=Model::LoadClothAsset(root/"FlaglessV3.cloth.json",garment,&e);
        Check(flaglessLoaded&&std::all_of(flaglessLoaded->convexes[0].planes.begin(),flaglessLoaded->convexes[0].planes.end(),[](const auto& plane){return plane.collision;}),"Missing optional collision flags default to physical faces for older version 3 writers");
        auto rigid=Read(root/"Standalone.cloth.json");rigid["convexes"][0].removeMember("skin");rigid["convexes"][0]["vertices"].resize(4);Write(root/"RigidV3.cloth.json",rigid);const auto rigidLoaded=Model::LoadClothAsset(root/"RigidV3.cloth.json",garment,&e);
        Check(rigidLoaded&&rigidLoaded->convexes[0].skin.empty(),"Version 3 shapes without skin metadata retain rigid bone binding");
        Check(loaded&&loaded->settings.convergenceTolerance==c.settings.convergenceTolerance,"Convergence tolerance roundtrips in SI meters");
        Check(loaded&&loaded->capsules[0].referenceValid&&loaded->capsules[0].restA==c.capsules[0].restA&&loaded->capsules[0].restB==c.capsules[0].restB,"Neutral collision endpoints needed for posed-motion startup roundtrip");
        auto version2=Read(root/"Standalone.cloth.json");version2["version"]=2;version2.removeMember("convexes");Write(root/"Version2.cloth.json",version2);const auto upgraded2=Model::LoadClothAsset(root/"Version2.cloth.json",garment,&e);
        Check(upgraded2&&upgraded2->version==3&&upgraded2->convexes.empty()&&upgraded2->capsules.size()==1&&upgraded2->settings.areaCompliance==c.settings.areaCompliance,"Version 2 capsule assets upgrade without silently adding skin collision shapes");
        auto legacy=version2;legacy["version"]=1;legacy.removeMember("motionBone");legacy.removeMember("motionInverseBind");legacy["settings"].removeMember("areaCompliance");legacy["settings"].removeMember("localDamping");legacy["settings"].removeMember("animationDamping");Write(root/"Legacy.cloth.json",legacy);
        auto legacyNoConvergence=Read(root/"Legacy.cloth.json");legacyNoConvergence["settings"].removeMember("convergenceTolerance");Write(root/"Legacy.cloth.json",legacyNoConvergence);
        const auto upgraded=Model::LoadClothAsset(root/"Legacy.cloth.json",garment,&e);Check(upgraded&&upgraded->version==3&&upgraded->convexes.empty()&&upgraded->motionBone==-1&&upgraded->settings.bendCompliance==100&&upgraded->settings.convergenceTolerance==0,"Version 1 cloth upgrades with legacy goal mode and converted bending defaults");
        Check(!Model::SaveClothAsset(c,garment,root/"Standalone.cloth.json",&e),"Refuse to overwrite an existing cloth asset");
        changed=garment;changed.geometry=std::make_shared<Model::StaticModel>(*garment.geometry);changed.geometry->meshes[0].vertices[0].position.x+=.001;
        Check(!Model::LoadClothAsset(root/"Standalone.cloth.json",changed,&e),"Reject cloth attached to same-count geometry with different positions");
        changed=garment;changed.geometry=std::make_shared<Model::StaticModel>(*garment.geometry);std::swap(changed.geometry->meshes[0].indices[0],changed.geometry->meshes[0].indices[1]);
        Check(!Model::ValidateClothAsset(c,changed,&e),"Topology fingerprint detects triangle index order changes");
        changed=garment;changed.skins[0].vertices[2].weights={.5,.5,0,0};Check(!Model::ValidateClothAsset(c,changed,&e),"Topology fingerprint detects changed skin weights");
        auto bad=c;bad.particles[0].inverseMass=1;bad.particles[1].inverseMass=1;Check(!Model::ValidateClothAsset(bad,garment,&e),"Reject unconstrained free-floating cloth component");
        bad=c;bad.renderBindings.push_back(bad.renderBindings.back());Check(!Model::ValidateClothAsset(bad,garment,&e),"Reject duplicate/incomplete render bindings");
        bad=c;bad.particles[2].restPosition.x=std::numeric_limits<double>::quiet_NaN();Check(!Model::ValidateClothAsset(bad,garment,&e),"Reject non-finite particle data");
        bad=c;bad.capsules[0].boneA=9;Check(!Model::ValidateClothAsset(bad,garment,&e),"Reject collision capsule with absent skeleton bone");
        bad=c;bad.convexes[0].bone=9;Check(!Model::ValidateClothAsset(bad,garment,&e),"Reject convex proxy with an absent skeleton bone");
        bad=c;bad.convexes.resize(32,c.convexes[0]);Check(Model::ValidateClothAsset(bad,garment,&e),"The thirty-two-shape collision budget accepts its exact boundary");bad.convexes.push_back(c.convexes[0]);Check(!Model::ValidateClothAsset(bad,garment,&e),"Reject more than thirty-two convex collision shapes");
        bad=c;bad.convexes[0].vertices.resize(1025,{0,0,0});Check(!Model::ValidateClothAsset(bad,garment,&e),"Reject convex support-cloud vertex-budget overflow");
        bad=c;bad.convexes[0].skin.clear();Check(!Model::ValidateClothAsset(bad,garment,&e),"Rigid legacy shapes still reject unreferenced interior support points");
        bad=c;bad.convexes[0].skin.pop_back();Check(!Model::ValidateClothAsset(bad,garment,&e),"Reject incomplete convex skin support bindings");
        bad=c;bad.convexes[0].skin[0][0].weight=.5;Check(!Model::ValidateClothAsset(bad,garment,&e),"Reject nonnormalized convex skin weights");
        bad=c;bad.convexes[0].skin[0][0].weightedPosition.x+=.02;Check(!Model::ValidateClothAsset(bad,garment,&e),"Reject skin contributions which do not reconstruct their neutral support point");
        bad=c;bad.convexes[0].skin[0][0].bone=99;Check(!Model::ValidateClothAsset(bad,garment,&e),"Reject convex skin references to an absent bone");
        bad=c;bad.convexes[0].skin[0].resize(17,bad.convexes[0].skin[0][0]);Check(!Model::ValidateClothAsset(bad,garment,&e),"Reject excessive convex support influences before runtime use");
        bad=c;bad.convexes[0].vertices[0].x=std::numeric_limits<double>::quiet_NaN();Check(!Model::ValidateClothAsset(bad,garment,&e),"Reject nonfinite convex source points");
        bad=c;bad.convexes[0].planes[0].normal*=2;Check(!Model::ValidateClothAsset(bad,garment,&e),"Reject unnormalized convex supporting planes");
        bad=c;for(auto& plane:bad.convexes[0].planes)plane.collision=false;Check(!Model::ValidateClothAsset(bad,garment,&e),"Reject a convex body with no physical exterior collision face");
        bad=c;bad.convexes[0].planes[0].distance-=.01;Check(!Model::ValidateClothAsset(bad,garment,&e),"Reject planes which disagree with the collision surface");
        bad=c;bad.convexes[0].triangles.pop_back();bad.convexes[0].planes.pop_back();Check(!Model::ValidateClothAsset(bad,garment,&e),"Reject open convex collision surfaces");
        bad=c;std::swap(bad.convexes[0].triangles[0][1],bad.convexes[0].triangles[0][2]);Check(!Model::ValidateClothAsset(bad,garment,&e),"Reject inward convex surface winding");
        bad=c;bad.convexes[0].referenceTransform[3].x=.02;Check(!Model::ValidateClothAsset(bad,garment,&e),"Reject convex neutral transforms from a different bind pose");
        bad=c;bad.settings.iterations=100000;Check(!Model::ValidateClothAsset(bad,garment,&e),"Reject solver resource-budget overflow");
        bad=c;bad.motionBone=9;Check(!Model::ValidateClothAsset(bad,garment,&e),"Reject missing rigid motion reference bone");
        bad=c;bad.motionInverseBind[3].x=.01;Check(!Model::ValidateClothAsset(bad,garment,&e),"Reject rigid motion reference with wrong skeleton bind pose");
        bad=c;bad.settings.localDamping=-1;Check(!Model::ValidateClothAsset(bad,garment,&e),"Reject negative local deformation damping");
        bad=c;bad.settings.convergenceTolerance=-.001;Check(!Model::ValidateClothAsset(bad,garment,&e),"Reject negative convergence tolerance");
        bad=c;bad.settings.convergenceTolerance=std::numeric_limits<double>::infinity();Check(!Model::ValidateClothAsset(bad,garment,&e),"Reject nonfinite convergence tolerance");
        bad=c;bad.tethers[0].anchor=3;Check(!Model::ValidateClothAsset(bad,garment,&e),"Reject tether to a simulated rather than pinned vertex");
        bad=c;bad.stretch.push_back(bad.stretch[0]);Check(!Model::ValidateClothAsset(bad,garment,&e),"Reject duplicated stretch constraints");
        bad=c;bad.triangles[0][1]=4;Check(!Model::ValidateClothAsset(bad,garment,&e),"Reject out-of-range simulation triangle");
        Model::ModularCharacterBundle bundle;bundle.body=std::make_shared<Model::SkeletalAssetPackage>(body);bundle.garment=std::make_shared<Model::SkeletalAssetPackage>(garment);bundle.cloth=c;
        bundle.assembly.id="character";bundle.assembly.name="IndependentParts";bundle.assembly.skeletonId=body.skeleton.id;bundle.assembly.slots.push_back({"skirt",garment.id,{}, {},false});bundle.diagnostics={"fixture"};
        const auto directory=root/"Character";Check(Model::SaveModularCharacter(bundle,directory,&e),"Publish verified body, garment and cloth in one modular assembly");if(!e.empty())std::printf("%s\n",e.c_str());
        auto round=Model::LoadModularCharacter(directory,&e);if(!Check(bool(round),"Load standalone modular character without source files"))throw std::runtime_error(e);
        Check(fs::exists(directory/"Body/Package.asset.json")&&fs::exists(directory/"Garments/Skirt/Package.asset.json")&&fs::exists(directory/"Cloth/Skirt.cloth.json"),"Body and skirt are separate reusable packages, not display-only filters");
        Check(!round->assembly.slots[0].enabled&&round->body->id==body.id&&round->garment->id==garment.id,"Hidden slot state and independent asset IDs roundtrip");
        Check(Model::LoadSkeletalAssetPackage(directory/"Garments/Skirt",&e)!=nullptr,"Garment package loads independently from the character assembly");
        Check(!Model::SaveModularCharacter(bundle,directory,&e),"Reject clobber of existing modular assembly");
        const auto manifest=directory/"Character.asset.json";const auto original=Read(manifest);auto json=original;json["slots"].append(original["slots"][0]);Write(manifest,json);
        Check(!Model::LoadModularCharacter(directory,&e),"Reject unsupported multiple slots instead of ignoring extra garments");Write(manifest,original);
        json=original;json["slots"][0]["package"]="../outside/Package.asset.json";Write(manifest,json);Check(!Model::LoadModularCharacter(directory,&e),"Reject garment reference escaping the modular package");Write(manifest,original);
        json=original;json["bodyPackage"]=fs::absolute(directory/"Body/Package.asset.json").generic_string();Write(manifest,json);Check(!Model::LoadModularCharacter(directory,&e),"Reject absolute package references");Write(manifest,original);
        const auto clothFile=directory/"Cloth/Skirt.cloth.json";const auto clothJson=Read(clothFile);json=clothJson;json["version"]=99;Write(clothFile,json);Check(!Model::LoadModularCharacter(directory,&e),"Reject unsupported cloth version through assembly load");Write(clothFile,clothJson);
        json=clothJson;json["particles"][2]["inverseMass"]=-1;Write(clothFile,json);Check(!Model::LoadClothAsset(clothFile,garment,&e),"Reject damaged solver data before allocation/runtime use");Write(clothFile,clothJson);
    }catch(const std::exception& x){std::printf("Exception: %s\n",x.what());++failures;}
    std::printf("Fixtures retained: %s\n%d failures\n",root.string().c_str(),failures);return failures?1:0;
}
