// Execution-only pruning of the distance-field GPU mirrors. The CPU field
// state, native shader, ray estimator and shared Surface Cache geometry remain
// unchanged. Raw atlas checks independently compare GPU uploads to CPU data.
void SurfaceCacheNativeUploadPruningContracts(MaterialLabModule& lab,const Options& options) {
    using namespace SurfaceCacheNativeFixture;
    Check(options.vulkan&&!options.softwareRays,"Native upload contracts require capability-enabled Vulkan");
    auto frame=Frame(options,true);frame.present=false;frame.width=192;frame.height=128;
    auto& placement=frame.effects.screenProbePlacement;auto& trace=placement.trace;auto& gi=frame.effects.surfaceCacheGi;
    placement.baseSpacing=32;placement.candidateCount=8;trace.rayResolution=4;trace.samplingSeed=31;
    trace.sdf.showHitPoints=false;trace.sdf.sliceView=0;trace.sdf.visualMode=0;
    gi.contact.collectSamplingDiagnostics=false;gi.areaShadows.tileResolution=64;
    frame.effects.areaLights.samplesPerAxis=4;frame.camera=SealedGiRoomCameraPreset(0,frame.OutputAspect());
    Mode(frame,true,false,15,false,false,false,true,3);trace.sdf.nativeUploadPruning=true;
    Reset(frame);Draw(lab,frame);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
    while(lab.ProbeSdfQualityStatistics().pending){Draw(lab,frame);
        Check(std::chrono::steady_clock::now()<deadline,"Native upload contract scene preparation timed out");}
    std::uint64_t comparedChannels=0,atlasChannels=0;
    unsigned pairs=0,historyToggles=0,catchups=0,fallbacks=0,prunedMovedFrames=0;
    std::string phase="first_requested_pruned_frame";
    const auto exact=[&](const auto& before,const auto& after,const char* label,bool atlas=false) {
        Check(!before.empty()&&before.size()==after.size(),std::string("Native upload contract output missing/changed extent: ")+label);
        for(std::size_t texel=0;texel<before.size();++texel)for(unsigned component=0;component<4;++component) {
            Check(std::isfinite(before[texel][component])&&std::isfinite(after[texel][component]),"Native upload contract output became nonfinite");
            if(std::bit_cast<std::uint32_t>(before[texel][component])!=std::bit_cast<std::uint32_t>(after[texel][component])) {
                std::cout<<"NATIVE UPLOAD CONTRACT mismatch stage="<<label<<" texel="<<texel<<" component="<<component
                    <<std::setprecision(12)<<" before="<<before[texel][component]<<" after="<<after[texel][component]<<std::endl;
                Check(false,"Native upload pruning changed exact FP32 output bits");
            }
            if(atlas)++atlasChannels;else ++comparedChannels;
        }
    };
    const auto active=[&](bool expected) {
        const auto stats=lab.PipelineStatistics();
        if(stats.probeSdfNativeUploadsPruned!=expected)std::cout<<"NATIVE UPLOAD CONTRACT execution_mismatch phase="<<phase
            <<" expected="<<expected<<" actual="<<stats.probeSdfNativeUploadsPruned<<" native="<<stats.probeSdfHardwareRayTracing
            <<" requested_prune="<<trace.sdf.nativeUploadPruning<<" requested_native="<<trace.sdf.hardwareRayTracing
            <<" show_hits="<<trace.sdf.showHitPoints<<" slice="<<trace.sdf.sliceView<<" visual="<<trace.sdf.visualMode
            <<" mrt="<<trace.singlePassOutputs<<" gs="<<trace.singleInvocation<<" compact="<<trace.compactSdfRays
            <<" actual_gs="<<stats.probeSdfSingleInvocation<<" actual_compact="<<stats.probeSdfCompactRays
            <<" gi_enabled="<<gi.enabled<<" gi_view="<<gi.view<<std::endl;
        Check(stats.probeSdfNativeUploadsPruned==expected,"Native upload pruning selected the wrong execution path");
        if(expected) {
            Check(stats.probeSdfHardwareRayTracing,"Native upload pruning lost native tracing");
            Check(stats.probeSdfUploadedTexels==0&&stats.probeSdfUploadRegions==0&&stats.probeSdfMeshUploadedTexels==0,
                "Native upload pruning still uploaded a distance-field mirror");
            // A stale or unallocated raw mirror must not be advertised as the
            // completed frame's data. Normal rendered/packed readbacks remain.
            Check(lab.ReadbackProbeGlobalSdf().empty()&&lab.ReadbackProbeSdfRanges().empty(),
                "Native upload pruning published stale raw distance-field atlases");
        }
    };
    const auto atlases=[&] {
        const auto scene=lab.PreparedProbeSdfScene();Check(bool(scene),"Native upload catch-up lost its CPU scene");
        const auto& global=scene->Clipmaps()?scene->Clipmaps()->Pixels():scene->GlobalPixels();
        exact(global,lab.ReadbackProbeGlobalSdf(),"global_gpu_vs_cpu",true);
        if(scene->Clipmaps())exact(scene->Clipmaps()->RangePixels(),lab.ReadbackProbeSdfRanges(),"range_gpu_vs_cpu",true);
        ++catchups;
    };
    active(true); // The first requested frame must already omit mirror uploads.
    for(unsigned test=0;test<3;++test) {
        frame.width=test==1?191:192;frame.height=test==1?123:128;
        trace.rayResolution=test==2?8:4;frame.screenProbeStudyState.occluderOffset=.19f*float(test);
        frame.camera=SealedGiRoomCameraPreset(test==2?1:0,frame.OutputAspect());
        phase="pair_control_"+std::to_string(test);trace.sdf.nativeUploadPruning=false;Reset(frame);Draw(lab,frame);active(false);atlases();
        const auto rays=lab.ReadbackScreenProbeTrace(false,false,true).packed;
        const auto spatial=lab.ReadbackSurfaceCacheSpatialGi(),contact=lab.ReadbackSurfaceCacheContactGi(),temporal=lab.ReadbackSurfaceCacheTemporalGi();
        phase="pair_pruned_"+std::to_string(test);trace.sdf.nativeUploadPruning=true;Reset(frame);Draw(lab,frame);active(true);
        exact(rays,lab.ReadbackScreenProbeTrace(false,false,true).packed,"packed_rays");
        exact(spatial,lab.ReadbackSurfaceCacheSpatialGi(),"spatial_gi");
        exact(contact,lab.ReadbackSurfaceCacheContactGi(),"contact_gi");
        exact(temporal,lab.ReadbackSurfaceCacheTemporalGi(),"temporal_gi");++pairs;
        if(test==0)for(unsigned toggle=0;toggle<2;++toggle) {
            phase="history_toggle_"+std::to_string(toggle);trace.sdf.nativeUploadPruning=toggle!=0;Draw(lab,frame);active(toggle!=0);
            Check(lab.PipelineStatistics().surfaceCacheHistoryUsed,"Native upload pruning toggle discarded valid GI history");++historyToggles;
        }
        std::cout<<"NATIVE UPLOAD CONTRACT pair="<<test<<" size="<<frame.width<<'x'<<frame.height
            <<" ray_resolution="<<trace.rayResolution<<" exact_fp32_bits=1 timed=0"<<std::endl;
    }
    // Several skipped generations and a camera scroll must fully catch up,
    // rather than applying only the latest dirty ranges to an old GPU atlas.
    const auto base=*frame.camera;const auto radiance=frame.effects.areaLights.lights[0].radiance;
    for(unsigned step=0;step<6;++step) {
        phase="model_generation_"+std::to_string(step);Pose(frame,base,2,step,radiance);trace.sdf.nativeUploadPruning=true;Draw(lab,frame);active(true);++prunedMovedFrames;
    }
    phase="pruned_camera_scroll";Pose(frame,base,3,7,radiance);Draw(lab,frame);active(true);
    phase="multi_generation_catchup";trace.sdf.nativeUploadPruning=false;Draw(lab,frame);active(false);atlases();
    // Preserve the requested flag while exercising concrete software/debug
    // fallbacks. Their full raw atlas must still match independently on CPU.
    phase="software_fallback";trace.sdf.nativeUploadPruning=true;trace.sdf.hardwareRayTracing=false;Draw(lab,frame);active(false);atlases();++fallbacks;
    phase="hit_markers_fallback";trace.sdf.hardwareRayTracing=true;trace.sdf.showHitPoints=true;Draw(lab,frame);active(false);atlases();++fallbacks;
    // GI gather intentionally normalizes slice/volume selectors to zero.
    // Exercise the existing standalone SDF debug route for this selector.
    phase="standalone_slice_fallback";gi.enabled=false;trace.sdf.showHitPoints=false;trace.sdf.sliceView=1;
    Draw(lab,frame);active(false);atlases();++fallbacks;
    phase="gs_request_fallback";gi.enabled=true;trace.sdf.sliceView=0;trace.singlePassOutputs=false;trace.singleInvocation=true;
    Draw(lab,frame);active(false);atlases();++fallbacks;
    phase="compact_request_fallback";trace.singleInvocation=false;trace.singlePassOutputs=true;trace.compactSdfRays=true;Draw(lab,frame);active(false);atlases();++fallbacks;
    phase="pruned_restore";trace.compactSdfRays=false;trace.sdf.nativeUploadPruning=true;Draw(lab,frame);active(true);
    Check(pairs==3&&historyToggles==2&&prunedMovedFrames==6&&fallbacks==5&&catchups==9&&comparedChannels>0&&atlasChannels>0,
        "Native upload contracts missed parity, generation catch-up, resizing, history or fallback coverage");
    phase="legacy_cleanup";trace.sdf.nativeUploadPruning=false;Reset(frame);Draw(lab,frame);active(false);
    std::cout<<"NATIVE UPLOAD CONTRACT PASS pairs="<<pairs<<" compared_fp32_channels="<<comparedChannels
        <<" atlas_fp32_channels="<<atlasChannels<<" cpu_atlas_catchups="<<catchups<<" skipped_model_generations="<<prunedMovedFrames
        <<" no_reset_history_toggles="<<historyToggles<<" software_debug_fallbacks="<<fallbacks
        <<" first_requested_frame_pruned=1 render_extent_changes=2 cpu_df_math_unchanged=1 timed=0"<<std::endl;
}
