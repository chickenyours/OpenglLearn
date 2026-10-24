// Direct area-light depth checks are untimed and separate from the ordinary
// GI parity/interactive sequences. Discard/abort and arbitrary DrawIndexed
// contracts live in the low-level pipeline/backend tests.
void SurfaceCacheShadowSubmissionContracts(MaterialLabModule& lab,const Options& options) {
    using namespace SurfaceCacheNativeFixture;
    Check(options.vulkan&&!options.softwareRays,"Shadow submission contracts require capability-enabled Vulkan");
    auto frame=Frame(options,true);frame.present=false;frame.width=192;frame.height=128;
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;
    gi.contact.collectSamplingDiagnostics=false;gi.temporal.enabled=false;gi.spatial.enabled=false;
    placement.baseSpacing=32;placement.candidateCount=8;placement.trace.rayResolution=4;
    const auto originalLight=frame.effects.areaLights.lights[0];
    const auto originalDistance=gi.maxDistance;
    frame.camera=SealedGiRoomCameraPreset(0,frame.OutputAspect());
    gi.areaShadows.tileResolution=64;frame.effects.areaLights.samplesPerAxis=4;
    Mode(frame,false,true,15,false,true);Reset(frame);Draw(lab,frame);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
    while(lab.ProbeSdfQualityStatistics().pending){Draw(lab,frame);
        Check(std::chrono::steady_clock::now()<deadline,"Shadow contract scene preparation timed out");}
    std::uint64_t comparedPixels=0,culledFaces=0,preparedObjectBytes=0;
    unsigned pairs=0,cacheChecks=0,radianceChecks=0,disabledChecks=0,extentChanges=0,snapshotModeChecks=0;
    glm::uvec2 previousExtent(0);
    for(unsigned test=0;test<12;++test){
        const unsigned mask=test<8?15u:1u<<(test-8);
        frame.width=test==2?191:192;frame.height=test==2?123:128;
        frame.effects.areaLights.lights[0]=originalLight;
        frame.effects.areaLights.samplesPerAxis=test==6?8:4;
        gi.areaShadows.tileResolution=test==6?128:64;
        gi.areaShadows.nearPlane=test==5?.022f:.015f;
        gi.maxDistance=test==5?20.f:originalDistance;
        frame.screenProbeStudyState.occluderOffset=test==1?.23f:test==6?.41f:0.f;
        if(test==3)frame.effects.areaLights.lights[0].center.x+=.17f;
        if(test==4){frame.effects.areaLights.lights[0].halfAxisU*=.8f;frame.effects.areaLights.lights[0].halfAxisV*=.9f;}
        frame.camera=SealedGiRoomCameraPreset(test==2?1:0,frame.OutputAspect());
        // Snapshot-only mode correctly leaves the shadow geometry cache key
        // unchanged. Force a real refresh for both groups through a reversible
        // emitter edit, instead of mistaking a cache hit for zero-cost redraw.
        const auto refresh=[&](bool candidate){Mode(frame,candidate,true,mask,false,true);
            const auto center=frame.effects.areaLights.lights[0].center;
            frame.effects.areaLights.lights[0].center.x+=.03125f;Draw(lab,frame);
            frame.effects.areaLights.lights[0].center=center;Reset(frame);Draw(lab,frame);
            Effective(lab,candidate,true,mask,false,true);};
        refresh(false);
        const auto before=lab.ReadbackAreaLightShadowDepth();const auto beforeSignature=AreaDepthSignature(before);
        const auto beforeStats=lab.PipelineStatistics();
        refresh(true);
        const auto after=lab.ReadbackAreaLightShadowDepth();const auto afterSignature=AreaDepthSignature(after);
        const auto afterStats=lab.PipelineStatistics();
        Check(beforeSignature==afterSignature,"Shadow focused pair changed atlas extent, exact bits or coverage");
        comparedPixels+=CompareAreaDepth(before.depth,after.depth);++pairs;
        Check(beforeStats.areaLightShadowPasses==afterStats.areaLightShadowPasses&&
            beforeStats.areaLightShadowCasterCount==afterStats.areaLightShadowCasterCount,
            "Shadow focused pair changed emitter quadrature or eligible caster set");
        if(mask&2){Check(afterStats.areaLightShadowCasterParameterBytes*beforeStats.areaLightShadowPasses==beforeStats.areaLightShadowCasterParameterBytes,
                "Prepared shadow material uploads changed bytes or repeated per-face work");
            preparedObjectBytes+=afterStats.areaLightShadowCasterObjectBytes;}
        if(mask&8)culledFaces+=afterStats.areaLightShadowCulledCasters;
        if(previousExtent!=glm::uvec2(after.width,after.height)&&previousExtent.x)++extentChanges;
        previousExtent={after.width,after.height};
        // Exactly one completed frame through the existing timing callback
        // proves actual backend mode; this is not a performance sample.
        const auto cached=lab.MeasureFrames(frame,1,0);Effective(lab,true,true,mask,false,true);
        Check(cached.uniformSnapshots.size()==1&&cached.uniformSnapshots.front()[0]==std::uint64_t(bool(mask&1)),
            "Focused shadow contract did not select the actual uniform snapshot mode");++snapshotModeChecks;
        const auto cachedDepth=lab.ReadbackAreaLightShadowDepth();
        Check(cachedDepth.cacheUsed&&!cachedDepth.updated,"Unchanged shadow frame did not reuse its completed atlas");
        comparedPixels+=CompareAreaDepth(after.depth,cachedDepth.depth);++cacheChecks;
        if(test==0||test==9){
            frame.effects.areaLights.lights[0].radiance*=.79f;Draw(lab,frame);Effective(lab,true,true,mask,false,true);
            const auto radianceDepth=lab.ReadbackAreaLightShadowDepth();
            Check(radianceDepth.cacheUsed&&!radianceDepth.updated,"Radiance-only light edit unnecessarily rebuilt geometry depth");
            comparedPixels+=CompareAreaDepth(after.depth,radianceDepth.depth);++radianceChecks;
        }
        if(test==7){
            gi.areaShadows.enabled=false;Draw(lab,frame);
            Check(lab.PipelineStatistics().areaLightShadowPasses==0,"Disabled area shadows issued depth passes");
            gi.areaShadows.enabled=true;Draw(lab,frame);Effective(lab,true,true,mask,false,true);
            const auto restored=lab.ReadbackAreaLightShadowDepth();AreaDepthSignature(restored);
            comparedPixels+=CompareAreaDepth(after.depth,restored.depth);++disabledChecks;
        }
        std::cout<<"SHADOW SUBMISSION CONTRACT pair="<<test<<" candidate_mask="<<mask<<" render_size="<<frame.width<<'x'<<frame.height
            <<" atlas_size="<<after.width<<'x'<<after.height<<" baseline_draws="<<beforeStats.areaLightShadowCasterDraws
            <<" candidate_draws="<<afterStats.areaLightShadowCasterDraws<<" culled_caster_faces="<<afterStats.areaLightShadowCulledCasters
            <<" baseline_object_bytes="<<beforeStats.areaLightShadowCasterObjectBytes<<" candidate_object_bytes="<<afterStats.areaLightShadowCasterObjectBytes
            <<" covered_depth_pixels="<<afterSignature[3]<<" complete_atlas_bit_exact=1 timed=0"<<std::endl;
    }
    Check(pairs==12&&cacheChecks==12&&snapshotModeChecks==12&&radianceChecks==2&&disabledChecks==1&&extentChanges>=2&&culledFaces>0&&preparedObjectBytes>0,
        "Shadow focused contracts did not exercise all modes, resizing, culling, cache and lighting transitions");
    Mode(frame,false,true,15,false,true);Reset(frame);Draw(lab,frame);
    std::cout<<"SHADOW SUBMISSION CONTRACT PASS pairs="<<pairs<<" direct_depth_pixels="<<comparedPixels<<" cache_hit_checks="<<cacheChecks
        <<" radiance_only_checks="<<radianceChecks<<" disable_restore_checks="<<disabledChecks<<" atlas_extent_changes="<<extentChanges
        <<" actual_snapshot_mode_checks="<<snapshotModeChecks<<" culled_caster_faces="<<culledFaces
        <<" complete_depth_including_clear_pixels=1 arbitrary_draw_ranges_and_discard_in_low_level_contracts=1 timing_claim=0"<<std::endl;
}
