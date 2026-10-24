// Included inside MaterialRenderPipeline::Impl. Placement is independent of all
// GI resources. Raster passes keep fixed candidate slots rather than UE's atomic
// compact atlas and global adaptive allocation budget.
void PrepareScreenProbePlacement(const ScreenProbePlacementSettings& s,bool nativeCompute=false,bool collectComparison=false,bool storageSpawned=false,bool uePlacement=false) {
    if(!s.enabled)return;
    Require(!uePlacement||s.candidateCount==16,"UE High placement requires the original sixteen adaptive candidates");
    if(!placementProgramsOwner) {
        PipelineDetail::ResourceBuilder b(device);
        const auto vertex=FullscreenVertexShader();
        placementUniformProgram=b.FullscreenPipeline(vertex,ScreenProbePlacementDetail::UniformShader());
        placementMarkProgram=b.FullscreenPipeline(vertex,ScreenProbePlacementDetail::MarkShader());
        placementSpawnProgram=b.FullscreenPipeline(vertex,ScreenProbePlacementDetail::SpawnShader());
        placementCoverageProgram=b.FullscreenPipeline(vertex,ScreenProbePlacementDetail::CoverageShader());
        CreateGraphicShaderDesc desc;
        desc.vertexShaderSource=b.Source(ShaderSourceType::Vertex,ScreenProbePlacementDetail::MarkerVertexShader());
        desc.fragmentShaderSource=b.Source(ShaderSourceType::Fragment,ScreenProbePlacementDetail::MarkerFragmentShader(ScreenProbeTraceDetail::MarkerTraceGLSL()));
        auto program=b.Create<ShaderProgramSpec>([&](auto cb){device.async_CreateGraphicShader(desc,cb);});
        PipelineSpec marker;marker.shaderProgram=program;marker.expectVertexLayout={{VertexFieldType::Vec2,VertexFieldType::Vec2}};
        marker.cullMode=CullMode::None;marker.depthTest=true;marker.depthWrite=false;marker.depthCompare=CompareOp::LessEqual;
        placementMarkerProgram=b.Create<PipelineSpec>([&](auto cb){device.async_CreatePipeline({marker},cb);});
        placementBuffer=b.Uniform(sizeof(ScreenProbePlacementConstants));placementProgramsOwner=b.Finish();
    }
    if(nativeCompute&&s.trace.cosineHemisphere&&!placementComputeChecked){
        placementComputeChecked=true;
        auto owner=std::shared_ptr<Compute::Device>(Compute::Device::Borrow(device));
        placementComputeSupported=owner->Diagnostics().frameAvailable;
        if(placementComputeSupported)placementComputeOwner=std::move(owner);
    }
    const bool useCompute=nativeCompute&&s.trace.cosineHemisphere&&placementComputeSupported&&bool(placementComputeOwner);
    if(useCompute){auto& program=uePlacement?placementUeComputeMark:placementComputeMark;
        if(!program){Compute::FramePipelineDesc d;d.source=ScreenProbePlacementDetail::ComputeMarkShader(uePlacement);
            d.label=uePlacement?"ue-screen-probe-compute-mark":"screen-probe-compute-mark";
            d.uniformMask=1u<<ScreenProbePlacementBinding;d.sampledMask=(1u<<1)|(1u<<2)|(1u<<3)|(1u<<4);d.storageImages=1;
            program=placementComputeOwner->CreateFramePipeline(d);}}
    if(uePlacement&&!placementUeProgramsOwner){PipelineDetail::ResourceBuilder b(device);const auto vertex=FullscreenVertexShader();
        placementUeMarkProgram=b.FullscreenPipeline(vertex,ScreenProbePlacementDetail::MarkShader(true));
        placementUeSpawnProgram=b.FullscreenPipeline(vertex,ScreenProbePlacementDetail::SpawnShader(true));placementUeProgramsOwner=b.Finish();}
    const auto gw=std::max(1u,(width+s.baseSpacing-1)/s.baseSpacing),gh=std::max(1u,(height+s.baseSpacing-1)/s.baseSpacing);
    const auto candidateWidth=gw*s.candidateCount*2;
    Require(candidateWidth<=8192,"Screen probe candidate atlas exceeds 8192 texels; increase spacing");
    if(!placementTargetsOwner||placementUniform[0].width!=gw*2||placementUniform[0].height!=gh||
        placementMarked[0].width!=candidateWidth||placementCoverage.width!=width||placementCoverage.height!=height||
        (useCompute&&!placementMarkedStorage)||(storageSpawned&&!placementSpawnedStorage)) {
        PipelineDetail::ResourceBuilder b(device);
        const auto target=[&](unsigned w,unsigned h){Target t;t.width=w;t.height=h;
            t.color=b.Texture(w,h,RHITextureFormat::RGBA32F);t.framebuffer=b.Target(t.color,{});return t;};
        std::array<Target,2> uniform,marked,spawned;
        for(unsigned i=0;i<2;++i){uniform[i]=target(gw*2,gh);
            if(storageSpawned){spawned[i].width=candidateWidth;spawned[i].height=gh;
                spawned[i].color=b.StorageTexture(candidateWidth,gh,RHITextureFormat::RGBA32F);spawned[i].framebuffer=b.Target(spawned[i].color,{});}
            else spawned[i]=target(candidateWidth,gh);
            if(useCompute){marked[i].width=candidateWidth;marked[i].height=gh;
                marked[i].color=b.StorageTexture(candidateWidth,gh,RHITextureFormat::RGBA32F);marked[i].framebuffer=b.Target(marked[i].color,{});}
            else marked[i]=target(candidateWidth,gh);}
        // Shared debug target also stores SDF view ray distance in alpha.
        // Preserve it at full precision so fine-field differences are not
        // dominated by half-float depth quantization during shape validation.
        auto coverage=target(width,height);auto owner=b.Finish();
        placementUniform=uniform;placementMarked=marked;placementSpawned=spawned;placementCoverage=coverage;
        placementTargetsOwner=std::move(owner);placementCompleted={};placementMarkedStorage=useCompute;placementSpawnedStorage=storageSpawned;placementComparisonValid=false;
    }
    if(useCompute&&collectComparison&&!uePlacement&&(!placementComparisonOwner||placementComparisonMarked.width!=candidateWidth||placementComparisonMarked.height!=gh)){
        PipelineDetail::ResourceBuilder b(device);const auto target=[&]{Target t;t.width=candidateWidth;t.height=gh;
            t.color=b.Texture(t.width,t.height,RHITextureFormat::RGBA32F);t.framebuffer=b.Target(t.color,{});return t;};
        placementComparisonMarked=target();placementComparisonSpawned=target();placementComparisonOwner=b.Finish();placementComparisonValid=false;
    }
}

