// Included inside MaterialRenderPipeline::Impl. This is a local actual-ray
// resolve after probe/spatial reconstruction, before temporal accumulation.
bool SurfaceCacheContactActive(const SurfaceCacheGiSettings& s) const {
    return s.enabled&&s.useScreenProbes&&s.contact.enabled&&!(s.view==2&&s.reconstructionDebug!=0)&&
        (s.view==0||s.view==2||(s.view==5&&s.temporal.enabled));
}
void PrepareSurfaceCacheContact(const SurfaceCacheGiSettings& s) {
    if(!SurfaceCacheContactActive(s))return;
    if(!surfaceCacheContactProgramsOwner) {
        PipelineDetail::ResourceBuilder b(device);const auto vertex=FullscreenVertexShader();
        surfaceCacheContactRiskProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ContactProbeRiskShader());
        surfaceCacheContactMaskProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ContactMaskShader());
        surfaceCacheContactMaskExpandProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ContactMaskExpandShader());
        surfaceCacheContactGatherProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ContactGatherShader());
        surfaceCacheContactFilterProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ContactFilterShader());
        surfaceCacheContactResolveProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ContactMeasurementResolveShader());
        surfaceCacheContactBuffer=b.Uniform(sizeof(SurfaceCacheContactConstants));surfaceCacheContactProgramsOwner=b.Finish();
    }
    const auto iw=surfaceCacheProbeIrradiance.width,ih=surfaceCacheProbeIrradiance.height;
    if(!surfaceCacheContactOwner||surfaceCacheContactRisk.width!=iw||surfaceCacheContactRisk.height!=ih||
        surfaceCacheContactGi.width!=width||surfaceCacheContactGi.height!=height) {
        PipelineDetail::ResourceBuilder b(device);
        const auto target=[&](unsigned w,unsigned h){Target t;t.width=w;t.height=h;
            t.color=b.Texture(w,h,RHITextureFormat::RGBA32F);t.framebuffer=b.Target(t.color,{});return t;};
        auto risk=target(iw,ih),mask=target(width,height),unfiltered=target(width,height),gi=target(width,height);
        auto scratch=target(width,height),measurements=target(width,height);
        auto owner=b.Finish();surfaceCacheContactRisk=risk;surfaceCacheContactMask=mask;
        surfaceCacheContactUnfilteredGi=unfiltered;surfaceCacheContactGi=gi;
        surfaceCacheContactFilterScratch=scratch;
        surfaceCacheContactMeasurements=measurements;
        surfaceCacheContactOwner=std::move(owner);
    }
}
void RecordSurfaceCacheContact(RHIFrameEncoder& encoder,const SurfaceCacheGiSettings& gi,const Target& input,
    std::uint32_t baseSeed,std::uint64_t sequence,bool temporal) {
    const auto& s=gi.contact;const auto probeImage=placementPending.image;
    const bool guided=s.geometryGuidedFilter&&!s.referenceReceiver&&s.filterStrength>0;
    const bool expandMask=s.screenContactDetection&&!s.referenceReceiver;
    const bool curvedMetadata=gi.directionalReconstruction||(int(placementFrameConstants.refinement.x+.5f)&8)!=0;
    SurfaceCacheContactConstants c;
    c.parameters={s.maxDistance,std::tan(glm::radians(s.parallaxAngleDegrees)),
        std::tan(glm::radians(std::min(85.f,2.f*s.parallaxAngleDegrees))),s.weightThreshold};
    c.options={int(s.raysPerPixel),int(s.filterRadius),s.layerValidation?1:0,
        (guided?1:0)|(s.referenceReceiver?2:0)|(curvedMetadata?8:0)};
    c.filter={s.filterStrength,.08f,.9659258263f,gi.originSafetyEpsilon};
    c.sampling={baseSeed,std::uint32_t(sequence),temporal&&s.temporalDirections?1u:0u,s.coverageWeightedFilter?1u:0u};
    c.guide={std::tan(glm::radians(s.guideHitAngleDegrees)),float(s.guideMaxRadius),s.screenContactDetection?1.f:0.f,s.coherentProbeRisk?1.f:0.f};
    Upload(encoder,surfaceCacheContactBuffer,&c,sizeof(c),SurfaceCacheContactBinding);
    Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheContactTrace),"Cannot begin contact trace GPU timing");
    // The unfiltered ray atlas supplies geometry distances and statuses. The
    // input lighting may already be spatially filtered; neither selects risk.
    Require(encoder.BindTexture({},15),"Cannot unbind contact probe-risk output");
    BindTarget(encoder,surfaceCacheContactRisk);
    Require(encoder.BindPipeline(surfaceCacheContactRiskProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(sdfRays[tracePending.image].color,7)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),
        "Cannot summarize geometric contact risk");
    Require(encoder.BindTexture({},1),"Cannot unbind contact receiver-mask output");
    // Reuse the public unfiltered target only until receiver tracing starts;
    // the following expansion writes the final mask into its dedicated image.
    BindTarget(encoder,expandMask?surfaceCacheContactUnfilteredGi:surfaceCacheContactMask);
    Require(encoder.BindPipeline(surfaceCacheContactMaskProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(input.color,0)&&encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&
        encoder.BindTexture(placementUniform[probeImage].color,5)&&encoder.BindTexture(placementSpawned[probeImage].color,6)&&
        encoder.BindTexture(curvedMetadata?surfaceCacheProbeDirectional.color:RenderResourceHandle<RHITextureSpec>{},8)&&
        encoder.BindTexture(surfaceCacheProbeActiveMask.color,10)&&
        encoder.BindTexture(surfaceCacheSafeOriginsActive?surfaceCacheProbeOrigins.color:RenderResourceHandle<RHITextureSpec>{},11)&&
        encoder.BindTexture(surfaceCacheContactRisk.color,15)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),
        "Cannot classify contact reconstruction receivers");
    if(expandMask) {
        BindTarget(encoder,surfaceCacheContactMask);
        Require(encoder.BindPipeline(surfaceCacheContactMaskExpandProgram)&&encoder.BindMesh(fullscreen)&&
            encoder.BindTexture(surfaceCacheContactUnfilteredGi.color,0)&&encoder.BindTexture(indirectPositions.color,2)&&
            encoder.BindTexture(normalCapture.color,3)&&encoder.BindTexture(diffuseCapture.color,4)&&
            encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot extend compatible contact receiver risk");
        // The non-guided gather writes this former raw-mask image next.
        Require(encoder.BindTexture({},0),"Cannot unbind temporary contact risk before receiver tracing");
    }
    Require(encoder.BindTexture({},6),"Cannot unbind contact measurements output");
    BindTarget(encoder,guided?surfaceCacheContactMeasurements:surfaceCacheContactUnfilteredGi);
    Require(encoder.BindPipeline(surfaceCacheContactGatherProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(input.color,0)&&encoder.BindTexture(surfaceCacheContactMask.color,1)&&
        encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&encoder.BindTexture(diffuseCapture.color,4)&&
        encoder.BindTexture(surfaceCacheGeometry,9)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),
        "Cannot trace actual contact receivers");
    Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheContactTrace),"Cannot end contact trace GPU timing");
    // These markers are omitted by ordinary frame encoders. Profiling closes
    // the producer's rendering scope before its timestamp and includes the
    // final filter attachment store, without another submission or readback.
    Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheContactFilter),"Cannot begin contact filter GPU timing");
    if(guided) {
        BindTarget(encoder,surfaceCacheContactUnfilteredGi);
        Require(encoder.BindPipeline(surfaceCacheContactResolveProgram)&&encoder.BindMesh(fullscreen)&&
            encoder.BindTexture(surfaceCacheContactMeasurements.color,0)&&encoder.BindTexture(surfaceCacheContactMask.color,1)&&
            encoder.BindTexture(input.color,5)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot decode contact measurement diagnostics");
    }
    const unsigned passes=s.filterStrength>0?s.filterPasses:0u;
    const Target* source=guided?&surfaceCacheContactMeasurements:&surfaceCacheContactUnfilteredGi;
    for(unsigned pass=0;pass<passes;++pass) {
        // Choose parity so the last pass always lands in the public GI target.
        // Reusing it on alternate passes needs only one extra full-size image.
        const auto& target=(passes-pass)%2?surfaceCacheContactGi:surfaceCacheContactFilterScratch;
        c.options.y=guided?int(std::min(s.guideMaxRadius,s.filterRadius<<pass)):int(s.filterRadius);
        c.options.w=(guided?1:0)|(s.referenceReceiver?2:0)|(pass+1==passes?4:0);
        Upload(encoder,surfaceCacheContactBuffer,&c,sizeof(c),SurfaceCacheContactBinding);
        Require(encoder.BindTexture({},0),"Cannot unbind contact-filter output");
        BindTarget(encoder,target);
        Require(encoder.BindPipeline(surfaceCacheContactFilterProgram)&&encoder.BindMesh(fullscreen)&&
            encoder.BindTexture(source->color,0)&&encoder.BindTexture(surfaceCacheContactMask.color,1)&&
            encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&encoder.BindTexture(diffuseCapture.color,4)&&
            encoder.BindTexture(input.color,5)&&encoder.BindTexture(guided?surfaceCacheContactMeasurements.color:input.color,6)&&
            encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot locally filter actual contact receivers");
        source=&target;
    }
    Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheContactFilter),"Cannot end contact filter GPU timing");
    surfaceCacheContactResult=passes?surfaceCacheContactGi:surfaceCacheContactUnfilteredGi;
    statistics.surfaceCacheContactPasses=3+passes+unsigned(guided)+unsigned(expandMask);statistics.surfaceCacheContactCollected=true;
    statistics.surfaceCachePasses+=statistics.surfaceCacheContactPasses;
}
