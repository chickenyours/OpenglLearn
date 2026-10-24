// Included inside MaterialRenderPipeline::Impl. This is a local actual-ray
// resolve after probe/spatial reconstruction, before temporal accumulation.
bool SurfaceCacheContactActive(const SurfaceCacheGiSettings& s) const {
    return s.enabled&&!s.nativeScreenProbeTransport&&s.useScreenProbes&&s.contact.enabled&&!(s.view==2&&s.reconstructionDebug!=0)&&
        (s.view==0||s.view==2||(s.view==5&&s.temporal.enabled));
}
bool SurfaceCacheFreshSamplingActive(const SurfaceCacheGiSettings& s) const {
    return (SurfaceCacheContactActive(s)||(s.enabled&&s.nativeScreenProbeTransport&&s.nativeAdaptiveFreshSampling))&&SurfaceCacheTemporalActive(s)&&
        s.contact.disocclusionSampling&&!s.contact.referenceReceiver;
}
// Native fusion consumes the original classifier's constants/age and the
// existing position-save variants. Populate the complete shared program owner
// so switching back to the original contact path retains all of its programs.
void PrepareNativeFreshSampling(const SurfaceCacheGiSettings& s) {
    if(!s.nativeAdaptiveFreshSampling)return;
    if(!surfaceCacheFreshProgramsOwner){
        PipelineDetail::ResourceBuilder b(device);const auto vertex=FullscreenVertexShader();
        surfaceCacheFreshClassifyProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ExposureSamplingShader(false));
        surfaceCacheFreshObjectClassifyProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ExposureSamplingShader(true));
        surfaceCacheFreshGatherProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ContactGatherShader(false,true));
        surfaceCacheFreshFilterProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ContactFilterShader(true));
        surfaceCacheFreshResolveProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ContactMeasurementResolveShader(true));
        surfaceCacheFreshPositionProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::TemporalGeometryShader(false,false,true));
        surfaceCacheFreshObjectPositionProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::TemporalGeometryShader(false,true,true));
        surfaceCacheFreshBuffer=b.Uniform(sizeof(glm::ivec4));surfaceCacheFreshProgramsOwner=b.Finish();
    }
    if(!surfaceCacheFreshOwner||surfaceCacheFreshSampling.width!=width||surfaceCacheFreshSampling.height!=height){
        PipelineDetail::ResourceBuilder b(device);Target t;t.width=width;t.height=height;
        t.color=b.Texture(width,height,RHITextureFormat::RGBA32F);t.framebuffer=b.Target(t.color,{});
        surfaceCacheFreshSampling=t;surfaceCacheFreshOwner=b.Finish();
    }
}
unsigned SurfaceCacheQueryContactKey(const SurfaceCacheGiSettings& s,bool nativeContact,bool nativeOrigins) const {
    return (s.reuseClosestTraversal?1u:0u)|(s.fastGeometryAddressing?2u:0u)|(nativeContact?4u:0u)|
        (nativeOrigins?8u:0u)|(nativeOrigins&&s.reuseOriginSoftwareCertification?16u:0u)|
        (s.contact.splitReceiverOrigins?32u:0u)|(s.contact.reuseLightingLookup?64u:0u)|(s.contact.fastLightingAddressing?128u:0u);
}
void PrepareSurfaceCacheContact(const SurfaceCacheGiSettings& s) {
    if(!SurfaceCacheContactActive(s))return;
    if(s.contact.halfResolutionScreenRisk&&(!surfaceCacheBudgetRiskOwner||
        surfaceCacheBudgetScreenRisk.width!=(width+1)/2||surfaceCacheBudgetScreenRisk.height!=(height+1)/2)) {
        PipelineDetail::ResourceBuilder b(device);Target target;target.width=(width+1)/2;target.height=(height+1)/2;
        target.color=b.Texture(target.width,target.height,RHITextureFormat::RGBA32F);target.framebuffer=b.Target(target.color,{});
        surfaceCacheBudgetScreenRiskProgram=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::ContactBudgetScreenRiskShader(true));
        surfaceCacheBudgetMaskProgram=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::ContactBudgetScreenRiskShader(false));
        surfaceCacheBudgetScreenRisk=target;surfaceCacheBudgetRiskOwner=b.Finish();
    }
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
    if(s.hardwareContactTracing&&!surfaceCacheNativeContactProgramsOwner&&PrepareSurfaceCacheHardwareCapability()) {
        PipelineDetail::ResourceBuilder b(device);const auto vertex=FullscreenVertexShader();
        surfaceCacheNativeContactGatherProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ContactGatherShader(false,false,true));
        surfaceCacheNativeFreshGatherProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ContactGatherShader(false,true,true));
        surfaceCacheNativeSamplingDiagnosticProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ContactGatherShader(true,false,true));
        surfaceCacheNativeFreshSamplingDiagnosticProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ContactGatherShader(true,true,true));
        surfaceCacheNativeContactProgramsOwner=b.Finish();
    }
    if(s.hardwareOriginTracing&&!s.receiverSoftwareOrigins&&PrepareSurfaceCacheHardwareCapability()) {
        const unsigned contact=s.hardwareContactTracing?1u:0u;
        auto& owners=s.reuseOriginSoftwareCertification?surfaceCacheReusedNativeOriginContactProgramsOwner:surfaceCacheNativeOriginContactProgramsOwner;
        auto& programs=s.reuseOriginSoftwareCertification?surfaceCacheReusedNativeOriginContactPrograms:surfaceCacheNativeOriginContactPrograms;
        if(!owners[contact]) {
            PipelineDetail::ResourceBuilder b(device);const auto vertex=FullscreenVertexShader();
            for(unsigned variant=0;variant<4;++variant)
                programs[contact][variant]=b.FullscreenPipeline(vertex,
                    SurfaceCacheDetail::ContactGatherShader((variant&2)!=0,(variant&1)!=0,contact!=0,true,s.reuseOriginSoftwareCertification));
            owners[contact]=b.Finish();
        }
    }
    if(s.reuseClosestTraversal||s.fastGeometryAddressing||s.contact.splitReceiverOrigins||s.contact.reuseLightingLookup||s.contact.fastLightingAddressing) {
        const bool nativeContact=s.hardwareContactTracing&&surfaceCacheHardwareSupported&&bool(surfaceCacheNativeContactProgramsOwner);
        const bool nativeOrigins=s.hardwareOriginTracing&&!s.receiverSoftwareOrigins&&surfaceCacheHardwareSupported;
        const unsigned key=SurfaceCacheQueryContactKey(s,nativeContact,nativeOrigins);
        if(!surfaceCacheQueryContactOwners[key]) {
            PipelineDetail::ResourceBuilder b(device);const auto vertex=FullscreenVertexShader();
            for(unsigned variant=0;variant<4;++variant)
                surfaceCacheQueryContactPrograms[key][variant]=b.FullscreenPipeline(vertex,
                    SurfaceCacheDetail::ContactGatherShader((variant&2)!=0,(variant&1)!=0,nativeContact,nativeOrigins,
                        s.reuseOriginSoftwareCertification,s.reuseClosestTraversal,s.fastGeometryAddressing,
                        s.contact.splitReceiverOrigins,s.contact.reuseLightingLookup,0,s.contact.fastLightingAddressing));
            if(s.contact.splitReceiverOrigins)for(unsigned fresh=0;fresh<2;++fresh)
                surfaceCacheQueryContactOriginPrograms[key][fresh]=b.FullscreenPipeline(vertex,
                    SurfaceCacheDetail::ContactOriginShader(fresh!=0,nativeOrigins,s.reuseOriginSoftwareCertification,
                        s.reuseClosestTraversal,s.fastGeometryAddressing));
            surfaceCacheQueryContactOwners[key]=b.Finish();
        }
    }
    if(s.contact.collectSamplingDiagnostics&&s.contact.traceDiagnosticPage>0) {
        const bool nativeContact=s.hardwareContactTracing&&surfaceCacheHardwareSupported&&bool(surfaceCacheNativeContactProgramsOwner);
        const bool nativeOrigins=s.hardwareOriginTracing&&!s.receiverSoftwareOrigins&&surfaceCacheHardwareSupported;
        const unsigned key=SurfaceCacheQueryContactKey(s,nativeContact,nativeOrigins),page=s.contact.traceDiagnosticPage;
        if(!surfaceCacheReceiverTraceDiagnosticOwners[key][page-1]) {
            PipelineDetail::ResourceBuilder b(device);
            for(unsigned fresh=0;fresh<2;++fresh)
                surfaceCacheReceiverTraceDiagnosticPrograms[key][(page-1)*2+fresh]=b.FullscreenPipeline(FullscreenVertexShader(),
                    SurfaceCacheDetail::ContactGatherShader(true,fresh!=0,nativeContact,nativeOrigins,
                        s.reuseOriginSoftwareCertification,s.reuseClosestTraversal,s.fastGeometryAddressing,
                        s.contact.splitReceiverOrigins,s.contact.reuseLightingLookup,page,s.contact.fastLightingAddressing));
            surfaceCacheReceiverTraceDiagnosticOwners[key][page-1]=b.Finish();
        }
    }
    if(s.contact.collectTraceDiagnostics) {
        const bool nativeContact=s.hardwareContactTracing&&surfaceCacheHardwareSupported&&bool(surfaceCacheNativeContactProgramsOwner);
        const bool nativeOrigins=s.hardwareOriginTracing&&!s.receiverSoftwareOrigins&&surfaceCacheHardwareSupported;
        const unsigned key=SurfaceCacheQueryContactKey(s,nativeContact,nativeOrigins);
        if(!surfaceCacheReceiverTraceMrtOwners[key]) {
            PipelineDetail::ResourceBuilder b(device);
            for(unsigned fresh=0;fresh<2;++fresh)
                surfaceCacheReceiverTraceMrtPrograms[key][fresh]=b.FullscreenPipeline(FullscreenVertexShader(),
                    SurfaceCacheDetail::ContactGatherTraceMrtShader(fresh!=0,nativeContact,nativeOrigins,
                        s.reuseOriginSoftwareCertification,s.reuseClosestTraversal,s.fastGeometryAddressing,
                        s.contact.splitReceiverOrigins,s.contact.reuseLightingLookup,s.contact.fastLightingAddressing));
            surfaceCacheReceiverTraceMrtOwners[key]=b.Finish();
        }
    }
    if(s.contact.splitReceiverOrigins&&(!surfaceCacheReceiverOriginsOwner||
        surfaceCacheReceiverOrigins.width!=width||surfaceCacheReceiverOrigins.height!=height)) {
        PipelineDetail::ResourceBuilder b(device);Target t;t.width=width;t.height=height;
        t.color=b.Texture(width,height,RHITextureFormat::RGBA32F);t.framebuffer=b.Target(t.color,{});
        surfaceCacheReceiverOrigins=t;surfaceCacheReceiverOriginsOwner=b.Finish();
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
    if(SurfaceCacheFreshSamplingActive(s)) {
        if(!surfaceCacheFreshProgramsOwner) {
            PipelineDetail::ResourceBuilder b(device);const auto vertex=FullscreenVertexShader();
            surfaceCacheFreshClassifyProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ExposureSamplingShader(false));
            surfaceCacheFreshObjectClassifyProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ExposureSamplingShader(true));
            surfaceCacheFreshGatherProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ContactGatherShader(false,true));
            surfaceCacheFreshFilterProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ContactFilterShader(true));
            surfaceCacheFreshResolveProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ContactMeasurementResolveShader(true));
            surfaceCacheFreshPositionProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::TemporalGeometryShader(false,false,true));
            surfaceCacheFreshObjectPositionProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::TemporalGeometryShader(false,true,true));
            surfaceCacheFreshBuffer=b.Uniform(sizeof(glm::ivec4));surfaceCacheFreshProgramsOwner=b.Finish();
        }
        if(!surfaceCacheFreshOwner||surfaceCacheFreshSampling.width!=width||surfaceCacheFreshSampling.height!=height) {
            PipelineDetail::ResourceBuilder b(device);Target t;t.width=width;t.height=height;
            t.color=b.Texture(width,height,RHITextureFormat::RGBA32F);t.framebuffer=b.Target(t.color,{});
            surfaceCacheFreshSampling=t;surfaceCacheFreshOwner=b.Finish();
        }
    }
    if(s.contact.collectSamplingDiagnostics&&(!surfaceCacheSamplingDiagnosticsOwner||
        surfaceCacheReceiverSampling.width!=width||surfaceCacheReceiverSampling.height!=height)) {
        PipelineDetail::ResourceBuilder b(device);const auto vertex=FullscreenVertexShader();
        surfaceCacheSamplingDiagnosticProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ContactGatherShader(true,false));
        surfaceCacheFreshSamplingDiagnosticProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ContactGatherShader(true,true));
        Target t;t.width=width;t.height=height;t.color=b.Texture(width,height,RHITextureFormat::RGBA32F);t.framebuffer=b.Target(t.color,{});
        surfaceCacheReceiverSampling=t;surfaceCacheSamplingDiagnosticsOwner=b.Finish();
    }
    if(s.contact.collectTraceDiagnostics&&(!surfaceCacheReceiverTraceMrtOwner||
        surfaceCacheReceiverTraceMrt.width!=width||surfaceCacheReceiverTraceMrt.height!=height||
        surfaceCacheReceiverTraceMrt.color!=surfaceCacheReceiverSampling.color)) {
        PipelineDetail::ResourceBuilder b(device);Target t;t.width=width;t.height=height;
        std::array<RenderResourceHandle<RHITextureSpec>,4> colors{};colors[0]=surfaceCacheReceiverSampling.color;
        for(unsigned i=0;i<3;++i)surfaceCacheReceiverTracePacked[i]=colors[i+1]=b.Texture(width,height,RHITextureFormat::RGBA32F);
        t.color=colors[0];t.framebuffer=b.TargetColors(colors);surfaceCacheReceiverTraceMrt=t;
        surfaceCacheReceiverTraceMrtOwner=b.Finish();
        surfaceCacheReceiverTraceSamplingOwner=surfaceCacheSamplingDiagnosticsOwner;
    }
}
void RecordSurfaceCacheFreshSampling(RHIFrameEncoder& encoder,const SurfaceCacheGiSettings& gi,
    const PipelineCamera& camera,bool historyValid,bool active,bool objectMotion) {
    const unsigned requestedMoving=gi.temporal.reactiveShadows&&gi.temporal.reactiveMovingReceivers?
        gi.contact.movingReceiverRayMultiplier:1u;
    const unsigned movingMultiplier=requestedMoving==2||requestedMoving==4?requestedMoving:1u;
    if(!SurfaceCacheFreshSamplingActive(gi)||!historyValid||!active||
        (gi.contact.disocclusionRayMultiplier<=1&&movingMultiplier<=1))return;
    SurfaceCacheTemporalConstants c;const auto& old=surfaceCacheTemporalCompleted;
    c.currentInverseView=glm::inverse(camera.view);c.previousViewProjection=old.camera.projection*old.camera.view;
    c.previousView=old.camera.view;c.screen={int(width),int(height),1|(objectMotion?32768:0),int(gi.temporal.maxFrames)};
    c.reject={gi.temporal.distanceThreshold,std::cos(glm::radians(gi.temporal.normalThresholdDegrees)),0,0};
    c.clamp.z=.002f;
    Upload(encoder,surfaceCacheTemporalBuffer,&c,sizeof(c),SurfaceCacheTemporalBinding);
    // Preserve the former 16-byte block and exact disabled value. Receiver
    // motion has a separate budget tag, never a fabricated exposure lifetime.
    const int packedCaps=4|(movingMultiplier>1?int(movingMultiplier<<4):0)|
        (movingMultiplier>1&&gi.contact.movingReceiverFullCurrent?256:0);
    const glm::ivec4 sampling{1,int(gi.contact.raysPerPixel),int(gi.contact.disocclusionRayMultiplier),packedCaps};
    Upload(encoder,surfaceCacheFreshBuffer,&sampling,sizeof(sampling),SurfaceCacheContactBinding);
    Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheFreshSampling),"Cannot begin fresh receiver classification timing");
    Require(encoder.BindTexture({},15),"Cannot unbind fresh receiver classification target");
    BindTarget(encoder,surfaceCacheFreshSampling);
    Require(encoder.BindPipeline(objectMotion?surfaceCacheFreshObjectClassifyProgram:surfaceCacheFreshClassifyProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(indirectPositions.color,0)&&encoder.BindTexture(normalCapture.color,1)&&encoder.BindTexture(diffuseCapture.color,2)&&
        encoder.BindTexture(surfaceCacheTemporalPositions[old.image].color,3)&&encoder.BindTexture(surfaceCacheTemporalNormals[old.image].color,4)&&
        encoder.BindTexture(surfaceCacheTemporalHistory[old.image].color,5)&&
        encoder.BindTexture(objectMotion?surfaceCacheObjectPreviousPositions.color:indirectPositions.color,6)&&
        encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot classify newly exposed GI receivers");
    Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheFreshSampling),"Cannot end fresh receiver classification timing");
    statistics.surfaceCacheFreshSamplingCollected=true;++statistics.surfaceCachePasses;
}
void RecordSurfaceCacheContact(RHIFrameEncoder& encoder,const SurfaceCacheGiSettings& gi,const Target& input,
    std::uint32_t baseSeed,std::uint64_t sequence,bool temporal) {
    const auto& s=gi.contact;const auto probeImage=placementPending.image;
    const bool nativeContact=gi.hardwareContactTracing&&surfaceCacheHardwareSupported&&bool(surfaceCacheNativeContactProgramsOwner);
    const auto& originOwners=gi.reuseOriginSoftwareCertification?surfaceCacheReusedNativeOriginContactProgramsOwner:surfaceCacheNativeOriginContactProgramsOwner;
    const auto& originPrograms=gi.reuseOriginSoftwareCertification?surfaceCacheReusedNativeOriginContactPrograms:surfaceCacheNativeOriginContactPrograms;
    const bool nativeOrigins=gi.hardwareOriginTracing&&!gi.receiverSoftwareOrigins&&surfaceCacheHardwareSupported&&
        bool(originOwners[nativeContact?1:0]);
    statistics.surfaceCacheHardwareContact=nativeContact;
    statistics.surfaceCacheHardwareOrigins|=nativeOrigins;
    statistics.surfaceCacheReusedOriginCertification|=nativeOrigins&&gi.reuseOriginSoftwareCertification;
    statistics.surfaceCacheSoftwareReceiverOrigins=!nativeOrigins;
    const unsigned queryKey=SurfaceCacheQueryContactKey(gi,nativeContact,nativeOrigins);
    const bool queryReuse=(gi.reuseClosestTraversal||gi.fastGeometryAddressing||s.splitReceiverOrigins||s.reuseLightingLookup||s.fastLightingAddressing)&&bool(surfaceCacheQueryContactOwners[queryKey]);
    statistics.surfaceCacheClosestTraversalReused|=queryReuse&&gi.reuseClosestTraversal;
    statistics.surfaceCacheFastGeometryAddressing|=queryReuse&&gi.fastGeometryAddressing;
    const bool splitOrigins=queryReuse&&s.splitReceiverOrigins&&bool(surfaceCacheReceiverOriginsOwner);
    statistics.surfaceCacheSplitReceiverOrigins=splitOrigins;
    statistics.surfaceCacheReceiverLookupReused=queryReuse&&s.reuseLightingLookup;
    statistics.surfaceCacheReceiverFastLightingAddressing=queryReuse&&s.fastLightingAddressing;
    const bool guided=s.geometryGuidedFilter&&!s.referenceReceiver&&s.filterStrength>0;
    const bool expandMask=s.screenContactDetection&&!s.referenceReceiver;
    const bool curvedMetadata=gi.directionalReconstruction||(int(placementFrameConstants.refinement.x+.5f)&8)!=0;
    const bool fresh=statistics.surfaceCacheFreshSamplingCollected;
    SurfaceCacheContactConstants c;
    c.parameters={s.maxDistance,std::tan(glm::radians(s.parallaxAngleDegrees)),
        std::tan(glm::radians(std::min(85.f,2.f*s.parallaxAngleDegrees))),s.weightThreshold};
    c.options={int(s.raysPerPixel),int(s.filterRadius),s.layerValidation?1:0,
        (guided?1:0)|(s.referenceReceiver?2:0)|(curvedMetadata?8:0)|(fresh?16:0)};
    c.filter={s.filterStrength,.08f,.9659258263f,gi.originSafetyEpsilon};
    c.sampling={baseSeed,std::uint32_t(sequence),temporal&&s.temporalDirections?1u:0u,s.coverageWeightedFilter?1u:0u};
    if(s.prioritizeContactCore)c.options.w|=32;
    c.guide={std::tan(glm::radians(s.guideHitAngleDegrees)),float(s.guideMaxRadius),s.screenContactDetection?1.f:0.f,s.coherentProbeRisk?1.f:0.f};
    Upload(encoder,surfaceCacheContactBuffer,&c,sizeof(c),SurfaceCacheContactBinding);
    Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheContactTrace),"Cannot begin contact trace GPU timing");
    if(s.collectTraceTimings)Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheContactRisk),"Cannot begin receiver risk timing");
    // The unfiltered ray atlas supplies geometry distances and statuses. The
    // input lighting may already be spatially filtered; neither selects risk.
    Require(encoder.BindTexture({},15),"Cannot unbind contact probe-risk output");
    BindTarget(encoder,surfaceCacheContactRisk);
    Require(encoder.BindPipeline(surfaceCacheContactRiskProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(sdfRays[tracePending.image].color,7)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),
        "Cannot summarize geometric contact risk");
    if(s.collectTraceTimings) {
        Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheContactRisk),"Cannot end receiver risk timing");
        Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheContactMask),"Cannot begin receiver mask timing");
    }
    Require(encoder.BindTexture({},1),"Cannot unbind contact receiver-mask output");
    if(s.halfResolutionScreenRisk) {
        Require(encoder.KeepAlive(surfaceCacheBudgetRiskOwner),"Cannot pin shared contact risk resources");
        BindTarget(encoder,surfaceCacheBudgetScreenRisk);
        Require(encoder.BindPipeline(surfaceCacheBudgetScreenRiskProgram)&&encoder.BindMesh(fullscreen)&&
            encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&
            encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot classify shared geometric contact risk");
        Require(encoder.BindTexture(surfaceCacheBudgetScreenRisk.color,1),"Cannot bind shared geometric risk");
    }
    // Reuse the public unfiltered target only until receiver tracing starts;
    // the following expansion writes the final mask into its dedicated image.
    BindTarget(encoder,expandMask?surfaceCacheContactUnfilteredGi:surfaceCacheContactMask);
    Require(encoder.BindPipeline(s.halfResolutionScreenRisk?surfaceCacheBudgetMaskProgram:surfaceCacheContactMaskProgram)&&encoder.BindMesh(fullscreen)&&
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
    if(s.collectTraceTimings)Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheContactMask),"Cannot end receiver mask timing");
    Require(encoder.BindTexture({},6),"Cannot unbind contact measurements output");
    if(fresh)Require(encoder.BindTexture(surfaceCacheFreshSampling.color,15),"Cannot bind current receiver ray budgets");
    if(splitOrigins) {
        if(s.collectTraceTimings)Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheContactOrigins),"Cannot begin receiver origin timing");
        Require(encoder.BindTexture({},5),"Cannot unbind receiver origin output");
        BindTarget(encoder,surfaceCacheReceiverOrigins);
        Require(encoder.BindPipeline(surfaceCacheQueryContactOriginPrograms[queryKey][fresh?1:0])&&encoder.BindMesh(fullscreen)&&
            encoder.BindTexture(surfaceCacheContactMask.color,1)&&encoder.BindTexture(indirectPositions.color,2)&&
            encoder.BindTexture(normalCapture.color,3)&&encoder.BindTexture(surfaceCacheGeometry,9)&&
            encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot prepare exact receiver origins");
        Require(encoder.BindTexture(surfaceCacheReceiverOrigins.color,5),"Cannot bind prepared receiver origins");
        if(s.collectTraceTimings)Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheContactOrigins),"Cannot end receiver origin timing");
    }
    if(s.collectTraceTimings)Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheContactGather),"Cannot begin receiver gather timing");
    BindTarget(encoder,guided?surfaceCacheContactMeasurements:surfaceCacheContactUnfilteredGi);
    const auto gatherProgram=queryReuse?surfaceCacheQueryContactPrograms[queryKey][fresh?1:0]:nativeOrigins?originPrograms[nativeContact?1:0][fresh?1:0]:
        nativeContact?(fresh?surfaceCacheNativeFreshGatherProgram:surfaceCacheNativeContactGatherProgram):
        (fresh?surfaceCacheFreshGatherProgram:surfaceCacheContactGatherProgram);
    Require(encoder.BindPipeline(gatherProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(input.color,0)&&encoder.BindTexture(surfaceCacheContactMask.color,1)&&
        encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&encoder.BindTexture(diffuseCapture.color,4)&&
        encoder.BindTexture(surfaceCacheGeometry,9)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),
        "Cannot trace actual contact receivers");
    if(s.collectTraceTimings)Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheContactGather),"Cannot end receiver gather timing");
    Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheContactTrace),"Cannot end contact trace GPU timing");
    if(s.collectSamplingDiagnostics) {
        // A diagnostic replay uses precisely the same origin/ray loop. It is
        // deliberately separate from ordinary rendering and benchmark spans.
        BindTarget(encoder,s.collectTraceDiagnostics?surfaceCacheReceiverTraceMrt:surfaceCacheReceiverSampling);
        const auto diagnosticProgram=s.collectTraceDiagnostics?surfaceCacheReceiverTraceMrtPrograms[queryKey][fresh?1:0]:
            s.traceDiagnosticPage>0?surfaceCacheReceiverTraceDiagnosticPrograms[queryKey][(s.traceDiagnosticPage-1)*2+(fresh?1:0)]:
            queryReuse?surfaceCacheQueryContactPrograms[queryKey][fresh?3:2]:nativeOrigins?originPrograms[nativeContact?1:0][fresh?3:2]:
            nativeContact?(fresh?surfaceCacheNativeFreshSamplingDiagnosticProgram:surfaceCacheNativeSamplingDiagnosticProgram):
            (fresh?surfaceCacheFreshSamplingDiagnosticProgram:surfaceCacheSamplingDiagnosticProgram);
        Require(encoder.BindPipeline(diagnosticProgram)&&
            encoder.BindMesh(fullscreen)&&encoder.BindTexture(input.color,0)&&encoder.BindTexture(surfaceCacheContactMask.color,1)&&
            encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&encoder.BindTexture(diffuseCapture.color,4)&&
            encoder.BindTexture(surfaceCacheGeometry,9)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot collect receiver sampling loop diagnostics");
        statistics.surfaceCacheReceiverSamplingCollected=true;++statistics.surfaceCachePasses;
        statistics.surfaceCacheReceiverTraceDiagnosticsCollected=s.collectTraceDiagnostics;
    }
    // These markers are omitted by ordinary frame encoders. Profiling closes
    // the producer's rendering scope before its timestamp and includes the
    // final filter attachment store, without another submission or readback.
    Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheContactFilter),"Cannot begin contact filter GPU timing");
    if(guided) {
        BindTarget(encoder,surfaceCacheContactUnfilteredGi);
        Require(encoder.BindPipeline(fresh?surfaceCacheFreshResolveProgram:surfaceCacheContactResolveProgram)&&encoder.BindMesh(fullscreen)&&
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
        c.options.w=(guided?1:0)|(s.referenceReceiver?2:0)|(pass+1==passes?4:0)|(fresh?16:0)|(s.prioritizeContactCore?32:0);
        Upload(encoder,surfaceCacheContactBuffer,&c,sizeof(c),SurfaceCacheContactBinding);
        Require(encoder.BindTexture({},0),"Cannot unbind contact-filter output");
        BindTarget(encoder,target);
        Require(encoder.BindPipeline(fresh?surfaceCacheFreshFilterProgram:surfaceCacheContactFilterProgram)&&encoder.BindMesh(fullscreen)&&
            encoder.BindTexture(source->color,0)&&encoder.BindTexture(surfaceCacheContactMask.color,1)&&
            encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&encoder.BindTexture(diffuseCapture.color,4)&&
            encoder.BindTexture(input.color,5)&&encoder.BindTexture(guided?surfaceCacheContactMeasurements.color:input.color,6)&&
            encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot locally filter actual contact receivers");
        source=&target;
    }
    Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheContactFilter),"Cannot end contact filter GPU timing");
    surfaceCacheContactResult=passes?surfaceCacheContactGi:surfaceCacheContactUnfilteredGi;
    statistics.surfaceCacheContactPasses=3+passes+unsigned(guided)+unsigned(expandMask);statistics.surfaceCacheContactCollected=true;
    statistics.surfaceCacheContactPasses+=unsigned(splitOrigins);
    statistics.surfaceCacheContactPasses+=unsigned(s.halfResolutionScreenRisk);
    statistics.surfaceCachePasses+=statistics.surfaceCacheContactPasses;
}
