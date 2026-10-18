// Included inside MaterialRenderPipeline::Impl. Filter directional probe
// radiance before integration, receiver albedo and temporal reuse.
bool SurfaceCacheSpatialActive(const SurfaceCacheGiSettings& s) const {
    return s.enabled&&s.useScreenProbes&&s.spatial.enabled&&s.spatial.strength>0&&!(s.view==2&&s.reconstructionDebug!=0)&&
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
    const auto rw=traceRays[0].width,rh=traceRays[0].height;
    const auto iw=surfaceCacheProbeIrradiance.width,ih=surfaceCacheProbeIrradiance.height;
    if(!surfaceCacheSpatialOwner||surfaceCacheSpatialRays[0].width!=rw||surfaceCacheSpatialRays[0].height!=rh||
        surfaceCacheSpatialIrradiance.width!=iw||surfaceCacheSpatialIrradiance.height!=ih||
        surfaceCacheSpatialGi.width!=width||surfaceCacheSpatialGi.height!=height) {
        PipelineDetail::ResourceBuilder b(device);
        const auto target=[&](unsigned w,unsigned h){Target t;t.width=w;t.height=h;
            t.color=b.Texture(w,h,RHITextureFormat::RGBA32F);t.framebuffer=b.Target(t.color,{});return t;};
        std::array<Target,2> rays{target(rw,rh),target(rw,rh)};
        auto neighbors=target(iw*8,ih),irradiance=target(iw,ih),image=target(width,height);
        auto owner=b.Finish();surfaceCacheSpatialRays=rays;surfaceCacheSpatialNeighbors=neighbors;
        surfaceCacheSpatialIrradiance=irradiance;surfaceCacheSpatialGi=image;surfaceCacheSpatialOwner=std::move(owner);
    }
}
void RecordSurfaceCacheSpatial(RHIFrameEncoder& encoder,const SurfaceCacheGiSettings& gi) {
    const auto& s=gi.spatial;const auto probeImage=placementPending.image;
    SurfaceCacheSpatialConstants c;c.options={0,int(s.halfKernelSize),s.visibilityCheck?1:0,0};
    c.filter={s.positionWeightScale,glm::radians(s.maxHitAngleDegrees),std::cos(glm::radians(s.normalThresholdDegrees)),s.strength};
    c.parameters={gi.rayBias,s.planeDistanceThreshold,0,0};
    Upload(encoder,surfaceCacheSpatialBuffer,&c,sizeof(c),SurfaceCacheSpatialBinding);
    Require(encoder.BindTexture({},5),"Cannot unbind spatial neighbor output");
    BindTarget(encoder,surfaceCacheSpatialNeighbors);
    Require(encoder.BindPipeline(surfaceCacheSpatialNeighborsProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(placementUniform[probeImage].color,3)&&encoder.BindTexture(placementSpawned[probeImage].color,4)&&
        encoder.BindTexture(surfaceCacheGeometry,9)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot find visible spatial probe neighbors");
    auto source=sdfRays[tracePending.image].color;
    for(unsigned pass=0;pass<s.passes;++pass) {
        const auto image=pass%2;c.options.x=int(pass);
        Upload(encoder,surfaceCacheSpatialBuffer,&c,sizeof(c),SurfaceCacheSpatialBinding);
        BindTarget(encoder,surfaceCacheSpatialRays[image]);
        Require(encoder.BindPipeline(surfaceCacheSpatialFilterProgram)&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(source,0)&&
            encoder.BindTexture(placementUniform[probeImage].color,3)&&encoder.BindTexture(placementSpawned[probeImage].color,4)&&
            encoder.BindTexture(surfaceCacheSpatialNeighbors.color,5)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot filter directional probe radiance");
        source=surfaceCacheSpatialRays[image].color;
    }
    BindTarget(encoder,surfaceCacheSpatialIrradiance);
    Require(encoder.BindPipeline(surfaceCacheProbeIntegrateProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(placementUniform[probeImage].color,0)&&encoder.BindTexture(placementSpawned[probeImage].color,1)&&
        encoder.BindTexture(source,7)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot integrate spatial probe radiance");
    BindTarget(encoder,surfaceCacheSpatialGi);
    Require(encoder.BindPipeline(surfaceCacheProbeReconstructProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&encoder.BindTexture(diffuseCapture.color,4)&&
        encoder.BindTexture(placementUniform[probeImage].color,5)&&encoder.BindTexture(placementSpawned[probeImage].color,6)&&
        encoder.BindTexture(surfaceCacheSpatialIrradiance.color,7)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot reconstruct spatial GI");
    surfaceCachePending.spatial=true;surfaceCachePending.spatialImage=(s.passes-1)%2;
    statistics.surfaceCacheSpatialPasses=1+s.passes+2;statistics.surfaceCachePasses+=statistics.surfaceCacheSpatialPasses;
}
