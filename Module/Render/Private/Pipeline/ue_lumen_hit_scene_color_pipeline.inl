void PrepareUeLumenClosestHzb(){
    auto& s=ueHitSceneColor;
    if(!s.computeOwner){auto compute=std::shared_ptr<Compute::Device>(Compute::Device::Borrow(device));
        const auto caps=compute->Diagnostics();
        Require(caps.frameAvailable,"UE closest HZB requires frame compute");
        Compute::FramePipelineDesc d;d.source=SurfaceCacheDetail::UeLumenClosestHzbReduceShader();
        d.label="ue-lumen-closest-hzb-reduce";d.uniformMask=1u<<8;d.sampledMask=1u;d.storageImages=2;
        s.hzbProgram=compute->CreateFramePipeline(d);s.computeOwner=std::move(compute);}
    if(s.hzbOwner&&s.hzbWidth==width&&s.hzbHeight==height)return;
    PipelineDetail::ResourceBuilder b(device);std::vector<Target> levels;
    unsigned w=width,h=height,packedHeight=0;
    while(w>1||h>1){w=(w+1)/2;h=(h+1)/2;Target t;t.width=w;t.height=h;
        t.color=b.StorageTexture(w,h,RHITextureFormat::RGBA32F);levels.push_back(t);packedHeight+=h;}
    Target packed;packed.width=(width+1)/2;packed.height=std::max(1u,packedHeight);
    packed.color=b.StorageTexture(packed.width,packed.height,RHITextureFormat::RGBA32F);
    auto constants=b.Uniform(sizeof(PipelineDetail::UeLumenClosestHzbConstants));auto owner=b.Finish();
    s.levels=std::move(levels);s.packed=packed;s.hzbConstantsBuffer=constants;s.hzbOwner=std::move(owner);
    s.hzbWidth=width;s.hzbHeight=height;s.hzbRecorded=false;
}

// Level 0 is the original full-resolution view-position texture. UE calls it
// BaseMip=-1 with FullResDepth=true; our level 1 is UE closest-HZB mip 0.
void RecordUeLumenClosestHzb(RHIFrameEncoder& encoder){
    auto& s=ueHitSceneColor;s.hzbRecorded=false;
    Require(s.computeOwner&&s.hzbOwner&&s.hzbWidth==width&&s.hzbHeight==height,"UE closest HZB was not prepared");
    Require(encoder.KeepAlive(s.computeOwner)&&encoder.KeepAlive(s.hzbOwner),"Cannot retain UE closest HZB resources");
    unsigned sourceWidth=width,sourceHeight=height,yOffset=0;
    auto source=indirectPositions.color;
    for(unsigned level=0;level<s.levels.size();++level){const auto& output=s.levels[level];
        PipelineDetail::UeLumenClosestHzbConstants c;
        c.extent={int(sourceWidth),int(sourceHeight),int(output.width),int(output.height)};
        c.flags={level==0?1:0,int(yOffset),0,0};
        Upload(encoder,s.hzbConstantsBuffer,&c,sizeof(c),8);
        Require(encoder.BindTexture(source,0),"Cannot bind UE closest HZB reduction input");
        RHICommand::DispatchFrameCompute d;d.pipeline=s.hzbProgram;d.images[0]=output.color;d.images[1]=s.packed.color;
        d.groupsX=(output.width+7)/8;d.groupsY=(output.height+7)/8;
        Require(encoder.DispatchCompute(d),"Cannot reduce UE closest HZB level");
        source=output.color;sourceWidth=output.width;sourceHeight=output.height;yOffset+=output.height;
    }
    s.hzbRecorded=true;
}

