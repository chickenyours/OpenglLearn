// Isolate full-atlas relighting without final-gather work obscuring its cost.
// Both variants recompute every card and cast the same shadow rays.
void SurfaceCacheLightingPerformance(MaterialLabModule& lab,const Options& options) {
    Check(options.vulkan&&options.softwareRays&&!options.validation,
        "Lighting performance requires Vulkan --software-rays without validation");
    auto fixture=options;fixture.sealedGiRoom=true;fixture.sdfBanquet=false;
    fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);frame.width=320;frame.height=200;
    frame.effects.outputSize={0,0};frame.present=false;frame.screenProbeStudyHud=false;
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::None;
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;
    gi.view=4;gi.spatial.enabled=false;gi.temporal.enabled=false;gi.contact.enabled=false;
    placement.enabled=false;placement.trace.enabled=false;
    frame.camera=SealedGiRoomCameraPreset(0,frame.OutputAspect());
    const auto original=frame.effects.areaLights.lights[0].radiance;
    const auto mean=[](const std::vector<double>& values){double sum=0;for(double x:values)sum+=x;
        return values.empty()?0.:sum/double(values.size());};
    const auto p95=[](std::vector<double> values){std::sort(values.begin(),values.end());
        return values.empty()?0.:values[std::size_t(std::ceil(.95*double(values.size())))-1];};
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;};
    constexpr std::array<bool,4> reuse{{false,true,true,false}};
    for(unsigned path=0;path<2;++path)for(unsigned trial=0;trial<reuse.size();++trial){
        gi.reuseAnyHitBounds=reuse[trial];frame.screenProbeStudyState.occluderOffset=0;
        frame.effects.areaLights.lights[0].radiance=original;frame.effects.cameraCut=true;
        for(unsigned i=0;i<8;++i)render();
        std::vector<double> light,whole,wall;
        std::uint64_t relit=0,attributes=0,metadata=0,geometry=0;
        for(unsigned i=0;i<options.surfaceCacheDynamicFrames;++i){
            if(path==0)frame.effects.areaLights.lights[0].radiance=original*(i%2?.8f:1.1f);
            else {const unsigned cycle=i%16,step=cycle<8?cycle+1:15-cycle;
                frame.screenProbeStudyState.occluderOffset=.06f*float(step);}
            const auto timing=lab.MeasureFrames(frame,1,0.f);
            Check(timing.surfaceCacheLightingFrameMilliseconds.size()==1&&timing.gpuFrameMilliseconds.size()==1,
                "Lighting performance did not execute a full relight");
            Check(timing.probeScreenTraceFrameMilliseconds.empty()&&timing.probeSdfTraceFrameMilliseconds.empty()&&
                timing.contactTraceFrameMilliseconds.empty()&&timing.surfaceCacheTemporalFrameMilliseconds.empty(),
                "Lighting-only performance accidentally traced final gather");
            light.push_back(timing.surfaceCacheLightingFrameMilliseconds[0]);
            whole.push_back(timing.gpuFrameMilliseconds[0]);wall.push_back(timing.elapsedMilliseconds);
            for(double x:{light.back(),whole.back(),wall.back()})Check(std::isfinite(x)&&x>0,"Invalid lighting timing");
            const auto stats=lab.PipelineStatistics();relit+=stats.surfaceCacheUpdatedTexels;
            attributes+=stats.surfaceCacheAttributeUploadedTexels;metadata+=stats.surfaceCacheMetadataUploadedTexels;
            geometry+=stats.surfaceCacheGeometryUploadedTexels;
            Check(stats.surfaceCacheUpdatedTexels==lab.PreparedSurfaceCache()->TexelCount(),
                "Lighting optimization skipped card relighting");
        }
        std::cout<<"CACHE LIGHTING PERF path="<<(path?"object":"intensity")<<" trial="<<trial
            <<" reuse_anyhit_bounds="<<reuse[trial]<<" samples="<<light.size()
            <<" lighting_mean_ms="<<mean(light)<<" lighting_p95_ms="<<p95(light)
            <<" gpu_mean_ms="<<mean(whole)<<" wall_mean_ms="<<mean(wall)<<" wall_p95_ms="<<p95(wall)
            <<" relit_texels="<<relit<<" attribute_uploaded_texels="<<attributes
            <<" metadata_uploaded_texels="<<metadata<<" geometry_uploaded_texels="<<geometry
            <<" full_relight=1 shadow_rays_unchanged=1 image_readback=0 present=0 order=ABBA"<<std::endl;
    }
    std::cout<<"CACHE LIGHTING PERF PASS: full-atlas paired GPU timing; no interactive FPS claim"<<std::endl;
}
