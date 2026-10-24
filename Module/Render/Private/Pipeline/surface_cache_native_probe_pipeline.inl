// Included inside MaterialRenderPipeline::Impl after placement/transport
// helpers. The declaration header is included outside Impl; add one member:
// PipelineDetail::NativeProbeTransportState<Target> nativeProbeTransport;
//
// Call prepare after PrepareSurfaceCacheGi. This path supplies its own packed
// trace targets and never calls PrepareProbeSdfTrace/RecordProbeSdfFallback.
void PrepareNativeScreenProbeTransport(const SurfaceCacheGiSettings& gi,const ScreenProbePlacementSettings& placement) {
    Require(gi.enabled&&gi.scene&&gi.safeProbeOrigins&&placement.enabled&&placement.trace.enabled&&
        (gi.ueLumenHigh||placement.trace.cosineHemisphere)&&placement.trace.rayResolution==8,
        "Native screen-probe transport requires safe 8x8 probes");
    Require(PrepareSurfaceCacheHardwareCapability(),"Native screen-probe transport requires native ray queries");
    auto allocation=placement;
    // The old source-certification API uses this flag to enable GI placement.
    // UE clears only the trace-direction bit after placement has completed.
    if(gi.ueLumenHigh)allocation.trace.cosineHemisphere=true;
    PrepareScreenProbePlacement(allocation,gi.nativeComputePlacement,gi.collectNativePlacementComparison,gi.ueLumenHigh,gi.ueLumenHigh);
    PrepareTransportPrecision(gi);
    auto& n=nativeProbeTransport;
    if(!gi.ueLumenHigh&&!n.programsOwner) {
        PipelineDetail::ResourceBuilder b(device);const auto vertex=FullscreenVertexShader();
        n.traceProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::NativeScreenProbeTraceShader());
        n.traceDiagnosticProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::NativeScreenProbeTraceShader(true));
        n.packProgram=b.FullscreenPipeline(vertex,ScreenProbeTraceDetail::PackTraceOutputsShader());
        n.integrateProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ProbeDirectionalIntegrateShader());
        n.activeMaskProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ProbeActiveMaskShader());
        n.neighborsProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::NativeProbeSpatialNeighborsShader());
        n.neighborsDiagnosticProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::NativeProbeSpatialNeighborsShader(true));
        n.coefficientFilterProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ProbeCoefficientSpatialFilterShader());
        n.extractProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ProbeCoefficientExtractIrradianceShader());
        n.reconstructProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ProbeReconstructShader());
        n.receiverCoreProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::NativeBaseTransportResolveShader(1));
        n.receiverLowProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::NativeBaseTransportResolveShader(2));
        n.receiverFullProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::NativeBaseTransportResolveShader(3));
        n.receiverReconstructionProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::NativeBaseTransportReconstructionShader());
        n.receiverDiagnosticProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::NativeBaseTransportRayDiagnosticShader());
        n.spatialBuffer=b.Uniform(sizeof(SurfaceCacheSpatialConstants));n.programsOwner=b.Finish();
    }
    if(!gi.ueLumenHigh&&gi.nativeAdaptiveFreshSampling&&!n.adaptiveProgramsOwner){
        PipelineDetail::ResourceBuilder b(device);const auto vertex=FullscreenVertexShader();
        n.adaptiveCoreProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::NativeBaseTransportResolveShader(1,true));
        n.adaptiveLowProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::NativeBaseTransportResolveShader(2,true));
        n.adaptiveFullProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::NativeBaseTransportResolveShader(3,true));
        n.adaptiveDiagnosticProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::NativeBaseTransportRayDiagnosticShader(true));
        n.adaptiveReconstructionProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::NativeBaseTransportReconstructionShader(true));
        n.adaptiveProgramsOwner=b.Finish();
    }
    const auto columns=placementUniform[0].width/2,baseRows=placementUniform[0].height;
    const auto rows=baseRows*(1+placement.candidateCount);
    Require(columns&&rows&&columns*4<=8192,"Native screen-probe coefficient atlas exceeds limits");
    const auto texels=std::uint64_t(columns)*rows*64u*ScreenProbeTraceTexelsPerRay;
    unsigned atlasWidth=1024;
    while(atlasWidth<8192&&(texels+atlasWidth-1)/atlasWidth>8192)atlasWidth*=2;
    const auto atlasHeight=unsigned((texels+atlasWidth-1)/atlasWidth);
    Require(atlasHeight&&atlasHeight<=8192,"Native screen-probe ray atlas exceeds limits");
    const auto neighborWidth=std::min(columns*SurfaceCacheSpatialNeighborCount,8192u);
    const auto neighborTexels=std::uint64_t(columns)*rows*SurfaceCacheSpatialNeighborCount;
    const auto neighborHeight=unsigned((neighborTexels+neighborWidth-1)/neighborWidth);
    if(!n.targetsOwner||n.targetsUseUe!=gi.ueLumenHigh||n.packedRays[0].width!=atlasWidth||n.packedRays[0].height!=atlasHeight||
        n.origins.width!=columns||n.origins.height!=rows||(!gi.ueLumenHigh&&
        (n.irradiance.width!=columns||n.irradiance.height!=rows||n.activeMask.height!=baseRows||
        n.NativeBaseGI.width!=width||n.NativeBaseGI.height!=height))) {
        PipelineDetail::ResourceBuilder b(device);
        const auto target=[&](unsigned w,unsigned h){Target t;t.width=w;t.height=h;
            t.color=b.Texture(w,h,RHITextureFormat::RGBA32F);t.framebuffer=b.Target(t.color,{});return t;};
        std::array<Target,2> packed{{target(atlasWidth,atlasHeight),target(atlasWidth,atlasHeight)}};
        std::array<RenderResourceHandle<RHITextureSpec>,4> parts;
        for(auto& part:parts)part=b.Texture(atlasWidth/4,atlasHeight,RHITextureFormat::RGBA32F);
        Target outputs;outputs.width=atlasWidth/4;outputs.height=atlasHeight;outputs.color=parts[0];
        outputs.framebuffer=b.TargetColors(parts);
        n.origins=target(columns,rows);
        if(!gi.ueLumenHigh){for(auto& coefficient:n.coefficients)coefficient=target(columns*4,rows);
            n.activeMask=target(columns,baseRows);n.coefficientRaw=target(columns*4,rows);
            n.irradiance=target(columns,rows);n.neighbors=target(neighborWidth,neighborHeight);n.NativeBaseGI=target(width,height);}
        else{n.coefficients={};n.activeMask={};n.coefficientRaw={};n.irradiance={};n.neighbors={};n.NativeBaseGI={};}
        auto owner=b.Finish();n.packedRays=packed;n.traceParts=parts;n.traceOutputs=outputs;
        n.targetsOwner=std::move(owner);n.targetsUseUe=gi.ueLumenHigh;n.recorded=false;
    }
    // Publish the original ABI handles for source certification and diagnostic
    // readback. Invalidate unused HZB/source handles so switching back to the
    // old prepare must rebuild its own complete target set and ownership.
    if(traceRays[0].color!=n.packedRays[0].color) {
        traceRays=n.packedRays;traceSource={};traceHzb={};traceHzbLevels=0;
        traceResolution=8;traceCompleted={};
    }
    traceTargetsOwner=n.targetsOwner;
    surfaceCacheProbeOrigins=n.origins;surfaceCacheProbeActiveMask=n.activeMask;
    surfaceCacheProbeIrradiance=n.irradiance;surfaceCacheProbeDirectional=n.coefficients[0];
    surfaceCacheProbeOwner=n.targetsOwner;
    n.rayCapacity=std::uint64_t(atlasWidth)*atlasHeight/ScreenProbeTraceTexelsPerRay;
    n.logicalRayBudget=std::uint64_t(columns)*rows*64;
    n.coefficientFilterTexels=std::uint64_t(columns)*4*rows*3;
    if(gi.ueLumenHigh){PrepareUeLumenRadianceCache(gi,placement.trace.sdf.collectRayDiagnostics);PrepareUeLumenHighScreenProbeGather(gi);PrepareUeLumenProbeImportance(gi);PrepareUeLumenHitSceneColor();}
}

