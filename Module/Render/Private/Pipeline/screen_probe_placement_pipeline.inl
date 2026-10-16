// Included inside MaterialRenderPipeline::Impl. Placement is independent of all
// GI resources. Raster passes keep fixed candidate slots rather than UE's atomic
// compact atlas and global adaptive allocation budget.
void PrepareScreenProbePlacement(const ScreenProbePlacementSettings& s) {
    if(!s.enabled)return;
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
    const auto gw=std::max(1u,(width+s.baseSpacing-1)/s.baseSpacing),gh=std::max(1u,(height+s.baseSpacing-1)/s.baseSpacing);
    const auto candidateWidth=gw*s.candidateCount*2;
    Require(candidateWidth<=8192,"Screen probe candidate atlas exceeds 8192 texels; increase spacing");
    if(!placementTargetsOwner||placementUniform[0].width!=gw*2||placementUniform[0].height!=gh||
        placementMarked[0].width!=candidateWidth||placementCoverage.width!=width||placementCoverage.height!=height) {
        PipelineDetail::ResourceBuilder b(device);
        const auto target=[&](unsigned w,unsigned h){Target t;t.width=w;t.height=h;
            t.color=b.Texture(w,h,RHITextureFormat::RGBA32F);t.framebuffer=b.Target(t.color,{});return t;};
        std::array<Target,2> uniform,marked,spawned;
        for(unsigned i=0;i<2;++i){uniform[i]=target(gw*2,gh);marked[i]=target(candidateWidth,gh);spawned[i]=target(candidateWidth,gh);}
        // Shared debug target also stores SDF view ray distance in alpha.
        // Preserve it at full precision so fine-field differences are not
        // dominated by half-float depth quantization during shape validation.
        auto coverage=target(width,height);auto owner=b.Finish();
        placementUniform=uniform;placementMarked=marked;placementSpawned=spawned;placementCoverage=coverage;
        placementTargetsOwner=std::move(owner);placementCompleted={};
    }
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
        traceDisplayProgram=b.FullscreenPipeline(vertex,ScreenProbeTraceDetail::DisplayShader());
        traceProgramsOwner=b.Finish();
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
}

void RecordScreenProbePlacement(RHIFrameEncoder& encoder,const RenderFrame& frame,const PipelineCamera& camera,
    const ScreenProbePlacementSettings& s,const SceneEffectsConstants& effects,const SurfacePassConstants& surface,const PostProcessConstants& post,
    bool captureSurface=true,bool drawDiagnostics=true) {
    if(s.trace.enabled&&s.trace.sdf.enabled&&s.trace.sdf.visualMode!=0) {
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
    bool cameraCompatible=true;
    if(s.trace.cosineHemisphere)for(int col=0;col<4;++col)for(int row=0;row<4;++row)
        cameraCompatible&=std::abs(placementCompleted.viewProjection[col][row]-currentViewProjection[col][row])<1e-6f;
    // GI reconstruction addresses screen tiles, so a camera change must
    // regenerate that stencil even when diagnostic placement is frozen.
    const bool compatible=placementCompleted.valid&&cameraCompatible&&placementCompleted.sourceWidth==width&&placementCompleted.sourceHeight==height&&
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
    const auto rayImage=traceCompleted.valid?1-traceCompleted.image:0;
    if(s.trace.enabled) {
        const auto mode=1u|(s.trace.cosineHemisphere?2u:0u)|(s.trace.samplingSeed<<2);
        c.tracing=glm::ivec4(int(s.trace.rayResolution),int(s.trace.maxSteps),int(std::min(s.trace.maxMip,traceHzbLevels-1)),int(mode));
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
        IndirectLightingSettings geometrySettings;
        const auto geometry=MakeIndirectLightingConstants(geometrySettings,camera.projection,camera.view);
        Upload(encoder,indirectBuffer,&geometry,sizeof(geometry),IndirectLightingBinding);
        Fullscreen(encoder,indirectPositions,indirectGeometry,{},{},diffuseCapture.depth,post);
        statistics.screenProbePlacementPasses+=3;
    }
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
        draw(placementUniform[image],placementUniformProgram);
        draw(placementMarked[image],placementMarkProgram);
        draw(placementSpawned[image],placementSpawnProgram);
        placementPending={true,image,phase,s.baseSpacing,s.candidateCount,width,height,s.relativeDepthThreshold,s.adaptive,sequence+1,s.trace.cosineHemisphere,currentViewProjection};
    } else placementPending=placementCompleted;
    if(s.trace.enabled) {
        // Snapshot HDR before every visualization pass. Diagnostics must never
        // become next frame's radiance or leak into the trace source.
        Fullscreen(encoder,traceSource,copy,surfaceCachePending.valid?surfaceCacheSource.color:scene.color,{},{},post);
        const auto hzbDraw=[&](const Target& target,RenderResourceHandle<PipelineSpec> program,RenderResourceHandle<RHITextureSpec> input) {
            BindTarget(encoder,target);
            Require(encoder.BindPipeline(program)&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(input,0)&&
                encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot record HZB reduction");
            ++statistics.screenProbeTracePasses;
        };
        hzbDraw(traceHzb[0],traceHzbBaseProgram,indirectPositions.color);
        for(unsigned level=1;level<traceHzbLevels;++level)hzbDraw(traceHzb[level],traceHzbReduceProgram,traceHzb[level-1].color);
        BindTarget(encoder,traceRays[rayImage]);
        Require(encoder.BindPipeline(traceRayProgram)&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(traceSource.color,0)&&
            encoder.BindTexture(normalCapture.color,1)&&encoder.BindTexture(indirectPositions.color,2)&&
            encoder.BindTexture(placementUniform[image].color,3)&&encoder.BindTexture(placementSpawned[image].color,4),"Cannot bind screen trace sources");
        for(unsigned level=0;level<ScreenProbeTraceMaxHzbLevels;++level)
            Require(encoder.BindTexture(traceHzb[std::min(level,traceHzbLevels-1)].color,5+level),"Cannot bind HZB level");
        Require(encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot record screen probe rays");
        ++statistics.screenProbeTracePasses;statistics.screenProbeHzbLevels=traceHzbLevels;
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
