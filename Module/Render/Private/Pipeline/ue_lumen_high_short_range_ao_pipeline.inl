// Included inside MaterialRenderPipeline::Impl. Include the independent state
// declaration outside Impl and add ueHighAO (see ue_lumen_high..._state.h).
// Root owns all call sites, GI metadata production, and frame submission.
// No function here changes legacy GI state, its flags, or its history.
void PrepareUeLumenHighSampling(
    const std::filesystem::path& noiseDirectory=std::filesystem::path("Assets/UE57/BlueNoise")) {
    auto& a=ueHighAO;
    if(a.samplingOwner)return;
    const auto scalar=PipelineDetail::LoadUeLumenHighBlueNoise(noiseDirectory,
        "FastBlueNoise_scalar_128x128x64.r8",128u*8192u);
    const auto vector=PipelineDetail::LoadUeLumenHighBlueNoise(noiseDirectory,
        "FastBlueNoise_vec2_128x128x64.rgba8",128u*8192u*4u);
    PipelineDetail::ResourceBuilder b(device);
    auto texture=[&](const void* bytes,RHITextureFormat format) {
        CreateRHITextureSpec desc;desc.width=128;desc.height=8192;
        desc.textureDataStoreType=desc.textureUseType=format;desc.data=bytes;
        desc.mipmaps=false;desc.filterMode=RHIFilterMode::Nearest;desc.addressMode=RHIAddressMode::Repeat;
        return b.Create<RHITextureSpec>([&](auto callback){device.async_CreateTexture(desc,callback);});
    };
    a.blueNoiseScalar=texture(scalar.data(),RHITextureFormat::R8_UNORM);
    a.blueNoiseVec2=texture(vector.data(),RHITextureFormat::RGBA8);
    a.samplingBuffer=b.Uniform(sizeof(PipelineDetail::UeLumenHighSamplingConstants));
    a.samplingOwner=b.Finish();
}

// Public to the enclosing Impl: GI gather/temporal and AO all use this binding,
// the same sourceFullResPixel and the same original UE 128x8192 noise atlases.
void BindUeLumenHighSampling(RHIFrameEncoder& encoder,const PipelineCamera& camera,
    std::uint64_t sequence,bool historyValid,unsigned scalarSlot=6,unsigned vec2Slot=7) {
    auto& a=ueHighAO;
    Require(a.samplingOwner&&scalarSlot<16&&vec2Slot<16&&scalarSlot!=vec2Slot,
        "UE High sampling was not prepared");
    Require(encoder.KeepAlive(a.samplingOwner),"Cannot retain original UE BlueNoise resources");
    PipelineDetail::UeLumenHighSamplingConstants c;
    c.inverseView=glm::inverse(camera.view);c.inverseProjection=glm::inverse(camera.projection);c.view=camera.view;
    c.extent={int(width),int(height),int((width+1)/2),int((height+1)/2)};
    c.frame={std::uint32_t(sequence),historyValid?1u:0u,0u,0u};
    Upload(encoder,a.samplingBuffer,&c,sizeof(c),14);
    Require(encoder.BindTexture(a.blueNoiseScalar,scalarSlot)&&encoder.BindTexture(a.blueNoiseVec2,vec2Slot),
        "Cannot bind original UE BlueNoise assets");
}

void PrepareUeLumenHighShortRangeAO(
    const std::filesystem::path& noiseDirectory=std::filesystem::path("Assets/UE57/BlueNoise")) {
    Require(width&&height,"UE High AO requires a nonempty viewport");
    PrepareUeLumenHighSampling(noiseDirectory);
    auto& a=ueHighAO;
    if(!a.programsOwner){
        PipelineDetail::ResourceBuilder b(device);const auto vertex=FullscreenVertexShader();
        a.furthestHzbProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::UeLumenHighFurthestHzbShader());
        a.rawAOProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::UeLumenHighShortRangeAOShader());
        a.temporalAOProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::UeLumenHighShortRangeAOTemporalShader());
        a.compositeProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::UeLumenHighShortRangeAOCompositeShader());
        a.programsOwner=b.Finish();
    }
    if(!a.targetsOwner||a.compositeGI.width!=width||a.compositeGI.height!=height){
        PipelineDetail::ResourceBuilder b(device);
        auto target=[&](unsigned w,unsigned h,RHITextureFormat format){
            Target t;t.width=w;t.height=h;t.color=b.Texture(w,h,format);t.framebuffer=b.Target(t.color,{});return t;
        };
        auto hz=target((width+1)/2,(height+1)/2,RHITextureFormat::RGBA32F);
        auto raw=target((width+1)/2,(height+1)/2,RHITextureFormat::R8_UNORM);
        std::array<Target,2> history{{target(width,height,RHITextureFormat::R8_UNORM),
            target(width,height,RHITextureFormat::R8_UNORM)}};
        auto composite=target(width,height,RHITextureFormat::RGBA32F);
        auto owner=b.Finish();a.furthestHzb=hz;a.rawAO=raw;a.history=history;a.compositeGI=composite;
        a.targetsOwner=std::move(owner);a.historyValid=false;a.pending=false;
    }
}