bool PrepareSingleInvocationTraceCapability() {
    if(!traceGeometryCapabilityChecked) {
        struct Capability {bool done=false,supported=false;};
        auto result=std::make_shared<Capability>();
        device.async_ExecuteCode([this,result]{result->supported=device.BackendDiagnostics()->GeometryShadersAvailable();},
            [result]{result->done=true;});
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(!result->done){device.returnSystem.DrainCallbacks();
            Require(std::chrono::steady_clock::now()<deadline,"Geometry trace capability query timed out");
            if(!result->done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(1));}
        traceGeometrySupported=result->supported;traceGeometryCapabilityChecked=true;
    }
    return traceGeometrySupported;
}
bool SingleInvocationTraceTarget(const Target& target) const {
    return target.width>=4&&target.width%4==0&&target.height>0&&
        std::uint64_t(target.width)*target.height/4<=std::numeric_limits<std::uint32_t>::max();
}
void PrepareProbeTraceOutputs() {
    Require(SingleInvocationTraceTarget(traceRays[0]),"Invalid packed probe trace extent for four outputs");
    if(!traceOutputProgramsOwner) {
        PipelineDetail::ResourceBuilder b(device);const auto vertex=FullscreenVertexShader();
        traceOutputRayProgram=b.FullscreenPipeline(vertex,ScreenProbeTraceDetail::TraceOutputsShader());
        traceOutputSafeRayProgram=b.FullscreenPipeline(vertex,ScreenProbeTraceDetail::TraceOutputsShader(true));
        traceOutputPackProgram=b.FullscreenPipeline(vertex,ScreenProbeTraceDetail::PackTraceOutputsShader());
        traceOutputProgramsOwner=b.Finish();
    }
    const auto w=traceRays[0].width/4,h=traceRays[0].height;
    if(!traceOutputTargetsOwner||traceOutputTarget.width!=w||traceOutputTarget.height!=h) {
        PipelineDetail::ResourceBuilder b(device);
        std::array<RenderResourceHandle<RHITextureSpec>,4> parts;
        for(auto& part:parts)part=b.Texture(w,h,RHITextureFormat::RGBA32F);
        Target target;target.width=w;target.height=h;target.color=parts[0];target.framebuffer=b.TargetColors(parts);
        auto owner=b.Finish();traceOutputParts=parts;traceOutputTarget=target;traceOutputTargetsOwner=std::move(owner);
    }
}
void PackProbeTraceOutputs(RHIFrameEncoder& encoder,const Target& destination) {
    BindTarget(encoder,destination);
    Require(encoder.BindPipeline(traceOutputPackProgram)&&encoder.BindMesh(fullscreen),"Cannot bind probe trace output packer");
    for(unsigned part=0;part<traceOutputParts.size();++part)
        Require(encoder.BindTexture(traceOutputParts[part],part),"Cannot bind probe trace output part");
    Require(encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot repack probe trace outputs");
    ++statistics.screenProbeTracePasses;
}
void PrepareScreenProbeTrace(const ScreenProbePlacementSettings& placement) {
    const auto& s=placement.trace;if(!placement.enabled||!s.enabled)return;
    // Field visualization consumes only the camera and distance textures. Do
    // not allocate or prepare the independent probe-ray/HZB experiment for it.
    if(s.sdf.enabled&&s.sdf.visualMode!=0)return;
    if(!traceProgramsOwner) {
        PipelineDetail::ResourceBuilder b(device);const auto vertex=FullscreenVertexShader();
        traceHzbBaseProgram=b.FullscreenPipeline(vertex,ScreenProbeTraceDetail::HzbBaseShader());
        traceHzbReduceProgram=b.FullscreenPipeline(vertex,ScreenProbeTraceDetail::HzbReduceShader());
        traceRayProgram=b.FullscreenPipeline(vertex,ScreenProbeTraceDetail::TraceShader());
        traceSafeRayProgram=b.FullscreenPipeline(vertex,ScreenProbeTraceDetail::TraceShader(true));
        traceDisplayProgram=b.FullscreenPipeline(vertex,ScreenProbeTraceDetail::DisplayShader());
        traceProgramsOwner=b.Finish();
    }
    if(s.singleInvocation&&!traceSingleProgramsOwner&&PrepareSingleInvocationTraceCapability()) {
        PipelineDetail::ResourceBuilder b(device);
        const auto vertex=ScreenProbeTraceDetail::RayQuadVertexShader(),fragment=ScreenProbeTraceDetail::RayQuadFragmentShader();
        traceSingleRayProgram=b.RayQuadPipeline(vertex,ScreenProbeTraceDetail::TraceGeometryShader(),fragment);
        traceSingleSafeRayProgram=b.RayQuadPipeline(vertex,ScreenProbeTraceDetail::TraceGeometryShader(true),fragment);
        traceSingleProgramsOwner=b.Finish();
    }
    const auto slots=(placementUniform[0].width/2)*placementUniform[0].height*(1+placement.candidateCount);
    const auto texels=std::uint64_t(slots)*s.rayResolution*s.rayResolution*ScreenProbeTraceTexelsPerRay;
    unsigned atlasWidth=1024;
    while(atlasWidth<8192&&(texels+atlasWidth-1)/atlasWidth>8192)atlasWidth*=2;
    const auto atlasHeight=unsigned((texels+atlasWidth-1)/atlasWidth);
    Require(atlasHeight<=8192,"Screen ray atlas exceeds 8192 rows; reduce ray resolution or probe density");
    if(!traceTargetsOwner||traceSource.width!=width||traceSource.height!=height||traceRays[0].width!=atlasWidth||traceRays[0].height!=atlasHeight||traceResolution!=s.rayResolution) {
        PipelineDetail::ResourceBuilder b(device);
        const auto target=[&](unsigned w,unsigned h){Target t;t.width=w;t.height=h;
            t.color=b.Texture(w,h,RHITextureFormat::RGBA32F);t.framebuffer=b.Target(t.color,{});return t;};
        auto source=MakeTarget(b,width,height,true,false);
        std::array<Target,2> rays;
        std::array<Target,ScreenProbeTraceMaxHzbLevels> hzb;
        for(auto& ray:rays)ray=target(atlasWidth,atlasHeight);
        unsigned w=width,h=height,levels=0;
        for(unsigned level=0;level<ScreenProbeTraceMaxHzbLevels;++level) {
            hzb[level]=target(w,h);++levels;
            if(w==1&&h==1)break;
            w=(w+1)/2;h=(h+1)/2;
        }
        auto owner=b.Finish();traceSource=source;traceRays=rays;traceHzb=hzb;traceHzbLevels=levels;
        traceResolution=s.rayResolution;traceTargetsOwner=std::move(owner);traceCompleted={};
    }
    if(s.singlePassOutputs)PrepareProbeTraceOutputs();
}

void RecordScreenProbePlacement(RHIFrameEncoder& encoder,const RenderFrame& frame,const PipelineCamera& camera,
    const ScreenProbePlacementSettings& s,const SceneEffectsConstants& effects,const SurfacePassConstants& surface,const PostProcessConstants& post,
    bool captureSurface=true,bool drawDiagnostics=true,bool placementOnly=false,bool nativeCompute=false,bool collectComparison=false,
    bool collectPlacementTimings=false,bool ueAdaptiveAllocation=false) {
    Require(!placementOnly||s.trace.enabled,"Placement-only native tracing requires enabled probe traces");
    if(!placementOnly&&s.trace.enabled&&s.trace.sdf.enabled&&s.trace.sdf.visualMode!=0) {
        ScreenProbePlacementConstants c;
        c.inverseView=glm::inverse(camera.view);c.inverseProjection=glm::inverse(camera.projection);
        c.viewProjection=camera.projection*camera.view;c.view=camera.view;
        c.screen={int(width),int(height),0,int(s.debugView)};
        Upload(encoder,placementBuffer,&c,sizeof(c),ScreenProbePlacementBinding);
        RecordProbeSdfVolume(encoder,s,post,camera);
        // No valid probe/ray image was generated. Switching back to tracing
        // must regenerate even frozen placement rather than expose stale data.
        placementPending={};tracePending={false,0,0,true,true};
        return;
    }
    const auto currentViewProjection=camera.projection*camera.view;
    const auto geometryGeneration=s.trace.cosineHemisphere&&s.trace.sdf.scene?s.trace.sdf.scene->GeometryGeneration():0;
    bool cameraCompatible=true;
    if(s.trace.cosineHemisphere)for(int col=0;col<4;++col)for(int row=0;row<4;++row)
        cameraCompatible&=std::abs(placementCompleted.viewProjection[col][row]-currentViewProjection[col][row])<1e-6f;
    // GI reconstruction addresses screen tiles, so a camera change must
    // regenerate that stencil even when diagnostic placement is frozen.
    const unsigned refinementFlags=ueAdaptiveAllocation?32u:(s.cornerRefinement&&s.trace.cosineHemisphere?1u:0u)|
        (s.reconstructionVisibility&&s.trace.cosineHemisphere?2u:0u)|
        (s.cornerRefinement&&s.curvatureRefinement&&s.trace.cosineHemisphere?4u:0u)|
        (s.curvatureAwareWeights&&s.trace.cosineHemisphere?8u:0u);
    const glm::vec4 refinement{float(refinementFlags),s.cornerDetectionRadius,s.cornerProbeSupport,std::cos(glm::radians(s.cornerNormalAngleDegrees))};
    const glm::vec4 curvature{std::cos(glm::radians(s.curvatureNormalAngleDegrees)),s.curvaturePlaneErrorPixels,0,s.continuousInterpolation?1.f:0.f};
    const glm::vec2 visibilityParameters=(refinementFlags&2u)?glm::vec2(s.trace.normalBias,s.trace.relativeThickness):glm::vec2(0);
    const bool compatible=placementCompleted.valid&&cameraCompatible&&placementCompleted.refinement==refinement&&placementCompleted.curvature==curvature&&placementCompleted.visibilityParameters==visibilityParameters&&placementCompleted.geometryGeneration==geometryGeneration&&placementCompleted.sourceWidth==width&&placementCompleted.sourceHeight==height&&
        placementCompleted.spacing==s.baseSpacing&&placementCompleted.candidates==s.candidateCount&&
        placementCompleted.threshold==s.relativeDepthThreshold&&placementCompleted.adaptive==s.adaptive&&
        placementCompleted.cosineHemisphere==s.trace.cosineHemisphere;
    const bool refresh=!s.freeze||s.reset||!compatible;
    const auto image=refresh?(placementCompleted.valid?1-placementCompleted.image:0):placementCompleted.image;
    const auto sequence=compatible?placementCompleted.sequence:0;
    const auto phase=refresh?(s.frameIndex==std::numeric_limits<std::uint32_t>::max()?std::uint32_t(sequence%8):s.frameIndex%8):placementCompleted.phase;
    ScreenProbePlacementConstants c;c.inverseView=glm::inverse(camera.view);c.inverseProjection=glm::inverse(camera.projection);
    c.viewProjection=camera.projection*camera.view;c.view=camera.view;
    c.grid=glm::ivec4(int(placementUniform[image].width/2),int(placementUniform[image].height),int(s.candidateCount),int(s.baseSpacing));
    c.screen=glm::ivec4(int(width),int(height),int(phase),int(s.debugView));c.options=glm::vec4(s.relativeDepthThreshold,s.markerRadiusPixels,s.adaptive?1:0,0);
    c.refinement=refinement;
    c.curvature=curvature;
    // Runtime transport validity is distinct from persistent allocation data.
    c.curvature.z=surfaceCachePending.valid&&surfaceCacheSafeOriginsActive&&s.trace.cosineHemisphere?1.f:0.f;
    const bool nativeOnly=c.curvature.z>.5f&&s.trace.sdf.nativeInteriorTracing&&!s.trace.sdf.directSoftwareRefinement&&
        s.trace.sdf.hardwareRayTracing&&surfaceCacheHardwareSupported&&!s.trace.compactSdfRays&&s.trace.singlePassOutputs&&
        bool(sdfNativeProgramsOwner[ProbeSdfNativeVariant(s.trace.sdf)][1]);
    if(nativeOnly)c.curvature.z=2;
    const auto rayImage=traceCompleted.valid?1-traceCompleted.image:0;
    if(s.trace.enabled) {
        const auto mode=1u|(s.trace.cosineHemisphere?2u:0u)|(s.trace.samplingSeed<<2);
        c.tracing=glm::ivec4(int(s.trace.rayResolution),int(s.trace.maxSteps),int(traceHzbLevels?std::min(s.trace.maxMip,traceHzbLevels-1):0),int(mode));
        c.traceParams={s.trace.maxDistance,s.trace.thickness,s.trace.normalBias,s.trace.relativeThickness};
        const auto uniformSlots=c.grid.x*c.grid.y;
        c.traceAtlas={int(traceRays[rayImage].width),int(traceRays[rayImage].height),uniformSlots,uniformSlots*(1+c.grid.z)};
        c.traceDisplay={int(s.trace.displayMode),s.trace.selectedProbe==std::numeric_limits<std::uint32_t>::max()?-1:int(s.trace.selectedProbe),s.trace.showPanel?1:0,int(traceHzbLevels)};
    }
    if(captureSurface&&(refresh||s.trace.enabled)) {
        auto captureEffects=effects;captureEffects.effects.y=0;captureEffects.screenAndAo.z=0;Effects(encoder,captureEffects);
        auto captureSurface=surface;captureSurface.options=glm::vec4(0);captureSurface.projection=camera.projection;
        Upload(encoder,surfaceBuffer,&captureSurface,sizeof(captureSurface),SurfacePassBinding);
        BindTarget(encoder,diffuseCapture,RHICommand::ClearColor|RHICommand::ClearDepth);
        Depth(encoder,frame,c.viewProjection,false);
        BindTarget(encoder,normalCapture,RHICommand::ClearColor);CaptureSurface(encoder,frame,camera,true);
        CaptureSurfacePositions(encoder,frame,camera,captureSurface);
        statistics.screenProbePlacementPasses+=3;
    }
    Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::ProbePlacement),"Cannot time probe placement");
    if(ueAdaptiveAllocation)Require(encoder.KeepAlive(placementUeProgramsOwner),"Cannot retain UE adaptive placement programs");
    const bool useCompute=nativeCompute&&s.trace.cosineHemisphere&&placementComputeSupported&&placementMarkedStorage&&bool(placementComputeOwner);
    if(refresh)placementInitialFrameConstants[image]=c;
    Upload(encoder,placementBuffer,&c,sizeof(c),ScreenProbePlacementBinding);
    const auto draw=[&](const Target& target,RenderResourceHandle<PipelineSpec> program,RenderResourceHandle<RHITextureSpec> input={}) {
        // Never bind an active color attachment for sampling, including inputs
        // whose declarations a backend's optimizer may remove from this pass.
        const auto uniformInput=target.color==placementUniform[image].color?RenderResourceHandle<RHITextureSpec>{}:placementUniform[image].color;
        const auto markedInput=target.color==placementMarked[image].color?RenderResourceHandle<RHITextureSpec>{}:placementMarked[image].color;
        const auto spawnedInput=target.color==placementSpawned[image].color?RenderResourceHandle<RHITextureSpec>{}:placementSpawned[image].color;
        BindTarget(encoder,target);
        Require(encoder.BindPipeline(program)&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(input,0)&&
            encoder.BindTexture(normalCapture.color,1)&&encoder.BindTexture(diffuseCapture.depth,2)&&encoder.BindTexture(indirectPositions.color,3)&&
            encoder.BindTexture(uniformInput,4)&&encoder.BindTexture(markedInput,5)&&
            encoder.BindTexture(spawnedInput,6)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot record probe placement pass");
        ++statistics.screenProbePlacementPasses;
    };
    if(refresh) {
        if(collectPlacementTimings)Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::PlacementUniform),"Cannot begin uniform placement timing");
        draw(placementUniform[image],placementUniformProgram);
        if(collectPlacementTimings)Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::PlacementUniform),"Cannot end uniform placement timing");
        const auto capacity=std::uint64_t(c.grid.x)*c.grid.y*s.candidateCount;
        statistics.screenProbeLegacyMarkInvocationCapacity=capacity*2;
        if(collectPlacementTimings)Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::PlacementMark),"Cannot begin adaptive placement mark timing");
        if(useCompute){
            Require(encoder.KeepAlive(placementComputeOwner),"Cannot retain compute probe placement");
            Require(encoder.BindTexture(normalCapture.color,1)&&encoder.BindTexture(diffuseCapture.depth,2)&&
                encoder.BindTexture(indirectPositions.color,3)&&encoder.BindTexture(placementUniform[image].color,4),"Cannot bind compute probe placement sources");
            RHICommand::DispatchFrameCompute mark;mark.pipeline=ueAdaptiveAllocation?placementUeComputeMark:placementComputeMark;mark.images[0]=placementMarked[image].color;
            const auto groups=std::uint32_t((capacity+63)/64);mark.groupsX=std::min(groups,65535u);mark.groupsY=(groups+mark.groupsX-1)/mark.groupsX;
            Require(encoder.DispatchCompute(mark),"Cannot mark paired probe representatives");
            statistics.screenProbeComputePlacement=true;statistics.screenProbeMarkInvocationCapacity=capacity;++statistics.screenProbePlacementPasses;
        }else{draw(placementMarked[image],ueAdaptiveAllocation?placementUeMarkProgram:placementMarkProgram);statistics.screenProbeMarkInvocationCapacity=capacity*2;}
        if(collectPlacementTimings)Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::PlacementMark),"Cannot end adaptive placement mark timing");
        if(collectPlacementTimings)Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::PlacementSpawn),"Cannot begin adaptive placement spawn timing");
        draw(placementSpawned[image],ueAdaptiveAllocation?placementUeSpawnProgram:placementSpawnProgram);
        if(collectPlacementTimings)Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::PlacementSpawn),"Cannot end adaptive placement spawn timing");
        placementPending={true,image,phase,s.baseSpacing,s.candidateCount,width,height,s.relativeDepthThreshold,s.adaptive,sequence+1,s.trace.cosineHemisphere,currentViewProjection,geometryGeneration,refinement,visibilityParameters,curvature};
    } else placementPending=placementCompleted;
    Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::ProbePlacement),"Cannot end probe placement timing");
    if(collectComparison&&useCompute&&!ueAdaptiveAllocation){
        Require(encoder.KeepAlive(placementComparisonOwner),"Cannot retain original placement comparison targets");
        if(refresh){
            const auto reference=[&](const Target& target,RenderResourceHandle<PipelineSpec> program,RenderResourceHandle<RHITextureSpec> marked){
                Require(encoder.BindTexture({},0)&&encoder.BindTexture({},5)&&encoder.BindTexture({},6),"Cannot unbind placement comparison outputs");
                BindTarget(encoder,target);
                Require(encoder.BindPipeline(program)&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(normalCapture.color,1)&&
                    encoder.BindTexture(diffuseCapture.depth,2)&&encoder.BindTexture(indirectPositions.color,3)&&
                    encoder.BindTexture(placementUniform[image].color,4)&&encoder.BindTexture(marked,5)&&
                    encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot record original placement comparison");
            };
            reference(placementComparisonMarked,placementMarkProgram,{});
            reference(placementComparisonSpawned,placementSpawnProgram,placementComparisonMarked.color);
            placementComparisonValid=true;placementComparisonImage=image;placementComparisonSequence=placementPending.sequence;
            statistics.screenProbePlacementComparisonPasses=2;
        }
        statistics.screenProbePlacementComparisonCollected=placementComparisonValid&&placementComparisonImage==image&&placementComparisonSequence==placementPending.sequence;
        statistics.screenProbePlacementComparisonReused=statistics.screenProbePlacementComparisonCollected&&!refresh;
    }
    if(ueAdaptiveAllocation){placementFrameConstants=c;RecordUeLumenAdaptiveAllocation(encoder,image);}
    if(s.trace.enabled) {
        // This shared recording function runs once per frame: Surface Cache
        // replaces the standalone call, so its scope cannot be recorded twice.
        Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::ProbeScreenTrace),"Cannot begin probe screen trace GPU timing");
        const bool safeOrigins=surfaceCachePending.valid&&surfaceCacheSafeOriginsActive&&s.trace.cosineHemisphere;
        if(safeOrigins) {
            Require(encoder.BindTexture({},0)&&encoder.BindTexture({},10)&&encoder.BindTexture({},11),"Cannot unbind probe origin output");
            BindTarget(encoder,surfaceCacheProbeOrigins);
            const bool nativeOrigins=surfaceCacheNativeOriginsActive;
            statistics.surfaceCacheHardwareOrigins=nativeOrigins;
            statistics.surfaceCacheReusedOriginCertification=surfaceCacheReusedNativeOriginsActive;
            statistics.surfaceCacheClosestTraversalReused|=surfaceCacheQueryProbeOriginActive&&(surfaceCacheQueryProbeOriginKey&1u)!=0;
            statistics.surfaceCacheFastGeometryAddressing|=surfaceCacheQueryProbeOriginActive&&(surfaceCacheQueryProbeOriginKey&2u)!=0;
            const auto originProgram=surfaceCacheQueryProbeOriginActive?surfaceCacheQueryProbeOriginPrograms[surfaceCacheQueryProbeOriginKey]:
                surfaceCacheReusedNativeOriginsActive?surfaceCacheReusedNativeProbeOriginProgram:
                nativeOrigins?surfaceCacheNativeProbeOriginProgram:surfaceCacheProbeOriginProgram;
            Require(encoder.BindPipeline(originProgram)&&encoder.BindMesh(fullscreen)&&
                encoder.BindTexture(placementUniform[image].color,0)&&encoder.BindTexture(placementSpawned[image].color,1)&&
                encoder.BindTexture(surfaceCacheGeometry,9)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot constrain probe ray origins");
            statistics.surfaceCacheProbeOriginsCollected=true;
            ++statistics.screenProbeTracePasses;
        }
        if(placementOnly) {
            // The native screen-probe consumer owns tracing and packing. Keep
            // the same placement IDs, sample seed/sequence and certified
            // origins, but record no HDR copy, HZB pass or SDF fallback here.
            Require(safeOrigins,"Placement-only native probes need certified source origins");
            Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::ProbeScreenTrace),"Cannot finish native source-origin timing");
            placementFrameConstants=c;
            tracePending={false,rayImage,s.trace.rayResolution,false,false};
            statistics.screenProbeUniformCapacity=c.grid.x*c.grid.y;
            statistics.screenProbeCandidateCapacity=statistics.screenProbeUniformCapacity*s.candidateCount;
            statistics.screenProbePlacementFrozen=!refresh;
            statistics.screenProbeHzbLevels=0;
            return;
        }
        // Snapshot HDR before every visualization pass. Diagnostics must never
        // become next frame's radiance or leak into the trace source.
        Fullscreen(encoder,traceSource,copy,surfaceCachePending.valid?surfaceCacheSource.color:scene.color,{},{},post);
        const auto hzbDraw=[&](const Target& target,RenderResourceHandle<PipelineSpec> program,RenderResourceHandle<RHITextureSpec> input) {
            BindTarget(encoder,target);
            Require(encoder.BindPipeline(program)&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(input,0)&&
                encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot record HZB reduction");
            ++statistics.screenProbeTracePasses;
        };
        if(!nativeOnly){hzbDraw(traceHzb[0],traceHzbBaseProgram,indirectPositions.color);
            for(unsigned level=1;level<traceHzbLevels;++level)hzbDraw(traceHzb[level],traceHzbReduceProgram,traceHzb[level-1].color);}
        const bool outputs=s.trace.singlePassOutputs&&traceOutputProgramsOwner&&traceOutputTargetsOwner;
        BindTarget(encoder,outputs?traceOutputTarget:traceRays[rayImage]);
        const bool single=!outputs&&s.trace.singleInvocation&&traceSingleProgramsOwner&&SingleInvocationTraceTarget(traceRays[rayImage]);
        const auto program=outputs?(safeOrigins?traceOutputSafeRayProgram:traceOutputRayProgram):
            (single?(safeOrigins?traceSingleSafeRayProgram:traceSingleRayProgram):(safeOrigins?traceSafeRayProgram:traceRayProgram));
        Require(encoder.BindPipeline(program)&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(safeOrigins?surfaceCacheProbeOrigins.color:traceSource.color,0)&&
            encoder.BindTexture(normalCapture.color,1)&&encoder.BindTexture(indirectPositions.color,2)&&
            encoder.BindTexture(placementUniform[image].color,3)&&encoder.BindTexture(placementSpawned[image].color,4),"Cannot bind screen trace sources");
        for(unsigned level=0;level<ScreenProbeTraceMaxHzbLevels;++level)
            Require(encoder.BindTexture(traceHzb[std::min(level,traceHzbLevels-1)].color,5+level),"Cannot bind HZB level");
        // Draw the physical capacity, including padding rays, so all four texels
        // receive the same inactive defaults as the original fullscreen path.
        const auto rayCapacity=std::uint32_t(std::uint64_t(traceRays[rayImage].width)*traceRays[rayImage].height/4);
        Require(encoder.DrawIndexed(single?RHICommand::DrawIndexed{1,0,0,rayCapacity,0}:RHICommand::DrawIndexed{3}),"Cannot record screen probe rays");
        statistics.screenProbeSingleInvocationSupported=traceGeometryCapabilityChecked&&traceGeometrySupported;
        statistics.screenProbeSingleInvocation=single;
        if(outputs)PackProbeTraceOutputs(encoder,traceRays[rayImage]);
        statistics.screenProbeSinglePassOutputs=outputs;
        Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::ProbeScreenTrace),"Cannot end probe screen trace GPU timing");
        ++statistics.screenProbeTracePasses;statistics.screenProbeHzbLevels=nativeOnly?0:traceHzbLevels;
        if(s.trace.sdf.enabled)RecordProbeSdfFallback(encoder,s,image,rayImage);
        tracePending={true,rayImage,s.trace.rayResolution,s.trace.sdf.enabled,s.trace.sdf.enabled&&s.trace.sdf.visualMode!=0};
    }
    // Frozen positions still project and depth-test correctly, but the atlas's
    // original screen tile stencil cannot describe another camera's coverage.
    if(drawDiagnostics&&refresh&&s.debugView==2) {
        draw(placementCoverage,placementCoverageProgram,scene.color);
        Fullscreen(encoder,scene,copy,placementCoverage.color,{},{},post);
    }
    placementFrameConstants=c;
    statistics.screenProbeUniformCapacity=c.grid.x*c.grid.y;
    statistics.screenProbeCandidateCapacity=statistics.screenProbeUniformCapacity*s.candidateCount;
    statistics.screenProbePlacementFrozen=!refresh;
    if(drawDiagnostics)RecordScreenProbeDebug(encoder,s,c,image,rayImage,post);
}

