#include "Model/Public/hair_asset.h"
#include "Model/Public/cloth_asset.h"
#include "hair_body_collision.h"
#include "hair_attachment_binding.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace Model {
namespace {
void Need(bool yes,const std::string& text){if(!yes)throw std::runtime_error(text);}
bool HairBone(const SkeletonBone& b){return b.name.starts_with("J_Sec_Hair")||b.englishName.starts_with("J_Sec_Hair");}
bool HairMesh(const SkeletalAssetPackage& p,std::size_t m){
    const auto& mesh=p.geometry->meshes[m];std::string name=mesh.name;
    if(mesh.materialIndex<p.geometry->materials.size())name+=' '+p.geometry->materials[mesh.materialIndex].name;
    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(name.find("hair")!=std::string::npos)return true;
    for(const auto& v:p.skins[m].vertices)for(int k=0;k<4;++k)if(v.weights[k]>0&&HairBone(p.skeleton.bones[v.joints[k]]))return true;
    return false;
}
struct Candidate {
    HairSurfaceProbe probe;glm::dvec3 position{0},normal{0,1,0};
    std::vector<double> skin;
    double bodyDistance=std::numeric_limits<double>::infinity(); // signed meters; negative is verified inside
    std::uint32_t patch=0;
};
HairSurfaceProbe Probe(const SkeletalAssetPackage& p,std::uint32_t mesh,std::array<std::uint32_t,3> tri,glm::dvec3 bary,bool triangle){
    HairSurfaceProbe r;r.mesh=mesh;r.vertex=tri[0];r.triangle=tri;r.barycentric=bary;r.triangleSample=triangle;
    const auto& skin=p.skins[mesh];std::map<std::uint32_t,HairProbeInfluence> byBone;
    for(int c=0;c<3;++c)if(bary[c]>0){const auto v=tri[c];const auto& source=skin.vertices[v];double total=0;for(int k=0;k<4;++k)total+=source.weights[k];
        for(int k=0;k<4;++k)if(source.weights[k]>0){const auto bone=source.joints[k];const double weight=bary[c]*source.weights[k]/total;
            const auto offset=bone<skin.boneFromMesh.size()?skin.boneFromMesh[bone]:p.skeleton.bones[bone].inverseBind*skin.bindTransform;
            auto& i=byBone[bone];i.bone=bone;i.weight+=weight;i.weightedPosition+=glm::dvec3(offset*glm::dvec4(p.geometry->meshes[mesh].vertices[v].position,1))*weight;}}
    for(const auto& [bone,i]:byBone)r.influences.push_back(i);return r;
}
double Weight(const HairSurfaceProbe& p,std::uint32_t bone){for(const auto& i:p.influences)if(i.bone==bone)return i.weight;return 0;}
void ClassifyAttachments(HairAsset& a,const SkeletalAssetPackage& target,const std::vector<glm::dmat4>& globals){
    struct Vertex {std::uint32_t mesh=0,vertex=0,chain=0;detail::HairAttachmentWeights profile;glm::dvec3 position{0};bool valid=false,seed=false;};
    std::vector<Vertex> vertices;std::vector<std::vector<std::uint32_t>> indices(target.geometry->meshes.size());
    for(auto m:a.hairMeshes){indices[m].assign(target.geometry->meshes[m].vertices.size(),UINT32_MAX);for(std::uint32_t v=0;v<indices[m].size();++v)for(std::uint32_t c=0;c<a.chains.size();++c){detail::HairAttachmentWeights w;if(!detail::AuthoredHairBlend(target,a.chains[c],m,v,a.unitsPerMeter,w))continue;indices[m][v]=static_cast<std::uint32_t>(vertices.size());vertices.push_back({m,v,c,w,detail::HairRestVertex(target,globals,m,v)});break;}}
    if(vertices.empty())return;detail::HairBodyCollision body;std::string error;if(!body.Initialize(target,a.hairMeshes,a.unitsPerMeter,&error))throw std::runtime_error("Hair attachment bind-body classification: "+error);
    for(auto& v:vertices){const auto near=body.Nearest(v.position/a.unitsPerMeter);v.valid=near.found&&near.inside&&body.TriangleBoneWeight(near.triangle,static_cast<std::uint32_t>(a.chains[v.chain].anchorBone))>=.5;v.seed=v.valid&&v.profile.anchor>=.5&&v.profile.distalWeight==0;}
    std::vector<std::vector<std::uint32_t>> adjacency(vertices.size());const auto connect=[&](std::uint32_t x,std::uint32_t y){if(x==UINT32_MAX||y==UINT32_MAX||x==y||!vertices[x].valid||!vertices[y].valid||vertices[x].chain!=vertices[y].chain)return;adjacency[x].push_back(y);adjacency[y].push_back(x);};
    for(auto m:a.hairMeshes){const auto& mesh=target.geometry->meshes[m];for(std::size_t t=0;t<mesh.indices.size();t+=3)for(int e=0;e<3;++e)connect(indices[m][mesh.indices[t+e]],indices[m][mesh.indices[t+(e+1)%3]]);}
    // UV/material seams share a physical EDGE. A disconnected island merely
    // touching one equal-position vertex must not inherit the scalp mask.
    const double tolerance=a.unitsPerMeter*1e-7;const auto origin=vertices.front().position;using Cell=std::array<std::int64_t,3>;std::map<Cell,std::vector<std::uint32_t>> cells;std::vector<glm::dvec3> positions;std::vector<std::vector<std::uint32_t>> welded(target.geometry->meshes.size());
    for(auto m:a.hairMeshes){welded[m].resize(target.geometry->meshes[m].vertices.size());for(std::uint32_t v=0;v<welded[m].size();++v){const auto p=detail::HairRestVertex(target,globals,m,v);Cell key;for(int k=0;k<3;++k){const double q=std::floor((p[k]-origin[k])/tolerance);Need(std::isfinite(q)&&std::abs(q)<double(std::numeric_limits<std::int64_t>::max()/4),"Hair attachment seam coordinates exceed the supported local range.");key[k]=static_cast<std::int64_t>(q);}auto id=UINT32_MAX;for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)for(int z=-1;z<=1;++z){const auto found=cells.find({key[0]+x,key[1]+y,key[2]+z});if(found!=cells.end())for(auto other:found->second)if(glm::length(p-positions[other])<=tolerance)id=std::min(id,other);}if(id==UINT32_MAX){id=static_cast<std::uint32_t>(positions.size());positions.push_back(p);cells[key].push_back(id);}welded[m][v]=id;}}
    struct SeamEdge {std::uint32_t lower=UINT32_MAX,upper=UINT32_MAX;};std::map<std::uint64_t,std::vector<SeamEdge>> seams;
    for(auto m:a.hairMeshes){const auto& mesh=target.geometry->meshes[m];for(std::size_t t=0;t<mesh.indices.size();t+=3)for(int e=0;e<3;++e){auto va=mesh.indices[t+e],vb=mesh.indices[t+(e+1)%3],wa=welded[m][va],wb=welded[m][vb];if(wa==wb)continue;if(wa>wb){std::swap(va,vb);std::swap(wa,wb);}SeamEdge edge{indices[m][va],indices[m][vb]};auto& matching=seams[(std::uint64_t(wa)<<32)|wb];for(const auto& other:matching){connect(edge.lower,other.lower);connect(edge.upper,other.upper);}matching.push_back(edge);}}
    std::queue<std::uint32_t> todo;std::vector<bool> selected(vertices.size(),false);for(std::uint32_t i=0;i<vertices.size();++i)if(vertices[i].seed){selected[i]=true;todo.push(i);}while(!todo.empty()){const auto i=todo.front();todo.pop();for(auto next:adjacency[i])if(!selected[next]){selected[next]=true;todo.push(next);}}
    std::vector<double> bound(a.chains.size(),3.14159265358979323846);for(std::size_t i=0;i<vertices.size();++i)if(selected[i]){const auto& v=vertices[i];a.attachments.push_back({v.mesh,v.vertex,v.chain});bound[v.chain]=std::min(bound[v.chain],detail::HairAttachmentAngleBound(v.profile.rootLever,v.profile.distalCapacity));}
    for(std::size_t c=0;c<a.chains.size();++c)a.chains[c].links[0].maxAngle=std::min(a.chains[c].links[0].maxAngle,bound[c]);
}
}
namespace detail {void GenerateHairAttachments(HairAsset& a,const SkeletalAssetPackage& target,const std::vector<glm::dmat4>& globals){ClassifyAttachments(a,target,globals);}}
std::string HairBindingHash(const SkeletalAssetPackage& target){return ClothTopologyHash(target);}
std::optional<HairAsset> BuildHairAsset(const SkeletalAssetPackage& source,const SkeletalAssetPackage& target,const HairBuildOptions& options,std::string* error){
    try{
        std::string e;const bool validSource=ValidateSkeletalAsset(source,&e);Need(validSource,"Invalid hair source: "+e);const bool validTarget=ValidateSkeletalAsset(target,&e);Need(validTarget,"Invalid hair target: "+e);
        Need(options.maxProbes>=64&&options.maxProbes<=512,"Hair probe budget must be 64..512.");
        Need(!source.rigidBodies.empty()&&source.rigidBodies.size()==source.sourceRigidBodies&&source.physicsJoints.size()==source.sourceJoints,"Hair import requires complete physical records; reimport the original PMX.");
        std::unordered_map<std::string,std::uint32_t> targetNames;for(std::uint32_t i=0;i<target.skeleton.bones.size();++i)Need(targetNames.emplace(target.skeleton.bones[i].name,i).second,"Target bone names must be unique.");
        std::vector<std::uint32_t> sourceToTarget;for(const auto& b:source.skeleton.bones){auto found=targetNames.find(b.name);Need(found!=targetNames.end(),"Target is missing source bone: "+b.name);sourceToTarget.push_back(found->second);}
        const auto sg=GlobalBoneTransforms(source.skeleton,RestPose(source.skeleton)),tg=GlobalBoneTransforms(target.skeleton,RestPose(target.skeleton));
        for(std::size_t i=0;i<sg.size();++i){const auto t=sourceToTarget[i];for(int c=0;c<4;++c)Need(glm::length(sg[i][c]-tg[t][c])<1e-7,"Source and target rest skeleton differ; retarget before building hair.");const auto sp=source.skeleton.bones[i].parent;Need(target.skeleton.bones[t].parent==(sp<0?-1:static_cast<std::int32_t>(sourceToTarget[sp])),"Source and target parent layout differs.");}
        HairAsset a;a.name=options.name;a.skeletonId=target.skeleton.id;a.bindingHash=HairBindingHash(target);a.id=target.id+"/hair/"+a.bindingHash;
        a.sourceRigidBodies=source.rigidBodies;a.sourceJoints=source.physicsJoints;for(auto& b:a.sourceRigidBodies)if(b.bone>=0)b.bone=static_cast<std::int32_t>(sourceToTarget[b.bone]);
        // Use the complete source character height, matching skirt cooking even
        // when the target contains only the body sections after garment split.
        const double height=source.geometry->bounds.maximum.y-source.geometry->bounds.minimum.y;
        Need(std::isfinite(height)&&height>1e-5,"Invalid source character height.");a.unitsPerMeter=height/1.6;
        const auto count=target.skeleton.bones.size();std::vector<std::vector<std::uint32_t>> children(count);std::vector<bool> weighted(count,false);
        for(std::uint32_t b=0;b<count;++b)if(target.skeleton.bones[b].parent>=0)children[target.skeleton.bones[b].parent].push_back(b);
        for(std::uint32_t m=0;m<target.geometry->meshes.size();++m)if(HairMesh(target,m)){a.hairMeshes.push_back(m);for(const auto& v:target.skins[m].vertices)for(int k=0;k<4;++k)if(v.weights[k]>0&&HairBone(target.skeleton.bones[v.joints[k]]))weighted[v.joints[k]]=true;}
        std::vector<std::int32_t> boneChain(count,-1);std::size_t dropped=0;
        for(std::uint32_t root=0;root<count;++root){const auto& rb=target.skeleton.bones[root];if(!HairBone(rb)||(rb.parent>=0&&HairBone(target.skeleton.bones[rb.parent])))continue;
            std::vector<std::uint32_t> line;auto b=root;for(;;){line.push_back(b);std::vector<std::uint32_t> hairChildren;for(auto c:children[b])if(HairBone(target.skeleton.bones[c]))hairChildren.push_back(c);Need(hairChildren.size()<=1,"Branched hair bone hierarchy requires separate strands.");if(hairChildren.empty())break;b=hairChildren[0];}
            if(std::none_of(line.begin(),line.end(),[&](auto i){return weighted[i];})){++dropped;continue;}
            HairChain chain;chain.name=rb.name;chain.anchorBone=rb.parent;Need(chain.anchorBone>=0,"Hair chain requires an animated anchor bone.");
            for(std::size_t k=0;k<line.size();++k){HairBoneLink link;link.bone=line[k];const auto origin=glm::dvec3(tg[link.bone][3]);glm::dvec3 tip;
                if(k+1<line.size())tip=glm::dvec3(tg[line[k+1]][3]);else{const auto previous=k?glm::dvec3(tg[line[k-1]][3]):glm::dvec3(tg[chain.anchorBone][3]);auto direction=origin-previous;if(glm::length(direction)<a.unitsPerMeter*.002)direction={0,-a.unitsPerMeter*.025,0};tip=origin+direction*.65;}
                link.restTip=glm::dvec3(glm::inverse(tg[link.bone])*glm::dvec4(tip,1));Need(glm::length(link.restTip)>a.unitsPerMeter*1e-6,"Zero-length hair segment.");
                for(std::size_t r=0;r<a.sourceRigidBodies.size();++r)if(a.sourceRigidBodies[r].bone==static_cast<std::int32_t>(link.bone)&&a.sourceRigidBodies[r].mode==2){link.sourceRigidBody=static_cast<std::int32_t>(r);break;}
                if(link.sourceRigidBody<0)for(std::size_t r=0;r<a.sourceRigidBodies.size();++r)if(a.sourceRigidBodies[r].bone==static_cast<std::int32_t>(link.bone)){link.sourceRigidBody=static_cast<std::int32_t>(r);break;}
                if(link.sourceRigidBody>=0){const auto& rigid=a.sourceRigidBodies[link.sourceRigidBody];link.radius=std::clamp(rigid.dimensions.x/a.unitsPerMeter,.001,.015);link.damping=std::clamp(2+rigid.angularDamping*6,2.,8.);
                    for(std::size_t j=0;j<a.sourceJoints.size();++j){const auto& joint=a.sourceJoints[j];if((joint.rigidBodyA==link.sourceRigidBody||joint.rigidBodyB==link.sourceRigidBody)&&glm::length(joint.rotationSpring)>0){link.sourceJoint=static_cast<std::int32_t>(j);const double spring=glm::length(joint.rotationSpring);link.stiffness=std::clamp(8+std::log1p(spring)*2,8.,18.);const double angle=std::max(glm::length(joint.rotationMinimum),glm::length(joint.rotationMaximum));link.maxAngle=std::clamp(angle,.35,1.2);break;}}}
                // The preview uses one swing cone, not the source joint's
                // three independent Euler limits. Their vector length can
                // otherwise turn three moderate axes into a very wide cone.
                link.maxAngle=std::min(link.maxAngle,20.*3.14159265358979323846/180.);
                boneChain[link.bone]=static_cast<std::int32_t>(a.chains.size());chain.links.push_back(link);}
            a.chains.push_back(std::move(chain));}
        Need(!a.chains.empty()&&a.chains.size()<=64,"No effective hair chains or too many chains.");
        detail::GenerateHairAttachments(a,target,tg);std::set<std::uint64_t> attachments;for(const auto& v:a.attachments)attachments.insert((std::uint64_t(v.mesh)<<32)|v.vertex);
        const auto restPose=EvaluateSkeletalPose(target,nullptr,0,false);StaticModel restSkin;const bool normalsReady=SkinSkeletalMesh(target,restPose,restSkin,&e);Need(normalsReady,"Cannot prepare actual rest surface normals: "+e);
        std::vector<glm::dmat3> normalFromMesh(target.skins.size());for(auto m:a.hairMeshes)normalFromMesh[m]=glm::transpose(glm::inverse(glm::dmat3(target.skins[m].bindTransform)));
        std::vector<std::vector<Candidate>> candidates(a.chains.size());
        const auto add=[&](HairSurfaceProbe p){std::vector<double> cw(a.chains.size(),0);glm::dvec3 rest(0);for(const auto& i:p.influences){if(boneChain[i.bone]>=0)cw[boneChain[i.bone]]+=i.weight;rest+=glm::dvec3(tg[i.bone]*glm::dvec4(i.weightedPosition,i.weight));}const auto chain=std::size_t(std::max_element(cw.begin(),cw.end())-cw.begin());if(cw[chain]<=1e-8)return;p.chain=static_cast<std::uint32_t>(chain);bool fixed=true;glm::dvec3 normal(0);for(int k=0;k<3;++k)if(p.barycentric[k]>0){fixed&=attachments.contains((std::uint64_t(p.mesh)<<32)|p.triangle[k])||detail::HairRigidAnchorVertex(target,a.chains[chain],p.mesh,p.triangle[k]);normal+=glm::dvec3(restSkin.meshes[p.mesh].vertices[p.triangle[k]].normal)*p.barycentric[k];}if(fixed)return;normal=normalFromMesh[p.mesh]*normal;if(glm::dot(normal,normal)>1e-20)normal=glm::normalize(normal);else normal={0,1,0};
            Candidate v;v.position=rest;v.normal=normal;v.skin.resize(a.chains[chain].links.size()+2,0);for(const auto& i:p.influences){auto found=std::find_if(a.chains[chain].links.begin(),a.chains[chain].links.end(),[&](const auto& l){return l.bone==i.bone;});const auto index=found!=a.chains[chain].links.end()?std::size_t(found-a.chains[chain].links.begin()):std::size_t(a.chains[chain].links.size()+(i.bone==static_cast<std::uint32_t>(a.chains[chain].anchorBone)?0:1));v.skin[index]+=i.weight;}const auto segment=static_cast<std::uint32_t>(std::max_element(v.skin.begin(),v.skin.end())-v.skin.begin());int axis=0;if(std::abs(normal.y)>std::abs(normal.x))axis=1;if(std::abs(normal.z)>std::abs(normal[axis]))axis=2;v.patch=segment*6+std::uint32_t(axis*2+(normal[axis]<0?1:0));v.probe=std::move(p);candidates[chain].push_back(std::move(v));};
        for(auto m:a.hairMeshes){const auto& mesh=target.geometry->meshes[m];const auto attached=[&](auto v){return attachments.contains((std::uint64_t(m)<<32)|v);};for(std::uint32_t v=0;v<mesh.vertices.size();++v)if(!attached(v))add(Probe(target,m,{v,v,v},{1,0,0},false));for(std::size_t t=0;t<mesh.indices.size();t+=3)if(!(attached(mesh.indices[t])&&attached(mesh.indices[t+1])&&attached(mesh.indices[t+2])))add(Probe(target,m,{mesh.indices[t],mesh.indices[t+1],mesh.indices[t+2]},glm::dvec3(1.0/3),true));}
        // Body risk is measured only during asset generation. It does not add
        // nearest/signed-distance queries to preview ticks. The independent
        // skin/normal strata also work for standalone hair-only source fixtures.
        detail::HairBodyCollision bindBody;std::string riskError;const bool hasBody=bindBody.Initialize(target,a.hairMeshes,a.unitsPerMeter,&riskError);
        if(hasBody)for(auto& list:candidates)for(auto& v:list){const auto near=bindBody.Nearest(v.position/a.unitsPerMeter);if(near.found)v.bodyDistance=near.signedDistance;}
        std::vector<std::vector<std::size_t>> seeds(a.chains.size());std::vector<std::size_t> quota(a.chains.size(),0);std::size_t budget=0;
        for(std::size_t c=0;c<a.chains.size();++c){const auto& list=candidates[c];Need(!list.empty(),"An effective hair strand has no surface candidates.");std::set<std::size_t> seen;const auto seed=[&](std::size_t i){if(seen.insert(i).second)seeds[c].push_back(i);};std::map<std::uint32_t,std::size_t> patches;
            for(std::size_t i=0;i<list.size();++i){const auto [found,inserted]=patches.emplace(list[i].patch,i);if(!inserted&&list[i].bodyDistance<list[found->second].bodyDistance)found->second=i;}for(const auto& [patch,i]:patches)seed(i);
            std::size_t closestOutside=list.size(),deepestInside=list.size();for(std::size_t i=0;i<list.size();++i){if(list[i].bodyDistance<0){if(deepestInside==list.size()||list[i].bodyDistance<list[deepestInside].bodyDistance)deepestInside=i;}else if(std::isfinite(list[i].bodyDistance)&&(closestOutside==list.size()||list[i].bodyDistance<list[closestOutside].bodyDistance))closestOutside=i;}if(deepestInside<list.size())seed(deepestInside);if(closestOutside<list.size())seed(closestOutside);
            for(const auto& link:a.chains[c].links)if(weighted[link.bone]){std::size_t best=0;double max=-1;for(std::size_t i=0;i<list.size();++i){const double w=Weight(list[i].probe,link.bone);if(w>max){max=w;best=i;}}if(max>1e-8)seed(best);}for(auto m:a.hairMeshes)for(bool face:{false,true}){const auto found=std::find_if(list.begin(),list.end(),[&](const auto& v){return v.probe.mesh==m&&v.probe.triangleSample==face;});if(found!=list.end())seed(static_cast<std::size_t>(found-list.begin()));}
            quota[c]=std::min(list.size(),std::max<std::size_t>(8,seeds[c].size()));budget+=quota[c];}
        Need(budget<=options.maxProbes,"Probe budget is too small to preserve all strand/skin-weight/normal strata.");
        while(budget<options.maxProbes){std::size_t best=a.chains.size();double score=-1;for(std::size_t c=0;c<a.chains.size();++c)if(quota[c]<candidates[c].size()){const double s=std::sqrt(double(candidates[c].size()))/double(quota[c]+1);if(s>score){score=s;best=c;}}if(best==a.chains.size())break;++quota[best];++budget;}
        for(std::size_t c=0;c<a.chains.size();++c){auto& list=candidates[c];std::vector<bool> selected(list.size(),false);std::vector<double> nearest(list.size(),std::numeric_limits<double>::infinity());std::size_t used=0;double length=0;for(const auto& link:a.chains[c].links)length+=glm::length(glm::dvec3(tg[link.bone]*glm::dvec4(link.restTip,0)))/a.unitsPerMeter;const double normalScale=std::clamp(length*.08,.004,.025),skinScale=std::clamp(length*.10,.006,.030);
            const auto choose=[&](std::size_t index){if(selected[index]||used>=quota[c])return;selected[index]=true;++used;a.probes.push_back(list[index].probe);for(std::size_t j=0;j<list.size();++j){const auto d=(list[j].position-list[index].position)/a.unitsPerMeter,n=list[j].normal-list[index].normal;double skin=0;for(std::size_t k=0;k<list[j].skin.size();++k){const double w=list[j].skin[k]-list[index].skin[k];skin+=w*w;}const double distance=glm::dot(d,d)+normalScale*normalScale*glm::dot(n,n)+skinScale*skinScale*skin;nearest[j]=std::min(nearest[j],distance);}};
            for(auto i:seeds[c])choose(i);
            while(used<quota[c]){std::size_t best=0;double distance=-1;for(std::size_t i=0;i<list.size();++i)if(!selected[i]&&nearest[i]>distance){distance=nearest[i];best=i;}choose(best);}}
        std::size_t weightedCount=0,hairMorphs=0;for(bool b:weighted)weightedCount+=b;std::set<std::uint32_t> hm(a.hairMeshes.begin(),a.hairMeshes.end());for(const auto& m:target.morphs)for(const auto& d:m.deltas)hairMorphs+=hm.contains(d.mesh);
        a.diagnostics.push_back("Engine right-handed Y-up; positions/source dimensions retain source units; rotation limits converted as axial vectors. Mass and spring source coefficients retained separately.");
        a.diagnostics.push_back("SI runtime profile uses bounded response rates; PMX spring values are not interpreted as XPBD compliance.");
        a.diagnostics.push_back("Runtime conservative swing cone is capped at 20 degrees per link, with tighter authored attachment drift limits retained. This is not exact PMX three-axis joint replay; complete original source joint limits remain unchanged.");
        a.diagnostics.push_back("Effective chains="+std::to_string(a.chains.size())+", directly weighted bones="+std::to_string(weightedCount)+", dropped entirely unweighted chains="+std::to_string(dropped)+", surface probes="+std::to_string(a.probes.size())+" (vertices and triangle centers).");
        a.diagnostics.push_back("Authored scalp attachments="+std::to_string(a.attachments.size())+"; bind-inside root-dominant scalp points grown from original anchor-majority seeds across topology and source shared-edge seams. Distal articulated rotation capacity must be <=0.5 mm; that capacity is reserved before deriving the 1.5 mm total drift angle bound. External and seed-disconnected root hair, and meaningful distal motion, stay collision-active.");
        a.diagnostics.push_back("Free-surface probes preserve dominant skin-weight segment / signed normal-axis strata with deepest bind-inside and nearest external body representatives, then farthest-point sample position + actual rest normal + full chain skin weights. Normal/skin feature scales are bounded in meters relative to strand length; no motion-specific point indices or preview-time sampling cost.");
        if(hairMorphs)a.diagnostics.push_back("Hair surface has "+std::to_string(hairMorphs)+" sparse morph deltas; runtime collision probes evaluate active hair morph deformation.");
        const bool validHair=ValidateHairAsset(a,target,&e);Need(validHair,"Generated hair asset invalid: "+e);if(error)error->clear();return a;
    }catch(const std::exception& x){if(error)*error=x.what();return {};}
}
}
