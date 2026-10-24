// Full production GI path, with no image oracle/readback in timed frames.
// Motion follows fixed poses rather than elapsed wall time, so a slower build
// does not silently get a different camera or occluder trajectory.
void SurfaceCacheDynamicPerformance(MaterialLabModule& lab,const Options& options) {
    if(options.surfaceCacheDynamicRecoveryPerformance){SurfaceCacheDynamicRecoveryPerformance(lab,options);return;}
    if(options.surfaceCacheMotionPreview){SurfaceCacheMotionPreview(lab,options);return;}
    if(options.surfaceCacheMotionPerformance){SurfaceCacheMotionPerformance(lab,options);return;}
    if(options.surfaceCacheLightingPerformance){SurfaceCacheLightingPerformance(lab,options);return;}
    Check(options.vulkan&&options.softwareRays,"Dynamic GI baseline requires Vulkan --software-rays");
    Check(!options.validation,"Dynamic GI baseline must run without validation");
    auto fixture=options;fixture.sealedGiRoom=true;fixture.sdfBanquet=false;
    fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);
    frame.width=options.renderSizeSet?options.benchmarkWidth:1280;
    frame.height=options.renderSizeSet?options.benchmarkHeight:800;
    frame.effects.outputSize={0,0};frame.present=false;frame.screenProbeStudyHud=false;
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::None;
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;
    auto& trace=placement.trace;
    gi.enabled=true;gi.useScreenProbes=true;gi.view=0;gi.reconstructionDebug=0;
    gi.probeDiagnostics=false;gi.probeOriginDiagnostics=false;gi.collectProbeCoverage=false;
    gi.safeProbeOrigins=true;gi.rasterSourcePositions=true;gi.directionalReconstruction=true;gi.cacheBilinear=true;
    gi.spatial={};gi.temporal={};gi.contact={};
    gi.spatial.enabled=true;gi.temporal.enabled=true;gi.temporal.staticConvergence=true;
    gi.temporal.retainHistoryOnObjectMotion=true;
    gi.temporal.motionConsistentSpatialReuse=options.surfaceCacheDynamicConsistent;
    gi.temporal.localCameraHistory=options.surfaceCacheLocalCameraHistory;
    gi.temporal.motionStability=options.surfaceCacheMotionStability;
    gi.temporal.motionSpatialFilter=options.surfaceCacheMotionSpatialFilter;
    gi.temporal.motionDisocclusionRecovery=options.surfaceCacheMotionDisocclusionRecovery;
    gi.contact.enabled=true;gi.contact.referenceReceiver=false;gi.contact.raysPerPixel=8;
    placement.enabled=true;placement.adaptive=true;placement.cornerRefinement=true;
    placement.curvatureAwareWeights=true;placement.curvatureRefinement=true;placement.continuousInterpolation=true;
    placement.baseSpacing=32;placement.freeze=false;placement.frameIndex=0;placement.candidateCount=8;
    trace.enabled=true;trace.cosineHemisphere=true;trace.rayResolution=8;trace.samplingSeed=0;
    trace.maxSteps=512;trace.sdf.enabled=true;trace.sdf.maxSteps=256;
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;
        placement.reset=false;gi.temporal.reset=false;};
    const auto reset=[&]{frame.effects.cameraCut=true;placement.reset=true;gi.temporal.reset=true;};
    const auto cameraAt=[&](unsigned view){auto camera=SealedGiRoomCameraPreset(0,frame.OutputAspect());
        if(view<2){camera.position=view?glm::vec3(2.55f,1.42f,-1.90f):glm::vec3(1.28f,1.30f,-2.20f);
            camera.view=glm::lookAt(camera.position,view?glm::vec3(1.48f,1.05f,-.72f):glm::vec3(1.28f,1.10f,-1.15f),glm::vec3(0,1,0));}
        return camera;};
    const auto mean=[](const std::vector<double>& values){double sum=0;for(double x:values)sum+=x;
        return values.empty()?0.:sum/double(values.size());};
    const auto quantile=[](std::vector<double> values,double fraction){
        if(values.empty())return 0.;std::sort(values.begin(),values.end());const double at=fraction*double(values.size()-1);
        const auto low=std::size_t(at),high=std::min(low+1,values.size()-1);
        return values[low]+(at-double(low))*(values[high]-values[low]);};
    constexpr unsigned scopes=9,warm=64;
    constexpr std::array<const char*,scopes> scopeNames{{"frame","screen_trace","sdf_trace","probe_reconstruction",
        "probe_spatial","contact_trace","contact_filter","temporal","cache_lighting"}};
    constexpr std::array<const char*,3> viewNames{{"oval_close","contact_near","room_objects"}};
    constexpr std::array<const char*,4> pathNames{{"static","camera","object","lighting"}};
    const auto beginView=options.surfaceCacheDynamicView<3?options.surfaceCacheDynamicView:0u;
    const auto endView=options.surfaceCacheDynamicView<3?beginView+1:3u;
