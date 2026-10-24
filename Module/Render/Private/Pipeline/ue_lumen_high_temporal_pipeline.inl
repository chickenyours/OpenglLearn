// Included inside MaterialRenderPipeline::Impl after the AO/shared-sampling
// helpers. Add PipelineDetail::UeLumenHighTemporalState<Target> ueHighTemporal.
// Root owns the branch selection and commits the existing completed state.
void PrepareUeLumenHighTemporal(const SurfaceCacheGiSettings& gi) {
    if(!gi.ueLumenHigh)return;
    Require(gi.enabled&&gi.useScreenProbes,"UE High temporal requires screen-probe GI");
    PrepareUeLumenHighShortRangeAO();
    auto& t=ueHighTemporal;
    if(!t.programsOwner){
        PipelineDetail::ResourceBuilder b(device);const auto vertex=FullscreenVertexShader();
        t.temporalProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::UeLumenHighTemporalShader());
        t.countProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::UeLumenHighCountShader());
        t.temporalBuffer=b.Uniform(sizeof(PipelineDetail::UeLumenHighTemporalConstants));
        // The runtime object-motion branch remains reflected even when false.
        // Always bind a valid full-sized palette instead of a stale ObjectData.
        t.identityMotionBuffer=b.Uniform(64u*sizeof(glm::mat4));
        t.directProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::TransportResolveShader(true,0,true));
        t.directBuffer=b.Uniform(sizeof(TransportConstants));
        t.programsOwner=b.Finish();
    }
    // Cache-local positions/normals are immutable under rigid movement. Decode
    // their current world frame once over the atlas instead of once per
    // primary receiver, projection and filter tap. GI/ray users keep local IO.
    t.worldAttributesActive=gi.cachedDirectDiffuse&&gi.cache&&
        gi.cache->Settings().localCaptureAttributes&&gi.cache->CardCount()>0&&gi.cache->PlaneTexels()>0;
    if(t.worldAttributesActive){
        if(!t.worldAttributesProgramsOwner){
            PipelineDetail::ResourceBuilder b(device);const auto vertex=FullscreenVertexShader();
            t.worldAttributesProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::UeLumenCardWorldAttributesShader());
            t.worldDirectProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::TransportResolveShader(true,0,true,true));
            t.worldAttributesProgramsOwner=b.Finish();
        }
        const auto atlasWidth=gi.cache->Width(),planeTexels=gi.cache->PlaneTexels();
        Require(atlasWidth>0&&planeTexels%atlasWidth==0,"World card attributes require whole atlas rows");
        const auto atlasHeight=2u*(planeTexels/atlasWidth);
        if(!t.worldAttributesOwner||t.worldAttributes.width!=atlasWidth||t.worldAttributes.height!=atlasHeight){
            PipelineDetail::ResourceBuilder b(device);Target target;target.width=atlasWidth;target.height=atlasHeight;
            CreateRHITextureSpec desc;desc.width=atlasWidth;desc.height=atlasHeight;
            desc.textureDataStoreType=desc.textureUseType=RHITextureFormat::RGBA32F;
            desc.mipmaps=false;desc.filterMode=RHIFilterMode::Nearest;desc.addressMode=RHIAddressMode::ClampToEdge;
            target.color=b.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(desc,cb);});
            target.framebuffer=b.Target(target.color,{});
            auto owner=b.Finish();t.worldAttributes=target;t.worldAttributesOwner=std::move(owner);
        }
    }
    if(gi.temporal.objectMotionReprojection&&gi.reuseStaticReceiverMotionCapture&&!surfaceCacheStaticMotionProgramsOwner){
        PipelineDetail::ResourceBuilder b(device);
        surfaceCacheStaticMotionProgram=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::StaticReceiverMotionShader());
        surfaceCacheStaticMotionBuffer=b.Uniform(80);
        surfaceCacheStaticMotionProgramsOwner=b.Finish();
    }
    if(!t.targetsOwner||t.history[0].width!=width||t.history[0].height!=height){
        PipelineDetail::ResourceBuilder b(device);
        auto target=[&](RHITextureFormat format){
            Target result;result.width=width;result.height=height;
            result.color=b.Texture(width,height,format);result.framebuffer=b.Target(result.color,{});return result;
        };
        std::array<Target,2> history,metadata,positions,normals,mrt;
        for(unsigned image=0;image<2;++image){
            // UE PF_FloatRGBA history; project IO adapters keep exact world
            // position/owner, full-resolution reprojection and float counts.
            history[image]=target(RHITextureFormat::RGBA16F);
            metadata[image]=target(RHITextureFormat::RGBA32F);
            positions[image]=target(RHITextureFormat::RGBA32F);
            normals[image]=target(RHITextureFormat::RGBA32F);
            mrt[image]=history[image];
            mrt[image].framebuffer=b.TargetColors({history[image].color,metadata[image].color,
                positions[image].color,normals[image].color});
        }
        auto owner=b.Finish();t.history=history;t.metadata=metadata;t.positions=positions;
        t.normals=normals;t.aux=metadata;t.mrt=mrt;t.targetsOwner=std::move(owner);
        // Newly allocated histories cannot represent an earlier completed frame.
        surfaceCacheTemporalCompleted.valid=false;
        ResetUeLumenHighShortRangeAOHistory();
    }
    // Publish existing public readback aliases, without borrowing legacy
    // programs. Force legacy target allocation if the caller later switches
    // away from UE High; its preparation must restore its own storage/layout.
    surfaceCacheTemporalOwner.reset();
    surfaceCacheTemporalHistory=t.history;surfaceCacheTemporalPositions=t.positions;
    surfaceCacheTemporalNormals=t.normals;surfaceCacheTemporalGi=ueHighAO.compositeGI;
}

