// Full-resolution production GI; camera poses and trial order are independent
// of execution speed. No quality fixture, image readback or CPU oracle runs.
void SurfaceCacheMotionPerformance(MaterialLabModule& lab,const Options& options) {
    Check(options.vulkan&&options.softwareRays&&!options.validation,
        "Motion performance requires Vulkan --software-rays without validation");
    const unsigned measured=options.surfaceCacheDynamicFrames,warm=64;
    Check(measured>=16&&measured%16==0,"Motion performance needs complete 16-frame camera laps (use 32 for two laps)");
    auto fixture=options;fixture.sealedGiRoom=true;fixture.sdfBanquet=false;
    fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);
    frame.width=options.renderSizeSet?options.benchmarkWidth:1280;
    frame.height=options.renderSizeSet?options.benchmarkHeight:800;
    frame.effects.outputSize={0,0};frame.present=false;frame.screenProbeStudyHud=false;
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::None;
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;auto& trace=placement.trace;
    gi.enabled=true;gi.useScreenProbes=true;gi.view=0;gi.reconstructionDebug=0;
    gi.probeDiagnostics=false;gi.probeOriginDiagnostics=false;gi.collectProbeCoverage=false;
    gi.safeProbeOrigins=true;gi.rasterSourcePositions=true;gi.directionalReconstruction=true;gi.cacheBilinear=true;
    gi.spatial={};gi.temporal={};gi.contact={};gi.temporal.staticConvergence=true;
    gi.temporal.motionConsistentSpatialReuse=false;gi.temporal.localCameraHistory=false;
    gi.temporal.retainHistoryOnObjectMotion=true;gi.contact.raysPerPixel=8;gi.contact.referenceReceiver=false;
    placement.enabled=true;placement.adaptive=true;placement.cornerRefinement=true;placement.curvatureRefinement=true;
    placement.curvatureAwareWeights=true;placement.continuousInterpolation=true;placement.baseSpacing=32;
    placement.freeze=false;placement.frameIndex=0;placement.candidateCount=8;
    trace.enabled=true;trace.cosineHemisphere=true;trace.singleInvocation=false;trace.rayResolution=8;trace.samplingSeed=0;
    trace.maxSteps=512;trace.sdf.enabled=true;trace.sdf.maxSteps=256;
    const unsigned view=options.surfaceCacheDynamicView<3?options.surfaceCacheDynamicView:1;
    constexpr std::array<const char*,3> views{{"oval_close","contact_near","room_objects"}};
    auto base=SealedGiRoomCameraPreset(0,frame.OutputAspect());
    if(view<2){base.position=view?glm::vec3(2.55f,1.42f,-1.90f):glm::vec3(1.28f,1.30f,-2.20f);
        base.view=glm::lookAt(base.position,view?glm::vec3(1.48f,1.05f,-.72f):glm::vec3(1.28f,1.10f,-1.15f),glm::vec3(0,1,0));}
    const auto inverse=glm::inverse(base.view);const auto right=glm::normalize(glm::vec3(inverse[0]));
    const auto forward=glm::normalize(-glm::vec3(inverse[2]));
    const auto reset=[&]{frame.effects.cameraCut=true;placement.reset=true;gi.temporal.reset=true;};
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;placement.reset=false;gi.temporal.reset=false;};
    gi.temporal.motionStability=false;gi.temporal.motionSpatialFilter=false;gi.temporal.motionDisocclusionRecovery=false;
    frame.camera=base;frame.screenProbeStudyState.occluderOffset=0;reset();render();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
    while(lab.ProbeSdfQualityStatistics().pending){render();Check(std::chrono::steady_clock::now()<deadline,"Motion performance SDF preparation timed out");}
    struct Samples {std::vector<double> temporal,gpu,wall;unsigned history=0;std::uint64_t probeRays=0;};
    std::array<Samples,4> combined;
    constexpr std::array<unsigned,8> order{{0,1,2,3,3,2,1,0}};
    constexpr std::array<const char*,4> names{{"legacy","motion_history","motion_history_filtered","motion_history_recovery"}};
    const auto mean=[](const std::vector<double>& a){double sum=0;for(double v:a)sum+=v;return sum/double(a.size());};
    const auto p95=[](std::vector<double> a){std::sort(a.begin(),a.end());return a[std::size_t(std::ceil(.95*double(a.size())))-1];};
    const auto report=[&](const Samples& a,unsigned variant,int trial){
        std::cout<<"MOTION GI PERF samples view="<<views[view]<<" variant="<<names[variant]<<" trial="<<trial
            <<" motion_stability="<<(variant>0)<<" motion_spatial_filter="<<(variant==2)<<" motion_disocclusion_recovery="<<(variant==3)
            <<" temporal_samples="<<a.temporal.size()<<" temporal_mean_ms="<<mean(a.temporal)<<" temporal_p95_ms="<<p95(a.temporal)
            <<" gpu_samples="<<a.gpu.size()<<" gpu_mean_ms="<<mean(a.gpu)<<" gpu_p95_ms="<<p95(a.gpu)
            <<" wall_samples="<<a.wall.size()<<" wall_mean_ms="<<mean(a.wall)<<" wall_p95_ms="<<p95(a.wall)
            <<" history_used_frames="<<a.history<<" probe_ray_budget="<<a.probeRays
            <<" cache_lighting_samples=0 cache_upload_texels=0 upload_regions=0 image_readbacks=0 cpu_oracle_samples=0"<<std::endl;
    };
