// Included inside MaterialRenderPipeline::Impl. All work uses the existing
// encoder and resource owners; no frame-time CPU ray tracing/readback.
void PrepareLumen(const LumenGiSettings& s) {
    lumenGeometryChanged=lumenCompleted.scene!=s.scene;
    lumenReactiveFrames=lumenCompleted.reactiveFrames;
    if(lumenGeometryChanged&&lumenCompleted.valid)lumenReactiveFrames=4;
    if(!s.enabled)return;
    if(!lumenProgramsOwner) {
        PipelineDetail::ResourceBuilder b(device);
        lumenDirect=b.FullscreenPipeline(FullscreenVertexShader(),PipelineDetail::LumenSurfaceDirectShader());
        lumenBounce=b.FullscreenPipeline(FullscreenVertexShader(),PipelineDetail::LumenSurfaceBounceShader());
        lumenIncidentFilter=b.FullscreenPipeline(FullscreenVertexShader(),PipelineDetail::LumenIncidentFilterShader());
        lumenCompose=b.FullscreenPipeline(FullscreenVertexShader(),PipelineDetail::LumenSurfaceComposeShader());
        lumenWorldProbes=b.FullscreenPipeline(FullscreenVertexShader(),PipelineDetail::LumenRadianceProbeShader());
        lumenGuide=b.FullscreenPipeline(FullscreenVertexShader(),PipelineDetail::LumenGuideShader());
        lumenGather=b.FullscreenPipeline(FullscreenVertexShader(),PipelineDetail::LumenGatherShader());
        lumenTemporal=b.FullscreenPipeline(FullscreenVertexShader(),PipelineDetail::LumenTemporalShader());
        lumenFilter=b.FullscreenPipeline(FullscreenVertexShader(),PipelineDetail::LumenFilterShader());
        lumenResolve=b.FullscreenPipeline(FullscreenVertexShader(),PipelineDetail::LumenResolveShader());
        lumenResolveDirect=b.FullscreenPipeline(FullscreenVertexShader(),PipelineDetail::LumenResolveShader(true));
        lumenReflect=b.FullscreenPipeline(FullscreenVertexShader(),PipelineDetail::LumenReflectionShader());
        lumenReflectResolve=b.FullscreenPipeline(FullscreenVertexShader(),PipelineDetail::LumenReflectionResolveShader());
        lumenReflectTemporal=b.FullscreenPipeline(FullscreenVertexShader(),PipelineDetail::LumenReflectionTemporalShader());
        lumenReflectFilter=b.FullscreenPipeline(FullscreenVertexShader(),PipelineDetail::LumenReflectionFilterShader());
        lumenBuffer=b.Uniform(sizeof(LumenGiConstants));lumenProgramsOwner=b.Finish();
    }
    const auto target=[](PipelineDetail::ResourceBuilder& b,unsigned w,unsigned h,RHITextureFormat format=RHITextureFormat::RGBA32F) {
        Target t;t.width=w;t.height=h;t.color=b.Texture(w,h,format);t.framebuffer=b.Target(t.color,{});return t;
    };
    if(lumenScene!=s.scene||!lumenSceneOwner) {
        PipelineDetail::ResourceBuilder b(device);CreateRHITextureSpec desc;
        desc.width=LumenSurfaceWidth;desc.height=s.scene->AttributeHeight();desc.data=s.scene->Attributes().data();
        desc.textureDataStoreType=desc.textureUseType=RHITextureFormat::RGBA32F;desc.mipmaps=false;
        desc.filterMode=RHIFilterMode::Nearest;desc.addressMode=RHIAddressMode::ClampToEdge;
        auto attributes=b.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(desc,cb);});
        const bool compatible=lumenScene&&lumenCacheOwner&&lumenScene->Geometry()->TopologyKey()==s.scene->Geometry()->TopologyKey();
        if(!lumenDistanceOwner||!lumenScene||lumenScene->DistanceField()!=s.scene->DistanceField()) {
            PipelineDetail::ResourceBuilder fieldBuilder(device);
            const auto& field=*s.scene->DistanceField();desc.width=field.Width();desc.height=field.Height();desc.data=field.Pixels().data();
            lumenDistanceField=fieldBuilder.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(desc,cb);});lumenDistanceOwner=fieldBuilder.Finish();
        }
        if(!compatible) {
            PipelineDetail::ResourceBuilder cacheBuilder(device);
            for(auto& t:lumenDirectCache)t=target(cacheBuilder,LumenSurfaceWidth,s.scene->CacheHeight());
            for(auto& t:lumenCache)t=target(cacheBuilder,LumenSurfaceWidth,s.scene->CacheHeight());
            for(auto& t:lumenIncidentCache)t=target(cacheBuilder,LumenSurfaceWidth,s.scene->CacheHeight());
            for(auto& t:lumenIncidentRaw)t=target(cacheBuilder,LumenSurfaceWidth,s.scene->CacheHeight());
            lumenCacheOwner=cacheBuilder.Finish();lumenCompleted.valid=false;lumenCompleted.viewValid=false;
        } else lumenReactiveFrames=4;
        lumenAttributes=attributes;lumenSceneOwner=b.Finish();lumenScene=s.scene;
    }
    unsigned w=std::max(1u,(width+s.probeSpacing-1)/s.probeSpacing),h=std::max(1u,(height+s.probeSpacing-1)/s.probeSpacing);
    const auto longest=std::max(w,h);if(longest>160){w=std::max(1u,w*160/longest);h=std::max(1u,h*160/longest);}
    const auto sw=std::max(1u,unsigned(std::ceil(width*s.reflectionResolutionScale))),sh=std::max(1u,unsigned(std::ceil(height*s.reflectionResolutionScale)));
    const auto sourceWidth=std::max(1u,unsigned(std::ceil(width*s.reflectionSourceResolutionScale)));
    const auto sourceHeight=std::max(1u,unsigned(std::ceil(height*s.reflectionSourceResolutionScale)));
    unsigned rw=std::max(1u,(width+3)/4),rh=std::max(1u,(height+3)/4),longestR=std::max(rw,rh);
    if(longestR>s.reflectionTraceMaxDimension){rw=std::max(1u,rw*s.reflectionTraceMaxDimension/longestR);rh=std::max(1u,rh*s.reflectionTraceMaxDimension/longestR);}
    if(!lumenViewOwner||lumenRaw.width!=w||lumenRaw.height!=h||lumenReflectionFull.width!=sw||lumenReflectionFull.height!=sh||lumenReflectionRaw.width!=rw||lumenReflectionRaw.height!=rh||lumenSpecularSource.width!=sourceWidth||lumenSpecularSource.height!=sourceHeight) {
        PipelineDetail::ResourceBuilder b(device);
        auto raw=target(b,w,h);std::array<Target,2> histories,guides,work;
        for(auto& t:histories)t=target(b,w,h);for(auto& t:guides)t=target(b,w,h);for(auto& t:work)t=target(b,w,h);
        auto reflectionRaw=target(b,rw,rh),reflectionGuide=target(b,rw,rh),reflectionFull=target(b,sw,sh,RHITextureFormat::RGBA16F);
        auto grazingFull=target(b,sw,sh,RHITextureFormat::RGBA16F);
        std::array<Target,2> reflectionGuides,reflectionHistory,grazingHistory,reflectionWork;
        for(auto& t:reflectionGuides)t=target(b,rw,rh);for(auto& t:reflectionHistory)t=target(b,rw,rh);
        for(auto& t:grazingHistory)t=target(b,rw,rh);for(auto& t:reflectionWork)t=target(b,rw,rh);
        // Own the depth attachment: main targets can be recreated at the same
        // viewport extent when switching MSAA without rebuilding this view.
        // Screen-ray intersection retains full internal depth and normals;
        // its HDR color lookup uses normalized UV and can have its own extent.
        auto specularSource=MakeTarget(b,sourceWidth,sourceHeight,true,true);
        auto owner=b.Finish();lumenRaw=raw;lumenGatherHistory=histories;lumenGuides=guides;lumenFilterWork=work;
        lumenReflectionRaw=reflectionRaw;lumenReflectionGuide=reflectionGuide;lumenReflectionFull=reflectionFull;
        lumenReflectionFullGrazing=grazingFull;lumenReflectionGuides=reflectionGuides;lumenReflectionHistory=reflectionHistory;
        lumenGrazingHistory=grazingHistory;lumenReflectionWork=reflectionWork;
        lumenSpecularSource=specularSource;
        lumenViewOwner=std::move(owner);lumenCompleted.viewValid=false;
    }
}
LumenGiConstants LumenConstants(const LumenGiSettings& s) const {
    LumenGiConstants c;c.trace={s.maxDistance,s.rayBias,s.bounceFeedback,s.surfaceHistory};
    c.settings={s.intensity,s.thickness,0,s.screenTraces?1:0};
    c.atlas={int(LumenSurfaceWidth),int(s.scene->SurfelCount()),int(s.scene->EmissiveTriangleCount()),int(s.scene->Geometry()->TriangleCount())};
    c.budget={0,0,int(s.surfaceRays),int(s.gatherRays)};
    c.gather={int(lumenRaw.width),int(lumenRaw.height),int(s.screenSteps),s.traceDirectLighting?1:0};
    const auto& field=*s.scene->DistanceField();c.fieldOrigin=glm::vec4(field.Origin(),0);c.fieldSpacing=glm::vec4(field.Spacing(),s.maxReflectionRoughness);
    c.fieldCounts=glm::ivec4(s.distanceFields&&s.scene->DistanceFieldCurrent()?glm::ivec3(field.Counts()):glm::ivec3(0),int(field.Width()));c.reflection.x=float(s.reflectionRays);return c;
}
void RecordLumenSurface(RHIFrameEncoder& encoder,MaterialPipelineSettings& settings) {
    const auto& s=settings.lumenGi;std::uint64_t key=14695981039346656037ull;
    const auto add=[&](const auto& v){const auto* p=reinterpret_cast<const unsigned char*>(&v);for(std::size_t i=0;i<sizeof(v);++i){key^=p[i];key*=1099511628211ull;}};
    add(s.scene->Geometry()->TopologyKey());add(s.surfaceRays);add(s.maxDistance);add(s.rayBias);add(s.traceDirectLighting);
    bool source=s.scene->Geometry()->HasEmission()||(settings.sky.enabled&&settings.sky.intensity>0&&settings.sky.diffuseStrength>0)||
        (settings.shadows.sunIntensity>0&&glm::any(glm::greaterThan(settings.shadows.sunColor,glm::vec3(0))));
    source|=settings.spotLight.enabled&&glm::any(glm::greaterThan(settings.spotLight.intensity,glm::vec3(0)));
    for(const auto& color:settings.lighting.lightColors)source|=glm::any(glm::greaterThan(glm::vec3(color),glm::vec3(0)));
    for(unsigned i=0;i<settings.areaLights.count;++i)source|=settings.areaLights.lights[i].enabled&&glm::any(glm::greaterThan(settings.areaLights.lights[i].radiance,glm::vec3(0)));
    const bool reset=s.reset||!source||!lumenCompleted.valid||key!=lumenCompleted.key;
    const auto sequence=reset?0:lumenCompleted.sequence;const auto total=s.scene->SurfelCount();
    const unsigned previous=reset?0:lumenCompleted.image,write=1-previous;
    const unsigned budget=lumenReactiveFrames?std::max(s.surfaceUpdatesPerFrame,(total+3)/4):s.surfaceUpdatesPerFrame;
    const unsigned first=unsigned(sequence*budget%std::max(1u,total));
    auto c=LumenConstants(s);c.settings.z=reset?1:0;c.budget.x=int(first);c.budget.y=int(reset?total:std::min(budget,total));
    if(lumenReactiveFrames){c.trace.w=0;--lumenReactiveFrames;}
    std::uint64_t lightingKey=14695981039346656037ull;
    const auto lightBytes=[&](const auto& v){const auto* p=reinterpret_cast<const unsigned char*>(&v);for(std::size_t i=0;i<sizeof(v);++i){lightingKey^=p[i];lightingKey*=1099511628211ull;}};
    lightBytes(settings.lighting.lightColors);
    for(unsigned i=0;i<settings.lighting.lightColors.size();++i)
        if(glm::any(glm::greaterThan(glm::vec3(settings.lighting.lightColors[i]),glm::vec3(0))))lightBytes(settings.lighting.lightPositions[i]);
    lightBytes(settings.shadows.sunDirection);lightBytes(settings.shadows.sunIntensity);lightBytes(settings.shadows.sunColor);
    const auto areas=MakeAreaLightsConstants(settings.areaLights);lightBytes(areas);
    const auto spot=MakeSpotLightConstants(settings.spotLight);
    lightBytes(spot.intensityInner);
    if(glm::any(glm::greaterThan(glm::vec3(spot.intensityInner),glm::vec3(0)))) {
        lightBytes(spot.positionRadius);lightBytes(spot.directionOuter);
    }
    SkyLightConstants sky;if(settings.sky.enabled&&skyEnvironment)sky=MakeSkyLightConstants(settings.sky,*skyEnvironment);lightBytes(sky);
    if(!reset&&lightingKey!=lumenCompleted.lightingKey) {
        lumenReactiveFrames=3;unsigned reactiveBudget=std::max(s.surfaceUpdatesPerFrame,(total+3)/4);
        c.budget.x=int(sequence*reactiveBudget%std::max(1u,total));c.budget.y=int(std::min(total,reactiveBudget));c.trace.w=0;
    }
    c.reflection.y=reset||lumenGeometryChanged||lightingKey!=lumenCompleted.lightingKey?1:0;
    if(reset) {
        for(unsigned slot:{0u,4u,5u,6u,7u,9u})Require(encoder.BindTexture({},slot),"Cannot clear hybrid GI samplers");
        BindTarget(encoder,lumenCache[previous],RHICommand::ClearColor);BindTarget(encoder,lumenDirectCache[previous],RHICommand::ClearColor);
        BindTarget(encoder,lumenIncidentCache[previous],RHICommand::ClearColor);
        BindTarget(encoder,lumenIncidentRaw[previous],RHICommand::ClearColor);
        settings.realtimeGi.reset=true;
    }
    // BVH/light ABI shared with DDGI; the radiance solution and Surface Cache
    // are independent. World probes below only read the solved surface field.
    const auto& rt=settings.realtimeGi;RealtimeGiConstants geometry;
    geometry.origin=glm::vec4(rt.origin,0);geometry.spacing=glm::vec4(rt.spacing,0);geometry.counts=glm::vec4(glm::vec3(rt.counts),rt.counts.x*rt.counts.y*rt.counts.z);
    geometry.trace={rt.maxDistance,rt.rayBias,rt.historyWeight,0};
    geometry.geometry={int(rt.scene->NodeCount()*2),int(rt.scene->Width()),int(rt.scene->NodeCount()),int(rt.scene->TriangleCount())};
    geometry.sunDirectionIntensity=glm::vec4(glm::normalize(settings.shadows.sunDirection),settings.shadows.sunIntensity);geometry.sunColor=glm::vec4(settings.shadows.sunColor,1);
    geometry.spotLight=spot;
    Upload(encoder,realtimeBuffer,&geometry,sizeof(geometry),RealtimeGiBinding);
    Upload(encoder,lumenBuffer,&c,sizeof(c),LumenGiBinding);
    Require(encoder.BindTexture(lumenDistanceField,8),"Cannot bind global distance field");
    BindTarget(encoder,lumenDirectCache[write]);
    Require(encoder.BindPipeline(lumenDirect)&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(realtimeGeometry,0)&&
            encoder.BindTexture(lumenAttributes,4)&&encoder.BindTexture(lumenDirectCache[previous].color,5)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot update Surface Cache direct light");
    BindTarget(encoder,lumenIncidentRaw[write]);
    Require(encoder.BindPipeline(lumenBounce)&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(lumenCache[previous].color,5)&&encoder.BindTexture(lumenDirectCache[write].color,6)&&
            encoder.BindTexture(lumenDirectCache[previous].color,7)&&encoder.BindTexture(lumenIncidentRaw[previous].color,9)&&
            encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot update Surface Cache incident irradiance");
    BindTarget(encoder,lumenIncidentCache[write]);
    Require(encoder.BindPipeline(lumenIncidentFilter)&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(lumenIncidentRaw[write].color,5)&&
            encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot filter Surface Cache incident irradiance");
    BindTarget(encoder,lumenCache[write]);
    Require(encoder.BindPipeline(lumenCompose)&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(lumenIncidentCache[write].color,5)&&
            encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot compose Surface Cache outgoing radiance");
    // Leave the newly completed surface field available to the world probes;
    // the active attachment changes to their ray grid before it is sampled.
    BindTarget(encoder,realtimeRays);Require(encoder.BindTexture(lumenCache[write].color,5),"Cannot bind surface radiance");
    lumenPending={};lumenPending.key=key;lumenPending.lightingKey=lightingKey;lumenPending.sequence=sequence+1;lumenPending.image=write;lumenPending.valid=true;
    lumenPending.scene=s.scene;lumenPending.reactiveFrames=lumenReactiveFrames;
    statistics.lumenGiPasses=4;statistics.lumenSurfaceTexels=total;statistics.lumenUpdatedSurfels=unsigned(c.budget.y);
}
void RecordLumenGather(RHIFrameEncoder& encoder,const RenderFrame& frame,const PipelineCamera& camera,
                       const MaterialPipelineSettings& settings,SceneEffectsConstants effects,SurfacePassConstants& surface,
                       const PostProcessConstants& post,bool cut,std::uint64_t contentHash) {
    const auto& s=settings.lumenGi;auto c=LumenConstants(s);c.inverseView=glm::inverse(camera.view);c.camera=glm::vec4(camera.position,s.gatherHistory);
    c.settings.z=lumenGeometryChanged||lumenPending.lightingKey!=lumenCompleted.lightingKey?1:0;
    const bool history=lumenCompleted.viewValid&&!cut&&!s.reset&&lumenCompleted.viewKey==contentHash;
    const unsigned previous=lumenCompleted.viewImage,write=history?1-previous:0;c.previousVP=lumenCompleted.vp;c.gather.w=history?1:0;
    Upload(encoder,lumenBuffer,&c,sizeof(c),LumenGiBinding);
    auto capture=effects;capture.effects.y=0;capture.screenAndAo.z=0;Effects(encoder,capture);
    const auto mainProjection=surface.projection;surface.projection=camera.projection;
    surface.options.z=1;Upload(encoder,surfaceBuffer,&surface,sizeof(surface),SurfacePassBinding);
    BindTarget(encoder,diffuseCapture,RHICommand::ClearColor|RHICommand::ClearDepth);
    // World irradiance reconstruction needs depth and geometric normals only.
    // The legacy SSGI path still captures its direct diffuse source separately.
    Depth(encoder,frame,camera.projection*camera.view,false);
    BindTarget(encoder,normalCapture,RHICommand::ClearColor);CaptureSurface(encoder,frame,camera,true);
    IndirectLightingSettings giSettings;giSettings.enabled=true;giSettings.radius=s.maxDistance;giSettings.bias=s.rayBias;giSettings.thickness=s.thickness;
    auto gi=MakeIndirectLightingConstants(giSettings,camera.projection,camera.view);Upload(encoder,indirectBuffer,&gi,sizeof(gi),IndirectLightingBinding);
    auto capturePost=post;capturePost.projection=camera.projection;capturePost.inverseProjection=glm::inverse(camera.projection);
    Fullscreen(encoder,indirectPositions,indirectGeometry,{},{},diffuseCapture.depth,capturePost);
    const auto draw=[&](const Target& target,RenderResourceHandle<PipelineSpec> program,RenderResourceHandle<RHITextureSpec> input,
                        RenderResourceHandle<RHITextureSpec> attributes,RenderResourceHandle<RHITextureSpec> cache,RenderResourceHandle<RHITextureSpec> extra={}) {
        BindTarget(encoder,target);Require(encoder.BindPipeline(program)&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(input,0)&&
            encoder.BindTexture(normalCapture.color,1)&&encoder.BindTexture(diffuseCapture.depth,2)&&encoder.BindTexture(indirectPositions.color,3)&&
            encoder.BindTexture(attributes,4)&&encoder.BindTexture(cache,5)&&encoder.BindTexture(realtimeGeometry,6)&&encoder.BindTexture(extra,7)&&
            encoder.BindTexture(lumenDistanceField,8)&&
            encoder.BindTexture(lumenIncidentCache[lumenPending.image].color,9)&&
            encoder.BindTexture(realtimeCache[realtimePending.image].color,15)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot record hybrid final gather");
    };
    draw(lumenGuides[write],lumenGuide,diffuseCapture.color,lumenAttributes,lumenCache[lumenPending.image].color);
    draw(lumenRaw,lumenGather,diffuseCapture.color,lumenAttributes,lumenCache[lumenPending.image].color,lumenDirectCache[lumenPending.image].color);
    surface.screenJitter.z=float(s.lightingView);Upload(encoder,surfaceBuffer,&surface,sizeof(surface),SurfacePassBinding);
    auto resolveCache=lumenIncidentCache[lumenPending.image].color;
    if(s.lightingView==LumenLightingView::SurfaceCache)resolveCache=lumenCache[lumenPending.image].color;
    else if(s.lightingView==LumenLightingView::DirectOnly)resolveCache=lumenDirectCache[lumenPending.image].color;
    const bool receiverDirect=s.fullResolutionDirectLighting&&
        (s.lightingView==LumenLightingView::SurfaceCache||s.lightingView==LumenLightingView::DirectOnly);
    draw(indirectFull,receiverDirect?lumenResolveDirect:lumenResolve,lumenRaw.color,lumenGuides[write].color,resolveCache,lumenAttributes);
    if(s.reflections&&s.screenTraces) {
        // Reuse the just-resolved world diffuse lighting instead of evaluating
        // the 3D probe field again for every reflected-source fragment.
        const auto mainScreenTime=surface.screenTime,mainJitter=surface.screenJitter;
        surface.screenTime.x=1.f/lumenSpecularSource.width;surface.screenTime.y=1.f/lumenSpecularSource.height;
        surface.screenJitter={};Upload(encoder,surfaceBuffer,&surface,sizeof(surface),SurfacePassBinding);
        const DiffuseProbeRuntimeConstants sourceRuntime{glm::vec4(3,1,0,0)};
        Upload(encoder,probeRuntimeBuffer,&sourceRuntime,sizeof(sourceRuntime),DiffuseProbeSettingsBinding);
        BindTarget(encoder,lumenSpecularSource,RHICommand::ClearColor|RHICommand::ClearDepth);
        Require(encoder.BindTexture(indirectFull.color,15),"Cannot reuse diffuse lighting in reflection source");
        Scene(encoder,frame,camera,false,false,0);
        surface.screenTime=mainScreenTime;surface.screenJitter=mainJitter;
        Upload(encoder,surfaceBuffer,&surface,sizeof(surface),SurfacePassBinding);
        ++statistics.lumenGiPasses;
    }
    if(s.reflections) {
        c.reflection.y=1;c.reflection.z=float(width)/lumenReflectionFull.width;c.fieldOrigin.w=float(height)/lumenReflectionFull.height;
        c.gather.x=int(lumenReflectionRaw.width);c.gather.y=int(lumenReflectionRaw.height);Upload(encoder,lumenBuffer,&c,sizeof(c),LumenGiBinding);
        // Diffuse gathering uses geometric normals. Reflection tracing gets
        // material shading normals, including normal maps, after that gather.
        surface.options.w=1;Upload(encoder,surfaceBuffer,&surface,sizeof(surface),SurfacePassBinding);
        BindTarget(encoder,normalCapture,RHICommand::ClearColor);CaptureSurface(encoder,frame,camera,true);
        draw(lumenReflectionGuides[write],lumenGuide,diffuseCapture.color,lumenAttributes,lumenCache[lumenPending.image].color);
        for(unsigned lobe=0;lobe<2;++lobe) {
            c.reflection.w=float(lobe);Upload(encoder,lumenBuffer,&c,sizeof(c),LumenGiBinding);
            draw(lumenReflectionRaw,lumenReflect,lumenSpecularSource.color,lumenAttributes,lumenCache[lumenPending.image].color,lumenDirectCache[lumenPending.image].color);
            auto& histories=lobe?lumenGrazingHistory:lumenReflectionHistory;
            draw(histories[write],lumenReflectTemporal,lumenReflectionRaw.color,lumenReflectionGuides[write].color,
                 history?histories[previous].color:RenderResourceHandle<RHITextureSpec>{},history?lumenReflectionGuides[previous].color:RenderResourceHandle<RHITextureSpec>{});
            auto radiance=histories[write].color;
            for(unsigned pass=0;pass<2;++pass){gi.options.w=1<<pass;Upload(encoder,indirectBuffer,&gi,sizeof(gi),IndirectLightingBinding);
                draw(lumenReflectionWork[pass],lumenReflectFilter,radiance,lumenReflectionGuides[write].color,{});radiance=lumenReflectionWork[pass].color;}
            draw(lobe?lumenReflectionFullGrazing:lumenReflectionFull,lumenReflectResolve,radiance,lumenReflectionGuides[write].color,{});
        }
        surface.options.w=0;surface.options.y=1;statistics.lumenGiPasses+=12;
    }
    surface.options.z=0;surface.projection=mainProjection;
    const DiffuseProbeRuntimeConstants runtime{glm::vec4(3,1,0,0)};Upload(encoder,probeRuntimeBuffer,&runtime,sizeof(runtime),DiffuseProbeSettingsBinding);
    lumenPending.viewValid=true;lumenPending.viewKey=contentHash;lumenPending.viewImage=write;lumenPending.vp=camera.projection*camera.view;
    statistics.lumenScreenProbes=lumenRaw.width*lumenRaw.height;statistics.lumenHistoryUsed=history;statistics.lumenGiPasses+=6;
}
