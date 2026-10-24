// Included inside MaterialRenderPipeline::Impl after UE radiance-cache/gather
// helpers. Root owns call sites, the ViewState frame index and frame commit.
void PrepareUeLumenProbeImportance(const SurfaceCacheGiSettings& gi){
    if(!gi.ueLumenHigh)return;
    auto& s=ueLumenProbeImportance;const auto& n=nativeProbeTransport;
    const unsigned columns=n.origins.width,rows=n.origins.height;
    Require(columns&&rows&&columns*8u<=8192&&rows*8u<=8192,"Invalid UE importance-sampling probe atlas");
    PrepareUeLumenHighSampling();
    if(s.computeOwner&&s.targetsOwner&&s.columns==columns&&s.rows==rows)return;
    auto compute=std::shared_ptr<Compute::Device>(Compute::Device::Borrow(device));
    Require(compute->Diagnostics().frameAvailable,"UE importance sampling requires frame compute");
    Compute::FramePipelineDesc d;d.source=SurfaceCacheDetail::UeLumenProbeBrdfPdfShader();d.label="ue-lumen-probe-brdf-pdf";
    d.uniformMask=(1u<<9)|(1u<<15);d.sampledMask=(1u<<2)|(1u<<3)|(1u<<4)|(1u<<5);d.storageBuffers=1;
    std::array<Compute::PipelineHandle,3> programs;programs[0]=compute->CreateFramePipeline(d);
    d={};d.source=SurfaceCacheDetail::UeLumenProbeLightingPdfShader();d.label="ue-lumen-probe-lighting-pdf";
    d.uniformMask=(1u<<9)|(1u<<13)|(1u<<15);
    d.sampledMask=(1u<<0)|(1u<<1)|(1u<<4)|(1u<<5)|(1u<<7)|(1u<<10)|(1u<<11)|(1u<<12);d.storageImages=1;
    programs[1]=compute->CreateFramePipeline(d);
    d={};d.source=SurfaceCacheDetail::UeLumenProbeGenerateRaysShader();d.label="ue-lumen-probe-generate-rays";
    d.uniformMask=(1u<<9)|(1u<<15);d.sampledMask=(1u<<0)|(1u<<4)|(1u<<5);d.storageBuffers=1;d.storageImages=1;
    programs[2]=compute->CreateFramePipeline(d);
    const auto brdf=compute->CreateStorageBuffer(std::size_t(columns)*rows*9u*sizeof(float),nullptr,"ue-lumen-probe-brdf-sh3");
    PipelineDetail::ResourceBuilder b(device);const auto target=[&](RHITextureFormat format){Target t;t.width=columns*8u;t.height=rows*8u;
        t.color=b.StorageTexture(t.width,t.height,format);return t;};
    auto pdf=target(RHITextureFormat::RGBA16F),rays=target(RHITextureFormat::RGBA32F);
    auto constants=b.Uniform(sizeof(PipelineDetail::UeLumenProbeImportanceConstants));auto owner=b.Finish();
    s.computeOwner=std::move(compute);s.programs=programs;s.brdfSh=brdf;s.targetsOwner=std::move(owner);
    s.lightingPdf=pdf;s.rayInfo=rays;s.constantsBuffer=constants;s.columns=columns;s.rows=rows;
    // UE disables this source when the old gather atlas extent no longer fits.
    s.completed={};s.pending={};s.recorded=false;
}