#ifdef NDEBUG
    constexpr const char* build="Release_NDEBUG";
#else
    constexpr const char* build="Debug";
#endif
    std::cout<<"MOTION GI PERF setup build="<<build<<" size="<<frame.width<<'x'<<frame.height<<" view="<<views[view]
        <<" order=0,1,2,3,3,2,1,0 warm_per_trial="<<warm<<" measured_per_trial="<<measured<<" motion_laps="<<measured/16
        <<" changed_settings=temporal.motionStability,temporal.motionSpatialFilter,temporal.motionDisocclusionRecovery probe_rays=64 receiver_rays=8 spacing=32"
        <<" static_convergence=1 probe_spatial=1 contact=1 temporal=1 single_invocation=0 software_bvh=1 validation=0 present=0"
        <<" camera_step_metres=.0125 yaw_step_radians=.006 image_readbacks=0 cpu_oracle_samples=0 confidence_interval=0"
        <<" gpu_excludes_cpu_wait=1 wall_includes_submit_wait=1 temporal_scope_includes_output_filter=1"<<std::endl;
    for(unsigned trial=0;trial<order.size();++trial){const unsigned variant=order[trial];
        gi.temporal.motionStability=variant>0;gi.temporal.motionSpatialFilter=variant==2;gi.temporal.motionDisocclusionRecovery=variant==3;
        frame.camera=base;reset();for(unsigned i=0;i<warm;++i)render();Samples current;
        for(unsigned i=0;i<measured;++i){const unsigned cycle=i%16,step=cycle<8?cycle+1:15-cycle;
            auto camera=base;camera.position+=right*(.0125f*float(step));
            const auto direction=glm::vec3(glm::rotate(glm::mat4(1),.006f*float(step),glm::vec3(0,1,0))*glm::vec4(forward,0));
            camera.view=glm::lookAt(camera.position,camera.position+direction,glm::vec3(0,1,0));frame.camera=camera;
            const auto timing=lab.MeasureFrames(frame,1,0.f);const auto stats=lab.PipelineStatistics();
            Check(timing.gpuFrameMilliseconds.size()==1&&timing.surfaceCacheTemporalFrameMilliseconds.size()==1,
                "Motion performance requires one fresh frame and temporal timestamp per measurement");
            Check(timing.probeScreenTraceFrameMilliseconds.size()==1&&timing.probeSdfTraceFrameMilliseconds.size()==1&&
                timing.probeReconstructionFrameMilliseconds.size()==1&&timing.probeSpatialFrameMilliseconds.size()==1&&
                timing.contactTraceFrameMilliseconds.size()==1&&timing.contactFilterFrameMilliseconds.size()==1,
                "Motion performance omitted a production GI stage");
            Check(timing.surfaceCacheLightingFrameMilliseconds.empty()&&!stats.surfaceCacheLightingUpdated&&stats.surfaceCacheUpdatedTexels==0&&
                stats.surfaceCacheAttributeUploadedTexels==0&&stats.surfaceCacheMetadataUploadedTexels==0&&
                stats.surfaceCacheGeometryUploadedTexels==0&&stats.surfaceCacheUploadRegions==0,
                "Camera-only motion performance unexpectedly relit or uploaded the world cache");
            Check(stats.surfaceCacheHistoryUsed&&stats.surfaceCacheSpatialPasses>0&&stats.surfaceCacheTemporalPasses>0&&
                stats.surfaceCacheContactCollected&&!lab.ProbeSdfQualityStatistics().pending,
                "Motion performance lost warm history, full GI or prepared SDF");
            const double temporal=timing.surfaceCacheTemporalFrameMilliseconds[0],gpu=timing.gpuFrameMilliseconds[0],wall=timing.elapsedMilliseconds;
            for(double v:{temporal,gpu,wall})Check(std::isfinite(v)&&v>0,"Invalid motion performance timestamp");
            current.temporal.push_back(temporal);current.gpu.push_back(gpu);current.wall.push_back(wall);++current.history;
            current.probeRays+=stats.surfaceCacheProbeRayBudget;
        }
        report(current,variant,int(trial));auto& total=combined[variant];
        total.temporal.insert(total.temporal.end(),current.temporal.begin(),current.temporal.end());
        total.gpu.insert(total.gpu.end(),current.gpu.begin(),current.gpu.end());total.wall.insert(total.wall.end(),current.wall.begin(),current.wall.end());
        total.history+=current.history;total.probeRays+=current.probeRays;
    }
    for(unsigned variant=0;variant<combined.size();++variant)report(combined[variant],variant,-1);
    std::cout<<"MOTION GI PERF PASS timed_frames="<<order.size()*measured<<" static_warm_frames="<<order.size()*warm
        <<" order=0,1,2,3,3,2,1,0 trials_per_variant=2 extra_image_readbacks=0 cpu_oracle_samples=0 cache_relights=0 cache_uploads=0"
        <<" no_interactive_fps_claim=1"<<std::endl;
}

