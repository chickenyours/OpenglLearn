// Included inside MaterialRenderPipeline::Impl after the native-probe state
// and the original contact helpers. This prepares only the original geometric
// probe risk and receiver soft mask. It never traces/gathers full-screen GI,
// optionally fuses original exposure classification into the final mask MRT,
// with an optional shared-stencil Base/raw-mask reconstruction before Expand.
void PrepareNativeContactRiskAndMaskOnly(const SurfaceCacheGiSettings& gi) {
    if(!gi.enabled||!gi.contact.enabled)return;
    auto& state=nativeProbeTransport;
    if(!state.contactRiskProgramsOwner) {
        PipelineDetail::ResourceBuilder b(device);const auto vertex=FullscreenVertexShader();
        state.contactRiskProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ContactProbeRiskShader());
        state.contactMaskProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::NativeContactMaskShader());
        state.contactMaskExpandProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ContactMaskExpandShader());
        state.contactRiskBuffer=b.Uniform(sizeof(SurfaceCacheContactConstants));state.contactRiskProgramsOwner=b.Finish();
    }
    const auto iw=state.irradiance.width,ih=state.irradiance.height;
    Require(iw>0&&ih>0,"Native contact risk requires prepared native probes");
    if(!state.contactRiskOwner||state.NativeProbeRisk.width!=iw||state.NativeProbeRisk.height!=ih||
        state.NativeSoftMask.width!=width||state.NativeSoftMask.height!=height) {
        PipelineDetail::ResourceBuilder b(device);
        const auto target=[&](unsigned w,unsigned h){Target t;t.width=w;t.height=h;
            t.color=b.Texture(w,h,RHITextureFormat::RGBA32F);t.framebuffer=b.Target(t.color,{});return t;};
        state.NativeProbeRisk=target(iw,ih);state.NativeSoftMask=target(width,height);state.NativeRawMask=target(width,height);
        state.contactRiskOwner=b.Finish();
    }
    if(gi.nativeAdaptiveFreshSampling){
        PrepareNativeFreshSampling(gi);
        if(!state.adaptiveMaskProgramsOwner){
            PipelineDetail::ResourceBuilder b(device);const auto vertex=FullscreenVertexShader();
            for(unsigned expand=0;expand<2;++expand)for(unsigned motion=0;motion<2;++motion)
                state.freshMaskPrograms[expand][motion]=b.FullscreenPipeline(vertex,SurfaceCacheDetail::NativeFreshContactMaskShader(expand!=0,motion!=0));
            state.adaptiveMaskProgramsOwner=b.Finish();
        }
        if(!state.adaptiveMaskOwner||state.freshMaskTarget.color!=state.NativeSoftMask.color||
            state.freshMaskSamplingIdentity!=surfaceCacheFreshSampling.color){
            PipelineDetail::ResourceBuilder b(device);Target t=state.NativeSoftMask;
            std::array<RenderResourceHandle<RHITextureSpec>,4> colors{{state.NativeSoftMask.color,surfaceCacheFreshSampling.color,{},{}}};
            t.framebuffer=b.TargetColors(colors);
            state.freshMaskSamplingIdentity=surfaceCacheFreshSampling.color;
            state.freshMaskTarget=t;state.adaptiveMaskOwner=b.Finish();
        }
    }
    if(gi.fuseNativeReconstruction||gi.nativeReconstructionComparison){
        if(!state.reconstructionProgramsOwner){
            PipelineDetail::ResourceBuilder b(device);
            state.reconstructionMaskProgram=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::NativeReconstructionMaskShader());
            state.reconstructionProgramsOwner=b.Finish();
        }
        if(!state.reconstructionTargetOwner||state.reconstructionMaskTarget.color!=state.NativeBaseGI.color||
            state.reconstructionRawIdentity!=state.NativeRawMask.color){
            PipelineDetail::ResourceBuilder b(device);auto t=state.NativeBaseGI;
            t.framebuffer=b.TargetColors({state.NativeBaseGI.color,state.NativeRawMask.color,{},{}});
            state.reconstructionMaskTarget=t;state.reconstructionRawIdentity=state.NativeRawMask.color;
            state.reconstructionTargetOwner=b.Finish();
        }
    }
    if(gi.nativeReconstructionComparison&&(!state.reconstructionComparisonOwner||
        state.reconstructionComparison[0].width!=width||state.reconstructionComparison[0].height!=height||
        state.reconstructionFailedOrigins.width!=state.origins.width||state.reconstructionFailedOrigins.height!=state.origins.height)){
        PipelineDetail::ResourceBuilder b(device);
        const auto target=[&](unsigned w,unsigned h){Target t;t.width=w;t.height=h;
            t.color=b.Texture(w,h,RHITextureFormat::RGBA32F);t.framebuffer=b.Target(t.color,{});return t;};
        for(auto& t:state.reconstructionComparison)t=target(width,height);
        auto mrt=state.reconstructionComparison[2];mrt.framebuffer=b.TargetColors({mrt.color,state.reconstructionComparison[3].color,{},{}});
        state.reconstructionComparisonMrt=mrt;state.reconstructionFailedOrigins=target(state.origins.width,state.origins.height);
        state.reconstructionComparisonOwner=b.Finish();
    }
}