// Record after receiver capture/card relighting, before receiver transport.
// SurfaceCacheData must already be uploaded and surfaceCachePending valid.
// NativeBaseGI is receiver radiance (rho * E/pi * indirectIntensity), with the
// original readable/full-N alpha. It does not contain contact receiver rays.
const Target& RecordNativeScreenProbeTransport(RHIFrameEncoder& encoder,const RenderFrame& frame,
    const PipelineCamera& camera,const MaterialPipelineSettings& settings,const SceneEffectsConstants& effects,
    const SurfacePassConstants& surface,const PostProcessConstants& post,std::uint64_t temporalSequence=0,bool angularReuseParity=false,bool historyValid=false,bool cameraCut=false) {
    const auto& gi=settings.surfaceCacheGi;auto& n=nativeProbeTransport;
    const auto traceProgramsOwner=gi.ueLumenHigh?n.ueRadianceCache.programsOwner:n.programsOwner;
    Require(traceProgramsOwner&&n.targetsOwner&&surfaceCachePending.valid,"Native probe transport was not prepared");
    n.recorded=false;n.traceMetadataDiagnostics=false;n.reconstructionComparisonRecorded=false;
    n.baseReconstructionDeferred=gi.fuseNativeReconstruction&&gi.contact.screenContactDetection&&!gi.contact.referenceReceiver;
    Require(encoder.KeepAlive(traceProgramsOwner)&&encoder.KeepAlive(n.targetsOwner)&&
        encoder.KeepAlive(placementProgramsOwner)&&encoder.KeepAlive(placementTargetsOwner)&&
        encoder.KeepAlive(transportPrecisionOwner),"Cannot retain native probe transport resources");
    if(!gi.ueLumenHigh&&gi.nativeAdaptiveFreshSampling)Require(encoder.KeepAlive(n.adaptiveProgramsOwner),"Cannot retain adaptive native receiver programs");
    auto placement=settings.screenProbePlacement;
    if(gi.ueLumenHigh){placement.trace.cosineHemisphere=true;placement.frameIndex=unsigned(ueLumenViewFrame%8u);}
    placement.trace.singlePassOutputs=true;placement.trace.singleInvocation=false;placement.trace.compactSdfRays=false;
    placement.trace.sdf.enabled=false;placement.trace.sdf.visualMode=0;
    if(SurfaceCacheTemporalActive(gi))placement.trace.samplingSeed=(placement.trace.samplingSeed+
        std::uint32_t(gi.temporal.staticConvergence&&gi.temporal.maxFrames>1?
            temporalSequence:temporalSequence%gi.temporal.directionPhases))&0x1fffffffu;
    surfaceCacheSafeOriginsActive=true;
    const SurfaceCacheOriginSafetyConstants safety{glm::vec4(gi.originSafetyFraction,gi.originSafetyEpsilon,gi.rasterSourcePositions?1.f:0.f,gi.ueLumenHigh?1.f:0.f)};
    Upload(encoder,surfaceCacheOriginSafetyBuffer,&safety,sizeof(safety),SurfaceCacheOriginSafetyBinding);
    BindSurfaceCache(encoder);
    Require(encoder.BindTexture(surfaceCacheGeometry,9)&&encoder.BindTexture(transportPrecision,15),"Cannot bind native probe geometry");
    RecordScreenProbePlacement(encoder,frame,camera,placement,effects,surface,post,false,false,true,gi.nativeComputePlacement,gi.collectNativePlacementComparison,gi.contact.collectTraceTimings,gi.ueLumenHigh);
    Require(placementPending.valid&&statistics.surfaceCacheProbeOriginsCollected,"Native probes lack certified origins");
    if(gi.nativeAdaptiveFreshSampling&&angularReuseParity)
        placementFrameConstants.refinement.x=float(int(placementFrameConstants.refinement.x+.5f)|16);
    const auto probeImage=placementPending.image,rayImage=tracePending.image;
    n.probeImage=probeImage;n.rayImage=rayImage;
    if(gi.ueLumenHigh)placementFrameConstants.tracing.w&=~2;
    Upload(encoder,placementBuffer,&placementFrameConstants,sizeof(placementFrameConstants),ScreenProbePlacementBinding);
    if(gi.ueLumenHigh){
        RecordUeLumenRadianceCache(encoder,camera,settings);
        BindUeLumenRadianceCache(encoder);
        BindUeLumenHighSampling(encoder,camera,ueLumenViewFrame,historyValid);
        RecordUeLumenProbeImportance(encoder,gi,camera,ueLumenViewFrame,cameraCut,
            gi.temporal.reset||ueLumenHighGather.motion.info.x==0);
    }
    // Existing timing ids retain their meaning as stage boundaries: screen
    // scope covers origins only; the former fallback scope covers native rays
    // and packing, including the UE closest-HZB producer and screen-color lookup.
    // No SDF field upload, voxelization or marcher is recorded.
    Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::ProbeSdfTrace),"Cannot time native screen-probe rays");
    Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::ProbeSdfKernel),"Cannot time native probe kernel");
    if(gi.ueLumenHigh)RecordUeLumenClosestHzb(encoder);
    for(unsigned slot=0;slot<16;++slot)Require(encoder.BindTexture({},slot),"Cannot unbind native ray output aliases");
    BindTarget(encoder,n.traceOutputs);
    const bool diagnostics=settings.screenProbePlacement.trace.sdf.collectRayDiagnostics;
    if(gi.ueLumenHigh){
        BindUeLumenRadianceCache(encoder);
        BindUeLumenHighSampling(encoder,camera,ueLumenViewFrame,historyValid);
        BindUeLumenProbeImportance(encoder);
        BindUeLumenHitSceneColor(encoder,gi,camera,ueLumenViewFrame,historyValid,cameraCut);
    }
    Require(encoder.BindPipeline(gi.ueLumenHigh?(diagnostics?n.ueRadianceCache.screenTraceDiagnosticProgram:n.ueRadianceCache.screenTraceProgram):(diagnostics?n.traceDiagnosticProgram:n.traceProgram))&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(placementUniform[probeImage].color,4)&&encoder.BindTexture(placementSpawned[probeImage].color,5)&&
        encoder.BindTexture(n.origins.color,10)&&encoder.BindTexture(surfaceCacheGeometry,9)&&encoder.BindTexture(transportPrecision,15)&&
        encoder.BindTexture(surfaceCacheAttributes,12)&&encoder.BindTexture(surfaceCacheMetadata,13)&&encoder.BindTexture(surfaceCacheLighting.color,14)&&
        encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot trace original native screen-probe rays");
    Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::ProbeSdfKernel),"Cannot finish native probe kernel timing");
    Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::ProbeSdfPack),"Cannot time native probe packing");
    BindTarget(encoder,traceRays[rayImage]);
    Require(encoder.BindPipeline(gi.ueLumenHigh?n.ueRadianceCache.screenPackProgram:n.packProgram)&&encoder.BindMesh(fullscreen),"Cannot bind native probe packer");
    for(unsigned part=0;part<n.traceParts.size();++part)
        Require(encoder.BindTexture(n.traceParts[part],part),"Cannot bind native probe tuple part");
    Require(encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot pack native screen-probe records");
    Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::ProbeSdfPack)&&
        encoder.EndGpuTiming(RHICommand::GpuTimingScope::ProbeSdfTrace),"Cannot finish native probe trace timing");
    tracePending={true,rayImage,8,false,false};n.traceMetadataDiagnostics=diagnostics;
    statistics.screenProbeTracePasses+=2;statistics.screenProbeHzbLevels=gi.ueLumenHigh?unsigned(ueHitSceneColor.levels.size())+1u:0u;
    statistics.screenProbeSingleInvocation=false;statistics.screenProbeSinglePassOutputs=true;
    statistics.probeSdfHardwareRayTracing=true;statistics.probeSdfNativeUploadsPruned=true;
    statistics.surfaceCacheNativeScreenProbeTransport=true;statistics.surfaceCacheNativeProbeQueryDiagnostics=diagnostics;
    statistics.surfaceCacheNativeAdaptiveFreshSampling=gi.nativeAdaptiveFreshSampling;
    if(gi.ueLumenHigh)return RecordUeLumenHighScreenProbeGather(encoder,gi,camera,temporalSequence,historyValid,ueLumenViewFrame);

    Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheProbeReconstruction),"Cannot time native probe integration");
    Require(encoder.BindTexture({},10),"Cannot unbind native adaptive occupancy");
    BindTarget(encoder,n.activeMask);
    Require(encoder.BindPipeline(n.activeMaskProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(placementSpawned[probeImage].color,1)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot summarize native adaptive probes");
    Require(encoder.BindTexture({},8),"Cannot unbind native coefficient output");
    BindTarget(encoder,n.coefficientRaw);
    Require(encoder.BindPipeline(n.integrateProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(placementUniform[probeImage].color,0)&&encoder.BindTexture(placementSpawned[probeImage].color,1)&&
        encoder.BindTexture(traceRays[rayImage].color,7)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot integrate native probe coefficients");
    Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheProbeReconstruction),"Cannot finish native probe integration timing");

    const auto& spatial=gi.spatial;SurfaceCacheSpatialConstants filter;
    filter.options={0,int(spatial.halfKernelSize),spatial.visibilityCheck?1:0,1};
    filter.filter={spatial.positionWeightScale,glm::radians(spatial.maxHitAngleDegrees),
        std::cos(glm::radians(spatial.normalThresholdDegrees)),spatial.strength};
    filter.parameters={gi.rayBias,spatial.planeDistanceThreshold,gi.originSafetyEpsilon,spatial.localAdaptiveNeighbors?1.f:0.f};
    Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheProbeSpatial),"Cannot time native coefficient reuse");
    Upload(encoder,n.spatialBuffer,&filter,sizeof(filter),SurfaceCacheSpatialBinding);
    if(gi.contact.collectTraceTimings)Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::NativeProbeNeighbors),"Cannot time native probe neighbors");
    Require(encoder.BindTexture({},5),"Cannot unbind native neighbor output");
    BindTarget(encoder,n.neighbors);
    Require(encoder.BindPipeline(diagnostics?n.neighborsDiagnosticProgram:n.neighborsProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(placementUniform[probeImage].color,3)&&encoder.BindTexture(placementSpawned[probeImage].color,4)&&
        encoder.BindTexture(surfaceCacheGeometry,9)&&encoder.BindTexture(transportPrecision,15)&&encoder.BindTexture(n.origins.color,10)&&
        encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot find native coefficient geometry support");
    statistics.surfaceCacheNativeNeighborQueryDiagnostics=diagnostics;
    if(gi.contact.collectTraceTimings)Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::NativeProbeNeighbors),"Cannot finish native probe neighbor timing");
    if(gi.contact.collectTraceTimings)Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::NativeProbeCoefficientFilter),"Cannot time native coefficient filtering");
    auto coefficientSource=n.coefficientRaw.color;
    for(unsigned pass=0;pass<3;++pass) {
        filter.options.x=int(pass);Upload(encoder,n.spatialBuffer,&filter,sizeof(filter),SurfaceCacheSpatialBinding);
        BindTarget(encoder,n.coefficients[pass%2]);
        Require(encoder.BindPipeline(n.coefficientFilterProgram)&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(coefficientSource,0)&&
            encoder.BindTexture(placementUniform[probeImage].color,3)&&encoder.BindTexture(placementSpawned[probeImage].color,4)&&
            encoder.BindTexture(n.neighbors.color,5)&&encoder.BindTexture(n.origins.color,10)&&
            encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot filter native probe coefficients");
        coefficientSource=n.coefficients[pass%2].color;
    }
    Require(encoder.BindTexture({},7),"Cannot unbind native scalar irradiance output");
    BindTarget(encoder,n.irradiance);
    Require(encoder.BindPipeline(n.extractProgram)&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(coefficientSource,0)&&
        encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot extract native probe scalar irradiance");
    if(gi.contact.collectTraceTimings)Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::NativeProbeCoefficientFilter),"Cannot finish native coefficient timing");
    if(!n.baseReconstructionDeferred){
    if(gi.contact.collectTraceTimings)Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::NativeProbeBaseReconstruction),"Cannot time native base reconstruction");
    // Integration/filtering already retained full-N RGB and the center's raw
    // coverage/support metadata. Reuse the original receiver reconstruction.
    Require(encoder.BindTexture({},0),"Cannot unbind native GI output");
    BindTarget(encoder,n.NativeBaseGI);
    Require(encoder.BindPipeline(n.reconstructProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&encoder.BindTexture(diffuseCapture.color,4)&&
        encoder.BindTexture(placementUniform[probeImage].color,5)&&encoder.BindTexture(placementSpawned[probeImage].color,6)&&
        encoder.BindTexture(n.irradiance.color,7)&&encoder.BindTexture(coefficientSource,8)&&
        encoder.BindTexture(n.activeMask.color,10)&&encoder.BindTexture(n.origins.color,11)&&
        encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot reconstruct native screen-probe base GI");
    if(gi.contact.collectTraceTimings)Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::NativeProbeBaseReconstruction),"Cannot finish native base reconstruction timing");
    }
    Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheProbeSpatial),"Cannot finish native coefficient timing");
    Require(encoder.BindTexture(surfaceCacheGeometry,9)&&encoder.BindTexture(transportPrecision,15),"Cannot restore native receiver geometry");
    surfaceCachePending.probes=true;surfaceCachePending.spatial=false;
    statistics.surfaceCacheDirectionalCollected=gi.directionalReconstruction;
    statistics.surfaceCacheProbeSlots=placementFrameConstants.traceAtlas.w;
    statistics.surfaceCacheProbeRayBudget=n.logicalRayBudget;statistics.surfaceCacheRays+=n.logicalRayBudget;
    statistics.surfaceCacheProbePasses+=2;statistics.surfaceCacheSpatialPasses+=n.baseReconstructionDeferred?5:6;
    statistics.surfaceCachePasses+=n.baseReconstructionDeferred?7:8;
    statistics.surfaceCacheSpatialFilterTexels=n.coefficientFilterTexels;
    statistics.surfaceCacheSpatialRayTargetBytes=std::uint64_t(n.coefficientRaw.width)*n.coefficientRaw.height*3u*4u*sizeof(float);
    statistics.surfaceCacheRadianceOnlyFiltering=false;n.recorded=true;
    return n.NativeBaseGI;
}