// Independent visual capture, deliberately separate from the timestamp-only
// performance entry point above. This function makes no timing/FPS claim.
void SurfaceCacheMotionPreview(MaterialLabModule& lab,const Options& options) {
    Check(!options.screenshot.empty(),"Motion preview requires --screenshot with an output PNG prefix");
    Check(!options.vulkan||options.softwareRays,"Vulkan motion preview requires --software-rays");
    auto fixture=options;fixture.sealedGiRoom=true;fixture.sdfBanquet=false;fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);frame.width=options.renderSizeSet?options.benchmarkWidth:1280;
    frame.height=options.renderSizeSet?options.benchmarkHeight:800;frame.effects.outputSize={0,0};frame.present=false;frame.screenProbeStudyHud=false;
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::None;
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;auto& trace=placement.trace;
    gi.enabled=true;gi.useScreenProbes=true;gi.view=0;gi.reconstructionDebug=0;
    gi.probeDiagnostics=false;gi.probeOriginDiagnostics=false;gi.collectProbeCoverage=false;
    gi.safeProbeOrigins=true;gi.rasterSourcePositions=true;gi.directionalReconstruction=true;gi.cacheBilinear=true;
    gi.spatial={};gi.temporal={};gi.contact={};gi.temporal.staticConvergence=true;gi.temporal.retainHistoryOnObjectMotion=true;
    gi.temporal.motionConsistentSpatialReuse=false;gi.temporal.localCameraHistory=false;gi.contact.referenceReceiver=false;gi.contact.raysPerPixel=8;
    placement.enabled=true;placement.adaptive=true;placement.cornerRefinement=true;placement.curvatureRefinement=true;
    placement.curvatureAwareWeights=true;placement.continuousInterpolation=true;placement.baseSpacing=32;
    placement.freeze=false;placement.frameIndex=0;placement.candidateCount=8;
    trace.enabled=true;trace.cosineHemisphere=true;trace.singleInvocation=false;trace.rayResolution=8;trace.samplingSeed=0;
    trace.maxSteps=512;trace.sdf.enabled=true;trace.sdf.maxSteps=256;
    const unsigned view=options.surfaceCacheDynamicView<3?options.surfaceCacheDynamicView:1;
    const bool recoveryPreview=options.surfaceCacheDynamicRecoveryPreview;
    const std::array<const char*,3> views{{"oval_close","contact_near",recoveryPreview?"cube_fixed_camera":"room_camera"}};
    constexpr std::array<const char*,4> oldNames{{"legacy","motion_history","motion_history_filtered","motion_history_recovery"}};
    constexpr std::array<const char*,4> recoveryNames{{"current_stability","object_reprojection","disocclusion_filter","object_reprojection_disocclusion_filter"}};
    const auto& names=recoveryPreview?recoveryNames:oldNames;
    const char* backend=options.vulkan?"Vulkan":"OpenGL";
    auto base=SealedGiRoomCameraPreset(0,frame.OutputAspect());
    if(view<2){base.position=view?glm::vec3(2.55f,1.42f,-1.90f):glm::vec3(1.28f,1.30f,-2.20f);
        base.view=glm::lookAt(base.position,view?glm::vec3(1.48f,1.05f,-.72f):glm::vec3(1.28f,1.10f,-1.15f),glm::vec3(0,1,0));}
    else if(recoveryPreview){base.position={0,3.4f,3.05f};base.view=glm::lookAt(base.position,glm::vec3(0,.8f,.25f),glm::vec3(0,1,0));}
    const auto inverse=glm::inverse(base.view);const auto right=glm::normalize(glm::vec3(inverse[0])),forward=glm::normalize(-glm::vec3(inverse[2]));
    const float scale=recoveryPreview&&options.surfaceCacheDynamicFast?3.f:1.f;
    const float translation=(view==1?.015f:.0125f)*scale,yaw=(view==1?.003f:.0025f)*scale;
    std::array<Render::PipelineCamera,9> poses;
    for(unsigned step=0;step<poses.size();++step){auto camera=base;
        if(!recoveryPreview||view<2){camera.position+=right*(translation*float(step));
            const auto direction=glm::vec3(glm::rotate(glm::mat4(1),yaw*float(step),glm::vec3(0,1,0))*glm::vec4(forward,0));
            camera.view=glm::lookAt(camera.position,camera.position+direction,glm::vec3(0,1,0));}poses[step]=camera;}
    const auto reset=[&]{frame.effects.cameraCut=true;placement.reset=true;gi.temporal.reset=true;};
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;placement.reset=false;gi.temporal.reset=false;};
    const auto configure=[&](unsigned variant){gi.temporal.motionStability=recoveryPreview||variant>0;
        gi.temporal.motionSpatialFilter=!recoveryPreview&&variant==2;gi.temporal.motionDisocclusionRecovery=!recoveryPreview&&variant==3;
        gi.temporal.objectMotionReprojection=recoveryPreview&&(variant==1||variant==3);gi.temporal.disocclusionFilter=recoveryPreview&&variant>=2;};
    configure(0);
    frame.camera=poses[0];frame.screenProbeStudyState.occluderOffset=0;reset();render();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
    while(lab.ProbeSdfQualityStatistics().pending){render();Check(std::chrono::steady_clock::now()<deadline,"Motion preview SDF preparation timed out");}
    std::vector<std::uint8_t> staticPixels;unsigned images=0;
    std::cout<<"MOTION GI PREVIEW setup backend="<<backend<<" size="<<frame.width<<'x'<<frame.height<<" view="<<views[view]
        <<" variants="<<(recoveryPreview?"current_stability,object_reprojection,disocclusion_filter,object_reprojection_disocclusion_filter":"legacy,motion_history,motion_history_filtered,motion_history_recovery")
        <<" dynamic_recovery_preview="<<recoveryPreview<<" warm=64 moving=32 stop=16 laps=2 probe_rays=64 receiver_rays=8 spacing=32"
        <<" camera_step_metres="<<(recoveryPreview&&view==2?0:translation)<<" yaw_step_radians="<<(recoveryPreview&&view==2?0:yaw)
        <<" object_step_metres="<<(recoveryPreview&&view==2?.15f:0)<<" sequence_frames=81..88 present=0"
        <<" cpu_oracle_samples=0 atlas_readbacks=0 timing_measurements=0 no_fps_claim=1"<<std::endl;
    for(unsigned variant=0;variant<names.size();++variant){configure(variant);
        frame.camera=poses[0];frame.screenProbeStudyState.occluderOffset=0;reset();unsigned historyFrames=0,variantImages=0;
        for(unsigned f=1;f<=112;++f){unsigned pose=0;
            if(f>64&&f<=96){const unsigned cycle=(f-65)%16;pose=cycle<8?cycle+1:15-cycle;}
            frame.camera=poses[pose];frame.screenProbeStudyState.occluderOffset=recoveryPreview&&view==2?.15f*float(pose):0;
            render();const auto stats=lab.PipelineStatistics();
            Check(!lab.ProbeSdfQualityStatistics().pending&&stats.surfaceCacheContactCollected&&stats.surfaceCacheSpatialPasses>0&&
                stats.surfaceCacheTemporalPasses>0,"Motion preview lost prepared full production GI");
            Check(stats.surfaceCacheHistoryUsed==(f>1),"Motion preview reset or lost compatible history");historyFrames+=stats.surfaceCacheHistoryUsed;
            // Frame 88 is shared by the selected still and consecutive sequence.
            const bool capture=f==64||f==72||f==88||f==97||f==104||f==112||(f>=81&&f<=88);
            if(capture){const auto pixels=lab.Readback();
                Check(pixels.width==frame.width&&pixels.height==frame.height&&pixels.rgba.size()==std::size_t(frame.width)*frame.height*4,
                    std::string("Missing full-resolution motion preview: ")+lab.LastError());
                if(f==64){if(variant==0)staticPixels=pixels.rgba;
                    else Check(pixels.rgba==staticPixels,"Motion preview changed the static frame-64 renderer output");}
                auto path=std::filesystem::path(options.screenshot);
                path.replace_filename(path.stem().string()+"_"+backend+"_"+views[view]+"_"+names[variant]+"_frame"+std::to_string(f)+"_total_renderer_ldr.png");
                Save(pixels,path.string());++images;++variantImages;
                std::cout<<"MOTION GI PREVIEW image backend="<<backend<<" variant="<<names[variant]<<" frame="<<f<<" pose="<<pose
                    <<" path=\""<<path.string()<<"\""<<std::endl;
            }
        }
        Check(variantImages==13,"Motion preview did not capture its complete still/sequence set");
        std::cout<<"MOTION GI PREVIEW variant="<<names[variant]<<" frames=112 history_used_frames="<<historyFrames
            <<" motion_stability="<<gi.temporal.motionStability<<" motion_spatial_filter="<<gi.temporal.motionSpatialFilter
            <<" motion_disocclusion_recovery="<<gi.temporal.motionDisocclusionRecovery
            <<" object_motion_reprojection="<<gi.temporal.objectMotionReprojection<<" disocclusion_filter="<<gi.temporal.disocclusionFilter
            <<" renderer_readbacks="<<variantImages<<" static_frame64_exact=1"<<std::endl;
    }
    Check(images==52,"Motion preview did not capture all four variants");
    std::cout<<"MOTION GI PREVIEW PASS backend="<<backend<<" renderer_readbacks="<<images
        <<" atlas_readbacks=0 cpu_oracle_samples=0 timing_measurements=0 no_fps_claim=1"<<std::endl;
}