// Call after the current full-resolution GBuffer capture. Produces raw HALF AO
// only, so it may run before GI temporal. HZB uses the same capture and no rays.
const Target& RecordUeLumenHighShortRangeAO(RHIFrameEncoder& encoder,
    const PipelineCamera& camera,std::uint64_t sequence,
    RenderResourceHandle<RHITextureSpec> currentViewPositions,
    RenderResourceHandle<RHITextureSpec> currentWorldNormals) {
    auto& a=ueHighAO;
    Require(a.programsOwner&&a.targetsOwner,"UE High AO targets were not prepared");
    Require(encoder.KeepAlive(a.programsOwner)&&encoder.KeepAlive(a.targetsOwner),"Cannot retain UE High AO targets");
    Require(encoder.BindTexture({},0),"Cannot unbind UE High HZB input");
    BindTarget(encoder,a.furthestHzb);
    BindUeLumenHighSampling(encoder,camera,sequence,false);
    Require(encoder.BindPipeline(a.furthestHzbProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(currentViewPositions,2)&&encoder.BindTexture(currentWorldNormals,3)&&
        encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot build UE High furthest HZB mip0");
    BindTarget(encoder,a.rawAO);
    BindUeLumenHighSampling(encoder,camera,sequence,false);
    Require(encoder.BindPipeline(a.rawAOProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(a.furthestHzb.color,0)&&encoder.BindTexture(currentViewPositions,2)&&
        encoder.BindTexture(currentWorldNormals,3)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),
        "Cannot record UE High scalar short-range AO");
    return a.rawAO;
}

// Call after the current GI temporal pass, using that pass's *actual* metadata.
// giHistory RGBA=(unmodulated E/pi*intensity RGB, UE frame count).
// giAux.y is actual current readability; metadata xy/alpha/valid bits is fullres.
// historyImage must be the same ping-pong image as the GI frame. No count from
// GI's A is treated as coverage, and neither AO nor rho enters GI RGB history.
const Target& RecordUeLumenHighAOHistoryAndComposite(RHIFrameEncoder& encoder,
    const PipelineCamera& camera,std::uint64_t sequence,bool giHistoryValid,unsigned historyImage,
    RenderResourceHandle<RHITextureSpec> currentViewPositions,
    RenderResourceHandle<RHITextureSpec> currentWorldNormals,
    RenderResourceHandle<RHITextureSpec> giHistory,
    RenderResourceHandle<RHITextureSpec> giAux,
    RenderResourceHandle<RHITextureSpec> giMetadata,
    RenderResourceHandle<RHITextureSpec> diffuseColor) {
    auto& a=ueHighAO;
    Require(historyImage<2&&a.targetsOwner&&a.programsOwner,"UE High AO temporal was not prepared");
    Require(encoder.KeepAlive(a.programsOwner)&&encoder.KeepAlive(a.targetsOwner),"Cannot retain UE High AO histories");
    const unsigned previous=1-historyImage;
    const bool valid=giHistoryValid&&a.historyValid&&a.completedImage==previous;
    BindTarget(encoder,a.history[historyImage]);
    BindUeLumenHighSampling(encoder,camera,sequence,valid);
    Require(encoder.BindPipeline(a.temporalAOProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(a.rawAO.color,0)&&encoder.BindTexture(a.history[previous].color,1)&&
        encoder.BindTexture(currentViewPositions,2)&&encoder.BindTexture(currentWorldNormals,3)&&
        encoder.BindTexture(giMetadata,4)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),
        "Cannot record UE High scalar AO history with shared GI temporal decision");
    BindTarget(encoder,a.compositeGI);
    Require(encoder.BindPipeline(a.compositeProgram)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(giHistory,0)&&encoder.BindTexture(a.history[historyImage].color,1)&&
        encoder.BindTexture(diffuseColor,2)&&encoder.BindTexture(giAux,3)&&
        encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot compose UE High diffuse GI and AO multibounce");
    a.pending=true;a.pendingImage=historyImage;
    return a.compositeGI;
}

void CompleteUeLumenHighShortRangeAO(bool success) {
    auto& a=ueHighAO;
    if(success&&a.pending){a.completedImage=a.pendingImage;a.historyValid=true;}
    else if(!success)a.historyValid=false;
    a.pending=false;
}
void ResetUeLumenHighShortRangeAOHistory(){ueHighAO.historyValid=false;ueHighAO.pending=false;}
