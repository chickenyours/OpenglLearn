#include "Model/Public/cloth_asset.h"
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <queue>
#include <set>
#include <tuple>

namespace {
using namespace Model;
int failures=0;
bool Expect(bool condition,const char* description){std::printf("  %-88s %s\n",description,condition?"ok":"FAIL");failures+=!condition;return condition;}
bool Diagnostic(const ClothAsset& cloth,const std::string& text){return std::any_of(cloth.diagnostics.begin(),cloth.diagnostics.end(),[&](const auto& line){return line.find(text)!=std::string::npos;});}
SkeletalAssetPackage Fixture(){
    SkeletalAssetPackage source;source.id="garment-fixture";source.name="Authored dress";source.skeleton.id="same-skeleton";
    SkeletonBone root;root.name="root";source.skeleton.bones.push_back(root);
    SkeletonBone hips;hips.name="hips";hips.parent=0;hips.restLocal.translation={0,1,0};source.skeleton.bones.push_back(hips);
    SkeletonBone skirt;skirt.name="SkirtFront";skirt.parent=1;skirt.restLocal.translation={0,-.05,.2};source.skeleton.bones.push_back(skirt);
    const auto global=GlobalBoneTransforms(source.skeleton,RestPose(source.skeleton));for(std::size_t i=0;i<global.size();++i)source.skeleton.bones[i].inverseBind=glm::inverse(global[i]);
    source.ikRig.skeletonId=source.skeleton.id;source.ikRig.humanoid.push_back({"hips",1,1,"fixture"});
    source.geometry=std::make_shared<StaticModel>();source.geometry->materials.resize(2);source.geometry->materials[1].name="Shared bodice and skirt material";source.geometry->materials[1].textures[0]=1;
    TextureImage pixel;pixel.name="unused";pixel.width=pixel.height=1;pixel.rgba={255,255,255,255};source.geometry->textures.push_back(pixel);pixel.name="dress texture";source.geometry->textures.push_back(pixel);
    StaticMesh mesh;mesh.name="Dress";mesh.materialIndex=1;MeshSkin skin;
    auto vertex=[&](glm::dvec3 position,int bone,std::uint32_t sourceId){StaticVertex v;v.position=position;mesh.vertices.push_back(v);mesh.bounds.Include(position);source.geometry->bounds.Include(position);VertexSkin weights;weights.joints={static_cast<std::uint32_t>(bone),0,0,0};skin.vertices.push_back(weights);skin.sourceVertexIds.push_back(sourceId);};
    // A 3 x 3 skirt grid. The highest row follows the hips and has no skirt
    // weights. It must still be extracted along with the seeded lower rows.
    for(int row=0;row<3;++row)for(int x=0;x<3;++x)vertex({(x-1)*.4,.4+row*.4,.2},row==2?1:2,static_cast<std::uint32_t>(row*3+x));
    for(std::uint32_t row=0;row<2;++row)for(std::uint32_t x=0;x<2;++x){const auto a=row*3+x;mesh.indices.insert(mesh.indices.end(),{a,a+1,a+4,a,a+4,a+3});}
    // A render seam duplicates one skirt corner. Physics must weld it without
    // removing either render vertex or its individual UV/normal attributes.
    vertex(mesh.vertices[0].position,2,0);mesh.vertices.back().uv={1,1};mesh.indices[0]=9;
    // The coincident seam of the bodice remains an authored, disconnected
    // component and must not be merged into the skirt during extraction.
    vertex(mesh.vertices[6].position,1,10);vertex({-.4,2,.2},1,11);vertex({.4,2,.2},1,12);mesh.indices.insert(mesh.indices.end(),{10,11,12});
    source.geometry->meshes.push_back(mesh);source.geometry->instances.push_back({"dress",0,glm::dmat4(1)});source.skins.push_back(skin);
    MorphTarget morph;morph.name="Skirt and torso adjustment";morph.deltas={{0,0,{.01,0,0},{0,0,0}},{0,9,{.01,0,0},{0,0,0}},{0,11,{.02,0,0},{0,0,0}}};source.morphs.push_back(morph);
    AnimationSequenceAsset sequence;sequence.id="fixture-animation";sequence.skeletonId=source.skeleton.id;sequence.name="Walk";sequence.duration=1;source.animations.push_back(sequence);
    return source;
}
SkeletalAssetPackage TubeFixture(bool smallOpening=false){
    auto source=Fixture();auto& mesh=source.geometry->meshes[0];auto& skin=source.skins[0];
    mesh.vertices.clear();mesh.indices.clear();mesh.bounds={};skin.vertices.clear();skin.sourceVertexIds.clear();source.geometry->bounds={};
    for(auto& morph:source.morphs)morph.deltas.clear();
    auto vertex=[&](glm::dvec3 position,int bone){StaticVertex v;v.position=position;mesh.vertices.push_back(v);mesh.bounds.Include(position);source.geometry->bounds.Include(position);VertexSkin weights;weights.joints={static_cast<std::uint32_t>(bone),0,0,0};skin.vertices.push_back(weights);skin.sourceVertexIds.push_back(static_cast<std::uint32_t>(mesh.vertices.size()-1));};
    // Keep a seeded row close to the unweighted attachment row. A long
    // unweighted upper tube deliberately triggers the separate continuous-dress
    // bodice partition heuristic, which is not the mask detector under test.
    for(int row=0;row<3;++row){const double y=row==0?.4:row==1?1.5:1.6;const double radius=row==0?.7:row==1?.55:smallOpening?.03:.4;
        for(int i=0;i<8;++i){const double angle=i*glm::two_pi<double>()/8;vertex({radius*std::cos(angle),y,radius*std::sin(angle)},row==2?1:2);}}
    for(std::uint32_t row=0;row<2;++row)for(std::uint32_t i=0;i<8;++i){const auto a=row*8+i,b=row*8+(i+1)%8;mesh.indices.insert(mesh.indices.end(),{a,b,a+8,b,b+8,a+8});}
    vertex(mesh.vertices[16].position,1);vertex({-.3,2,0},1);vertex({.3,2,0},1);mesh.indices.insert(mesh.indices.end(),{24,25,26});
    return source;
}
SkeletalAssetPackage LimbFixture(bool pollution){
    auto source=Fixture();source.geometry->materials[0].name="Body_SKIN";
    auto bone=[&](const char* name,int parent,glm::dvec3 translation){SkeletonBone b;b.name=name;b.parent=parent;b.restLocal.translation=translation;source.skeleton.bones.push_back(b);};
    bone("LeftUpperLeg",1,{.15,-.1,0});bone("LeftLowerLeg",3,{0,-.4,0});bone("LeftFoot",4,{0,-.4,0});bone("LeftToes",5,{0,-.05,.1});
    for(const auto& [role,index]:std::vector<std::pair<std::string,int>>{{"leftUpperLeg",3},{"leftLowerLeg",4},{"leftFoot",5},{"leftToes",6}})source.ikRig.humanoid.push_back({role,index,1,"fixture"});
    const auto global=GlobalBoneTransforms(source.skeleton,RestPose(source.skeleton));for(std::size_t i=0;i<global.size();++i)source.skeleton.bones[i].inverseBind=glm::inverse(global[i]);
    auto limb=[&](int joint,bool oversized){StaticMesh mesh;mesh.name=oversized?"Separate leggings":"Tapered flattened body SKIN";mesh.materialIndex=oversized?1:0;MeshSkin skin;
        const auto start=glm::dvec3(global[joint][3]);constexpr std::uint32_t around=12,rows=9;
        for(std::uint32_t row=0;row<rows;++row){const double t=double(row)/(rows-1),width=(oversized?.28:.07)*(1-.45*t),depth=(oversized?.28:.09)*(1-.45*t);
            for(std::uint32_t i=0;i<around;++i){const double angle=i*glm::two_pi<double>()/around;StaticVertex vertex;vertex.position=start+glm::dvec3(width*std::cos(angle),-.4*t,depth*std::sin(angle));mesh.vertices.push_back(vertex);mesh.bounds.Include(vertex.position);source.geometry->bounds.Include(vertex.position);VertexSkin weights;weights.joints={std::uint32_t(joint),std::uint32_t(joint+1),0,0};const float blend=float(.65+.3*(1-t));weights.weights={blend,1-blend,0,0};skin.vertices.push_back(weights);skin.sourceVertexIds.push_back(1000+std::uint32_t(source.geometry->meshes.size()*1000)+std::uint32_t(skin.vertices.size()));}}
        for(std::uint32_t row=0;row+1<rows;++row)for(std::uint32_t i=0;i<around;++i){const auto a=row*around+i,b=row*around+(i+1)%around;mesh.indices.insert(mesh.indices.end(),{a,b,a+around,b,b+around,a+around});}
        for(std::uint32_t i=1;i+1<around;++i){mesh.indices.insert(mesh.indices.end(),{0,i+1,i});const auto offset=(rows-1)*around;mesh.indices.insert(mesh.indices.end(),{offset,offset+i,offset+i+1});}
        source.geometry->instances.push_back({mesh.name,std::uint32_t(source.geometry->meshes.size()),glm::dmat4(1)});source.geometry->meshes.push_back(std::move(mesh));source.skins.push_back(std::move(skin));};
    limb(3,false);limb(4,false);if(pollution)limb(3,true);return source;
}
using Triangle=std::tuple<std::string,std::uint32_t,std::uint32_t,std::uint32_t>;
std::multiset<Triangle> Triangles(const SkeletalAssetPackage& asset){
    std::multiset<Triangle> result;
    for(std::size_t mi=0;mi<asset.geometry->meshes.size();++mi){const auto& mesh=asset.geometry->meshes[mi];const auto& ids=asset.skins[mi].sourceVertexIds;
        for(std::size_t t=0;t<mesh.indices.size();t+=3){const auto id=[&](std::uint32_t vi){return ids.empty()?vi:ids[vi];};result.emplace(asset.geometry->materials[mesh.materialIndex].name,id(mesh.indices[t]),id(mesh.indices[t+1]),id(mesh.indices[t+2]));}}
    return result;
}
void Conservation(const SkeletalAssetPackage& source,const ModularCharacterBundle& bundle){
    auto parts=Triangles(*bundle.body);const auto skirt=Triangles(*bundle.garment);parts.insert(skirt.begin(),skirt.end());
    Expect(parts==Triangles(source),"Body plus garment exactly preserves every source triangle and winding");
    Expect(bundle.body->skeleton.id==source.skeleton.id&&bundle.garment->skeleton.id==source.skeleton.id,"Both parts keep the source skeleton identity");
    Expect(bundle.body->skeleton.bones.size()==source.skeleton.bones.size()&&bundle.garment->skeleton.bones.size()==source.skeleton.bones.size(),"Both parts retain the full source bone order");
    bool names=bundle.body->morphs.size()==source.morphs.size()&&bundle.garment->morphs.size()==source.morphs.size();
    if(names)for(std::size_t i=0;i<source.morphs.size();++i)names&=source.morphs[i].name==bundle.body->morphs[i].name&&source.morphs[i].name==bundle.garment->morphs[i].name;
    Expect(names,"Morph layout is preserved so source facial animation remains compatible");
    Expect(bundle.body->animations.size()==source.animations.size()&&bundle.garment->animations.empty(),"Animations live on the body and the garment uses the shared pose");
    std::string error;Expect(GarmentCompatible(*bundle.body,*bundle.garment,&error),"The extracted garment passes skeleton and morph compatibility checks");
}
void ConstraintQuality(const ModularCharacterBundle& bundle){
    const auto& cloth=bundle.cloth;std::string error;
    Expect(ValidateClothAsset(cloth,*bundle.garment,&error),"Generated bindings, simulation topology and constraints validate");if(!error.empty())std::printf("    %s\n",error.c_str());
    Expect(cloth.renderBindings.size()==bundle.garment->geometry->VertexCount(),"Every garment render vertex has a simulation binding");
    Expect(!cloth.stretch.empty()&&!cloth.bend.empty()&&!cloth.tethers.empty(),"Stretch, bending and geodesic tether constraints are generated");
    std::vector<std::vector<std::uint32_t>> adjacency(cloth.particles.size());for(const auto& edge:cloth.stretch){adjacency[edge.a].push_back(edge.b);adjacency[edge.b].push_back(edge.a);}
    std::vector<bool> anchored(cloth.particles.size(),false);std::queue<std::uint32_t> pending;std::size_t pinned=0;
    for(std::uint32_t i=0;i<cloth.particles.size();++i)if(cloth.particles[i].inverseMass==0){anchored[i]=true;pending.push(i);++pinned;}
    while(!pending.empty()){const auto v=pending.front();pending.pop();for(auto n:adjacency[v])if(!anchored[n]){anchored[n]=true;pending.push(n);}}
    Expect(pinned>0&&pinned<cloth.particles.size()&&std::find(anchored.begin(),anchored.end(),false)==anchored.end(),"Every cloth component is anchored and still has free particles");
    std::set<std::uint32_t> tethered;bool tetherLengths=true;for(const auto& tether:cloth.tethers){tethered.insert(tether.particle);tetherLengths&=cloth.particles[tether.anchor].inverseMass==0&&tether.restLength+1e-8>=glm::length(cloth.particles[tether.particle].restPosition-cloth.particles[tether.anchor].restPosition);}
    Expect(tethered.size()==cloth.particles.size()-pinned&&tetherLengths,"Every free particle has a valid pinned geodesic tether");
    bool pinMap=true;double maximum=0;for(const auto& particle:cloth.particles){if(particle.inverseMass==0)pinMap&=particle.maxDistance==0&&particle.animationDrive==1;else maximum=std::max(maximum,particle.maxDistance);}
    Expect(pinMap&&maximum>0,"Waist anchors have zero max distance and the hem has a nonzero movement map");
}
void SyntheticTests(){
    std::puts("Garment partition and cloth asset generation:");auto source=Fixture();std::string error;GarmentBuildOptions coarse;coarse.refinementLevels=0;auto built=BuildSkirtGarment(source,coarse,&error);
    if(!Expect(built.has_value(),"A shared-material dress can be partitioned by skirt-weighted components")){std::printf("    %s\n",error.c_str());return;}
    Conservation(source,*built);ConstraintQuality(*built);
    Expect(built->garment->geometry->TriangleCount()==8&&built->body->geometry->TriangleCount()==1,"Unweighted skirt attachment is included and the coincident bodice is retained");
    Expect(built->garment->geometry->VertexCount()==10&&built->cloth.particles.size()==9,"A duplicated render seam is welded only in the simulation mesh");
    Expect(built->garment->geometry->materials.size()==1&&built->garment->geometry->textures.size()==1&&built->garment->geometry->materials[0].textures[0]==0,"Extracted material and texture references are compacted and remapped");
    Expect(built->garment->morphs[0].deltas.size()==2&&built->body->morphs[0].deltas.size()==1,"Morph deltas are partitioned and remapped to each part's vertices");
    Expect(Diagnostic(built->cloth,"Semantic waist fallback"),"An open sheet with a tall outer perimeter uses the semantic attachment fallback");
    GarmentBuildOptions restricted=coarse;restricted.materialIndices={1};auto again=BuildSkirtGarment(source,restricted,&error);
    Expect(again&&again->garment->geometry->TriangleCount()==8,"Material restriction does not accidentally extract the same-material torso");
    GarmentBuildOptions invalid;invalid.weldTolerance=0;Expect(!BuildSkirtGarment(source,invalid,&error)&&!error.empty(),"Invalid weld tolerance reports an actionable failure");
    invalid={};invalid.materialIndices={99};Expect(!BuildSkirtGarment(source,invalid,&error),"Out-of-range material restriction is rejected");
    invalid={};invalid.refinementLevels=3;Expect(!BuildSkirtGarment(source,invalid,&error),"Excessive offline refinement is rejected before allocating geometry");
    invalid={};invalid.surfaceDensity=0;Expect(!BuildSkirtGarment(source,invalid,&error),"Zero fabric surface density is rejected");
    auto noSkirt=source;noSkirt.skeleton.bones[2].name="accessory";Expect(!BuildSkirtGarment(noSkirt,{},&error),"Assets without recognized skirt bones require explicit selection");
    auto original=Triangles(source);auto changed=*built->garment;changed.skeleton.bones[1].restLocal.translation.y+=.1;
    Expect(!GarmentCompatible(*built->body,changed,&error),"A garment with the same skeleton ID but different rest pose is rejected");
    Expect(Triangles(source)==original,"Partition and compatibility checks do not mutate source geometry");
}
void MeshRegionRemapTests(){
    std::puts("Classification provenance across modular garment extraction:");
    auto source=Fixture();
    const auto sharedMesh=source.geometry->meshes[0];const auto sharedSkin=source.skins[0];
    const auto trianglePart=[&](const char* name,std::uint32_t material,int bone,std::uint32_t firstId,glm::dvec3 offset){
        StaticMesh mesh;mesh.name=name;mesh.materialIndex=material;MeshSkin skin;
        for(const auto p:{glm::dvec3(-.2,.6,0),glm::dvec3(.2,.6,0),glm::dvec3(0,1.,0)}) {
            StaticVertex vertex;vertex.position=p+offset;mesh.vertices.push_back(vertex);mesh.bounds.Include(vertex.position);
            VertexSkin weight;weight.joints={std::uint32_t(bone),0,0,0};skin.vertices.push_back(weight);skin.sourceVertexIds.push_back(firstId++);
        }
        mesh.indices={0,1,2};return std::pair{mesh,skin};
    };
    const auto bodyOnly=trianglePart("Body only",0,1,100,{0,1.,-.3});
    const auto garmentOnly=trianglePart("Garment only",1,2,200,{.7,-.1,.1});
    source.geometry->meshes={bodyOnly.first,sharedMesh,garmentOnly.first};source.skins={bodyOnly.second,sharedSkin,garmentOnly.second};
    source.geometry->instances.clear();source.geometry->bounds={};
    for(std::uint32_t mi=0;mi<3;++mi){source.geometry->instances.push_back({source.geometry->meshes[mi].name,mi,glm::dmat4(1)});for(const auto& v:source.geometry->meshes[mi].vertices)source.geometry->bounds.Include(v.position);}
    for(auto& morph:source.morphs)for(auto& delta:morph.deltas)delta.mesh=1;
    for(std::uint32_t mi=0;mi<3;++mi) {
        MeshRegionInfo region;region.mesh=mi;region.kind=mi==0?MeshRegionKind::Body:MeshRegionKind::Garment;
        region.confidence=.8f+.05f*mi;region.authoredCloth=mi!=0;region.requiresReview=mi!=1;region.userOverride=mi==1;
        region.sourceObject="Source object "+std::to_string(mi);region.sourceMesh="Source datablock "+std::to_string(mi);
        region.sourceMaterial=mi==0?"Skin":"Shared fabric";region.collections={"Character","Outfit"};region.vertexGroups={"Waist","Skirt"};
        region.modifiers={"Armature [ARMATURE]","Authored [CLOTH]"};region.evidence={"Original evidence "+std::to_string(mi)};region.clothPinGroup="Waist";
        source.meshRegions.push_back(std::move(region));
    }
    const auto original=source.meshRegions;
    GarmentBuildOptions options;options.refinementLevels=0;std::string error;const auto built=BuildSkirtGarment(source,options,&error);
    if(!Expect(bool(built),"Classified source meshes build a valid modular character after mesh compaction")){std::printf("    %s\n",error.c_str());return;}
    const auto same=[](const MeshRegionInfo& a,const MeshRegionInfo& b,std::uint32_t target){return a.mesh==target&&a.kind==b.kind&&a.confidence==b.confidence&&a.authoredCloth==b.authoredCloth&&a.requiresReview==b.requiresReview&&a.userOverride==b.userOverride&&a.sourceObject==b.sourceObject&&a.sourceMesh==b.sourceMesh&&a.sourceMaterial==b.sourceMaterial&&a.collections==b.collections&&a.vertexGroups==b.vertexGroups&&a.modifiers==b.modifiers&&a.evidence==b.evidence&&a.clothPinGroup==b.clothPinGroup;};
    const auto& body=built->body->meshRegions;const auto& garment=built->garment->meshRegions;
    Expect(body.size()==2&&garment.size()==2,"Each part drops classifications for source sections absent from that part");
    if(body.size()==2&&garment.size()==2) {
        Expect(same(body[0],original[0],0)&&same(body[1],original[1],1),"Body-only and shared reports retain every provenance and manual review field");
        Expect(same(garment[0],original[1],0)&&same(garment[1],original[2],1),"Garment source meshes one/two remap to zero/one while preserving classifications and overrides");
        Expect(body[1].sourceMaterial==garment[0].sourceMaterial&&built->garment->geometry->meshes[0].materialIndex==0,
            "Source material provenance survives independent compacted material indices");
    }
    bool unchanged=source.meshRegions.size()==original.size();for(std::size_t mi=0;unchanged&&mi<original.size();++mi)unchanged=same(source.meshRegions[mi],original[mi],std::uint32_t(mi));
    Expect(unchanged,"Partition leaves all source classification reports untouched");
    Expect(ValidateSkeletalAsset(*built->body,&error)&&ValidateSkeletalAsset(*built->garment,&error),"Both remapped packages pass section-reference validation");
    Conservation(source,*built);
}
void AttachmentBoundaryTests(){
    std::puts("Authored upper attachment boundary recognition:");std::string error;const auto source=TubeFixture();GarmentBuildOptions coarse;coarse.refinementLevels=0;auto built=BuildSkirtGarment(source,coarse,&error);
    if(!Expect(built.has_value(),"A closed skirt tube with distinct top and hem loops generates cloth")){std::printf("    %s\n",error.c_str());return;}
    Conservation(source,*built);ConstraintQuality(*built);
    Expect(built->garment->geometry->TriangleCount()==32&&built->body->geometry->TriangleCount()==1,"The complete tube is extracted before attachment-mask recognition");
    Expect(Diagnostic(built->cloth,"Closed upper attachment boundary: 8 vertices")&&!Diagnostic(built->cloth,"Semantic waist fallback: no"),"The broad highest closed loop takes priority over the lower pelvis bone");
    std::size_t fixed=0;bool topOnly=true,midFree=true;
    for(const auto& p:built->cloth.particles){if(p.inverseMass==0){++fixed;topOnly&=p.restPosition.y>1.59;}if(std::abs(p.restPosition.y-1.5)<1e-6)midFree&=p.inverseMass>0&&p.maxDistance/built->cloth.unitsPerMeter>.02;}
    Expect(fixed==8&&topOnly,"Only the authored upper attachment ring is fixed; the lower hem stays free");
    Expect(midFree,"The weighted row above the pelvis remains free with a graded max-distance map");
    auto narrow=BuildSkirtGarment(TubeFixture(true),coarse,&error);
    if(Expect(narrow.has_value(),"A narrow upper hole still produces a valid fallback cloth asset"))
        Expect(Diagnostic(narrow->cloth,"Semantic waist fallback")&&!Diagnostic(narrow->cloth,"Closed upper attachment boundary"),"A tiny hole with insufficient transverse coverage is not mistaken for the attachment ring");
}
glm::dvec3 MorphPosition(const MorphTarget& morph,std::uint32_t mesh,std::uint32_t vertex){glm::dvec3 result(0);for(const auto& delta:morph.deltas)if(delta.mesh==mesh&&delta.vertex==vertex)result+=delta.position;return result;}
double SurfaceArea(const ClothAsset& cloth){double area=0;for(const auto& t:cloth.triangles)area+=glm::length(glm::cross(cloth.particles[t[1]].restPosition-cloth.particles[t[0]].restPosition,cloth.particles[t[2]].restPosition-cloth.particles[t[0]].restPosition))*.5;return area;}
void RefinementTests(){
    std::puts("Refined cloth cooking, physical mass and independent waist reference:");const auto source=Fixture();std::string error;
    GarmentBuildOptions options;options.refinementLevels=0;const auto coarse=BuildSkirtGarment(source,options,&error);options.refinementLevels=1;const auto refined=BuildSkirtGarment(source,options,&error);
    if(!Expect(coarse&&refined,"Coarse and default one-level refined assets both cook successfully")){std::printf("    %s\n",error.c_str());return;}
    ConstraintQuality(*refined);
    Expect(refined->garment->geometry->TriangleCount()==32&&Triangles(*coarse->body)==Triangles(*refined->body),"Refinement splits only skirt triangles into four and leaves every body triangle untouched");
    Expect(refined->cloth.particles.size()==25&&refined->garment->geometry->VertexCount()>25,"One-level grid refinement welds UV midpoint copies only on the simulation surface");
    const auto& oldMesh=coarse->garment->geometry->meshes[0];const auto& newMesh=refined->garment->geometry->meshes[0];const auto& oldSkin=coarse->garment->skins[0];const auto& newSkin=refined->garment->skins[0];
    std::set<std::pair<std::uint32_t,std::uint32_t>> edges;std::size_t midpoint=oldMesh.vertices.size();bool interpolation=true,provenance=true,originals=true;
    for(std::size_t i=0;i<oldMesh.vertices.size();++i)originals&=newMesh.vertices[i].position==oldMesh.vertices[i].position&&newMesh.vertices[i].uv==oldMesh.vertices[i].uv&&newSkin.sourceVertexIds[i]==oldSkin.sourceVertexIds[i];
    const auto maxOriginal=*std::max_element(oldSkin.sourceVertexIds.begin(),oldSkin.sourceVertexIds.end());std::set<std::uint32_t> ids;
    for(std::size_t t=0;t<oldMesh.indices.size();t+=3)for(int c=0;c<3;++c){auto a=oldMesh.indices[t+c],b=oldMesh.indices[t+(c+1)%3];if(a>b)std::swap(a,b);if(!edges.insert({a,b}).second)continue;
        const auto& vertex=newMesh.vertices[midpoint];const auto& weights=newSkin.vertices[midpoint];
        interpolation&=glm::length(vertex.position-(oldMesh.vertices[a].position+oldMesh.vertices[b].position)*.5)<1e-12
            &&glm::length(vertex.uv-(oldMesh.vertices[a].uv+oldMesh.vertices[b].uv)*.5f)<1e-6f
            &&std::abs(weights.weights.x+weights.weights.y+weights.weights.z+weights.weights.w-1)<1e-6f
            &&glm::length(MorphPosition(refined->garment->morphs[0],0,static_cast<std::uint32_t>(midpoint))-(MorphPosition(coarse->garment->morphs[0],0,a)+MorphPosition(coarse->garment->morphs[0],0,b))*.5)<1e-12;
        provenance&=newSkin.sourceVertexIds[midpoint]>maxOriginal&&ids.insert(newSkin.sourceVertexIds[midpoint]).second;++midpoint;
    }
    Expect(originals&&interpolation,"Authored vertices survive and midpoint positions, UVs, skin weights and sparse morph fields interpolate correctly");
    Expect(provenance&&midpoint==newMesh.vertices.size(),"Every new render midpoint receives a distinct synthetic provenance ID");
    Expect(std::abs(SurfaceArea(coarse->cloth)-SurfaceArea(refined->cloth))<1e-12,"Refinement exactly preserves the authored rest surface area");
    std::vector<double> areas(refined->cloth.particles.size(),0);for(const auto& t:refined->cloth.triangles){const auto& p=refined->cloth.particles;const double area=glm::length(glm::cross(p[t[1]].restPosition-p[t[0]].restPosition,p[t[2]].restPosition-p[t[0]].restPosition))*.5;for(auto i:t)areas[i]+=area/3;}
    bool physicalMass=true,broadEnvelope=true,zeroFreeHem=false;for(std::size_t i=0;i<areas.size();++i){const auto& p=refined->cloth.particles[i];if(p.inverseMass==0)continue;physicalMass&=std::abs(p.inverseMass*areas[i]*options.surfaceDensity/(refined->cloth.unitsPerMeter*refined->cloth.unitsPerMeter)-1)<1e-9;broadEnvelope&=p.maxDistance/refined->cloth.unitsPerMeter>=.75;zeroFreeHem|=p.animationDrive==0;}
    Expect(physicalMass,"Free-particle mass derives from local surface area and physical kg/m^2 density");
    Expect(broadEnvelope&&zeroFreeHem,"Free cloth can sag through a broad safety envelope and the lower hem has zero animation drive");
    Expect(refined->cloth.motionBone==1&&refined->cloth.motionInverseBind==source.skeleton.bones[1].inverseBind,"Cloth target frame follows the waist inverse bind instead of authored animated skirt bones");
    options.refinementLevels=2;const auto fine=BuildSkirtGarment(source,options,&error);Expect(fine&&fine->garment->geometry->TriangleCount()==128&&fine->cloth.particles.size()==81,"Two refinement levels cook a denser cloth without changing the body");
    auto tube=BuildSkirtGarment(TubeFixture(),{},&error);if(Expect(tube.has_value(),"Refined closed skirt attachment still cooks")){
        const auto pins=std::count_if(tube->cloth.particles.begin(),tube->cloth.particles.end(),[](const auto& p){return p.inverseMass==0;});Expect(pins==16,"New upper boundary midpoints stay attached while all interior refined particles remain dynamic");}
}
void FullInfluenceTests(){
    std::puts("Complete skin influences across garment partition and refinement:");auto source=Fixture();
    for(int i=3;i<8;++i){SkeletonBone bone;bone.name="Auxiliary "+std::to_string(i);source.skeleton.bones.push_back(bone);}
    auto& skin=source.skins[0];skin.influenceOffsets={0};
    for(auto& vertex:skin.vertices){const bool skirt=vertex.joints[0]==2;vertex.joints={skirt?2u:1u,3,4,5};vertex.weights={skirt?.25f:2.f/3,skirt?.25f:1.f/9,skirt?.25f:1.f/9,skirt?.25f:1.f/9};
        for(const auto [joint,weight]:std::array<std::pair<std::uint32_t,float>,5>{{{skirt?2u:1u,skirt?.2f:.6f},{3,skirt?.2f:.1f},{4,skirt?.2f:.1f},{5,skirt?.2f:.1f},{skirt?6u:7u,skirt?.2f:.1f}}})skin.influences.push_back({joint,weight});
        skin.influenceOffsets.push_back(static_cast<std::uint32_t>(skin.influences.size()));}
    const auto weights=[](const MeshSkin& value,std::size_t vi){std::map<std::uint32_t,double> result;ForEachSkinInfluence(value,vi,[&](std::uint32_t joint,float weight){if(weight>0)result[joint]+=weight;});return result;};
    GarmentBuildOptions options;options.refinementLevels=0;std::string error;const auto coarse=BuildSkirtGarment(source,options,&error);
    if(!Expect(bool(coarse),"A source with five authored influences per vertex partitions successfully")){std::printf("    %s\n",error.c_str());return;}
    bool preserved=true;
    for(const auto* part:{coarse->body.get(),coarse->garment.get()})for(const auto& partSkin:part->skins){preserved&=partSkin.influenceOffsets.size()==partSkin.vertices.size()+1;
        for(std::size_t vi=0;vi<partSkin.vertices.size();++vi){const auto id=partSkin.sourceVertexIds[vi];const auto found=std::find(skin.sourceVertexIds.begin(),skin.sourceVertexIds.end(),id);preserved&=found!=skin.sourceVertexIds.end();if(found!=skin.sourceVertexIds.end())preserved&=weights(partSkin,vi)==weights(skin,static_cast<std::size_t>(found-skin.sourceVertexIds.begin()));}}
    Expect(preserved,"Every body and garment vertex retains all authored influences through source-vertex remapping");
    options.refinementLevels=1;const auto refined=BuildSkirtGarment(source,options,&error);
    if(!Expect(bool(refined),"A five-influence garment refines without discarding joints")){std::printf("    %s\n",error.c_str());return;}
    const auto& oldMesh=coarse->garment->geometry->meshes[0];const auto& oldSkin=coarse->garment->skins[0];const auto& newSkin=refined->garment->skins[0];
    bool interpolated=true;std::size_t maximum=0,midpoint=oldMesh.vertices.size();std::set<std::pair<std::uint32_t,std::uint32_t>> edges;
    for(std::size_t ti=0;ti<oldMesh.indices.size();ti+=3)for(int corner=0;corner<3;++corner){auto a=oldMesh.indices[ti+corner],b=oldMesh.indices[ti+(corner+1)%3];if(a>b)std::swap(a,b);if(!edges.emplace(a,b).second)continue;
        auto expected=weights(oldSkin,a);for(auto& [joint,weight]:expected)weight*=.5;for(const auto& [joint,weight]:weights(oldSkin,b))expected[joint]+=weight*.5;
        const auto actual=weights(newSkin,midpoint++);maximum=std::max(maximum,actual.size());interpolated&=actual.size()==expected.size();
        double total=0;for(const auto& [joint,weight]:actual){const auto found=expected.find(joint);interpolated&=found!=expected.end()&&std::abs(weight-found->second)<1e-7;total+=weight;}interpolated&=std::abs(total-1)<1e-6;}
    Expect(interpolated&&maximum>=7&&midpoint==newSkin.vertices.size(),"Midpoints average the complete endpoint sets, including seven-joint seams, with normalized weights");
    Expect(ValidateSkeletalAsset(*refined->body,&error)&&ValidateSkeletalAsset(*refined->garment,&error),"Both full-influence refined packages pass skeletal asset validation");
}
void SkinCollisionTests(){
    std::puts("Skin-fitted collision, taper, flattened sections and clothing exclusion:");std::string error;const auto cleanSource=LimbFixture(false),pollutedSource=LimbFixture(true);auto clean=BuildSkirtGarment(cleanSource,{},&error),polluted=BuildSkirtGarment(pollutedSource,{},&error);
    if(!Expect(clean&&polluted,"Tapered skin limbs cook convex collision without external geometry libraries")){std::printf("    %s\n",error.c_str());return;}ConstraintQuality(*polluted);
    const auto limbHulls=std::count_if(clean->cloth.convexes.begin(),clean->cloth.convexes.end(),[](const auto& shape){return shape.name.starts_with("left thigh")||shape.name.starts_with("left calf");});
    const auto footHulls=std::count_if(clean->cloth.convexes.begin(),clean->cloth.convexes.end(),[](const auto& shape){return shape.name=="left foot";}),footCaps=std::count_if(clean->cloth.capsules.begin(),clean->cloth.capsules.end(),[](const auto& cap){return cap.name=="left foot";});
    Expect(limbHulls==4&&footHulls+footCaps==1&&clean->cloth.convexes.size()==polluted->cloth.convexes.size(),"Four fitted leg hulls coexist with either valid foot geometry or its explicit capsule fallback");
    bool same=true,rigid=true,splitCaps=false;double minimumGap=1,maximumGap=0;const auto global=GlobalBoneTransforms(pollutedSource.skeleton,RestPose(pollutedSource.skeleton));
    for(std::size_t i=0;i<clean->cloth.convexes.size()&&same;++i){const auto& a=clean->cloth.convexes[i];const auto& b=polluted->cloth.convexes[i];same&=a.vertices==b.vertices&&a.triangles==b.triangles&&a.planes.size()==b.planes.size()&&a.skin.size()==b.skin.size();for(std::size_t p=0;p<a.planes.size()&&same;++p){same&=a.planes[p].normal==b.planes[p].normal&&a.planes[p].distance==b.planes[p].distance&&a.planes[p].collision==b.planes[p].collision;splitCaps|=!b.planes[p].collision;}
        for(std::size_t v=0;v<a.skin.size()&&same;++v){same&=a.skin[v].size()==b.skin[v].size();for(std::size_t j=0;j<a.skin[v].size()&&same;++j)same&=a.skin[v][j].bone==b.skin[v][j].bone&&a.skin[v][j].weight==b.skin[v][j].weight&&a.skin[v][j].weightedPosition==b.skin[v][j].weightedPosition;}
        rigid&=b.referenceValid&&(b.bone==3||b.bone==4||b.bone==5)&&b.referenceTransform==global[b.bone]&&b.vertices.size()<=1024&&b.triangles.size()<=256&&b.skin.size()==b.vertices.size();}
    for(std::size_t mi=1;mi<=2;++mi)for(const auto& vertex:cleanSource.geometry->meshes[mi].vertices){double depth=-1e10;const int bone=mi==1?3:4;
        for(const auto& shape:clean->cloth.convexes)if(shape.bone==bone){const auto local=glm::dvec3(glm::inverse(shape.referenceTransform)*glm::dvec4(vertex.position,1));double membership=-1e10,exterior=membership;for(const auto& plane:shape.planes){const double phi=glm::dot(plane.normal,local)-plane.distance;membership=std::max(membership,phi);if(plane.collision)exterior=std::max(exterior,phi);}if(membership<=clean->cloth.settings.thickness*clean->cloth.unitsPerMeter+1e-8)depth=std::max(depth,-exterior);}
        const double gap=depth/clean->cloth.unitsPerMeter+clean->cloth.settings.thickness;minimumGap=std::min(minimumGap,gap);maximumGap=std::max(maximumGap,gap);}
    Expect(same,"Weighted leggings do not enlarge collision when a named body skin surface is present");
    Expect(rigid&&polluted->cloth.settings.thickness==.003,"Every hull keeps its anatomical neutral frame, complete skin bindings and three-millimeter thickness");
    Expect(splitCaps,"Artificial axial partition faces remain in the closed hull but cannot push cloth toward another part");
    Expect(minimumGap>=.003-1e-6&&maximumGap<.008,"All source skin vertices stay covered while tapered elliptical sections remain within eight millimeters");
    auto pose=EvaluateSkeletalPose(*polluted->body,nullptr,0,false);const auto upperBefore=pose.global[3];pose.local[4].rotation=glm::angleAxis(.8,glm::dvec3(1,0,0));pose.global=GlobalBoneTransforms(polluted->body->skeleton,pose.local);
    Expect(pose.global[3]==upperBefore&&pose.global[4]!=global[4],"The pose independently moves the knee while keeping the upper-leg reference frame stable");
    auto skinnedPose=EvaluateSkeletalPose(cleanSource,nullptr,0,false);skinnedPose.local[4].rotation=glm::angleAxis(.8,glm::dvec3(1,0,0));skinnedPose.local[5].rotation=glm::angleAxis(-.3,glm::dvec3(1,0,0));skinnedPose.global=GlobalBoneTransforms(cleanSource.skeleton,skinnedPose.local);StaticModel skinned;bool skinCorrect=SkinSkeletalMesh(cleanSource,skinnedPose,skinned,&error);std::size_t checked=0,clipped=0,interior=0;double largestError=0;
    for(const auto& shape:clean->cloth.convexes){std::set<std::uint32_t> faceVertices;for(const auto& t:shape.triangles)faceVertices.insert(t.begin(),t.end());
        for(std::size_t vi=0;vi<shape.vertices.size();++vi){const auto rest=glm::dvec3(shape.referenceTransform*glm::dvec4(shape.vertices[vi],1));glm::dvec4 actual(0);for(const auto& influence:shape.skin[vi])actual+=skinnedPose.global[influence.bone]*glm::dvec4(influence.weightedPosition,influence.weight);bool found=false,isOriginal=false;double bestError=1e10;
            for(std::size_t mi=1;mi<=2;++mi){const auto& mesh=cleanSource.geometry->meshes[mi];for(const auto& vertex:mesh.vertices)isOriginal|=glm::length(vertex.position-rest)<1e-9;
                for(std::size_t ti=0;ti<mesh.indices.size();ti+=3){const auto i0=mesh.indices[ti],i1=mesh.indices[ti+1],i2=mesh.indices[ti+2];const auto a=mesh.vertices[i0].position,u=mesh.vertices[i1].position-a,v=mesh.vertices[i2].position-a,w=rest-a;const double uu=glm::dot(u,u),uv=glm::dot(u,v),vv=glm::dot(v,v),wu=glm::dot(w,u),wv=glm::dot(w,v),denominator=uu*vv-uv*uv;if(std::abs(denominator)<1e-16)continue;
                    const double y=(wu*vv-wv*uv)/denominator,z=(wv*uu-wu*uv)/denominator,x=1-y-z;if(std::min({x,y,z})< -1e-7||glm::length(a+u*y+v*z-rest)>1e-8)continue;
                    const auto expected=skinned.meshes[mi].vertices[i0].position*x+skinned.meshes[mi].vertices[i1].position*y+skinned.meshes[mi].vertices[i2].position*z;bestError=std::min(bestError,glm::length(glm::dvec3(actual)-expected));found=true;}}
            skinCorrect&=found;largestError=std::max(largestError,bestError);++checked;clipped+=!isOriginal;interior+=!faceVertices.contains(std::uint32_t(vi));}}
    Expect(skinCorrect&&checked>0&&clipped>0&&interior>0&&largestError<1e-8,"Clipped and rest-interior support points exactly follow the real body triangles after mixed-bone skinning");
    Expect(Diagnostic(polluted->cloth,"outside hull=0/")&&Diagnostic(polluted->cloth,"outside thickness shell=0/"),"Cooking reports uncovered skin samples alongside positive clearance percentiles");
    auto missingFoot=LimbFixture(false);for(auto& weights:missingFoot.skins[2].vertices){weights.joints={4,0,0,0};weights.weights={1,0,0,0};}auto fallback=BuildSkirtGarment(missingFoot,{},&error);
    Expect(fallback&&fallback->cloth.convexes.size()==4&&fallback->cloth.capsules.size()==1&&fallback->cloth.capsules[0].name=="left foot"&&Diagnostic(fallback->cloth,"left foot: foot/toe skin hull was degenerate"),"A skeleton with no weighted foot surface retains an explicit foot capsule instead of inventing a skin hull");
}
void PerMeshBindOffsetTests(){
    std::puts("Mesh-specific bind offsets and actual neutral CPU skin geometry:");auto source=LimbFixture(false);const auto globals=GlobalBoneTransforms(source.skeleton,RestPose(source.skeleton));
    // These valid mesh offsets deliberately differ from inverseBind * bindTransform.
    // An independently stored inverse bind also need not equal inverse(restGlobal).
    source.skeleton.bones[4].inverseBind[3].x+=.023;
    for(std::size_t mi=1;mi<=2;++mi){auto& skin=source.skins[mi];skin.bindTransform=glm::translate(glm::dmat4(1),glm::dvec3(.011,0,-.013));source.geometry->instances[mi].transform=skin.bindTransform;
        for(std::size_t joint=0;joint<globals.size();++joint){const auto adjustment=glm::translate(glm::dmat4(1),glm::dvec3(.004*mi+.001*joint,0,-.003*(joint%2)));skin.boneFromMesh.push_back(glm::inverse(globals[joint])*adjustment*skin.bindTransform);}}
    std::string error;auto built=BuildSkirtGarment(source,{},&error);if(!Expect(bool(built),"Valid per-mesh bone offsets cook without assuming the skeleton inverse bind is their replacement")){std::printf("    %s\n",error.c_str());return;}ConstraintQuality(*built);
    auto rest=EvaluateSkeletalPose(source,nullptr,0,false),posed=rest;posed.local[4].rotation=glm::angleAxis(.8,glm::dvec3(1,0,0));posed.local[5].rotation=glm::angleAxis(-.3,glm::dvec3(1,0,0));posed.global=GlobalBoneTransforms(source.skeleton,posed.local);StaticModel restMesh,posedMesh;bool valid=SkinSkeletalMesh(source,rest,restMesh,&error)&&SkinSkeletalMesh(source,posed,posedMesh,&error);std::size_t tested=0;double maximumRestShift=0,maximumError=0;
    const auto world=[&](const StaticModel& mesh,std::size_t mi,std::uint32_t vi){return glm::dvec3(source.geometry->instances[mi].transform*glm::dvec4(mesh.meshes[mi].vertices[vi].position,1));};
    for(std::size_t mi=1;mi<=2;++mi)for(std::uint32_t vi=0;vi<source.geometry->meshes[mi].vertices.size();++vi){const auto raw=glm::dvec3(source.skins[mi].bindTransform*glm::dvec4(source.geometry->meshes[mi].vertices[vi].position,1));maximumRestShift=std::max(maximumRestShift,glm::length(world(restMesh,mi,vi)-raw));}
    for(const auto& shape:built->cloth.convexes)for(std::size_t vi=0;vi<shape.vertices.size();++vi){const auto support=glm::dvec3(shape.referenceTransform*glm::dvec4(shape.vertices[vi],1));glm::dvec4 neutral(0),animated(0);for(const auto& influence:shape.skin[vi]){neutral+=rest.global[influence.bone]*glm::dvec4(influence.weightedPosition,influence.weight);animated+=posed.global[influence.bone]*glm::dvec4(influence.weightedPosition,influence.weight);}valid&=glm::length(glm::dvec3(neutral)-support)<1e-8;bool found=false;double best=1e10;
        for(std::size_t mi=1;mi<=2;++mi){const auto& indices=source.geometry->meshes[mi].indices;for(std::size_t ti=0;ti<indices.size();ti+=3){const auto a=world(restMesh,mi,indices[ti]),u=world(restMesh,mi,indices[ti+1])-a,v=world(restMesh,mi,indices[ti+2])-a,w=support-a;const double uu=glm::dot(u,u),uv=glm::dot(u,v),vv=glm::dot(v,v),wu=glm::dot(w,u),wv=glm::dot(w,v),denominator=uu*vv-uv*uv;if(std::abs(denominator)<1e-16)continue;const double y=(wu*vv-wv*uv)/denominator,z=(wv*uu-wu*uv)/denominator,x=1-y-z;
                if(std::min({x,y,z})< -1e-7||glm::length(a+u*y+v*z-support)>1e-8)continue;const auto expected=world(posedMesh,mi,indices[ti])*x+world(posedMesh,mi,indices[ti+1])*y+world(posedMesh,mi,indices[ti+2])*z;best=std::min(best,glm::length(glm::dvec3(animated)-expected));found=true;}}
        valid&=found;maximumError=std::max(maximumError,best);++tested;}
    Expect(maximumRestShift>.005,"The fixture actually moves neutral skin away from raw bind-transformed mesh vertices");
    Expect(valid&&tested>0&&maximumError<1e-8,"All original, clipped and interior supports match real CPU skin triangles with mesh-specific offsets");
}
void RealFixture(){
#ifdef MODEL_SOURCE_DIR
    const auto root=std::filesystem::path(MODEL_SOURCE_DIR);auto path=root/"Asset/Generated/MahiroTaitui/Package.asset.json";
    if(!std::filesystem::exists(path))path=root/"Asset/Generated/MahiroMaid/Package.asset.json";
    if(!std::filesystem::exists(path)){std::puts("Real Mahiro fixture unavailable; synthetic checks completed.");return;}
    std::string error;auto source=LoadSkeletalAssetPackage(path,&error);if(!Expect(source!=nullptr,"Load the real Mahiro generated package")){std::printf("    %s\n",error.c_str());return;}
    auto built=BuildSkirtGarment(*source,{},&error);if(!Expect(built.has_value(),"Extract real Mahiro skirt and generate a connected cloth asset")){std::printf("    %s\n",error.c_str());return;}
    ConstraintQuality(*built);
    Expect(source->geometry->TriangleCount()==38632&&built->garment->geometry->TriangleCount()==1976&&built->body->geometry->TriangleCount()==38138,"Real skirt refines 494 to 1,976 triangles; all 38,138 body triangles remain separate");
    Expect(built->garment->geometry->meshes.size()==1&&built->garment->geometry->VertexCount()>1034&&built->cloth.particles.size()==1034,"Only the lower skirt surface is refined: 1,034 welded simulation particles with separate render seams");
    Expect(built->body->geometry->meshes.size()==16&&built->garment->geometry->materials.size()==1,"Shared material bodice and both sleeves remain in the body asset");
    Expect(built->body->skeleton.bones.size()==186&&built->body->morphs.size()==57,"All 186 skeleton bones and 57 facial morph slots survive the partition");
    Expect(built->cloth.convexes.size()==18&&built->cloth.capsules.empty(),"Eighteen skin-fitted limb, layered pelvis and foot hulls replace inflated round collision bodies");
    const auto pelvisLayers=std::count_if(built->cloth.convexes.begin(),built->cloth.convexes.end(),[](const auto& shape){return shape.name.starts_with("Pelvis ")&&(shape.name.ends_with("lower")||shape.name.ends_with("upper"));});
    Expect(pelvisLayers==8&&Diagnostic(built->cloth,"at upper-leg center minus 2.5%"),"Eight one-millimeter-overlap pelvis sectors split at the clamped femoral-to-hip blend boundary");
    const auto fixed=std::count_if(built->cloth.particles.begin(),built->cloth.particles.end(),[](const auto& p){return p.inverseMass==0;});
    bool upperBand=true,apronFree=false;for(const auto& p:built->cloth.particles){if(p.inverseMass==0)upperBand&=p.restPosition.y>13.17;if(glm::length(p.restPosition-glm::dvec3(-.000403292477,12.00946331,1.78942704))<1e-5)apronFree=p.inverseMass>0&&p.maxDistance/built->cloth.unitsPerMeter>.07;}
    Expect(fixed==44&&upperBand&&Diagnostic(built->cloth,"Closed upper attachment boundary: 44 vertices"),"Real Mahiro fixes 22 authored upper boundary vertices plus their 22 refinement midpoints");
    Expect(apronFree,"The real front apron point above the pelvis can move around the raised thigh");
    for(const auto& diagnostic:built->cloth.diagnostics)std::printf("    %s\n",diagnostic.c_str());
#endif
}
}
int main(){SyntheticTests();MeshRegionRemapTests();AttachmentBoundaryTests();RefinementTests();FullInfluenceTests();SkinCollisionTests();PerMeshBindOffsetTests();RealFixture();std::printf("Garment builder: %d failure(s).\n",failures);return failures?1:0;}