// Current shipped camera stability stays enabled in all four configurations.
// Measure real model/cache updates separately from camera-only final gather;
// no image readback, reference integration or CPU geometry oracle is timed.
void SurfaceCacheDynamicRecoveryPerformance(MaterialLabModule& lab,const Options& options) {
    Check(options.vulkan&&options.softwareRays&&!options.validation,"Dynamic recovery performance requires Vulkan software rays without validation");
    const unsigned measured=options.surfaceCacheDynamicFrames,warm=64,settle=16;
    Check(measured>=16&&measured%16==0,"Dynamic recovery performance needs complete 16-frame laps");
    auto fixture=options;fixture.sealedGiRoom=true;fixture.sdfBanquet=false;fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);frame.width=options.renderSizeSet?options.benchmarkWidth:1280;
    frame.height=options.renderSizeSet?options.benchmarkHeight:800;frame.effects.outputSize={0,0};frame.present=false;frame.screenProbeStudyHud=false;
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::None;
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;auto& trace=placement.trace;
    gi.enabled=true;gi.useScreenProbes=true;gi.view=0;gi.reconstructionDebug=0;gi.probeDiagnostics=false;gi.probeOriginDiagnostics=false;
    gi.collectProbeCoverage=false;gi.safeProbeOrigins=true;gi.rasterSourcePositions=true;gi.directionalReconstruction=true;gi.cacheBilinear=true;
    gi.spatial={};gi.temporal={};gi.contact={};gi.temporal.staticConvergence=true;gi.temporal.motionStability=true;
    gi.temporal.motionConsistentSpatialReuse=false;gi.temporal.localCameraHistory=false;gi.temporal.motionSpatialFilter=false;
    gi.temporal.motionDisocclusionRecovery=false;gi.temporal.retainHistoryOnObjectMotion=true;gi.contact.raysPerPixel=8;gi.contact.referenceReceiver=false;
    placement.enabled=true;placement.adaptive=true;placement.cornerRefinement=true;placement.curvatureRefinement=true;
    placement.curvatureAwareWeights=true;placement.continuousInterpolation=true;placement.baseSpacing=32;placement.candidateCount=8;
    placement.freeze=false;placement.frameIndex=0;trace.enabled=true;trace.cosineHemisphere=true;trace.singleInvocation=false;
    trace.rayResolution=8;trace.samplingSeed=0;trace.maxDistance=gi.maxDistance;trace.maxSteps=512;trace.sdf.enabled=true;trace.sdf.maxSteps=256;
    const auto reset=[&]{frame.effects.cameraCut=true;placement.reset=true;gi.temporal.reset=true;};
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;placement.reset=false;gi.temporal.reset=false;};
    constexpr unsigned scopeCount=10;
    constexpr std::array<const char*,scopeCount> scopeNames{{"frame","screen_trace","sdf_trace","reconstruction","probe_spatial","contact_trace","contact_filter","temporal","cache_lighting","receiver_motion"}};
    constexpr std::array<const char*,3> viewNames{{"oval_camera","contact_camera","cube_fixed_camera"}};
    constexpr std::array<const char*,4> names{{"current_stability","object_reprojection","disocclusion_filter","object_reprojection_disocclusion_filter"}};
    constexpr std::array<unsigned,8> order{{0,1,2,3,3,2,1,0}};
    struct Samples {std::array<std::vector<double>,scopeCount> scope;std::vector<double> wall;
        std::uint64_t attributes=0,metadata=0,geometry=0,regions=0,lit=0,rays=0;unsigned history=0;};
    const auto mean=[](const auto& a){double sum=0;for(double v:a)sum+=v;return a.empty()?0.:sum/double(a.size());};
    const auto median=[](std::vector<double> a){if(a.empty())return 0.;std::sort(a.begin(),a.end());const auto n=a.size();
        return n%2?a[n/2]:.5*(a[n/2-1]+a[n/2]);};
    const auto p95=[](std::vector<double> a){if(a.empty())return 0.;std::sort(a.begin(),a.end());return a[std::size_t(std::ceil(.95*double(a.size())))-1];};
    const auto append=[](Samples& total,const Samples& a){for(unsigned s=0;s<scopeCount;++s)
            total.scope[s].insert(total.scope[s].end(),a.scope[s].begin(),a.scope[s].end());
        total.wall.insert(total.wall.end(),a.wall.begin(),a.wall.end());total.attributes+=a.attributes;total.metadata+=a.metadata;
        total.geometry+=a.geometry;total.regions+=a.regions;total.lit+=a.lit;total.rays+=a.rays;total.history+=a.history;};
    const auto report=[&](const Samples& a,unsigned view,unsigned variant,unsigned segment,int trial){
        for(unsigned s=0;s<scopeCount;++s)std::cout<<"DYNAMIC RECOVERY PERF scope view="<<viewNames[view]<<" variant="<<names[variant]
            <<" segment="<<(segment?"stopping16":"moving")<<" trial="<<trial<<" name="<<scopeNames[s]<<" samples="<<a.scope[s].size()
            <<" executed="<<!a.scope[s].empty()<<" mean_ms="<<mean(a.scope[s])<<" median_ms="<<median(a.scope[s])<<" p95_ms="<<p95(a.scope[s])<<std::endl;
        std::cout<<"DYNAMIC RECOVERY PERF wall view="<<viewNames[view]<<" variant="<<names[variant]<<" segment="<<(segment?"stopping16":"moving")
            <<" trial="<<trial<<" samples="<<a.wall.size()<<" mean_ms="<<mean(a.wall)<<" median_ms="<<median(a.wall)<<" p95_ms="<<p95(a.wall)
            <<" object_motion_reprojection="<<(variant==1||variant==3)<<" disocclusion_filter="<<(variant>=2)
            <<" history_used_frames="<<a.history<<" probe_ray_budget="<<a.rays<<" attribute_uploaded_texels="<<a.attributes
            <<" metadata_uploaded_texels="<<a.metadata<<" geometry_uploaded_texels="<<a.geometry<<" upload_regions="<<a.regions
            <<" cache_relit_texels="<<a.lit<<" motion_stability=1 extra_image_readbacks=0 cpu_oracle_samples=0 confidence_interval=0"<<std::endl;};