// Draw only after composing GI: the diagnostic colors are never radiance.
void RecordScreenProbeDebug(RHIFrameEncoder& encoder,const ScreenProbePlacementSettings& s,ScreenProbePlacementConstants c,
    unsigned image,unsigned rayImage,const PostProcessConstants& post) {
    const bool volumeView=s.trace.enabled&&s.trace.sdf.enabled&&s.trace.sdf.visualMode!=0;
    const auto markers=[&](const Target& atlas,float set) {
        c.options.w=set;Upload(encoder,placementBuffer,&c,sizeof(c),ScreenProbePlacementBinding);
        BindTarget(encoder,scene);
        Require(encoder.BindPipeline(placementMarkerProgram)&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(atlas.color,0)&&
            encoder.BindTexture(s.trace.enabled?(s.trace.sdf.enabled?sdfRays[rayImage].color:traceRays[rayImage].color):RenderResourceHandle<RHITextureSpec>{},7)&&
            encoder.DrawIndexed(RHICommand::DrawIndexed{3,0,0,atlas.width*atlas.height/2,0}),"Cannot draw probe placement markers");
        ++statistics.screenProbePlacementPasses;
    };
    if(!volumeView){markers(placementUniform[image],0);
        if(s.debugView==1)markers(placementMarked[image],1);
        else if(s.adaptive)markers(placementSpawned[image],1);
        if(s.trace.enabled)RecordProbeSdfHitMarkers(encoder,s,rayImage);}
    if(s.trace.enabled&&s.trace.showPanel&&!volumeView) {
        BindTarget(encoder,placementCoverage);
        Require(encoder.BindPipeline(traceDisplayProgram)&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(scene.color,0)&&
            encoder.BindTexture(s.trace.sdf.enabled?sdfRays[rayImage].color:traceRays[rayImage].color,7)&&encoder.BindTexture(placementUniform[image].color,3)&&
            encoder.BindTexture(placementSpawned[image].color,4)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot draw screen ray panel");
        Fullscreen(encoder,scene,copy,placementCoverage.color,{},{},post);++statistics.screenProbeTracePasses;
    }
    if(s.trace.enabled&&!volumeView)RecordProbeSdfSlice(encoder,s,post);
}
