// Included inside MaterialRenderPipeline::Impl. Lighting persists only as a
// camera-independent card solution; every probe gather is evaluated afresh.
void PrepareSurfaceCacheGi(const SurfaceCacheGiSettings& s,const ScreenProbePlacementSettings& placement) {
    if(!s.enabled)return;
    if(!surfaceCacheConstantsOwner) {
        PipelineDetail::ResourceBuilder b(device);
        surfaceCacheBuffer=b.Uniform(sizeof(SurfaceCacheGiConstants));surfaceCacheConstantsOwner=b.Finish();
    }
    if(s.enabled&&!surfaceCacheProgramsOwner) {
        PipelineDetail::ResourceBuilder b(device);const auto vertex=FullscreenVertexShader();
        surfaceCacheLightingProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::LightingShader());
        surfaceCacheGatherProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::GatherShader());
        surfaceCacheResolveProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ResolveShader());
        surfaceCacheCoverageProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ResolveShader(true));
        surfaceCacheProbeIntegrateProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ProbeIntegrateShader());
        surfaceCacheProbeReconstructProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ProbeReconstructShader());
        surfaceCacheProbeCoverageProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ProbeReconstructShader(true));
        surfaceCacheProgramsOwner=b.Finish();
    }
    if(s.enabled&&s.useScreenProbes&&(s.view==0||s.view==2||s.view==6||(s.view==5&&s.temporal.enabled)||s.probeDiagnostics)) {
        const auto w=placementUniform[0].width/2,h=placementUniform[0].height*(1+placement.candidateCount);
        if(!surfaceCacheProbeOwner||surfaceCacheProbeIrradiance.width!=w||surfaceCacheProbeIrradiance.height!=h) {
            PipelineDetail::ResourceBuilder b(device);Target t;t.width=w;t.height=h;
            t.color=b.Texture(w,h,RHITextureFormat::RGBA32F);t.framebuffer=b.Target(t.color,{});
            auto owner=b.Finish();surfaceCacheProbeIrradiance=t;surfaceCacheProbeOwner=std::move(owner);
        }
    }
    const auto& cache=*s.cache;const auto& geometry=*s.scene->PackedGeometry();
    Require(cache.TriangleCount()==geometry.TriangleCount(),"Surface Cache owner map does not match trace geometry");
    const bool cacheChanged=surfaceCacheAsset!=s.cache||surfaceCacheGeneration!=cache.Generation();
    const bool geometryChanged=surfaceCacheGeometryGeneration!=s.scene->GeometryGeneration()||surfaceCacheGeometryScene!=s.scene;
    const bool recreate=!surfaceCacheDataOwner||surfaceCacheAttributeHeight!=cache.AtlasHeight()||
        surfaceCacheMetadataHeight!=cache.MetadataHeight()||surfaceCacheGeometryHeight!=geometry.Height();
    if(recreate) {
        PipelineDetail::ResourceBuilder b(device);
        const auto texture=[&](const std::vector<glm::vec4>& pixels,unsigned w) {
            CreateRHITextureSpec d;d.width=w;d.height=unsigned(pixels.size()/w);d.data=pixels.data();
            d.textureDataStoreType=d.textureUseType=RHITextureFormat::RGBA32F;d.mipmaps=false;
            d.filterMode=RHIFilterMode::Nearest;d.addressMode=RHIAddressMode::ClampToEdge;
            return b.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(d,cb);});
        };
        auto attributes=texture(cache.Attributes(),cache.Width()),metadata=texture(cache.Metadata(),cache.Width());
        auto triangles=texture(geometry.Pixels(),geometry.Width());
        Target lighting;lighting.width=cache.Width();lighting.height=cache.PlaneTexels()/cache.Width();
        lighting.color=b.Texture(lighting.width,lighting.height,RHITextureFormat::RGBA32F);lighting.framebuffer=b.Target(lighting.color,{});
        auto owner=b.Finish();surfaceCacheAttributes=attributes;surfaceCacheMetadata=metadata;surfaceCacheGeometry=triangles;
        surfaceCacheLighting=lighting;surfaceCacheDataOwner=std::move(owner);surfaceCacheAsset=s.cache;surfaceCacheGeometryScene=s.scene;
        surfaceCacheAttributeHeight=cache.AtlasHeight();surfaceCacheMetadataHeight=cache.MetadataHeight();surfaceCacheGeometryHeight=geometry.Height();
        surfaceCacheCompleted={};
    } else if(cacheChanged||geometryChanged) {
        struct Completion {unsigned pending=0;bool success=true;};auto done=std::make_shared<Completion>();
        const auto update=[&](RenderResourceHandle<RHITextureSpec> texture,const std::vector<glm::vec4>& pixels,unsigned w) {
            UpdateRHITextureDesc d;d.width=w;d.height=unsigned(pixels.size()/w);d.format=RHITextureFormat::RGBA32F;
            d.data=pixels.data();d.byteSize=pixels.size()*sizeof(glm::vec4);++done->pending;
            device.async_UpdateTexture(texture,d,[done](bool ok){done->success&=ok;--done->pending;});
        };
        if(cacheChanged){update(surfaceCacheAttributes,cache.Attributes(),cache.Width());update(surfaceCacheMetadata,cache.Metadata(),cache.Width());}
        if(geometryChanged)update(surfaceCacheGeometry,geometry.Pixels(),geometry.Width());
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(done->pending){device.returnSystem.DrainCallbacks();Require(std::chrono::steady_clock::now()<deadline,"Surface Cache uploads timed out");
            if(done->pending)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(1));}
        Require(done->success,"Cannot upload changed Surface Cache data");
    }
    surfaceCacheAsset=s.cache;surfaceCacheGeometryScene=s.scene;
    surfaceCacheGeneration=cache.Generation();surfaceCacheGeometryGeneration=s.scene->GeometryGeneration();
    if(!s.enabled)return; // Direct area shadows do not need screen-space GI targets.
    if(!surfaceCacheViewOwner||surfaceCacheRaw.width!=width||surfaceCacheRaw.height!=height) {
        PipelineDetail::ResourceBuilder b(device);
        const auto target=[&](){Target t;t.width=width;t.height=height;t.color=b.Texture(width,height,RHITextureFormat::RGBA32F);t.framebuffer=b.Target(t.color,{});return t;};
        auto raw=target(),resolved=target(),coverage=target(),source=target(),probeCoverage=target();auto owner=b.Finish();
        surfaceCacheRaw=raw;surfaceCacheResolved=resolved;surfaceCacheCoverage=coverage;surfaceCacheSource=source;surfaceCacheViewOwner=std::move(owner);
        surfaceCacheProbeCoverage=probeCoverage;
    }
    statistics.surfaceCacheTexels=cache.TexelCount();
}
SurfaceCacheGiConstants SurfaceCacheConstants(const SurfaceCacheGiSettings& s,const MaterialPipelineSettings& settings,const PipelineCamera& camera) const {
    SurfaceCacheGiConstants c;const auto& cache=*s.cache;const auto& geometry=*s.scene->PackedGeometry();
    c.inverseView=glm::inverse(camera.view);
    c.atlas={int(cache.Width()),int(cache.PlaneTexels()),int(cache.TexelCount()),int(cache.CardCount())};
    c.geometry={int(geometry.NodeCount()*2),int(geometry.Width()),int(geometry.NodeCount()),int(geometry.TriangleCount())};
    c.screen={int(width),int(height),int(s.raysPerPixel),int(s.view)};
    const auto debug=(s.view==2&&s.useScreenProbes)?s.reconstructionDebug:0u;
    c.parameters={s.maxDistance,s.rayBias,s.intensity,float((s.cacheBilinear?3u:1u)|(debug<<3))};
    c.sunDirectionIntensity=glm::vec4(glm::normalize(settings.shadows.sunDirection),settings.shadows.sunIntensity);
    c.sunColor=glm::vec4(settings.shadows.sunColor,glm::uintBitsToFloat(settings.screenProbePlacement.trace.samplingSeed));c.spot=MakeSpotLightConstants(settings.spotLight);return c;
}
void BindSurfaceCache(RHIFrameEncoder& encoder) {
    Require(encoder.BindTexture(surfaceCacheAttributes,12)&&encoder.BindTexture(surfaceCacheMetadata,13)&&
        encoder.BindTexture(surfaceCacheLighting.color,14),"Cannot bind card Surface Cache");
}
void RecordSurfaceCacheGi(RHIFrameEncoder& encoder,const RenderFrame& frame,const PipelineCamera& camera,
    const MaterialPipelineSettings& settings,const SceneEffectsConstants& effects,const SurfacePassConstants& surface,const PostProcessConstants& post,bool cameraCut) {
    const auto& s=settings.surfaceCacheGi;if(!s.enabled)return;
    const auto c=SurfaceCacheConstants(s,settings,camera);Upload(encoder,surfaceCacheBuffer,&c,sizeof(c),SurfaceCacheGiBinding);
    std::uint64_t key=14695981039346656037ull;
    const auto add=[&](const auto& value){const auto* p=reinterpret_cast<const unsigned char*>(&value);for(std::size_t i=0;i<sizeof(value);++i){key^=p[i];key*=1099511628211ull;}};
    add(surfaceCacheGeneration);add(surfaceCacheGeometryGeneration);add(settings.lighting.lightPositions);add(settings.lighting.lightColors);
    add(reinterpret_cast<std::uintptr_t>(s.cache.get()));add(reinterpret_cast<std::uintptr_t>(s.scene.get()));
    add(c.sunDirectionIntensity);add(glm::vec3(c.sunColor));add(c.spot);add(s.maxDistance);add(s.rayBias);
    const auto areas=MakeAreaLightsConstants(settings.areaLights);add(areas);
    const bool relight=!surfaceCacheCompleted.valid||surfaceCacheCompleted.lightingKey!=key;
    // The lighting target must not still be bound for sampling while it is
    // attached for writing. Unbound optimized declarations remain harmless.
    if(relight) {
        Require(encoder.BindTexture({},14),"Cannot unbind Surface Cache lighting");
        BindTarget(encoder,surfaceCacheLighting);
        Require(encoder.BindPipeline(surfaceCacheLightingProgram)&&encoder.BindMesh(fullscreen)&&
            encoder.BindTexture(surfaceCacheAttributes,12)&&encoder.BindTexture(surfaceCacheMetadata,13)&&encoder.BindTexture(surfaceCacheGeometry,9)&&
            encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot light Surface Cache cards");
        ++statistics.surfaceCachePasses;statistics.surfaceCacheUpdatedTexels=s.cache->TexelCount();statistics.surfaceCacheLightingUpdated=true;
    }
    auto captureEffects=effects;captureEffects.effects.y=0;captureEffects.screenAndAo.z=0;Effects(encoder,captureEffects);
    auto captureSurface=surface;captureSurface.options=glm::vec4(0,0,2,0);captureSurface.projection=camera.projection;
    Upload(encoder,surfaceBuffer,&captureSurface,sizeof(captureSurface),SurfacePassBinding);
    BindTarget(encoder,diffuseCapture,RHICommand::ClearColor|RHICommand::ClearDepth);
    Depth(encoder,frame,camera.projection*camera.view,false);
    // The existing DiffuseRadiance material variant explicitly returns rho
    // under capture mode 2; it includes texture and metallic material factors.
    CaptureSurface(encoder,frame,camera,false);
    BindTarget(encoder,normalCapture,RHICommand::ClearColor);CaptureSurface(encoder,frame,camera,true);
    const auto geometry=MakeIndirectLightingConstants(IndirectLightingSettings{},camera.projection,camera.view);
    Upload(encoder,indirectBuffer,&geometry,sizeof(geometry),IndirectLightingBinding);
    Fullscreen(encoder,indirectPositions,indirectGeometry,{},{},diffuseCapture.depth,post);
    Fullscreen(encoder,surfaceCacheSource,copy,scene.color,{},{},post);
    // Leave the lighting attachment before sampling it. Also restore cache
    // bindings after material captures have installed their texture slots.
    BindSurfaceCache(encoder);
    const bool gather=s.view==0||s.view==2||(s.view==5&&SurfaceCacheTemporalActive(s));
    const bool probes=s.useScreenProbes&&(gather||s.view==6||s.probeDiagnostics);
    const bool temporal=SurfaceCacheTemporalActive(s);
    const auto historyKey=temporal?SurfaceCacheHistoryKey(frame,settings):0;
    const bool historyValid=temporal&&surfaceCacheTemporalCompleted.valid&&!s.temporal.reset&&
        !cameraCut&&surfaceCacheTemporalCompleted.key==historyKey;
    const auto temporalSequence=historyValid?surfaceCacheTemporalCompleted.sequence:0;
    surfaceCachePending={true,key,probes};
    if(probes) {
        // Reuse the receiver capture (including rho). Placement must not
        // overwrite it or render diagnostics before the lighting resolve.
        auto placement=settings.screenProbePlacement;
        if(temporal)placement.trace.samplingSeed=(placement.trace.samplingSeed+std::uint32_t(temporalSequence%s.temporal.directionPhases))&0x1fffffffu;
        RecordScreenProbePlacement(encoder,frame,camera,placement,effects,surface,post,false,false);
        Upload(encoder,surfaceCacheBuffer,&c,sizeof(c),SurfaceCacheGiBinding);
        Upload(encoder,placementBuffer,&placementFrameConstants,sizeof(placementFrameConstants),ScreenProbePlacementBinding);
        BindSurfaceCache(encoder);
        const auto image=placementPending.image,rayImage=tracePending.image;
        BindTarget(encoder,surfaceCacheProbeIrradiance);
        Require(encoder.BindPipeline(surfaceCacheProbeIntegrateProgram)&&encoder.BindMesh(fullscreen)&&
            encoder.BindTexture(placementUniform[image].color,0)&&encoder.BindTexture(placementSpawned[image].color,1)&&
            encoder.BindTexture(sdfRays[rayImage].color,7)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot integrate probe irradiance");
        if(gather) {
            BindTarget(encoder,surfaceCacheRaw);
            Require(encoder.BindPipeline(surfaceCacheProbeReconstructProgram)&&encoder.BindMesh(fullscreen)&&
                encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&encoder.BindTexture(diffuseCapture.color,4)&&
                encoder.BindTexture(placementUniform[image].color,5)&&encoder.BindTexture(placementSpawned[image].color,6)&&
                encoder.BindTexture(surfaceCacheProbeIrradiance.color,7)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot reconstruct probe GI");
        } else BindTarget(encoder,surfaceCacheRaw,RHICommand::ClearColor);
        const bool collectCoverage=s.collectProbeCoverage||s.view==6;
        if(collectCoverage) {
            BindTarget(encoder,surfaceCacheProbeCoverage);
            Require(encoder.BindPipeline(surfaceCacheProbeCoverageProgram)&&encoder.BindMesh(fullscreen)&&
                encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&encoder.BindTexture(diffuseCapture.color,4)&&
                encoder.BindTexture(placementUniform[image].color,5)&&encoder.BindTexture(placementSpawned[image].color,6)&&
                encoder.BindTexture(surfaceCacheProbeIrradiance.color,7)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot diagnose geometric probe coverage");
        }
        statistics.surfaceCacheProbeCoverageCollected=collectCoverage;
        statistics.surfaceCacheProbePasses=unsigned(gather)+1+unsigned(collectCoverage);
        statistics.surfaceCacheProbeSlots=placementFrameConstants.traceAtlas.w;
        statistics.surfaceCacheProbeRayBudget=std::uint64_t(statistics.surfaceCacheProbeSlots)*settings.screenProbePlacement.trace.rayResolution*settings.screenProbePlacement.trace.rayResolution;
        statistics.surfaceCacheRays=statistics.surfaceCacheProbeRayBudget;
    } else {
        BindTarget(encoder,surfaceCacheRaw,gather?RHICommand::ClearNone:RHICommand::ClearColor);
        if(gather)Require(encoder.BindPipeline(surfaceCacheGatherProgram)&&encoder.BindMesh(fullscreen)&&
            encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&encoder.BindTexture(diffuseCapture.color,4)&&
            encoder.BindTexture(surfaceCacheGeometry,9)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot gather raw software GI");
        statistics.surfaceCacheRays=gather?std::uint64_t(width)*height*s.raysPerPixel:0;
    }
    const bool spatial=SurfaceCacheSpatialActive(s);
    if(spatial)RecordSurfaceCacheSpatial(encoder,s);
    const auto& giSignal=spatial?surfaceCacheSpatialGi:surfaceCacheRaw;
    if(temporal)RecordSurfaceCacheTemporal(encoder,s,camera,historyKey,temporalSequence,historyValid,giSignal);
    const auto resolve=[&](const Target& target,RenderResourceHandle<PipelineSpec> program,bool rawCoverage=false) {
        BindTarget(encoder,target);Require(encoder.BindPipeline(program)&&encoder.BindMesh(fullscreen)&&
            encoder.BindTexture(surfaceCacheSource.color,0)&&encoder.BindTexture(rawCoverage?surfaceCacheRaw.color:(temporal?surfaceCacheTemporalGi.color:giSignal.color),1)&&
            encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&
            encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot resolve card Surface Cache GI");
    };
    resolve(surfaceCacheCoverage,surfaceCacheCoverageProgram,true);
    if(s.view==5) {
        BindTarget(encoder,surfaceCacheResolved,temporal?RHICommand::ClearNone:RHICommand::ClearColor);
        if(temporal)Require(encoder.BindPipeline(surfaceCacheTemporalCountProgram)&&encoder.BindMesh(fullscreen)&&
            encoder.BindTexture(surfaceCacheTemporalHistory[surfaceCacheTemporalPending.image].color,0)&&encoder.BindTexture(surfaceCacheRaw.color,1)&&
            encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot display GI history length");
    } else if(s.view==6) {
        if(probes)Fullscreen(encoder,surfaceCacheResolved,copy,surfaceCacheProbeCoverage.color,{},{},post);
        else BindTarget(encoder,surfaceCacheResolved,RHICommand::ClearColor);
    }
    else resolve(surfaceCacheResolved,surfaceCacheResolveProgram);
    Fullscreen(encoder,scene,copy,surfaceCacheResolved.color,{},{},post);
    if(probes&&s.probeDiagnostics)RecordScreenProbeDebug(encoder,settings.screenProbePlacement,placementFrameConstants,placementPending.image,tracePending.image,post);
    Upload(encoder,surfaceBuffer,&surface,sizeof(surface),SurfacePassBinding);Effects(encoder,effects);
    statistics.surfaceCachePasses+=(gather?9:8)+(probes?statistics.surfaceCacheProbePasses-unsigned(gather):0);
}
