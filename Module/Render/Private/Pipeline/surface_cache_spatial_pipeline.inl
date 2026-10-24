// Included inside MaterialRenderPipeline::Impl. Filter directional probe
// radiance before integration, receiver albedo and temporal reuse.
bool SurfaceCacheSpatialActive(const SurfaceCacheGiSettings& s) const {
    return s.enabled&&!s.nativeScreenProbeTransport&&s.useScreenProbes&&s.spatial.enabled&&s.spatial.strength>0&&!(s.view==2&&s.reconstructionDebug!=0)&&
        (s.view==0||s.view==2||(s.view==5&&s.temporal.enabled));
}
void PrepareSurfaceCacheSpatial(const SurfaceCacheGiSettings& s) {
    if(!SurfaceCacheSpatialActive(s))return;
    if(!surfaceCacheSpatialProgramsOwner) {
        PipelineDetail::ResourceBuilder b(device);const auto vertex=FullscreenVertexShader();
        surfaceCacheSpatialNeighborsProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::SpatialNeighborsShader());
        surfaceCacheSpatialFilterProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::SpatialFilterShader());
        surfaceCacheSpatialBuffer=b.Uniform(sizeof(SurfaceCacheSpatialConstants));surfaceCacheSpatialProgramsOwner=b.Finish();
    }
    const bool radianceOnly=s.spatial.radianceOnlyFiltering&&traceRays[0].width%4==0;
    if(radianceOnly&&!surfaceCacheSpatialRadianceProgramsOwner) {
        PipelineDetail::ResourceBuilder b(device);const auto vertex=FullscreenVertexShader();
        surfaceCacheSpatialRadianceFilterProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::SpatialFilterShader(true));
        surfaceCacheSpatialRadianceIntegrateProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ProbeIntegrateShader(true));
        surfaceCacheSpatialRadianceDirectionalProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ProbeDirectionalIntegrateShader(true));
        surfaceCacheSpatialRadianceProgramsOwner=b.Finish();
    }
    const auto rw=traceRays[0].width/(radianceOnly?4u:1u),rh=traceRays[0].height;
    const auto iw=surfaceCacheProbeIrradiance.width,ih=surfaceCacheProbeIrradiance.height;
    const auto nw=std::min(iw*SurfaceCacheSpatialNeighborCount,8192u);
    const auto neighborTexels=std::size_t(iw)*ih*SurfaceCacheSpatialNeighborCount;
    const auto nh=unsigned((neighborTexels+nw-1)/nw);
    if(!surfaceCacheSpatialOwner||surfaceCacheSpatialRays[0].width!=rw||surfaceCacheSpatialRays[0].height!=rh||
        surfaceCacheSpatialIrradiance.width!=iw||surfaceCacheSpatialIrradiance.height!=ih||
        surfaceCacheSpatialGi.width!=width||surfaceCacheSpatialGi.height!=height||
        surfaceCacheSpatialNeighbors.width!=nw||surfaceCacheSpatialNeighbors.height!=nh) {
        PipelineDetail::ResourceBuilder b(device);
        const auto target=[&](unsigned w,unsigned h){Target t;t.width=w;t.height=h;
            t.color=b.Texture(w,h,RHITextureFormat::RGBA32F);t.framebuffer=b.Target(t.color,{});return t;};
        std::array<Target,2> rays{target(rw,rh),target(rw,rh)};
        auto neighbors=target(nw,nh),irradiance=target(iw,ih),image=target(width,height);
        auto owner=b.Finish();surfaceCacheSpatialRays=rays;surfaceCacheSpatialNeighbors=neighbors;
        surfaceCacheSpatialIrradiance=irradiance;surfaceCacheSpatialGi=image;surfaceCacheSpatialOwner=std::move(owner);
    }
}
void RecordSurfaceCacheSpatial(RHIFrameEncoder& encoder,const SurfaceCacheGiSettings& gi) {
    const auto& s=gi.spatial;const auto probeImage=placementPending.image;
    const bool radianceOnly=s.radianceOnlyFiltering&&traceRays[0].width%4==0;
    const bool safeOrigins=statistics.surfaceCacheProbeOriginsCollected;
    SurfaceCacheSpatialConstants c;c.options={0,int(s.halfKernelSize),s.visibilityCheck?1:0,safeOrigins?1:0};
    c.filter={s.positionWeightScale,glm::radians(s.maxHitAngleDegrees),std::cos(glm::radians(s.normalThresholdDegrees)),s.strength};
    c.parameters={gi.rayBias,s.planeDistanceThreshold,gi.originSafetyEpsilon,s.localAdaptiveNeighbors?1.f:0.f};
    Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheProbeSpatial),"Cannot begin probe spatial GPU timing");
    Upload(encoder,surfaceCacheSpatialBuffer,&c,sizeof(c),SurfaceCacheSpatialBinding);
    Require(encoder.BindTexture({},5),"Cannot unbind spatial neighbor output");
    BindTarget(encoder,surfaceCacheSpatialNeighbors);
    Require(encoder.BindPipeline(surfaceCacheSpatialNeighborsProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(placementUniform[probeImage].color,3)&&encoder.BindTexture(placementSpawned[probeImage].color,4)&&
        encoder.BindTexture(surfaceCacheGeometry,9)&&
        encoder.BindTexture(safeOrigins?surfaceCacheProbeOrigins.color:RenderResourceHandle<RHITextureSpec>{},10)&&
        encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot find visible spatial probe neighbors");
    const auto metadata=sdfRays[tracePending.image].color;
    auto source=metadata;
    for(unsigned pass=0;pass<s.passes;++pass) {
        const auto image=pass%2;c.options.x=int(pass);
        Upload(encoder,surfaceCacheSpatialBuffer,&c,sizeof(c),SurfaceCacheSpatialBinding);
        BindTarget(encoder,surfaceCacheSpatialRays[image]);
        Require(encoder.BindPipeline(radianceOnly?surfaceCacheSpatialRadianceFilterProgram:surfaceCacheSpatialFilterProgram)&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(source,0)&&
            encoder.BindTexture(radianceOnly?metadata:RenderResourceHandle<RHITextureSpec>{},6)&&
            encoder.BindTexture(placementUniform[probeImage].color,3)&&encoder.BindTexture(placementSpawned[probeImage].color,4)&&
            encoder.BindTexture(surfaceCacheSpatialNeighbors.color,5)&&
            encoder.BindTexture(safeOrigins?surfaceCacheProbeOrigins.color:RenderResourceHandle<RHITextureSpec>{},10)&&
            encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot filter directional probe radiance");
        source=surfaceCacheSpatialRays[image].color;
    }
    BindTarget(encoder,surfaceCacheSpatialIrradiance);
    Require(encoder.BindPipeline(radianceOnly?surfaceCacheSpatialRadianceIntegrateProgram:surfaceCacheProbeIntegrateProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(placementUniform[probeImage].color,0)&&encoder.BindTexture(placementSpawned[probeImage].color,1)&&
        encoder.BindTexture(source,7)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot integrate spatial probe radiance");
    BindTarget(encoder,surfaceCacheSpatialGi);
    const bool directionalData=gi.directionalReconstruction||(int(placementFrameConstants.refinement.x+.5f)&8)!=0;
    if(directionalData) {
        Require(encoder.BindTexture({},8),"Cannot unbind spatial directional probe output");
        BindTarget(encoder,surfaceCacheProbeDirectional);
        Require(encoder.BindPipeline(radianceOnly?surfaceCacheSpatialRadianceDirectionalProgram:surfaceCacheProbeDirectionalProgram)&&encoder.BindMesh(fullscreen)&&
            encoder.BindTexture(placementUniform[probeImage].color,0)&&encoder.BindTexture(placementSpawned[probeImage].color,1)&&
            encoder.BindTexture(radianceOnly?metadata:RenderResourceHandle<RHITextureSpec>{},6)&&
            encoder.BindTexture(source,7)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot integrate spatial directional irradiance");
        BindTarget(encoder,surfaceCacheSpatialGi);
    }
    Require(encoder.BindPipeline(surfaceCacheProbeReconstructProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&encoder.BindTexture(diffuseCapture.color,4)&&
        encoder.BindTexture(placementUniform[probeImage].color,5)&&encoder.BindTexture(placementSpawned[probeImage].color,6)&&
        encoder.BindTexture(surfaceCacheSpatialIrradiance.color,7)&&encoder.BindTexture(surfaceCacheProbeDirectional.color,8)&&encoder.BindTexture(surfaceCacheProbeActiveMask.color,10)&&
        encoder.BindTexture(safeOrigins?surfaceCacheProbeOrigins.color:RenderResourceHandle<RHITextureSpec>{},11)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot reconstruct spatial GI");
    Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheProbeSpatial),"Cannot end probe spatial GPU timing");
    surfaceCachePending.spatial=true;surfaceCachePending.spatialImage=(s.passes-1)%2;surfaceCachePending.spatialRadianceOnly=radianceOnly;
    statistics.surfaceCacheRadianceOnlyFiltering=radianceOnly;
    const auto filterTexels=std::uint64_t(surfaceCacheSpatialRays[0].width)*surfaceCacheSpatialRays[0].height;
    statistics.surfaceCacheSpatialFilterTexels=filterTexels*s.passes;
    statistics.surfaceCacheSpatialRayTargetBytes=filterTexels*2u*4u*sizeof(float);
    statistics.surfaceCacheSpatialPasses=1+s.passes+2+unsigned(directionalData);statistics.surfaceCachePasses+=statistics.surfaceCacheSpatialPasses;
}
