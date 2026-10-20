// Included inside MaterialRenderPipeline::Impl. Final gather histories have
// their own completion/sequence state; TAA can remain entirely disabled.
bool SurfaceCacheTemporalActive(const SurfaceCacheGiSettings& s) const {
    return s.enabled&&s.useScreenProbes&&s.temporal.enabled&&!(s.view==2&&s.reconstructionDebug!=0)&&
        (s.view==0||s.view==2||s.view==5);
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
std::uint64_t SurfaceCacheHistoryKey(const RenderFrame& frame,const MaterialPipelineSettings& settings,bool motionCompatible=false) const {
    // The general content key includes material bytes and raster geometry.
    // Omit diagnostic probe options and camera-driven GDF scrolling; neither
    // changes the underlying scene. Add the actual gather algorithm below.
    auto contentSettings=settings;contentSettings.screenProbePlacement.enabled=false;
    contentSettings.surfaceCacheGi.probeOriginDiagnostics=false;
    auto key=ContentHash(frame,contentSettings,!motionCompatible);
    const auto add=[&](const auto& value){const auto* p=reinterpret_cast<const unsigned char*>(&value);
        for(std::size_t i=0;i<sizeof(value);++i){key^=p[i];key*=1099511628211ull;}};
    const auto& sc=settings.surfaceCacheGi;const auto& p=settings.screenProbePlacement;const auto& t=p.trace;
    add(sc.cache.get());add(sc.scene.get());add(sc.scene->MeshGeneration());
    if(!motionCompatible) {add(sc.cache->Generation());add(sc.scene->GeometryGeneration());}
    else {
        // Card generations include transforms, but transport material edits
        // must still invalidate the compatible key even when a raster packet
        // does not expose the cache's independent albedo/emission values.
        add(sc.cache->Settings().pageResolution);add(sc.cache->CardCount());add(sc.cache->TriangleCount());
        const auto& metadata=sc.cache->Metadata();
        for(std::uint32_t card=0;card<sc.cache->CardCount();++card) {
            const auto offset=std::size_t(card)*SurfaceCacheCardTexels;
            if(offset+8>=metadata.size()) {add(metadata.size());break;}
            add(glm::vec3(metadata[offset+7]));add(glm::vec3(metadata[offset+8]));
        }
    }
    add(settings.lighting.lightPositions);add(settings.lighting.lightColors);
    // The generic TAA key intentionally ignores spotlight pose for its own
    // reactive filter. Only certified object motion uses the GI reactive path;
    // light edits, including spotlight pose, still require a full reset.
    const auto spot=MakeSpotLightConstants(settings.spotLight);add(spot);
    add(sc.useScreenProbes);add(sc.cacheBilinear);add(sc.directionalReconstruction);add(sc.reconstructionDebug);add(sc.maxDistance);add(sc.rayBias);add(sc.intensity);
    add(settings.temporal.antialiasing);
    add(p.adaptive);add(p.freeze);add(p.baseSpacing);add(p.candidateCount);add(p.relativeDepthThreshold);add(p.frameIndex);
    add(p.cornerRefinement);add(p.cornerDetectionRadius);add(p.cornerProbeSupport);add(p.cornerNormalAngleDegrees);
    add(p.reconstructionVisibility);
    add(p.continuousInterpolation);
    add(p.curvatureAwareWeights);
    add(p.curvatureRefinement);add(p.curvatureNormalAngleDegrees);add(p.curvaturePlaneErrorPixels);
    add(sc.safeProbeOrigins);add(sc.rasterSourcePositions);add(sc.originSafetyFraction);add(sc.originSafetyEpsilon);
    add(t.rayResolution);add(t.samplingSeed);add(t.maxSteps);add(t.maxMip);add(t.thickness);add(t.relativeThickness);
    add(t.normalBias);add(t.maxDistance);
    add(t.sdf.useMesh);add(t.sdf.useGlobal);add(t.sdf.meshTraceDistance);add(t.sdf.hitEpsilon);add(t.sdf.maxSteps);
    const auto& s=sc.temporal;add(s.maxFrames);add(s.directionPhases);add(s.staticConvergence);add(s.distanceThreshold);add(s.normalThresholdDegrees);
    add(s.movingMaxFrames);add(s.motionThresholdPixels);add(s.neighborhoodClamp);add(s.clampSigma);
    add(s.edgeProtection);add(s.edgeMaxFrames);add(s.clampSlowMotion);
    add(s.youngFilter);add(s.youngFilterRadius);add(s.youngFilterMaxFrames);add(s.youngFilterStrength);
    add(s.retainHistoryOnObjectMotion);add(s.dynamicMaxFrames);add(s.dynamicClampSigma);add(s.dynamicSettleFrames);
    const auto& f=sc.spatial;add(f.enabled);add(f.visibilityCheck);add(f.passes);add(f.halfKernelSize);add(f.strength);
    add(f.positionWeightScale);add(f.maxHitAngleDegrees);add(f.normalThresholdDegrees);add(f.planeDistanceThreshold);add(f.localAdaptiveNeighbors);
    const auto& contact=sc.contact;add(contact.enabled);add(contact.raysPerPixel);add(contact.maxDistance);
    add(contact.parallaxAngleDegrees);add(contact.weightThreshold);
    add(contact.filterRadius);add(contact.filterStrength);add(contact.layerValidation);
    add(contact.filterPasses);add(contact.temporalDirections);
    add(contact.coverageWeightedFilter);
    add(contact.geometryGuidedFilter);add(contact.referenceReceiver);add(contact.guideMaxRadius);add(contact.guideHitAngleDegrees);
    add(contact.screenContactDetection);
    add(contact.coherentProbeRisk);
    return key;
}
std::shared_ptr<const SurfaceCacheMotionSnapshot> CaptureSurfaceCacheMotionSnapshot(
    const RenderFrame& frame,const SurfaceCacheGiSettings& settings) const {
    auto snapshot=std::make_shared<SurfaceCacheMotionSnapshot>();
    snapshot->cacheGeneration=settings.cache->Generation();snapshot->geometryGeneration=settings.scene->GeometryGeneration();
    const auto& instances=settings.scene->Instances();const auto& metadata=settings.cache->Metadata();
    snapshot->certified=!instances.empty()&&settings.cache->CardCount()==instances.size()*6&&
        metadata.size()>=instances.size()*6*SurfaceCacheCardTexels;
    snapshot->instances.reserve(instances.size());
    for(std::size_t index=0;index<instances.size();++index) {
        const auto& instance=instances[index];const auto source=instance.geometryMesh?instance.geometryMesh:instance.mesh;
        snapshot->instances.push_back({source,instance.localToWorld,instance.userId,instance.movable});
        if(!source||source->Triangles().empty()||!Finite(instance.localToWorld)) {snapshot->certified=false;continue;}
        // A matching raster transform alone is insufficient if the caller
        // forgot to update the cache. Validate the card transform as well.
        const auto inverse=glm::inverse(instance.localToWorld);
        if(!Finite(inverse)) {snapshot->certified=false;continue;}
        if(std::size_t(index*6)*SurfaceCacheCardTexels+3>=metadata.size()) {snapshot->certified=false;continue;}
        for(unsigned direction=0;direction<6;++direction) {
            const auto offset=(index*6+direction)*SurfaceCacheCardTexels;
            if(offset+3>=metadata.size()) {snapshot->certified=false;break;}
            for(unsigned column=0;column<4;++column)if(metadata[offset+column]!=inverse[column])snapshot->certified=false;
        }
    }
    // The independent Surface Cache studies extract one opaque raster item
    // per SDF instance in the same order. Unknown extraction mappings retain
    // the original full-reset behavior rather than guessing an object owner.
    std::size_t rasterIndex=0;
    for(const auto& item:frame.items) {
        if(item.layer==RenderLayer::Overlay)continue;
        if(rasterIndex>=instances.size()||item.EffectiveLayer()!=RenderLayer::Opaque||
            item.model!=instances[rasterIndex].localToWorld)snapshot->certified=false;
        ++rasterIndex;
    }
    if(rasterIndex!=instances.size())snapshot->certified=false;
    return snapshot;
}
SurfaceCacheTemporalMotionState ClassifySurfaceCacheTemporalMotion(const RenderFrame& frame,
    const MaterialPipelineSettings& settings,std::uint64_t fullKey,bool historyAllowed) const {
    SurfaceCacheTemporalMotionState result;const auto& previous=surfaceCacheTemporalCompleted;
    const auto& temporal=settings.surfaceCacheGi.temporal;
    if(historyAllowed&&previous.key==fullKey) {
        // The common static/camera-motion path reuses an immutable value
        // snapshot: no second content hash or per-instance allocation/copy.
        result.compatible=true;result.snapshot=previous.motionSnapshot;result.stableKey=previous.stableKey;
        if(temporal.retainHistoryOnObjectMotion&&previous.dynamicSettleRemaining>0) {
            result.dynamicLighting=true;result.settleRemaining=previous.dynamicSettleRemaining-1;
        }
        return result;
    }
    if(!temporal.retainHistoryOnObjectMotion)return result;
    result.stableKey=SurfaceCacheHistoryKey(frame,settings,true);
    result.snapshot=CaptureSurfaceCacheMotionSnapshot(frame,settings.surfaceCacheGi);
    if(!historyAllowed||!previous.motionSnapshot||result.stableKey!=previous.stableKey)return result;
    const auto& old=*previous.motionSnapshot;const auto& current=*result.snapshot;
    if(!old.certified||!current.certified||old.instances.size()!=current.instances.size())return result;
    bool moved=false;
    for(std::size_t index=0;index<current.instances.size();++index) {
        const auto& before=old.instances[index];const auto& after=current.instances[index];
        if(before.source!=after.source||before.userId!=after.userId||before.movable!=after.movable)return result;
        if(before.transform!=after.transform) {
            if(!after.movable)return result;
            moved=true;
        }
    }
    // Unknown generation-only edits cannot be certified as motion. Require
    // coherent SDF and card updates for the copied transform changes.
    if(!moved||current.geometryGeneration<=old.geometryGeneration||current.cacheGeneration<=old.cacheGeneration)return result;
    result.compatible=true;result.dynamicLighting=true;result.settleRemaining=temporal.dynamicSettleFrames;
    return result;
}
void RecordSurfaceCacheTemporal(RHIFrameEncoder& encoder,const SurfaceCacheGiSettings& gi,const PipelineCamera& camera,
    std::uint64_t key,std::uint64_t sequence,bool historyValid,const Target& signal,bool dynamicLighting,bool stationary) {
    const auto& s=gi.temporal;const auto& previous=surfaceCacheTemporalCompleted;
    const auto read=previous.image,write=1-read;
    SurfaceCacheTemporalConstants c;c.currentInverseView=glm::inverse(camera.view);
    c.previousViewProjection=previous.camera.projection*previous.camera.view;c.previousView=previous.camera.view;
    c.screen={int(width),int(height),historyValid?1:0,int(stationary?std::max(s.maxFrames,64u):s.maxFrames)};
    c.reject={s.distanceThreshold,std::cos(glm::radians(s.normalThresholdDegrees)),s.movingMaxFrames,s.motionThresholdPixels};
    c.clamp={s.clampSigma,s.neighborhoodClamp?(s.clampSlowMotion?2.f:1.f):0.f,.002f,s.edgeProtection?s.edgeMaxFrames:0.f};
    c.recovery={s.youngFilter?s.youngFilterStrength:0.f,float(s.youngFilterRadius),s.youngFilterMaxFrames,.08f};
    c.dynamic={dynamicLighting?1.f:0.f,s.dynamicMaxFrames,s.dynamicClampSigma,
        gi.contact.enabled&&gi.contact.coherentProbeRisk?1.f:0.f};
    Upload(encoder,surfaceCacheTemporalBuffer,&c,sizeof(c),SurfaceCacheTemporalBinding);
    // A reset/discard still writes the other image. Never destroy the last
    // completed history until this frame has been submitted successfully.
    Require(encoder.BindTexture({},3)&&encoder.BindTexture({},4)&&encoder.BindTexture({},5),"Cannot unbind GI history targets");
    BindTarget(encoder,surfaceCacheTemporalHistory[write]);
    Require(encoder.BindPipeline(surfaceCacheTemporalProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(signal.color,0)&&encoder.BindTexture(indirectPositions.color,1)&&encoder.BindTexture(normalCapture.color,2)&&
        encoder.BindTexture(surfaceCacheTemporalHistory[read].color,3)&&encoder.BindTexture(surfaceCacheTemporalPositions[read].color,4)&&
        encoder.BindTexture(surfaceCacheTemporalNormals[read].color,5)&&encoder.BindTexture(diffuseCapture.color,6)&&
        encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot accumulate GI history");
    const auto saveGeometry=[&](const Target& target,RenderResourceHandle<PipelineSpec> program) {
        BindTarget(encoder,target);Require(encoder.BindPipeline(program)&&encoder.BindMesh(fullscreen)&&
            encoder.BindTexture(signal.color,0)&&encoder.BindTexture(indirectPositions.color,1)&&encoder.BindTexture(normalCapture.color,2)&&
            encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot save GI history geometry");
    };
    saveGeometry(surfaceCacheTemporalPositions[write],surfaceCacheTemporalPositionProgram);
    saveGeometry(surfaceCacheTemporalNormals[write],surfaceCacheTemporalNormalProgram);
    BindTarget(encoder,surfaceCacheTemporalGi);
    Require(encoder.BindPipeline(surfaceCacheTemporalDisplayProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(surfaceCacheTemporalHistory[write].color,0)&&encoder.BindTexture(signal.color,1)&&
        encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot resolve GI temporal radiance");
    surfaceCacheTemporalPending={};
    surfaceCacheTemporalPending.valid=true;surfaceCacheTemporalPending.image=write;
    surfaceCacheTemporalPending.key=key;surfaceCacheTemporalPending.sequence=sequence+1;
    surfaceCacheTemporalPending.camera=camera;
    statistics.surfaceCacheTemporalPasses=4;statistics.surfaceCacheHistoryUsed=historyValid;
    statistics.surfaceCachePasses+=4;
}
