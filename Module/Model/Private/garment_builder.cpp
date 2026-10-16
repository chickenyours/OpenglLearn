#include "Model/Public/cloth_asset.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

namespace Model {
namespace {
constexpr auto Missing=std::numeric_limits<std::uint32_t>::max();
void Require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
struct DisjointSet {
    std::vector<std::uint32_t> parent,rank;
    explicit DisjointSet(std::size_t count):parent(count),rank(count){std::iota(parent.begin(),parent.end(),0);}
    std::uint32_t Find(std::uint32_t v){while(parent[v]!=v){parent[v]=parent[parent[v]];v=parent[v];}return v;}
    void Join(std::uint32_t a,std::uint32_t b){a=Find(a);b=Find(b);if(a==b)return;if(rank[a]<rank[b])std::swap(a,b);parent[b]=a;if(rank[a]==rank[b])++rank[a];}
};
struct Cell {
    std::int64_t x=0,y=0,z=0;
    bool operator==(const Cell&)const=default;
};
struct CellHash {
    std::size_t operator()(const Cell& p)const{
        const auto h=[](std::int64_t n){std::uint64_t x=static_cast<std::uint64_t>(n);x^=x>>30;x*=0xbf58476d1ce4e5b9ULL;x^=x>>27;x*=0x94d049bb133111ebULL;return x^(x>>31);};
        return static_cast<std::size_t>(h(p.x)^(h(p.y)<<1)^(h(p.z)<<7));
    }
};
Cell GridCell(const glm::dvec3& p,double tolerance){
    Cell result;auto value=[&](double n){const double q=std::floor(n/tolerance);Require(std::isfinite(q)&&std::abs(q)<9e18,"Weld grid exceeds supported coordinate range.");return static_cast<std::int64_t>(q);};
    result.x=value(p.x);result.y=value(p.y);result.z=value(p.z);return result;
}
void WeldComponents(DisjointSet& components,const std::vector<glm::dvec3>& positions,double tolerance){
    std::unordered_map<Cell,std::vector<std::uint32_t>,CellHash> grid;
    const double squared=tolerance*tolerance;
    for(std::uint32_t i=0;i<positions.size();++i){
        const auto cell=GridCell(positions[i],tolerance);
        for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)for(int z=-1;z<=1;++z){
            const auto found=grid.find({cell.x+x,cell.y+y,cell.z+z});if(found==grid.end())continue;
            for(const auto other:found->second){const auto d=positions[i]-positions[other];if(glm::dot(d,d)<=squared)components.Join(i,other);}
        }
        grid[cell].push_back(i);
    }
}
bool SkirtBone(const SkeletonBone& bone){
    auto name=bone.name+" "+bone.englishName;
    for(auto& c:name)if(static_cast<unsigned char>(c)<128)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for(const auto term:{"skirt","スカート","裙","スカ－ト"})if(name.find(term)!=std::string::npos)return true;
    return false;
}
int RoleBone(const SkeletalAssetPackage& source,const std::string& role){
    for(const auto& entry:source.ikRig.humanoid)if(entry.role==role&&entry.bone>=0&&static_cast<std::size_t>(entry.bone)<source.skeleton.bones.size())return entry.bone;
    return -1;
}
glm::dvec3 Point(const glm::dmat4& matrix,const glm::dvec3& value){return glm::dvec3(matrix*glm::dvec4(value,1));}
std::vector<glm::dvec3> Positions(const StaticMesh& mesh,const MeshSkin& skin){
    std::vector<glm::dvec3> result;result.reserve(mesh.vertices.size());for(const auto& vertex:mesh.vertices)result.push_back(Point(skin.bindTransform,vertex.position));return result;
}
struct Part {
    std::shared_ptr<SkeletalAssetPackage> asset;
    std::vector<std::uint32_t> sourceMeshes;
    // Each source mesh has a source vertex -> part vertex lookup.
    std::vector<std::vector<std::uint32_t>> remap;
    std::vector<std::uint32_t> meshRemap;
};
Part ExtractPart(const SkeletalAssetPackage& source,const std::vector<std::vector<bool>>& selected,bool garment,const std::string& name){
    Part result;result.asset=std::make_shared<SkeletalAssetPackage>(source);auto& asset=*result.asset;
    asset.id=source.id+(garment?"/garments/":"/body/")+name;asset.name=source.name+(garment?"_":"_Body_")+name;
    asset.geometry=std::make_shared<StaticModel>();asset.geometry->sourcePath=source.geometry->sourcePath;asset.geometry->warnings=source.geometry->warnings;
    asset.skins.clear();asset.meshRegions.clear();asset.sourceRigidBodies=0;asset.sourceJoints=0;
    asset.rigidBodies.clear();asset.physicsJoints.clear();
    if(garment)asset.animations.clear();
    result.remap.resize(source.geometry->meshes.size());result.meshRemap.assign(source.geometry->meshes.size(),Missing);
    std::map<std::uint32_t,std::uint32_t> materials;
    for(std::uint32_t mi=0;mi<source.geometry->meshes.size();++mi){
        const auto& original=source.geometry->meshes[mi];auto& lookup=result.remap[mi];lookup.assign(original.vertices.size(),Missing);
        StaticMesh mesh;mesh.name=original.name+(garment?"_Garment":"_Body");
        MeshSkin skin;skin.bindTransform=source.skins[mi].bindTransform;skin.boneFromMesh=source.skins[mi].boneFromMesh;
        const bool fullInfluences=!source.skins[mi].influenceOffsets.empty();
        if(fullInfluences)skin.influenceOffsets.push_back(0);
        for(std::size_t triangle=0;triangle<original.indices.size()/3;++triangle){
            if(selected[mi][triangle]!=garment)continue;
            for(int corner=0;corner<3;++corner){
                const auto index=original.indices[triangle*3+corner];
                if(lookup[index]==Missing){
                    lookup[index]=static_cast<std::uint32_t>(mesh.vertices.size());mesh.vertices.push_back(original.vertices[index]);skin.vertices.push_back(source.skins[mi].vertices[index]);
                    if(fullInfluences){ForEachSkinInfluence(source.skins[mi],index,[&](std::uint32_t joint,float weight){skin.influences.push_back({joint,weight});});skin.influenceOffsets.push_back(static_cast<std::uint32_t>(skin.influences.size()));}
                    if(!source.skins[mi].sourceVertexIds.empty())skin.sourceVertexIds.push_back(source.skins[mi].sourceVertexIds[index]);
                    mesh.bounds.Include(original.vertices[index].position);
                }
                mesh.indices.push_back(lookup[index]);
            }
        }
        if(mesh.indices.empty())continue;
        auto [material,inserted]=materials.emplace(original.materialIndex,static_cast<std::uint32_t>(materials.size()));
        if(inserted)asset.geometry->materials.push_back(source.geometry->materials[original.materialIndex]);mesh.materialIndex=material->second;
        result.meshRemap[mi]=static_cast<std::uint32_t>(asset.geometry->meshes.size());result.sourceMeshes.push_back(mi);
        asset.geometry->meshes.push_back(std::move(mesh));asset.skins.push_back(std::move(skin));
    }
    // Reports refer to render sections, not to the compacted material index.
    // A shared section may have triangles in both parts: retain its authored
    // provenance/manual review on each surviving copy, with its new mesh ID.
    for(const auto& region:source.meshRegions)if(result.meshRemap[region.mesh]!=Missing) {
        auto remapped=region;remapped.mesh=result.meshRemap[region.mesh];asset.meshRegions.push_back(std::move(remapped));
    }
    // Keep source transforms and instance counts. A source triangle belongs to
    // exactly one part, even when a material contains both the bodice and skirt.
    for(const auto& old:source.geometry->instances)if(result.meshRemap[old.meshIndex]!=Missing){auto instance=old;instance.meshIndex=result.meshRemap[old.meshIndex];asset.geometry->instances.push_back(instance);}
    for(std::uint32_t mi=0;mi<asset.geometry->meshes.size();++mi){
        bool instanced=false;for(const auto& instance:asset.geometry->instances)if(instance.meshIndex==mi){instanced=true;for(const auto& vertex:asset.geometry->meshes[mi].vertices)asset.geometry->bounds.Include(Point(instance.transform,vertex.position));}
        if(!instanced){asset.geometry->instances.push_back({asset.geometry->meshes[mi].name,mi,asset.skins[mi].bindTransform});for(const auto& vertex:asset.geometry->meshes[mi].vertices)asset.geometry->bounds.Include(Point(asset.skins[mi].bindTransform,vertex.position));}
    }
    std::map<std::int32_t,std::int32_t> textures;
    for(auto& material:asset.geometry->materials)for(auto& slot:material.textures)if(slot>=0){auto [entry,inserted]=textures.emplace(slot,static_cast<std::int32_t>(textures.size()));if(inserted)asset.geometry->textures.push_back(source.geometry->textures[slot]);slot=entry->second;}
    for(auto& morph:asset.morphs){
        std::vector<MorphVertexDelta> deltas;deltas.reserve(morph.deltas.size());
        for(auto delta:morph.deltas)if(result.meshRemap[delta.mesh]!=Missing&&result.remap[delta.mesh][delta.vertex]!=Missing){delta.vertex=result.remap[delta.mesh][delta.vertex];delta.mesh=result.meshRemap[delta.mesh];deltas.push_back(delta);}
        morph.deltas=std::move(deltas);
    }
    asset.warnings.push_back(garment?"Modular garment contains extracted triangles only; skeleton and morph indices match the body.":"Garment triangles were removed from the body geometry; animation and morph indices are unchanged.");
    return result;
}
// Split authored render edges after partitioning. In particular, do not weld a
// blouse/skirt seam before extraction or interpolate across a UV/material seam.
// The later simulation weld joins coincident midpoint copies again.
void RefineGarment(SkeletalAssetPackage& garment,std::uint32_t levels){
    std::uint64_t nextSourceId=0;
    for(const auto& skin:garment.skins)for(auto id:skin.sourceVertexIds)nextSourceId=std::max(nextSourceId,static_cast<std::uint64_t>(id)+1);
    for(std::uint32_t level=0;level<levels;++level){
        for(std::uint32_t mi=0;mi<garment.geometry->meshes.size();++mi){
            auto& mesh=garment.geometry->meshes[mi];auto& skin=garment.skins[mi];
            // A midpoint may combine two disjoint four-joint sets. Promote the
            // mesh to a full influence table before adding vertices so neither
            // imported weights nor newly interpolated joints are truncated.
            if(skin.influenceOffsets.empty()){
                std::vector<SkinInfluence> influences;std::vector<std::uint32_t> offsets{0};
                for(std::size_t vi=0;vi<skin.vertices.size();++vi){ForEachSkinInfluence(skin,vi,[&](std::uint32_t joint,float weight){if(weight>0)influences.push_back({joint,weight});});offsets.push_back(static_cast<std::uint32_t>(influences.size()));}
                skin.influences=std::move(influences);skin.influenceOffsets=std::move(offsets);
            }
            const auto originalCount=mesh.vertices.size();std::map<std::pair<std::uint32_t,std::uint32_t>,std::uint32_t> mids;
            std::vector<std::array<std::uint32_t,2>> parents;
            const bool sourceIds=!skin.sourceVertexIds.empty();
            auto midpoint=[&](std::uint32_t a,std::uint32_t b){
                if(a>b)std::swap(a,b);auto [found,inserted]=mids.emplace(std::pair{a,b},static_cast<std::uint32_t>(mesh.vertices.size()));
                if(!inserted)return found->second;
                const auto va=mesh.vertices[a],vb=mesh.vertices[b];StaticVertex v;v.position=(va.position+vb.position)*.5;v.uv=(va.uv+vb.uv)*.5f;
                const auto normal=va.normal+vb.normal;v.normal=glm::dot(normal,normal)>1e-12f?glm::normalize(normal):va.normal;
                const auto tangent=glm::vec3(va.tangent)+glm::vec3(vb.tangent);v.tangent=glm::vec4(glm::dot(tangent,tangent)>1e-12f?glm::normalize(tangent):glm::vec3(va.tangent),(va.tangent.w+vb.tangent.w)<0?-1.f:1.f);
                mesh.vertices.push_back(v);parents.push_back({a,b});
                // New vertices use LBS: endpoint SDEF centres are not meaningful
                // for the merged set of influences. Original vertices retain
                // their authored skin mode and SDEF/QDEF metadata unchanged.
                std::map<std::uint32_t,double> combined;
                for(const auto index:{a,b})ForEachSkinInfluence(skin,index,[&](std::uint32_t joint,float weight){if(weight>0)combined[joint]+=weight*.5;});
                std::vector<std::pair<std::uint32_t,double>> ordered(combined.begin(),combined.end());std::sort(ordered.begin(),ordered.end(),[](const auto& x,const auto& y){return x.second!=y.second?x.second>y.second:x.first<y.first;});
                double fullTotal=0;for(const auto& influence:ordered)fullTotal+=influence.second;Require(fullTotal>1e-12,"A refined garment vertex has no skin influences.");
                for(const auto& [joint,weight]:ordered)skin.influences.push_back({joint,static_cast<float>(weight/fullTotal)});
                skin.influenceOffsets.push_back(static_cast<std::uint32_t>(skin.influences.size()));
                VertexSkin weights;weights.mode=SkinningMode::Bdef4;weights.weights={0,0,0,0};double total=0;
                for(std::size_t k=0;k<std::min<std::size_t>(4,ordered.size());++k){weights.joints[k]=ordered[k].first;weights.weights[k]=static_cast<float>(ordered[k].second);total+=ordered[k].second;}
                Require(total>1e-12,"A refined garment vertex has no skin influences.");weights.weights/=static_cast<float>(total);skin.vertices.push_back(weights);
                if(sourceIds){Require(nextSourceId<Missing,"Refined garment source vertex provenance exceeds 32-bit IDs.");skin.sourceVertexIds.push_back(static_cast<std::uint32_t>(nextSourceId++));}
                return found->second;
            };
            std::vector<std::uint32_t> refined;refined.reserve(mesh.indices.size()*4);
            for(std::size_t t=0;t<mesh.indices.size();t+=3){const auto a=mesh.indices[t],b=mesh.indices[t+1],c=mesh.indices[t+2];const auto ab=midpoint(a,b),bc=midpoint(b,c),ca=midpoint(c,a);
                refined.insert(refined.end(),{a,ab,ca,ab,b,bc,ca,bc,c,ab,bc,ca});}
            mesh.indices=std::move(refined);
            // Sparse source morphs become a dense lookup only while cooking.
            // Interpolation preserves their linear field, including UV seams.
            for(auto& morph:garment.morphs){
                std::vector<glm::dvec3> position(originalCount,glm::dvec3(0));std::vector<glm::vec3> normal(originalCount,glm::vec3(0));
                for(const auto& delta:morph.deltas)if(delta.mesh==mi){position[delta.vertex]+=delta.position;normal[delta.vertex]+=delta.normal;}
                for(std::size_t i=0;i<parents.size();++i){const auto [a,b]=parents[i];const auto p=(position[a]+position[b])*.5;const auto n=(normal[a]+normal[b])*.5f;
                    if(glm::dot(p,p)>0||glm::dot(n,n)>0)morph.deltas.push_back({mi,static_cast<std::uint32_t>(originalCount+i),p,n});}
            }
        }
    }
    if(levels)garment.warnings.push_back("Garment render/simulation surfaces were midpoint-refined after separation; new source vertex IDs are synthetic provenance IDs. UV seams, material boundaries, morph fields and original authored vertices are preserved.");
}
double SegmentDistance(const glm::dvec3& p,const glm::dvec3& a,const glm::dvec3& b){
    const auto d=b-a;const double squared=glm::dot(d,d);const double t=squared>1e-16?glm::clamp(glm::dot(p-a,d)/squared,0.0,1.0):0;
    return glm::length(p-(a+t*d));
}
// Offline deterministic incremental convex hull. Keeping the source support
// points avoids turning a tapered or flattened limb into one oversized circle.
struct SkinFitVertex {glm::dvec3 point;double weight=0;std::vector<ClothConvexInfluence> skin;};
SkinFitVertex SourceSkinSupport(const SkeletalAssetPackage& body,const std::vector<glm::dmat4>& global,std::size_t meshIndex,std::size_t vertexIndex,int selectedBone,int additionalBone=-1){
    const auto& mesh=body.geometry->meshes[meshIndex];const auto& skin=body.skins[meshIndex];SkinFitVertex result{{0,0,0},0,{}};double total=0;
    ForEachSkinInfluence(skin,vertexIndex,[&](std::uint32_t joint,float weight){if(weight>0&&joint<body.skeleton.bones.size()){total+=weight;if(joint==std::uint32_t(selectedBone)||(additionalBone>=0&&joint==std::uint32_t(additionalBone)))result.weight+=weight;}});
    Require(total>1e-12,"Body collision support has no valid normalized skin influences.");std::map<std::int32_t,ClothConvexInfluence> bindings;
    ForEachSkinInfluence(skin,vertexIndex,[&](std::uint32_t joint,float weight){if(weight>0&&joint<body.skeleton.bones.size()){const double normalized=weight/total;
        const auto offset=joint<skin.boneFromMesh.size()?skin.boneFromMesh[joint]:body.skeleton.bones[joint].inverseBind*skin.bindTransform;
        Require(std::abs(offset[0][3])<1e-10&&std::abs(offset[1][3])<1e-10&&std::abs(offset[2][3])<1e-10&&std::abs(offset[3][3]-1)<1e-10,"Body collision skin offsets must be affine.");
        auto& influence=bindings[joint];influence.bone=std::int32_t(joint);influence.weightedPosition+=Point(offset,mesh.vertices[vertexIndex].position)*normalized;influence.weight+=normalized;}});
    for(const auto& [joint,influence]:bindings){result.skin.push_back(influence);result.point+=glm::dvec3(global[joint]*glm::dvec4(influence.weightedPosition,influence.weight));}return result;
}
SkinFitVertex InterpolateSkinSupport(const SkinFitVertex& a,const SkinFitVertex& b,double t){
    SkinFitVertex result{glm::mix(a.point,b.point,t),a.weight+(b.weight-a.weight)*t,{}};std::map<std::int32_t,ClothConvexInfluence> merged;
    for(const auto& [vertex,factor]:std::array<std::pair<const SkinFitVertex*,double>,2>{{{&a,1-t},{&b,t}}})for(const auto& influence:vertex->skin){auto& value=merged[influence.bone];value.bone=influence.bone;value.weightedPosition+=influence.weightedPosition*factor;value.weight+=influence.weight*factor;}
    for(const auto& [bone,influence]:merged)if(influence.weight>0)result.skin.push_back(influence);return result;
}
std::optional<ClothConvexCollider> FitConvex(const std::string& name,int bone,std::vector<SkinFitVertex> supports,
    const SkeletalAssetPackage& body,const std::vector<glm::dmat4>& global,double height,const std::vector<ClothConvexPlane>& partitionPlanes){
    if(bone<0||std::size_t(bone)>=body.skeleton.bones.size()||supports.size()<4)return {};
    std::stable_sort(supports.begin(),supports.end(),[](const auto& a,const auto& b){return std::tie(a.point.x,a.point.y,a.point.z)<std::tie(b.point.x,b.point.y,b.point.z);});
    const double epsilon=std::max(1e-10,height*1e-8);std::vector<SkinFitVertex> unique;
    for(auto& support:supports){bool duplicate=false;for(auto it=unique.rbegin();it!=unique.rend()&&support.point.x-it->point.x<=epsilon;++it){if(glm::length(support.point-it->point)>epsilon||support.skin.size()!=it->skin.size())continue;bool same=true;for(std::size_t k=0;k<support.skin.size()&&same;++k)same=support.skin[k].bone==it->skin[k].bone&&std::abs(support.skin[k].weight-it->skin[k].weight)<=1e-10&&glm::length(support.skin[k].weightedPosition-it->skin[k].weightedPosition)<=epsilon;if(same){duplicate=true;break;}}if(!duplicate)unique.push_back(std::move(support));}supports=std::move(unique);
    if(supports.size()<4||supports.size()>1024)return {};std::vector<glm::dvec3> points;points.reserve(supports.size());for(const auto& vertex:supports)points.push_back(vertex.point);
    const std::uint32_t first=0;std::uint32_t second=0,third=0,fourth=0;double best=0;
    for(std::uint32_t i=1;i<points.size();++i){const auto d=points[i]-points[first];const double squared=glm::dot(d,d);if(squared>best){best=squared;second=i;}}
    if(best<=epsilon*epsilon)return {};const auto axis=points[second]-points[first];best=0;
    for(std::uint32_t i=0;i<points.size();++i){const double distance=glm::length(glm::cross(points[i]-points[first],axis))/glm::length(axis);if(distance>best){best=distance;third=i;}}
    if(best<=epsilon)return {};const auto initialNormal=glm::normalize(glm::cross(points[second]-points[first],points[third]-points[first]));best=0;
    for(std::uint32_t i=0;i<points.size();++i){const double distance=std::abs(glm::dot(initialNormal,points[i]-points[first]));if(distance>best){best=distance;fourth=i;}}
    if(best<=epsilon)return {};const auto interior=(points[first]+points[second]+points[third]+points[fourth])*.25;
    struct Face {std::array<std::uint32_t,3> indices;glm::dvec3 normal;double distance;};std::vector<Face> faces;
    auto face=[&](std::uint32_t a,std::uint32_t b,std::uint32_t c)->std::optional<Face>{auto cross=glm::cross(points[b]-points[a],points[c]-points[a]);const double length=glm::length(cross);if(length<=epsilon*epsilon)return {};
        auto normal=cross/length;if(glm::dot(normal,interior-points[a])>0){std::swap(b,c);normal=-normal;}return Face{{a,b,c},normal,glm::dot(normal,points[a])};};
    for(auto t:{std::array<std::uint32_t,3>{first,second,third},{first,fourth,second},{second,fourth,third},{third,fourth,first}}){const auto f=face(t[0],t[1],t[2]);if(!f)return {};faces.push_back(*f);}
    for(std::uint32_t p=0;p<points.size();++p){if(p==first||p==second||p==third||p==fourth)continue;
        std::vector<bool> visible(faces.size(),false);std::map<std::pair<std::uint32_t,std::uint32_t>,int> edges;bool outside=false;
        for(std::size_t f=0;f<faces.size();++f)if(glm::dot(faces[f].normal,points[p])-faces[f].distance>epsilon){visible[f]=true;outside=true;const auto& t=faces[f].indices;for(int k=0;k<3;++k){const auto a=t[k],b=t[(k+1)%3];++edges[{std::min(a,b),std::max(a,b)}];}}
        if(!outside)continue;std::vector<Face> remaining;remaining.reserve(faces.size()+edges.size());for(std::size_t f=0;f<faces.size();++f)if(!visible[f])remaining.push_back(faces[f]);
        for(const auto& [edge,count]:edges)if(count==1){const auto f=face(edge.first,edge.second,p);if(!f)return {};remaining.push_back(*f);}faces.swap(remaining);if(faces.size()>8192)return {};
    }
    if(faces.size()<4||faces.size()>256)return {};
    ClothConvexCollider result;result.name=name;result.bone=bone;result.referenceTransform=global[bone];result.referenceValid=true;
    const auto inverseReference=glm::inverse(global[bone]);for(const auto& support:supports){if(support.skin.empty()||support.skin.size()>16)return {};result.vertices.push_back(Point(inverseReference,support.point));result.skin.push_back(support.skin);}
    const auto localInterior=Point(inverseReference,interior);
    std::map<std::pair<std::uint32_t,std::uint32_t>,std::pair<int,int>> topology;
    for(const auto& f:faces){auto triangle=f.indices;const auto a=result.vertices[triangle[0]];auto normal=glm::normalize(glm::cross(result.vertices[triangle[1]]-a,result.vertices[triangle[2]]-a));
        if(glm::dot(normal,localInterior-a)>0){std::swap(triangle[1],triangle[2]);normal=-normal;}const double distance=glm::dot(normal,a);
        for(const auto& v:result.vertices)if(glm::dot(normal,v)>distance+epsilon*8)return {};
        for(int k=0;k<3;++k){const auto x=triangle[k],y=triangle[(k+1)%3];auto& e=topology[{std::min(x,y),std::max(x,y)}];++e.first;e.second+=x<y?1:-1;}
        bool collision=true;for(const auto& cut:partitionPlanes){const double length=glm::length(cut.normal);if(length>epsilon&&glm::dot(f.normal,cut.normal/length)>1-1e-8&&std::abs(f.distance-cut.distance/length)<=epsilon*8){collision=false;break;}}
        result.triangles.push_back(triangle);result.planes.push_back({normal,distance,collision});}
    for(const auto& [edge,count]:topology)if(count.first!=2||count.second!=0)return {};
    return result;
}
std::vector<glm::dvec3> BodySkinSamples(const SkeletalAssetPackage& body,const std::vector<glm::dmat4>& global,int bone,int additionalBone=-1){
    std::vector<glm::dvec3> skinPoints,fallback;
    for(std::size_t mi=0;mi<body.geometry->meshes.size();++mi){const auto& mesh=body.geometry->meshes[mi];const auto& skin=body.skins[mi];auto label=mesh.name+" "+body.geometry->materials[mesh.materialIndex].name;
        for(auto& letter:label)if(static_cast<unsigned char>(letter)<128)letter=static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));const bool namedSkin=label.find("skin")!=std::string::npos;
        for(std::size_t vi=0;vi<mesh.vertices.size();++vi){double weight=0;ForEachSkinInfluence(skin,vi,[&](std::uint32_t joint,float value){if(joint==std::uint32_t(bone)||(additionalBone>=0&&joint==std::uint32_t(additionalBone)))weight+=value;});if(weight<.2)continue;
            const auto p=SourceSkinSupport(body,global,mi,vi,bone,additionalBone).point;fallback.push_back(p);if(namedSkin)skinPoints.push_back(p);}}
    return skinPoints.empty()?fallback:skinPoints;
}
std::vector<SkinFitVertex> BodySkinPatch(const SkeletalAssetPackage& body,const std::vector<glm::dmat4>& global,int bone,const std::vector<ClothConvexPlane>& boundaries,int additionalBone=-1){
    using Vertex=SkinFitVertex;std::vector<Vertex> result;bool hasNamedSkin=false;
    auto skinMesh=[&](std::size_t mi){auto label=body.geometry->meshes[mi].name+" "+body.geometry->materials[body.geometry->meshes[mi].materialIndex].name;for(auto& letter:label)if(static_cast<unsigned char>(letter)<128)letter=static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));return label.find("skin")!=std::string::npos;};
    auto weight=[&](const MeshSkin& skin,std::size_t vi){double value=0;ForEachSkinInfluence(skin,vi,[&](std::uint32_t joint,float w){if(joint==std::uint32_t(bone)||(additionalBone>=0&&joint==std::uint32_t(additionalBone)))value+=w;});return value;};
    for(std::size_t mi=0;mi<body.skins.size()&&!hasNamedSkin;++mi)if(skinMesh(mi))for(std::size_t vi=0;vi<body.skins[mi].vertices.size();++vi)if(weight(body.skins[mi],vi)>=.2){hasNamedSkin=true;break;}
    auto clip=[](std::vector<Vertex> polygon,const auto& signedDistance){std::vector<Vertex> out;if(polygon.empty())return out;auto previous=polygon.back();double before=signedDistance(previous);
        for(const auto& current:polygon){const double after=signedDistance(current);if((before<=0)!=(after<=0)){const double t=before/(before-after);out.push_back(InterpolateSkinSupport(previous,current,t));}if(after<=0)out.push_back(current);previous=current;before=after;}return out;};
    for(std::size_t mi=0;mi<body.geometry->meshes.size();++mi){if(hasNamedSkin&&!skinMesh(mi))continue;const auto& mesh=body.geometry->meshes[mi];
        for(std::size_t t=0;t<mesh.indices.size();t+=3){std::vector<Vertex> polygon;bool influenced=false;for(int k=0;k<3;++k){const auto vi=mesh.indices[t+k];auto vertex=SourceSkinSupport(body,global,mi,vi,bone,additionalBone);influenced|=vertex.weight>=.2;polygon.push_back(std::move(vertex));}if(!influenced)continue;
            polygon=clip(std::move(polygon),[](const Vertex& v){return .2-v.weight;});for(const auto& plane:boundaries){polygon=clip(std::move(polygon),[&](const Vertex& v){return glm::dot(plane.normal,v.point)-plane.distance;});if(polygon.empty())break;}for(auto& p:polygon)result.push_back(std::move(p));}}
    return result;
}
void ConvexFitDiagnostic(ClothAsset& cloth,const std::string& region,const std::vector<glm::dvec3>& samples,const std::vector<ClothConvexCollider>& shapes){
    std::vector<double> gaps;std::vector<std::vector<ClothConvexPlane>> worldPlanes;std::size_t faces=0,outsideHull=0,outsideExclusion=0;
    for(const auto& shape:shapes){faces+=shape.triangles.size();std::vector<ClothConvexPlane> planes;const auto normalMatrix=glm::transpose(glm::inverse(glm::dmat3(shape.referenceTransform)));const auto translation=glm::dvec3(shape.referenceTransform[3]);
        for(const auto& plane:shape.planes){const auto normal=normalMatrix*plane.normal;const double length=glm::length(normal);planes.push_back({normal/length,(plane.distance+glm::dot(normal,translation))/length,plane.collision});}worldPlanes.push_back(std::move(planes));}
    for(const auto& p:samples){double depth=-std::numeric_limits<double>::infinity(),membership=std::numeric_limits<double>::infinity();
        for(const auto& planes:worldPlanes){double all=-std::numeric_limits<double>::infinity(),exterior=all;for(const auto& plane:planes){const double phi=glm::dot(plane.normal,p)-plane.distance;all=std::max(all,phi);if(plane.collision)exterior=std::max(exterior,phi);}membership=std::min(membership,all);if(all/cloth.unitsPerMeter<=cloth.settings.thickness+1e-7)depth=std::max(depth,-exterior);}
        const double gap=std::isfinite(depth)?depth/cloth.unitsPerMeter+cloth.settings.thickness:cloth.settings.thickness-membership/cloth.unitsPerMeter;gaps.push_back(gap);outsideHull+=membership/cloth.unitsPerMeter>1e-6;outsideExclusion+=membership/cloth.unitsPerMeter>cloth.settings.thickness+1e-6;}
    std::sort(gaps.begin(),gaps.end());if(gaps.empty())return;const auto percentile=[&](double p){return gaps[std::min(gaps.size()-1,std::size_t(p*(gaps.size()-1)))];};
    std::ostringstream diagnostic;diagnostic<<region<<" skin-fitted convex collision: "<<shapes.size()<<" hulls, "<<faces<<" triangles, "<<samples.size()<<" weighted body skin samples; rest skin-to-exclusion-plane clearance min/median/p95/max="<<gaps.front()*1000<<'/'<<percentile(.5)*1000<<'/'<<percentile(.95)*1000<<'/'<<gaps.back()*1000<<" mm including "<<cloth.settings.thickness*1000<<" mm cloth thickness; outside hull="<<outsideHull<<'/'<<samples.size()<<" ("<<100.0*outsideHull/samples.size()<<"%), outside thickness shell="<<outsideExclusion<<'/'<<samples.size()<<" ("<<100.0*outsideExclusion/samples.size()<<"%).";cloth.diagnostics.push_back(diagnostic.str());
}
void GenerateConvexes(const SkeletalAssetPackage& body,const std::vector<glm::dmat4>& global,ClothAsset& cloth,double height){
    auto publish=[&](const std::string& replaced,const std::vector<glm::dvec3>& samples,std::vector<ClothConvexCollider> shapes){if(shapes.empty())return;ConvexFitDiagnostic(cloth,replaced,samples,shapes);for(auto& shape:shapes)cloth.convexes.push_back(std::move(shape));std::erase_if(cloth.capsules,[&](const auto& cap){return cap.name==replaced;});};
    for(const std::string side:{"left","right"})for(const std::string limb:{"thigh","calf"}){const bool thigh=limb=="thigh";const int bone=RoleBone(body,side+(thigh?"UpperLeg":"LowerLeg")),end=RoleBone(body,side+(thigh?"LowerLeg":"Foot"));if(bone<0||end<0)continue;
        const auto a=glm::dvec3(global[bone][3]),axis=glm::dvec3(global[end][3])-a;const double length2=glm::dot(axis,axis);if(length2<=height*height*1e-8)continue;std::vector<glm::dvec3> samples;
        for(const auto& p:BodySkinSamples(body,global,bone)){const double t=glm::dot(p-a,axis)/length2;if(t>=0&&t<=1)samples.push_back(p);}std::vector<ClothConvexCollider> shapes;bool valid=true;
        for(int part=0;part<2;++part){const auto normal=axis/length2;const double lo=std::max(0.0,part*.5-.03),hi=std::min(1.0,(part+1)*.5+.03);const std::vector<ClothConvexPlane> boundaries={{normal,glm::dot(normal,a)+hi},{-normal,-glm::dot(normal,a)-lo}};auto points=BodySkinPatch(body,global,bone,boundaries);
            auto hull=FitConvex(side+" "+limb+" "+std::to_string(part+1),bone,std::move(points),body,global,height,boundaries);if(!hull){valid=false;break;}shapes.push_back(std::move(*hull));}
        if(valid)publish(side+" "+limb,samples,std::move(shapes));else cloth.diagnostics.push_back(side+" "+limb+": skin-fitted convex generation was degenerate or exceeded 256 faces; retained the compatible capsule.");
    }
    const int hips=RoleBone(body,"hips"),left=RoleBone(body,"leftUpperLeg"),right=RoleBone(body,"rightUpperLeg");
    if(hips>=0&&left>=0&&right>=0){const auto center=(glm::dvec3(global[left][3])+glm::dvec3(global[right][3]))*.5;std::vector<glm::dvec3> samples;
        for(const auto& p:BodySkinSamples(body,global,hips))if(p.y>=center.y-height*.075&&p.y<=center.y+height*.1125)samples.push_back(p);
        double lowest=center.y-height*.075,highest=center.y+height*.1125;
        if(!samples.empty()){lowest=highest=samples.front().y;for(const auto& point:samples){lowest=std::min(lowest,point.y);highest=std::max(highest,point.y);}}
        // The femoral/hip blend changes below the upper-leg origins. Splitting
        // there separates the crotch fold from the broader waist surface.
        const double guard=std::min((highest-lowest)*.2,height*.01);
        const double middle=std::clamp(center.y-height*.025,lowest+guard,highest-guard),overlap=cloth.unitsPerMeter*.001;
        std::vector<ClothConvexCollider> shapes;bool valid=true;
        for(int side:{-1,1})for(int facing:{-1,1})for(int layer=0;layer<2;++layer){const double lo=layer==0?center.y-height*.075:middle-overlap,hi=layer==0?middle+overlap:center.y+height*.1125;
            const std::vector<ClothConvexPlane> boundaries={{{0,-1,0},-lo},{{0,1,0},hi},{{-double(side),0,0},-side*center.x+overlap},{{0,0,-double(facing)},-facing*center.z+overlap}};auto points=BodySkinPatch(body,global,hips,boundaries);
            auto hull=FitConvex(std::string("Pelvis ")+(side<0?"right ":"left ")+(facing<0?"back ":"front ")+(layer==0?"lower":"upper"),hips,std::move(points),body,global,height,boundaries);if(!hull){valid=false;break;}shapes.push_back(std::move(*hull));}
        if(valid&&shapes.size()==8){publish("Pelvis",samples,std::move(shapes));std::ostringstream split;split<<"Pelvis layer split: Y="<<middle<<" source units, at upper-leg center minus 2.5% of character height, clamped inside skin range "<<lowest<<".."<<highest<<"; layer/sector overlap is 1 mm.";cloth.diagnostics.push_back(split.str());}else cloth.diagnostics.push_back("Pelvis: skin-fitted height/sector hulls were degenerate or exceeded the face budget; retained the compatible capsule.");
    }
    for(const std::string side:{"left","right"}){const int foot=RoleBone(body,side+"Foot"),toes=RoleBone(body,side+"Toes");if(foot<0)continue;const auto samples=BodySkinSamples(body,global,foot,toes);
        auto points=BodySkinPatch(body,global,foot,{},toes);auto hull=FitConvex(side+" foot",foot,std::move(points),body,global,height,{});
        if(hull){std::vector<ClothConvexCollider> shapes;shapes.push_back(std::move(*hull));publish(side+" foot",samples,std::move(shapes));}else cloth.diagnostics.push_back(side+" foot: foot/toe skin hull was degenerate or exceeded the support/face budget; retained the compatible capsule.");
    }
}
void GenerateCapsules(const SkeletalAssetPackage& body,const std::vector<glm::dmat4>& global,ClothAsset& cloth,double height){
    auto add=[&](const std::string& name,int a,int b,const std::vector<int>& weighted,double fallback,double maximum){
        if(a<0||b<0)return;const auto pa=glm::dvec3(global[a][3]),pb=glm::dvec3(global[b][3]);
        std::vector<double> radii;
        for(std::size_t mi=0;mi<body.geometry->meshes.size();++mi){const auto& mesh=body.geometry->meshes[mi];const auto& skin=body.skins[mi];
            for(std::size_t vi=0;vi<mesh.vertices.size();++vi){double weight=0;ForEachSkinInfluence(skin,vi,[&](std::uint32_t joint,float w){if(std::find(weighted.begin(),weighted.end(),static_cast<int>(joint))!=weighted.end())weight+=w;});
                if(weight<.2)continue;const auto position=SourceSkinSupport(body,global,mi,vi,a).point;
                // Fit the middle of a limb. Joint fans would otherwise enlarge
                // a thigh capsule using vertices from the pelvis or ankle.
                const auto direction=pb-pa;const double length2=glm::dot(direction,direction);const double t=length2>1e-12?glm::dot(position-pa,direction)/length2:.5;
                if(t<.08||t>.92)continue;radii.push_back(SegmentDistance(position,pa,pb));
            }
        }
        double radius=fallback;if(!radii.empty()){const auto p=static_cast<std::size_t>((radii.size()-1)*.95);std::nth_element(radii.begin(),radii.begin()+p,radii.end());radius=radii[p]*1.015;}
        radius=glm::clamp(radius,height*.012,maximum);
        ClothCapsule capsule;capsule.name=name;capsule.boneA=a;capsule.boneB=b;capsule.localA=Point(body.skeleton.bones[a].inverseBind,pa);capsule.localB=Point(body.skeleton.bones[b].inverseBind,pb);capsule.radius=radius;capsule.referenceValid=true;capsule.restA=pa;capsule.restB=pb;cloth.capsules.push_back(capsule);
    };
    const int leftThigh=RoleBone(body,"leftUpperLeg"),rightThigh=RoleBone(body,"rightUpperLeg"),hips=RoleBone(body,"hips");
    add("Pelvis",leftThigh,rightThigh,{hips},height*.038,height*.055);
    for(const std::string side:{"left","right"}){
        const int upper=RoleBone(body,side+"UpperLeg"),lower=RoleBone(body,side+"LowerLeg"),foot=RoleBone(body,side+"Foot"),toes=RoleBone(body,side+"Toes");
        add(side+" thigh",upper,lower,{upper},height*.034,height*.052);
        add(side+" calf",lower,foot,{lower},height*.023,height*.038);
        add(side+" foot",foot,toes,{foot,toes},height*.020,height*.032);
    }
    if(cloth.capsules.empty())cloth.diagnostics.push_back("No recognized body limbs: add body collision capsules manually before enabling collision.");
    else cloth.diagnostics.push_back("Body collision capsules conservatively fit the 95th percentile of body skin influenced by the corresponding limb, with bounded radii and a small surface margin.");
}
struct AttachmentRing {
    std::vector<std::uint32_t> vertices;
    Bounds bounds;
    double meanHeight=0;
};
std::optional<AttachmentRing> UpperAttachmentRing(const ClothAsset& cloth,const std::vector<std::uint32_t>& component,
    const std::map<std::pair<std::uint32_t,std::uint32_t>,std::vector<std::uint32_t>>& opposite,double modelHeight){
    Bounds componentBounds;std::set<std::uint32_t> members(component.begin(),component.end());
    for(auto i:component)componentBounds.Include(cloth.particles[i].restPosition);
    const auto extent=componentBounds.Extent();if(extent.y<=modelHeight*.02)return {};
    std::map<std::uint32_t,std::vector<std::uint32_t>> boundary;
    for(const auto& [edge,others]:opposite)if(others.size()==1&&members.contains(edge.first)&&members.contains(edge.second)){
        boundary[edge.first].push_back(edge.second);boundary[edge.second].push_back(edge.first);
    }
    std::set<std::uint32_t> seen;std::vector<AttachmentRing> candidates;
    for(const auto& [start,neighbors]:boundary){
        if(seen.contains(start))continue;AttachmentRing ring;ring.vertices.push_back(start);seen.insert(start);bool closed=true;
        for(std::size_t next=0;next<ring.vertices.size();++next){const auto i=ring.vertices[next];const auto& adjacent=boundary.at(i);closed&=adjacent.size()==2;
            ring.bounds.Include(cloth.particles[i].restPosition);ring.meanHeight+=cloth.particles[i].restPosition.y;
            for(auto neighbor:adjacent)if(seen.insert(neighbor).second)ring.vertices.push_back(neighbor);
        }
        if(!closed||ring.vertices.size()<6)continue;ring.meanHeight/=ring.vertices.size();const auto ringExtent=ring.bounds.Extent();
        // A skirt opening is a broad, fairly horizontal upper loop. Open cloth
        // sheets, the hem, pinholes and little detached trim loops are not
        // reliable evidence of a waistband and retain the semantic fallback.
        if(ringExtent.y>std::max(extent.y*.12,modelHeight*.01)
            ||ring.bounds.minimum.y<componentBounds.maximum.y-extent.y*.20
            ||ring.meanHeight<componentBounds.minimum.y+extent.y*.65)continue;
        bool spansSurface=false;
        if(extent.x>modelHeight*.01){if(ringExtent.x<extent.x*.30)continue;spansSurface=true;}
        if(extent.z>modelHeight*.01){if(ringExtent.z<extent.z*.30)continue;spansSurface=true;}
        if(!spansSurface)continue;candidates.push_back(std::move(ring));
    }
    if(candidates.empty())return {};
    std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.meanHeight>b.meanHeight;});
    // Two similarly high broad openings in one connected cloth piece are
    // ambiguous (for example leg holes); do not silently choose one anchor.
    if(candidates.size()>1&&candidates.front().meanHeight-candidates[1].meanHeight<extent.y*.12)return {};
    return candidates.front();
}
ClothAsset GenerateCloth(const SkeletalAssetPackage& source,const SkeletalAssetPackage& garment,const SkeletalAssetPackage& body,
    const std::vector<bool>& skirtBones,const std::vector<glm::dmat4>& global,const GarmentBuildOptions& options,double height){
    ClothAsset cloth;cloth.id=garment.id+"/cloth";cloth.name=options.name+"_Cloth";cloth.skeletonId=source.skeleton.id;cloth.garmentId=garment.id;cloth.unitsPerMeter=height/1.6;cloth.settings.thickness=.003;
    std::vector<glm::dvec3> positions;std::vector<std::vector<std::uint32_t>> renderIndex(garment.geometry->meshes.size());
    for(std::uint32_t mi=0;mi<garment.geometry->meshes.size();++mi){
        for(const auto& instance:garment.geometry->instances)if(instance.meshIndex==mi){
            for(int c=0;c<4;++c)for(int r=0;r<4;++r)Require(std::abs(instance.transform[c][r]-garment.skins[mi].bindTransform[c][r])<1e-7,"Garment preview requires one consistent bind transform per mesh; expand source mesh instances first.");
        }
        renderIndex[mi].resize(garment.geometry->meshes[mi].vertices.size());
        for(std::uint32_t vi=0;vi<renderIndex[mi].size();++vi){renderIndex[mi][vi]=static_cast<std::uint32_t>(positions.size());positions.push_back(Point(garment.skins[mi].bindTransform,garment.geometry->meshes[mi].vertices[vi].position));}
    }
    DisjointSet welded(positions.size());WeldComponents(welded,positions,options.weldTolerance);
    std::map<std::uint32_t,std::uint32_t> weldedIndices;std::vector<std::uint32_t> particleIndex(positions.size());
    for(std::uint32_t i=0;i<positions.size();++i){auto [entry,inserted]=weldedIndices.emplace(welded.Find(i),static_cast<std::uint32_t>(weldedIndices.size()));particleIndex[i]=entry->second;if(inserted){ClothParticle p;p.restPosition=positions[i];cloth.particles.push_back(p);}}
    for(std::uint32_t mi=0;mi<renderIndex.size();++mi)for(std::uint32_t vi=0;vi<renderIndex[mi].size();++vi)cloth.renderBindings.push_back({mi,vi,particleIndex[renderIndex[mi][vi]]});
    std::map<std::pair<std::uint32_t,std::uint32_t>,std::vector<std::uint32_t>> opposite;
    std::set<std::array<std::uint32_t,3>> seenTriangles;
    for(std::uint32_t mi=0;mi<garment.geometry->meshes.size();++mi){const auto& indices=garment.geometry->meshes[mi].indices;
        for(std::size_t t=0;t<indices.size();t+=3){std::array<std::uint32_t,3> tri;for(int c=0;c<3;++c)tri[c]=particleIndex[renderIndex[mi][indices[t+c]]];
            if(tri[0]==tri[1]||tri[1]==tri[2]||tri[2]==tri[0])continue;
            const auto cross=glm::cross(cloth.particles[tri[1]].restPosition-cloth.particles[tri[0]].restPosition,cloth.particles[tri[2]].restPosition-cloth.particles[tri[0]].restPosition);
            if(glm::dot(cross,cross)<=1e-24)continue;
            auto key=tri;std::sort(key.begin(),key.end());if(!seenTriangles.insert(key).second)continue;cloth.triangles.push_back(tri);
            for(int c=0;c<3;++c){auto a=tri[c],b=tri[(c+1)%3];if(a>b)std::swap(a,b);opposite[{a,b}].push_back(tri[(c+2)%3]);}
        }
    }
    Require(!cloth.triangles.empty(),"The extracted garment has no nondegenerate simulation triangles.");
    DisjointSet components(cloth.particles.size());std::vector<std::vector<std::pair<std::uint32_t,double>>> adjacency(cloth.particles.size());
    std::set<std::pair<std::uint32_t,std::uint32_t>> bending;
    for(const auto& [edge,others]:opposite){
        const auto [a,b]=edge;const double length=glm::length(cloth.particles[a].restPosition-cloth.particles[b].restPosition);if(length<=1e-12)continue;
        cloth.stretch.push_back({a,b,length});components.Join(a,b);adjacency[a].push_back({b,length});adjacency[b].push_back({a,length});
        for(std::size_t i=0;i<others.size();++i)for(std::size_t j=i+1;j<others.size();++j){auto x=others[i],y=others[j];if(x>y)std::swap(x,y);if(x!=y)bending.insert({x,y});}
    }
    for(const auto& [a,b]:bending){const double length=glm::length(cloth.particles[a].restPosition-cloth.particles[b].restPosition);if(length>1e-12)cloth.bend.push_back({a,b,length});}
    std::map<std::uint32_t,std::vector<std::uint32_t>> groups;for(std::uint32_t i=0;i<cloth.particles.size();++i)groups[components.Find(i)].push_back(i);
    const int hips=RoleBone(source,"hips");double waist=-std::numeric_limits<double>::infinity();
    cloth.motionBone=hips>=0?hips:0;cloth.motionInverseBind=source.skeleton.bones[cloth.motionBone].inverseBind;
    if(hips>=0)waist=global[hips][3].y;
    // Semantic hips are only a fallback. A dress's authored attachment seam can
    // be substantially above the pelvis, particularly for an apron panel.
    for(std::size_t i=0;i<skirtBones.size();++i)if(skirtBones[i]&&(source.skeleton.bones[i].parent<0||!skirtBones[source.skeleton.bones[i].parent]))waist=std::max(waist,global[i][3].y);
    if(std::isfinite(waist))waist+=height*.01;
    std::size_t pinned=0;double actualLowestPin=std::numeric_limits<double>::infinity();
    for(const auto& [component,vertices]:groups){
        double lo=std::numeric_limits<double>::infinity(),hi=-lo;for(auto i:vertices){lo=std::min(lo,cloth.particles[i].restPosition.y);hi=std::max(hi,cloth.particles[i].restPosition.y);}
        double band=std::isfinite(waist)?waist:hi-(hi-lo)*options.waistPinFraction;
        const auto attachment=UpperAttachmentRing(cloth,vertices,opposite,height);
        if(attachment){
            band=attachment->bounds.minimum.y-options.weldTolerance*2;
            std::ostringstream diagnostic;diagnostic<<"Closed upper attachment boundary: "<<attachment->vertices.size()<<" vertices, Y="<<attachment->bounds.minimum.y<<".."<<attachment->bounds.maximum.y<<"; only this authored boundary and its refined edge points are fixed. The semantic pelvis is below the authored clothing seam.";
            cloth.diagnostics.push_back(diagnostic.str());
        }else{
            // Detached accessories have their own top anchor. This also keeps
            // a component entirely below the semantic waist from falling off.
            band=std::min(band,hi-std::max(options.weldTolerance*2,(hi-lo)*.015));
            std::ostringstream diagnostic;diagnostic<<"Semantic waist fallback: no unambiguous broad closed upper boundary; fixed band Y >= "<<band<<" source units.";cloth.diagnostics.push_back(diagnostic.str());
        }
        const std::set<std::uint32_t> exactBoundary=attachment?std::set<std::uint32_t>(attachment->vertices.begin(),attachment->vertices.end()):std::set<std::uint32_t>{};
        for(auto i:vertices){auto& p=cloth.particles[i];
            const bool fixed=attachment?exactBoundary.contains(i):p.restPosition.y>=band;
            if(fixed){p.inverseMass=0;p.maxDistance=0;p.animationDrive=1;++pinned;actualLowestPin=std::min(actualLowestPin,p.restPosition.y);}
            else{p.inverseMass=1;p.maxDistance=cloth.unitsPerMeter*.75;p.animationDrive=0;}
        }
    }
    using QueueEntry=std::pair<double,std::uint32_t>;std::priority_queue<QueueEntry,std::vector<QueueEntry>,std::greater<QueueEntry>> pending;
    std::vector<double> distances(cloth.particles.size(),std::numeric_limits<double>::infinity());std::vector<std::uint32_t> anchor(cloth.particles.size(),Missing);
    for(std::uint32_t i=0;i<cloth.particles.size();++i)if(cloth.particles[i].inverseMass==0){distances[i]=0;anchor[i]=i;pending.push({0,i});}
    while(!pending.empty()){const auto [distance,i]=pending.top();pending.pop();if(distance>distances[i])continue;
        for(const auto& [neighbor,length]:adjacency[i])if(distance+length<distances[neighbor]){distances[neighbor]=distance+length;anchor[neighbor]=anchor[i];pending.push({distances[neighbor],neighbor});}
    }
    double longestDistance=0;for(auto distance:distances)if(std::isfinite(distance))longestDistance=std::max(longestDistance,distance);
    const double transition=std::max(cloth.unitsPerMeter*.025,longestDistance*options.waistPinFraction);
    for(std::uint32_t i=0;i<cloth.particles.size();++i)if(cloth.particles[i].inverseMass>0){
        Require(anchor[i]!=Missing&&std::isfinite(distances[i]),"Garment component has no reachable pinned anchor.");cloth.tethers.push_back({i,anchor[i],distances[i]});
        auto& particle=cloth.particles[i];
        // Max Distance is a safety envelope around one rigid waist frame, not
        // an actuator pulling the hem back to animated thigh/skirt bones.
        particle.maxDistance=std::max(cloth.unitsPerMeter*.75,distances[i]*1.6);
        const double weight=glm::clamp(1-distances[i]/transition,0.0,1.0);particle.animationDrive=.08*weight*weight;
    }
    std::vector<double> particleAreas(cloth.particles.size(),0);double totalArea=0;
    for(const auto& tri:cloth.triangles){const double area=glm::length(glm::cross(cloth.particles[tri[1]].restPosition-cloth.particles[tri[0]].restPosition,cloth.particles[tri[2]].restPosition-cloth.particles[tri[0]].restPosition))*.5/(cloth.unitsPerMeter*cloth.unitsPerMeter);totalArea+=area;for(auto i:tri)particleAreas[i]+=area/3;}
    for(std::size_t i=0;i<cloth.particles.size();++i)if(cloth.particles[i].inverseMass>0){Require(particleAreas[i]>0,"Cloth particle has no incident surface area.");cloth.particles[i].inverseMass=1/std::max(1e-8,particleAreas[i]*options.surfaceDensity);}
    std::ostringstream description;description<<"Attachment anchors: "<<pinned<<" / "<<cloth.particles.size()<<" particles pinned across "<<groups.size()<<" connected components; lowest fixed Y="<<actualLowestPin<<" source units; semantic waist fallback Y="<<(std::isfinite(waist)?waist:actualLowestPin)<<'.';cloth.diagnostics.push_back(description.str());
    {std::ostringstream diagnostic;diagnostic<<"Area-weighted mass: "<<totalArea<<" m^2 at "<<options.surfaceDensity<<" kg/m^2, total surface mass "<<totalArea*options.surfaceDensity<<" kg; fixed attachment particles remain kinematic.";cloth.diagnostics.push_back(diagnostic.str());}
    cloth.diagnostics.push_back("Free cloth reference follows the actual skinned attachment-ring fit; waist bone "+source.skeleton.bones[cloth.motionBone].name+" supplies the fallback, independently of thigh/skirt animation. Animation drive fades to zero below the attachment transition; broad max-distance envelopes permit natural drag and sag.");
    cloth.diagnostics.push_back("Render seams are welded only for simulation. Refined triangles support area and signed-dihedral bending constraints, edge stretch, geodesic anchor tethers, max-distance and attachment-drive maps.");
    cloth.diagnostics.push_back("XPBD constraint settings are an independent implementation inspired by public Chaos Cloth concepts; source PMX rigid bodies are not reused as vertex-cloth constraints.");
    GenerateCapsules(body,global,cloth,height);GenerateConvexes(body,global,cloth,height);cloth.topologyHash=ClothTopologyHash(garment);return cloth;
}
} // namespace

