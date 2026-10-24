// Included inside MaterialRenderPipeline::Impl. Each rectangle quadrature cell
// has six cached perspective depth tiles. Changes build a replacement atlas;
// the last completed atlas remains intact if recording/submission is discarded.
void AreaShadowAtlasPoolStatistics() {
    statistics.areaLightShadowAtlasPoolEntries=std::uint32_t(areaLightShadowAtlasPool.size());
    statistics.areaLightShadowAtlasPoolBytes=0;
    for(const auto& entry:areaLightShadowAtlasPool)
        statistics.areaLightShadowAtlasPoolBytes+=std::uint64_t(entry.atlas.width)*entry.atlas.height*sizeof(float);
}
std::pair<Target,std::shared_ptr<const void>> AcquireAreaShadowTarget(
    RHIFrameEncoder&,std::uint32_t width,std::uint32_t height,bool reuse) {
    if(reuse) {
        // Retire only unused obsolete sizes. An encoder can still own a lease
        // after DiscardFrame, before Cancel/retirement releases its frame pins.
        std::erase_if(areaLightShadowAtlasPool,[&](const auto& entry){
            return entry.owner.use_count()==1&&(entry.atlas.width!=width||entry.atlas.height!=height);
        });
        for(const auto& entry:areaLightShadowAtlasPool)
            if(entry.atlas.width==width&&entry.atlas.height==height&&entry.owner.use_count()==1) {
                ++statistics.areaLightShadowAtlasReuses;AreaShadowAtlasPoolStatistics();
                return {entry.atlas,entry.owner};
            }
    }
    PipelineDetail::ResourceBuilder builder(device);
    auto target=MakeTarget(builder,width,height,false,true);auto owner=builder.Finish();
    if(reuse)areaLightShadowAtlasPool.push_back({target,owner});
    ++statistics.areaLightShadowAtlasAllocations;AreaShadowAtlasPoolStatistics();
    return {target,std::move(owner)};
}
bool AreaShadowCasterOutside(const RenderItem& item,const glm::mat4& viewProjection) const {
    if(item.EffectiveLayer()!=RenderLayer::Opaque||!item.shadowCasterBounds)return false;
    const auto& bounds=*item.shadowCasterBounds;
    for(unsigned axis=0;axis<3;++axis)
        if(!std::isfinite(bounds.minimum[axis])||!std::isfinite(bounds.maximum[axis])||bounds.minimum[axis]>bounds.maximum[axis])return false;
    std::array<float,6> maximum;maximum.fill(-std::numeric_limits<float>::infinity());
    float scale=1.f,nearMinimum=std::numeric_limits<float>::infinity();
    for(unsigned corner=0;corner<8;++corner) {
        glm::vec3 world;for(unsigned axis=0;axis<3;++axis)world[axis]=(corner&(1u<<axis))?bounds.maximum[axis]:bounds.minimum[axis];
        const auto clip=viewProjection*glm::vec4(world,1.f);
        for(unsigned channel=0;channel<4;++channel){if(!std::isfinite(clip[channel]))return false;scale=std::max(scale,std::abs(clip[channel]));}
        const std::array<float,6> distance{{clip.x+clip.w,clip.w-clip.x,clip.y+clip.w,clip.w-clip.y,clip.z+clip.w,clip.w-clip.z}};
        for(unsigned plane=0;plane<distance.size();++plane){if(!std::isfinite(distance[plane]))return false;maximum[plane]=std::max(maximum[plane],distance[plane]);}
        nearMinimum=std::min(nearMinimum,distance[4]);
    }
    // Keep touching/near-plane-crossing bounds. This slack also covers the
    // FP32 difference between a bound-corner test and the original vertex path.
    const float tolerance=std::max(2e-4f,128.f*std::numeric_limits<float>::epsilon()*scale);
    if(nearMinimum<=tolerance&&maximum[4]>=-tolerance)return false;
    for(float plane:maximum)if(plane < -tolerance)return true;
    return false;
}
void RecordAreaLightShadows(RHIFrameEncoder& encoder,const RenderFrame& frame,const MaterialPipelineSettings& settings) {
    AreaShadowAtlasPoolStatistics();
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
        add(shadow.reuseCasterPreparation);add(shadow.cullCasters);add(shadow.reuseAtlasStorage);
        // Raster visibility/caster flags and transforms may be edited without
        // modifying the distance-volume representation. They also affect depth.
        for(const auto& item:frame.items) {
            if(item.EffectiveLayer()!=RenderLayer::Opaque&&item.EffectiveLayer()!=RenderLayer::Cutout)continue;
            add(item.mesh);add(item.model);add(item.draw.indexCount);add(item.draw.firstIndex);add(item.draw.baseVertex);
            add(item.draw.instanceCount);add(item.draw.firstInstance);add(item.castsShadows);
            if(item.material)add(item.material->Resources().shadowPipeline);
            if(shadow.cullCasters){add(item.EffectiveLayer());add(bool(item.shadowCasterBounds));
                if(item.shadowCasterBounds){add(item.shadowCasterBounds->minimum);add(item.shadowCasterBounds->maximum);}}
        }
        if(!areaLightShadowCompleted.valid||areaLightShadowCompleted.key!=key) {
            Require(encoder.BeginGpuTiming(RHICommand::GpuTimingScope::AreaLightShadowUpdate),"Cannot begin rectangle shadow update timing");
            auto [target,owner]=AcquireAreaShadowTarget(encoder,columns*tile,rows*tile,shadow.reuseAtlasStorage);
            Require(encoder.KeepAlive(owner),"Cannot retain changed rectangle shadow atlas");
            Require(encoder.BindTexture({},AreaLightShadowTextureSlot),"Cannot unbind rectangle shadow atlas");
            BindTarget(encoder,target,RHICommand::ClearDepth);
            const auto projection=glm::perspective(glm::radians(90.f),1.f,shadow.nearPlane,sc.maxDistance);
            const std::array<glm::vec3,6> forward{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};
            const std::array<glm::vec3,6> up{{{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}}};
            std::vector<const RenderItem*> casters;std::vector<std::uint32_t> parameterSizes;
            std::uint64_t parameterBytes=0;
            for(const auto& item:frame.items) {
                if((item.EffectiveLayer()!=RenderLayer::Opaque&&item.EffectiveLayer()!=RenderLayer::Cutout)||!item.castsShadows||
                    !item.material||!item.material->Resources().shadowPipeline.IsValid())continue;
                casters.push_back(&item);parameterSizes.push_back(std::uint32_t(item.material->Parameters().Bytes().size()));
                parameterBytes+=parameterSizes.back();
            }
            statistics.areaLightShadowCasterCount=std::uint32_t(casters.size());
            if(shadow.reuseCasterPreparation) {
                if(!areaCasterPreparationOwner||areaCasterParameterSizes!=parameterSizes) {
                    PipelineDetail::ResourceBuilder b(device);
                    std::vector<RenderResourceHandle<UniformBufferSpec>> objects,materials;
                    objects.reserve(casters.size());materials.reserve(casters.size());
                    for(auto bytes:parameterSizes){objects.push_back(b.Uniform(sizeof(glm::mat4)));materials.push_back(bytes?b.Uniform(bytes):RenderResourceHandle<UniformBufferSpec>{});}
                    auto lease=b.Finish();areaCasterObjectBuffers=std::move(objects);areaCasterMaterialBuffers=std::move(materials);
                    areaCasterParameterSizes=parameterSizes;areaCasterPreparationOwner=std::move(lease);
                }
                Require(encoder.KeepAlive(areaCasterPreparationOwner),"Cannot retain prepared shadow caster uniforms");
                for(std::size_t i=0;i<casters.size();++i){const auto& item=*casters[i];const auto& bytes=item.material->Parameters().Bytes();
                    Require(encoder.KeepAlive(item.material),"Cannot retain prepared shadow caster material");
                    Upload(encoder,areaCasterObjectBuffers[i],&item.model,sizeof(item.model),1);
                    if(!bytes.empty())Upload(encoder,areaCasterMaterialBuffers[i],bytes.data(),std::uint32_t(bytes.size()),2);
                }
                statistics.areaLightShadowCasterPreparationUsed=true;
                statistics.areaLightShadowMaterialPins+=casters.size();
                statistics.areaLightShadowCasterObjectBytes+=casters.size()*sizeof(glm::mat4);
                statistics.areaLightShadowCasterParameterBytes+=parameterBytes;
            }
            // Only the prepared path omits redundant binds. The shaders,
            // texture values, object matrices and retained draw order stay the
            // same, including cutout/unknown/position-modifying materials.
            RenderResourceHandle<PipelineSpec> boundPipeline;RenderResourceHandle<VertexBufferSpec> boundMesh;
            RenderResourceHandle<UniformBufferSpec> boundObject,boundMaterial;
            std::array<RenderResourceHandle<RHITextureSpec>,16> boundTextures{};std::uint32_t knownTextures=0;
            for(unsigned y=0;y<grid;++y)for(unsigned x=0;x<grid;++x) {
                const auto parameter=AreaLightSampleParameters(x,y,grid,areas.stratifiedSamples);
                const auto source=light.center+light.halfAxisU*parameter.x+light.halfAxisV*parameter.y;
                for(unsigned face=0;face<6;++face) {
                    const auto index=(y*grid+x)*6+face;
                    const auto left=std::int32_t((index%columns)*tile),bottom=std::int32_t((index/columns)*tile);
                    Require(encoder.SetViewport({left,bottom,tile,tile})&&encoder.SetScissor({true,left,bottom,tile,tile}),
                        "Cannot set rectangle shadow tile");
                    const auto viewProjection=projection*glm::lookAt(source,source+forward[face],up[face]);
                    if(!shadow.reuseCasterPreparation&&!shadow.cullCasters) {
                        Depth(encoder,frame,viewProjection,true);
                        statistics.areaLightShadowCasterDraws+=std::uint32_t(casters.size());
                        statistics.areaLightShadowMaterialPins+=casters.size();
                        statistics.areaLightShadowCasterObjectBytes+=casters.size()*sizeof(glm::mat4);
                        statistics.areaLightShadowCasterParameterBytes+=parameterBytes;
                    } else {
                        ViewConstants view;view.viewProjection=viewProjection;Upload(encoder,viewBuffer,&view,sizeof(view),0);
                        for(std::size_t i=0;i<casters.size();++i){const auto& item=*casters[i];
                            if(shadow.cullCasters&&AreaShadowCasterOutside(item,viewProjection)){++statistics.areaLightShadowCulledCasters;continue;}
                            const auto& resources=item.material->Resources();
                            if(!shadow.reuseCasterPreparation) {
                                Require(encoder.KeepAlive(item.material)&&encoder.BindPipeline(resources.shadowPipeline),"Cannot retain shadow caster");
                                for(const auto& binding:resources.textures)Require(encoder.BindTexture(binding.texture,binding.slot),"Cannot bind shadow caster texture");
                                const auto& bytes=item.material->Parameters().Bytes();
                                if(!bytes.empty())Upload(encoder,resources.parameterBuffer,bytes.data(),std::uint32_t(bytes.size()),2);
                                Upload(encoder,objectBuffer,&item.model,sizeof(item.model),1);
                                Require(encoder.BindMesh(item.mesh)&&encoder.DrawIndexed(item.draw),"Cannot draw shadow caster");
                                ++statistics.areaLightShadowMaterialPins;statistics.areaLightShadowCasterObjectBytes+=sizeof(item.model);
                                statistics.areaLightShadowCasterParameterBytes+=bytes.size();
                            } else {
                                if(boundPipeline!=resources.shadowPipeline){Require(encoder.BindPipeline(resources.shadowPipeline),"Cannot bind prepared shadow pipeline");boundPipeline=resources.shadowPipeline;}
                                for(const auto& binding:resources.textures){
                                    if(binding.slot<boundTextures.size()&&(knownTextures&(1u<<binding.slot))&&boundTextures[binding.slot]==binding.texture)continue;
                                    Require(encoder.BindTexture(binding.texture,binding.slot),"Cannot bind prepared shadow texture");
                                    if(binding.slot<boundTextures.size()){boundTextures[binding.slot]=binding.texture;knownTextures|=1u<<binding.slot;}
                                }
                                if(boundObject!=areaCasterObjectBuffers[i]){Require(encoder.BindUniformBuffer(areaCasterObjectBuffers[i],1),"Cannot bind prepared shadow object");boundObject=areaCasterObjectBuffers[i];}
                                if(parameterSizes[i]&&boundMaterial!=areaCasterMaterialBuffers[i]){Require(encoder.BindUniformBuffer(areaCasterMaterialBuffers[i],2),"Cannot bind prepared shadow material");boundMaterial=areaCasterMaterialBuffers[i];}
                                if(boundMesh!=item.mesh){Require(encoder.BindMesh(item.mesh),"Cannot bind prepared shadow mesh");boundMesh=item.mesh;}
                                Require(encoder.DrawIndexed(item.draw),"Cannot draw prepared shadow caster");
                            }
                            ++statistics.areaLightShadowCasterDraws;
                        }
                    }
                    ++statistics.areaLightShadowPasses;
                }
            }
            areaLightShadowPending={true,key,target,std::move(owner)};
            statistics.areaLightShadowsUpdated=true;
            // Sampling an attached depth texture is forbidden. Leave it before
            // installing the main PBR descriptor, including on the first frame.
            BindTarget(encoder,shadows);
            Require(encoder.EndGpuTiming(RHICommand::GpuTimingScope::AreaLightShadowUpdate),"Cannot end rectangle shadow update timing");
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