void RecordUeLumenProbeImportance(RHIFrameEncoder& encoder,const SurfaceCacheGiSettings& gi,
    const PipelineCamera& camera,std::uint64_t viewStateFrame,bool cameraCut,bool previousTransformsReset=false){
    Require(gi.ueLumenHigh,"UE importance sampling is only available in UE High");
    auto& s=ueLumenProbeImportance;const auto& n=nativeProbeTransport;const auto& gather=ueLumenHighGather;
    Require(s.computeOwner&&s.targetsOwner&&gather.targetsOwner&&gather.radiance[0].color.IsValid(),"UE importance sampling was not prepared");
    Require(encoder.KeepAlive(s.computeOwner)&&encoder.KeepAlive(s.targetsOwner)&&encoder.KeepAlive(ueHighAO.samplingOwner),
        "Cannot retain UE importance-sampling resources");
    Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::UeLumenProbeImportance),"Cannot time UE importance sampling");
    const auto& history=s.completed;
    const bool useHistory=history.valid&&!cameraCut&&!previousTransformsReset&&history.extent.x==int(width)&&history.extent.y==int(height)&&
        history.extent.z==int(s.columns)&&history.extent.w==int(placementUniform[n.probeImage].height)&&
        history.atlas.y==int(gather.radiance[0].width)&&history.atlas.z==int(gather.radiance[0].height);
    if(useHistory)Require(encoder.KeepAlive(history.placementOwner)&&encoder.KeepAlive(history.radianceOwner),"Cannot retain UE importance radiance history");
    const bool objectMotion=statistics.surfaceCacheReceiverMotionDraws>0||statistics.surfaceCacheStaticReceiverMotionCapture;
    PipelineDetail::UeLumenProbeImportanceConstants c;c.traceInfo={16,1,int(viewStateFrame%8u),0};
    if(useHistory){c.historyViewProjection=history.viewProjection;c.historyExtent=history.extent;c.historyAtlas=history.atlas;c.historyJitter=history.jitter;}
    c.historyFlags={useHistory?1:0,objectMotion?1:0,0,0};s.recordedConstants=c;
    Upload(encoder,s.constantsBuffer,&c,sizeof(c),15);
    Require(encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&
        encoder.BindTexture(placementUniform[n.probeImage].color,4)&&encoder.BindTexture(placementSpawned[n.probeImage].color,5),
        "Cannot bind current UE BRDF source footprint");
    const auto command=[&](unsigned stage){RHICommand::DispatchFrameCompute d;d.pipeline=s.programs[stage];d.groupsX=s.columns;d.groupsY=s.rows;return d;};
    auto brdf=command(0);brdf.buffers[0]=s.brdfSh;Require(encoder.DispatchCompute(brdf),"Cannot compute UE BRDF SH3 PDF");
    BindUeLumenRadianceCache(encoder);
    Require(encoder.BindTexture(ueHighAO.blueNoiseVec2,7)&&
        encoder.BindTexture(useHistory?history.radiance:RenderResourceHandle<RHITextureSpec>{},10)&&
        encoder.BindTexture(useHistory?history.uniform:RenderResourceHandle<RHITextureSpec>{},11)&&
        encoder.BindTexture(objectMotion?surfaceCacheObjectPreviousPositions.color:RenderResourceHandle<RHITextureSpec>{},12),
        "Cannot bind UE lighting PDF history/cache sources");
    auto lighting=command(1);lighting.images[0]=s.lightingPdf.color;
    Require(encoder.DispatchCompute(lighting),"Cannot compute UE incoming-lighting PDF");
    Require(encoder.BindTexture(s.lightingPdf.color,0),"Cannot bind UE incoming-lighting PDF");
    auto generate=command(2);generate.buffers[0]=s.brdfSh;generate.images[0]=s.rayInfo.color;
    Require(encoder.DispatchCompute(generate),"Cannot generate UE structured importance RayInfos");

    s.pending.valid=true;s.pending.viewProjection=camera.projection*camera.view;
    s.pending.extent={int(width),int(height),int(s.columns),int(placementUniform[n.probeImage].height)};
    s.pending.atlas={placementFrameConstants.grid.w,int(gather.radiance[0].width),int(gather.radiance[0].height),0};
    const unsigned phase=unsigned(placementFrameConstants.screen.z)&7u,reverse=((phase&1u)<<2u)|(phase&2u)|((phase&4u)>>2u);
    const auto jitter=glm::ivec2(glm::vec2(float(phase),float(reverse))*(float(placementFrameConstants.grid.w)/8.f));
    s.pending.jitter=glm::vec4(glm::vec2(jitter),0,0);s.pending.uniform=placementUniform[n.probeImage].color;
    // The same queue executes the gather after this pass. The next frame reads
    // this filtered atlas before that frame's gather overwrites it, as UE's
    // extracted ImportanceSamplingHistoryScreenProbeRadiance source does.
    s.pending.radiance=gather.radiance[0].color;s.pending.placementOwner=placementTargetsOwner;s.pending.radianceOwner=gather.targetsOwner;
    s.recorded=true;
    Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::UeLumenProbeImportance),"Cannot finish UE importance timing");
}

void BindUeLumenProbeImportance(RHIFrameEncoder& encoder){auto& s=ueLumenProbeImportance;
    Require(s.recorded&&s.targetsOwner&&ueHighAO.samplingOwner,"UE importance RayInfos were not recorded");
    Require(encoder.KeepAlive(s.targetsOwner)&&encoder.KeepAlive(ueHighAO.samplingOwner),"Cannot retain UE importance tracing inputs");
    Upload(encoder,s.constantsBuffer,&s.recordedConstants,sizeof(s.recordedConstants),15);
    Require(encoder.BindTexture(ueHighAO.blueNoiseScalar,6)&&encoder.BindTexture(ueHighAO.blueNoiseVec2,7)&&encoder.BindTexture(s.rayInfo.color,8),
        "Cannot bind UE structured importance RayInfos");
}

void CommitUeLumenProbeImportance(bool success){auto& s=ueLumenProbeImportance;
    if(!s.recorded)return;if(success)s.completed=std::move(s.pending);else s.completed={};s.pending={};s.recorded=false;
}
