    static bool SkinParameterFlag(const Material::MaterialSnapshot& material,const char* name) {
        const auto& parameters=material.Parameters();
        const auto* layout=parameters.GetTemplate()->FindLayout(name);
        if(!layout || layout->size<sizeof(std::uint32_t) || layout->offset+sizeof(std::uint32_t)>parameters.Bytes().size())return false;
        std::uint32_t value=0;
        std::memcpy(&value,parameters.Bytes().data()+layout->offset,sizeof(value));
        return value!=0;
    }
    auto SkinReadOnlyVariant(RHIFrameEncoder& encoder,RenderResourceHandle<PipelineSpec> source,
                             std::shared_ptr<const void> sourceOwner) {
        const auto key=std::make_pair(source.id,source.version);
        auto found=skinDepthVariants.find(key);
        if(found==skinDepthVariants.end()) {
            PipelineDetail::ResourceBuilder builder(device);
            auto pipeline=builder.DepthReadOnlyPipeline(source);
            auto owner=std::make_shared<std::pair<std::shared_ptr<const void>,std::shared_ptr<const void>>>(builder.Finish(),std::move(sourceOwner));
            found=skinDepthVariants.emplace(key,SkinDepthVariant{pipeline,std::move(owner),true}).first;
        }
        found->second.used=true;
        Require(encoder.KeepAlive(found->second.owner),"Cannot retain read-only skin capture variant");
        return found->second.pipeline;
    }
    void EnsureSkinDiffusion() {
        if(!skinProgramsOwner) {
            PipelineDetail::ResourceBuilder builder(device);
            skinBlurProgram=builder.FullscreenPipeline(FullscreenVertexShader(),PipelineDetail::SkinDiffusionBlurShader());
            skinBurleyProgram=builder.FullscreenPipeline(FullscreenVertexShader(),PipelineDetail::SkinDiffusionBurleyShader());
            CreateGraphicShaderDesc shader;
            shader.vertexShaderSource=builder.Source(ShaderSourceType::Vertex,FullscreenVertexShader());
            shader.fragmentShaderSource=builder.Source(ShaderSourceType::Fragment,PipelineDetail::SkinDiffusionComposeShader());
            auto program=builder.Create<ShaderProgramSpec>([&](auto cb){device.async_CreateGraphicShader(shader,cb);});
            PipelineSpec spec;spec.shaderProgram=program;
            spec.expectVertexLayout={{VertexFieldType::Vec2,VertexFieldType::Vec2}};
            spec.depthTest=spec.depthWrite=false;spec.cullMode=CullMode::None;
            spec.blendEnable=true;spec.blendMode=BlendMode::AdditiveRaw;
            skinComposeProgram=builder.Create<PipelineSpec>([&](auto cb){device.async_CreatePipeline({spec},cb);});
            skinBuffer=builder.Uniform(sizeof(PipelineDetail::SkinDiffusionConstants));
            skinProgramsOwner=builder.Finish();
        }
        if(!skinTargetsOwner||skinDiffuse.width!=width||skinDiffuse.height!=height||skinAttachmentDepth!=scene.depth) {
            PipelineDetail::ResourceBuilder builder(device);
            auto diffuse=MakeTarget(builder,width,height,true,false);
            diffuse.framebuffer=builder.Target(diffuse.color,scene.depth);
            auto albedo=MakeTarget(builder,width,height,true,false);
            albedo.framebuffer=builder.Target(albedo.color,scene.depth);
            Target profile;profile.width=width;profile.height=height;
            profile.color=builder.Texture(width,height,RHITextureFormat::RGBA32F);
            profile.framebuffer=builder.Target(profile.color,scene.depth);
            std::array<Target,2> work;
            for(auto& target:work)target=MakeTarget(builder,width,height,true,false);
            auto owner=builder.Finish();
            skinDiffuse=diffuse;skinAlbedo=albedo;skinProfile=profile;skinWork=work;skinAttachmentDepth=scene.depth;skinTargetsOwner=std::move(owner);
        }
    }
    void RecordSkinDiffusion(RHIFrameEncoder& encoder,const RenderFrame& frame,const PipelineCamera& camera,
                             const SkinDiffusionSettings& settings,const SurfacePassConstants& surface,const Target& sceneTarget) {
        if(!settings.enabled||settings.strength<=0||settings.radiusMillimeters<=0)return;
        std::map<const Material::MaterialSnapshot*,float> groups;
        for(const auto& item:frame.items) {
            if(!item.material || (item.EffectiveLayer()!=RenderLayer::Opaque&&item.EffectiveLayer()!=RenderLayer::Cutout))continue;
            if(!SkinParameterFlag(*item.material,"skinEnabled")||!SkinParameterFlag(*item.material,"skinDiffusionEnabled"))continue;
            const auto& resources=item.material->Resources();
            if(!resources.normalPipeline.IsValid()||!resources.diffuseRadiancePipeline.IsValid())continue;
            if(!groups.count(item.material.get()))groups.emplace(item.material.get(),float(groups.size()+1));
        }
        if(groups.empty())return;
        EnsureSkinDiffusion();
        Require(encoder.KeepAlive(skinProgramsOwner)&&encoder.KeepAlive(skinTargetsOwner),"Cannot retain skin diffusion resources");
        const ViewConstants view{camera.projection*camera.view,glm::vec4(camera.position,1)};
        Upload(encoder,viewBuffer,&view,sizeof(view),0);
        // Read-only Equal variants attach the existing opaque depth. Never
        // sample an attachment and never permit a tolerance behind near cards.
        for(auto& [key,variant]:skinDepthVariants)variant.used=false;
        Require(encoder.BindTexture({},9),"Cannot detach sampled opaque depth for skin capture");
        for(int pass=0;pass<3;++pass) {
            BindTarget(encoder,pass==0?skinDiffuse:pass==1?skinProfile:skinAlbedo,RHICommand::ClearColor);
            for(const auto& item:frame.items) {
                auto group=groups.find(item.material.get());if(group==groups.end())continue;
                const auto& resources=item.material->Resources();
                auto capture=surface;capture.view=camera.view;capture.projection=camera.projection;
                capture.options.z=12.f+float(pass);capture.options.w=group->second;capture.options.y=0;
                Upload(encoder,surfaceBuffer,&capture,sizeof(capture),SurfacePassBinding);
                const auto variant=SkinReadOnlyVariant(encoder,pass==1?resources.normalPipeline:resources.diffuseRadiancePipeline,resources.lifetime);
                Require(encoder.KeepAlive(item.material)&&encoder.BindPipeline(variant),"Cannot bind skin capture");
                for(const auto& binding:resources.textures)Require(encoder.BindTexture(binding.texture,binding.slot),"Cannot bind skin texture");

                const auto& bytes=item.material->Parameters().Bytes();
                if(!bytes.empty())Upload(encoder,resources.parameterBuffer,bytes.data(),static_cast<std::uint32_t>(bytes.size()),2);
                Upload(encoder,objectBuffer,&item.model,sizeof(item.model),1);
                Require(encoder.BindMesh(item.mesh)&&encoder.DrawIndexed(item.draw),"Cannot capture skin diffuse/normal");
                ++statistics.skinDiffusionDraws;
            }
        }
        std::erase_if(skinDepthVariants,[](const auto& entry){return !entry.second.used;});
        PipelineDetail::SkinDiffusionConstants constants;
        constants.projection=camera.projection;constants.inverseProjection=glm::inverse(camera.projection);
        constants.radius=glm::vec4(settings.radiusMillimeters*.001f*settings.worldUnitScale,settings.strength,settings.normalRejectCosine,settings.depthRejectScale);
        constants.channels=glm::vec4(settings.channelRadii,settings.maxRadiusPixels);
        constants.texturing.x=settings.albedoPreservation;
        constants.radial=glm::vec4(float(settings.diskSamples),settings.worldUnitScale,0,0);
        for(int pass=0;pass<2;++pass) {
            BindTarget(encoder,skinWork[pass]);
            constants.viewport=glm::vec4(float(width),float(height),pass?0.f:1.f,pass?1.f:0.f);
            constants.texturing.y=pass?0.f:1.f;
            Upload(encoder,skinBuffer,&constants,sizeof(constants),5);
            Require(encoder.BindPipeline(settings.kernel==SkinDiffusionKernel::SourceBurley?skinBurleyProgram:skinBlurProgram)&&encoder.BindMesh(fullscreen)&&
                encoder.BindTexture(pass?skinWork[0].color:skinDiffuse.color,0)&&encoder.BindTexture(skinProfile.color,1)&&encoder.BindTexture(scene.depth,2)&&encoder.BindTexture(skinAlbedo.color,3)&&
                encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot diffuse skin radiance");
        }
        BindTarget(encoder,sceneTarget);
        Upload(encoder,skinBuffer,&constants,sizeof(constants),5);
        Require(encoder.BindPipeline(skinComposeProgram)&&encoder.BindMesh(fullscreen)&&encoder.BindTexture(skinDiffuse.color,0)&&
            encoder.BindTexture(skinWork[1].color,1)&&encoder.BindTexture(skinProfile.color,2)&&encoder.BindTexture(skinAlbedo.color,3)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3}),"Cannot compose skin diffusion");
        Upload(encoder,surfaceBuffer,&surface,sizeof(surface),SurfacePassBinding);
        Resolve(encoder,sceneTarget,scene,true,false);
        statistics.skinDiffusionPasses=6;
    }
