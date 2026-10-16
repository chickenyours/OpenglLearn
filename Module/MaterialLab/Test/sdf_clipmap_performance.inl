// No present, frame pacing or diagnostic readback inside timed batches.
// The same executable and camera/resolution are used before and after changes.
void SdfClipmapPerformance(MaterialLabModule& lab,const Options& options) {
    auto frame=MakeSdfClipmapFrame(options);frame.present=false;frame.screenProbeStudyHud=false;
    auto& placement=frame.effects.screenProbePlacement;auto& sdf=placement.trace.sdf;
    frame.camera=SdfClipmapCameraPreset(0,frame.OutputAspect());
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;placement.reset=false;};
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
    do {render();Check(std::chrono::steady_clock::now()<deadline,"Performance fixture source bake timed out");}
    while(lab.ProbeSdfQualityStatistics().pending);
    const auto measure=[&](const char* name) {
        render();const auto timing=lab.MeasureFrames(frame,6);const auto p=lab.PipelineStatistics();
        Check(std::isfinite(timing.gpuMilliseconds)&&timing.gpuMilliseconds>0,"Invalid SDF performance timestamp");
        std::cout<<"CLIPMAP PERF "<<name<<" size="<<frame.width<<'x'<<frame.height
            <<" gpu_ms="<<timing.gpuMilliseconds<<" elapsed_ms="<<timing.elapsedMilliseconds
            <<" fps="<<1000./timing.elapsedMilliseconds<<" placement="<<p.screenProbePlacementPasses
            <<" trace="<<p.screenProbeTracePasses<<" hzb="<<p.screenProbeHzbLevels
            <<" updated="<<p.probeSdfUpdatedVoxels<<" uploaded="<<p.probeSdfUploadedTexels<<std::endl;
    };
    placement.enabled=false;measure("model_only");placement.enabled=true;
    sdf.visualMode=0;sdf.showHitPoints=false;measure("model_probe_trace");
    sdf.visualMode=2;sdf.showClipmapBounds=false;measure("global_plain");
    sdf.showClipmapBounds=true;frame.screenProbeStudyHud=true;measure("global_bounds_hud");
    frame.screenProbeStudyHud=false;frame.camera=SdfClipmapCameraPreset(1,frame.OutputAspect());measure("global_close");
    frame.camera=SdfClipmapCameraPreset(0,frame.OutputAspect());sdf.visualMode=1;measure("mesh");
    sdf.visualMode=3;measure("split");
    sdf.visualMode=2;
    double gpu=0,elapsed=0,update=0;std::uint64_t voxels=0,uploaded=0;
    for(unsigned i=0;i<4;++i) {
        auto camera=SdfClipmapCameraPreset(0,frame.OutputAspect());camera.position.x+=.13f*float(i+1);
        camera.view=glm::lookAt(camera.position,glm::vec3(0,1.3f,-1.5f),glm::vec3(0,1,0));frame.camera=camera;
        const auto timing=lab.MeasureFrames(frame,1);gpu+=timing.gpuMilliseconds;elapsed+=timing.elapsedMilliseconds;
        update+=lab.PreparedProbeSdfScene()->Statistics().updateMilliseconds;
        voxels+=lab.PipelineStatistics().probeSdfUpdatedVoxels;uploaded+=lab.PipelineStatistics().probeSdfUploadedTexels;
    }
    std::cout<<"CLIPMAP PERF moving size="<<frame.width<<'x'<<frame.height<<" gpu_ms="<<gpu/4
        <<" elapsed_ms="<<elapsed/4<<" clipmap_cpu_ms="<<update/4<<" updated="<<voxels/4<<" uploaded="<<uploaded/4<<std::endl;
    if(!options.screenshot.empty()) {
        frame.camera=SdfClipmapCameraPreset(0,frame.OutputAspect());frame.screenProbeStudyHud=true;render();Save(lab.Readback(),options.screenshot);
    }
    std::cout<<"SDF clipmap performance complete (6 timed frames/case; no present or readback in batches)."<<std::endl;
}
