// Included inside MaterialRenderPipeline::Impl. Final gather histories have
// their own completion/sequence state; TAA can remain entirely disabled.
bool SurfaceCacheTemporalActive(const SurfaceCacheGiSettings& s) const {
    return s.enabled&&s.useScreenProbes&&s.temporal.enabled&&(s.view==0||s.view==2||s.view==5);
}
void PrepareSurfaceCacheTemporal(const SurfaceCacheGiSettings& s) {
    if(!SurfaceCacheTemporalActive(s))return;
    if(!surfaceCacheTemporalProgramsOwner) {
        PipelineDetail::ResourceBuilder b(device);const auto vertex=FullscreenVertexShader();
        surfaceCacheTemporalProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::TemporalShader());
        surfaceCacheTemporalPositionProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::TemporalGeometryShader(false));
        surfaceCacheTemporalNormalProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::TemporalGeometryShader(true));
        surfaceCacheTemporalDisplayProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::TemporalDisplayShader());
        surfaceCacheTemporalCountProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::TemporalDisplayShader(true));
        surfaceCacheTemporalBuffer=b.Uniform(sizeof(SurfaceCacheTemporalConstants));surfaceCacheTemporalProgramsOwner=b.Finish();
    }
    if(!surfaceCacheTemporalOwner||surfaceCacheTemporalGi.width!=width||surfaceCacheTemporalGi.height!=height) {
        PipelineDetail::ResourceBuilder b(device);
        const auto target=[&](){Target t;t.width=width;t.height=height;
            t.color=b.Texture(width,height,RHITextureFormat::RGBA32F);t.framebuffer=b.Target(t.color,{});return t;};
        std::array<Target,2> history,positions,normals;
        for(unsigned i=0;i<2;++i){history[i]=target();positions[i]=target();normals[i]=target();}
        auto filtered=target();auto owner=b.Finish();
        surfaceCacheTemporalHistory=history;surfaceCacheTemporalPositions=positions;surfaceCacheTemporalNormals=normals;
        surfaceCacheTemporalGi=filtered;surfaceCacheTemporalOwner=std::move(owner);surfaceCacheTemporalCompleted={};
    }
}
std::uint64_t SurfaceCacheHistoryKey(const RenderFrame& frame,const MaterialPipelineSettings& settings) const {
    // The general content key includes material bytes and raster geometry.
    // Omit diagnostic probe options and camera-driven GDF scrolling; neither
    // changes the underlying scene. Add the actual gather algorithm below.
    auto contentSettings=settings;contentSettings.screenProbePlacement.enabled=false;
    auto key=ContentHash(frame,contentSettings);
    const auto add=[&](const auto& value){const auto* p=reinterpret_cast<const unsigned char*>(&value);
        for(std::size_t i=0;i<sizeof(value);++i){key^=p[i];key*=1099511628211ull;}};
    const auto& sc=settings.surfaceCacheGi;const auto& p=settings.screenProbePlacement;const auto& t=p.trace;
    add(sc.cache.get());add(sc.scene.get());add(sc.cache->Generation());add(sc.scene->GeometryGeneration());add(sc.scene->MeshGeneration());
    add(settings.lighting.lightPositions);add(settings.lighting.lightColors);
    // The generic TAA key intentionally ignores spotlight pose for its own
    // reactive filter. This GI stage has no lighting velocity mask yet.
    const auto spot=MakeSpotLightConstants(settings.spotLight);add(spot);
    add(sc.useScreenProbes);add(sc.maxDistance);add(sc.rayBias);add(sc.intensity);
    add(settings.temporal.antialiasing);
    add(p.adaptive);add(p.freeze);add(p.baseSpacing);add(p.candidateCount);add(p.relativeDepthThreshold);add(p.frameIndex);
    add(t.rayResolution);add(t.samplingSeed);add(t.maxSteps);add(t.maxMip);add(t.thickness);add(t.relativeThickness);
    add(t.sdf.useMesh);add(t.sdf.useGlobal);add(t.sdf.meshTraceDistance);add(t.sdf.hitEpsilon);add(t.sdf.maxSteps);
    const auto& s=sc.temporal;add(s.maxFrames);add(s.directionPhases);add(s.distanceThreshold);add(s.normalThresholdDegrees);
    add(s.movingMaxFrames);add(s.motionThresholdPixels);add(s.neighborhoodClamp);add(s.clampSigma);
    return key;
}
void RecordSurfaceCacheTemporal(RHIFrameEncoder& encoder,const SurfaceCacheGiSettings& gi,const PipelineCamera& camera,
    std::uint64_t key,std::uint64_t sequence,bool historyValid) {
    const auto& s=gi.temporal;const auto& previous=surfaceCacheTemporalCompleted;
    const auto read=previous.image,write=1-read;
    SurfaceCacheTemporalConstants c;c.currentInverseView=glm::inverse(camera.view);
    c.previousViewProjection=previous.camera.projection*previous.camera.view;c.previousView=previous.camera.view;
    c.screen={int(width),int(height),historyValid?1:0,int(s.maxFrames)};
    c.reject={s.distanceThreshold,std::cos(glm::radians(s.normalThresholdDegrees)),s.movingMaxFrames,s.motionThresholdPixels};
    c.clamp={s.clampSigma,s.neighborhoodClamp?1.f:0.f,.002f,0};
    Upload(encoder,surfaceCacheTemporalBuffer,&c,sizeof(c),SurfaceCacheTemporalBinding);
    // A reset/discard still writes the other image. Never destroy the last
    // completed history until this frame has been submitted successfully.
    Require(encoder.BindTexture({},3)&&encoder.BindTexture({},4)&&encoder.BindTexture({},5),"Cannot unbind GI history targets");
    BindTarget(encoder,surfaceCacheTemporalHistory[write]);
    Require(encoder.BindPipeline(surfaceCacheTemporalProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(surfaceCacheRaw.color,0)&&encoder.BindTexture(indirectPositions.color,1)&&encoder.BindTexture(normalCapture.color,2)&&
        encoder.BindTexture(surfaceCacheTemporalHistory[read].color,3)&&encoder.BindTexture(surfaceCacheTemporalPositions[read].color,4)&&
        encoder.BindTexture(surfaceCacheTemporalNormals[read].color,5)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot accumulate GI history");
    const auto saveGeometry=[&](const Target& target,RenderResourceHandle<PipelineSpec> program) {
        BindTarget(encoder,target);Require(encoder.BindPipeline(program)&&encoder.BindMesh(fullscreen)&&
            encoder.BindTexture(surfaceCacheRaw.color,0)&&encoder.BindTexture(indirectPositions.color,1)&&encoder.BindTexture(normalCapture.color,2)&&
            encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot save GI history geometry");
    };
    saveGeometry(surfaceCacheTemporalPositions[write],surfaceCacheTemporalPositionProgram);
    saveGeometry(surfaceCacheTemporalNormals[write],surfaceCacheTemporalNormalProgram);
    BindTarget(encoder,surfaceCacheTemporalGi);
    Require(encoder.BindPipeline(surfaceCacheTemporalDisplayProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(surfaceCacheTemporalHistory[write].color,0)&&encoder.BindTexture(surfaceCacheRaw.color,1)&&
        encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot resolve GI temporal radiance");
    surfaceCacheTemporalPending={true,write,key,sequence+1,camera};
    statistics.surfaceCacheTemporalPasses=4;statistics.surfaceCacheHistoryUsed=historyValid;
    statistics.surfaceCachePasses+=4;
}