std::optional<ModularCharacterBundle> BuildSkirtGarment(const SkeletalAssetPackage& source,const GarmentBuildOptions& options,std::string* error){
    try{
        std::string detail;const bool sourceValid=ValidateSkeletalAsset(source,&detail);Require(sourceValid,"Invalid source skeletal asset: "+detail);
        Require(std::isfinite(options.skirtWeightThreshold)&&options.skirtWeightThreshold>0&&options.skirtWeightThreshold<=1,"Skirt weight threshold must be in (0, 1].");
        Require(std::isfinite(options.waistPinFraction)&&options.waistPinFraction>0&&options.waistPinFraction<1,"Waist pin fraction must be in (0, 1).");
        Require(std::isfinite(options.weldTolerance)&&options.weldTolerance>0,"Weld tolerance must be positive and finite.");
        Require(options.refinementLevels<=2,"Garment refinement levels must be in [0, 2].");
        Require(std::isfinite(options.surfaceDensity)&&options.surfaceDensity>=.01&&options.surfaceDensity<=5,"Garment surface density must be in [0.01, 5] kg/m^2.");
        Require(!options.name.empty()&&!options.slot.empty(),"A garment name and slot are required.");
        const auto& geometry=*source.geometry;Require(geometry.bounds.valid,"The source asset needs valid model bounds.");
        const double height=geometry.bounds.Extent().y;Require(std::isfinite(height)&&height>1e-8,"The source asset needs a nonzero Y-up height.");
        std::set<std::uint32_t> materialRestriction(options.materialIndices.begin(),options.materialIndices.end());
        for(auto index:materialRestriction)Require(index<geometry.materials.size(),"Garment material restriction is out of range.");
        std::vector<bool> skirtBones(source.skeleton.bones.size(),false);for(std::size_t i=0;i<skirtBones.size();++i)skirtBones[i]=SkirtBone(source.skeleton.bones[i]);
        const bool hasSkirtBones=std::find(skirtBones.begin(),skirtBones.end(),true)!=skirtBones.end();
        Require(hasSkirtBones||!materialRestriction.empty(),"No skirt bones were recognized; specify explicit garment material indices.");
        const auto global=GlobalBoneTransforms(source.skeleton,RestPose(source.skeleton));const int hips=RoleBone(source,"hips");
        std::vector<std::vector<bool>> selected(geometry.meshes.size());std::uint64_t garmentTriangles=0;
        ModularCharacterBundle bundle;
        for(std::uint32_t mi=0;mi<geometry.meshes.size();++mi){const auto& mesh=geometry.meshes[mi];selected[mi].assign(mesh.indices.size()/3,false);
            if(!materialRestriction.empty()&&!materialRestriction.contains(mesh.materialIndex))continue;
            const auto positions=Positions(mesh,source.skins[mi]);std::vector<double> weights(positions.size(),0);bool anySeed=false;
            for(std::size_t vi=0;vi<weights.size();++vi)ForEachSkinInfluence(source.skins[mi],vi,[&](std::uint32_t joint,float weight){if(joint<skirtBones.size()&&skirtBones[joint]){weights[vi]+=weight;if(weights[vi]>=options.skirtWeightThreshold)anySeed=true;}});
            const bool explicitMaterial=!materialRestriction.empty()&&!hasSkirtBones;if(!anySeed&&!explicitMaterial)continue;
            DisjointSet components(positions.size());for(std::size_t t=0;t<mesh.indices.size();t+=3){components.Join(mesh.indices[t],mesh.indices[t+1]);components.Join(mesh.indices[t],mesh.indices[t+2]);}
            // Keep authored component boundaries while extracting. A bodice
            // and a detachable skirt can have coincident seam vertices; welding
            // them here would pull the bodice into the garment selection.
            // Simulation welding happens only after the partition is complete.
            struct Region{bool seed=false;double lo=std::numeric_limits<double>::infinity(),hi=-std::numeric_limits<double>::infinity(),weightedHi=-std::numeric_limits<double>::infinity();std::size_t triangles=0;};
            std::map<std::uint32_t,Region> regions;
            for(std::uint32_t vi=0;vi<positions.size();++vi){auto& region=regions[components.Find(vi)];region.lo=std::min(region.lo,positions[vi].y);region.hi=std::max(region.hi,positions[vi].y);if(weights[vi]>=options.skirtWeightThreshold){region.seed=true;region.weightedHi=std::max(region.weightedHi,positions[vi].y);}}
            for(std::size_t t=0;t<mesh.indices.size()/3;++t){const auto root=components.Find(mesh.indices[t*3]);auto& region=regions[root];if(!region.seed&&!explicitMaterial)continue;
                bool include=true;
                // A continuous dress may share vertices between skirt and
                // bodice. A lower-body envelope keeps torso/arms in the body;
                // disconnected weighted skirt components need no geometric cut.
                if(region.seed){const double upper=std::max((hips>=0?global[hips][3].y:region.weightedHi)+height*.10,region.weightedHi+height*.035);
                    if(region.hi>upper+height*.045&&region.hi-region.lo>height*.30){const double center=(positions[mesh.indices[t*3]].y+positions[mesh.indices[t*3+1]].y+positions[mesh.indices[t*3+2]].y)/3;include=center<=upper;}
                }
                if(include){selected[mi][t]=true;++region.triangles;++garmentTriangles;}
            }
            for(const auto& [component,region]:regions)if(region.triangles){std::ostringstream description;description<<"Mesh "<<mi<<" ("<<geometry.materials[mesh.materialIndex].name<<"): extracted "<<region.triangles<<" triangles in connected skirt region Y="<<region.lo<<".."<<region.hi<<'.';bundle.diagnostics.push_back(description.str());}
        }
        Require(garmentTriangles>0,"No garment triangles matched the skirt bone weights/material restriction.");
        Require(garmentTriangles<geometry.TriangleCount(),"Garment extraction must leave body geometry; restrict the selected materials.");
        auto body=ExtractPart(source,selected,false,options.name);auto garment=ExtractPart(source,selected,true,options.name);bundle.body=body.asset;bundle.garment=garment.asset;
        Require(bundle.body->geometry->TriangleCount()+bundle.garment->geometry->TriangleCount()==geometry.TriangleCount(),"Garment partition did not preserve the source triangle count.");
        const auto originalGarmentTriangles=bundle.garment->geometry->TriangleCount();
        RefineGarment(*bundle.garment,options.refinementLevels);
        const bool bodyValid=ValidateSkeletalAsset(*bundle.body,&detail);Require(bodyValid,"Invalid extracted body: "+detail);
        const bool garmentValid=ValidateSkeletalAsset(*bundle.garment,&detail);Require(garmentValid,"Invalid extracted garment: "+detail);
        const bool compatible=GarmentCompatible(*bundle.body,*bundle.garment,&detail);Require(compatible,"Extracted garment is incompatible with its body: "+detail);
        bundle.cloth=GenerateCloth(source,*bundle.garment,*bundle.body,skirtBones,global,options,height);
        const bool clothValid=ValidateClothAsset(bundle.cloth,*bundle.garment,&detail);Require(clothValid,"Generated cloth failed validation: "+detail);
        bundle.assembly.id=source.id+"/modular-character";bundle.assembly.name=source.name+"_Modular";bundle.assembly.skeletonId=source.skeleton.id;bundle.assembly.bodyPackage="Body/Package.asset.json";
        bundle.assembly.slots.push_back({options.slot,bundle.garment->id,"Garments/Skirt/Package.asset.json","Cloth/Skirt.cloth.json",true});
        bundle.diagnostics.push_back("Source triangles partitioned exactly once before refinement: "+std::to_string(geometry.TriangleCount())+" = body "+std::to_string(bundle.body->geometry->TriangleCount())+" + original garment "+std::to_string(originalGarmentTriangles)+".");
        bundle.diagnostics.push_back("Garment midpoint refinement: "+std::to_string(options.refinementLevels)+" level(s), "+std::to_string(originalGarmentTriangles)+" -> "+std::to_string(bundle.garment->geometry->TriangleCount())+" triangles; body geometry is unchanged.");
        for(const auto& diagnostic:bundle.diagnostics)bundle.cloth.diagnostics.push_back(diagnostic);if(error)error->clear();return bundle;
    }catch(const std::exception& exception){if(error)*error=exception.what();return std::nullopt;}
}
} // namespace Model
