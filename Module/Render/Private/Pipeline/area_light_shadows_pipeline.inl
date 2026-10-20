// Included inside MaterialRenderPipeline::Impl. Each rectangle quadrature cell
// has six cached perspective depth tiles. Changes build a replacement atlas;
// the last completed atlas remains intact if recording/submission is discarded.
void RecordAreaLightShadows(RHIFrameEncoder& encoder,const RenderFrame& frame,const MaterialPipelineSettings& settings) {
    const auto& sc=settings.surfaceCacheGi;if(!sc.areaLightShadows)return;
    if(!areaLightShadowConstantsOwner) {
        PipelineDetail::ResourceBuilder b(device);
        areaLightShadowBuffer=b.Uniform(sizeof(AreaLightShadowConstants));areaLightShadowConstantsOwner=b.Finish();
    }
    Require(encoder.KeepAlive(areaLightShadowConstantsOwner),"Cannot retain rectangle shadow constants");
    const auto& areas=settings.areaLights;const auto& shadow=sc.areaShadows;
    // Binding 12 is reused by GI later in the frame. Always install either
    // valid shadow constants or explicit disabled constants before opaque PBR.
    const bool active=shadow.enabled&&areas.count==1&&areas.lights[0].enabled&&glm::any(glm::greaterThan(areas.lights[0].radiance,glm::vec3(0)));
    AreaLightShadowConstants constants;
    constants.parameters={shadow.nearPlane,sc.maxDistance,shadow.depthBiasTexels,shadow.normalBiasTexels};
    if(active) {
        const auto& light=areas.lights[0];const auto grid=areas.samplesPerAxis;
        const auto tile=grid==8?shadow.tileResolution/2:shadow.tileResolution;
        constexpr unsigned columns=12;const auto rows=(grid*grid*6+columns-1)/columns;
        std::uint64_t key=14695981039346656037ull;
        const auto add=[&](const auto& value){const auto* bytes=reinterpret_cast<const unsigned char*>(&value);
            for(std::size_t i=0;i<sizeof(value);++i){key^=bytes[i];key*=1099511628211ull;}};
        add(sc.scene.get());add(sc.scene->GeometryGeneration());
        add(light.center);add(light.halfAxisU);add(light.halfAxisV);add(grid);add(tile);add(areas.stratifiedSamples);
        add(shadow.nearPlane);add(sc.maxDistance);
        // Raster visibility/caster flags and transforms may be edited without
        // modifying the distance-volume representation. They also affect depth.
        for(const auto& item:frame.items) {
            if(item.EffectiveLayer()!=RenderLayer::Opaque&&item.EffectiveLayer()!=RenderLayer::Cutout)continue;
            add(item.mesh);add(item.model);add(item.draw.indexCount);add(item.draw.firstIndex);add(item.draw.baseVertex);
            add(item.draw.instanceCount);add(item.draw.firstInstance);add(item.castsShadows);
            if(item.material)add(item.material->Resources().shadowPipeline);
        }
        if(!areaLightShadowCompleted.valid||areaLightShadowCompleted.key!=key) {
            PipelineDetail::ResourceBuilder b(device);
            auto target=MakeTarget(b,columns*tile,rows*tile,false,true);
            auto owner=b.Finish();
            Require(encoder.KeepAlive(owner),"Cannot retain changed rectangle shadow atlas");
            Require(encoder.BindTexture({},AreaLightShadowTextureSlot),"Cannot unbind rectangle shadow atlas");
            BindTarget(encoder,target,RHICommand::ClearDepth);
            const auto projection=glm::perspective(glm::radians(90.f),1.f,shadow.nearPlane,sc.maxDistance);
            const std::array<glm::vec3,6> forward{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};
            const std::array<glm::vec3,6> up{{{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}}};
            for(unsigned y=0;y<grid;++y)for(unsigned x=0;x<grid;++x) {
                const auto parameter=AreaLightSampleParameters(x,y,grid,areas.stratifiedSamples);
                const auto source=light.center+light.halfAxisU*parameter.x+light.halfAxisV*parameter.y;
                for(unsigned face=0;face<6;++face) {
                    const auto index=(y*grid+x)*6+face;
                    const auto left=std::int32_t((index%columns)*tile),bottom=std::int32_t((index/columns)*tile);
                    Require(encoder.SetViewport({left,bottom,tile,tile})&&encoder.SetScissor({true,left,bottom,tile,tile}),
                        "Cannot set rectangle shadow tile");
                    Depth(encoder,frame,projection*glm::lookAt(source,source+forward[face],up[face]),true);
                    ++statistics.areaLightShadowPasses;
                }
            }
            areaLightShadowPending={true,key,target,std::move(owner)};
            statistics.areaLightShadowsUpdated=true;
            // Sampling an attached depth texture is forbidden. Leave it before
            // installing the main PBR descriptor, including on the first frame.
            BindTarget(encoder,shadows);
        } else areaLightShadowPending=areaLightShadowCompleted;
        const auto& target=areaLightShadowPending.atlas;
        Require(encoder.KeepAlive(areaLightShadowPending.owner),"Cannot retain cached rectangle shadow atlas");
        constants.atlas={int(target.width),int(target.height),int(tile),int(columns)};
        constants.options={1,int(grid),0,0};
        Require(encoder.BindTexture(target.depth,AreaLightShadowTextureSlot),"Cannot bind rectangle shadow atlas");
        statistics.areaLightShadowBytes=std::uint64_t(target.width)*target.height*4;
        statistics.areaLightShadowCacheUsed=!statistics.areaLightShadowsUpdated;
    } else {
        // A disabled emitter must work from startup without an initialized atlas.
        Require(encoder.BindTexture({},AreaLightShadowTextureSlot),"Cannot clear disabled rectangle shadow atlas");
    }
    Upload(encoder,areaLightShadowBuffer,&constants,sizeof(constants),AreaLightShadowBinding);
}