// Included inside MaterialRenderPipeline::Impl. The root owns UE-only call
// sites and the same success/failure commit boundary as the other histories.
void PrepareUeLumenHitSceneColor(){
    Require(width&&height,"UE native-hit scene color requires a nonempty viewport");
    auto& s=ueHitSceneColor;PrepareUeLumenClosestHzb();
    if(!s.programsOwner){PipelineDetail::ResourceBuilder b(device);
        s.snapshotProgram=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::UeLumenHitSceneColorSnapshotShader());
        s.constantsBuffer=b.Uniform(sizeof(PipelineDetail::UeLumenHitSceneColorConstants));s.programsOwner=b.Finish();}
    if(s.targetsOwner&&s.sceneColor[0].width==width&&s.sceneColor[0].height==height)return;
    PipelineDetail::ResourceBuilder b(device);
    const auto target=[&](RHITextureFormat format){Target t;t.width=width;t.height=height;
        t.color=b.Texture(width,height,format);t.framebuffer=b.Target(t.color,{});return t;};
    std::array<Target,2> colors,depths,mrt;
    for(unsigned image=0;image<2;++image){colors[image]=target(RHITextureFormat::RGBA16F);
        depths[image]=target(RHITextureFormat::RGBA32F);mrt[image]=colors[image];
        mrt[image].framebuffer=b.TargetColors({colors[image].color,depths[image].color});}
    auto owner=b.Finish();s.sceneColor=colors;s.depth=depths;s.mrt=mrt;s.targetsOwner=std::move(owner);
    s.completed={};s.pending={};s.recorded=false;s.bound=false;
}

std::uint64_t UeLumenHitSceneColorLightingKey(const SurfaceCacheGiSettings& gi) const {
    // Reuse the project's byte-wise FNV history-key extension: the world cache
    // deliberately omits GI intensity, but the composited scene contains it.
    auto key=nativeProbeTransport.ueRadianceCache.pending.lightingKey;
    const auto* bytes=reinterpret_cast<const unsigned char*>(&gi.intensity);
    for(std::size_t i=0;i<sizeof(gi.intensity);++i){key^=bytes[i];key*=1099511628211ull;}
    return key;
}

void BindUeLumenHitSceneColor(RHIFrameEncoder& encoder,const SurfaceCacheGiSettings& gi,
    const PipelineCamera& camera,std::uint64_t frame,bool historyValid,bool cameraCut){
    auto& s=ueHitSceneColor;const auto& g=ueLumenHighGather;const auto& world=nativeProbeTransport.ueRadianceCache;
    Require(gi.ueLumenHigh&&s.programsOwner&&s.targetsOwner&&s.hzbOwner&&s.hzbRecorded,
        "UE diffuse screen-trace scene history or closest HZB was not prepared");
    Require(encoder.KeepAlive(s.programsOwner)&&encoder.KeepAlive(s.targetsOwner)&&encoder.KeepAlive(s.hzbOwner),
        "Cannot retain UE diffuse screen-trace history and closest HZB");
    s.pending={};s.recorded=false;s.bound=false;
    const bool currentCertified=g.motionSnapshotPrepared&&gi.cache&&gi.scene&&
        g.motionCacheGeneration==gi.cache->Generation()&&g.motionGeometryGeneration==gi.scene->GeometryGeneration()&&
        g.motion.info.x>0&&g.motion.info.x<=64;
    const bool mappingValid=currentCertified&&g.motion.info.y!=0;
    const bool keyReady=world.recorded&&world.pending.valid;
    const auto key=keyReady?UeLumenHitSceneColorLightingKey(gi):0;
    const bool sameExtent=s.completed.width==width&&s.completed.height==height;
    const bool valid=s.completed.valid&&sameExtent&&historyValid&&!cameraCut&&mappingValid&&
        keyReady&&s.completed.lightingKey==key;
    if(!valid)s.completed.valid=false;
    PipelineDetail::UeLumenHitSceneColorConstants c;
    for(auto& matrix:c.previousFromCurrent)matrix=glm::mat4(1);
    if(currentCertified)for(int owner=0;owner<g.motion.info.x;++owner)c.previousFromCurrent[owner]=g.motion.previousFromCurrent[owner];
    if(valid){c.previousViewProjection=s.completed.viewProjection;c.previousView=s.completed.view;}
    c.flags={valid?1:0,currentCertified?g.motion.info.x:0,int(frame%8u),int(width)};
    c.camera=glm::vec4(glm::vec3(glm::inverse(camera.view)[3]),float(height));
    s.recordedConstants=c;s.boundLightingKey=key;
    Upload(encoder,s.constantsBuffer,&c,sizeof(c),11);
    const unsigned read=s.completed.image;
    Require(encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(s.depth[read].color,3)&&
        encoder.BindTexture(s.sceneColor[read].color,6)&&encoder.BindTexture(s.packed.color,11),
        "Cannot bind UE previous full scene color, depth and current closest HZB");
    s.bound=true;
}