#ifdef NDEBUG
    constexpr const char* buildName="Release_NDEBUG";
#else
    constexpr const char* buildName="Debug";
#endif
    std::cout<<"DYNAMIC GI PERF setup build="<<buildName<<" size="<<frame.width<<'x'<<frame.height
        <<" consistent_angular="<<gi.temporal.motionConsistentSpatialReuse
        <<" local_camera_history="<<gi.temporal.localCameraHistory<<" warm="<<warm
        <<" motion_stability="<<gi.temporal.motionStability
        <<" motion_spatial_filter="<<gi.temporal.motionSpatialFilter
        <<" motion_disocclusion_recovery="<<gi.temporal.motionDisocclusionRecovery
        <<" reuse_anyhit_bounds="<<gi.reuseAnyHitBounds
        <<" single_invocation_requested="<<trace.singleInvocation
        <<" measured="<<options.surfaceCacheDynamicFrames<<" nominal_step_seconds="<<1./60.
        <<" full_production_gi=1 static_convergence=1 contact=1 retain_motion_history=1"
        <<" probe_rays=64 receiver_rays=8 temporal_aa=0 present=0 validation=0 image_readback=0 cpu_oracle=0"
        <<" surface_cache_trace_backend=software_bvh gpu_spans_exclude_cpu_wait=1 wall_span_includes_submit_wait=1"
        <<std::endl;
    for(unsigned view=beginView;view<endView;++view){const auto base=cameraAt(view);
        const auto inverse=glm::inverse(base.view);const auto right=glm::normalize(glm::vec3(inverse[0]));
        const auto forward=glm::normalize(-glm::vec3(inverse[2]));
        // The expensive cold prepare is intentionally separate from each warmup.
        frame.camera=base;frame.screenProbeStudyState.occluderOffset=0;reset();render();
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
        while(lab.ProbeSdfQualityStatistics().pending){render();Check(std::chrono::steady_clock::now()<deadline,"Dynamic baseline SDF preparation timed out");}
        const auto originalRadiance=frame.effects.areaLights.lights[0].radiance;
        for(unsigned path=0;path<pathNames.size();++path){
            frame.camera=base;frame.screenProbeStudyState.occluderOffset=0;
            frame.effects.areaLights.lights[0].radiance=originalRadiance;reset();
            for(unsigned i=0;i<warm;++i)render();
            std::array<std::vector<double>,scopes> samples;std::vector<double> wall;
            std::uint64_t attributes=0,metadata=0,geometry=0,regions=0,litTexels=0;
            unsigned historyFrames=0,singleScreenFrames=0,singleSdfFrames=0;
            for(unsigned i=0;i<options.surfaceCacheDynamicFrames;++i){
                const unsigned cycle=i%16,step=cycle<8?cycle+1:15-cycle;
                if(path==1){auto camera=base;camera.position+=right*(.0125f*float(step));
                    const auto direction=glm::vec3(glm::rotate(glm::mat4(1),.006f*float(step),glm::vec3(0,1,0))*glm::vec4(forward,0));
                    camera.view=glm::lookAt(camera.position,camera.position+direction,glm::vec3(0,1,0));frame.camera=camera;}
                if(path==2)frame.screenProbeStudyState.occluderOffset=.06f*float(step);
                if(path==3)frame.effects.areaLights.lights[0].radiance=originalRadiance*(i%2?.8f:1.1f);
                const auto timing=lab.MeasureFrames(frame,1,0.f);
                const std::array<const std::vector<double>*,scopes> source{{&timing.gpuFrameMilliseconds,
                    &timing.probeScreenTraceFrameMilliseconds,&timing.probeSdfTraceFrameMilliseconds,
                    &timing.probeReconstructionFrameMilliseconds,&timing.probeSpatialFrameMilliseconds,
                    &timing.contactTraceFrameMilliseconds,&timing.contactFilterFrameMilliseconds,
                    &timing.surfaceCacheTemporalFrameMilliseconds,&timing.surfaceCacheLightingFrameMilliseconds}};
                for(unsigned scope=0;scope<scopes;++scope){const auto& values=*source[scope];
                    Check(values.size()<2,"Dynamic baseline returned duplicate frame timing");
                    if(scope+1<scopes)Check(values.size()==1,std::string("Missing dynamic GPU scope ")+scopeNames[scope]);
                    for(double value:values){Check(std::isfinite(value)&&value>0,"Invalid dynamic GPU timing");samples[scope].push_back(value);}}
                Check(std::isfinite(timing.elapsedMilliseconds)&&timing.elapsedMilliseconds>0,"Invalid dynamic CPU/wall timing");
                wall.push_back(timing.elapsedMilliseconds);
                const auto stats=lab.PipelineStatistics();attributes+=stats.surfaceCacheAttributeUploadedTexels;
                metadata+=stats.surfaceCacheMetadataUploadedTexels;geometry+=stats.surfaceCacheGeometryUploadedTexels;
                regions+=stats.surfaceCacheUploadRegions;litTexels+=stats.surfaceCacheUpdatedTexels;
                historyFrames+=stats.surfaceCacheHistoryUsed;
                singleScreenFrames+=stats.screenProbeSingleInvocation;singleSdfFrames+=stats.probeSdfSingleInvocation;
                Check(!lab.ProbeSdfQualityStatistics().pending,"Dynamic baseline measured an unprepared SDF tier");
            }
            for(unsigned scope=0;scope<scopes;++scope)std::cout<<"DYNAMIC GI PERF scope view="<<viewNames[view]
                <<" path="<<pathNames[path]<<" name="<<scopeNames[scope]<<" samples="<<samples[scope].size()
                <<" executed="<<!samples[scope].empty()<<" mean_ms="<<mean(samples[scope])
                <<" median_ms="<<quantile(samples[scope],.5)<<" p95_ms="<<quantile(samples[scope],.95)<<std::endl;
            std::cout<<"DYNAMIC GI PERF wall view="<<viewNames[view]<<" path="<<pathNames[path]
                <<" samples="<<wall.size()<<" mean_ms="<<mean(wall)<<" p95_ms="<<quantile(wall,.95)
                <<" history_used_frames="<<historyFrames<<" attribute_uploaded_texels="<<attributes
                <<" metadata_uploaded_texels="<<metadata<<" geometry_uploaded_texels="<<geometry
                <<" upload_regions="<<regions<<" cache_relit_texels="<<litTexels<<std::endl;
            std::cout<<"DYNAMIC GI PERF trace_backend view="<<viewNames[view]<<" path="<<pathNames[path]
                <<" single_screen_frames="<<singleScreenFrames<<" single_sdf_frames="<<singleSdfFrames
                <<" measured_frames="<<options.surfaceCacheDynamicFrames<<std::endl;
            if(path<2)Check(samples[8].empty()&&attributes==0&&metadata==0&&geometry==0,"Static/camera baseline unexpectedly updated world cache");
            else {Check(samples[8].size()==options.surfaceCacheDynamicFrames,"Dynamic cache lighting scope did not execute each changed frame");
                const auto cache=lab.PreparedSurfaceCache();Check(bool(cache),"Dynamic baseline lost its prepared cache");
                Check(litTexels==std::uint64_t(options.surfaceCacheDynamicFrames)*cache->TexelCount(),"Dynamic baseline did not retain full-atlas relighting");}
        }
        frame.effects.areaLights.lights[0].radiance=originalRadiance;
    }
    std::cout<<"DYNAMIC GI PERF PASS: independent GPU scopes and CPU/wall spans; no interactive FPS claim"<<std::endl;
}