#ifdef NDEBUG
    constexpr const char* build="Release_NDEBUG";
#else
    constexpr const char* build="Debug";
#endif
    const unsigned first=options.surfaceCacheDynamicView<3?options.surfaceCacheDynamicView:0,last=options.surfaceCacheDynamicView<3?first+1:3;
    for(unsigned view=first;view<last;++view){const std::array<glm::vec3,3> eyes{{{1.28f,1.30f,-2.20f},{2.55f,1.42f,-1.90f},{0,3.4f,3.05f}}};
        const std::array<glm::vec3,3> targets{{{1.28f,1.10f,-1.15f},{1.48f,1.05f,-.72f},{0,.8f,.25f}}};
        auto base=SealedGiRoomCameraPreset(0,frame.OutputAspect());base.position=eyes[view];base.view=glm::lookAt(base.position,targets[view],glm::vec3(0,1,0));
        const auto inverse=glm::inverse(base.view);const auto right=glm::normalize(glm::vec3(inverse[0])),forward=glm::normalize(-glm::vec3(inverse[2]));
        const float scale=options.surfaceCacheDynamicFast?3.f:1.f,translation=(view==1?.015f:.0125f)*scale,yaw=(view==1?.003f:.0025f)*scale;
        gi.temporal.objectMotionReprojection=false;gi.temporal.disocclusionFilter=false;frame.camera=base;frame.screenProbeStudyState.occluderOffset=0;
        reset();render();const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
        while(lab.ProbeSdfQualityStatistics().pending){render();Check(std::chrono::steady_clock::now()<deadline,"Dynamic recovery performance SDF preparation timed out");}
        std::array<std::array<Samples,2>,4> combined;std::vector<std::array<std::uint64_t,6>> baselineWork;
        std::cout<<"DYNAMIC RECOVERY PERF setup build="<<build<<" size="<<frame.width<<'x'<<frame.height<<" view="<<viewNames[view]
            <<" order=0,1,2,3,3,2,1,0 warm_per_trial="<<warm<<" measured_moving_per_trial="<<measured<<" measured_stopping_per_trial="<<settle
            <<" camera_translation_step="<<(view==2?0:translation)<<" camera_yaw_step="<<(view==2?0:yaw)<<" object_translation_step="<<(view==2?.15f:0)
            <<" probe_rays=64 receiver_rays=8 spacing=32 motion_stability=1 object_dynamic_cap="<<gi.temporal.dynamicMaxFrames
            <<" static_convergence=1 contact=1 temporal=1 single_invocation=0 software_bvh=1 validation=0 present=0"
            <<" no_image_readbacks=1 cpu_oracle_samples=0 confidence_interval=0 gpu_excludes_cpu_wait=1 wall_includes_submit_wait=1"<<std::endl;
        for(unsigned trial=0;trial<order.size();++trial){const unsigned variant=order[trial];
            gi.temporal.objectMotionReprojection=variant==1||variant==3;gi.temporal.disocclusionFilter=variant>=2;
            frame.camera=base;frame.screenProbeStudyState.occluderOffset=0;reset();for(unsigned f=0;f<warm;++f)render();
            std::array<Samples,2> samples;
            for(unsigned f=0;f<measured+settle;++f){const bool moving=f<measured;const unsigned cycle=f%16,step=moving?(cycle<8?cycle+1:15-cycle):0;
                frame.camera=base;if(view==2)frame.screenProbeStudyState.occluderOffset=.15f*float(step);
                else {frame.camera->position+=right*(translation*float(step));const auto direction=glm::vec3(glm::rotate(glm::mat4(1),yaw*float(step),glm::vec3(0,1,0))*glm::vec4(forward,0));
                    frame.camera->view=glm::lookAt(frame.camera->position,frame.camera->position+direction,glm::vec3(0,1,0));}
                const auto timing=lab.MeasureFrames(frame,1,0.f);const auto stats=lab.PipelineStatistics();auto& sample=samples[moving?0:1];
                const std::array<const std::vector<double>*,scopeCount> source{{&timing.gpuFrameMilliseconds,&timing.probeScreenTraceFrameMilliseconds,
                    &timing.probeSdfTraceFrameMilliseconds,&timing.probeReconstructionFrameMilliseconds,&timing.probeSpatialFrameMilliseconds,
                    &timing.contactTraceFrameMilliseconds,&timing.contactFilterFrameMilliseconds,&timing.surfaceCacheTemporalFrameMilliseconds,&timing.surfaceCacheLightingFrameMilliseconds,
                    &timing.receiverMotionFrameMilliseconds}};
                for(unsigned s=0;s<scopeCount;++s){Check(source[s]->size()<2,"Duplicate dynamic recovery GPU timestamp");
                    if(s<8)Check(source[s]->size()==1,std::string("Missing dynamic recovery GPU stage ")+scopeNames[s]);
                    for(double v:*source[s]){Check(std::isfinite(v)&&v>0,"Invalid dynamic recovery GPU timestamp");sample.scope[s].push_back(v);}}
                Check(source[9]->size()==unsigned(gi.temporal.objectMotionReprojection),
                    "Receiver-motion GPU scope was missing or executed in a non-object variant");
                Check(std::isfinite(timing.elapsedMilliseconds)&&timing.elapsedMilliseconds>0,"Invalid dynamic recovery wall timestamp");sample.wall.push_back(timing.elapsedMilliseconds);
                Check(stats.surfaceCacheHistoryUsed&&stats.surfaceCacheContactCollected&&stats.surfaceCacheSpatialPasses>0&&stats.surfaceCacheTemporalPasses>0&&
                    !lab.ProbeSdfQualityStatistics().pending,"Dynamic recovery performance lost compatible history or prepared full GI");
                if(view==2&&moving){const auto cache=lab.PreparedSurfaceCache();Check(bool(cache)&&source[8]->size()==1&&stats.surfaceCacheLightingUpdated&&
                        stats.surfaceCacheUpdatedTexels==cache->TexelCount(),"Object recovery performance omitted full current cache relighting");}
                else Check(source[8]->empty()&&!stats.surfaceCacheLightingUpdated&&stats.surfaceCacheUpdatedTexels==0&&stats.surfaceCacheAttributeUploadedTexels==0&&
                    stats.surfaceCacheMetadataUploadedTexels==0&&stats.surfaceCacheGeometryUploadedTexels==0&&stats.surfaceCacheUploadRegions==0,
                    "Camera/stop recovery performance unexpectedly relit or uploaded cache");
                const std::array<std::uint64_t,6> work{{stats.surfaceCacheProbeRayBudget,stats.surfaceCacheAttributeUploadedTexels,stats.surfaceCacheMetadataUploadedTexels,
                    stats.surfaceCacheGeometryUploadedTexels,stats.surfaceCacheUploadRegions,stats.surfaceCacheUpdatedTexels}};
                if(trial==0)baselineWork.push_back(work);else Check(work==baselineWork[f],"Dynamic recovery A/B changed probe budget or cache work");
                sample.rays+=work[0];sample.attributes+=work[1];sample.metadata+=work[2];sample.geometry+=work[3];sample.regions+=work[4];sample.lit+=work[5];++sample.history;
            }
            for(unsigned segment=0;segment<2;++segment){report(samples[segment],view,variant,segment,int(trial));append(combined[variant][segment],samples[segment]);}
        }
        for(unsigned variant=0;variant<combined.size();++variant)for(unsigned segment=0;segment<2;++segment){
            const bool receiverMotion=variant==1||variant==3;
            const auto& samples=combined[variant][segment];const unsigned expected=2*(segment?settle:measured);
            Check(samples.scope[9].size()==(receiverMotion?expected:0),"Receiver-motion GPU sample coverage did not match the symmetric trials");
            if(receiverMotion)Check(samples.scope[9].size()>=32,"Receiver-motion GPU cost needs at least 32 fresh executed samples per segment");
            report(samples,view,variant,segment,-1);}
        std::cout<<"DYNAMIC RECOVERY PERF PASS view="<<viewNames[view]<<" timed_frames="<<order.size()*(measured+settle)<<" static_warm_frames="<<order.size()*warm
            <<" trials_per_variant=2 cache_work_exact=1 extra_image_readbacks=0 cpu_oracle_samples=0 no_interactive_fps_claim=1"<<std::endl;
    }
}
