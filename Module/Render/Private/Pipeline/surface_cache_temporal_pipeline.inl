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
    if(s.temporal.objectMotionReprojection&&!surfaceCacheObjectTemporalProgramsOwner) {
        PipelineDetail::ResourceBuilder b(device);const auto vertex=FullscreenVertexShader();
        surfaceCacheObjectTemporalProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::TemporalShader(true));
        surfaceCacheObjectTemporalPositionProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::TemporalGeometryShader(false,true));
        surfaceCacheObjectTemporalProgramsOwner=b.Finish();
    }
    if(s.temporal.objectMotionReprojection&&s.reuseStaticReceiverMotionCapture&&!surfaceCacheStaticMotionProgramsOwner) {
        PipelineDetail::ResourceBuilder b(device);
        surfaceCacheStaticMotionProgram=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::StaticReceiverMotionShader());
        surfaceCacheStaticMotionBuffer=b.Uniform(80);surfaceCacheStaticMotionProgramsOwner=b.Finish();
    }
    if(s.temporal.disocclusionFilter&&!surfaceCacheDisocclusionTemporalProgramsOwner) {
        PipelineDetail::ResourceBuilder b(device);
        surfaceCacheDisocclusionTemporalProgram=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::TemporalShader(false,true));
        surfaceCacheDisocclusionTemporalProgramsOwner=b.Finish();
    }
    if(s.temporal.disocclusionFilter&&s.temporal.objectMotionReprojection&&!surfaceCacheObjectDisocclusionTemporalProgramsOwner) {
        PipelineDetail::ResourceBuilder b(device);
        surfaceCacheObjectDisocclusionTemporalProgram=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::TemporalShader(true,true));
        surfaceCacheObjectDisocclusionTemporalProgramsOwner=b.Finish();
    }
    if(s.temporal.reactiveShadows&&!surfaceCacheShadowTemporalProgramsOwner) {
        PipelineDetail::ResourceBuilder b(device);const auto vertex=FullscreenVertexShader();
        surfaceCacheShadowTemporalProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::TemporalShader(false,false,true));
        surfaceCacheShadowObjectTemporalProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::TemporalShader(true,false,true));
        surfaceCacheShadowDisocclusionTemporalProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::TemporalShader(false,true,true));
        surfaceCacheShadowObjectDisocclusionTemporalProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::TemporalShader(true,true,true));
        surfaceCacheShadowMotionBuffer=b.Uniform(2*sizeof(glm::vec4));
        surfaceCacheShadowTemporalProgramsOwner=b.Finish();
    }
    if(!surfaceCacheTemporalOwner||surfaceCacheTemporalGi.width!=width||surfaceCacheTemporalGi.height!=height||surfaceCacheTemporalHalfReceiverRadiance!=s.halfReceiverRadiance) {
        PipelineDetail::ResourceBuilder b(device);
        const auto target=[&](RHITextureFormat format=RHITextureFormat::RGBA32F){Target t;t.width=width;t.height=height;
            t.color=b.Texture(width,height,format);t.framebuffer=b.Target(t.color,{});return t;};
        const auto radianceFormat=s.halfReceiverRadiance?RHITextureFormat::RGBA16F:RHITextureFormat::RGBA32F;
        std::array<Target,2> history,positions,normals;
        for(unsigned i=0;i<2;++i){history[i]=target(radianceFormat);positions[i]=target();normals[i]=target();}
        auto filtered=target(radianceFormat);auto owner=b.Finish();
        surfaceCacheTemporalHistory=history;surfaceCacheTemporalPositions=positions;surfaceCacheTemporalNormals=normals;
        surfaceCacheTemporalGi=filtered;surfaceCacheTemporalOwner=std::move(owner);surfaceCacheTemporalCompleted={};
        surfaceCacheTemporalHalfReceiverRadiance=s.halfReceiverRadiance;
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
    add(sc.useScreenProbes);add(sc.cacheBilinear);add(sc.reuseAnyHitBounds);add(sc.directionalReconstruction);add(sc.reconstructionDebug);add(sc.maxDistance);add(sc.rayBias);add(sc.intensity);
    add(sc.hardwareContactTracing);add(sc.hardwareCacheVisibility);
    add(sc.hardwareOriginTracing);add(t.sdf.hardwareRayTracing);
    add(sc.receiverSoftwareOrigins);add(sc.reuseOriginSoftwareCertification);
    add(sc.reuseClosestTraversal);add(sc.fastGeometryAddressing);add(t.sdf.reuseNativeCandidate);
    add(t.sdf.directSoftwareRefinement);
    add(t.sdf.nativeInteriorTracing);add(sc.contact.budgetedSampling);
    add(sc.cacheSettledGather);
    add(sc.splitReceiverGather);add(sc.sparseReceiverSampling);add(sc.coherentReceiverTracing);add(sc.geometryReceiverRisk);add(sc.receiverLatticeMax);add(sc.cachedDirectDiffuse);add(sc.transportRays);add(sc.transportUpdatePeriod);add(sc.receiverRiskDistance);
    add(sc.transportTexelStride);
    add(sc.compactReceiverTracing);
    add(sc.deferredReceiverMix);
    add(sc.nativeScreenProbeTransport);
    add(sc.nativeAdaptiveFreshSampling);
    if(sc.nativeForceRasterCore)add(std::uint64_t(0x4e42434f52453330ull));
    if(sc.nativeSyntheticReceiverFilterSupport)add(std::uint64_t(0x4e535946494c5431ull));
    if(sc.nativeReceiverFilterRiskWeights)add(std::uint64_t(0x4e52465249534b31ull));
    if(sc.nativeReceiverFilterSupportHalo)add(std::uint64_t(0x4e524648414c4f31ull));
    if(sc.ueLumenHigh)add(std::uint64_t(0x55454c554d484947ull));
    add(sc.nativeComputePlacement);
    // Preserve the old hash stream while this optional metadata ABI is off.
    if(sc.nativeTemporalContactMetadata)add(std::uint64_t(0x4e54434d45544131ull));
    add(sc.halfReceiverRadiance);
    add(sc.mrtReceiverCapture);add(sc.diagnosticReceiverClipPlane);
    add(sc.reuseStaticReceiverMotionCapture);
    if(sc.fuseNativeReconstruction)add(std::uint64_t(0x4e524d5254ull));
    add(sc.omitUnusedRawGather);
    add(sc.cachedAreaSamplesPerAxis);
    add(sc.contact.halfResolutionScreenRisk);
    add(sc.contact.prioritizeContactCore);
    add(settings.temporal.antialiasing);
    add(p.adaptive);add(p.freeze);add(p.baseSpacing);add(p.candidateCount);add(p.relativeDepthThreshold);add(p.frameIndex);
    add(p.cornerRefinement);add(p.cornerDetectionRadius);add(p.cornerProbeSupport);add(p.cornerNormalAngleDegrees);
    add(p.reconstructionVisibility);
    add(p.continuousInterpolation);
    add(p.curvatureAwareWeights);
    add(p.curvatureRefinement);add(p.curvatureNormalAngleDegrees);add(p.curvaturePlaneErrorPixels);
    add(sc.safeProbeOrigins);add(sc.rasterSourcePositions);add(sc.originSafetyFraction);add(sc.originSafetyEpsilon);
    add(t.rayResolution);add(t.samplingSeed);add(t.maxSteps);add(t.maxMip);add(t.thickness);add(t.relativeThickness);
    add(t.normalBias);add(t.maxDistance);add(t.singleInvocation);add(t.singlePassOutputs);add(t.compactSdfRays);
    add(t.sdf.useMesh);add(t.sdf.useGlobal);add(t.sdf.meshTraceDistance);add(t.sdf.hitEpsilon);add(t.sdf.maxSteps);
    const auto& s=sc.temporal;add(s.maxFrames);add(s.directionPhases);add(s.staticConvergence);add(s.motionConsistentSpatialReuse);add(s.localCameraHistory);add(s.motionStability);add(s.motionSpatialFilter);add(s.motionDisocclusionRecovery);add(s.objectMotionReprojection);add(s.reactiveShadows);add(s.reactiveMovingReceivers);add(s.disocclusionFilter);add(s.distanceThreshold);add(s.normalThresholdDegrees);
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
    add(contact.splitReceiverOrigins);add(contact.reuseLightingLookup);
    add(contact.fastLightingAddressing);
    add(contact.disocclusionSampling);add(contact.disocclusionRayMultiplier);add(contact.movingReceiverRayMultiplier);add(contact.movingReceiverFullCurrent);
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
    const auto advanceShadowBounds=[&]() {
        if(!temporal.reactiveShadows)return;
        // Keep each certified motion event separate until it expires. A new
        // mover must not discard another mover's still-settling old footprint,
        // and a continuously moving object must not grow an unlimited union.
        const auto currentMin=result.shadowBoundsMin,currentMax=result.shadowBoundsMax;
        result.shadowBoundsHistoryMin=previous.shadowBoundsHistoryMin;
        result.shadowBoundsHistoryMax=previous.shadowBoundsHistoryMax;
        const unsigned cursor=(previous.shadowBoundsHistoryCursor+1)%SurfaceCacheShadowBoundsHistoryCapacity;
        result.shadowBoundsHistoryCursor=cursor;
        result.shadowBoundsHistoryMin[cursor]=currentMin;
        result.shadowBoundsHistoryMax[cursor]=currentMax;
        result.shadowBoundsMin=glm::vec4(0);result.shadowBoundsMax=glm::vec4(0);
        const unsigned retainedFrames=std::min(temporal.dynamicSettleFrames,SurfaceCacheShadowBoundsHistoryCapacity-1);
        for(unsigned age=0;age<=retainedFrames;++age) {
            const unsigned at=(cursor+SurfaceCacheShadowBoundsHistoryCapacity-age)%SurfaceCacheShadowBoundsHistoryCapacity;
            const auto low=result.shadowBoundsHistoryMin[at],high=result.shadowBoundsHistoryMax[at];
            if(high.w<.5f)continue;
            const bool first=result.shadowBoundsMax.w<.5f;
            result.shadowBoundsMin=glm::vec4(first?glm::vec3(low):glm::min(glm::vec3(result.shadowBoundsMin),glm::vec3(low)),
                std::max(result.shadowBoundsMin.w,low.w));
            result.shadowBoundsMax=glm::vec4(first?glm::vec3(high):glm::max(glm::vec3(result.shadowBoundsMax),glm::vec3(high)),1);
        }
    };
    if(historyAllowed&&previous.key==fullKey) {
        // The common static/camera-motion path reuses an immutable value
        // snapshot: no second content hash or per-instance allocation/copy.
        result.compatible=true;result.snapshot=previous.motionSnapshot;result.stableKey=previous.stableKey;
        if(temporal.retainHistoryOnObjectMotion&&previous.dynamicSettleRemaining>0) {
            result.dynamicLighting=true;result.settleRemaining=previous.dynamicSettleRemaining-1;
            advanceShadowBounds();
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
            if(temporal.reactiveShadows&&after.source) {
                const auto lo=after.source->BoundsMin(),hi=after.source->BoundsMax();
                glm::vec3 beforeLo(std::numeric_limits<float>::max()),beforeHi(std::numeric_limits<float>::lowest());
                glm::vec3 afterLo=beforeLo,afterHi=beforeHi;
                for(unsigned corner=0;corner<8;++corner) {
                    const glm::vec4 local(corner&1?hi.x:lo.x,corner&2?hi.y:lo.y,corner&4?hi.z:lo.z,1);
                    const auto a=glm::vec3(before.transform*local),b=glm::vec3(after.transform*local);
                    beforeLo=glm::min(beforeLo,a);beforeHi=glm::max(beforeHi,a);
                    afterLo=glm::min(afterLo,b);afterHi=glm::max(afterHi,b);
                }
                const auto low=glm::min(beforeLo,afterLo),high=glm::max(beforeHi,afterHi);
                const float padding=.5f*std::max(glm::length(beforeHi-beforeLo),glm::length(afterHi-afterLo));
                if(std::isfinite(padding)&&padding>0) {
                    const bool first=result.shadowBoundsMax.w<.5f;
                    result.shadowBoundsMin=glm::vec4(first?low:glm::min(glm::vec3(result.shadowBoundsMin),low),
                        std::max(result.shadowBoundsMin.w,std::max(padding,.05f)));
                    result.shadowBoundsMax=glm::vec4(first?high:glm::max(glm::vec3(result.shadowBoundsMax),high),1);
                }
            }
        }
    }
    // Unknown generation-only edits cannot be certified as motion. Require
    // coherent SDF and card updates for the copied transform changes.
    if(!moved||current.geometryGeneration<=old.geometryGeneration||current.cacheGeneration<=old.cacheGeneration)return result;
    result.compatible=true;result.dynamicLighting=true;result.settleRemaining=temporal.dynamicSettleFrames;
    advanceShadowBounds();
    return result;
}
void RecordSurfaceCacheTemporal(RHIFrameEncoder& encoder,const SurfaceCacheGiSettings& gi,const PipelineCamera& camera,
    std::uint64_t key,std::uint64_t sequence,bool historyValid,const Target& signal,const SurfaceCacheTemporalMotionState& motion,bool stationary,
    bool objectMotion) {
    const auto& s=gi.temporal;const auto& previous=surfaceCacheTemporalCompleted;
    const bool dynamicLighting=motion.dynamicLighting;
    const auto read=previous.image,write=1-read;
    Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheTemporal),"Cannot begin Surface Cache temporal GPU timing");
    // The old local-camera experiment rejects the first movement frame. The
    // separate stability candidate validates geometry/albedo in the shader;
    // neither extends history for scene motion or a projection edit.
    const bool cameraMotion=historyValid&&s.staticConvergence&&s.maxFrames>1&&!dynamicLighting&&!stationary&&
        camera.view!=previous.camera.view&&camera.projection==previous.camera.projection;
    const bool cameraEpoch=s.localCameraHistory&&cameraMotion;
    const bool disocclusion=s.disocclusionFilter&&historyValid&&(cameraMotion||dynamicLighting);
    const bool shadowResponse=s.reactiveShadows&&historyValid&&dynamicLighting&&motion.shadowBoundsMax.w>.5f;
    const bool localAdmission=cameraEpoch&&previous.cameraMotion;
    const bool stabilize=s.motionStability&&s.staticConvergence&&s.maxFrames>1;
    const bool recover=s.motionDisocclusionRecovery&&s.staticConvergence&&s.maxFrames>1;
    const bool nativeContactMetadata=(gi.nativeTemporalContactMetadata||gi.nativeForceRasterCore)&&gi.nativeScreenProbeTransport&&
        statistics.surfaceCacheNativeScreenProbeTransport;
    const bool nativeContactRiskUnion=nativeContactMetadata&&gi.nativeForceRasterCore;
    const bool temporalContact=statistics.surfaceCacheContactCollected||nativeContactMetadata;
    const bool contactGuided=temporalContact&&gi.contact.geometryGuidedFilter&&
        !gi.contact.referenceReceiver&&gi.contact.filterStrength>0;
    const bool filterEnabled=s.motionSpatialFilter&&s.staticConvergence&&s.maxFrames>1;
    // Stopping switches curved probe filtering back to matching angular strata.
    // Fold the moving estimator's weight once, then resume static convergence.
    const bool stopped=historyValid&&stabilize&&stationary&&!dynamicLighting&&previous.cameraMotion;
    const bool motionActive=historyValid&&filterEnabled&&(cameraMotion||dynamicLighting);
    const unsigned stabilityFade=motionActive?8u:historyValid&&filterEnabled&&previous.motionStabilityFade?
        previous.motionStabilityFade-1:0;
    SurfaceCacheTemporalConstants c;c.currentInverseView=glm::inverse(camera.view);
    c.previousViewProjection=previous.camera.projection*previous.camera.view;c.previousView=previous.camera.view;
    c.screen={int(width),int(height),(historyValid?1:0)|(localAdmission?2:0)|(cameraEpoch?4:0)|
        ((stabilize||recover||s.disocclusionFilter||gi.contact.disocclusionSampling)?32:0)|(stabilize&&cameraMotion?8:0)|(temporalContact?64:0)|
        (recover&&cameraMotion?128:0)|(contactGuided?4096:0)|(stopped?8192:0)|
        (disocclusion?16384:0)|(objectMotion?32768:0)|
        (s.disocclusionFilter&&cameraMotion&&previous.stableLightingFrames>=32?65536:0)|
        (statistics.surfaceCacheFreshSamplingCollected?131072:0)|(shadowResponse?262144:0)|
        (shadowResponse&&s.reactiveMovingReceivers?524288:0)|
        (gi.splitReceiverGather&&!gi.geometryReceiverRisk?1048576:0)|
        (nativeContactMetadata?2097152:0)|
        (nativeContactMetadata&&gi.sparseReceiverSampling?4194304:0)|
        (nativeContactRiskUnion?8388608:0)|
        int(stabilityFade<<8),int(stationary?std::max(s.maxFrames,64u):s.maxFrames)};
    c.reject={s.distanceThreshold,std::cos(glm::radians(s.normalThresholdDegrees)),s.movingMaxFrames,s.motionThresholdPixels};
    c.clamp={s.clampSigma,s.neighborhoodClamp?(s.clampSlowMotion?2.f:1.f):0.f,.002f,s.edgeProtection?s.edgeMaxFrames:0.f};
    c.recovery={s.youngFilter?s.youngFilterStrength:0.f,float(s.youngFilterRadius),s.youngFilterMaxFrames,.08f};
    c.dynamic={dynamicLighting?1.f:0.f,stopped?float(std::min(s.maxFrames,4u)):s.dynamicMaxFrames,s.dynamicClampSigma,
        (gi.contact.enabled&&gi.contact.coherentProbeRisk)||(gi.splitReceiverGather&&gi.geometryReceiverRisk)?1.f:0.f};
    Upload(encoder,surfaceCacheTemporalBuffer,&c,sizeof(c),SurfaceCacheTemporalBinding);
    const auto temporalContactMask=nativeContactMetadata?nativeProbeTransport.NativeSoftMask.color:
        gi.splitReceiverGather?transportMask.color:statistics.surfaceCacheContactCollected?surfaceCacheContactMask.color:signal.color;
    if(shadowResponse) {
        const std::array<glm::vec4,2> bounds{{motion.shadowBoundsMin,motion.shadowBoundsMax}};
        Upload(encoder,surfaceCacheShadowMotionBuffer,bounds.data(),sizeof(bounds),14);
    }
    // A reset/discard still writes the other image. Never destroy the last
    // completed history until this frame has been submitted successfully.
    Require(encoder.BindTexture({},3)&&encoder.BindTexture({},4)&&encoder.BindTexture({},5),"Cannot unbind GI history targets");
    BindTarget(encoder,surfaceCacheTemporalHistory[write]);
    const auto temporalProgram=shadowResponse?
        (objectMotion?(disocclusion?surfaceCacheShadowObjectDisocclusionTemporalProgram:surfaceCacheShadowObjectTemporalProgram):
            (disocclusion?surfaceCacheShadowDisocclusionTemporalProgram:surfaceCacheShadowTemporalProgram)):
        (objectMotion?(disocclusion?surfaceCacheObjectDisocclusionTemporalProgram:surfaceCacheObjectTemporalProgram):
            (disocclusion?surfaceCacheDisocclusionTemporalProgram:surfaceCacheTemporalProgram));
    Require(encoder.BindPipeline(temporalProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(signal.color,0)&&encoder.BindTexture(indirectPositions.color,1)&&encoder.BindTexture(normalCapture.color,2)&&
        encoder.BindTexture(surfaceCacheTemporalHistory[read].color,3)&&encoder.BindTexture(surfaceCacheTemporalPositions[read].color,4)&&
        encoder.BindTexture(surfaceCacheTemporalNormals[read].color,5)&&encoder.BindTexture(diffuseCapture.color,6)&&
        encoder.BindTexture(temporalContactMask,7)&&
        encoder.BindTexture(nativeContactMetadata?transportMask.color:contactGuided?surfaceCacheContactMeasurements.color:signal.color,8)&&
        encoder.BindTexture(nativeContactMetadata&&gi.sparseReceiverSampling?transportReconstructed.color:signal.color,10)&&
        encoder.BindTexture(objectMotion?surfaceCacheObjectPreviousPositions.color:signal.color,9)&&
        encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot accumulate GI history");
    const auto saveGeometry=[&](const Target& target,RenderResourceHandle<PipelineSpec> program) {
        BindTarget(encoder,target);Require(encoder.BindPipeline(program)&&encoder.BindMesh(fullscreen)&&
            encoder.BindTexture(signal.color,0)&&encoder.BindTexture(indirectPositions.color,1)&&encoder.BindTexture(normalCapture.color,2)&&
            encoder.BindTexture(diffuseCapture.color,3)&&encoder.BindTexture(objectMotion?surfaceCacheObjectPreviousPositions.color:signal.color,4)&&
            encoder.BindTexture(statistics.surfaceCacheFreshSamplingCollected?surfaceCacheFreshSampling.color:signal.color,5)&&
            encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot save GI history geometry");
    };
    saveGeometry(surfaceCacheTemporalPositions[write],statistics.surfaceCacheFreshSamplingCollected?
        (objectMotion?surfaceCacheFreshObjectPositionProgram:surfaceCacheFreshPositionProgram):
        (objectMotion?surfaceCacheObjectTemporalPositionProgram:surfaceCacheTemporalPositionProgram));
    saveGeometry(surfaceCacheTemporalNormals[write],surfaceCacheTemporalNormalProgram);
    BindTarget(encoder,surfaceCacheTemporalGi);
    Require(encoder.BindPipeline(surfaceCacheTemporalDisplayProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(surfaceCacheTemporalHistory[write].color,0)&&encoder.BindTexture(signal.color,1)&&
        encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&
        encoder.BindTexture(diffuseCapture.color,4)&&encoder.BindTexture(temporalContactMask,5)&&
        encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot resolve GI temporal radiance");
    Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheTemporal),"Cannot end Surface Cache temporal GPU timing");
    if(nativeContactMetadata)Require(encoder.BindTexture({},10),"Cannot unbind native temporal current support");
    surfaceCacheTemporalPending={};
    surfaceCacheTemporalPending.valid=true;surfaceCacheTemporalPending.image=write;
    surfaceCacheTemporalPending.key=key;surfaceCacheTemporalPending.sequence=sequence+1;
    surfaceCacheTemporalPending.camera=camera;
    surfaceCacheTemporalPending.cameraMotion=cameraMotion;
    surfaceCacheTemporalPending.motionStabilityFade=stabilityFade;
    surfaceCacheTemporalPending.stableLightingFrames=historyValid&&!dynamicLighting?std::min(previous.stableLightingFrames+1,64u):0;
    statistics.surfaceCacheTemporalPasses=4+unsigned(objectMotion);statistics.surfaceCacheHistoryUsed=historyValid;
    statistics.surfaceCachePasses+=statistics.surfaceCacheTemporalPasses;
}
