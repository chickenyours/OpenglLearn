#include "Render/Public/Pipeline/realtime_gi.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <numeric>
#include <stdexcept>
namespace Render {
namespace {
bool Bounded(glm::vec3 p) {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&glm::all(glm::lessThanEqual(glm::abs(p),glm::vec3(100000)));
}
struct Node { glm::vec3 lo,hi; std::uint32_t left=0,right=0,begin=0,count=0; };
void ValidateTriangles(std::span<const ProbeTriangle> triangles) {
    if(triangles.size()>1048576) throw std::invalid_argument("Realtime GI scene exceeds 1048576 triangles");
    for(const auto& t:triangles) if(!Bounded(t.a)||!Bounded(t.b)||!Bounded(t.c)||!Bounded(t.diffuseReflectance)||!Bounded(t.emission)||
        glm::length(glm::cross(t.b-t.a,t.c-t.a))<1e-7f||
        glm::any(glm::lessThan(t.diffuseReflectance,glm::vec3(0)))||glm::any(glm::greaterThan(t.diffuseReflectance,glm::vec3(1)))||
        glm::any(glm::lessThan(t.emission,glm::vec3(0)))||glm::any(glm::greaterThan(t.emission,glm::vec3(60000)))||
        !Bounded(t.specularF0)||glm::any(glm::lessThan(t.specularF0,glm::vec3(0)))||glm::any(glm::greaterThan(t.specularF0,glm::vec3(1)))||!std::isfinite(t.roughness)||t.roughness<0||t.roughness>1)
        throw std::invalid_argument("Invalid realtime GI geometry/material");
}
}
void RealtimeGiScene::PublishSnapshotVersion(std::uint64_t parent,bool fullRefitBarrier,bool recordDelta) noexcept {
    // Metadata only: retaining scenes here would prevent storage retirement.
    snapshotVersion_=0;
    try {
        const auto storage=topology_->snapshotStorage;std::lock_guard lock(storage->mutex);
        snapshotVersion_=storage->nextVersion++;
        if(!recordDelta)return;
        SnapshotStoragePool::Delta delta;delta.version=snapshotVersion_;delta.parent=parent;delta.fullRefitBarrier=fullRefitBarrier;
        if(!fullRefitBarrier)delta.ranges=dirtyRanges_;
        if(storage->journal.size()==SnapshotJournalCapacity)storage->journal.pop_front();
        storage->journal.push_back(std::move(delta));
    } catch(...) {
        // Missing version/edge disables catch-up, never immutable publication.
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
    auto topology=std::make_shared<RealtimeGiScene::Topology>();topology->inputOrder=indices;result->topology_=topology;
    topology->packedForInput.resize(indices.size());topology->leafForTriangle.resize(indices.size());topology->parents.assign(nodes.size(),0xffffffffu);
    for(unsigned i=0;i<indices.size();++i)topology->packedForInput[indices[i]]=i;
    for(unsigned i=0;i<nodes.size();++i){const auto& n=nodes[i];
        if(n.count)for(unsigned t=n.begin;t<n.begin+n.count;++t)topology->leafForTriangle[t]=i;
        else {topology->parents[n.left]=i;topology->parents[n.right]=i;}}
    const auto size=nodes.size()*2+triangles.size()*5;
    result->pixels_.resize(std::max<std::size_t>(1024,((size+1023)/1024)*1024),glm::vec4(0));
    for(std::size_t i=0;i<nodes.size();++i) {
        const auto& n=nodes[i]; result->pixels_[i*2]=glm::vec4(n.lo,float(n.count?n.begin:n.left));
        result->pixels_[i*2+1]=glm::vec4(n.hi,n.count?-float(n.count):float(n.right));
    }
    for(std::size_t i=0;i<indices.size();++i) {
        const auto& t=triangles[indices[i]]; const auto offset=nodes.size()*2+i*5;
        if(glm::any(glm::greaterThan(t.emission,glm::vec3(0))))++result->emissiveTriangles_;
        result->pixels_[offset]=glm::vec4(t.a,t.specularF0.r); result->pixels_[offset+1]=glm::vec4(t.b,t.specularF0.g);result->pixels_[offset+2]=glm::vec4(t.c,t.specularF0.b);
        result->pixels_[offset+3]=glm::vec4(t.diffuseReflectance,t.roughness);result->pixels_[offset+4]=glm::vec4(t.emission,float((t.twoSidedEmission?1:0)|(t.analyticEmission?2:0)));
    }
    result->hasEmission_=result->emissiveTriangles_!=0;result->PublishSnapshotVersion(0,false,true);return result;
}
std::shared_ptr<const RealtimeGiScene> RefitRealtimeGiScene(const RealtimeGiScene& previous,std::span<const ProbeTriangle> triangles) {
    ValidateTriangles(triangles);
    if(triangles.size()!=previous.TriangleCount())throw std::invalid_argument("GI refit requires identical triangle topology/order");
    auto result=std::make_shared<RealtimeGiScene>(previous);result->hasEmission_=false;result->emissiveTriangles_=0;result->refitStatistics_={};
    result->incremental_=false;result->dirtyRanges_.clear();
    for(unsigned i=0;i<result->triangles_;++i) {
        const auto& t=triangles[result->topology_->inputOrder[i]];auto at=result->nodes_*2+i*5;
        result->pixels_[at]=glm::vec4(t.a,t.specularF0.r);result->pixels_[at+1]=glm::vec4(t.b,t.specularF0.g);result->pixels_[at+2]=glm::vec4(t.c,t.specularF0.b);
        result->pixels_[at+3]=glm::vec4(t.diffuseReflectance,t.roughness);result->pixels_[at+4]=glm::vec4(t.emission,float((t.twoSidedEmission?1:0)|(t.analyticEmission?2:0)));
        if(glm::any(glm::greaterThan(t.emission,glm::vec3(0))))++result->emissiveTriangles_;
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
    result->hasEmission_=result->emissiveTriangles_!=0;
    result->refitStatistics_.snapshotCopiedBytes=std::uint64_t(previous.pixels_.size())*sizeof(glm::vec4);
    result->PublishSnapshotVersion(previous.snapshotVersion_,true,true);return result;
}
std::shared_ptr<const RealtimeGiScene> RefitRealtimeGiSceneChanged(const RealtimeGiScene& previous,
    std::span<const ProbeTriangle> triangles,std::span<const std::uint32_t> changed,bool snapshotRowCatchup) {
    const auto begin=std::chrono::steady_clock::now();
    if(triangles.size()!=previous.TriangleCount())throw std::invalid_argument("Sparse GI refit requires identical topology/order");
    for(auto input:changed){if(input>=triangles.size())throw std::invalid_argument("Sparse GI triangle index outside extraction");
        ValidateTriangles(triangles.subspan(input,1));}
    const auto snapshotBegin=std::chrono::steady_clock::now();
    const auto storage=previous.topology_->snapshotStorage;
    // Only the final immutable owner returns writable storage. Parent-chain
    // catch-up mutates retired pixels exclusively, including old-base forks.
    auto result=std::shared_ptr<RealtimeGiScene>(new RealtimeGiScene,[storage](RealtimeGiScene* scene) noexcept {
        const auto version=scene->snapshotVersion_;auto pixels=std::move(scene->pixels_);delete scene;
        if(!pixels.capacity())return;
        try {std::lock_guard lock(storage->mutex);
            if(storage->available.size()<3)storage->available.push_back({std::move(pixels),version});
        } catch(...) {}
    });
    bool reusedStorage=false,catchup=false;std::uint64_t retiredVersion=0;
    std::vector<RealtimeGiScene::DirtyRange> catchupRanges;
    auto fallback=snapshotRowCatchup?RealtimeGiScene::SnapshotCopyFallback::PoolEmpty:RealtimeGiScene::SnapshotCopyFallback::Disabled;
    std::uint32_t catchupVersions=0;
    {std::lock_guard lock(storage->mutex);if(!storage->available.empty()){
        retiredVersion=storage->available.back().version;
        result->pixels_=std::move(storage->available.back().pixels);storage->available.pop_back();
        reusedStorage=result->pixels_.capacity()>=previous.pixels_.size();}}
    if(snapshotRowCatchup&&reusedStorage) {
        if(result->pixels_.size()!=previous.pixels_.size())fallback=RealtimeGiScene::SnapshotCopyFallback::IncompatibleStorage;
        else if(!retiredVersion||!previous.snapshotVersion_)fallback=RealtimeGiScene::SnapshotCopyFallback::MissingJournal;
        else try {
            std::lock_guard lock(storage->mutex);std::uint64_t version=previous.snapshotVersion_;
            fallback=RealtimeGiScene::SnapshotCopyFallback::MissingJournal;
            // Numeric order is not ancestry: follow actual parent identities.
            for(std::size_t hop=0;hop<=RealtimeGiScene::SnapshotJournalCapacity;++hop){
                if(version==retiredVersion){catchup=true;fallback=RealtimeGiScene::SnapshotCopyFallback::None;break;}
                const auto edge=std::find_if(storage->journal.begin(),storage->journal.end(),
                    [&](const auto& value){return value.version==version;});
                if(edge==storage->journal.end())break;
                if(edge->fullRefitBarrier){fallback=RealtimeGiScene::SnapshotCopyFallback::FullRefitBarrier;break;}
                if(!edge->parent){fallback=RealtimeGiScene::SnapshotCopyFallback::NotAncestor;break;}
                for(const auto range:edge->ranges){
                    if(range.first%1024||!range.count||range.count%1024||range.first>previous.pixels_.size()||
                        range.count>previous.pixels_.size()-range.first)throw std::logic_error("Invalid snapshot journal row range");
                    catchupRanges.push_back(range);
                }
                version=edge->parent;++catchupVersions;
            }
        }catch(...){catchup=false;fallback=RealtimeGiScene::SnapshotCopyFallback::JournalFailure;}
    }
    if(catchup){
        std::sort(catchupRanges.begin(),catchupRanges.end(),[](auto a,auto b){return a.first<b.first;});
        std::size_t used=0;
        for(const auto range:catchupRanges){
            if(used&&range.first<=catchupRanges[used-1].first+catchupRanges[used-1].count){
                auto& last=catchupRanges[used-1];last.count=std::max(last.first+last.count,range.first+range.count)-last.first;
            }else catchupRanges[used++]=range;
        }
        catchupRanges.resize(used);
        for(const auto range:catchupRanges){
            std::copy_n(previous.pixels_.begin()+range.first,range.count,result->pixels_.begin()+range.first);
            result->refitStatistics_.snapshotCopiedBytes+=std::uint64_t(range.count)*sizeof(glm::vec4);
            result->refitStatistics_.snapshotPatchedRows+=range.count/1024;
        }
    }else {
        result->pixels_.assign(previous.pixels_.begin(),previous.pixels_.end());
        result->refitStatistics_.snapshotCopiedBytes=std::uint64_t(previous.pixels_.size())*sizeof(glm::vec4);
    }
    result->refitStatistics_.snapshotCatchupUsed=catchup;
    result->refitStatistics_.snapshotCatchupVersions=catchup?catchupVersions:0;
    result->refitStatistics_.snapshotCopyFallback=fallback;
    result->topology_=previous.topology_;result->nodes_=previous.nodes_;result->triangles_=previous.triangles_;
    result->emissiveTriangles_=previous.emissiveTriangles_;result->hasEmission_=previous.hasEmission_;
    const auto snapshotEnd=std::chrono::steady_clock::now();
    result->incremental_=true;result->dirtyRanges_.clear();
    std::vector<std::uint8_t> nodes(result->nodes_,0),rows(result->pixels_.size()/1024,0);
    std::vector<std::uint32_t> dirtyNodes;dirtyNodes.reserve(std::min<std::size_t>(result->nodes_,changed.size()*4));
    const auto dirty=[&](unsigned first,unsigned count){for(unsigned row=first/1024;row<=(first+count-1)/1024;++row)rows[row]=1;};
    const auto& topology=*result->topology_;
    for(auto input:changed){const auto packed=topology.packedForInput[input];const auto& t=triangles[input];const auto at=result->nodes_*2+packed*5;
        const bool wasEmissive=glm::any(glm::greaterThan(glm::vec3(result->pixels_[at+4]),glm::vec3(0)));
        const bool isEmissive=glm::any(glm::greaterThan(t.emission,glm::vec3(0)));
        if(wasEmissive!=isEmissive){if(isEmissive)++result->emissiveTriangles_;else --result->emissiveTriangles_;}
        result->pixels_[at]=glm::vec4(t.a,t.specularF0.r);result->pixels_[at+1]=glm::vec4(t.b,t.specularF0.g);result->pixels_[at+2]=glm::vec4(t.c,t.specularF0.b);
        result->pixels_[at+3]=glm::vec4(t.diffuseReflectance,t.roughness);result->pixels_[at+4]=glm::vec4(t.emission,float((t.twoSidedEmission?1:0)|(t.analyticEmission?2:0)));dirty(at,5);
        auto node=topology.leafForTriangle[packed];while(node!=0xffffffffu&&!nodes[node]){nodes[node]=1;dirtyNodes.push_back(node);node=topology.parents[node];}}
    // Children have larger indices. Only changed leaves and ancestors need
    // refitting; a tiny moving instance must not walk every static BVH node.
    const auto refitNode=[&](unsigned i){
        auto& a=result->pixels_[i*2];auto& b=result->pixels_[i*2+1];glm::vec3 lo(1e30f),hi(-1e30f);
        if(b.w<0)for(unsigned t=unsigned(a.w);t<unsigned(a.w-b.w);++t)for(unsigned v=0;v<3;++v){
            const auto p=glm::vec3(result->pixels_[result->nodes_*2+t*5+v]);lo=glm::min(lo,p);hi=glm::max(hi,p);}
        else {lo=glm::min(glm::vec3(result->pixels_[unsigned(a.w)*2]),glm::vec3(result->pixels_[unsigned(b.w)*2]));
            hi=glm::max(glm::vec3(result->pixels_[unsigned(a.w)*2+1]),glm::vec3(result->pixels_[unsigned(b.w)*2+1]));}
        a=glm::vec4(lo,a.w);b=glm::vec4(hi,b.w);dirty(i*2,2);};
    if(dirtyNodes.size()>result->nodes_/8) {
        // A large moving mesh touches most nodes; a reverse linear scan is
        // cheaper than sorting that dense update list.
        for(unsigned end=result->nodes_;end>0;--end)if(nodes[end-1])refitNode(end-1);
    } else {
        std::sort(dirtyNodes.begin(),dirtyNodes.end(),std::greater<>());
        for(const auto i:dirtyNodes)refitNode(i);
    }
    result->hasEmission_=result->emissiveTriangles_!=0;
    for(unsigned first=0;first<rows.size();){if(!rows[first]){++first;continue;}unsigned end=first+1;while(end<rows.size()&&rows[end])++end;
        result->dirtyRanges_.push_back({first*1024,(end-first)*1024});first=end;}
    // CPU repair rows never replace the new snapshot's GPU upload deltas.
    result->PublishSnapshotVersion(previous.snapshotVersion_,false,snapshotRowCatchup);
    result->refitStatistics_.snapshotMilliseconds=std::chrono::duration<double,std::milli>(snapshotEnd-snapshotBegin).count();
    result->refitStatistics_.snapshotStorageReused=reusedStorage;
    result->refitStatistics_.refitMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()-
        result->refitStatistics_.snapshotMilliseconds;
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
