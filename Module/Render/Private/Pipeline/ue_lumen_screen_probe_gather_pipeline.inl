// Included inside MaterialRenderPipeline::Impl. All resources belong to the
// independent ueLumenHighGather state; Native state keeps only trace producers.
void PrepareUeLumenHighScreenProbeGather(const SurfaceCacheGiSettings& gi){
    if(!gi.ueLumenHigh)return;
    auto& g=ueLumenHighGather;const auto& n=nativeProbeTransport;
    Require(gi.nativeScreenProbeTransport&&traceResolution==8,"UE High gather requires the current native 8x8 world-octa trace");
    if(!g.programsOwner){
        PipelineDetail::ResourceBuilder b(device);const auto vertex=FullscreenVertexShader();
        g.neighborsProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::UeLumenHighProbeNeighborsShader());
        g.neighborsDiagnosticProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::UeLumenHighProbeNeighborsShader(true));
        g.spatialProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::UeLumenHighProbeSpatialShader());
        g.irradianceProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::UeLumenHighProbeIrradianceShader());
        g.gatherProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::UeLumenHighProbeGatherShader());
        g.spatialBuffer=b.Uniform(sizeof(SurfaceCacheSpatialConstants));
        g.motionBuffer=b.Uniform(sizeof(PipelineDetail::UeLumenHighProbeMotionConstants));
        g.programsOwner=b.Finish();
    }
    if(!g.computeOwner){
        auto owner=std::shared_ptr<Compute::Device>(Compute::Device::Borrow(device));
        Require(owner->Diagnostics().frameAvailable,"UE High SH projection requires frame compute");
        Compute::FramePipelineDesc d;d.source=SurfaceCacheDetail::UeLumenHighProbeScatterShader();
        d.label="ue-lumen-high-composite-traces";d.uniformMask=(1u<<9)|(1u<<12)|(1u<<13)|(1u<<15);
        d.sampledMask=(1u<<2)|(1u<<3)|(1u<<4)|(1u<<6)|(1u<<7)|(1u<<8)|(1u<<10)|(1u<<13);d.storageImages=3;
        g.scatterProgram=owner->CreateFramePipeline(d);
        d.source=SurfaceCacheDetail::UeLumenHighProbeProjectShader();d.label="ue-lumen-high-probe-sh3-project";
        d.uniformMask=(1u<<9)|(1u<<12)|(1u<<13);
        d.sampledMask=(1u<<0)|(1u<<2)|(1u<<3)|(1u<<4)|(1u<<6)|(1u<<10)|(1u<<11)|(1u<<13);d.storageImages=2;
        g.projectProgram=owner->CreateFramePipeline(d);g.computeOwner=std::move(owner);
    }
    const auto columns=n.origins.width,probeRows=n.origins.height;
    Require(columns&&probeRows&&columns*9<=8192&&probeRows*8<=8192,"UE High irradiance atlas exceeds limits");
    const auto rayWidth=n.packedRays[0].width/4,rayHeight=n.packedRays[0].height;
    if(!g.targetsOwner||g.radiance[0].width!=rayWidth||g.radiance[0].height!=rayHeight||
        g.irradiance.width!=columns*8||g.irradiance.height!=probeRows*8||g.halfGi.width!=(width+1)/2||g.halfGi.height!=(height+1)/2){
        PipelineDetail::ResourceBuilder b(device);
        const auto target=[&](unsigned w,unsigned h,RHITextureFormat f,bool storage=false){
            Target t;t.width=w;t.height=h;t.color=storage?b.StorageTexture(w,h,f):b.Texture(w,h,f);
            if(!storage)t.framebuffer=b.Target(t.color,{});return t;};
        g.radiance={target(rayWidth,rayHeight,RHITextureFormat::RGBA16F),target(rayWidth,rayHeight,RHITextureFormat::RGBA16F)};
        g.scatterRaw=target(rayWidth,rayHeight,RHITextureFormat::RGBA16F,true);
        g.scatterAux=target(rayWidth,rayHeight,RHITextureFormat::RGBA32F,true);
        g.scatterProbeInfo=target(columns,probeRows,RHITextureFormat::RGBA32F,true);
        g.sh=target(columns*9,probeRows,RHITextureFormat::RGBA32F,true);
        g.probeInfo=target(columns,probeRows,RHITextureFormat::RGBA32F,true);
        g.irradiance=target(columns*8,probeRows*8,RHITextureFormat::RGBA16F);

        const auto neighborWidth=std::min(columns*SurfaceCacheSpatialNeighborCount,8192u);
        const auto neighborTexels=std::uint64_t(columns)*probeRows*SurfaceCacheSpatialNeighborCount;
        g.neighbors=target(neighborWidth,unsigned((neighborTexels+neighborWidth-1)/neighborWidth),RHITextureFormat::RGBA32F);
        g.halfGi=target((width+1)/2,(height+1)/2,RHITextureFormat::RGBA16F);
        g.lightIsMoving=target((width+1)/2,(height+1)/2,RHITextureFormat::R8_UNORM);
        std::array<RenderResourceHandle<RHITextureSpec>,4> colors{{g.halfGi.color,g.lightIsMoving.color,{},{}}};
        g.halfGi.framebuffer=b.TargetColors(colors);g.targetsOwner=b.Finish();
    }
    // Old scalar/F4 handles are never published for an early-return UE frame.
    surfaceCacheProbeIrradiance=g.irradiance;surfaceCacheProbeDirectional={};surfaceCacheProbeActiveMask={};
    surfaceCacheProbeOwner=g.targetsOwner;
}
bool SetUeLumenHighProbeMotion(const std::shared_ptr<const SurfaceCacheMotionSnapshot>& current,
    const std::shared_ptr<const SurfaceCacheMotionSnapshot>& previous,bool historyValid){
    auto& g=ueLumenHighGather;g.motion={};g.motionSnapshotPrepared=false;
    g.motionCacheGeneration=g.motionGeometryGeneration=0;
    if(!current)return false;
    g.motionSnapshotPrepared=true;g.motionCacheGeneration=current->cacheGeneration;
    g.motionGeometryGeneration=current->geometryGeneration;
    // An intentionally unknown/uncertified cache still renders valid black.
    // Its snapshot proves only the current generation; no velocity/history
    // mapping is consumed until the owner/transform contract is certified.
    if(!current->certified||current->instances.size()>256)return false;
    g.motion.info.x=int(current->instances.size());
    bool compatible=historyValid&&previous&&previous->certified&&previous->instances.size()==current->instances.size();
    if(compatible)for(std::size_t i=0;i<current->instances.size();++i){
        const auto& a=current->instances[i];const auto& b=previous->instances[i];
        if(a.source!=b.source||a.userId!=b.userId||a.movable!=b.movable){compatible=false;break;}
    }
    for(std::size_t i=0;i<current->instances.size();++i)
        g.motion.previousFromCurrent[i]=compatible?previous->instances[i].transform*glm::inverse(current->instances[i].transform):glm::mat4(1);
    g.motion.info.y=compatible?1:0;return compatible;
}
const Target& RecordUeLumenHighScreenProbeGather(RHIFrameEncoder& encoder,const SurfaceCacheGiSettings& gi,
    const PipelineCamera& camera,std::uint64_t sequence,bool historyValid,std::uint64_t ueViewFrame){
    auto& g=ueLumenHighGather;auto& n=nativeProbeTransport;
    Require(gi.ueLumenHigh&&g.programsOwner&&g.targetsOwner&&g.computeOwner,"UE High gather is not prepared");
    Require(g.motionSnapshotPrepared&&g.motionCacheGeneration==gi.cache->Generation()&&
        g.motionGeometryGeneration==gi.scene->GeometryGeneration(),"UE probe velocity snapshot differs from the current trace geometry");
    Require(encoder.KeepAlive(g.programsOwner)&&encoder.KeepAlive(g.targetsOwner)&&encoder.KeepAlive(g.computeOwner),
        "Cannot retain UE High gather resources");
    const auto probeImage=n.probeImage,rayImage=n.rayImage;
    const bool diagnostics=n.traceMetadataDiagnostics;
    statistics.surfaceCacheNativeNeighborQueryDiagnostics=diagnostics;
    SurfaceCacheSpatialConstants filter;filter.options={0,1,gi.spatial.visibilityCheck?1:0,1};
    filter.filter={1000.0f,glm::radians(10.0f),std::cos(glm::radians(gi.spatial.normalThresholdDegrees)),1.0f};
    filter.parameters={gi.rayBias,gi.spatial.planeDistanceThreshold,gi.originSafetyEpsilon,gi.spatial.localAdaptiveNeighbors?1.f:0.f};
    (void)sequence; // Physical ray jitter follows ViewState, never history age.
    Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheProbeSpatial),"Cannot time UE composite and probe spatial filtering");
    const auto motionBytes=std::uint32_t(offsetof(PipelineDetail::UeLumenHighProbeMotionConstants,previousFromCurrent)+
        std::size_t(g.motion.info.x)*sizeof(glm::mat4));
    Upload(encoder,g.motionBuffer,&g.motion,motionBytes,13);
    BindUeLumenHighSampling(encoder,camera,ueViewFrame,historyValid,6,7);
    Require(encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(placementUniform[probeImage].color,3)&&
        encoder.BindTexture(placementSpawned[probeImage].color,4)&&encoder.BindTexture(n.packedRays[rayImage].color,6)&&
        encoder.BindTexture(n.origins.color,10)&&encoder.BindTexture(surfaceCacheMetadata,13),
        "Cannot bind immutable UE trace / original actual-hit velocity inputs");
    // RayInfo sampler8 and UBO15 are retained from the same native trace.
    RHICommand::DispatchFrameCompute scatter;scatter.pipeline=g.scatterProgram;
    scatter.images[0]=g.scatterRaw.color;scatter.images[1]=g.scatterAux.color;scatter.images[2]=g.scatterProbeInfo.color;
    scatter.groupsX=g.probeInfo.width;scatter.groupsY=g.probeInfo.height;
    Require(encoder.DispatchCompute(scatter),"Cannot scatter UE jittered importance rays to fixed gather bins");
    Upload(encoder,g.spatialBuffer,&filter,sizeof(filter),SurfaceCacheSpatialBinding);
    if(gi.contact.collectTraceTimings)Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::NativeProbeNeighbors),"Cannot time UE probe geometry support");
    Require(encoder.BindTexture({},5),"Cannot unbind UE probe neighbor output");BindTarget(encoder,g.neighbors);
    Require(encoder.BindPipeline(diagnostics?g.neighborsDiagnosticProgram:g.neighborsProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(placementUniform[probeImage].color,3)&&encoder.BindTexture(placementSpawned[probeImage].color,4)&&
        encoder.BindTexture(surfaceCacheGeometry,9)&&encoder.BindTexture(transportPrecision,15)&&encoder.BindTexture(n.origins.color,10)&&
        encoder.BindTexture(g.scatterProbeInfo.color,11)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot prepare UE directional filter geometry");
    if(gi.contact.collectTraceTimings)Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::NativeProbeNeighbors),"Cannot finish UE probe geometry timing");
    if(gi.contact.collectTraceTimings)Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::NativeProbeCoefficientFilter),"Cannot time UE directional radiance filter");
    auto source=g.scatterRaw.color;
    for(unsigned pass=0;pass<3;++pass){
        filter.options.x=int(pass);Upload(encoder,g.spatialBuffer,&filter,sizeof(filter),SurfaceCacheSpatialBinding);
        Require(encoder.BindTexture({},0),"Cannot unbind UE radiance output");BindTarget(encoder,g.radiance[pass%2]);
        Require(encoder.BindPipeline(g.spatialProgram)&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(source,0)&&
            encoder.BindTexture(placementUniform[probeImage].color,3)&&encoder.BindTexture(placementSpawned[probeImage].color,4)&&
            encoder.BindTexture(g.neighbors.color,5)&&encoder.BindTexture(g.scatterAux.color,6)&&
            encoder.BindTexture(n.origins.color,10)&&encoder.BindTexture(g.scatterProbeInfo.color,11)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot filter UE world-octa radiance");
        source=g.radiance[pass%2].color;
    }
    if(gi.contact.collectTraceTimings)Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::NativeProbeCoefficientFilter),"Cannot finish UE directional filter timing");
    Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheProbeSpatial),"Cannot finish UE probe spatial timing");
    Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheProbeReconstruction),"Cannot time UE SH3 irradiance convolution");
    // Upload the matrices of the same immutable current/previous motion snapshots
    // used by GI reprojection; shader values never alias mutable scene instances.
    Require(encoder.BindTexture(g.scatterProbeInfo.color,11)&&encoder.BindTexture(source,0)&&encoder.BindTexture(indirectPositions.color,2)&&
        encoder.BindTexture(placementUniform[probeImage].color,3)&&encoder.BindTexture(placementSpawned[probeImage].color,4)&&
        encoder.BindTexture(n.packedRays[rayImage].color,6)&&encoder.BindTexture(n.origins.color,10)&&encoder.BindTexture(surfaceCacheMetadata,13),
        "Cannot bind UE SH projection and actual-hit velocity inputs");
    RHICommand::DispatchFrameCompute project;project.pipeline=g.projectProgram;
    project.images[0]=g.sh.color;project.images[1]=g.probeInfo.color;project.groupsX=g.probeInfo.width;project.groupsY=g.probeInfo.height;
    Require(encoder.DispatchCompute(project),"Cannot project UE SH3 and actual-hit moving fractions");
    Require(encoder.BindTexture({},7)&&encoder.BindTexture({},0)&&encoder.BindTexture({},1),"Cannot detach UE octahedral irradiance output");
    BindTarget(encoder,g.irradiance);
    Require(encoder.BindPipeline(g.irradianceProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(g.sh.color,0)&&encoder.BindTexture(g.probeInfo.color,1)&&
        encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot convolve UE SH3 to bordered octahedral irradiance");
    Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheProbeReconstruction),"Cannot finish UE irradiance convolution timing");
    if(gi.contact.collectTraceTimings)Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::NativeProbeBaseReconstruction),"Cannot time UE half-resolution stochastic integration");
    // This is the same real BlueNoise / four-rooks source-pixel sequence as AO
    // and GI temporal. Slot 14 no longer contains spatial-filter constants.
    BindTarget(encoder,g.halfGi);
    BindUeLumenHighSampling(encoder,camera,ueViewFrame,historyValid,0,1);
    Require(encoder.BindPipeline(g.gatherProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&
        encoder.BindTexture(placementUniform[probeImage].color,5)&&encoder.BindTexture(placementSpawned[probeImage].color,6)&&
        encoder.BindTexture(g.irradiance.color,7)&&encoder.BindTexture(g.probeInfo.color,8)&&
        encoder.BindTexture(n.origins.color,11)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),
        "Cannot integrate UE octahedral irradiance and actual-hit LightIsMoving");
    if(gi.contact.collectTraceTimings)Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::NativeProbeBaseReconstruction),"Cannot finish UE stochastic integration timing");
    Require(encoder.BindTexture(surfaceCacheGeometry,9)&&encoder.BindTexture(transportPrecision,15),"Cannot restore receiver geometry after UE gather");
    surfaceCachePending.probes=true;surfaceCachePending.spatial=false;
    statistics.surfaceCacheDirectionalCollected=false;
    statistics.surfaceCacheProbeSlots=placementFrameConstants.traceAtlas.w;
    statistics.surfaceCacheProbeRayBudget=n.logicalRayBudget;statistics.surfaceCacheRays+=n.logicalRayBudget;
    statistics.surfaceCacheProbePasses+=2;statistics.surfaceCacheSpatialPasses+=8;statistics.surfaceCachePasses+=10;
    statistics.surfaceCacheSpatialFilterTexels=std::uint64_t(g.radiance[0].width)*g.radiance[0].height*3;
    statistics.surfaceCacheSpatialRayTargetBytes=std::uint64_t(g.radiance[0].width)*g.radiance[0].height*(3*8+16);
    statistics.surfaceCacheRadianceOnlyFiltering=true;n.recorded=true;n.baseReconstructionDeferred=false;
    return g.halfGi;
}