bool NativeReconstructionSharedStencil(const SurfaceCacheGiSettings& gi,const SurfaceCacheContactConstants& c) const {
    const auto& p=placementFrameConstants;const unsigned refinement=unsigned(p.refinement.x+.5f);
    const unsigned debug=gi.view==2?gi.reconstructionDebug:0;
    if(debug||(p.tracing.w&2)==0||(refinement&1)==0)return false;
    // SurfaceCacheConstants uses this same curvature-weights flag for PG's
    // bit128. The original CT consumer has its own bit8 and must match it.
    if((refinement&8)!=0&&((c.options.w&8)==0))return false;
    return true;
}
void BindNativeReconstructionSources(RHIFrameEncoder& encoder,RenderResourceHandle<RHITextureSpec> origins) {
    const auto& n=nativeProbeTransport;const auto image=placementPending.image;
    Require(encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&
        encoder.BindTexture(diffuseCapture.color,4)&&encoder.BindTexture(placementUniform[image].color,5)&&
        encoder.BindTexture(placementSpawned[image].color,6)&&encoder.BindTexture(n.irradiance.color,7)&&
        encoder.BindTexture(n.coefficients[0].color,8)&&encoder.BindTexture(n.activeMask.color,10)&&
        encoder.BindTexture(origins,11)&&encoder.BindTexture(n.NativeProbeRisk.color,15),"Cannot bind native reconstruction snapshot");
}
void ReplayNativeReconstructionComparison(RHIFrameEncoder& encoder,const SurfaceCacheGiSettings& gi,const SurfaceCacheContactConstants& original) {
    if(!gi.nativeReconstructionComparison)return;auto& n=nativeProbeTransport;
    Require(encoder.KeepAlive(n.reconstructionComparisonOwner)&&encoder.KeepAlive(n.reconstructionProgramsOwner),
        "Cannot retain same-producer reconstruction replay");
    auto c=original;auto origins=n.origins.color;
    if(gi.nativeReconstructionComparison==2)c.options.w&=~8;
    if(gi.nativeReconstructionComparison==3){
        Require(encoder.BindTexture({},11),"Cannot unbind failed-origin replay image");
        BindTarget(encoder,n.reconstructionFailedOrigins,RHICommand::ClearColor);origins=n.reconstructionFailedOrigins.color;
    }
    Upload(encoder,n.contactRiskBuffer,&c,sizeof(c),SurfaceCacheContactBinding);
    Require(encoder.BindTexture({},0),"Cannot unbind reference reconstruction output");
    BindTarget(encoder,n.reconstructionComparison[0]);BindNativeReconstructionSources(encoder,origins);
    Require(encoder.BindPipeline(n.reconstructProgram)&&encoder.BindMesh(fullscreen)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),
        "Cannot replay original native Base reconstruction");
    // SetRenderTarget clears backend mesh/pipeline state. Every replay draw
    // must rebind both, even when its geometry is the same fullscreen triangle.
    BindTarget(encoder,n.reconstructionComparison[1]);
    Require(encoder.BindPipeline(n.contactMaskProgram)&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(n.reconstructionComparison[0].color,0)&&
        encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot replay original native raw contact mask");
    Require(encoder.BindTexture({},0),"Cannot unbind MRT reconstruction replay outputs");
    BindTarget(encoder,n.reconstructionComparisonMrt);BindNativeReconstructionSources(encoder,origins);
    Require(encoder.BindPipeline(n.reconstructionMaskProgram)&&encoder.BindMesh(fullscreen)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),
        "Cannot replay shared native reconstruction MRT");
    // No producer, sampling phase, history or production image advanced.
    Upload(encoder,n.contactRiskBuffer,&original,sizeof(original),SurfaceCacheContactBinding);
    Require(encoder.BindTexture(n.origins.color,11),"Cannot restore native certified origins after replay");
    n.reconstructionComparisonRecorded=true;statistics.surfaceCacheNativeReconstructionCompared=true;
    statistics.surfaceCacheNativeReconstructionComparisonShared=NativeReconstructionSharedStencil(gi,c);
    statistics.surfaceCacheNativeReconstructionReplayDraws=3;statistics.surfaceCachePasses+=3+unsigned(gi.nativeReconstructionComparison==3);
}

