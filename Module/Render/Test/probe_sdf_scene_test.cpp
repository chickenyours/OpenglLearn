#include "Render/Public/Pipeline/probe_sdf_scene.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

namespace {
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
std::vector<Render::ProbeTriangle> Cube(std::uint32_t subdivisions) {
    std::vector<Render::ProbeTriangle> triangles;
    for (int axis = 0; axis != 3; ++axis) for (int side = 0; side != 2; ++side) {
        const int u = (axis + 1) % 3, v = (axis + 2) % 3;
        const auto point = [&](std::uint32_t x, std::uint32_t y) {
            glm::vec3 p(0); p[axis] = side ? .5f : -.5f; p[u] = float(x) / subdivisions - .5f; p[v] = float(y) / subdivisions - .5f; return p;
        };
        for (std::uint32_t y = 0; y < subdivisions; ++y) for (std::uint32_t x = 0; x < subdivisions; ++x) {
            const auto a = point(x, y), b = point(x + 1, y), c = point(x + 1, y + 1), d = point(x, y + 1);
            triangles.push_back({a, b, c}); triangles.push_back({a, c, d});
        }
    }
    return triangles;
}
float BoxSdf(glm::vec3 p) {
    const auto q = glm::abs(p) - glm::vec3(.5f);
    return glm::length(glm::max(q, glm::vec3(0))) + std::min(std::max({q.x, q.y, q.z}), 0.f);
}
void EqualFields(const Render::ProbeSdfScene& a, const Render::ProbeSdfScene& b) {
    Check(a.GlobalPixels().size() == b.GlobalPixels().size(), "Global atlas size mismatch");
    for (std::size_t i = 0; i < a.GlobalPixels().size(); ++i)
        Check(glm::all(glm::lessThanEqual(glm::abs(a.GlobalPixels()[i] - b.GlobalPixels()[i]), glm::vec4(2e-6f))), "Dirty brick update differs from full union rebuild");
}

bool SameSnapshotBits(const std::vector<glm::vec4>& a,const std::vector<glm::vec4>& b) {
    return a.size()==b.size()&&std::memcmp(a.data(),b.data(),a.size()*sizeof(glm::vec4))==0;
}
bool SameDirtyRows(const Render::RealtimeGiScene& a,const Render::RealtimeGiScene& b) {
    const auto& x=a.DirtyRanges();const auto& y=b.DirtyRanges();if(x.size()!=y.size())return false;
    for(std::size_t i=0;i<x.size();++i)if(x[i].first!=y[i].first||x[i].count!=y[i].count)return false;
    return true;
}
void SnapshotCatchupContracts() {
    using Scene=Render::RealtimeGiScene;using Fallback=Scene::SnapshotCopyFallback;
    const auto baseline=Cube(12);const auto fullBytes=[](const auto& scene){return std::uint64_t(scene.Pixels().size())*sizeof(glm::vec4);};
    const auto equivalent=[&](const auto& actual,const auto& root,const auto& values){
        const auto reference=Render::RefitRealtimeGiScene(root,values);
        Check(SameSnapshotBits(actual.Pixels(),reference->Pixels())&&actual.HasEmission()==reference->HasEmission(),
            "Snapshot catch-up differs bitwise from the independent full refit");
    };
    const auto fullFallback=[&](const auto& scene,Fallback reason){
        const auto& stats=scene.RefitCpuStatistics();
        Check(!stats.snapshotCatchupUsed&&stats.snapshotCopyFallback==reason&&stats.snapshotPatchedRows==0&&
            stats.snapshotCopiedBytes==fullBytes(scene),"Snapshot fallback did not copy the complete prior image");
    };
    // Three independently retained generations force an old retired buffer
    // to catch up through two parents, with edits in different packed rows.
    auto root=Render::BuildRealtimeGiScene(baseline);auto values=baseline;
    const auto rootBits=root->Pixels();const auto& order=root->InputTriangleIndices();
    const std::array<std::uint32_t,4> input{{order[0],order[order.size()/3],order[2*order.size()/3],order.back()}};
    values[input[0]].emission=glm::vec3(.1f);
    auto a=Render::RefitRealtimeGiSceneChanged(*root,values,std::span(input.data(),1),true);
    fullFallback(*a,Fallback::PoolEmpty);const auto retiredAddress=a->Pixels().data();
    values[input[1]].a.x+=.001f;values[input[1]].roughness=.2f;
    auto b=Render::RefitRealtimeGiSceneChanged(*a,values,std::span(input.data()+1,1),true);const auto bBits=b->Pixels();
    values[input[2]].emission=glm::vec3(.4f);values[input[2]].specularF0=glm::vec3(.1f);
    auto c=Render::RefitRealtimeGiSceneChanged(*b,values,std::span(input.data()+2,1),true);const auto cBits=c->Pixels();
    a.reset();values[input[3]].a.z-=.001f;values[input[3]].emission=glm::vec3(.3f);
    auto patched=Render::RefitRealtimeGiSceneChanged(*c,values,std::span(input.data()+3,1),true);
    const auto& patch=patched->RefitCpuStatistics();
    Check(patch.snapshotStorageReused&&patch.snapshotCatchupUsed&&patch.snapshotCopyFallback==Fallback::None&&
        patch.snapshotCatchupVersions==2&&patch.snapshotPatchedRows>0&&patch.snapshotCopiedBytes<fullBytes(*patched)&&
        patch.snapshotCopiedBytes==patch.snapshotPatchedRows*1024*sizeof(glm::vec4)&&patched->Pixels().data()==retiredAddress,
        "Multi-parent catch-up failed to reuse only certified retired dirty rows");
    equivalent(*patched,*root,values);
    auto ordinary=Render::RefitRealtimeGiSceneChanged(*c,values,std::span(input.data()+3,1));
    fullFallback(*ordinary,Fallback::Disabled);
    Check(SameSnapshotBits(patched->Pixels(),ordinary->Pixels())&&SameDirtyRows(*patched,*ordinary),
        "CPU catch-up rows replaced or expanded the actual GPU upload deltas");
    Check(SameSnapshotBits(root->Pixels(),rootBits)&&SameSnapshotBits(b->Pixels(),bBits)&&SameSnapshotBits(c->Pixels(),cBits)&&
        patched->Pixels().data()!=b->Pixels().data()&&patched->Pixels().data()!=c->Pixels().data(),
        "Catching up retired storage mutated a retained immutable generation");
    const auto copied=patch.snapshotCopiedBytes,rows=patch.snapshotPatchedRows;
    // A newer sibling is not an ancestor of the retained old root. Replaying
    // numeric generations would leave its unrelated material/geometry behind.
    auto forkRoot=Render::BuildRealtimeGiScene(baseline);auto forkValues=baseline;
    forkValues[0].emission=glm::vec3(.5f);const std::array<std::uint32_t,1> first{{0}},second{{1}};
    auto sibling=Render::RefitRealtimeGiSceneChanged(*forkRoot,forkValues,first,true);
    const auto siblingAddress=sibling->Pixels().data();sibling.reset();
    forkValues=baseline;forkValues[1].a.z+=.002f;
    auto branch=Render::RefitRealtimeGiSceneChanged(*forkRoot,forkValues,second,true);
    fullFallback(*branch,Fallback::NotAncestor);equivalent(*branch,*forkRoot,forkValues);
    Check(branch->Pixels().data()==siblingAddress&&SameSnapshotBits(forkRoot->Pixels(),Render::BuildRealtimeGiScene(baseline)->Pixels()),
        "Old-base branch did not safely overwrite the retired sibling image");
    // A disabled intermediate update deliberately has no journal edge.
    auto gapRoot=Render::BuildRealtimeGiScene(baseline);auto gapValues=baseline;gapValues[0].emission=glm::vec3(.2f);
    auto gapOld=Render::RefitRealtimeGiSceneChanged(*gapRoot,gapValues,first,true);
    gapValues[1].diffuseReflectance=glm::vec3(.3f);
    auto gap=Render::RefitRealtimeGiSceneChanged(*gapOld,gapValues,second,false);gapOld.reset();
    gapValues[0].emission=glm::vec3(0);
    auto gapNext=Render::RefitRealtimeGiSceneChanged(*gap,gapValues,first,true);
    fullFallback(*gapNext,Fallback::MissingJournal);equivalent(*gapNext,*gapRoot,gapValues);
    // Full refit preserves topology but must publish a distinct barrier version.
    auto barrierRoot=Render::BuildRealtimeGiScene(baseline);auto barrierValues=baseline;barrierValues[0].a.x+=.001f;
    auto beforeBarrier=Render::RefitRealtimeGiSceneChanged(*barrierRoot,barrierValues,first,true);
    barrierValues[1].emission=glm::vec3(.7f);
    auto barrier=Render::RefitRealtimeGiScene(*beforeBarrier,barrierValues);const auto barrierBits=barrier->Pixels();beforeBarrier.reset();
    barrierValues[0].emission=glm::vec3(.1f);
    auto afterBarrier=Render::RefitRealtimeGiSceneChanged(*barrier,barrierValues,first,true);
    fullFallback(*afterBarrier,Fallback::FullRefitBarrier);equivalent(*afterBarrier,*barrierRoot,barrierValues);
    Check(SameSnapshotBits(barrier->Pixels(),barrierBits),"Crossing a refit barrier mutated its retained image");
    // Keep every generation alive so an ancient buffer becomes available only
    // after its required parent edges have expired from the bounded journal.
    auto overflowRoot=Render::BuildRealtimeGiScene(baseline);auto overflowValues=baseline;
    std::vector<std::shared_ptr<const Scene>> held{overflowRoot};std::vector<std::vector<glm::vec4>> heldBits{overflowRoot->Pixels()};
    for(std::size_t step=0;step<Scene::SnapshotJournalCapacity+4;++step){
        const std::uint32_t changed=std::uint32_t((step*97)%overflowValues.size());
        overflowValues[changed].emission=glm::vec3(float(step%5)*.1f);
        auto next=Render::RefitRealtimeGiSceneChanged(*held.back(),overflowValues,std::span(&changed,1),true);
        heldBits.push_back(next->Pixels());held.push_back(std::move(next));
    }
    const auto overflowAddress=held[1]->Pixels().data();held[1].reset();overflowValues[0].roughness=.4f;
    auto overflow=Render::RefitRealtimeGiSceneChanged(*held.back(),overflowValues,first,true);
    fullFallback(*overflow,Fallback::MissingJournal);equivalent(*overflow,*overflowRoot,overflowValues);
    Check(overflow->Pixels().data()==overflowAddress,"Journal overflow failed to reuse capacity with a complete copy");
    for(std::size_t i=0;i<held.size();++i)if(held[i])
        Check(SameSnapshotBits(held[i]->Pixels(),heldBits[i]),"Journal expiry mutated a held immutable generation");
    // Equal dimensions do not permit reuse across a rebuilt topology/pool.
    auto topologyRoot=Render::BuildRealtimeGiScene(baseline);auto disposable=Render::RefitRealtimeGiSceneChanged(*topologyRoot,baseline,{},true);
    disposable.reset();auto reordered=baseline;std::reverse(reordered.begin(),reordered.end());
    auto topology=Render::BuildRealtimeGiScene(reordered);
    auto isolated=Render::RefitRealtimeGiSceneChanged(*topology,reordered,{},true);
    fullFallback(*isolated,Fallback::PoolEmpty);
    Check(topology->TopologyKey()!=topologyRoot->TopologyKey()&&topology->Pixels().size()==topologyRoot->Pixels().size()&&
        SameSnapshotBits(isolated->Pixels(),topology->Pixels()),"Rebuilt topology consumed another topology's retired storage");
    // Empty deltas need no pixel copies, but still require the actual parent edge.
    auto stableRoot=Render::BuildRealtimeGiScene(baseline);
    auto stableA=Render::RefitRealtimeGiSceneChanged(*stableRoot,baseline,{},true);
    auto stableB=Render::RefitRealtimeGiSceneChanged(*stableA,baseline,{},true);stableA.reset();
    auto stableC=Render::RefitRealtimeGiSceneChanged(*stableB,baseline,{},true);
    Check(stableC->RefitCpuStatistics().snapshotCatchupUsed&&stableC->RefitCpuStatistics().snapshotCopiedBytes==0&&
        stableC->RefitCpuStatistics().snapshotPatchedRows==0&&SameSnapshotBits(stableC->Pixels(),stableRoot->Pixels()),
        "A certified empty parent delta copied pixels or changed geometry");
    std::cout<<"Snapshot catch-up CPU contracts passed: bitwise full equivalence, multi-parent rows="<<rows<<" copied_bytes="<<copied
        <<" full_bytes="<<fullBytes(*patched)<<" immutable_held=1 gpu_deltas_unchanged=1 old_base_branch=1 missing_edge=1"
        <<" full_refit_barrier=1 journal_overflow=1 topology_isolation=1 empty_delta=1\n";
}
} // namespace
int main() {
    try {
        const auto cube = Cube(8); const auto mesh = Render::BakeProbeMeshSdf(cube, 28, true);
        Check(mesh->Closed(), "Watertight subdivided cube was not signed");
        Check(mesh->Sample({0, 0, 0}) < -.45f, "Closed mesh interior must be negative");
        Check(mesh->Sample({.6f, 0, 0}) > .09f, "Closed mesh exterior must be positive");
        const auto& bake = mesh->Statistics();
        const auto bruteForceTests = bake.voxels * cube.size();
        Check(bake.distanceTriangleTests < bruteForceTests / 3, "Triangle BVH did not reduce nearest-distance triangle tests");
        for (int z = -8; z <= 8; ++z) for (int y = -8; y <= 8; ++y) for (int x = -8; x <= 8; ++x) {
            const glm::vec3 p = glm::vec3(x, y, z) * .1f;
            const float bound = mesh->ConservativeDistance(p);
            Check(std::isfinite(bound) && bound >= 0 && bound <= std::abs(BoxSdf(p)) + 2e-5f, "Mesh distance advance is not conservative");
        }
        const std::array<Render::ProbeTriangle, 2> plane{{{{-.5f, 0, -.5f}, {.5f, 0, -.5f}, {.5f, 0, .5f}}, {{-.5f, 0, -.5f}, {.5f, 0, .5f}, {-.5f, 0, .5f}}}};
        const auto open = Render::BakeProbeMeshSdf(plane, 20, true);
        Check(!open->Closed(), "Open plane must fall back to unsigned distance");
        Check(open->Sample({0, .1f, 0}) > .09f && open->Sample({0, -.1f, 0}) > .09f, "Unsigned plane distance must preserve both sides");

        Render::ProbeGlobalSdfSettings settings; settings.origin = {-5, -2, -5}; settings.spacing = {.2f, .2f, .22f}; settings.counts = {51, 31, 49}; settings.brickSize = 4; settings.maxDistance = .6f;
        std::vector<Render::ProbeSdfInstance> instances{{mesh, glm::translate(glm::mat4(1), glm::vec3(-2, 0, 0)), false, 17}, {mesh, glm::translate(glm::mat4(1), glm::vec3(1, 0, 0)), true, 23}};
        auto scene = Render::BuildProbeSdfScene(instances, settings);
        Check(scene->Statistics().meshAssets == 1 && scene->Statistics().bakedVoxels == bake.voxels, "Instanced meshes did not share one bake");
        Check(scene->ObjectDistance(1, {1, 0, 0}) < -.45f, "Object transform did not preserve signed interior");
        const auto initial = scene->GlobalPixels(); const auto generation = scene->Statistics().generation, meshGeneration = scene->MeshGeneration();
        const auto geometryTopology = scene->PackedGeometry()->TopologyKey();
        Check(!scene->UpdateInstances(instances) && scene->Statistics().generation == generation, "No-op SDF update changed generation");
        Check(scene->DirtyBricks().empty() && scene->DirtyRanges().empty(), "No-op update reports dirty data");
        instances[1].localToWorld = glm::translate(glm::mat4(1), glm::vec3(2.3f, 0, .5f));
        Check(scene->UpdateInstances(instances), "Moved instance did not dirty global SDF");
        const auto movedStats = scene->Statistics();
        Check(!movedStats.fullRebuild && movedStats.updatedVoxels > 0 && movedStats.updatedVoxels < movedStats.totalVoxels / 2, "Movement unnecessarily rebuilt the entire global SDF");
        Check(movedStats.staticVoxelUpdates == 0 && movedStats.dynamicVoxelUpdates == movedStats.updatedVoxels, "Movement touched the cached static layer");
        Check(scene->MeshGeneration() == meshGeneration && scene->PackedGeometry()->TopologyKey() == geometryTopology, "Movement rebaked a mesh or rebuilt BVH topology");
        for (std::size_t i = 0; i < initial.size(); ++i) Check(initial[i].z == scene->GlobalPixels()[i].z, "Static cache changed after dynamic motion");
        EqualFields(*scene, *Render::BuildProbeSdfScene(instances, settings));
        std::uint64_t dirtyRangeVoxels = 0; for (auto range : scene->DirtyRanges()) dirtyRangeVoxels += range.count;
        Check(dirtyRangeVoxels == movedStats.updatedVoxels, "Partial upload ranges do not cover exactly the changed voxels");

        // Rotated, strongly nonuniform scale tests both atlas-domain padding
        // and the world-bounds guard used by dirty-region composition.
        instances[1].localToWorld = glm::translate(glm::mat4(1), glm::vec3(1.7f, .4f, -.3f)) * glm::rotate(glm::mat4(1), .43f, glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1), glm::vec3(2.1f, .15f, .7f));
        scene->UpdateInstances(instances); EqualFields(*scene, *Render::BuildProbeSdfScene(instances, settings));
        Check(scene->ObjectDistance(1, {1.7f, .4f, -.3f}) < 0, "Nonuniform scale lost negative interior");
        const float expectedError = mesh->InterpolationError() * .15f;
        Check(std::abs(scene->ObjectPixels()[Render::ProbeSdfObjectTexels + 9].y - expectedError) < 1e-6f, "Nonuniform mesh error must match min-scale distance units");
        const auto beforeStaticMove = scene->GlobalPixels(); instances[0].localToWorld = glm::translate(glm::mat4(1), glm::vec3(-2, .5f, 0));
        scene->UpdateInstances(instances); EqualFields(*scene, *Render::BuildProbeSdfScene(instances, settings));
        Check(scene->Statistics().dynamicVoxelUpdates == 0, "Static change touched movable cache");
        for (std::size_t i = 0; i < beforeStaticMove.size(); ++i) Check(beforeStaticMove[i].w == scene->GlobalPixels()[i].w, "Dynamic cache changed after static motion");
        auto lazy=Render::BuildProbeSdfScene(instances,settings);
        auto lazyInstances=instances;const auto lazyGeometry=lazy->GeometryGeneration();
        for(unsigned step=0;step<3;++step){lazyInstances[1].localToWorld[3].x+=.15f;
            Check(lazy->UpdateInstances(lazyInstances,true)&&lazy->GlobalFieldDeferred(),"Triangle profile did not defer unused global field");
            Check(lazy->GeometryGeneration()>lazyGeometry&&lazy->Statistics().updatedVoxels==0,"Deferred field changed voxel work or lost current geometry");}
        EqualFields(*lazy,*Render::BuildProbeSdfScene(lazyInstances,settings));
        auto eagerGeometry=Render::BuildProbeSdfScene(instances,settings),cachedGeometry=Render::BuildProbeSdfScene(instances,settings);
        auto geometryInstances=instances;
        for(unsigned step=0;step<5;++step){geometryInstances[step%instances.size()].localToWorld[3].z+=.035f;
            auto uploadedGeometry=cachedGeometry->PackedGeometry()->Pixels();
            eagerGeometry->UpdateInstances(geometryInstances,true,false);
            cachedGeometry->UpdateInstances(geometryInstances,true,step!=3);
            Check(eagerGeometry->PackedGeometry()->Pixels()==cachedGeometry->PackedGeometry()->Pixels(),
                "Cached transformed geometry diverged after motion or execution-policy change");
            Check(eagerGeometry->ObjectPixels()==cachedGeometry->ObjectPixels(),
                "Changed-instance metadata lost an inverse transform, atlas offset or bound");
            if(cachedGeometry->PackedGeometry()->IncrementalUpdate()){
                const auto& pixels=cachedGeometry->PackedGeometry()->Pixels();for(const auto& range:cachedGeometry->PackedGeometry()->DirtyRanges())
                    std::copy(pixels.begin()+range.first,pixels.begin()+range.first+range.count,uploadedGeometry.begin()+range.first);
                Check(uploadedGeometry==pixels,"Sparse geometry uploads omitted a changed triangle or ancestor bound");}
            Check(eagerGeometry->PackedGeometry()->HasEmission()==cachedGeometry->PackedGeometry()->HasEmission(),"Sparse geometry lost emission state");}
        const auto geometryTimings=cachedGeometry->Statistics();
        Check(std::isfinite(geometryTimings.geometryTransformMilliseconds)&&geometryTimings.geometryTransformMilliseconds>=0&&
            std::isfinite(geometryTimings.geometrySnapshotMilliseconds)&&geometryTimings.geometrySnapshotMilliseconds>=0&&
            std::isfinite(geometryTimings.geometryRefitMilliseconds)&&geometryTimings.geometryRefitMilliseconds>=0&&
            std::isfinite(geometryTimings.metadataMilliseconds)&&geometryTimings.metadataMilliseconds>=0,
            "Incremental geometry CPU detail timings are invalid");
        Check(!cachedGeometry->UpdateInstances(geometryInstances,true,true),"Unchanged timed geometry unexpectedly updated");
        const auto idleGeometryTimings=cachedGeometry->Statistics();
        Check(idleGeometryTimings.geometryTransformMilliseconds==0&&idleGeometryTimings.geometrySnapshotMilliseconds==0&&
            idleGeometryTimings.geometryRefitMilliseconds==0&&idleGeometryTimings.metadataMilliseconds==0,
            "Unchanged geometry retained stale CPU detail timings");
        // Sparse material changes must preserve the immutable old snapshot,
        // share only topology, and handle both adding and removing the last
        // emissive triangle. Duplicate certified indices are harmless.
        std::vector<Render::ProbeTriangle> emissionTriangles(plane.begin(),plane.end());
        auto unlitGeometry=Render::BuildRealtimeGiScene(emissionTriangles);
        const auto unlitPixels=unlitGeometry->Pixels();
        const std::array<std::uint32_t,2> repeatedChange{{1,1}};
        emissionTriangles[1].emission=glm::vec3(.5f,.25f,.125f);
        auto litGeometry=Render::RefitRealtimeGiSceneChanged(*unlitGeometry,emissionTriangles,repeatedChange);
        Check(litGeometry->HasEmission()&&!unlitGeometry->HasEmission()&&unlitGeometry->Pixels()==unlitPixels,
            "Sparse emission change mutated a retained immutable geometry snapshot");
        Check(litGeometry->TopologyKey()==unlitGeometry->TopologyKey()&&
            &litGeometry->InputTriangleIndices()==&unlitGeometry->InputTriangleIndices(),
            "A sparse rigid/material refit copied or replaced immutable topology maps");
        Check(litGeometry->Pixels()==Render::RefitRealtimeGiScene(*unlitGeometry,emissionTriangles)->Pixels(),
            "Sparse emission addition differs from full refit");
        emissionTriangles[1].emission=glm::vec3(0);
        auto extinguishedGeometry=Render::RefitRealtimeGiSceneChanged(*litGeometry,emissionTriangles,repeatedChange);
        Check(!extinguishedGeometry->HasEmission()&&litGeometry->HasEmission()&&extinguishedGeometry->Pixels()==unlitPixels,
            "Removing the last sparse emitter lost emission state or changed unrelated geometry");
        // Capacity recycling starts only after the final snapshot reference
        // dies. Retain several generations to exercise the allocation fallback
        // and verify every old image remains unchanged while the pool is used.
        auto poolTriangles=emissionTriangles;
        auto poolBase=Render::BuildRealtimeGiScene(poolTriangles);
        auto disposable=Render::RefitRealtimeGiSceneChanged(*poolBase,poolTriangles,{});
        const auto recycledAddress=disposable->Pixels().data();disposable.reset();
        const std::array<std::uint32_t,1> poolChanged{{1}};
        poolTriangles[1].emission=glm::vec3(.1f);
        auto pooled=Render::RefitRealtimeGiSceneChanged(*poolBase,poolTriangles,poolChanged);
        Check(pooled->RefitCpuStatistics().snapshotStorageReused&&pooled->Pixels().data()==recycledAddress&&
            pooled->Pixels()==Render::RefitRealtimeGiScene(*poolBase,poolTriangles)->Pixels(),
            "A retired snapshot capacity was not safely reused for a full-equivalent sparse refit");
        std::vector<std::shared_ptr<const Render::RealtimeGiScene>> retainedSnapshots{poolBase,pooled};
        std::vector<std::vector<glm::vec4>> retainedPixels{poolBase->Pixels(),pooled->Pixels()};
        for(unsigned step=0;step<7;++step){poolTriangles[1].a.x+=.003f;poolTriangles[1].emission=glm::vec3(float(step%3)*.1f);
            auto next=Render::RefitRealtimeGiSceneChanged(*retainedSnapshots.back(),poolTriangles,poolChanged);
            Check(next->Pixels()==Render::RefitRealtimeGiScene(*poolBase,poolTriangles)->Pixels(),
                "Snapshot capacity fallback changed sparse geometry or material results");
            for(std::size_t old=0;old<retainedSnapshots.size();++old)
                Check(retainedSnapshots[old]->Pixels()==retainedPixels[old]&&
                    retainedSnapshots[old]->Pixels().data()!=next->Pixels().data(),
                    "A retained immutable snapshot storage was recycled before its last owner released it");
            retainedPixels.push_back(next->Pixels());retainedSnapshots.push_back(std::move(next));}
        pooled.reset();retainedSnapshots.clear();
        auto afterRetirement=Render::RefitRealtimeGiSceneChanged(*poolBase,poolTriangles,poolChanged);
        Check(afterRetirement->RefitCpuStatistics().snapshotStorageReused&&
            afterRetirement->Pixels()==Render::RefitRealtimeGiScene(*poolBase,poolTriangles)->Pixels()&&poolBase->Pixels()==retainedPixels[0],
            "Multiple retired snapshots did not recycle capacity without modifying the retained base");
        SnapshotCatchupContracts();
        Check(!lazy->GlobalFieldDeferred(),"Reading a deferred field did not catch up");
        lazyInstances[0].localToWorld[3].z+=.25f;lazy->UpdateInstances(lazyInstances,true);
        Check(lazy->UpdateInstances(lazyInstances)&&!lazy->GlobalFieldDeferred(),"Switching to software without motion did not catch up");
        EqualFields(*lazy,*Render::BuildProbeSdfScene(lazyInstances,settings));
        std::cout << "Mesh SDF signed/open and conservative samples passed. BVH triangle tests " << bake.distanceTriangleTests << "/" << bruteForceTests
                  << ". Dynamic update " << movedStats.updatedBricks << "/" << movedStats.totalBricks << " bricks, " << movedStats.updatedVoxels << "/" << movedStats.totalVoxels
                  << " voxels in " << movedStats.updateMilliseconds << " ms. Shared bake, static cache, partial uploads, odd volume, nonuniform scale and full-union equivalence passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
