// Included inside MaterialRenderPipeline::Impl before the native-probe helper.
// UE5.7.1 High persistent world radiance cache. This does not cap, retain, or
// replace the screen-probe allocation. Its complete GPU state lives in Native.
struct UeRadianceCacheConstants {
    glm::uvec4 frame{0};
    std::array<glm::vec4,4> corners{},previousCorners{};
    glm::vec4 camera{0};
};
static_assert(sizeof(UeRadianceCacheConstants)==160);
void PrepareUeLumenRadianceCache(const SurfaceCacheGiSettings& gi,bool collectRayDiagnostics=false){
    if(!gi.ueLumenHigh)return;
    auto& r=nativeProbeTransport.ueRadianceCache;
    if(!r.computeOwner){
        auto owner=std::shared_ptr<Compute::Device>(Compute::Device::Borrow(device));const auto caps=owner->Diagnostics();
        Require(caps.frameAvailable&&caps.maxStorageBindings>=7,"UE radiance cache requires seven frame storage bindings");
        Compute::FramePipelineDesc d;d.uniformMask=1u<<13;d.storageBuffers=7;
        for(unsigned stage=0;stage<8;++stage){d.source=SurfaceCacheDetail::UeRadianceCacheUpdateShader(stage);
            d.label="ue-lumen-radiance-cache-update-"+std::to_string(stage);d.uniformMask=1u<<13;d.sampledMask=0;d.storageImages=stage==7?1:0;
            if(stage==1){d.uniformMask|=1u<<9;d.sampledMask=(1u<<4)|(1u<<5)|(1u<<10);}
            r.updatePrograms[stage]=owner->CreateFramePipeline(d);
        }
        for(unsigned diagnostic=0;diagnostic<1;++diagnostic){d={};d.source=SurfaceCacheDetail::UeRadianceCacheTraceShader(diagnostic!=0);
            d.label=diagnostic?"ue-lumen-radiance-cache-trace-diagnostic":"ue-lumen-radiance-cache-trace";
            d.uniformMask=(1u<<12)|(1u<<13);d.sampledMask=(1u<<9)|(1u<<12)|(1u<<13)|(1u<<14)|(1u<<15);
            d.storageBuffers=7;d.storageImages=2;d.nativeScene=Compute::NativeSceneDesc{};
            r.tracePrograms[diagnostic]=owner->CreateFramePipeline(d);
        }
        d={};d.source=SurfaceCacheDetail::UeRadianceCacheResolveShader();d.label="ue-lumen-radiance-cache-resolve-border";
        d.uniformMask=1u<<13;d.sampledMask=(1u<<0)|(1u<<6);d.storageBuffers=7;d.storageImages=1;
        r.resolveProgram=owner->CreateFramePipeline(d);
        d={};d.uniformMask=1u<<ScreenProbePlacementBinding;d.storageBuffers=1;
        d.source=SurfaceCacheDetail::UeLumenAdaptiveAllocationShader(true);d.label="ue-lumen-adaptive-allocation-reset";
        r.adaptiveResetProgram=owner->CreateFramePipeline(d);
        d.source=SurfaceCacheDetail::UeLumenAdaptiveAllocationShader();d.storageImages=1;d.label="ue-lumen-adaptive-allocation";
        r.adaptiveAllocateProgram=owner->CreateFramePipeline(d);
        r.control=owner->CreateStorageBuffer(64*sizeof(std::uint32_t),nullptr,"ue-radiance-cache-control");
        for(unsigned i=0;i<2;++i)r.indirection[i]=owner->CreateStorageBuffer(442368*sizeof(std::uint32_t),nullptr,"ue-radiance-cache-indirection");
        r.probeState=owner->CreateStorageBuffer(16384*4*sizeof(std::uint32_t),nullptr,"ue-radiance-cache-last-used-traced");
        r.freeList=owner->CreateStorageBuffer(16384*sizeof(std::uint32_t),nullptr,"ue-radiance-cache-free-list");
        r.traceQueue=owner->CreateStorageBuffer(16384*4*sizeof(std::uint32_t),nullptr,"ue-radiance-cache-trace-queue");
        r.usedMarks=owner->CreateStorageBuffer(442368*sizeof(std::uint32_t),nullptr,"ue-radiance-cache-needed");
        r.computeOwner=std::move(owner);
    }
    if(collectRayDiagnostics&&!r.traceAudit){
        Require(r.computeOwner->Diagnostics().maxStorageBindings>=8,"UE radiance cache diagnostics require eight storage bindings");
        Compute::FramePipelineDesc d;d.source=SurfaceCacheDetail::UeRadianceCacheTraceShader(true);d.label="ue-lumen-radiance-cache-trace-diagnostic";
        d.uniformMask=(1u<<12)|(1u<<13);d.sampledMask=(1u<<9)|(1u<<12)|(1u<<13)|(1u<<14)|(1u<<15);
        d.storageBuffers=8;d.storageImages=2;d.nativeScene=Compute::NativeSceneDesc{};
        r.tracePrograms[1]=r.computeOwner->CreateFramePipeline(d);
        // A bounded diagnostic sample; overflow is explicit and invalidates QA.
        r.traceAudit=r.computeOwner->CreateStorageBuffer(65536u*4u*sizeof(std::uint32_t),nullptr,"ue-radiance-cache-actual-trace-audit");
    }
    if(!r.targetsOwner){PipelineDetail::ResourceBuilder b(device);
        const auto target=[&](unsigned w,unsigned h,RHITextureFormat format){Target result;result.width=w;result.height=h;
            result.color=b.StorageTexture(w,h,format);return result;};
        r.rawAtlas=target(2048,2048,RHITextureFormat::RGBA16F);
        // The current RHI has no single-channel float storage-image format.
        // Keep depth in x of an existing full-precision RGBA32F image.
        r.rawDepth=target(2048,2048,RHITextureFormat::RGBA32F);
        r.finalAtlas=target(2304,2304,RHITextureFormat::RGBA16F);
        r.indirectionView=target(1024,432,RHITextureFormat::RGBA32F);
        r.constantsBuffer=b.Uniform(sizeof(UeRadianceCacheConstants));r.targetsOwner=b.Finish();
    }
    if(!r.programsOwner){PipelineDetail::ResourceBuilder b(device);const auto vertex=FullscreenVertexShader();
        r.screenTraceProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::UeLumenHighScreenProbeTraceShader());
        r.screenTraceDiagnosticProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::UeLumenHighScreenProbeTraceShader(true));
        r.screenPackProgram=b.FullscreenPipeline(vertex,ScreenProbeTraceDetail::PackTraceOutputsShader());
        r.programsOwner=b.Finish();
    }
}
void RecordUeLumenAdaptiveAllocation(RHIFrameEncoder& encoder,unsigned probeImage){
    auto& r=nativeProbeTransport.ueRadianceCache;
    Require(r.computeOwner&&placementSpawnedStorage,"UE adaptive allocation requires a storage-capable spawned atlas");
    Require(encoder.KeepAlive(r.computeOwner),"Cannot retain UE adaptive allocator");
    RHICommand::DispatchFrameCompute reset;reset.pipeline=r.adaptiveResetProgram;reset.buffers[0]=r.control;reset.groupsX=1;
    Require(encoder.DispatchCompute(reset),"Cannot reset UE adaptive allocation budget");
    RHICommand::DispatchFrameCompute allocate;allocate.pipeline=r.adaptiveAllocateProgram;allocate.buffers[0]=r.control;
    allocate.images[0]=placementSpawned[probeImage].color;
    allocate.groupsX=unsigned((placementFrameConstants.grid.x+1)/2);allocate.groupsY=unsigned((placementFrameConstants.grid.y+1)/2);
    Require(encoder.DispatchCompute(allocate),"Cannot apply UE adaptive allocation capacity");
}
std::uint64_t UeRadianceCacheLightingKey(const MaterialPipelineSettings& settings) const {
    std::uint64_t key=1469598103934665603ull;
    const auto add=[&](const auto& value){const auto* bytes=reinterpret_cast<const unsigned char*>(&value);
        for(std::size_t i=0;i<sizeof(value);++i){key^=bytes[i];key*=1099511628211ull;}};
    const auto& gi=settings.surfaceCacheGi;
    add(settings.lighting.lightPositions);add(settings.lighting.lightColors);
    const auto areas=MakeAreaLightsConstants(settings.areaLights);add(areas);
    const auto spot=MakeSpotLightConstants(settings.spotLight);add(spot);
    add(gi.areaLightShadows);add(gi.areaShadows.enabled);add(gi.areaShadows.tileResolution);
    add(gi.areaShadows.nearPlane);add(gi.areaShadows.depthBiasTexels);add(gi.areaShadows.normalBiasTexels);
    add(gi.cachedAreaSamplesPerAxis);
    // Card generations and instance transforms change during ordinary motion.
    // They must not turn a persistent world cache into an every-frame clear.
    // Material/emission or topology changes do invalidate cached radiance.
    add(gi.cache->CardCount());add(gi.cache->TriangleCount());add(gi.scene->MeshGeneration());
    const auto& metadata=gi.cache->Metadata();
    for(std::uint32_t card=0;card<gi.cache->CardCount();++card){const auto at=std::size_t(card)*SurfaceCacheCardTexels;
        if(at+8<metadata.size()){add(glm::vec3(metadata[at+7]));add(glm::vec3(metadata[at+8]));}}
    add(placementFrameConstants.traceParams.x);
    return key;
}
void RecordUeLumenRadianceCache(RHIFrameEncoder& encoder,const PipelineCamera& camera,const MaterialPipelineSettings& settings){
    auto& r=nativeProbeTransport.ueRadianceCache;
    Require(r.computeOwner&&r.targetsOwner&&r.programsOwner,"UE radiance cache was not prepared");
    Require(encoder.KeepAlive(r.computeOwner)&&encoder.KeepAlive(r.targetsOwner)&&encoder.KeepAlive(r.programsOwner),"Cannot retain UE radiance cache");
    Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::UeLumenRadianceCache),"Cannot time UE world radiance cache");
    const auto key=UeRadianceCacheLightingKey(settings);
    const bool lightingChanged=r.completed.valid&&r.completed.lightingKey!=key,reset=!r.completed.valid||lightingChanged;
    const unsigned image=r.completed.valid?1-r.completed.image:0;
    UeRadianceCacheConstants c;c.frame={r.completed.frame+1u,reset?1u:0u,400u*(lightingChanged?4u:1u),0u};
    r.queryDiagnostics=settings.screenProbePlacement.trace.sdf.collectRayDiagnostics;c.frame.w=r.queryDiagnostics?1u:0u;
    const auto viewOrigin=glm::dvec3(glm::inverse(camera.view)[3]);
    for(unsigned level=0;level<4;++level){const double cell=(50.0/48.0)*double(1u<<level);
        const auto snapped=glm::floor(viewOrigin/cell)*cell;
        c.corners[level]=glm::vec4(glm::vec3(snapped-glm::dvec3(24.5*cell)),float(cell));}
    c.previousCorners=r.completed.corners;c.camera=glm::vec4(glm::vec3(viewOrigin),placementFrameConstants.traceParams.x);
    Upload(encoder,r.constantsBuffer,&c,sizeof(c),13);
    std::array<Compute::BufferHandle,7> buffers{{r.control,r.indirection[image],r.indirection[1-image],r.probeState,r.freeList,r.traceQueue,r.usedMarks}};
    const auto command=[&](Compute::PipelineHandle pipeline,std::uint64_t invocations){RHICommand::DispatchFrameCompute d;d.pipeline=pipeline;
        for(unsigned i=0;i<buffers.size();++i)d.buffers[i]=buffers[i];
        const auto groups=(invocations+63u)/64u;d.groupsX=unsigned(std::min<std::uint64_t>(groups,65535));
        d.groupsY=unsigned(std::max<std::uint64_t>((groups+65534u)/65535u,1));return d;};
    const auto dispatch=[&](unsigned stage,std::uint64_t invocations){auto d=command(r.updatePrograms[stage],invocations);
        Require(encoder.DispatchCompute(d),"Cannot update UE radiance cache");};
    dispatch(0,442368);
    Require(encoder.BindTexture(placementUniform[nativeProbeTransport.probeImage].color,4)&&
        encoder.BindTexture(placementSpawned[nativeProbeTransport.probeImage].color,5)&&
        encoder.BindTexture(nativeProbeTransport.origins.color,10),"Cannot bind UE radiance-cache demand sources");
    dispatch(1,std::uint64_t(placementFrameConstants.traceAtlas.w));
    dispatch(2,442368);dispatch(3,442368);dispatch(4,64);dispatch(5,442368);dispatch(6,64);
    Require(encoder.BindTexture({},0)&&encoder.BindTexture({},1)&&encoder.BindTexture(surfaceCacheGeometry,9)&&
        encoder.BindTexture(transportPrecision,15)&&encoder.BindTexture(surfaceCacheAttributes,12)&&
        encoder.BindTexture(surfaceCacheMetadata,13)&&encoder.BindTexture(surfaceCacheLighting.color,14),"Cannot bind UE radiance-cache trace scene");
    auto trace=command(r.tracePrograms[r.queryDiagnostics?1:0],64);trace.images[0]=r.rawAtlas.color;trace.images[1]=r.rawDepth.color;
    if(r.queryDiagnostics){Require(bool(r.traceAudit),"UE ray diagnostics were not prepared");trace.buffers[7]=r.traceAudit;}
    trace.indirect=r.control;trace.indirectOffset=8*sizeof(std::uint32_t);
    Require(encoder.DispatchCompute(trace),"Cannot trace selected UE radiance-cache probes");
    Require(encoder.BindTexture(r.rawAtlas.color,0)&&encoder.BindTexture(r.rawDepth.color,6),"Cannot bind UE radiance-cache raw directional radiance/depth");
    auto resolve=command(r.resolveProgram,64);resolve.images[0]=r.finalAtlas.color;
    resolve.indirect=r.control;resolve.indirectOffset=48*sizeof(std::uint32_t);
    Require(encoder.DispatchCompute(resolve),"Cannot resolve UE radiance-cache directional atlas");
    auto publish=command(r.updatePrograms[7],442368);publish.images[0]=r.indirectionView.color;
    Require(encoder.DispatchCompute(publish),"Cannot publish UE radiance-cache interpolation coverage");
    Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::UeLumenRadianceCache),"Cannot finish UE world radiance cache timing");
    r.pending.valid=true;r.pending.image=image;r.pending.frame=c.frame.x;r.pending.lightingKey=key;r.pending.corners=c.corners;r.recorded=true;
}
void BindUeLumenRadianceCache(RHIFrameEncoder& encoder){auto& r=nativeProbeTransport.ueRadianceCache;
    Require(encoder.BindTexture(r.finalAtlas.color,0)&&encoder.BindTexture(r.indirectionView.color,1),"Cannot bind persistent UE world radiance cache");
}
// Call exactly once for a submitted frame at the same success/failure boundary
// used by the existing placement/screen-probe pending state.
void CommitUeLumenRadianceCache(bool success){auto& r=nativeProbeTransport.ueRadianceCache;
    if(!r.recorded)return;if(success)r.completed=r.pending;else r.completed={};
    r.pending={};r.recorded=false;
}