const Target& RecordNativeContactRiskAndMaskOnly(RHIFrameEncoder& encoder,const SurfaceCacheGiSettings& gi,
    const PipelineCamera* camera=nullptr,bool historyValid=false,bool active=false,bool objectMotion=false) {
    auto& state=nativeProbeTransport;const auto& s=gi.contact;const auto probeImage=placementPending.image;
    Require(state.contactRiskOwner&&state.contactRiskProgramsOwner&&state.NativeBaseGI.color.IsValid(),
        "Native contact risk resources were not prepared");
    Require(encoder.KeepAlive(state.contactRiskOwner)&&encoder.KeepAlive(state.contactRiskProgramsOwner),
        "Cannot pin native contact risk resources");
    const bool guided=s.geometryGuidedFilter&&!s.referenceReceiver&&s.filterStrength>0;
    const bool expandMask=s.screenContactDetection&&!s.referenceReceiver;
    const bool curvedMetadata=gi.directionalReconstruction||(int(placementFrameConstants.refinement.x+.5f)&8)!=0;
    SurfaceCacheContactConstants c;
    c.parameters={s.maxDistance,std::tan(glm::radians(s.parallaxAngleDegrees)),
        std::tan(glm::radians(std::min(85.f,2.f*s.parallaxAngleDegrees))),s.weightThreshold};
    c.options={int(s.raysPerPixel),int(s.filterRadius),s.layerValidation?1:0,
        (guided?1:0)|(s.referenceReceiver?2:0)|(curvedMetadata?8:0)};
    c.filter={s.filterStrength,.08f,.9659258263f,gi.originSafetyEpsilon};
    c.sampling={0,0,0,s.coverageWeightedFilter?1u:0u};
    if(s.prioritizeContactCore)c.options.w|=32;
    c.guide={std::tan(glm::radians(s.guideHitAngleDegrees)),float(s.guideMaxRadius),s.screenContactDetection?1.f:0.f,s.coherentProbeRisk?1.f:0.f};
    Upload(encoder,state.contactRiskBuffer,&c,sizeof(c),SurfaceCacheContactBinding);
    const bool adaptive=gi.nativeAdaptiveFreshSampling;
    if(adaptive){
        Require(camera&&encoder.KeepAlive(state.adaptiveMaskProgramsOwner)&&encoder.KeepAlive(state.adaptiveMaskOwner)&&
            encoder.KeepAlive(surfaceCacheFreshOwner)&&encoder.KeepAlive(surfaceCacheFreshProgramsOwner),"Cannot retain fused native exposure resources");
        const unsigned requestedMoving=gi.temporal.reactiveShadows&&gi.temporal.reactiveMovingReceivers?gi.contact.movingReceiverRayMultiplier:1u;
        const unsigned movingMultiplier=requestedMoving==2||requestedMoving==4?requestedMoving:1u;
        const bool enabled=SurfaceCacheFreshSamplingActive(gi)&&historyValid&&active&&
            (gi.contact.disocclusionRayMultiplier>1||movingMultiplier>1);
        SurfaceCacheTemporalConstants exposure;const auto& old=surfaceCacheTemporalCompleted;
        exposure.currentInverseView=glm::inverse(camera->view);exposure.previousViewProjection=old.camera.projection*old.camera.view;
        exposure.previousView=old.camera.view;exposure.screen={int(width),int(height),(enabled?1:0)|(objectMotion?32768:0),int(gi.temporal.maxFrames)};
        exposure.reject={gi.temporal.distanceThreshold,std::cos(glm::radians(gi.temporal.normalThresholdDegrees)),0,0};exposure.clamp.z=.002f;
        Upload(encoder,surfaceCacheTemporalBuffer,&exposure,sizeof(exposure),SurfaceCacheTemporalBinding);
        const int packedCaps=4|(movingMultiplier>1?int(movingMultiplier<<4):0)|
            (movingMultiplier>1&&gi.contact.movingReceiverFullCurrent?256:0);
        const glm::ivec4 sampling{enabled?1:0,int(gi.contact.raysPerPixel),int(gi.contact.disocclusionRayMultiplier),packedCaps};
        Upload(encoder,surfaceCacheFreshBuffer,&sampling,sizeof(sampling),11);
        Require(encoder.BindTexture(enabled?surfaceCacheTemporalPositions[old.image].color:RenderResourceHandle<RHITextureSpec>{},12)&&
            encoder.BindTexture(enabled?surfaceCacheTemporalNormals[old.image].color:RenderResourceHandle<RHITextureSpec>{},13)&&
            encoder.BindTexture(objectMotion?surfaceCacheObjectPreviousPositions.color:indirectPositions.color,14),"Cannot bind native exposure geometry");
        statistics.surfaceCacheFreshSamplingCollected=enabled;
    }
    if(s.collectTraceTimings)Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheContactRisk),"Cannot begin native-base receiver risk timing");
    Require(encoder.BindTexture({},15),"Cannot unbind native contact probe risk output");
    BindTarget(encoder,state.NativeProbeRisk);
    const auto rayMetadata=tracePending.sdf?sdfRays[tracePending.image].color:traceRays[tracePending.image].color;
    Require(encoder.BindPipeline(state.contactRiskProgram)&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(rayMetadata,7)&&
        encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot summarize native probe geometric contact risk");
    if(s.collectTraceTimings) {
        Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheContactRisk),"Cannot end native-base receiver risk timing");
        Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheContactMask),"Cannot begin native-base receiver mask timing");
    }
    const bool fused=state.baseReconstructionDeferred;
    if(fused){
        Require(expandMask&&encoder.KeepAlive(state.reconstructionProgramsOwner)&&encoder.KeepAlive(state.reconstructionTargetOwner),
            "Cannot retain fused native reconstruction resources");
        Require(encoder.BindTexture({},0)&&encoder.BindTexture({},1)&&encoder.BindTexture({},8),"Cannot unbind fused native reconstruction outputs");
        BindTarget(encoder,state.reconstructionMaskTarget);BindNativeReconstructionSources(encoder,state.origins.color);
        if(s.collectTraceTimings)Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::NativeProbeBaseReconstruction),"Cannot time fused native Base/raw-mask reconstruction");
        Require(encoder.BindPipeline(state.reconstructionMaskProgram)&&encoder.BindMesh(fullscreen)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),
            "Cannot reconstruct native Base and raw contact mask together");
        if(s.collectTraceTimings)Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::NativeProbeBaseReconstruction),"Cannot finish fused native Base/raw-mask reconstruction timing");
        statistics.surfaceCacheNativeReconstructionFused=true;
        statistics.surfaceCacheNativeReconstructionSharedStencil=NativeReconstructionSharedStencil(gi,c);
        statistics.surfaceCacheNativeBaseTimedWithContactMask=true;
    } else {
    Require(encoder.BindTexture({},1)&&encoder.BindTexture({},8),"Cannot unbind native contact soft mask output");
    BindTarget(encoder,expandMask?state.NativeRawMask:(adaptive?state.freshMaskTarget:state.NativeSoftMask));
    Require(encoder.BindPipeline(adaptive&&!expandMask?state.freshMaskPrograms[0][objectMotion?1:0]:state.contactMaskProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(state.NativeBaseGI.color,0)&&encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&
        (!adaptive||encoder.BindTexture(diffuseCapture.color,4))&&
        encoder.BindTexture(placementUniform[probeImage].color,5)&&encoder.BindTexture(placementSpawned[probeImage].color,6)&&
        encoder.BindTexture(curvedMetadata?surfaceCacheProbeDirectional.color:RenderResourceHandle<RHITextureSpec>{},8)&&
        encoder.BindTexture(surfaceCacheProbeActiveMask.color,10)&&
        encoder.BindTexture(surfaceCacheSafeOriginsActive?surfaceCacheProbeOrigins.color:RenderResourceHandle<RHITextureSpec>{},11)&&
        encoder.BindTexture(state.NativeProbeRisk.color,15)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),
        "Cannot classify original contact mask from native probe geometry");
    }
    statistics.surfaceCacheNativeReconstructionDraws=fused?1:2;
    ReplayNativeReconstructionComparison(encoder,gi,c);
    if(expandMask) {
        BindTarget(encoder,adaptive?state.freshMaskTarget:state.NativeSoftMask);
        Require(encoder.BindPipeline(adaptive?state.freshMaskPrograms[1][objectMotion?1:0]:state.contactMaskExpandProgram)&&encoder.BindMesh(fullscreen)&&
            encoder.BindTexture(state.NativeRawMask.color,0)&&encoder.BindTexture(indirectPositions.color,2)&&
            encoder.BindTexture(normalCapture.color,3)&&encoder.BindTexture(diffuseCapture.color,4)&&
            encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot extend original compatible native contact mask");
        Require(encoder.BindTexture({},0),"Cannot unbind native raw contact mask");
    }
    if(s.collectTraceTimings)Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheContactMask),"Cannot end native-base receiver mask timing");
    Require(encoder.BindTexture(surfaceCacheGeometry,9)&&encoder.BindTexture(transportPrecision,15),
        "Cannot restore native receiver geometry after contact risk");
    const unsigned passes=2u+unsigned(expandMask);
    statistics.surfaceCacheContactPasses+=passes;statistics.surfaceCachePasses+=passes;
    if(adaptive)BindSurfaceCache(encoder);
    return state.NativeSoftMask;
}