// This function records RawAO itself: the main pipeline must NOT also record
// that pass. read/write are exactly the existing committed GI ping-pong image;
// no helper advances the completed frame, direction seed or sampling sequence.
const Target& RecordUeLumenHighTemporal(RHIFrameEncoder& encoder,const SurfaceCacheGiSettings& gi,
    const PipelineCamera& camera,std::uint64_t key,std::uint64_t sequence,bool historyValid,
    const Target& rawGiHalf,const Target& movingHalf,const SurfaceCacheTemporalMotionState& motion,bool objectMotion) {
    auto& t=ueHighTemporal;const auto& previous=surfaceCacheTemporalCompleted;
    Require(gi.ueLumenHigh&&t.programsOwner&&t.targetsOwner,"UE High temporal was not prepared");
    Require(rawGiHalf.width==(width+1)/2&&rawGiHalf.height==(height+1)/2&&
        movingHalf.width==rawGiHalf.width&&movingHalf.height==rawGiHalf.height,
        "UE High temporal expects matching half-resolution GI/moving inputs");
    Require(encoder.KeepAlive(t.programsOwner)&&encoder.KeepAlive(t.targetsOwner)&&
        encoder.KeepAlive(ueLumenHighGather.targetsOwner)&&encoder.KeepAlive(targetsOwner),
        "Cannot retain UE High temporal inputs and histories");
    if(objectMotion)Require(surfaceCacheObjectMotionOwner&&surfaceCacheObjectMotionBuffer.IsValid()&&
        encoder.KeepAlive(surfaceCacheObjectMotionOwner),"UE High object reprojection lacks certified capture/palette");
    if(objectMotion&&gi.reuseStaticReceiverMotionCapture&&surfaceCacheStaticMotionProgramsOwner)
        Require(encoder.KeepAlive(surfaceCacheStaticMotionProgramsOwner),"Cannot retain static receiver-motion programs");
    const unsigned read=previous.image,write=1-read;
    Require(read<2,"UE High history image is invalid");
    historyValid=historyValid&&previous.valid;
    Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheTemporal),"Cannot begin UE High GI/AO temporal timing");
    RecordUeLumenHighShortRangeAO(encoder,camera,ueLumenViewFrame,indirectPositions.color,normalCapture.color);
    PipelineDetail::UeLumenHighTemporalConstants c;
    c.previousViewProjection=previous.camera.projection*previous.camera.view;c.previousView=previous.camera.view;
    // HISTORY_REJECT_BASED_ON_NORMAL is an official UE quality option; enable
    // its documented45-degree threshold for the project's feet/wall details.
    // It is additional to High defaults, not presented as the default setting.
    c.flags={historyValid?1:0,objectMotion?1:0,1,0};c.parameters={.01f,10.f,10.f,.9f};
    Upload(encoder,t.temporalBuffer,&c,sizeof(c),8);
    if(objectMotion)Require(encoder.BindUniformBuffer(surfaceCacheObjectMotionBuffer,1),"Cannot bind actual UE High object-motion palette");
    else {
        std::array<glm::mat4,64> identity;
        for(auto& matrix:identity)matrix=glm::mat4(1);
        Upload(encoder,t.identityMotionBuffer,identity.data(),sizeof(identity),1);
    }
    Require(encoder.BindTexture({},0)&&encoder.BindTexture({},1)&&encoder.BindTexture({},4)&&
        encoder.BindTexture({},5)&&encoder.BindTexture({},8),"Cannot unbind UE High MRT output aliases");
    BindTarget(encoder,t.mrt[write]);
    BindUeLumenHighSampling(encoder,camera,ueLumenViewFrame,historyValid);
    Require(encoder.BindPipeline(t.temporalProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(rawGiHalf.color,0)&&encoder.BindTexture(t.history[read].color,1)&&
        encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&
        encoder.BindTexture(t.positions[read].color,4)&&encoder.BindTexture(t.normals[read].color,5)&&
        encoder.BindTexture(t.metadata[read].color,8)&&encoder.BindTexture(movingHalf.color,9)&&
        encoder.BindTexture(objectMotion?surfaceCacheObjectPreviousPositions.color:indirectPositions.color,10)&&
        encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot accumulate UE High GI history and actual reprojection metadata");
    surfaceCacheTemporalGi=RecordUeLumenHighAOHistoryAndComposite(encoder,camera,ueLumenViewFrame,historyValid,write,
        indirectPositions.color,normalCapture.color,t.history[write].color,t.metadata[write].color,
        t.metadata[write].color,diffuseCapture.color);
    Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheTemporal),"Cannot finish UE High GI/AO temporal timing");
    surfaceCacheTemporalPending={};surfaceCacheTemporalPending.valid=true;
    surfaceCacheTemporalPending.image=write;surfaceCacheTemporalPending.key=key;
    surfaceCacheTemporalPending.sequence=sequence+1;surfaceCacheTemporalPending.camera=camera;
    surfaceCacheTemporalPending.motionSnapshot=motion.snapshot;surfaceCacheTemporalPending.stableKey=motion.stableKey;
    surfaceCacheTemporalPending.dynamicSettleRemaining=motion.settleRemaining;
    surfaceCacheTemporalPending.shadowBoundsMin=motion.shadowBoundsMin;
    surfaceCacheTemporalPending.shadowBoundsMax=motion.shadowBoundsMax;
    surfaceCacheTemporalPending.shadowBoundsHistoryMin=motion.shadowBoundsHistoryMin;
    surfaceCacheTemporalPending.shadowBoundsHistoryMax=motion.shadowBoundsHistoryMax;
    surfaceCacheTemporalPending.shadowBoundsHistoryCursor=motion.shadowBoundsHistoryCursor;
    surfaceCacheTemporalPending.cameraMotion=historyValid&&!motion.dynamicLighting&&camera.view!=previous.camera.view&&
        camera.projection==previous.camera.projection;
    surfaceCacheTemporalPending.stableLightingFrames=historyValid&&!motion.dynamicLighting?
        std::min(previous.stableLightingFrames+1,64u):0;
    statistics.surfaceCacheTemporalPasses=5+unsigned(objectMotion);
    statistics.surfaceCacheHistoryUsed=historyValid;
    statistics.surfaceCachePasses+=statistics.surfaceCacheTemporalPasses;
    return surfaceCacheTemporalGi;
}

