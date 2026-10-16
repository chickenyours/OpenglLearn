#include "hair_surface_contact.h"
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <tuple>

namespace Model {
namespace {
bool Finite(glm::dvec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
bool Fail(std::string* error,const std::string& message){if(error)*error=message;return false;}
glm::dmat4 Instance(const StaticModel& model,std::uint32_t mesh){for(const auto& i:model.instances)if(i.meshIndex==mesh)return i.transform;return glm::dmat4(1);}
glm::dquat NormalRotation(glm::dvec3 a,glm::dvec3 b){
    if(glm::dot(a,a)<1e-24||glm::dot(b,b)<1e-24)return {1,0,0,0};
    a=glm::normalize(a);b=glm::normalize(b);const double cosine=std::clamp(glm::dot(a,b),-1.,1.);
    if(cosine>1.-1e-12)return {1,0,0,0};
    if(cosine< -1.+1e-10){auto axis=glm::cross(a,std::abs(a.x)<.8?glm::dvec3(1,0,0):glm::dvec3(0,1,0));return glm::angleAxis(glm::pi<double>(),glm::normalize(axis));}
    return glm::normalize(glm::dquat(1.+cosine,glm::cross(a,b)));
}
void SurfaceCounters(HairSimulationStats& stats,std::size_t vertices,std::size_t clusters){
    stats.surfaceVertices=vertices;stats.surfaceClusters=clusters;stats.surfaceContacts=stats.surfaceSweeps=stats.surfaceBroadphaseRejects=0;
    stats.surfaceMilliseconds=stats.maxSurfacePenetration=stats.maxMeshCorrection=0;
}
}

bool ApplyHairSurfaceOffsets(const SkeletalAssetPackage& target,const SkeletalPose& pose,
    std::span<const HairSurfaceOffset> offsets,StaticModel& output,std::string* error){
    if(error)error->clear();if(offsets.empty())return true;
    if(!target.geometry||output.meshes.size()!=target.geometry->meshes.size())return Fail(error,"Hair surface output layout does not match target");
    // Validate before modifying output, including duplicate records. Displacements
    // live in anchor local space; the renderer still owns mesh-local vertices.
    std::map<std::uint32_t,std::vector<std::pair<std::uint32_t,glm::dvec3>>> changes;
    std::set<std::pair<std::uint32_t,std::uint32_t>> unique;
    std::map<std::uint32_t,glm::dmat4> inverseInstances;
    std::map<std::pair<std::uint32_t,std::uint32_t>,glm::dmat3> offsetMatrices;
    for(const auto& o:offsets){
        if(o.mesh>=output.meshes.size()||o.vertex>=output.meshes[o.mesh].vertices.size()||o.anchorBone>=pose.global.size()||!Finite(o.displacement))return Fail(error,"Invalid hair surface offset");
        if(output.meshes[o.mesh].vertices.size()!=target.geometry->meshes[o.mesh].vertices.size()||!unique.emplace(o.mesh,o.vertex).second)return Fail(error,"Duplicate or incompatible hair surface offset");
        if(!inverseInstances.contains(o.mesh)){const auto instance=Instance(*target.geometry,o.mesh);const double determinant=glm::determinant(instance);
            if(!std::isfinite(determinant)||std::abs(determinant)<1e-15)return Fail(error,"Singular hair mesh instance");inverseInstances.emplace(o.mesh,glm::inverse(instance));}
        const auto key=std::pair{o.mesh,o.anchorBone};if(!offsetMatrices.contains(key))offsetMatrices.emplace(key,glm::dmat3(inverseInstances.at(o.mesh)*pose.global[o.anchorBone]));
        const auto delta=offsetMatrices.at(key)*o.displacement;
        if(!Finite(delta))return Fail(error,"Nonfinite transformed hair surface offset");
        changes[o.mesh].emplace_back(o.vertex,delta);
    }
    for(const auto& [m,records]:changes){
        auto& mesh=output.meshes[m];const std::size_t count=mesh.vertices.size();
        std::vector<glm::dvec3> delta(count,glm::dvec3(0)),before(count,glm::dvec3(0)),after(count,glm::dvec3(0));
        std::vector<bool> changed(count,false),normalChanged(count,false);
        for(const auto& [v,d]:records){delta[v]=d;changed[v]=glm::dot(d,d)>1e-30;mesh.vertices[v].position+=d;}
        for(std::size_t t=0;t+2<mesh.indices.size();t+=3){const auto a=mesh.indices[t],b=mesh.indices[t+1],c=mesh.indices[t+2];if(a>=count||b>=count||c>=count)continue;
            if(changed[a]||changed[b]||changed[c])normalChanged[a]=normalChanged[b]=normalChanged[c]=true;
        }
        // Rotate supplied artist normals by the change in their full geometric
        // fan instead of replacing stylized normals with geometric normals.
        for(std::size_t t=0;t+2<mesh.indices.size();t+=3){const auto a=mesh.indices[t],b=mesh.indices[t+1],c=mesh.indices[t+2];if(a>=count||b>=count||c>=count)continue;
            if(!normalChanged[a]&&!normalChanged[b]&&!normalChanged[c])continue;
            const auto pa=mesh.vertices[a].position,pb=mesh.vertices[b].position,pc=mesh.vertices[c].position;
            const auto old=glm::cross(pb-delta[b]-pa+delta[a],pc-delta[c]-pa+delta[a]),now=glm::cross(pb-pa,pc-pa);
            for(auto v:{a,b,c})if(normalChanged[v]){before[v]+=old;after[v]+=now;}
        }
        mesh.bounds={};for(std::size_t v=0;v<count;++v){auto& vertex=mesh.vertices[v];
            if(normalChanged[v]){const auto rotation=NormalRotation(before[v],after[v]);auto normal=rotation*glm::dvec3(vertex.normal),tangent=rotation*glm::dvec3(vertex.tangent);
                if(glm::dot(normal,normal)>1e-24)normal=glm::normalize(normal);tangent-=normal*glm::dot(tangent,normal);
                vertex.normal=glm::vec3(normal);if(glm::dot(tangent,tangent)>1e-24)vertex.tangent=glm::vec4(glm::vec3(glm::normalize(tangent)),vertex.tangent.w);
            }mesh.bounds.Include(vertex.position);
        }
    }
    output.bounds={};
    if(output.instances.empty()){for(const auto& mesh:output.meshes)if(mesh.bounds.valid){output.bounds.Include(mesh.bounds.minimum);output.bounds.Include(mesh.bounds.maximum);}}
    for(const auto& i:output.instances)if(i.meshIndex<output.meshes.size()){const auto& b=output.meshes[i.meshIndex].bounds;if(!b.valid)continue;
        for(int c=0;c<8;++c)output.bounds.Include(glm::dvec3(i.transform*glm::dvec4(c&1?b.maximum.x:b.minimum.x,c&2?b.maximum.y:b.minimum.y,c&4?b.maximum.z:b.minimum.z,1)));
    }
    return true;
}

namespace detail {
struct HairSurfaceContact::Impl {
    struct Influence{std::uint32_t bone;double weight;glm::dvec3 position;glm::dmat3 linear;};
    struct Morph{std::uint32_t index;glm::dvec3 delta;};
    struct OutsideCertificate{glm::dvec3 point{0};std::uint64_t epoch=0;double gap=0;bool valid=false,backfacing=false;};
    struct Node{
        std::uint32_t mesh=0,vertex=0,anchor=0;std::vector<Influence> influences;std::vector<Morph> morphs;
        glm::dmat4 fromSkeleton{1};glm::dvec3 rest{0},source{0},current{0},previous{0},offset{0},previousSource{0};glm::dmat3 previousAnchor{1};
        std::vector<std::uint32_t> adjacent;std::size_t fixedAdjacent=0;std::uint32_t hint=UINT32_MAX,surfaceHint=UINT32_MAX,seamGroup=UINT32_MAX;bool unresolved=false;
        std::uint64_t revision=0,endpointRevision=UINT64_MAX;
        OutsideCertificate outside;
    };
    struct Cluster{std::size_t begin=0,end=0;};
    struct Face{std::array<std::uint32_t,3> nodes;glm::dvec3 rest{0};std::uint32_t hint=UINT32_MAX,surfaceHint=UINT32_MAX;bool unresolved=false,evaluated=false;std::array<std::uint64_t,3> revisions{};OutsideCertificate outside;};
    struct SweepMemo{bool valid=false,pendingIn=false,pendingOut=false;glm::dvec3 previous{0},candidate{0},result{0};std::uint32_t hint=UINT32_MAX;OutsideCertificate outside;};
    struct ProfileRecord{std::uint64_t calls=0,nearest=0,sweep=0,anyback=0,rays=0;double milliseconds=0;};
    enum Phase{EvaluatePhase,MovingPhase,SmoothPhase,Face1Phase,Face2Phase,FinalPhase,PublishPhase,PhaseCount};
    bool reuse=true,profile=false,conservativeGate=true,staticContacts=true;std::array<ProfileRecord,PhaseCount> profileRecords{};
    Impl(){const auto* option=std::getenv("MODEL_HAIR_SURFACE_REUSE");reuse=!(option&&option[0]=='0'&&option[1]=='\0');const auto* tracing=std::getenv("MODEL_HAIR_PROFILE");profile=tracing&&tracing[0]=='1'&&tracing[1]=='\0';const auto* gate=std::getenv("MODEL_HAIR_CONSERVATIVE_GATE");conservativeGate=!(gate&&gate[0]=='0'&&gate[1]=='\0');const auto* steady=std::getenv("MODEL_HAIR_SURFACE_STATIC_CONTACTS");staticContacts=!(steady&&steady[0]=='0'&&steady[1]=='\0');}
    ~Impl(){
        if(!profile||profileRecords[PublishPhase].calls==0)return;
        static constexpr const char* names[]{"Evaluate","MovingSweep","Smooth+Weld","Face1","Face2","FinalGuard","Publish"};
        std::fprintf(stderr,"HairSurface profile reuse=%d conservativeGate=%d staticContacts=%d vertices=%zu faces=%zu seamGroups=%zu physicalCalls=%llu\n",reuse?1:0,conservativeGate?1:0,staticContacts?1:0,nodes.size(),faces.size(),seamGroups.size(),static_cast<unsigned long long>(profileRecords[PublishPhase].calls));
        for(int i=0;i<PhaseCount;++i){const auto& p=profileRecords[i];std::fprintf(stderr,"  %-12s mean_ms=%.6f total_ms=%.3f scopes=%llu nearest=%llu sweep=%llu anyback=%llu rays=%llu\n",names[i],p.milliseconds/double(profileRecords[PublishPhase].calls),p.milliseconds,static_cast<unsigned long long>(p.calls),static_cast<unsigned long long>(p.nearest),static_cast<unsigned long long>(p.sweep),static_cast<unsigned long long>(p.anyback),static_cast<unsigned long long>(p.rays));}
    }
    struct PhaseScope{
        Impl& owner;Phase phase;const HairBodyCollision& body;std::chrono::steady_clock::time_point start;HairBodyQueryStats before{};
        PhaseScope(Impl& value,Phase index,const HairBodyCollision& collider):owner(value),phase(index),body(collider){if(owner.profile){start=std::chrono::steady_clock::now();before=body.QueryStats();}}
        ~PhaseScope(){if(!owner.profile)return;const auto& after=body.QueryStats();auto& p=owner.profileRecords[phase];++p.calls;p.milliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();p.nearest+=after.nearestQueries-before.nearestQueries;p.sweep+=after.sweepQueries-before.sweepQueries;p.anyback+=after.nearbyBackfaceQueries-before.nearbyBackfaceQueries;p.rays+=after.rayQueries-before.rayQueries;}
    };
    const HairAsset* asset=nullptr;const SkeletalAssetPackage* target=nullptr;double units=1;
    bool ready=false,collisionEnabled=false;std::vector<Node> nodes;std::vector<std::uint32_t> order;std::vector<Cluster> clusters;
    std::vector<std::vector<std::uint32_t>> seamGroups;
    std::vector<SweepMemo> sweepMemos,smoothMemos;
    std::uint64_t contactEpoch=0;
    const HairBodyCollision* previousBody=nullptr;std::uint64_t previousBodyRevision=0;double previousMargin=0;
    std::vector<Face> faces;std::vector<std::uint32_t> faceOrder;std::vector<Cluster> faceClusters;
    std::vector<std::vector<std::int32_t>> mapping;std::vector<HairSurfaceOffset> offsets;std::vector<glm::dvec3> smooth;