void RecordUeLumenHitSceneColorHistory(RHIFrameEncoder& encoder,const SurfaceCacheGiSettings& gi,
    const PipelineCamera& camera,const Target& baseSceneColor,const Target& finalReflectedGI){
    auto& s=ueHitSceneColor;const auto& g=ueLumenHighGather;const auto& world=nativeProbeTransport.ueRadianceCache;
    Require(gi.ueLumenHigh&&s.bound&&s.programsOwner&&s.targetsOwner,"UE scene-color history inputs were not bound");
    Require(baseSceneColor.width==width&&baseSceneColor.height==height&&finalReflectedGI.width==width&&
        finalReflectedGI.height==height,"UE scene-color history requires full-resolution linear HDR inputs");
    Require(encoder.KeepAlive(s.programsOwner)&&encoder.KeepAlive(s.targetsOwner),"Cannot retain UE scene-color snapshot");
    const unsigned write=s.completed.valid?1u-s.completed.image:0u;
    // Explicitly remove all snapshot aliases left by tracing before making
    // either image an attachment. The opposite completed image remains intact.
    for(unsigned slot:{0u,1u,2u,3u,6u})Require(encoder.BindTexture({},slot),"Cannot unbind UE scene-color snapshot aliases");
    BindTarget(encoder,s.mrt[write]);
    Upload(encoder,s.constantsBuffer,&s.recordedConstants,sizeof(s.recordedConstants),11);
    // PlacementData supplies the current projection and inverse view. Restore
    // its immutable snapshot after any intervening GI/temporal programs.
    Upload(encoder,placementBuffer,&placementFrameConstants,sizeof(placementFrameConstants),ScreenProbePlacementBinding);
    Require(encoder.BindPipeline(s.snapshotProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(baseSceneColor.color,0)&&encoder.BindTexture(finalReflectedGI.color,1)&&
        encoder.BindTexture(indirectPositions.color,2)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),
        "Cannot snapshot UE native-hit scene color and depth");
    const bool currentCertified=g.motionSnapshotPrepared&&gi.cache&&gi.scene&&
        g.motionCacheGeneration==gi.cache->Generation()&&g.motionGeometryGeneration==gi.scene->GeometryGeneration()&&
        g.motion.info.x>0&&g.motion.info.x<=64;
    const bool keyReady=world.recorded&&world.pending.valid;
    const auto key=keyReady?UeLumenHitSceneColorLightingKey(gi):0;
    s.pending={};s.pending.valid=currentCertified&&keyReady&&key==s.boundLightingKey;
    s.pending.image=write;s.pending.width=width;s.pending.height=height;
    s.pending.viewProjection=camera.projection*camera.view;s.pending.view=camera.view;s.pending.lightingKey=key;
    s.recorded=true;
}

void CommitUeLumenHitSceneColor(bool success){
    auto& s=ueHitSceneColor;
    if(s.recorded&&success)s.completed=s.pending;
    else if(!success||s.bound)s.completed={};
    s.pending={};s.recorded=false;s.bound=false;s.hzbRecorded=false;
}

void ResetUeLumenHitSceneColorHistory(){
    auto& s=ueHitSceneColor;s.completed={};s.pending={};s.bound=false;s.recorded=false;s.hzbRecorded=false;
    s.boundLightingKey=0;s.recordedConstants.flags.x=0;s.recordedConstants.flags.y=0;
}