// The opaque pass emits only specular when cachedDirectDiffuse is enabled.
// Restore retained direct-diffuse resolve independently of receiver GI.
void RecordUeLumenHighCachedDirect(RHIFrameEncoder& encoder,const MaterialPipelineSettings& settings,const PipelineCamera& camera){
    const auto& s=settings.surfaceCacheGi;if(!s.ueLumenHigh||!s.cachedDirectDiffuse)return;
    auto& t=ueHighTemporal;Require(encoder.KeepAlive(t.programsOwner),"Cannot retain cached direct resolve");
    TransportConstants c{{0,0,0,0},{s.receiverRiskDistance,float(s.transportRays),float(s.contact.raysPerPixel),float(s.transportTexelStride)}};
    c.viewProjection=camera.projection*camera.view;c.launchBias={settings.screenProbePlacement.trace.normalBias,1.f,0,0};
    c.receiverOptions={float(s.receiverLatticeMax),0,.1f,0};
    const auto& metadata=s.cache->Metadata();const auto objects=s.cache->CardCount()/6;
    Require(objects<=256,"Cached direct object count exceeds its bounds palette");
    for(unsigned object=0;object<objects;++object){c.bounds[object*2]=metadata[object*6*SurfaceCacheCardTexels+9];c.bounds[object*2+1]=metadata[object*6*SurfaceCacheCardTexels+10];}
    const auto bytes=std::uint32_t(offsetof(TransportConstants,bounds)+objects*2*sizeof(glm::vec4));
    Upload(encoder,t.directBuffer,&c,bytes,14);
    Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheDirectResolve),"Cannot time UE cached direct resolve");
    // Probe/HZB work shares the frame's UBO namespace: closest HZB leaves its
    // 32-byte reduction constants at AreaLightsBinding (8). Restore the actual
    // scene light blocks for every direct variant, including non-local capture.
    // Sun/spot live in SurfaceCacheGiConstants; point and rectangle lights have
    // their independent PBR (3) and area (8) blocks. Buffer contents are current.
    Require(encoder.BindUniformBuffer(surfaceCacheBuffer,SurfaceCacheGiBinding)&&
        encoder.BindUniformBuffer(lightingBuffer,Material::PbrPassBinding)&&
        encoder.BindUniformBuffer(areaBuffer,AreaLightsBinding),
        "Cannot restore cached-direct scene lighting constants");
    const bool worldAttributes=t.worldAttributesActive;
    if(worldAttributes){
        Require(t.worldAttributesProgramsOwner&&t.worldAttributesOwner&&
            encoder.KeepAlive(t.worldAttributesProgramsOwner)&&encoder.KeepAlive(t.worldAttributesOwner),
            "Cannot retain cached world attributes");
        // This is a full atlas refresh from the current metadata every frame.
        // Its cost is included in cached_direct, not hidden in preparation or
        // counted as a saved ray. Unbind last frame's read before this write.
        Require(encoder.BindTexture({},7),"Cannot unbind world card attribute output");
        BindTarget(encoder,t.worldAttributes);
        Require(encoder.BindUniformBuffer(surfaceCacheBuffer,SurfaceCacheGiBinding)&&
            encoder.BindTexture(surfaceCacheAttributes,12)&&encoder.BindTexture(surfaceCacheMetadata,13)&&
            encoder.BindPipeline(t.worldAttributesProgram)&&encoder.BindMesh(fullscreen)&&
            encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot decode cached card world attributes");
    }
    // BindTarget restores the primary viewport after the smaller atlas pass.
    // Opaque scene is still unchanged; write the retained direct result once.
    BindTarget(encoder,surfaceCacheSource);
    Require(encoder.BindPipeline(worldAttributes?t.worldDirectProgram:t.directProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(worldAttributes?t.worldAttributes.color:RenderResourceHandle<RHITextureSpec>{},7)&&
        encoder.BindTexture(surfaceCacheLighting.color,0)&&encoder.BindTexture(scene.color,5)&&
        encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&encoder.BindTexture(diffuseCapture.color,4)&&
        encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot resolve retained cached direct diffuse");
    Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheDirectResolve),"Cannot finish UE cached direct resolve");
    statistics.surfaceCachePasses+=1+unsigned(worldAttributes);
}