    bool Evaluate(const SkeletalPose& pose,std::string* error,double* maximumSourceMotion=nullptr){
        if(maximumSourceMotion)*maximumSourceMotion=0;
        for(auto& n:nodes){if(n.anchor>=pose.global.size())return Fail(error,"Hair surface anchor pose is missing");
            const glm::dmat3 anchor(pose.global[n.anchor]);if(!std::isfinite(glm::determinant(anchor))||std::abs(glm::determinant(anchor))<1e-15)return Fail(error,"Singular hair surface anchor");
            glm::dvec3 morph(0);for(const auto& m:n.morphs)if(m.index<pose.morphWeights.size()&&std::isfinite(pose.morphWeights[m.index]))morph+=m.delta*double(pose.morphWeights[m.index]);
            glm::dvec3 p(0);for(const auto& i:n.influences){if(i.bone>=pose.global.size())return Fail(error,"Hair surface skin pose is missing");p+=glm::dvec3(pose.global[i.bone]*glm::dvec4(i.position+i.linear*morph*i.weight,i.weight));}
            const auto source=glm::dvec3(n.fromSkeleton*glm::dvec4(p,1))/units;if(!Finite(source))return Fail(error,"Nonfinite skinned hair surface point");
            if(maximumSourceMotion)*maximumSourceMotion=std::max(*maximumSourceMotion,glm::length(source-n.source));n.source=source;
        }return true;
    }
    void Partition(std::size_t begin,std::size_t end){
        if(end-begin<=32){clusters.push_back({begin,end});return;}
        glm::dvec3 lo(std::numeric_limits<double>::infinity()),hi(-std::numeric_limits<double>::infinity());for(auto i=begin;i<end;++i){lo=glm::min(lo,nodes[order[i]].rest);hi=glm::max(hi,nodes[order[i]].rest);}
        const auto extent=hi-lo;int axis=extent.y>extent.x?1:0;if(extent.z>extent[axis])axis=2;const auto middle=begin+(end-begin)/2;
        std::nth_element(order.begin()+begin,order.begin()+middle,order.begin()+end,[&](auto a,auto b){const double x=nodes[a].rest[axis],y=nodes[b].rest[axis];return x==y?a<b:x<y;});Partition(begin,middle);Partition(middle,end);
    }
    void PartitionFaces(std::size_t begin,std::size_t end){
        if(end-begin<=32){faceClusters.push_back({begin,end});return;}
        glm::dvec3 lo(std::numeric_limits<double>::infinity()),hi(-std::numeric_limits<double>::infinity());for(auto i=begin;i<end;++i){lo=glm::min(lo,faces[faceOrder[i]].rest);hi=glm::max(hi,faces[faceOrder[i]].rest);}
        const auto extent=hi-lo;int axis=extent.y>extent.x?1:0;if(extent.z>extent[axis])axis=2;const auto middle=begin+(end-begin)/2;
        std::nth_element(faceOrder.begin()+begin,faceOrder.begin()+middle,faceOrder.begin()+end,[&](auto a,auto b){const double x=faces[a].rest[axis],y=faces[b].rest[axis];return x==y?a<b:x<y;});PartitionFaces(begin,middle);PartitionFaces(middle,end);
    }
    // This residual is measured only for points actually queried by this pass.
    // The independent quality tool must scan the whole rendered free surface.
    void Project(Node& n,const HairBodyCollision& body,double margin,HairSimulationStats& stats,bool unbounded,bool insideOnly=false){
        if(reuse&&n.outside.valid&&n.outside.epoch==contactEpoch&&n.outside.point==n.current&&
            (insideOnly||n.outside.backfacing||n.outside.gap>=margin-1e-9)){n.unresolved=false;return;}
        bool unresolved=n.unresolved;double residual=0;
        for(int iteration=0;iteration<8;++iteration){const bool firstInsideOnly=iteration==0&&insideOnly;
            const auto nearest=firstInsideOnly?body.NearestInside(n.current,&n.hint):body.Nearest(n.current,unbounded?std::numeric_limits<double>::infinity():.06,&n.hint);
            if(firstInsideOnly&&!nearest.found&&Finite(n.current)){
                // NearestInside verifies the same finite enclosure rule and
                // zero-distance boundary exception as Nearest. Outside needs
                // no global closest surface, so its certificate has no known
                // clearance or backface value and is usable only inside-only.
                n.outside={n.current,contactEpoch,0,true,false};n.unresolved=false;return;
            }
            if(!nearest.found)break;
            n.outside.valid=!nearest.inside;if(n.outside.valid){n.outside.point=n.current;n.outside.epoch=contactEpoch;n.outside.gap=nearest.distance;n.outside.backfacing=nearest.backfacing;}
            residual=nearest.inside?nearest.distance:0;unresolved=nearest.inside;
            if(iteration==0&&insideOnly&&!nearest.inside)break;
            if(!nearest.inside&&(nearest.backfacing||nearest.distance>=margin-1e-9))break;
            const double distance=nearest.inside?nearest.distance+margin:margin-nearest.distance;
            if(distance<=1e-10)break;n.current+=nearest.normal*distance;++stats.surfaceContacts;
            if(iteration==7){const auto final=body.Nearest(n.current,std::numeric_limits<double>::infinity(),&n.hint);if(final.found){unresolved=final.inside;residual=final.inside?final.distance:0;n.outside.valid=!final.inside;if(n.outside.valid){n.outside.point=n.current;n.outside.epoch=contactEpoch;n.outside.gap=final.distance;n.outside.backfacing=final.backfacing;}}}
        }
        n.unresolved=unresolved;stats.maxSurfacePenetration=std::max(stats.maxSurfacePenetration,residual);
    }
    void Sweep(Node& n,glm::dvec3 previous,const HairBodyCollision& body,double margin,bool moving,HairSimulationStats& stats){
        ++stats.surfaceSweeps;const auto hit=body.SweepContact(previous,n.current,margin,moving);
        if(hit.hit){n.current+=hit.correction;n.hint=hit.triangle;++stats.surfaceContacts;Project(n,body,margin,stats,true);}
        else if(n.unresolved)Project(n,body,margin,stats,true);
    }
    void MemoSweep(Node& n,glm::dvec3 previous,const HairBodyCollision& body,double margin,bool moving,std::vector<SweepMemo>& memos,HairSimulationStats& stats){
        if(!reuse||n.seamGroup==UINT32_MAX){Sweep(n,previous,body,margin,moving,stats);return;}
        auto& memo=memos[n.seamGroup];const auto candidate=n.current;const bool pending=n.unresolved;
        if(memo.valid&&memo.previous==previous&&memo.candidate==candidate&&memo.pendingIn==pending){n.current=memo.result;n.unresolved=memo.pendingOut;n.hint=memo.hint;n.outside=memo.outside;return;}
        Sweep(n,previous,body,margin,moving,stats);
        if(!memo.valid){memo.valid=true;memo.previous=previous;memo.candidate=candidate;memo.pendingIn=pending;memo.result=n.current;memo.pendingOut=n.unresolved;memo.hint=n.hint;memo.outside=n.outside;}
    }
    void Smooth(const HairBodyCollision& body,double margin,bool collision,HairSimulationStats& stats){
        smooth.resize(nodes.size());
        for(std::size_t i=0;i<nodes.size();++i){const auto& n=nodes[i];auto sum=glm::dvec3(0);for(auto j:n.adjacent)sum+=nodes[j].current-nodes[j].source;const auto count=n.adjacent.size()+n.fixedAdjacent;
            const auto original=n.current-n.source;smooth[i]=n.source+(count?glm::mix(original,sum/double(count),.18):original);
        }
        for(std::size_t i=0;i<nodes.size();++i){auto& n=nodes[i];const auto before=n.current;
            if(glm::dot(smooth[i]-before,smooth[i]-before)<1e-24)continue;n.current=smooth[i];
            if(collision&&body.MayIntersectSweptBounds(glm::min(before,n.current),glm::max(before,n.current),margin,false))MemoSweep(n,before,body,margin,false,smoothMemos,stats);
            else if(collision&&n.unresolved)Project(n,body,margin,stats,true);
        }
    }
    void WeldSeams(const HairBodyCollision& body,double margin,bool collision,HairSimulationStats& stats){
        // One contact position per matching skin/morph trajectory. Merely
        // adding seam adjacency permits different triangle fans to pull the
        // duplicate corners apart in tangential directions.
        for(const auto& group:seamGroups){
            if(reuse){bool identical=true,pending=false;const auto point=nodes[group.front()].current;for(auto i:group){identical&=nodes[i].current==point;pending|=nodes[i].unresolved;}if(identical&&!pending){for(auto i:group)if(nodes[i].outside.valid&&nodes[i].outside.epoch==contactEpoch&&nodes[i].outside.point==point){const auto certificate=nodes[i].outside;for(auto j:group)nodes[j].outside=certificate;break;}continue;}}
            glm::dvec3 common(0);bool pending=false;std::vector<glm::dvec3> starts;starts.reserve(group.size());
            for(auto i:group){common+=nodes[i].current;pending|=nodes[i].unresolved;starts.push_back(nodes[i].current);}common/=double(group.size());
            auto& representative=nodes[group.front()];
            if(collision)for(int pass=0;pass<2;++pass){bool hit=false;
                for(std::size_t k=0;k<group.size();++k){if(!body.MayIntersectSweptBounds(glm::min(starts[k],common),glm::max(starts[k],common),margin,false))continue;
                    ++stats.surfaceSweeps;const auto contact=body.SweepContact(starts[k],common,margin,false);if(contact.hit){common+=contact.correction;representative.hint=contact.triangle;++stats.surfaceContacts;hit=true;}}
                if(hit||pending){representative.current=common;representative.unresolved=pending;Project(representative,body,margin,stats,true);common=representative.current;pending=representative.unresolved;}
                if(!hit)break;
            }
            for(std::size_t k=0;k<group.size();++k){auto& n=nodes[group[k]];if(starts[k]!=common)++n.revision;n.current=common;n.unresolved=pending;n.hint=representative.hint;n.outside=representative.outside;}
        }
    }
    glm::dvec3 FaceCenter(const Face& face)const{return (nodes[face.nodes[0]].current+nodes[face.nodes[1]].current+nodes[face.nodes[2]].current)/3.;}
    std::array<std::uint64_t,3> FaceRevisions(const Face& face)const{return {nodes[face.nodes[0]].revision,nodes[face.nodes[1]].revision,nodes[face.nodes[2]].revision};}
    void MovePhysicalPoint(std::uint32_t index,glm::dvec3 correction,bool pending){
        auto move=[&](std::uint32_t i){auto& n=nodes[i];const auto point=n.current+correction;if(point!=n.current)++n.revision;n.current=point;n.unresolved|=pending;};
        if(nodes[index].seamGroup==UINT32_MAX)move(index);else for(auto i:seamGroups[nodes[index].seamGroup])move(i);
    }
    bool FaceContacts(const HairBodyCollision& body,double margin,HairSimulationStats& stats,bool second=false,bool dirtyOnly=false){
        constexpr double range=.012;bool moved=false;
        for(const auto& c:faceClusters){glm::dvec3 lo(std::numeric_limits<double>::infinity()),hi(-std::numeric_limits<double>::infinity());bool pending=false;
            bool needed=false;for(auto i=c.begin;i<c.end;++i){const auto& face=faces[faceOrder[i]];if(dirtyOnly&&!face.unresolved&&face.evaluated&&face.revisions==FaceRevisions(face))continue;needed=true;const auto center=FaceCenter(face);lo=glm::min(lo,center);hi=glm::max(hi,center);pending|=face.unresolved;}
            if(!needed)continue;
            const bool near=conservativeGate?body.MayOverlapComponentBounds(lo,hi):body.MayIntersectSweptBounds(lo,hi,std::max(margin,range),false);
            if(!near&&!pending){for(auto i=c.begin;i<c.end;++i){auto& face=faces[faceOrder[i]];face.revisions=FaceRevisions(face);face.evaluated=true;}continue;}
            for(auto i=c.begin;i<c.end;++i){auto& face=faces[faceOrder[i]];const auto center=FaceCenter(face);
                if((dirtyOnly||(reuse&&second))&&!face.unresolved&&face.evaluated&&face.revisions==FaceRevisions(face))continue;
                const bool candidate=near&&(conservativeGate?body.MayBeInsideComponentBounds(center):body.HasNearbyBackFacingSurface(center,range,&face.surfaceHint));
                if(!face.unresolved&&!candidate){face.revisions=FaceRevisions(face);face.evaluated=true;continue;}
                Node contact;contact.current=center;contact.hint=face.hint;contact.unresolved=face.unresolved;contact.outside=face.outside;Project(contact,body,margin,stats,true,true);face.hint=contact.hint;face.unresolved=contact.unresolved;face.outside=contact.outside;
                const auto correction=contact.current-center;if(glm::dot(correction,correction)<1e-24){face.revisions=FaceRevisions(face);face.evaluated=true;continue;}
                // Barycentric minimum-norm distribution: b_i / sum(b_j^2).
                // At a centroid b_i=1/3, so all three free physical points
                // receive d. UV copies must move with their entire group;
                // averaging independently corrected copies afterward dilutes
                // this constraint and can bury the face in the body again.
                std::array<std::uint32_t,3> movedPoints{UINT32_MAX,UINT32_MAX,UINT32_MAX};std::size_t pointCount=0;
                for(auto index:face.nodes){const auto representative=nodes[index].seamGroup==UINT32_MAX?index:seamGroups[nodes[index].seamGroup].front();if(std::find(movedPoints.begin(),movedPoints.begin()+pointCount,representative)!=movedPoints.begin()+pointCount)continue;movedPoints[pointCount++]=representative;MovePhysicalPoint(index,correction,face.unresolved);}
                moved=true;
                face.revisions=FaceRevisions(face);face.evaluated=true;
            }
        }
        return moved;
    }
    void FinalEndpoints(const HairBodyCollision& body,double margin,HairSimulationStats& stats,bool dirtyOnly=false){
        constexpr double range=.03;
        for(const auto& c:clusters){glm::dvec3 lo(std::numeric_limits<double>::infinity()),hi(-std::numeric_limits<double>::infinity());bool pending=false;
            bool needed=false;for(auto i=c.begin;i<c.end;++i){const auto& n=nodes[order[i]];if(dirtyOnly&&!n.unresolved&&n.endpointRevision==n.revision)continue;needed=true;lo=glm::min(lo,n.current);hi=glm::max(hi,n.current);pending|=n.unresolved;}
            if(!needed)continue;
            const bool near=conservativeGate?body.MayOverlapComponentBounds(lo,hi):body.MayIntersectSweptBounds(lo,hi,std::max(margin,range),false);if(!near&&!pending){for(auto i=c.begin;i<c.end;++i)nodes[order[i]].endpointRevision=nodes[order[i]].revision;continue;}
            for(auto i=c.begin;i<c.end;++i){const auto index=order[i];auto& n=nodes[index];
                if(dirtyOnly&&!n.unresolved&&n.endpointRevision==n.revision)continue;
                if(n.seamGroup!=UINT32_MAX&&seamGroups[n.seamGroup].front()!=index)continue;
                // Inside() itself requires containment by a real component's
                // current box. Unlike a nearby backface/distance heuristic,
                // this remains conservative for flipped faces and deep points.
                const bool candidate=near&&(conservativeGate?body.MayBeInsideComponentBounds(n.current):body.HasNearbyBackFacingSurface(n.current,range,&n.surfaceHint));
                const auto before=n.current;if(n.unresolved||candidate)Project(n,body,margin,stats,true,conservativeGate);if(n.current!=before)++n.revision;n.endpointRevision=n.revision;
                if(n.seamGroup!=UINT32_MAX)for(auto j:seamGroups[n.seamGroup]){auto& copy=nodes[j];if(copy.current!=n.current)++copy.revision;copy.current=n.current;copy.unresolved=n.unresolved;copy.hint=n.hint;copy.outside=n.outside;copy.endpointRevision=copy.revision;}
            }
        }
    }
    void ConvergeDirtyContacts(const HairBodyCollision& body,double margin,HairSimulationStats& stats){
        // A corner's final body projection can invalidate adjacent face
        // centers. Iterate only those changed constraints, with a fixed work
        // bound; no full-scene extra nearest pass and no baseline exemption.
        for(int pass=0;pass<4;++pass){bool moved=false;{PhaseScope phase(*this,Face2Phase,body);moved=FaceContacts(body,margin,stats,true,true);}if(!moved)break;
            {PhaseScope phase(*this,FinalPhase,body);FinalEndpoints(body,margin,stats,true);}
        }
    }
    bool Publish(const SkeletalPose& pose,HairSimulationStats& stats,std::string* error){
        offsets.clear();offsets.reserve(nodes.size());for(auto& n:nodes){const auto world=n.current-n.source;n.offset=glm::inverse(glm::dmat3(pose.global[n.anchor]))*(world*units);
            if(!Finite(n.offset))return Fail(error,"Nonfinite hair surface displacement");n.previous=n.current;n.previousSource=n.source;n.previousAnchor=glm::dmat3(pose.global[n.anchor]);stats.maxMeshCorrection=std::max(stats.maxMeshCorrection,glm::length(world));
            if(glm::dot(n.offset,n.offset)>1e-24)offsets.push_back({n.mesh,n.vertex,n.anchor,n.offset});
        }return true;
    }
};

HairSurfaceContact::HairSurfaceContact():impl_(std::make_unique<Impl>()){}
HairSurfaceContact::~HairSurfaceContact()=default;
HairSurfaceContact::HairSurfaceContact(HairSurfaceContact&&) noexcept=default;
HairSurfaceContact& HairSurfaceContact::operator=(HairSurfaceContact&&) noexcept=default;
const std::vector<HairSurfaceOffset>& HairSurfaceContact::Offsets()const{return impl_->offsets;}

bool HairSurfaceContact::Initialize(const HairAsset& asset,const SkeletalAssetPackage& target,std::string* error){
    if(error)error->clear();impl_=std::make_unique<Impl>();auto& s=*impl_;s.asset=&asset;s.target=&target;s.units=asset.unitsPerMeter;
    if(!std::isfinite(s.units)||s.units<=0)return Fail(error,"Invalid hair surface source units");
    if(!target.geometry||asset.hairMeshes.empty()){s.ready=true;return true;}
    std::map<std::uint32_t,std::uint32_t> chainByBone;for(std::uint32_t c=0;c<asset.chains.size();++c){const auto& chain=asset.chains[c];if(chain.anchorBone<0||std::size_t(chain.anchorBone)>=target.skeleton.bones.size())return Fail(error,"Invalid hair surface chain anchor");for(const auto& link:chain.links)chainByBone[link.bone]=c;}
    std::set<std::pair<std::uint32_t,std::uint32_t>> attachments;for(const auto& a:asset.attachments)attachments.emplace(a.mesh,a.vertex);
    s.mapping.resize(target.geometry->meshes.size());
    for(auto m:asset.hairMeshes){if(m>=target.geometry->meshes.size()||m>=target.skins.size())return Fail(error,"Invalid hair surface mesh");
        const auto& mesh=target.geometry->meshes[m];const auto& skin=target.skins[m];if(skin.vertices.size()!=mesh.vertices.size())return Fail(error,"Hair surface skin count mismatch");
        if(!std::isfinite(glm::determinant(skin.bindTransform))||std::abs(glm::determinant(skin.bindTransform))<1e-15)return Fail(error,"Singular hair surface bind transform");
        s.mapping[m].assign(mesh.vertices.size(),-1);const auto fromSkeleton=Instance(*target.geometry,m)*glm::inverse(skin.bindTransform);
        for(std::uint32_t v=0;v<mesh.vertices.size();++v){if(attachments.contains({m,v}))continue;const auto& weights=skin.vertices[v];std::map<std::uint32_t,double> chains;double total=0;
            for(int k=0;k<4;++k){const auto joint=weights.joints[k];const double weight=weights.weights[k];if(weight<=0||!std::isfinite(weight)||joint>=target.skeleton.bones.size())continue;total+=weight;if(auto found=chainByBone.find(joint);found!=chainByBone.end())chains[found->second]+=weight;}
            if(chains.empty()||total<=1e-12)continue;const auto best=std::max_element(chains.begin(),chains.end(),[](auto a,auto b){return a.second<b.second;});Impl::Node n;n.mesh=m;n.vertex=v;n.anchor=asset.chains[best->first].anchorBone;n.fromSkeleton=fromSkeleton;
            for(int k=0;k<4;++k){const auto joint=weights.joints[k];const double weight=weights.weights[k];if(weight<=0||!std::isfinite(weight)||joint>=target.skeleton.bones.size())continue;
                const auto matrix=joint<skin.boneFromMesh.size()?skin.boneFromMesh[joint]:target.skeleton.bones[joint].inverseBind*skin.bindTransform;const double normalized=weight/total;
                n.influences.push_back({joint,normalized,glm::dvec3(matrix*glm::dvec4(mesh.vertices[v].position,1))*normalized,glm::dmat3(matrix)});
            }s.mapping[m][v]=static_cast<std::int32_t>(s.nodes.size());s.nodes.push_back(std::move(n));
        }
    }
    for(std::uint32_t morph=0;morph<target.morphs.size();++morph)for(const auto& d:target.morphs[morph].deltas)if(d.mesh<s.mapping.size()&&d.vertex<s.mapping[d.mesh].size()&&s.mapping[d.mesh][d.vertex]>=0)s.nodes[s.mapping[d.mesh][d.vertex]].morphs.push_back({morph,d.position});
    auto rest=EvaluateSkeletalPose(target,nullptr,0,false);if(!s.Evaluate(rest,error))return false;
    auto edge=[&](std::uint32_t m,std::uint32_t a,std::uint32_t b){if(a>=s.mapping[m].size()||b>=s.mapping[m].size())return;const auto ia=s.mapping[m][a],ib=s.mapping[m][b];if(ia>=0){if(ib>=0&&ia!=ib)s.nodes[ia].adjacent.push_back(ib);else if(ib<0)++s.nodes[ia].fixedAdjacent;}if(ib>=0){if(ia>=0&&ia!=ib)s.nodes[ib].adjacent.push_back(ia);else if(ia<0)++s.nodes[ib].fixedAdjacent;}};
    for(auto m:asset.hairMeshes){const auto& mesh=target.geometry->meshes[m];for(std::size_t t=0;t+2<mesh.indices.size();t+=3){edge(m,mesh.indices[t],mesh.indices[t+1]);edge(m,mesh.indices[t+1],mesh.indices[t+2]);edge(m,mesh.indices[t+2],mesh.indices[t]);}}
    // Couple duplicate UV seam vertices only when their complete normalized
    // skin/morph trajectories match, not merely their common bind position.
    using SeamKey=std::tuple<long long,long long,long long,std::uint32_t,std::vector<long long>>;
    std::map<SeamKey,std::vector<std::uint32_t>> seams;
    for(std::uint32_t i=0;i<s.nodes.size();++i){auto& n=s.nodes[i];n.rest=n.source;const auto p=n.rest;std::vector<long long> signature;
        for(const auto& w:n.influences){signature.push_back(w.bone);signature.push_back(std::llround(w.weight*1e9));for(int axis=0;axis<3;++axis)signature.push_back(std::llround(w.position[axis]/s.units*1e9));for(int c=0;c<3;++c)for(int r=0;r<3;++r)signature.push_back(std::llround(w.linear[c][r]*1e9));}
        signature.push_back(-1);for(const auto& m:n.morphs){signature.push_back(m.index);for(int axis=0;axis<3;++axis)signature.push_back(std::llround(m.delta[axis]/s.units*1e9));}
        signature.push_back(-2);for(int c=0;c<4;++c)for(int r=0;r<4;++r)signature.push_back(std::llround(n.fromSkeleton[c][r]*(c==3&&r<3?1./s.units:1.)*1e9));
        auto& group=seams[{std::llround(p.x*1e7),std::llround(p.y*1e7),std::llround(p.z*1e7),n.anchor,std::move(signature)}];for(auto other:group){n.adjacent.push_back(other);s.nodes[other].adjacent.push_back(i);}group.push_back(i);
    }
    for(auto& [key,group]:seams)if(group.size()>1){for(auto i:group)s.nodes[i].seamGroup=static_cast<std::uint32_t>(s.seamGroups.size());s.seamGroups.push_back(std::move(group));}
    s.sweepMemos.resize(s.seamGroups.size());s.smoothMemos.resize(s.seamGroups.size());
    for(auto& n:s.nodes){std::sort(n.adjacent.begin(),n.adjacent.end());n.adjacent.erase(std::unique(n.adjacent.begin(),n.adjacent.end()),n.adjacent.end());}
    for(auto m:asset.hairMeshes){const auto& mesh=target.geometry->meshes[m];for(std::size_t t=0;t+2<mesh.indices.size();t+=3){std::array<std::uint32_t,3> indices{};bool free=true;
        for(int k=0;k<3;++k){const auto vertex=mesh.indices[t+k];if(vertex>=s.mapping[m].size()||s.mapping[m][vertex]<0){free=false;break;}indices[k]=static_cast<std::uint32_t>(s.mapping[m][vertex]);}
        if(!free)continue;const auto a=s.nodes[indices[0]].rest,b=s.nodes[indices[1]].rest,c=s.nodes[indices[2]].rest;if(glm::dot(glm::cross(b-a,c-a),glm::cross(b-a,c-a))<1e-24)continue;
        Impl::Face face;face.nodes=indices;face.rest=(a+b+c)/3.;s.faces.push_back(face);
    }}
    s.order.resize(s.nodes.size());std::iota(s.order.begin(),s.order.end(),0);if(!s.order.empty())s.Partition(0,s.order.size());
    s.faceOrder.resize(s.faces.size());std::iota(s.faceOrder.begin(),s.faceOrder.end(),0);if(!s.faceOrder.empty())s.PartitionFaces(0,s.faceOrder.size());s.ready=true;return true;
}

bool HairSurfaceContact::Reset(const HairAsset& asset,const SkeletalAssetPackage& target,const SkeletalPose& pose,const HairBodyCollision& body,HairSimulationStats& stats,std::string* error){
    if(error)error->clear();if(impl_->asset!=&asset||impl_->target!=&target||!impl_->ready)if(!Initialize(asset,target,error))return false;
    auto& s=*impl_;const auto start=std::chrono::steady_clock::now();SurfaceCounters(stats,s.nodes.size(),s.clusters.size());
    ++s.contactEpoch;for(auto& memo:s.smoothMemos)memo.valid=false;
    {Impl::PhaseScope phase(s,Impl::EvaluatePhase,body);if(!s.Evaluate(pose,error))return false;}
    const double margin=std::max(0.,asset.settings.thickness);
    {Impl::PhaseScope phase(s,Impl::MovingPhase,body);for(auto& n:s.nodes){n.offset={0,0,0};n.current=n.source;n.unresolved=false;n.revision=0;n.hint=n.surfaceHint=UINT32_MAX;if(asset.settings.collision)s.Project(n,body,margin,stats,true);}}
    {Impl::PhaseScope phase(s,Impl::SmoothPhase,body);s.Smooth(body,margin,asset.settings.collision,stats);s.WeldSeams(body,margin,asset.settings.collision,stats);}
    for(auto& face:s.faces){face.unresolved=false;face.evaluated=false;face.hint=face.surfaceHint=UINT32_MAX;}
    if(asset.settings.collision){
        {Impl::PhaseScope phase(s,Impl::Face1Phase,body);s.FaceContacts(body,margin,stats);}
        {Impl::PhaseScope phase(s,Impl::Face2Phase,body);s.FaceContacts(body,margin,stats,true);}
        {Impl::PhaseScope phase(s,Impl::SmoothPhase,body);s.WeldSeams(body,margin,true,stats);}
        {Impl::PhaseScope phase(s,Impl::FinalPhase,body);s.FinalEndpoints(body,margin,stats);}
        s.ConvergeDirtyContacts(body,margin,stats);
    }
    s.collisionEnabled=asset.settings.collision;{Impl::PhaseScope phase(s,Impl::PublishPhase,body);if(!s.Publish(pose,stats,error))return false;}
    s.previousBody=&body;s.previousBodyRevision=body.GeometryRevision();s.previousMargin=margin;
    stats.surfaceMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();return true;
}

bool HairSurfaceContact::Step(const HairAsset& asset,const SkeletalAssetPackage& target,const SkeletalPose& pose,const HairBodyCollision& body,double dt,const HairSolverSettings& settings,HairSimulationStats& stats,std::string* error){
    if(error)error->clear();if(!std::isfinite(dt)||dt<0)return Fail(error,"Invalid hair surface time step");
    if(impl_->asset!=&asset||impl_->target!=&target||!impl_->ready){if(!Initialize(asset,target,error))return false;if(dt>0)return Reset(asset,target,pose,body,stats,error);}
    auto& s=*impl_;
    // Render-only presentation is deliberately independent of the stale body
    // BVH and must not consume, decay or rebase the last physical trajectory.
    if(dt==0)return true;
    // Nearest depends on current body geometry and query position, not on the
    // physical clock. A true outside result therefore remains an exact
    // certificate across ticks while BOTH inputs remain bitwise unchanged.
    // CCD memos below are still per tick because they consume body motion.
    const bool sameBody=s.previousBody==&body&&s.previousBodyRevision==body.GeometryRevision();
    if(!sameBody)++s.contactEpoch;
    SurfaceCounters(stats,s.nodes.size(),s.clusters.size());
    const auto start=std::chrono::steady_clock::now();double sourceMotion=0;{Impl::PhaseScope phase(s,Impl::EvaluatePhase,body);if(!s.Evaluate(pose,error,&sourceMotion))return false;}const double decay=std::exp(-2.*std::min(dt,.1)),margin=std::max(0.,settings.thickness);
    const bool newlyEnabled=settings.collision&&!s.collisionEnabled;s.collisionEnabled=settings.collision;
    // This layer supplies a collision displacement, not another spring or
    // gravity integrator. With a stationary target, repeatedly decaying that
    // displacement and diffusing it through the topology pulls resting contact
    // back into the body and injects a projection/relaxation limit cycle.
    // Stop only those artificial relaxation terms for unchanged geometry and
    // at most nanometer skin/advection noise. Actual source evaluation, anchor
    // transport, moving-body CCD, face contacts and final inside checks remain
    // active: an unfinished or newly embedded contact is never frozen.
    bool stationary=s.staticContacts&&settings.collision&&!newlyEnabled&&sameBody&&s.previousMargin==margin&&sourceMotion<=1e-9;
    for(auto& n:s.nodes){n.current=n.source+glm::dmat3(pose.global[n.anchor])*n.offset/s.units;stationary&=glm::length(n.current-n.previous)<=1e-9;n.revision=0;if(newlyEnabled)n.unresolved=true;}
    if(stationary)for(auto& n:s.nodes){
        // Incremental transport is algebraically identical to source + anchor
        // * offset, but unchanged inputs add EXACT zero rather than repeatedly
        // round-tripping inverse(anchor) and anchor. Nonzero source/anchor
        // motion, including motion inside the nanometer deadband, is retained.
        n.current=n.previous+(n.source-n.previousSource)+(glm::dmat3(pose.global[n.anchor])-n.previousAnchor)*n.offset/s.units;
    }
    else for(auto& n:s.nodes)n.current=n.source+glm::dmat3(pose.global[n.anchor])*(n.offset*decay)/s.units;
    for(auto& memo:s.sweepMemos)memo.valid=false;for(auto& memo:s.smoothMemos)memo.valid=false;for(auto& face:s.faces)face.evaluated=false;
    {Impl::PhaseScope phase(s,Impl::MovingPhase,body);if(settings.collision)for(const auto& c:s.clusters){glm::dvec3 lo(std::numeric_limits<double>::infinity()),hi(-std::numeric_limits<double>::infinity());bool unresolved=false;
        for(auto i=c.begin;i<c.end;++i){const auto& n=s.nodes[s.order[i]];lo=glm::min(lo,glm::min(n.previous,n.current));hi=glm::max(hi,glm::max(n.previous,n.current));unresolved|=n.unresolved;}
        const bool active=body.MayIntersectSweptBounds(lo,hi,margin,true);if(!active)++stats.surfaceBroadphaseRejects;if(!active&&!unresolved)continue;
        for(auto i=c.begin;i<c.end;++i){auto& n=s.nodes[s.order[i]];if(active)s.MemoSweep(n,n.previous,body,margin,true,s.sweepMemos,stats);else if(n.unresolved)s.Project(n,body,margin,stats,true);}
    }}
    {Impl::PhaseScope phase(s,Impl::SmoothPhase,body);if(!stationary)s.Smooth(body,margin,settings.collision,stats);s.WeldSeams(body,margin,settings.collision,stats);}
    if(settings.collision){
        {Impl::PhaseScope phase(s,Impl::Face1Phase,body);s.FaceContacts(body,margin,stats);}
        {Impl::PhaseScope phase(s,Impl::Face2Phase,body);s.FaceContacts(body,margin,stats,true);}
        {Impl::PhaseScope phase(s,Impl::SmoothPhase,body);s.WeldSeams(body,margin,true,stats);}
        {Impl::PhaseScope phase(s,Impl::FinalPhase,body);s.FinalEndpoints(body,margin,stats);}
        s.ConvergeDirtyContacts(body,margin,stats);
    }
    {Impl::PhaseScope phase(s,Impl::PublishPhase,body);if(!s.Publish(pose,stats,error))return false;}
    s.previousBody=&body;s.previousBodyRevision=body.GeometryRevision();s.previousMargin=margin;
    stats.surfaceMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();return true;
}
}
}
