// Included inside MaterialRenderPipeline::Impl. Lighting persists only as a
// camera-independent card solution; every probe gather is evaluated afresh.
bool PrepareSurfaceCacheHardwareCapability() {
    if(!surfaceCacheHardwareCapabilityChecked) {
        struct Capability {bool done=false,supported=false;};
        auto result=std::make_shared<Capability>();
        device.async_ExecuteCode([this,result]{result->supported=device.BackendDiagnostics()->HardwareRayTracingAvailable();},
            [result]{result->done=true;});
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(!result->done){device.returnSystem.DrainCallbacks();
            Require(std::chrono::steady_clock::now()<deadline,"Native Surface Cache capability query timed out");
            if(!result->done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(1));}
        surfaceCacheHardwareSupported=result->supported;surfaceCacheHardwareCapabilityChecked=true;
    }
    return surfaceCacheHardwareSupported;
}
void PrepareSurfaceCacheGi(const SurfaceCacheGiSettings& s,const ScreenProbePlacementSettings& placement) {
    surfaceCacheNativeOriginsActive=surfaceCacheReusedNativeOriginsActive=false;
    surfaceCacheQueryProbeOriginActive=false;
    if(!s.enabled)return;
    if(!surfaceCacheConstantsOwner) {
        PipelineDetail::ResourceBuilder b(device);
        surfaceCacheBuffer=b.Uniform(sizeof(SurfaceCacheGiConstants));
        surfaceCacheOriginSafetyBuffer=b.Uniform(sizeof(SurfaceCacheOriginSafetyConstants));surfaceCacheConstantsOwner=b.Finish();
    }
    if(s.enabled&&!surfaceCacheProgramsOwner) {
        PipelineDetail::ResourceBuilder b(device);const auto vertex=FullscreenVertexShader();
        surfaceCacheLightingProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::LightingShader());
        surfaceCacheGatherProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::GatherShader());
        surfaceCacheResolveProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ResolveShader());
        surfaceCacheCoverageProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ResolveShader(true));
        surfaceCacheProbeIntegrateProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ProbeIntegrateShader());
        surfaceCacheProbeOriginProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ProbeOriginShader());
        surfaceCacheProbeOriginMarkerProgram=b.FullscreenPipeline(SurfaceCacheDetail::ProbeOriginMarkerVertexShader(),SurfaceCacheDetail::ProbeOriginMarkerFragmentShader());
        surfaceCacheProbeActiveMaskProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ProbeActiveMaskShader());
        surfaceCacheProbeDirectionalProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ProbeDirectionalIntegrateShader());
        surfaceCacheProbeReconstructProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ProbeReconstructShader());
        surfaceCacheProbeCoverageProgram=b.FullscreenPipeline(vertex,SurfaceCacheDetail::ProbeReconstructShader(true));
        surfaceCacheProgramsOwner=b.Finish();
    }
    if(s.hardwareCacheVisibility&&!surfaceCacheNativeLightingProgramsOwner&&PrepareSurfaceCacheHardwareCapability()) {
        PipelineDetail::ResourceBuilder b(device);
        surfaceCacheNativeLightingProgram=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::LightingShader(true));
        surfaceCacheNativeLightingProgramsOwner=b.Finish();
    }
    if(s.hardwareOriginTracing&&PrepareSurfaceCacheHardwareCapability()) {
        auto& owner=s.reuseOriginSoftwareCertification?surfaceCacheReusedNativeProbeOriginProgramsOwner:surfaceCacheNativeProbeOriginProgramsOwner;
        auto& program=s.reuseOriginSoftwareCertification?surfaceCacheReusedNativeProbeOriginProgram:surfaceCacheNativeProbeOriginProgram;
        if(!owner) {
            PipelineDetail::ResourceBuilder b(device);
            program=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::ProbeOriginShader(true,s.reuseOriginSoftwareCertification));
            owner=b.Finish();
        }
    }
    surfaceCacheReusedNativeOriginsActive=s.hardwareOriginTracing&&s.reuseOriginSoftwareCertification&&surfaceCacheHardwareSupported&&
        bool(surfaceCacheReusedNativeProbeOriginProgramsOwner);
    surfaceCacheNativeOriginsActive=surfaceCacheReusedNativeOriginsActive||
        (s.hardwareOriginTracing&&!s.reuseOriginSoftwareCertification&&surfaceCacheHardwareSupported&&bool(surfaceCacheNativeProbeOriginProgramsOwner));
    if(s.reuseClosestTraversal||s.fastGeometryAddressing) {
        surfaceCacheQueryProbeOriginKey=(s.reuseClosestTraversal?1u:0u)|(s.fastGeometryAddressing?2u:0u)|
            (surfaceCacheNativeOriginsActive?4u:0u)|(surfaceCacheReusedNativeOriginsActive?8u:0u);
        auto& owner=surfaceCacheQueryProbeOriginOwners[surfaceCacheQueryProbeOriginKey];
        if(!owner) {
            PipelineDetail::ResourceBuilder b(device);
            surfaceCacheQueryProbeOriginPrograms[surfaceCacheQueryProbeOriginKey]=b.FullscreenPipeline(FullscreenVertexShader(),
                SurfaceCacheDetail::ProbeOriginShader(surfaceCacheNativeOriginsActive,surfaceCacheReusedNativeOriginsActive,
                    s.reuseClosestTraversal,s.fastGeometryAddressing));
            owner=b.Finish();
        }
        surfaceCacheQueryProbeOriginActive=true;
    }
    if(s.enabled&&s.useScreenProbes&&!s.nativeScreenProbeTransport&&!s.splitReceiverGather&&(s.view==0||s.view==2||s.view==6||(s.view==5&&s.temporal.enabled)||s.probeDiagnostics||s.probeOriginDiagnostics)) {
        const auto w=placementUniform[0].width/2,h=placementUniform[0].height*(1+placement.candidateCount);
        if(!surfaceCacheProbeOwner||surfaceCacheProbeIrradiance.width!=w||surfaceCacheProbeIrradiance.height!=h) {
            PipelineDetail::ResourceBuilder b(device);Target t;t.width=w;t.height=h;
            t.color=b.Texture(w,h,RHITextureFormat::RGBA32F);t.framebuffer=b.Target(t.color,{});
            Target origins;origins.width=w;origins.height=h;
            origins.color=b.Texture(w,h,RHITextureFormat::RGBA32F);origins.framebuffer=b.Target(origins.color,{});
            Target directional;directional.width=w*4;directional.height=h;
            directional.color=b.Texture(w*4,h,RHITextureFormat::RGBA32F);directional.framebuffer=b.Target(directional.color,{});
            Target activeMask;activeMask.width=w;activeMask.height=placementUniform[0].height;
            activeMask.color=b.Texture(activeMask.width,activeMask.height,RHITextureFormat::RGBA32F);activeMask.framebuffer=b.Target(activeMask.color,{});
            auto owner=b.Finish();surfaceCacheProbeIrradiance=t;surfaceCacheProbeOrigins=origins;surfaceCacheProbeDirectional=directional;surfaceCacheProbeActiveMask=activeMask;surfaceCacheProbeOwner=std::move(owner);
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
        auto attributes=texture(cache.UploadAttributes(),cache.Width()),metadata=texture(cache.Metadata(),cache.Width());
        auto triangles=texture(geometry.Pixels(),geometry.Width());
        Target lighting;lighting.width=cache.Width();lighting.height=cache.PlaneTexels()/cache.Width();
        lighting.color=b.Texture(lighting.width,lighting.height,RHITextureFormat::RGBA32F);lighting.framebuffer=b.Target(lighting.color,{});
        auto owner=b.Finish();surfaceCacheAttributes=attributes;surfaceCacheMetadata=metadata;surfaceCacheGeometry=triangles;
        surfaceCacheLighting=lighting;surfaceCacheDataOwner=std::move(owner);surfaceCacheAsset=s.cache;surfaceCacheGeometryScene=s.scene;
        surfaceCacheAttributeHeight=cache.AtlasHeight();surfaceCacheMetadataHeight=cache.MetadataHeight();surfaceCacheGeometryHeight=geometry.Height();
        statistics.surfaceCacheAttributeUploadedTexels=cache.UploadAttributes().size();
        statistics.surfaceCacheMetadataUploadedTexels=cache.Metadata().size();
        statistics.surfaceCacheGeometryUploadedTexels=geometry.Pixels().size();statistics.surfaceCacheUploadRegions=3;
        surfaceCacheCompleted={};
    } else if(cacheChanged||geometryChanged) {
        struct Completion {unsigned pending=0;bool success=true;};auto done=std::make_shared<Completion>();
        const auto update=[&](RenderResourceHandle<RHITextureSpec> texture,const std::vector<glm::vec4>& pixels,unsigned w,
                              unsigned firstRow,unsigned rows,std::uint64_t& uploadedTexels) {
            Require(rows&&std::size_t(firstRow+rows)*w<=pixels.size(),"Invalid Surface Cache upload rows");
            UpdateRHITextureDesc d;d.y=firstRow;d.width=w;d.height=rows;d.format=RHITextureFormat::RGBA32F;
            d.deferUntilFrame=s.deferResourceUploads;
            d.data=pixels.data()+std::size_t(firstRow)*w;d.byteSize=std::size_t(w)*rows*sizeof(glm::vec4);++done->pending;
            device.async_UpdateTexture(texture,d,[done](bool ok){done->success&=ok;--done->pending;});
            uploadedTexels+=std::uint64_t(w)*rows;++statistics.surfaceCacheUploadRegions;
        };
        const auto fullUpdate=[&](RenderResourceHandle<RHITextureSpec> texture,const std::vector<glm::vec4>& pixels,unsigned w,
                                  std::uint64_t& uploadedTexels) {
            update(texture,pixels,w,0,unsigned(pixels.size()/w),uploadedTexels);
        };
        if(cacheChanged) {
            const auto& dirty=cache.DirtyCards();
            // DirtyCards describes only the most recent Update. Catch up in full
            // after missed generations, no-op updates that cleared the list,
            // cache replacement, or layout/owner-map changes. Retain the old
            // immutable geometry so its topology identity cannot be recycled.
            const bool incremental=surfaceCacheAsset==s.cache&&surfaceCacheGeometryScene==s.scene&&
                cache.Generation()>surfaceCacheGeneration&&cache.Generation()-surfaceCacheGeneration==1&&
                surfaceCacheUploadedCardCount==cache.CardCount()&&surfaceCacheUploadedOwnerGeometry&&
                surfaceCacheUploadedOwnerGeometry->TopologyKey()==geometry.TopologyKey()&&
                cache.OwnerGeometry()&&cache.OwnerGeometry()->TopologyKey()==geometry.TopologyKey()&&!dirty.empty()&&
                std::all_of(dirty.begin(),dirty.end(),[&](unsigned card){return card<cache.CardCount();});
            if(incremental) {
                struct Rows {unsigned first,count;};
                const auto dirtyUpdate=[&](RenderResourceHandle<RHITextureSpec> texture,const std::vector<glm::vec4>& pixels,
                                           const std::vector<std::uint32_t>& cards,unsigned cardTexels,unsigned planes,unsigned planeTexels,std::uint64_t& uploadedTexels) {
                    std::vector<Rows> ranges;ranges.reserve(cards.size()*planes);const auto w=cache.Width();
                    for(unsigned plane=0;plane<planes;++plane)for(const auto card:cards) {
                        const auto first=plane*planeTexels+card*cardTexels,last=first+cardTexels;
                        const auto firstRow=first/w,endRow=(last+w-1)/w;
                        ranges.push_back({firstRow,endRow-firstRow});
                    }
                    std::sort(ranges.begin(),ranges.end(),[](const Rows& a,const Rows& b){return a.first<b.first;});
                    // Whole packed rows avoid temporary repacking and preserve
                    // every untouched texel sharing a row with a changed card.
                    for(std::size_t first=0;first<ranges.size();) {
                        auto endRow=ranges[first].first+ranges[first].count;std::size_t end=first+1;
                        while(end<ranges.size()&&ranges[end].first<=endRow) {
                            endRow=std::max(endRow,ranges[end].first+ranges[end].count);++end;
                        }
                        update(texture,pixels,w,ranges[first].first,endRow-ranges[first].first,uploadedTexels);first=end;
                    }
                };
                const auto resolution=cache.Settings().pageResolution;
                dirtyUpdate(surfaceCacheAttributes,cache.UploadAttributes(),cache.AttributeDirtyCards(),resolution*resolution,cache.AttributePlaneCount(),cache.PlaneTexels(),statistics.surfaceCacheAttributeUploadedTexels);
                dirtyUpdate(surfaceCacheMetadata,cache.Metadata(),dirty,SurfaceCacheCardTexels,1,0,statistics.surfaceCacheMetadataUploadedTexels);
            } else {
                fullUpdate(surfaceCacheAttributes,cache.UploadAttributes(),cache.Width(),statistics.surfaceCacheAttributeUploadedTexels);
                fullUpdate(surfaceCacheMetadata,cache.Metadata(),cache.Width(),statistics.surfaceCacheMetadataUploadedTexels);
            }
        }
        if(geometryChanged){
            const bool partial=s.sparseNativeVertexUpdates&&geometry.IncrementalUpdate()&&surfaceCacheGeometryScene==s.scene&&
                s.scene->GeometryGeneration()==surfaceCacheGeometryGeneration+1&&surfaceCacheUploadedOwnerGeometry&&
                surfaceCacheUploadedOwnerGeometry->TopologyKey()==geometry.TopologyKey();
            if(partial)for(const auto& range:geometry.DirtyRanges()){
                Require(range.first%geometry.Width()==0&&range.count%geometry.Width()==0,"Unaligned sparse geometry upload");
                update(surfaceCacheGeometry,geometry.Pixels(),geometry.Width(),range.first/geometry.Width(),range.count/geometry.Width(),statistics.surfaceCacheGeometryUploadedTexels);}
            else fullUpdate(surfaceCacheGeometry,geometry.Pixels(),geometry.Width(),statistics.surfaceCacheGeometryUploadedTexels);
        }
        // The coherent path queues both steady-state upload sets before one
        // acknowledgement barrier. Cold precision resources/topology changes
        // and all other paths retain their original synchronous preparation.
        const bool combinePrecision=s.coherentReceiverTracing&&s.splitReceiverGather&&s.deferResourceUploads&&
            transportPrecisionOwner&&transportPrecisionScene==s.scene.get()&&transportPrecisionGeometry&&
            transportPrecisionGeometry->TopologyKey()==geometry.TopologyKey()&&
            transportPrecisionGeometry->TriangleCount()==geometry.TriangleCount()&&
            transportPrecisionForInput.size()==geometry.TriangleCount()&&
            transportPrecisionInputs.size()==s.scene->Instances().size()&&
            transportPrecisionPixels.size()==((std::size_t(geometry.TriangleCount())*3+1023)/1024)*1024;
        if(combinePrecision)PrepareTransportPrecision(s,true);
        const auto precisionDone=combinePrecision?transportPrecisionUploads:nullptr;
        const auto pending=[&](){return done->pending||(precisionDone&&precisionDone->pending);};
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(pending()){device.returnSystem.DrainCallbacks();Require(std::chrono::steady_clock::now()<deadline,"Surface Cache uploads timed out");
            if(pending())device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(1));}
        Require(done->success,"Cannot upload changed Surface Cache data");
        if(combinePrecision)WaitTransportPrecisionUploads();
    }
    surfaceCacheAsset=s.cache;surfaceCacheGeometryScene=s.scene;
    surfaceCacheGeneration=cache.Generation();surfaceCacheGeometryGeneration=s.scene->GeometryGeneration();
    if(cacheChanged||recreate) {
        surfaceCacheUploadedOwnerGeometry=cache.OwnerGeometry();surfaceCacheUploadedCardCount=cache.CardCount();
    }
    if(!s.enabled)return; // Direct area shadows do not need screen-space GI targets.
    if(!surfaceCacheViewOwner||surfaceCacheRaw.width!=width||surfaceCacheRaw.height!=height||surfaceCacheHalfReceiverRadiance!=s.halfReceiverRadiance) {
        PipelineDetail::ResourceBuilder b(device);
        const auto target=[&](RHITextureFormat format=RHITextureFormat::RGBA32F){Target t;t.width=width;t.height=height;t.color=b.Texture(width,height,format);t.framebuffer=b.Target(t.color,{});return t;};
        auto raw=target(s.halfReceiverRadiance?RHITextureFormat::RGBA16F:RHITextureFormat::RGBA32F);
        auto resolved=target(),coverage=target(),source=target(),probeCoverage=target();auto owner=b.Finish();
        surfaceCacheRaw=raw;surfaceCacheResolved=resolved;surfaceCacheCoverage=coverage;surfaceCacheSource=source;surfaceCacheViewOwner=std::move(owner);
        surfaceCacheProbeCoverage=probeCoverage;
        surfaceCacheHalfReceiverRadiance=s.halfReceiverRadiance;
    }
    statistics.surfaceCacheTexels=cache.TexelCount();
    const bool actualLightingDiagnostics=s.ueLumenHigh&&placement.trace.sdf.collectRayDiagnostics;
    const bool replayLightingDiagnostics=!s.ueLumenHigh&&s.contact.collectSamplingDiagnostics&&s.hardwareCacheVisibility;
    if(actualLightingDiagnostics||replayLightingDiagnostics){
        if(replayLightingDiagnostics&&!lightingQueryProgramsOwner){PipelineDetail::ResourceBuilder b(device);
            lightingQueryProgram=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::LightingQueryDiagnosticShader());lightingQueryProgramsOwner=b.Finish();}
        if(!lightingQueryOwner||lightingQueryTarget.width!=surfaceCacheLighting.width||lightingQueryTarget.height!=surfaceCacheLighting.height){
            PipelineDetail::ResourceBuilder b(device);lightingQueryTarget.width=surfaceCacheLighting.width;lightingQueryTarget.height=surfaceCacheLighting.height;
            lightingQueryTarget.color=b.Texture(lightingQueryTarget.width,lightingQueryTarget.height,RHITextureFormat::RGBA32F);
            lightingQueryTarget.framebuffer=b.Target(lightingQueryTarget.color,{});lightingQueryOwner=b.Finish();}
        if(actualLightingDiagnostics){
            const bool native=s.hardwareCacheVisibility&&surfaceCacheHardwareSupported&&bool(surfaceCacheNativeLightingProgramsOwner);
            if(!lightingActualProgramsOwner||lightingActualNativeVisibility!=native){
                PipelineDetail::ResourceBuilder b(device);
                const auto program=b.FullscreenPipeline(FullscreenVertexShader(),SurfaceCacheDetail::LightingShader(native,true,true));
                auto owner=b.Finish();lightingActualProgram=program;lightingActualProgramsOwner=std::move(owner);lightingActualNativeVisibility=native;
            }
            if(!lightingActualOwner||lightingActualTarget.color!=surfaceCacheLighting.color||lightingActualQueryTexture!=lightingQueryTarget.color){
                PipelineDetail::ResourceBuilder b(device);auto target=surfaceCacheLighting;
                target.framebuffer=b.TargetColors({surfaceCacheLighting.color,lightingQueryTarget.color,{},{}});
                auto owner=b.Finish();lightingActualTarget=target;lightingActualOwner=std::move(owner);lightingActualQueryTexture=lightingQueryTarget.color;
            }
        }
    }
    if(s.ueLumenHigh)PrepareUeLumenDirectLightingSchedule(
        s.hardwareCacheVisibility&&surfaceCacheHardwareSupported&&bool(surfaceCacheNativeLightingProgramsOwner),actualLightingDiagnostics);
}
SurfaceCacheGiConstants SurfaceCacheConstants(const SurfaceCacheGiSettings& s,const MaterialPipelineSettings& settings,const PipelineCamera& camera) const {
    SurfaceCacheGiConstants c;const auto& cache=*s.cache;const auto& geometry=*s.scene->PackedGeometry();
    c.inverseView=glm::inverse(camera.view);
    c.atlas={int(cache.Width()),int(cache.PlaneTexels()),int(cache.TexelCount()),int(cache.CardCount())};
    c.geometry={int(geometry.NodeCount()*2),int(geometry.Width()),int(geometry.NodeCount()),int(geometry.TriangleCount())};
    c.screen={int(width),int(height),int(s.raysPerPixel),int(s.view)};
    const auto debug=(s.view==2&&s.useScreenProbes)?s.reconstructionDebug:0u;
    c.parameters={s.maxDistance,s.rayBias,s.intensity,float((s.cacheBilinear?3u:1u)|(s.reuseAnyHitBounds?4u:0u)|(debug<<3)|
        (s.directionalReconstruction?64u:0u)|(settings.screenProbePlacement.curvatureAwareWeights?128u:0u)|(cache.Settings().localCaptureAttributes?256u:0u)|(cache.Settings().captureShadingNormals?512u:0u))};
    c.sunDirectionIntensity=glm::vec4(glm::normalize(settings.shadows.sunDirection),settings.shadows.sunIntensity);
    c.sunColor=glm::vec4(settings.shadows.sunColor,glm::uintBitsToFloat(settings.screenProbePlacement.trace.samplingSeed));c.spot=MakeSpotLightConstants(settings.spotLight);return c;
}
void BindSurfaceCache(RHIFrameEncoder& encoder) {
    Require(encoder.BindTexture(surfaceCacheAttributes,12)&&encoder.BindTexture(surfaceCacheMetadata,13)&&
        encoder.BindTexture(surfaceCacheLighting.color,14),"Cannot bind card Surface Cache");
}
void RecordSurfaceCacheGi(RHIFrameEncoder& encoder,const RenderFrame& frame,const PipelineCamera& camera,
    const MaterialPipelineSettings& settings,const SceneEffectsConstants& effects,const SurfacePassConstants& surface,const PostProcessConstants& post,bool cameraCut) {
    const auto& s=settings.surfaceCacheGi;
    if(!s.ueLumenHigh)ResetUeLumenDirectLightingSchedule();
    if(!s.enabled)return;
    const bool nativeVisibility=s.hardwareCacheVisibility&&surfaceCacheHardwareSupported&&bool(surfaceCacheNativeLightingProgramsOwner);
    statistics.surfaceCacheHardwareVisibility=nativeVisibility;
    // Classify once before card scheduling; all later temporal consumers reuse
    // this immutable snapshot and the original history-key byte stream.
    const bool temporal=SurfaceCacheTemporalActive(s);
    const auto historyKey=temporal?SurfaceCacheHistoryKey(frame,settings):0;
    const bool historyAllowed=temporal&&surfaceCacheTemporalCompleted.valid&&!s.temporal.reset&&!cameraCut;
    const auto temporalMotion=temporal?ClassifySurfaceCacheTemporalMotion(frame,settings,historyKey,historyAllowed):SurfaceCacheTemporalMotionState{};
    const auto c=SurfaceCacheConstants(s,settings,camera);Upload(encoder,surfaceCacheBuffer,&c,sizeof(c),SurfaceCacheGiBinding);
    std::uint64_t key=14695981039346656037ull;
    const auto add=[&](const auto& value){const auto* p=reinterpret_cast<const unsigned char*>(&value);for(std::size_t i=0;i<sizeof(value);++i){key^=p[i];key*=1099511628211ull;}};
    if(s.ueLumenHigh) {
        // Rigid transform epochs affect priorities, not the global lighting
        // identity. Layout, mesh topology and material edits still relight all.
        const auto& cache=*s.cache;
        add(cache.CardCount());add(cache.Settings().pageResolution);
        add(cache.TriangleCount());add(s.scene->MeshGeneration());
        const auto& metadata=cache.Metadata();
        Require(metadata.size()>=std::size_t(cache.CardCount())*SurfaceCacheCardTexels,"Incomplete Surface Cache material metadata");
        for(unsigned card=0;card<cache.CardCount();++card) {
            add(glm::vec3(metadata[std::size_t(card)*SurfaceCacheCardTexels+7]));
            add(glm::vec3(metadata[std::size_t(card)*SurfaceCacheCardTexels+8]));
        }
    } else {add(surfaceCacheGeneration);add(surfaceCacheGeometryGeneration);}
    add(settings.lighting.lightPositions);add(settings.lighting.lightColors);
    add(reinterpret_cast<std::uintptr_t>(s.cache.get()));add(reinterpret_cast<std::uintptr_t>(s.scene.get()));
    add(c.sunDirectionIntensity);add(glm::vec3(c.sunColor));add(c.spot);add(s.maxDistance);add(s.rayBias);add(s.reuseAnyHitBounds);
    add(nativeVisibility);
    const auto areas=MakeAreaLightsConstants(settings.areaLights);add(areas);
    auto cachedAreaSettings=settings.areaLights;
    if(s.cachedAreaSamplesPerAxis){cachedAreaSettings.samplesPerAxis=s.cachedAreaSamplesPerAxis;cachedAreaSettings.stratifiedSamples=false;}
    const auto cachedAreas=MakeAreaLightsConstants(cachedAreaSettings);
    if(s.cachedAreaSamplesPerAxis)add(cachedAreas);
    const bool relight=!surfaceCacheCompleted.valid||surfaceCacheCompleted.lightingKey!=key;
    const PipelineDetail::UeLumenDirectLightingPlan* lightingPlan=nullptr;
    if(s.ueLumenHigh) {
        const auto& current=temporalMotion.snapshot;
        const auto& previous=surfaceCacheTemporalCompleted.motionSnapshot;
        const bool certified=current&&current->certified;
        const bool generationChanged=!previous||!previous->certified||
            previous->cacheGeneration!=s.cache->Generation()||
            previous->geometryGeneration!=s.scene->GeometryGeneration();
        // Only the existing two-snapshot rigid-motion certificate may defer
        // a geometry change. Unknown edits and unclassified epochs remain full.
        const bool forceFull=relight||!certified||(generationChanged&&!temporalMotion.compatible);
        lightingPlan=&PlanUeLumenDirectLightingSchedule(*s.cache,camera,key,forceFull);
    }
    if(!s.nativeScreenProbeTransport&&s.cacheSettledGather&&SurfaceCacheTemporalActive(s)&&s.view==0&&!s.probeDiagnostics&&!s.probeOriginDiagnostics&&
        !s.contact.collectSamplingDiagnostics&&!settings.screenProbePlacement.trace.sdf.collectRayDiagnostics) {
        auto cacheKey=SurfaceCacheHistoryKey(frame,settings);
        const auto mix=[&](const auto& value){const auto* bytes=reinterpret_cast<const unsigned char*>(&value);
            for(std::size_t i=0;i<sizeof(value);++i){cacheKey^=bytes[i];cacheKey*=1099511628211ull;}};
        mix(key);mix(camera.view);mix(camera.projection);mix(width);mix(height);
        const bool compatible=!cameraCut&&!s.temporal.reset&&surfaceCacheGatherCacheCompleted.valid&&
            surfaceCacheGatherCacheCompleted.key==cacheKey&&surfaceCacheTemporalCompleted.valid&&!relight;
        const unsigned stable=compatible?std::min(64u,surfaceCacheGatherCacheCompleted.stableFrames+1):0;
        surfaceCacheGatherCachePending={cacheKey,stable,true};
        if(compatible&&stable>=64) {
            surfaceCachePending=surfaceCacheCompleted;surfaceCacheTemporalPending=surfaceCacheTemporalCompleted;
            placementPending=placementCompleted;tracePending=traceCompleted;
            BindTarget(encoder,surfaceCacheResolved);
            Require(encoder.BindPipeline(surfaceCacheResolveProgram)&&encoder.BindMesh(fullscreen)&&
                encoder.BindTexture(scene.color,0)&&encoder.BindTexture(surfaceCacheTemporalGi.color,1)&&
                encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&
                encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot resolve cached settled GI");
            Fullscreen(encoder,scene,copy,surfaceCacheResolved.color,{},{},post);
            statistics.surfaceCacheGatherCacheReused=true;statistics.surfaceCacheHistoryUsed=true;
            statistics.surfaceCachePasses=2;
            return;
        }
    }
    // The lighting target must not still be bound for sampling while it is
    // attached for writing. Unbound optimized declarations remain harmless.
    if(lightingPlan?(lightingPlan->fullRefresh||!lightingPlan->rects.empty()):relight) {
        if(s.cachedAreaSamplesPerAxis)Upload(encoder,areaBuffer,&cachedAreas,sizeof(cachedAreas),AreaLightsBinding);
        Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheLighting),"Cannot begin Surface Cache lighting GPU timing");
        Require(encoder.BindTexture({},14),"Cannot unbind Surface Cache lighting");
        const bool actualLightingDiagnostics=s.ueLumenHigh&&settings.screenProbePlacement.trace.sdf.collectRayDiagnostics;
        if(actualLightingDiagnostics){
            Require(lightingActualNativeVisibility==nativeVisibility&&lightingActualTarget.color==surfaceCacheLighting.color&&
                lightingActualQueryTexture==lightingQueryTarget.color&&encoder.KeepAlive(lightingActualProgramsOwner)&&
                encoder.KeepAlive(lightingActualOwner)&&encoder.KeepAlive(lightingQueryOwner),"Cannot retain actual lighting query counters");
        }
        const bool partialLighting=lightingPlan&&!lightingPlan->fullRefresh;
        // Clear only the query attachment: unscheduled lighting must survive,
        // while diagnostic w records exactly this frame's actual rect writes.
        if(actualLightingDiagnostics&&partialLighting)
            BindTarget(encoder,lightingQueryTarget,RHICommand::ClearColor);
        BindTarget(encoder,actualLightingDiagnostics?lightingActualTarget:surfaceCacheLighting,RHICommand::ClearNone);
        if(partialLighting) {
            Require(encoder.BindTexture(surfaceCacheAttributes,12)&&encoder.BindTexture(surfaceCacheMetadata,13)&&encoder.BindTexture(surfaceCacheGeometry,9),
                "Cannot bind scheduled Surface Cache cards");
            RecordUeLumenDirectLightingSchedule(encoder,nativeVisibility,actualLightingDiagnostics);
        } else {
            const unsigned ueVisibilityIndex=nativeVisibility?1u:0u;
            if(s.ueLumenHigh&&!actualLightingDiagnostics)
                Require(encoder.KeepAlive(ueLumenDirectLightingSchedule.fullProgramOwners[ueVisibilityIndex]),"Cannot retain full UE card lighting");
            const auto fullLightingProgram=actualLightingDiagnostics?lightingActualProgram:
                s.ueLumenHigh?ueLumenDirectLightingSchedule.fullPrograms[ueVisibilityIndex]:
                nativeVisibility?surfaceCacheNativeLightingProgram:surfaceCacheLightingProgram;
            Require(encoder.BindPipeline(fullLightingProgram)&&encoder.BindMesh(fullscreen)&&
                encoder.BindTexture(surfaceCacheAttributes,12)&&encoder.BindTexture(surfaceCacheMetadata,13)&&encoder.BindTexture(surfaceCacheGeometry,9)&&
                encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot light Surface Cache cards");
        }
        if(actualLightingDiagnostics)statistics.surfaceCacheLightingQueriesCollected=true;
        // Legacy diagnostics are a separate replay, never actual UE counters.
        if(!s.ueLumenHigh&&s.contact.collectSamplingDiagnostics&&nativeVisibility){
            Require(encoder.KeepAlive(lightingQueryOwner)&&encoder.KeepAlive(lightingQueryProgramsOwner),"Cannot retain lighting query counters");
            BindTarget(encoder,lightingQueryTarget);
            Require(encoder.BindPipeline(lightingQueryProgram)&&encoder.BindMesh(fullscreen)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot inspect cached-lighting visibility queries");
            statistics.surfaceCacheLightingQueriesCollected=true;
        }
        Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheLighting),"Cannot end Surface Cache lighting GPU timing");
        ++statistics.surfaceCachePasses;
        statistics.surfaceCacheUpdatedTexels=lightingPlan?lightingPlan->scheduledTexels:s.cache->TexelCount();
        statistics.surfaceCacheLightingWrittenTexels=lightingPlan?lightingPlan->writtenTexels:s.cache->PlaneTexels();
        statistics.surfaceCacheLightingUpdated=true;
        if(s.cachedAreaSamplesPerAxis)Upload(encoder,areaBuffer,&areas,sizeof(areas),AreaLightsBinding);
    }
    Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheCapture),"Cannot time GI receiver captures");
    auto captureEffects=effects;captureEffects.effects.y=0;captureEffects.screenAndAo.z=0;
    if(glm::dot(glm::vec3(s.diagnosticReceiverClipPlane),glm::vec3(s.diagnosticReceiverClipPlane))>0) {
        captureEffects.effects.z=1;captureEffects.clipPlane=s.diagnosticReceiverClipPlane;
    }
    Effects(encoder,captureEffects);
    auto captureSurface=surface;captureSurface.options=glm::vec4(0,0,2,0);captureSurface.projection=camera.projection;
    if(s.splitReceiverGather)captureSurface.options.w=-1;
    Upload(encoder,surfaceBuffer,&captureSurface,sizeof(captureSurface),SurfacePassBinding);
    BindTarget(encoder,diffuseCapture,RHICommand::ClearColor|RHICommand::ClearDepth);
    Depth(encoder,frame,camera.projection*camera.view,false);
    if(s.mrtReceiverCapture&&s.rasterSourcePositions&&CanCaptureSurfaceMrt(frame)) {
        // Clear every color attachment after the unchanged depth prepass.
        // All three existing images retain their owners and readback handles.
        BindTarget(encoder,receiverCaptureMrt,RHICommand::ClearColor);
        CaptureSurfaceMrt(encoder,frame,camera,captureSurface);
        statistics.surfaceCacheMrtReceiverCapture=true;
    } else {
        // Keep the original three-pass capture as the whole-frame fallback.
        CaptureSurface(encoder,frame,camera,false);
        BindTarget(encoder,normalCapture,RHICommand::ClearColor);CaptureSurface(encoder,frame,camera,true);
        if(s.rasterSourcePositions)CaptureSurfacePositions(encoder,frame,camera,captureSurface);
        else {
            const auto geometry=MakeIndirectLightingConstants(IndirectLightingSettings{},camera.projection,camera.view);
            Upload(encoder,indirectBuffer,&geometry,sizeof(geometry),IndirectLightingBinding);
            Fullscreen(encoder,indirectPositions,indirectGeometry,{},{},diffuseCapture.depth,post);
        }
    }
    // UE cached direct writes the base from the unchanged opaque scene later.
    if(!s.ueLumenHigh||!s.cachedDirectDiffuse)
        Fullscreen(encoder,surfaceCacheSource,copy,scene.color,{},{},post);
    Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheCapture),"Cannot end GI receiver capture timing");
    // Leave the lighting attachment before sampling it. Also restore cache
    // bindings after material captures have installed their texture slots.
    BindSurfaceCache(encoder);
    const bool gather=s.view==0||s.view==2||(s.view==5&&SurfaceCacheTemporalActive(s));
    const bool probes=s.useScreenProbes&&!s.splitReceiverGather&&(gather||s.view==6||s.probeDiagnostics||s.probeOriginDiagnostics);
    const bool omitRaw=s.ueLumenHigh||(s.omitUnusedRawGather&&SurfaceCacheSpatialActive(s)&&s.view==0&&!s.collectProbeCoverage&&
        !s.probeDiagnostics&&!s.probeOriginDiagnostics&&!s.contact.collectSamplingDiagnostics&&
        !settings.screenProbePlacement.trace.sdf.collectRayDiagnostics);
    bool historyValid=temporal&&temporalMotion.compatible;
    if(s.ueLumenHigh){
        if(!temporalMotion.snapshot||!temporalMotion.snapshot->certified)
            nativeProbeTransport.ueRadianceCache.completed={};
        const bool motionValid=SetUeLumenHighProbeMotion(temporalMotion.snapshot,
            surfaceCacheTemporalCompleted.motionSnapshot,historyValid);
        historyValid=historyValid&&motionValid;
    }
    const auto temporalSequence=historyValid?surfaceCacheTemporalCompleted.sequence:0;
    // Exact camera equality deliberately excludes even slow camera movement.
    // Certified object motion and its settling window keep responsive history.
    const bool stationary=historyValid&&s.temporal.staticConvergence&&s.temporal.maxFrames>1&&!temporalMotion.dynamicLighting&&
        surfaceCacheTemporalCompleted.camera.view==camera.view&&
        surfaceCacheTemporalCompleted.camera.projection==camera.projection;
    surfaceCachePending={true,key,probes};
    surfaceCachePending.rawCollected=!omitRaw;statistics.surfaceCacheRawGatherOmitted=omitRaw;
    surfaceCacheSafeOriginsActive=s.safeProbeOrigins&&(probes||s.nativeScreenProbeTransport);
    bool nativeObjectMotion=false;
    const Target* ueGiSignal=nullptr;
    statistics.surfaceCacheUeLumenHigh=s.ueLumenHigh;
    if(s.nativeScreenProbeTransport&&(s.nativeAdaptiveFreshSampling||s.ueLumenHigh)&&temporal){
        nativeObjectMotion=RecordSurfaceCacheObjectMotion(encoder,frame,camera,captureSurface,
            temporalMotion,historyValid,s.temporal.objectMotionReprojection,s.reuseStaticReceiverMotionCapture&&s.rasterSourcePositions);
        if(nativeObjectMotion)BindSurfaceCache(encoder);
    }
    if(s.nativeScreenProbeTransport){
        const bool angularReuseParity=historyValid&&s.temporal.staticConvergence&&s.temporal.maxFrames>1&&
            (stationary||s.temporal.motionConsistentSpatialReuse)&&s.reconstructionDebug==0;
        const auto& nativeGi=RecordNativeScreenProbeTransport(encoder,frame,camera,settings,effects,surface,post,temporalSequence,angularReuseParity,historyValid,cameraCut);
        if(s.ueLumenHigh)ueGiSignal=&nativeGi;
        const bool nativeExposureMotion=historyValid&&(temporalMotion.dynamicLighting||
            surfaceCacheTemporalCompleted.camera.view!=camera.view||surfaceCacheTemporalCompleted.camera.projection!=camera.projection);
        if(!s.ueLumenHigh){
        RecordNativeContactRiskAndMaskOnly(encoder,s,&camera,historyValid,
            nativeExposureMotion||surfaceCacheTemporalCompleted.freshSamplingSettleRemaining>0,nativeObjectMotion);
        RecordSurfaceTransport(encoder,frame,camera,settings,temporalSequence);
        }
    }
    else if(s.splitReceiverGather)RecordSurfaceTransport(encoder,frame,camera,settings,temporalSequence);
    else if(probes) {
        // Reuse the receiver capture (including rho). Placement must not
        // overwrite it or render diagnostics before the lighting resolve.
        auto placement=settings.screenProbePlacement;
        const SurfaceCacheOriginSafetyConstants safety{glm::vec4(s.originSafetyFraction,s.originSafetyEpsilon,s.rasterSourcePositions?1.f:0.f,0)};
        Upload(encoder,surfaceCacheOriginSafetyBuffer,&safety,sizeof(safety),SurfaceCacheOriginSafetyBinding);
        // Keep the sample sequence continuous through stop/start transitions;
        // only the history limit changes with motion. A fixed eight-phase loop
        // otherwise leaves a non-vanishing probe-specific Monte Carlo imprint.
        if(temporal)placement.trace.samplingSeed=(placement.trace.samplingSeed+
            std::uint32_t(s.temporal.staticConvergence&&s.temporal.maxFrames>1?
                temporalSequence:temporalSequence%s.temporal.directionPhases))&0x1fffffffu;
        RecordScreenProbePlacement(encoder,frame,camera,placement,effects,surface,post,false,false);
        // Local cosine-hemisphere strata have an exact one-to-one mapping only
        // for matching normal frames. Static convergence can afford fresh rays
        // instead of accumulating curved nearest-direction resampling bias.
        if(historyValid&&s.temporal.staticConvergence&&s.temporal.maxFrames>1&&
            (stationary||s.temporal.motionConsistentSpatialReuse)&&s.reconstructionDebug==0)
            placementFrameConstants.refinement.x=float(int(placementFrameConstants.refinement.x+.5f)|16);
        Upload(encoder,surfaceCacheBuffer,&c,sizeof(c),SurfaceCacheGiBinding);
        Upload(encoder,placementBuffer,&placementFrameConstants,sizeof(placementFrameConstants),ScreenProbePlacementBinding);
        BindSurfaceCache(encoder);
        const auto image=placementPending.image,rayImage=tracePending.image;
        Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheProbeReconstruction),"Cannot begin probe reconstruction GPU timing");
        Require(encoder.BindTexture({},10),"Cannot unbind adaptive activity output");
        BindTarget(encoder,surfaceCacheProbeActiveMask);
        Require(encoder.BindPipeline(surfaceCacheProbeActiveMaskProgram)&&encoder.BindMesh(fullscreen)&&
            encoder.BindTexture(placementSpawned[image].color,1)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot summarize active adaptive probes");
        if(!omitRaw) {
        BindTarget(encoder,surfaceCacheProbeIrradiance);
        Require(encoder.BindPipeline(surfaceCacheProbeIntegrateProgram)&&encoder.BindMesh(fullscreen)&&
            encoder.BindTexture(placementUniform[image].color,0)&&encoder.BindTexture(placementSpawned[image].color,1)&&
            encoder.BindTexture(sdfRays[rayImage].color,7)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot integrate probe irradiance");
        }
        const bool directionalData=(s.directionalReconstruction||settings.screenProbePlacement.curvatureAwareWeights)&&gather;
        if(directionalData&&!omitRaw) {
            Require(encoder.BindTexture({},8),"Cannot unbind directional probe output");
            BindTarget(encoder,surfaceCacheProbeDirectional);
            Require(encoder.BindPipeline(surfaceCacheProbeDirectionalProgram)&&encoder.BindMesh(fullscreen)&&
                encoder.BindTexture(placementUniform[image].color,0)&&encoder.BindTexture(placementSpawned[image].color,1)&&
                encoder.BindTexture(sdfRays[rayImage].color,7)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot integrate directional probe irradiance");
        }
        statistics.surfaceCacheDirectionalCollected=s.directionalReconstruction&&gather;
        if(gather&&!omitRaw) {
            BindTarget(encoder,surfaceCacheRaw);
            Require(encoder.BindPipeline(surfaceCacheProbeReconstructProgram)&&encoder.BindMesh(fullscreen)&&
                encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&encoder.BindTexture(diffuseCapture.color,4)&&
                encoder.BindTexture(placementUniform[image].color,5)&&encoder.BindTexture(placementSpawned[image].color,6)&&
                encoder.BindTexture(surfaceCacheProbeIrradiance.color,7)&&encoder.BindTexture(surfaceCacheProbeDirectional.color,8)&&encoder.BindTexture(surfaceCacheProbeActiveMask.color,10)&&
                encoder.BindTexture(surfaceCacheSafeOriginsActive?surfaceCacheProbeOrigins.color:RenderResourceHandle<RHITextureSpec>{},11)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot reconstruct probe GI");
        } else if(!omitRaw)BindTarget(encoder,surfaceCacheRaw,RHICommand::ClearColor);
        const bool collectCoverage=s.collectProbeCoverage||s.view==6;
        if(collectCoverage) {
            BindTarget(encoder,surfaceCacheProbeCoverage);
            Require(encoder.BindPipeline(surfaceCacheProbeCoverageProgram)&&encoder.BindMesh(fullscreen)&&
                encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&encoder.BindTexture(diffuseCapture.color,4)&&
                encoder.BindTexture(placementUniform[image].color,5)&&encoder.BindTexture(placementSpawned[image].color,6)&&
                encoder.BindTexture(surfaceCacheProbeIrradiance.color,7)&&encoder.BindTexture(surfaceCacheProbeActiveMask.color,10)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot diagnose geometric probe coverage");
        }
        Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::SurfaceCacheProbeReconstruction),"Cannot end probe reconstruction GPU timing");
        statistics.surfaceCacheProbeCoverageCollected=collectCoverage;
        statistics.surfaceCacheProbePasses=1+(omitRaw?0:unsigned(gather)+1+unsigned(directionalData))+unsigned(collectCoverage);
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
    if(s.ueLumenHigh)RecordUeLumenHighCachedDirect(encoder,settings,camera);
    const bool spatial=SurfaceCacheSpatialActive(s);
    if(spatial)RecordSurfaceCacheSpatial(encoder,s);
    const Target* giSignalTarget=s.ueLumenHigh?ueGiSignal:spatial?&surfaceCacheSpatialGi:&surfaceCacheRaw;
    Require(giSignalTarget,"Missing current GI signal");
    // Receiver motion is captured once, before exposure classification. Both
    // classifiers and temporal reprojection use this same certified mapping.
    const bool objectMotion=s.nativeScreenProbeTransport&&(s.nativeAdaptiveFreshSampling||s.ueLumenHigh)?nativeObjectMotion:
        temporal&&RecordSurfaceCacheObjectMotion(encoder,frame,camera,captureSurface,
            temporalMotion,historyValid,s.temporal.objectMotionReprojection,s.reuseStaticReceiverMotionCapture&&s.rasterSourcePositions);
    if(objectMotion)BindSurfaceCache(encoder); // Material captures may have installed their own texture slots.
    const bool exposureMotion=historyValid&&(temporalMotion.dynamicLighting||
        surfaceCacheTemporalCompleted.camera.view!=camera.view||
        surfaceCacheTemporalCompleted.camera.projection!=camera.projection);
    if(SurfaceCacheContactActive(s)) {
        if(s.contact.disocclusionSampling)
            RecordSurfaceCacheFreshSampling(encoder,s,camera,historyValid,
                exposureMotion||surfaceCacheTemporalCompleted.freshSamplingSettleRemaining>0,objectMotion);
        RecordSurfaceCacheContact(encoder,s,*giSignalTarget,settings.screenProbePlacement.trace.samplingSeed,temporalSequence,temporal);giSignalTarget=&surfaceCacheContactResult;
    }
    const auto& giSignal=*giSignalTarget;
    if(temporal) {
        if(s.ueLumenHigh)RecordUeLumenHighTemporal(encoder,s,camera,historyKey,temporalSequence,historyValid,
            giSignal,ueLumenHighGather.lightIsMoving,temporalMotion,objectMotion);
        else RecordSurfaceCacheTemporal(encoder,s,camera,historyKey,temporalSequence,historyValid,giSignal,temporalMotion,stationary,objectMotion);
        surfaceCacheTemporalPending.motionSnapshot=temporalMotion.snapshot;
        surfaceCacheTemporalPending.stableKey=temporalMotion.stableKey;
        surfaceCacheTemporalPending.dynamicSettleRemaining=temporalMotion.settleRemaining;
        surfaceCacheTemporalPending.shadowBoundsMin=temporalMotion.shadowBoundsMin;
        surfaceCacheTemporalPending.shadowBoundsMax=temporalMotion.shadowBoundsMax;
        surfaceCacheTemporalPending.shadowBoundsHistoryMin=temporalMotion.shadowBoundsHistoryMin;
        surfaceCacheTemporalPending.shadowBoundsHistoryMax=temporalMotion.shadowBoundsHistoryMax;
        surfaceCacheTemporalPending.shadowBoundsHistoryCursor=temporalMotion.shadowBoundsHistoryCursor;
        surfaceCacheTemporalPending.freshSamplingSettleRemaining=s.contact.disocclusionSampling&&historyValid?
            (exposureMotion?3u:surfaceCacheTemporalCompleted.freshSamplingSettleRemaining?
                surfaceCacheTemporalCompleted.freshSamplingSettleRemaining-1:0):0;
    }
    if(s.ueLumenHigh)RecordUeLumenHitSceneColorHistory(encoder,s,camera,surfaceCacheSource,surfaceCacheTemporalGi);
    const auto resolve=[&](const Target& target,RenderResourceHandle<PipelineSpec> program,bool rawCoverage=false) {
        BindTarget(encoder,target);Require(encoder.BindPipeline(program)&&encoder.BindMesh(fullscreen)&&
            encoder.BindTexture(surfaceCacheSource.color,0)&&encoder.BindTexture(rawCoverage?surfaceCacheRaw.color:(temporal?surfaceCacheTemporalGi.color:giSignal.color),1)&&
            encoder.BindTexture(indirectPositions.color,2)&&encoder.BindTexture(normalCapture.color,3)&&
            encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot resolve card Surface Cache GI");
    };
    if(!omitRaw)resolve(surfaceCacheCoverage,surfaceCacheCoverageProgram,true);
    if(s.view==5) {
        BindTarget(encoder,surfaceCacheResolved,temporal?RHICommand::ClearNone:RHICommand::ClearColor);
        if(temporal)Require(encoder.BindPipeline(s.ueLumenHigh?ueHighTemporal.countProgram:surfaceCacheTemporalCountProgram)&&encoder.BindMesh(fullscreen)&&
            encoder.BindTexture(surfaceCacheTemporalHistory[surfaceCacheTemporalPending.image].color,0)&&
            encoder.BindTexture(s.ueLumenHigh?ueHighTemporal.aux[surfaceCacheTemporalPending.image].color:surfaceCacheRaw.color,1)&&
            encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot display GI history length");
    } else if(s.view==6) {
        if(probes)Fullscreen(encoder,surfaceCacheResolved,copy,surfaceCacheProbeCoverage.color,{},{},post);
        else BindTarget(encoder,surfaceCacheResolved,RHICommand::ClearColor);
    }
    else if(s.ueLumenHigh&&(s.view==0||s.view==2)) {
        // Deferred composite can write scene directly: its base and GI inputs
        // have independent owners. Keep the original diagnostic resolve path.
        Require(encoder.BindTexture({},5),"Cannot release opaque cached-direct input");
        resolve(scene,surfaceCacheResolveProgram);
    } else resolve(surfaceCacheResolved,surfaceCacheResolveProgram);
    if(!(s.ueLumenHigh&&(s.view==0||s.view==2)))
        Fullscreen(encoder,scene,copy,surfaceCacheResolved.color,{},{},post);
    if(probes&&s.probeDiagnostics)RecordScreenProbeDebug(encoder,settings.screenProbePlacement,placementFrameConstants,placementPending.image,tracePending.image,post);
    if(probes&&s.probeOriginDiagnostics&&statistics.surfaceCacheProbeOriginsCollected) {
        Upload(encoder,placementBuffer,&placementFrameConstants,sizeof(placementFrameConstants),ScreenProbePlacementBinding);
        BindTarget(encoder,scene);
        Require(encoder.BindPipeline(surfaceCacheProbeOriginMarkerProgram)&&encoder.BindMesh(fullscreen)&&
            encoder.BindTexture(surfaceCacheProbeOrigins.color,0)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3,0,0,unsigned(placementFrameConstants.traceAtlas.w),0}),"Cannot show actual probe ray origins");
        ++statistics.surfaceCachePasses;
    }
    Upload(encoder,surfaceBuffer,&surface,sizeof(surface),SurfacePassBinding);Effects(encoder,effects);
    statistics.surfaceCachePasses+=(gather?9:8)+(probes?statistics.surfaceCacheProbePasses-unsigned(gather):0)+unsigned(statistics.surfaceCacheProbeOriginsCollected);
    if(omitRaw)statistics.surfaceCachePasses-=2;
    if(s.ueLumenHigh)statistics.surfaceCachePasses-=unsigned(s.cachedDirectDiffuse)+unsigned(s.view==0||s.view==2);
}
