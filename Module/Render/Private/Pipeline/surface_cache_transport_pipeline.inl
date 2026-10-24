// World-space transport persists across camera changes. Complete current GI
// rays correct contact regions and covered cache holes at actual receivers.
struct TransportConstants {glm::ivec4 schedule;glm::vec4 parameters;glm::vec4 launchBias{0};glm::vec4 receiverOptions{4,0,.1f,0};glm::mat4 viewProjection{1};std::array<glm::vec4,512> bounds{};};
void WaitTransportPrecisionUploads(){
    const auto done=transportPrecisionUploads;if(!done)return;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(done->pending){device.returnSystem.DrainCallbacks();Require(std::chrono::steady_clock::now()<deadline,"Geometry residual upload timed out");
        if(done->pending)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(1));}
    // Preserve a failed completion so later prepares cannot silently accept
    // keys published by the enqueue-only path after an unsuccessful upload.
    Require(done->success,"Cannot upload geometry precision residuals");
    transportPrecisionUploads.reset();
}
void PrepareTransportPrecision(const SurfaceCacheGiSettings& s,bool enqueueOnly=false){
    // Ordinarily this is empty. An interrupted/failed preceding prepare still
    // has to acknowledge all callbacks before reusing precision state.
    WaitTransportPrecisionUploads();
    const auto geometrySnapshot=s.scene->PackedGeometry();
    const auto& geometry=*geometrySnapshot;const auto& inputs=s.scene->Instances();
    const auto& order=geometry.InputTriangleIndices();
    const auto size=((std::size_t(geometry.TriangleCount())*3+1023)/1024)*1024;
    const bool sameTopology=transportPrecisionGeometry&&
        transportPrecisionGeometry->TopologyKey()==geometry.TopologyKey()&&
        transportPrecisionGeometry->TriangleCount()==geometry.TriangleCount();
    if(transportPrecisionOwner&&transportPrecisionScene==s.scene.get()&&
        transportPrecisionGeneration==s.scene->GeometryGeneration()&&sameTopology&&
        transportPrecisionPixels.size()==size&&transportPrecisionForInput.size()==order.size()&&
        transportPrecisionInputs.size()==inputs.size())return;
    const bool full=!transportPrecisionOwner||transportPrecisionPixels.size()!=size||transportPrecisionScene!=s.scene.get()||
        !sameTopology||transportPrecisionForInput.size()!=order.size()||transportPrecisionInputs.size()!=inputs.size();
    if(full){transportPrecisionPixels.assign(size,glm::vec4(0));transportPrecisionForInput.resize(order.size());
        for(std::size_t packed=0;packed<order.size();++packed)transportPrecisionForInput[order[packed]]=unsigned(packed);}
    const auto mesh=[](const ProbeSdfInstance& i){return i.geometryMesh?i.geometryMesh:i.mesh;};
    std::size_t offset=0,first=size,last=0;
    for(std::size_t object=0;object<inputs.size();++object){const auto& input=inputs[object];const auto& triangles=mesh(input)->Triangles();
        bool same=!full&&mesh(input)==mesh(transportPrecisionInputs[object]);
        if(same)for(unsigned column=0;column<4;++column)same&=input.localToWorld[column]==transportPrecisionInputs[object].localToWorld[column];
        if(!same){const glm::dmat4 transform(input.localToWorld);
            for(std::size_t local=0;local<triangles.size();++local){const auto packed=transportPrecisionForInput[offset+local];const auto& t=triangles[local];
                const std::array<glm::vec3,3> vertices{{t.a,t.b,t.c}};
                for(unsigned v=0;v<3;++v){const auto exact=glm::dvec3(transform*glm::dvec4(vertices[v],1));
                    const auto high=glm::dvec3(geometry.Pixels()[geometry.NodeCount()*2+packed*5+v]);
                    const auto low=glm::vec3(exact-high);Require(glm::length(exact-high-glm::dvec3(low))<1e-10,"Geometry precision residual lost source coordinates");
                    const auto at=std::size_t(packed)*3+v;transportPrecisionPixels[at]=glm::vec4(low,0);first=std::min(first,at);last=std::max(last,at);}
            }}offset+=triangles.size();}
    Require(offset==order.size(),"Precision geometry extraction order differs from BVH");
    if(!transportPrecisionOwner||full){PipelineDetail::ResourceBuilder b(device);CreateRHITextureSpec d;d.width=1024;d.height=unsigned(size/1024);
        d.data=transportPrecisionPixels.data();d.textureDataStoreType=d.textureUseType=RHITextureFormat::RGBA32F;
        d.mipmaps=false;d.filterMode=RHIFilterMode::Nearest;d.addressMode=RHIAddressMode::ClampToEdge;
        transportPrecision=b.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(d,cb);});transportPrecisionOwner=b.Finish();
    }else if(first<size){auto done=std::make_shared<TransportPrecisionUploadCompletion>();transportPrecisionUploads=done;
        // BVH sorting can scatter one moving instance across distant rows.
        // Upload only rows containing changed residuals, retaining all current
        // CPU values for unchanged texels that share an uploaded row.
        std::vector<std::uint8_t> dirtyRows(size/1024,0);std::size_t inputOffset=0;
        for(std::size_t object=0;object<inputs.size();++object){const auto& input=inputs[object];const auto& triangles=mesh(input)->Triangles();
            bool same=mesh(input)==mesh(transportPrecisionInputs[object]);
            if(same)for(unsigned column=0;column<4;++column)same&=input.localToWorld[column]==transportPrecisionInputs[object].localToWorld[column];
            if(!same)for(std::size_t local=0;local<triangles.size();++local){const auto at=std::size_t(transportPrecisionForInput[inputOffset+local])*3;
                dirtyRows[at/1024]=dirtyRows[(at+2)/1024]=1;}
            inputOffset+=triangles.size();}
        for(unsigned row=0;row<dirtyRows.size();){if(!dirtyRows[row]){++row;continue;}const auto firstRow=row;
            while(row<dirtyRows.size()&&dirtyRows[row])++row;
            UpdateRHITextureDesc d;d.y=firstRow;d.width=1024;d.height=row-firstRow;
            d.format=RHITextureFormat::RGBA32F;d.deferUntilFrame=s.deferResourceUploads;
            d.data=transportPrecisionPixels.data()+std::size_t(d.y)*1024;d.byteSize=std::size_t(d.width)*d.height*sizeof(glm::vec4);
            ++done->pending;device.async_UpdateTexture(transportPrecision,d,[done](bool ok){done->success&=ok;--done->pending;});}
        if(!enqueueOnly)WaitTransportPrecisionUploads();}
    transportPrecisionInputs=inputs;transportPrecisionGeometry=geometrySnapshot;
    transportPrecisionScene=s.scene.get();transportPrecisionGeneration=s.scene->GeometryGeneration();
}
void PrepareTransportReceiverCompaction(const SurfaceCacheGiSettings& s){
    if(!s.compactReceiverTracing)return;
    Require(s.coherentReceiverTracing&&s.sparseReceiverSampling,"Compact receivers require coherent sparse transport");
    const auto capacity=std::uint64_t(width)*height;
    const bool supportHalo=s.nativeReceiverFilterSupportHalo&&s.nativeScreenProbeTransport&&s.sparseReceiverSampling;
    Require(capacity>0&&capacity<=65535u*64u,"Compact receiver queue exceeds portable indirect dispatch range");
    if(!transportCompactOwner||transportCompactHalfRadiance!=s.halfReceiverRadiance||transportCompactDeferredMix!=s.deferredReceiverMix||
        nativeProbeTransport.compactPipelineUsesNativeBase!=s.nativeScreenProbeTransport||
        nativeProbeTransport.compactPipelineUsesAdaptiveFresh!=s.nativeAdaptiveFreshSampling||
        nativeProbeTransport.compactPipelineUsesForceRasterCore!=s.nativeForceRasterCore||
        nativeProbeTransport.compactPipelineUsesSupportHalo!=supportHalo){
        auto owner=std::shared_ptr<Compute::Device>(Compute::Device::Borrow(device));
        const auto caps=owner->Diagnostics();
        Require(caps.frameAvailable&&capacity<=caps.maxStorageBufferBytes/sizeof(std::uint32_t),"Compact receiver storage unavailable");
        Compute::FramePipelineDesc d;d.source=SurfaceCacheDetail::TransportReceiverCompactResetShader();
        d.label="surface-cache-receiver-reset";d.storageBuffers=1;d.pushConstantBytes=4;
        transportCompactReset=owner->CreateFramePipeline(d);
        d={};d.source=SurfaceCacheDetail::TransportReceiverCompactClassifyShader(s.halfReceiverRadiance,s.nativeForceRasterCore,supportHalo);d.label="surface-cache-receiver-classify";
        d.uniformMask=(1u<<12)|(1u<<14);d.sampledMask=(1u<<2)|(1u<<3);d.storageBuffers=2;d.storageImages=4;
        transportCompactClassify=owner->CreateFramePipeline(d);
        d.source=s.nativeScreenProbeTransport?SurfaceCacheDetail::NativeBaseTransportCompactTraceShader(s.halfReceiverRadiance,s.nativeAdaptiveFreshSampling,s.nativeForceRasterCore):
            SurfaceCacheDetail::TransportReceiverCompactTraceShader(s.halfReceiverRadiance,s.deferredReceiverMix);d.label="surface-cache-receiver-trace";
        d.uniformMask=(1u<<9)|(1u<<12)|(1u<<14)|(1u<<15);
        d.sampledMask=(1u<<2)|(1u<<3)|(1u<<4)|(1u<<9)|(1u<<12)|(1u<<13)|(1u<<14)|(1u<<15);
        if(s.nativeScreenProbeTransport)d.sampledMask|=(1u<<0)|(1u<<8);
        if(s.nativeAdaptiveFreshSampling)d.sampledMask|=1u<<11;
        if(s.nativeForceRasterCore)d.sampledMask|=1u<<5;
        d.storageImages=3;d.nativeScene=Compute::NativeSceneDesc{};
        transportCompactTrace=owner->CreateFramePipeline(d);transportCompactOwner=std::move(owner);transportCompactHalfRadiance=s.halfReceiverRadiance;transportCompactDeferredMix=s.deferredReceiverMix;
        nativeProbeTransport.compactPipelineUsesNativeBase=s.nativeScreenProbeTransport;
        nativeProbeTransport.compactPipelineUsesAdaptiveFresh=s.nativeAdaptiveFreshSampling;
        nativeProbeTransport.compactPipelineUsesForceRasterCore=s.nativeForceRasterCore;
        nativeProbeTransport.compactPipelineUsesSupportHalo=supportHalo;
    }
    if(!transportCompactDataOwner||transportCompactCapacity!=capacity){
        auto owner=std::shared_ptr<Compute::Device>(Compute::Device::Borrow(device));
        Require(capacity<=owner->Diagnostics().maxStorageBufferBytes/sizeof(std::uint32_t),"Compact receiver queue exceeds storage-buffer range");
        transportCompactControl=owner->CreateStorageBuffer(16*sizeof(std::uint32_t),nullptr,"surface-cache-receiver-control");
        transportCompactQueue=owner->CreateStorageBuffer(std::size_t(capacity)*sizeof(std::uint32_t),nullptr,"surface-cache-receiver-queue");
        transportCompactCapacity=std::uint32_t(capacity);transportCompactDataOwner=std::move(owner);
    }
}
bool NativeReceiverSupportHaloActive(const SurfaceCacheGiSettings& s) const {
    return s.nativeReceiverFilterSupportHalo&&s.nativeScreenProbeTransport&&s.compactReceiverTracing&&s.sparseReceiverSampling;
}
void PrepareNativeReceiverSupportHalo(const SurfaceCacheGiSettings& s){
    if(!NativeReceiverSupportHaloActive(s))return;
    auto& n=nativeProbeTransport;
    if(!n.receiverHaloComputeOwner){
        auto owner=std::shared_ptr<Compute::Device>(Compute::Device::Borrow(device));
        Require(owner->Diagnostics().frameAvailable,"Native receiver support halo requires frame compute");
        Compute::FramePipelineDesc d;d.source=SurfaceCacheDetail::TransportReceiverSupportHaloShader(false);d.label="surface-cache-receiver-halo-horizontal";
        d.uniformMask=(1u<<12)|(1u<<14);d.sampledMask=(1u<<2)|(1u<<3)|(1u<<5);d.storageImages=1;
        n.receiverHaloHorizontal=owner->CreateFramePipeline(d);
        d.source=SurfaceCacheDetail::TransportReceiverSupportHaloShader(true);d.label="surface-cache-receiver-halo-vertical-queue";d.storageBuffers=2;
        n.receiverHaloVertical=owner->CreateFramePipeline(d);n.receiverHaloComputeOwner=std::move(owner);
    }
    if(!n.receiverHaloTargetsOwner||n.receiverHaloScratch.width!=width||n.receiverHaloScratch.height!=height){
        PipelineDetail::ResourceBuilder b(device);Target scratch;scratch.width=width;scratch.height=height;
        scratch.color=b.StorageTexture(width,height,RHITextureFormat::RGBA32F);
        n.receiverHaloScratch=scratch;n.receiverHaloTargetsOwner=b.Finish();
    }
    if(s.contact.collectSamplingDiagnostics&&(!n.receiverHaloDiagnosticOwner||
        n.receiverHaloDiagnosticAdaptiveFresh!=s.nativeAdaptiveFreshSampling||n.receiverHaloDiagnosticForceRasterCore!=s.nativeForceRasterCore)){
        PipelineDetail::ResourceBuilder b(device);
        n.receiverHaloDiagnosticProgram=b.FullscreenPipeline(FullscreenVertexShader(),
            SurfaceCacheDetail::NativeBaseTransportRayDiagnosticShader(s.nativeAdaptiveFreshSampling,s.nativeForceRasterCore,true));
        n.receiverHaloDiagnosticOwner=b.Finish();n.receiverHaloDiagnosticAdaptiveFresh=s.nativeAdaptiveFreshSampling;
        n.receiverHaloDiagnosticForceRasterCore=s.nativeForceRasterCore;
    }
}
void RecordNativeReceiverSupportHalo(RHIFrameEncoder& encoder){
    auto& n=nativeProbeTransport;
    Require(encoder.KeepAlive(n.receiverHaloComputeOwner)&&encoder.KeepAlive(n.receiverHaloTargetsOwner),"Cannot retain native receiver support halo");
    Require(encoder.BindTexture(transportClassification.color,5),"Cannot bind original receiver classification for halo");
    RHICommand::DispatchFrameCompute horizontal;horizontal.pipeline=n.receiverHaloHorizontal;
    horizontal.images[0]=n.receiverHaloScratch.color;horizontal.groupsX=(transportCompactCapacity+63)/64;
    Require(encoder.DispatchCompute(horizontal),"Cannot expand horizontal receiver support");
    Require(encoder.BindTexture(n.receiverHaloScratch.color,5),"Cannot bind horizontal receiver support");
    RHICommand::DispatchFrameCompute vertical;vertical.pipeline=n.receiverHaloVertical;
    vertical.images[0]=transportClassification.color;vertical.groupsX=horizontal.groupsX;
    vertical.buffers[0]=transportCompactControl;vertical.buffers[1]=transportCompactQueue;
    Require(encoder.DispatchCompute(vertical),"Cannot expand and queue vertical receiver support");
    Require(encoder.BindTexture(transportClassification.color,5),"Cannot bind complete receiver support classification");
}
void PrepareSurfaceTransport(const SurfaceCacheGiSettings& s) {
    if(!s.splitReceiverGather)return;
    Require(surfaceCacheHardwareSupported,"Split transport requires native Vulkan ray queries");
    PrepareTransportPrecision(s);
    PrepareTransportReceiverCompaction(s);
    PrepareNativeReceiverSupportHalo(s);
    const auto grid=std::min(std::clamp(width/320u,1u,4u),s.receiverLatticeMax);
    const auto lowWidth=(width+grid-1)/grid,lowHeight=(height+grid-1)/grid;
    const auto radianceFormat=s.halfReceiverRadiance?RHITextureFormat::RGBA16F:RHITextureFormat::RGBA32F;
    if(!transportViewOwner||transportMask.width!=width||transportMask.height!=height||transportReceiverTarget.color!=surfaceCacheRaw.color||transportLowFrequency.width!=lowWidth||transportLowFrequency.height!=lowHeight||transportCompactView!=s.compactReceiverTracing||transportHalfReceiverRadiance!=s.halfReceiverRadiance||transportDeferredView!=s.deferredReceiverMix){
        PipelineDetail::ResourceBuilder b(device);transportMask.width=width;transportMask.height=height;
        const auto receiverTexture=[&](RHITextureFormat format=RHITextureFormat::RGBA32F){return s.compactReceiverTracing?b.StorageTexture(width,height,format):b.Texture(width,height,format);};
        transportMask.color=receiverTexture();transportMask.framebuffer=b.Target(transportMask.color,{});
        transportSamplingMask=transportMask;
        transportClassification.width=width;transportClassification.height=height;
        transportClassification.color=receiverTexture();
        transportClassification.framebuffer=b.Target(transportClassification.color,{});
        transportBase.width=width;transportBase.height=height;transportBase.color=receiverTexture(radianceFormat);
        transportCompactRaw={};if(s.compactReceiverTracing){transportCompactRaw.width=width;transportCompactRaw.height=height;transportCompactRaw.color=receiverTexture(radianceFormat);}
        transportReconstructed.width=width;transportReconstructed.height=height;transportReconstructed.color=b.Texture(width,height,radianceFormat);
        transportReconstructedMask.width=width;transportReconstructedMask.height=height;transportReconstructedMask.color=b.Texture(width,height,RHITextureFormat::RGBA32F);
        transportReconstructedBase={};if(s.deferredReceiverMix){transportReconstructedBase.width=width;transportReconstructedBase.height=height;
            transportReconstructedBase.color=b.Texture(width,height,radianceFormat);}
        std::array<RenderResourceHandle<RHITextureSpec>,4> reconstructed{{transportReconstructed.color,transportReconstructedMask.color,transportReconstructedBase.color,{}}};
        transportReconstructed.framebuffer=b.TargetColors(reconstructed);
        for(auto* low:{&transportLowFrequency,&transportLowFrequencyMask,&transportLowFrequencyBase}){
            low->width=lowWidth;low->height=lowHeight;low->color=b.Texture(lowWidth,lowHeight,low==&transportLowFrequencyMask?RHITextureFormat::RGBA32F:radianceFormat);}
        std::array<RenderResourceHandle<RHITextureSpec>,4> lowColors{{transportLowFrequency.color,transportLowFrequencyMask.color,transportLowFrequencyBase.color,{}}};
        transportLowFrequency.framebuffer=b.TargetColors(lowColors);
        transportReceiverTarget=surfaceCacheRaw;std::array<RenderResourceHandle<RHITextureSpec>,4> colors{{surfaceCacheRaw.color,transportMask.color,transportBase.color,{}}};
        transportReceiverTarget.framebuffer=b.TargetColors(colors);transportViewOwner=b.Finish();transportCompactView=s.compactReceiverTracing;transportHalfReceiverRadiance=s.halfReceiverRadiance;transportDeferredView=s.deferredReceiverMix;}
    if(!transportReceiverScratch.color.IsValid()||transportReceiverScratch.width!=width||transportReceiverScratch.height!=height||transportReceiverScratchHalfRadiance!=s.halfReceiverRadiance){
        PipelineDetail::ResourceBuilder b(device);transportReceiverScratch.width=width;transportReceiverScratch.height=height;
        transportReceiverScratch.color=b.Texture(width,height,radianceFormat);transportReceiverScratch.framebuffer=b.Target(transportReceiverScratch.color,{});
        transportReceiverFilterOwner=b.Finish();transportReceiverScratchHalfRadiance=s.halfReceiverRadiance;}
    if(!transportProgramsOwner){PipelineDetail::ResourceBuilder b(device);
        transportUpdate=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::TransportUpdateShader());
        transportUpdateCoherent=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::TransportUpdateShader(true));
        transportResolve=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::TransportResolveShader());
        transportResolveCore=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::TransportResolveShader(false,1));
        transportResolveLow=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::TransportResolveShader(false,2));
        transportResolveFull=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::TransportResolveShader(false,3));
        transportClassify=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::TransportReceiverClassificationShader());
        transportDirect=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::TransportResolveShader(true));
        transportFilter=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::TransportFilterShader());
        transportReceiverFilter=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::TransportReceiverFilterShader());
        transportReceiverPhysicalFilter=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::TransportReceiverFilterShader(true));
        transportReconstructionProgram=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::TransportReconstructionShader());
        transportDeferredCore=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::TransportDeferredResolveShader(1));
        transportDeferredLow=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::TransportDeferredResolveShader(2));
        transportDeferredFull=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::TransportDeferredResolveShader(3));
        transportDeferredReconstruction=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::TransportDeferredReconstructionShader());
        transportDeferredFilter=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::TransportDeferredReceiverFilterShader());
        transportReceiverFilterBuffer=b.Uniform(sizeof(SurfaceCacheContactConstants));
        transportBuffer=b.Uniform(sizeof(TransportConstants));transportPlacementBuffer=b.Uniform(sizeof(ScreenProbePlacementConstants));
        transportProgramsOwner=b.Finish();}
    if(s.nativeForceRasterCore&&(!nativeProbeTransport.forceRasterCoreProgramsOwner||
        nativeProbeTransport.forceRasterCoreProgramsAdaptiveFresh!=s.nativeAdaptiveFreshSampling)){
        auto& n=nativeProbeTransport;PipelineDetail::ResourceBuilder b(device);const auto vertex=FullscreenVertexShader();
        n.forceRasterCoreProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::NativeBaseTransportResolveShader(1,s.nativeAdaptiveFreshSampling,true));
        n.forceRasterLowProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::NativeBaseTransportResolveShader(2,s.nativeAdaptiveFreshSampling,true));
        n.forceRasterFullProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::NativeBaseTransportResolveShader(3,s.nativeAdaptiveFreshSampling,true));
        n.forceRasterReconstructionProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::NativeBaseTransportReconstructionShader(s.nativeAdaptiveFreshSampling,true));
        n.forceRasterDiagnosticProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::NativeBaseTransportRayDiagnosticShader(s.nativeAdaptiveFreshSampling,true));
        n.forceRasterClassificationProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::TransportReceiverRasterCoreClassificationShader());
        n.forceRasterCoreProgramsOwner=b.Finish();n.forceRasterCoreProgramsAdaptiveFresh=s.nativeAdaptiveFreshSampling;
    }
    if(!transportOwner||transport[0].width!=surfaceCacheLighting.width||transport[0].height!=surfaceCacheLighting.height){
        PipelineDetail::ResourceBuilder b(device);
        for(unsigned image=0;image<2;++image){auto& target=transport[image];target.width=surfaceCacheLighting.width;target.height=surfaceCacheLighting.height;
            std::array<RenderResourceHandle<RHITextureSpec>,4> colors{};target.color=colors[0]=b.Texture(target.width,target.height,RHITextureFormat::RGBA32F);
            for(unsigned axis=0;axis<3;++axis)transportCoefficients[image][axis]=colors[axis+1]=b.Texture(target.width,target.height,RHITextureFormat::RGBA32F);
            target.framebuffer=b.TargetColors(colors);}
        transportFiltered.width=surfaceCacheLighting.width;transportFiltered.height=surfaceCacheLighting.height;
        std::array<RenderResourceHandle<RHITextureSpec>,4> colors{};
        transportFiltered.color=colors[0]=b.Texture(transportFiltered.width,transportFiltered.height,RHITextureFormat::RGBA32F);
        for(unsigned axis=0;axis<3;++axis)transportFilteredCoefficients[axis]=colors[axis+1]=b.Texture(transportFiltered.width,transportFiltered.height,RHITextureFormat::RGBA32F);
        transportFiltered.framebuffer=b.TargetColors(colors);
        transportOwner=b.Finish();transportCompleted={};}
    if(s.contact.collectSamplingDiagnostics){
        if(!transportDiagnosticProgramsOwner){PipelineDetail::ResourceBuilder b(device);
            for(unsigned i=0;i<2;++i)transportDiagnosticPrograms[i]=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::TransportRayDiagnosticShader(i!=0));
            transportDiagnosticPrograms[2]=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::TransportRayDiagnosticShader(true,true));
            transportDiagnosticPrograms[3]=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::TransportRayDiagnosticShader(false,true));
            transportDiagnosticProgramsOwner=b.Finish();}
        const std::array<unsigned,2> heights{{unsigned((std::uint64_t(s.cache->TexelCount())*s.transportRays*4+4095)/4096),
            unsigned((std::uint64_t(width)*height*s.contact.raysPerPixel*4+4095)/4096)}};
        if(!transportDiagnosticOwner||transportDiagnostics[0].height!=heights[0]||transportDiagnostics[1].height!=heights[1]){
            PipelineDetail::ResourceBuilder b(device);for(unsigned i=0;i<2;++i){Require(heights[i]<=8192,"Transport diagnostic atlas exceeds supported extent");
                auto& t=transportDiagnostics[i];t.width=4096;t.height=heights[i];t.color=b.Texture(t.width,t.height,RHITextureFormat::RGBA32F);t.framebuffer=b.Target(t.color,{});}
            transportDiagnosticOwner=b.Finish();}
        if(s.nativeAdaptiveFreshSampling){auto& n=nativeProbeTransport;
            if(!n.receiverDiagnosticPagesOwner||n.receiverDiagnosticPages[0].color!=transportDiagnostics[1].color){
                PipelineDetail::ResourceBuilder b(device);n.receiverDiagnosticPages[0]=transportDiagnostics[1];
                for(unsigned page=1;page<4;++page){Target t;t.width=4096;t.height=heights[1];
                    t.color=b.Texture(t.width,t.height,RHITextureFormat::RGBA32F);t.framebuffer=b.Target(t.color,{});n.receiverDiagnosticPages[page]=t;}
                n.receiverDiagnosticPagesOwner=b.Finish();
            }
        }
    }
}
void RecordSurfaceTransport(RHIFrameEncoder& encoder,const RenderFrame& frame,const PipelineCamera& camera,
    const MaterialPipelineSettings& settings,std::uint64_t temporalSequence) {
    const auto& s=settings.surfaceCacheGi;const auto key=SurfaceCacheHistoryKey(frame,settings,true);
    const bool supportHalo=NativeReceiverSupportHaloActive(s);
    if(s.nativeForceRasterCore)Require(encoder.KeepAlive(nativeProbeTransport.forceRasterCoreProgramsOwner),"Cannot retain native forced-core programs");
    const bool reset=!transportCompleted.valid||transportCompleted.key!=key;
    const unsigned image=transportCompleted.valid?1-transportCompleted.image:0;
    unsigned first=s.cache->CardCount(),last=0;
    if(s.cache->Generation()!=transportCompleted.generation) {
        if(transportCompleted.valid&&s.cache->Generation()!=transportCompleted.generation+1){first=0;last=s.cache->CardCount()-1;}
        else for(auto card:s.cache->DirtyCards()){first=std::min(first,card);last=std::max(last,card);}
    }
    TransportConstants c{{int(transportCompleted.phase),reset?0:int(s.transportUpdatePeriod),int(first),int(last)},
        {s.receiverRiskDistance,float(s.transportRays),float(s.contact.raysPerPixel),float(s.transportTexelStride)}};
    c.viewProjection=camera.projection*camera.view;
    c.launchBias.x=settings.screenProbePlacement.trace.normalBias;
    c.launchBias.y=s.coherentReceiverTracing?1.f:0.f;
    c.receiverOptions={float(s.receiverLatticeMax),s.geometryReceiverRisk?1.f:0.f,.1f,s.deferredReceiverMix?1.f:0.f};
    if(s.nativeAdaptiveFreshSampling&&statistics.surfaceCacheFreshSamplingCollected)c.receiverOptions.w+=2;
    c.launchBias.z=s.sparseReceiverSampling?1.f:0.f;
    transportMask=transportSamplingMask;
    Require(s.cache->CardCount()/6<=256,"World transport object count exceeds uniform bounds");
    const auto& metadata=s.cache->Metadata();for(unsigned object=0;object<s.cache->CardCount()/6;++object){
        c.bounds[object*2]=metadata[object*6*SurfaceCacheCardTexels+9];c.bounds[object*2+1]=metadata[object*6*SurfaceCacheCardTexels+10];}
    Require(encoder.KeepAlive(transportProgramsOwner)&&encoder.KeepAlive(transportOwner)&&encoder.KeepAlive(transportViewOwner),"Cannot retain world transport cache");
    Require(encoder.KeepAlive(transportPrecisionOwner)&&encoder.BindTexture(transportPrecision,15),"Cannot bind exact geometry residuals");
    const auto activeBytes=std::uint32_t(offsetof(TransportConstants,bounds)+(s.cache->CardCount()/6)*2*sizeof(glm::vec4));
    Upload(encoder,transportBuffer,&c,activeBytes,14);
    ScreenProbePlacementConstants p;p.inverseView=glm::inverse(camera.view);p.inverseProjection=glm::inverse(camera.projection);
    p.viewProjection=camera.projection*camera.view;p.view=camera.view;p.screen={int(width),int(height),int(temporalSequence),0};
    p.tracing.w=int(settings.screenProbePlacement.trace.samplingSeed);
    p.traceParams.z=settings.screenProbePlacement.trace.normalBias;
    Upload(encoder,transportPlacementBuffer,&p,sizeof(p),ScreenProbePlacementBinding);
    const SurfaceCacheOriginSafetyConstants safety{{s.originSafetyFraction,s.originSafetyEpsilon,s.rasterSourcePositions?1.f:0.f,0}};
    Upload(encoder,surfaceCacheOriginSafetyBuffer,&safety,sizeof(safety),SurfaceCacheOriginSafetyBinding);
    if(!s.nativeScreenProbeTransport){
    Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheProbeReconstruction),"Cannot time world transport");
    Require(encoder.BindTexture({},0),"Cannot unbind previous transport");BindTarget(encoder,transport[image]);
    Require(encoder.BindPipeline(s.coherentReceiverTracing?transportUpdateCoherent:transportUpdate)&&encoder.BindMesh(fullscreen)&&
        encoder.BindTexture(transport[1-image].color,0)&&encoder.BindTexture(surfaceCacheGeometry,9)&&
        encoder.BindTexture(transportCoefficients[1-image][0],1)&&encoder.BindTexture(transportCoefficients[1-image][1],6)&&encoder.BindTexture(transportCoefficients[1-image][2],7)&&
        encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&
        encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot update low-frequency world transport");
    BindTarget(encoder,transportFiltered);
    Require(encoder.BindPipeline(transportFilter)&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(transport[image].color,0)&&
        encoder.BindTexture(transportCoefficients[image][0],1)&&encoder.BindTexture(transportCoefficients[image][1],6)&&encoder.BindTexture(transportCoefficients[image][2],7)&&
        encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot filter low-frequency world transport");
    Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheProbeReconstruction),"Cannot finish world transport timing");
    }
    const auto receiverBase=s.nativeScreenProbeTransport?nativeProbeTransport.NativeBaseGI.color:transportFiltered.color;
    if(s.nativeScreenProbeTransport)Require(encoder.BindTexture(nativeProbeTransport.NativeSoftMask.color,8),"Cannot bind native contact soft mask");
    if(s.nativeAdaptiveFreshSampling)Require(encoder.BindTexture(surfaceCacheFreshSampling.color,11),"Cannot bind original native receiver budgets");
    Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheContactTrace),"Cannot time near contact");
    if(s.compactReceiverTracing){
        Require(encoder.KeepAlive(transportCompactOwner)&&encoder.KeepAlive(transportCompactDataOwner),"Cannot pin compact receiver resources");
        Require(encoder.BindTexture(receiverBase,0)&&encoder.BindTexture(transportFilteredCoefficients[0],1)&&
            encoder.BindTexture(transportFilteredCoefficients[1],6)&&encoder.BindTexture(transportFilteredCoefficients[2],7)&&
            encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&encoder.BindTexture(diffuseCapture.color,4)&&
            encoder.BindTexture(surfaceCacheGeometry,9)&&encoder.BindTexture(surfaceCacheAttributes,12)&&encoder.BindTexture(surfaceCacheMetadata,13)&&
            encoder.BindTexture(surfaceCacheLighting.color,14)&&encoder.BindTexture(transportPrecision,15),"Cannot bind compact receiver sources");
        RHICommand::DispatchFrameCompute reset;reset.pipeline=transportCompactReset;reset.buffers[0]=transportCompactControl;reset.pushBytes=4;
        std::memcpy(reset.pushConstants.data(),&transportCompactCapacity,sizeof(transportCompactCapacity));
        Require(encoder.DispatchCompute(reset),"Cannot reset compact receiver queue");
        RHICommand::DispatchFrameCompute classify;classify.pipeline=transportCompactClassify;
        classify.buffers[0]=transportCompactControl;classify.buffers[1]=transportCompactQueue;
        classify.images[0]=transportCompactRaw.color;classify.images[1]=transportSamplingMask.color;
        classify.images[2]=transportBase.color;classify.images[3]=transportClassification.color;
        classify.groupsX=(transportCompactCapacity+63)/64;
        Require(encoder.DispatchCompute(classify),"Cannot classify compact receiver queue");
        if(supportHalo)RecordNativeReceiverSupportHalo(encoder);
        if(s.nativeForceRasterCore)Require(encoder.BindTexture(transportClassification.color,5),"Cannot bind compact forced-core classification");
        auto trace=classify;trace.pipeline=transportCompactTrace;trace.images[3]={};trace.indirect=transportCompactControl;trace.indirectOffset=4;
        Require(encoder.DispatchCompute(trace),"Cannot consume compact receiver queue");
    }else{
    if(s.coherentReceiverTracing&&s.sparseReceiverSampling){
        Require(encoder.BindTexture({},5),"Cannot unbind receiver classifier output");BindTarget(encoder,transportClassification);
        Require(encoder.BindPipeline(s.nativeForceRasterCore?nativeProbeTransport.forceRasterClassificationProgram:transportClassify)&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(indirectPositions.color,2)&&
            encoder.BindTexture(normalCapture.color,3)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot classify receiver trace work");
        Require(encoder.BindTexture(transportClassification.color,5),"Cannot bind receiver trace classification");
    }
    BindTarget(encoder,transportReceiverTarget);
    Require(encoder.BindPipeline(s.nativeForceRasterCore?(s.sparseReceiverSampling?nativeProbeTransport.forceRasterCoreProgram:nativeProbeTransport.forceRasterFullProgram):s.nativeScreenProbeTransport?(s.nativeAdaptiveFreshSampling?
        (s.sparseReceiverSampling?nativeProbeTransport.adaptiveCoreProgram:nativeProbeTransport.adaptiveFullProgram):
        (s.sparseReceiverSampling?nativeProbeTransport.receiverCoreProgram:nativeProbeTransport.receiverFullProgram)):
        (s.deferredReceiverMix?(s.sparseReceiverSampling?transportDeferredCore:transportDeferredFull):(s.coherentReceiverTracing?(s.sparseReceiverSampling?transportResolveCore:transportResolveFull):transportResolve)))&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(receiverBase,0)&&
        encoder.BindTexture(transportFilteredCoefficients[0],1)&&encoder.BindTexture(transportFilteredCoefficients[1],6)&&encoder.BindTexture(transportFilteredCoefficients[2],7)&&
        encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&encoder.BindTexture(diffuseCapture.color,4)&&
        encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot evaluate receiver-local contact correction");
    }
    if(s.sparseReceiverSampling){
        c.launchBias.w=1;Upload(encoder,transportBuffer,&c,activeBytes,14);
        Require(encoder.BindTexture(s.coherentReceiverTracing?transportClassification.color:RenderResourceHandle<RHITextureSpec>{},5),"Cannot bind sparse receiver classification");
        BindTarget(encoder,transportLowFrequency);
        Require(encoder.BindPipeline(s.nativeForceRasterCore?nativeProbeTransport.forceRasterLowProgram:s.nativeScreenProbeTransport?(s.nativeAdaptiveFreshSampling?nativeProbeTransport.adaptiveLowProgram:nativeProbeTransport.receiverLowProgram):(s.deferredReceiverMix?transportDeferredLow:(s.coherentReceiverTracing?transportResolveLow:transportResolve)))&&encoder.BindMesh(fullscreen)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot trace dense low-frequency receiver lattice");
        c.launchBias.w=0;Upload(encoder,transportBuffer,&c,activeBytes,14);
    }
    Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheContactTrace),"Cannot finish near contact timing");
    if(s.contact.collectSamplingDiagnostics){
        Require(encoder.KeepAlive(transportDiagnosticOwner)&&encoder.KeepAlive(transportDiagnosticProgramsOwner),"Cannot retain transport diagnostics");
        if(supportHalo)Require(encoder.KeepAlive(nativeProbeTransport.receiverHaloDiagnosticOwner)&&encoder.BindTexture(transportClassification.color,5),"Cannot retain native support halo diagnostics");
        if(s.nativeAdaptiveFreshSampling){
            Require(encoder.KeepAlive(nativeProbeTransport.receiverDiagnosticPagesOwner),"Cannot retain variable native direction pages");
            for(unsigned page=0;page<4;++page){
                p.traceDisplay.w=int(page);Upload(encoder,transportPlacementBuffer,&p,sizeof(p),ScreenProbePlacementBinding);
                Require(encoder.BindTexture({},0),"Cannot unbind native diagnostic page");BindTarget(encoder,nativeProbeTransport.receiverDiagnosticPages[page]);
                Require(encoder.BindPipeline(supportHalo?nativeProbeTransport.receiverHaloDiagnosticProgram:s.nativeForceRasterCore?nativeProbeTransport.forceRasterDiagnosticProgram:nativeProbeTransport.adaptiveDiagnosticProgram)&&encoder.BindMesh(fullscreen)&&
                    encoder.BindTexture(receiverBase,0)&&encoder.BindTexture(nativeProbeTransport.NativeSoftMask.color,8)&&
                    encoder.BindTexture(surfaceCacheFreshSampling.color,11)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot inspect actual native receiver batch");
            }
            p.traceDisplay.w=0;Upload(encoder,transportPlacementBuffer,&p,sizeof(p),ScreenProbePlacementBinding);
            statistics.surfaceCacheNativeReceiverDiagnosticPages=4;
        }else{
        for(unsigned i=s.nativeScreenProbeTransport?1u:0u;i<2;++i){Require(encoder.BindTexture({},0),"Cannot unbind transport diagnostic output");BindTarget(encoder,transportDiagnostics[i]);
            Require(encoder.BindPipeline(supportHalo?nativeProbeTransport.receiverHaloDiagnosticProgram:s.nativeForceRasterCore?nativeProbeTransport.forceRasterDiagnosticProgram:s.nativeScreenProbeTransport?nativeProbeTransport.receiverDiagnosticProgram:transportDiagnosticPrograms[s.coherentReceiverTracing?(i==1?2:3):i])&&encoder.BindMesh(fullscreen)&&
                encoder.BindTexture(s.nativeScreenProbeTransport?receiverBase:(i?transportFiltered.color:transport[1-image].color),0)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot inspect actual transport directions");}
        }
        statistics.surfaceCacheTransportDiagnostics=true;
    }
    Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheContactFilter),"Cannot time receiver filtering");
    Require(encoder.KeepAlive(transportReceiverFilterOwner),"Cannot retain receiver filter");
    const Target* receiverSource=s.compactReceiverTracing?&transportCompactRaw:&surfaceCacheRaw;
    if(s.sparseReceiverSampling){
        Require(encoder.BindTexture({},0)&&encoder.BindTexture({},1),"Cannot unbind sparse reconstruction outputs");
        BindTarget(encoder,transportReconstructed);
        if(s.deferredReceiverMix&&!s.nativeScreenProbeTransport)Require(encoder.BindTexture(transportBase.color,9)&&encoder.BindTexture(transportLowFrequencyBase.color,15),"Cannot bind deferred receiver bases");
        Require(encoder.BindPipeline(s.nativeForceRasterCore?nativeProbeTransport.forceRasterReconstructionProgram:s.nativeScreenProbeTransport?(s.nativeAdaptiveFreshSampling?nativeProbeTransport.adaptiveReconstructionProgram:nativeProbeTransport.receiverReconstructionProgram):(s.deferredReceiverMix?transportDeferredReconstruction:transportReconstructionProgram))&&encoder.BindMesh(fullscreen)&&
            encoder.BindTexture(receiverSource->color,0)&&encoder.BindTexture(transportSamplingMask.color,10)&&
            encoder.BindTexture(transportLowFrequency.color,5)&&encoder.BindTexture(transportLowFrequencyMask.color,6)&&
            encoder.BindTexture(receiverBase,11)&&encoder.BindTexture(s.nativeAdaptiveFreshSampling?surfaceCacheFreshSampling.color:transportFilteredCoefficients[0],1)&&
            encoder.BindTexture(s.nativeForceRasterCore?transportClassification.color:transportFilteredCoefficients[1],7)&&encoder.BindTexture(s.nativeScreenProbeTransport?nativeProbeTransport.NativeSoftMask.color:transportFilteredCoefficients[2],8)&&
            encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&encoder.BindTexture(diffuseCapture.color,4)&&
            encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot reconstruct current sparse receiver residuals");
        receiverSource=&transportReconstructed;transportMask=transportReconstructedMask;
    }
    SurfaceCacheContactConstants filter;filter.options={int(s.contact.raysPerPixel),1,1,0};
    filter.filter={1,.08f,.9659258263f,s.originSafetyEpsilon};filter.sampling.w=1;
    const bool directFilterOutput=s.sparseReceiverSampling&&(s.compactReceiverTracing||s.geometryReceiverRisk);
    for(unsigned pass=0;pass<3;++pass){filter.options.y=int(std::min(4u,s.contact.filterRadius<<pass));
        filter.options.w=(s.deferredReceiverMix?(64|(pass==2?4:0)):0)|(s.nativeSyntheticReceiverFilterSupport?128:0)|(s.nativeReceiverFilterRiskWeights?256:0);
        Upload(encoder,transportReceiverFilterBuffer,&filter,sizeof(filter),SurfaceCacheContactBinding);
        Require(encoder.BindTexture({},0),"Cannot unbind receiver filter");
        BindTarget(encoder,directFilterOutput?(pass%2?transportReceiverScratch:surfaceCacheRaw):(pass%2?surfaceCacheRaw:transportReceiverScratch));
        Require(encoder.BindPipeline(s.deferredReceiverMix?transportDeferredFilter:(s.coherentReceiverTracing?transportReceiverPhysicalFilter:transportReceiverFilter))&&encoder.BindMesh(fullscreen)&&
            encoder.BindTexture(pass==0?receiverSource->color:directFilterOutput?(pass%2?surfaceCacheRaw.color:transportReceiverScratch.color):(pass%2?transportReceiverScratch.color:surfaceCacheRaw.color),0)&&encoder.BindTexture(transportMask.color,1)&&
            encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&encoder.BindTexture(diffuseCapture.color,4)&&
            encoder.BindTexture(s.deferredReceiverMix?(s.sparseReceiverSampling?transportReconstructedBase.color:transportBase.color):transportMask.color,5)&&encoder.BindTexture(transportMask.color,6)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot filter actual receiver samples");}
    if(!directFilterOutput){PostProcessConstants receiverPost;Fullscreen(encoder,surfaceCacheRaw,copy,transportReceiverScratch.color,{},{},receiverPost);}
    Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheContactFilter),"Cannot finish receiver filtering");
    if(s.deferredReceiverMix)Require(encoder.BindTexture(surfaceCacheGeometry,9)&&encoder.BindTexture(transportPrecision,15),"Cannot restore native geometry after deferred receiver bases");
    Upload(encoder,transportBuffer,&c,activeBytes,14);
    if(s.cachedDirectDiffuse){
        Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheDirectResolve),"Cannot time cached direct resolve");
        BindTarget(encoder,surfaceCacheResolved);
        Require(encoder.BindPipeline(transportDirect)&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(surfaceCacheLighting.color,0)&&encoder.BindTexture(surfaceCacheSource.color,5)&&
            encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&encoder.BindTexture(diffuseCapture.color,4)&&
            encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot resolve smooth cached direct diffuse");
        PostProcessConstants post;Fullscreen(encoder,surfaceCacheSource,copy,surfaceCacheResolved.color,{},{},post);
        Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheDirectResolve),"Cannot finish cached direct resolve timing");}
    transportPending={true,key,transportCompleted.phase+1,image,s.cache->Generation()};
    statistics.surfaceCacheSplitTransport=true;statistics.surfaceCacheHardwareContact=true;
    statistics.surfaceCacheCompactReceivers=s.compactReceiverTracing;
    statistics.surfaceCacheNativeForceRasterCore=s.nativeForceRasterCore;
    statistics.surfaceCacheNativeReceiverFilterSupportHalo=supportHalo;
    statistics.surfaceCacheWorldTransportReused=!s.nativeScreenProbeTransport&&!reset;
    statistics.surfaceCacheWorldTransportPhase=s.nativeScreenProbeTransport?0:transportCompleted.phase;
}
