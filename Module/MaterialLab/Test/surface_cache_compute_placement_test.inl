// Include after surface_cache_budget_test.inl. No timing or automatic readback
// enters production: both original replay draws and all reads are explicit QA.
namespace ComputePlacementComparisonFixture {
using namespace SurfaceCacheBudgetFixture;
std::uint64_t CompareBits(const std::vector<glm::vec4>& a,const std::vector<glm::vec4>& b,const char* label){
    Check(!a.empty()&&a.size()==b.size(),"Compute placement comparison extent differs");
    std::uint64_t changed=0;
    for(std::size_t i=0;i<a.size();++i)for(unsigned c=0;c<4;++c){
        Check(std::isfinite(a[i][c])&&std::isfinite(b[i][c]),"Nonfinite compute placement record");
        if(std::bit_cast<std::uint32_t>(a[i][c])!=std::bit_cast<std::uint32_t>(b[i][c])){
            if(!changed)std::cout<<"GI90 PLACEMENT_COMPARE first_difference="<<label<<" texel="<<i<<" component="<<c
                <<" before="<<a[i][c]<<" after="<<b[i][c]<<std::endl;
            ++changed;
        }
    }
    return changed;
}
struct Totals {std::uint64_t frames=0,texels=0,secondary=0,curvature=0,reused=0;};
void Inspect(MaterialLabModule& lab,const FrameSettings& frame,const char* scene,unsigned gates,bool reused,Totals& totals){
    const auto stats=lab.PipelineStatistics();const auto read=lab.ReadbackScreenProbePlacementComparison();
    Check(read.width>0&&read.height>0&&stats.screenProbePlacementComparisonCollected,lab.LastError().empty()?"Missing placement comparison":lab.LastError());
    Check(read.reused==reused&&stats.screenProbePlacementComparisonReused==reused,"Placement comparison reuse differs from freeze semantics");
    const auto& p=frame.effects.screenProbePlacement;
    const auto gw=(frame.width+p.baseSpacing-1)/p.baseSpacing,gh=(frame.height+p.baseSpacing-1)/p.baseSpacing;
    const auto slots=std::uint64_t(gw)*gh*p.candidateCount;
    Check(read.width==gw*p.candidateCount*2&&read.height==gh,"Placement comparison lost interleaved candidate layout");
    for(const auto& image:read.images)Check(image.size()==slots*2,"Placement comparison omitted inactive atlas texels");
    if(reused)Check(!stats.screenProbeComputePlacement&&stats.screenProbeMarkInvocationCapacity==0&&stats.screenProbePlacementComparisonPasses==0,
        "Frozen placement unexpectedly recomputed its representatives");
    else Check(stats.screenProbeComputePlacement&&stats.screenProbeMarkInvocationCapacity==slots&&
        stats.screenProbeLegacyMarkInvocationCapacity==slots*2&&stats.screenProbePlacementComparisonPasses==2,
        "Compute placement did not halve logical Mark invocation capacity");
    const auto marked=CompareBits(read.images[2],read.images[0],"marked"),spawned=CompareBits(read.images[3],read.images[1],"spawned");
    std::uint64_t secondary=0,curvature=0;
    for(std::size_t slot=0;slot<slots;++slot){const auto state=read.images[0][slot*2].w;
        secondary+=(slot%p.candidateCount)%2==1&&state>.5f;curvature+=state>5.f&&state<6.01f;}
    std::cout<<"GI90 PLACEMENT_COMPARE scene="<<scene<<" gates="<<gates<<" source_raster="<<frame.effects.surfaceCacheGi.rasterSourcePositions
        <<" candidates="<<p.candidateCount<<" spacing="<<p.baseSpacing<<" width="<<frame.width<<" height="<<frame.height
        <<" jitter="<<p.frameIndex<<" frozen_reused="<<reused<<" marked_bitwise="<<(marked==0)<<" spawned_bitwise="<<(spawned==0)
        <<" physical_texels="<<slots*2<<" secondary="<<secondary<<" curvature="<<curvature
        <<" logical_mark_invocations="<<stats.screenProbeMarkInvocationCapacity<<" legacy_logical_mark_invocations="<<stats.screenProbeLegacyMarkInvocationCapacity
        <<" untimed=1 same_frame=1 placement_speed_claim=0"<<std::endl;
    Check(marked==0&&spawned==0,"Compute placement changed original Mark/Spawn FP32 bits");
    ++totals.frames;totals.texels+=slots*2;totals.secondary+=secondary;totals.curvature+=curvature;totals.reused+=reused;
}
FrameSettings Scene(const Options& options,unsigned scene){
    auto frame=Make(options,14,true);frame.width=321;frame.height=203;frame.effects.outputSize={0,0};frame.present=options.benchmarkPresent;
    auto& gi=frame.effects.surfaceCacheGi;gi.nativeComputePlacement=true;gi.collectNativePlacementComparison=true;
    gi.contact.collectSamplingDiagnostics=false;gi.contact.collectTraceDiagnostics=false;frame.effects.screenProbePlacement.trace.sdf.collectRayDiagnostics=false;
    gi.cacheSettledGather=false;
    if(scene){
        frame.sealedGiRoomStudy=false;
        auto objects=std::make_shared<std::vector<ProbePlacementObject>>(std::initializer_list<ProbePlacementObject>{
            {"placement floor",ShowcaseMesh::Plane,ProbePlacementMaterial::Floor,{0,0,0},{4,1,4}},
            {"placement back face",ShowcaseMesh::Box,ProbePlacementMaterial::Wall,{-.3f,1.f,0},{1.5f,1.f,.08f}},
            {"placement foreground",ShowcaseMesh::Box,ProbePlacementMaterial::Block,{.3f,.9f,.8f},{.55f,.9f,.08f},22},
            {"placement curved receiver",ShowcaseMesh::Sphere,ProbePlacementMaterial::Sphere,{-1.2f,.65f,1.1f},{.65f,.65f,.65f}}});
        frame.probeSceneOverride=objects;frame.probeSceneMaterialsOverride=std::make_shared<std::vector<SealedGiRoomMaterial>>(objects->size());
        auto camera=ProbePlacementCameraPreset(0,frame.OutputAspect());camera.position={0,2.3f,5.8f};
        camera.view=glm::lookAt(camera.position,scene==1?glm::vec3(0,.8f,0):glm::vec3(0,8,9),glm::vec3(0,1,0));frame.camera=camera;
    }
    return frame;
}
void Run(MaterialLabModule& lab,const Options& options){
    Check(options.vulkan&&!options.softwareRays,"Compute placement comparison requires Vulkan frame compute");
    constexpr std::array<const char*,3> names{{"sealed_curve_junction","two_surface_curve","empty_view"}};
    Totals totals;
    for(unsigned scene=0;scene<names.size();++scene){const auto base=Scene(options,scene);
      for(unsigned candidates:{4u,8u,16u})for(unsigned raster=0;raster<2;++raster){
        auto frame=base;auto& p=frame.effects.screenProbePlacement;p.candidateCount=candidates;p.baseSpacing=31;
        frame.effects.surfaceCacheGi.rasterSourcePositions=raster!=0;
        p.cornerRefinement=true;p.curvatureRefinement=true;p.reconstructionVisibility=true;p.curvatureAwareWeights=true;
        for(unsigned phase=0;phase<8;++phase){p.frameIndex=phase;Reset(frame);Draw(lab,frame);Inspect(lab,frame,names[scene],7,false,totals);}
        p.freeze=true;Draw(lab,frame);Inspect(lab,frame,names[scene],7,true,totals);
        p.reset=true;Draw(lab,frame);Inspect(lab,frame,names[scene],7,false,totals);
        const auto computed=lab.ReadbackScreenProbePlacement();
        frame.effects.surfaceCacheGi.nativeComputePlacement=false;frame.effects.surfaceCacheGi.collectNativePlacementComparison=false;
        p.freeze=false;Draw(lab,frame);const auto fallback=lab.ReadbackScreenProbePlacement();const auto stats=lab.PipelineStatistics();
        Check(!stats.screenProbeComputePlacement&&!stats.screenProbePlacementComparisonCollected&&lab.ReadbackScreenProbePlacementComparison().images[0].empty(),
            "Disabled compute placement exposed a stale comparison or missed original fallback");
        Check(CompareBits(computed.marked,fallback.marked,"fallback_marked")==0&&CompareBits(computed.markedNormals,fallback.markedNormals,"fallback_normals")==0&&
            CompareBits(computed.spawned,fallback.spawned,"fallback_spawned")==0&&CompareBits(computed.spawnedNormals,fallback.spawnedNormals,"fallback_spawned_normals")==0,
            "Disabled compute placement changed the retained original producer");
      }
    }
    // Independent gates and very short edge cells exercise the same producer,
    // not a CPU approximation or a separately ported representative selector.
    const auto edgeBase=Scene(options,1);
    for(unsigned gates=0;gates<8;++gates){auto frame=edgeBase;frame.width=17;frame.height=11;
        auto& p=frame.effects.screenProbePlacement;p.candidateCount=16;p.baseSpacing=9;p.frameIndex=gates;
        p.cornerRefinement=bool(gates&1);p.curvatureRefinement=bool(gates&2);p.reconstructionVisibility=bool(gates&4);p.curvatureAwareWeights=bool(gates&2);
        Reset(frame);Draw(lab,frame);Inspect(lab,frame,"short_edge_cells",gates,false,totals);}
    Check(totals.secondary>0,"Placement comparison did not exercise the second surface representative");
    std::cout<<"GI90 PLACEMENT_COMPARE COMPLETE accepted=1 untimed=1 frames="<<totals.frames<<" texels="<<totals.texels
        <<" reused="<<totals.reused<<" secondary="<<totals.secondary<<" curvature="<<totals.curvature
        <<" marked_bitwise=1 spawned_bitwise=1 no_pack=1 logical_mark_capacity_halved=1 placement_speed_claim=0"<<std::endl;
}
}
void SurfaceCacheComputePlacementComparison(MaterialLabModule& lab,const Options& options){ComputePlacementComparisonFixture::Run(lab,options);}
