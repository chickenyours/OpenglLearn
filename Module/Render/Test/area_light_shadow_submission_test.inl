// Included in the area-light test namespace. This entry point exercises the
// actual material pipeline on either backend, independently of GL-only BRDF QA.
template<class F> auto ShadowThread(RHIDevice& device,F operation) {
    using T=std::invoke_result_t<F>;
    auto done=std::make_shared<std::promise<T>>();auto result=done->get_future();
    device.async_ExecuteCode([done,operation]{try {
        if constexpr(std::is_void_v<T>){operation();done->set_value();}
        else done->set_value(operation());
    } catch(...){done->set_exception(std::current_exception());}});
    return result.get();
}
void ShadowSubmit(RHIDevice& device,RHIFrameEncoder& encoder) {
    Check(encoder.End(false),"End shadow contract frame");
    auto done=std::make_shared<Completion<bool>>();
    Check(device.async_SubmitFrameCommands(encoder.GetCommandBuffer(),[done]{done->done=true;}),"Submit shadow contract frame");
    const auto deadline=std::chrono::steady_clock::now()+30s;
    while(!done->done){device.returnSystem.DrainCallbacks();
        Check(std::chrono::steady_clock::now()<deadline,"Shadow contract frame timed out");
        if(!done->done)device.returnSystem.WaitForCallbacks(2ms);}
}
void ShadowAtlasContracts(RHIDevice& device,bool vulkan) {
    PipelineDetail::ResourceBuilder builder(device);
    const std::string vertex=R"GLSL(#version 450 core
layout(location=0) in vec3 position;
layout(std140,binding=0) uniform ViewData {mat4 viewProjection;vec4 cameraPosition;};
layout(std140,binding=1) uniform ObjectData {mat4 model;};
invariant gl_Position;
void main(){gl_Position=viewProjection*model*vec4(position,1);}
)GLSL";
    const auto program=[&](const std::string& fragment){CreateGraphicShaderDesc desc;
        desc.vertexShaderSource=builder.Source(ShaderSourceType::Vertex,vertex);
        desc.fragmentShaderSource=builder.Source(ShaderSourceType::Fragment,fragment);
        return builder.Create<ShaderProgramSpec>([&](auto cb){device.async_CreateGraphicShader(desc,cb);});};
    const auto pipelineFor=[&](auto shader){PipelineSpec desc;desc.shaderProgram=shader;
        desc.expectVertexLayout={{VertexFieldType::Vec3}};desc.cullMode=CullMode::None;
        return builder.Create<PipelineSpec>([&](auto cb){device.async_CreatePipeline({desc},cb);});};
    const auto colorPipeline=pipelineFor(program("#version 450 core\nlayout(location=0) out vec4 outColor;void main(){outColor=vec4(.3,.4,.5,1);}"));
    const auto depthPipeline=pipelineFor(program("#version 450 core\nvoid main(){}"));
    const std::array<float,9> vertices{{-.8f,0,-.8f,.8f,0,-.8f,0,0,.8f}};
    const std::array<std::uint32_t,3> indices{{0,1,2}};
    CreateMeshBufferDesc mesh;mesh.vertexLayout={{VertexFieldType::Vec3}};mesh.vertexCount=3;
    mesh.vertexData=vertices.data();mesh.vertexByteSize=sizeof(vertices);
    mesh.indexData=indices.data();mesh.indexCount=3;mesh.indexByteSize=sizeof(indices);
    const auto geometry=builder.Create<VertexBufferSpec>([&](auto cb){device.async_CreateMeshBuffer(mesh,cb);});
    auto owner=builder.Finish();
    auto declaration=Material::MaterialTemplate::Create({"ShadowLeaseTest",Material::Domain::Surface,{},{}});
    Check(bool(declaration),"Create shadow lease material schema");
    Material::MaterialPassResources resources;resources.expectedTemplate=declaration;resources.pipeline=colorPipeline;
    resources.shadowPipeline=depthPipeline;resources.lifetime=owner;
    Material::MaterialInstance instance(Material::MaterialAsset::Create(declaration));
    auto material=Material::MaterialSnapshot::Create(instance,resources);Check(bool(material),"Create shadow lease material");
    RenderItem item;item.mesh=geometry;item.material=material;item.draw.indexCount=3;
    RenderFrame frame;frame.items={item};
    PipelineCamera camera;camera.position={0,2,3};camera.nearPlane=.05f;camera.farPlane=10;
    camera.view=glm::lookAt(camera.position,glm::vec3(0),glm::vec3(0,1,0));
    camera.projection=glm::perspective(glm::radians(60.f),1.f,camera.nearPlane,camera.farPlane);
    MaterialPipelineSettings settings;settings.shadows.enabled=false;settings.shadows.atlasResolution=128;
    settings.reflection.enabled=false;settings.sky.enabled=false;settings.post.bloomEnabled=settings.post.ssaoEnabled=false;
    settings.indirect.enabled=settings.probes.enabled=settings.realtimeGi.enabled=settings.lumenGi.enabled=false;
    settings.temporal.antialiasing=AntialiasingMode::None;
    settings.areaLights=Light();settings.surfaceCacheGi.areaLightShadows=true;
    settings.surfaceCacheGi.areaShadows.tileResolution=64;settings.surfaceCacheGi.areaShadows.reuseAtlasStorage=true;
    settings.surfaceCacheGi.areaShadows.reuseCasterPreparation=true;
    ProbeGlobalSdfSettings sdf;sdf.counts=glm::uvec3(2);
    auto scene=BuildProbeSdfScene(std::span<const ProbeSdfInstance>{},sdf);
    settings.surfaceCacheGi.scene=scene;settings.surfaceCacheGi.cache=BuildSurfaceCache(*scene);
    MaterialRenderPipeline pipeline(device);Check(pipeline.Initialize(),pipeline.LastError());
    Check(pipeline.Resize(32,32,128),pipeline.LastError());
    unsigned frameNumber=0;
    const auto read=[&](auto texture,unsigned width,unsigned height){return ShadowThread(device,[&]{
        if(vulkan)return device.BackendDiagnostics()->ReadTexture(texture);
        const auto* entry=device.GetResourcePool().TextureTable.Get(texture);Check(entry!=nullptr,"Retired shadow atlas was released too early");
        std::vector<float> depth(std::size_t(width)*height);
        glGetTextureImage(entry->rhi_id,0,GL_DEPTH_COMPONENT,GL_FLOAT,GLsizei(depth.size()*sizeof(float)),depth.data());
        Check(glGetError()==GL_NO_ERROR,"GL shadow atlas readback failed");
        return depth;
    });};
    const auto completedDepth=[&]{const auto debug=pipeline.DebugTextures();
        return read(debug.areaLightShadowAtlas,debug.areaLightShadowWidth,debug.areaLightShadowHeight);};
    const auto equal=[](const auto& left,const auto& right){return !left.empty()&&left.size()==right.size()&&
        std::memcmp(left.data(),right.data(),left.size()*sizeof(float))==0;};
    const auto record=[&](float x){frame.items[0].model=glm::translate(glm::mat4(1),glm::vec3(x,0,0));
        auto encoder=device.BeginFrame({++frameNumber,32,32});Check(encoder.KeepAlive(owner),"Pin shadow contract geometry");
        Check(pipeline.Record(encoder,frame,camera,settings),pipeline.LastError());return encoder;};
    const auto submit=[&](float x){auto encoder=record(x);const auto token=pipeline.RecordedFrameToken();
        ShadowSubmit(device,encoder);Check(pipeline.CompleteFrame(token),pipeline.LastError());return pipeline.Statistics();};
    const auto first=submit(0);
    Check(first.areaLightShadowsUpdated&&first.areaLightShadowAtlasAllocations==1&&first.areaLightShadowAtlasReuses==0&&
          first.areaLightShadowAtlasPoolEntries==1,"First pooled shadow update must allocate one atlas");
    const auto atlasA=pipeline.DebugTextures();const auto depthA=completedDepth();
    Check(depthA.size()==std::size_t(atlasA.areaLightShadowWidth)*atlasA.areaLightShadowHeight,"Shadow atlas readback extent mismatch");
    bool covered=false;for(float value:depthA){Check(std::isfinite(value)&&value>=0&&value<=1,"Invalid shadow depth");covered|=value<.99999f;}
    Check(covered,"Shadow lifetime fixture did not rasterize depth");
    const auto cached=submit(0);Check(cached.areaLightShadowCacheUsed&&!cached.areaLightShadowsUpdated&&
        cached.areaLightShadowAtlasAllocations==0&&cached.areaLightShadowAtlasReuses==0&&equal(depthA,completedDepth()),"Cache-hit frame changed completed depth or allocated storage");
    auto held=record(.2f);const auto heldStats=pipeline.Statistics();
    Check(heldStats.areaLightShadowAtlasAllocations==1&&heldStats.areaLightShadowAtlasReuses==0,"Completed atlas was reused while recording replacement");
    Check(held.End(false),"End unsubmitted shadow replacement");
    Check(equal(depthA,completedDepth()),"Recording an unsubmitted replacement changed completed depth");
    Check(pipeline.DiscardFrame(pipeline.RecordedFrameToken()),pipeline.LastError());
    Check(pipeline.DebugTextures().areaLightShadowAtlas==atlasA.areaLightShadowAtlas&&equal(depthA,completedDepth()),"Discarding replacement changed completed atlas");
    // Keep 'held' alive after DiscardFrame: it still owns the unsubmitted frame
    // pins. Only A and B exist, so C must allocate instead of recycling either.
    const auto third=submit(.4f);
    Check(third.areaLightShadowAtlasAllocations==1&&third.areaLightShadowAtlasReuses==0&&third.areaLightShadowAtlasPoolEntries==3,
          "Pool reused the completed or recorded-but-unsubmitted atlas lease");
    const auto depthC=completedDepth();Check(!equal(depthA,depthC),"Moved caster negative control did not change depth");
    const auto fourth=submit(.6f);Check(fourth.areaLightShadowAtlasAllocations==0&&fourth.areaLightShadowAtlasReuses==1,
          "Warm shadow update did not reuse the retired atlas");
    const auto completedBeforeResize=pipeline.DebugTextures();const auto depthBeforeResize=completedDepth();
    settings.surfaceCacheGi.areaShadows.tileResolution=128;
    auto resized=record(.8f);
    Check(pipeline.Statistics().areaLightShadowAtlasAllocations==1&&pipeline.Statistics().areaLightShadowAtlasReuses==0,
          "Different-sized shadow atlas reused incompatible storage");
    Check(resized.End(false),"End unsubmitted resized shadow frame");
    Check(pipeline.DiscardFrame(pipeline.RecordedFrameToken()),pipeline.LastError());
    Check(pipeline.DebugTextures().areaLightShadowAtlas==completedBeforeResize.areaLightShadowAtlas&&equal(depthBeforeResize,completedDepth()),
          "Discarded resize overwrote the last completed shadow depth");
    settings.surfaceCacheGi.areaShadows.tileResolution=64;
    const auto back=submit(1.f);
    Check(back.areaLightShadowAtlasAllocations==1&&back.areaLightShadowAtlasReuses==0&&back.areaLightShadowAtlasPoolEntries==4,
          "Resize pruning recycled an old-size atlas still pinned by an unsubmitted encoder");
    Check(held.Cancel()&&resized.Cancel(),"Release abandoned shadow frame pins");
    const auto released=submit(1.2f);
    Check(released.areaLightShadowAtlasAllocations==0&&released.areaLightShadowAtlasReuses==1&&released.areaLightShadowAtlasPoolEntries==3,
          "Cancelled old-size lease was not reusable, or unused incompatible storage was retained");
    const auto stable=completedDepth();settings.surfaceCacheGi.areaShadows.reuseAtlasStorage=false;
    const auto original=submit(1.2f);Check(original.areaLightShadowAtlasAllocations==1&&original.areaLightShadowAtlasReuses==0&&equal(stable,completedDepth()),
          "Disabled atlas pooling changed original allocation or depth semantics");
    // Exercise culling independently of storage reuse. The fixture's shadow
    // vertex shader only applies model, so these explicit world bounds cover
    // every emitted vertex; invalid or absent contracts must retain all draws.
    const auto unculled=completedDepth();settings.surfaceCacheGi.areaShadows.cullCasters=true;
    const auto retained=[&](const char* label){const auto stats=submit(1.2f);
        Check(stats.areaLightShadowCasterCount==1&&stats.areaLightShadowCulledCasters==0&&stats.areaLightShadowCasterDraws==96,
              std::string(label)+" incorrectly rejected an uncertain caster");
        Check(equal(unculled,completedDepth()),std::string(label)+" changed full shadow depth");};
    retained("Unknown bounds");
    frame.items[0].shadowCasterBounds=ShadowCasterBounds{{std::numeric_limits<float>::quiet_NaN(),0,-.8f},{2,0,.8f}};
    retained("NaN bounds");
    frame.items[0].shadowCasterBounds=ShadowCasterBounds{{.4f,0,-.8f},{std::numeric_limits<float>::infinity(),0,.8f}};
    retained("Infinite bounds");
    frame.items[0].shadowCasterBounds=ShadowCasterBounds{{2,0,-.8f},{.4f,0,.8f}};
    retained("Reversed bounds");
    frame.items[0].shadowCasterBounds=ShadowCasterBounds{{.3999f,-.0001f,-.8001f},{2.0001f,.0001f,.8001f}};
    const auto trusted=submit(1.2f);
    Check(trusted.areaLightShadowCulledCasters>0&&trusted.areaLightShadowCasterDraws+trusted.areaLightShadowCulledCasters==96,
          "Valid trusted bounds did not reduce shadow draws or lost face coverage");
    Check(equal(unculled,completedDepth()),"Trusted conservative culling changed full shadow depth");
    frame.items[0].layer=RenderLayer::Cutout;settings.surfaceCacheGi.areaShadows.cullCasters=false;
    submit(1.2f);Check(equal(unculled,completedDepth()),"Cutout baseline changed shadow depth");
    settings.surfaceCacheGi.areaShadows.cullCasters=true;retained("Cutout trusted bounds");
    frame.items[0].layer=RenderLayer::Opaque;frame.items[0].shadowCasterBounds.reset();
    settings.surfaceCacheGi.areaShadows.cullCasters=false;submit(0);const auto nearReference=completedDepth();
    frame.items[0].shadowCasterBounds=ShadowCasterBounds{{-.8f,0,-.8f},{.8f,0,.8f}};
    settings.surfaceCacheGi.areaShadows.cullCasters=true;const auto crossing=submit(0);
    // Every source is at y=2, x/z in [-.375,.375]. On all +/-X/Z
    // faces these bounds cross the near plane, while y=0 lies strictly
    // outside a side plane. Retain those 64 draws despite the side test;
    // keep the 16 -Y faces and reject only the 16 faces pointing upward.
    Check(crossing.areaLightShadowCasterDraws==80&&crossing.areaLightShadowCulledCasters==16,
          "Near-plane-straddling bounds were culled by an unrelated cube side plane");
    Check(equal(nearReference,completedDepth()),"Near-plane-straddling culling changed full shadow depth");
    pipeline.Shutdown();
    Check(!pipeline.DebugTextures().areaLightShadowAtlas.IsValid(),"Shutdown published a retired area shadow atlas");
    std::cout<<"Area shadow submission contracts passed: completed depth bit-exact through Record/Discard/resize, live encoder leases isolated, warm reuse, disabled-path parity; unknown/NaN/infinite/reversed/cutout retained, trusted/near-straddling culling depth-exact\n";
}
void TestShadowSubmissionContracts(bool vulkan,bool validation) {
    if(vulkan)Check(RegisterVulkanBackend(),"Vulkan backend unavailable for shadow submission contracts");
    Check(glfwInit()==GLFW_TRUE,"glfwInit shadow contracts");glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    ApplicationWindow::Window window({64,64,"Area shadow submission contracts",false,vulkan?BackendType::Vulkan:BackendType::Opengl});
    Check(window.Activate(),"Activate shadow contract window");RHIDevice device;
    if(vulkan){auto context=*window.GetRenderContextAsVulkan();context.validation=validation;context.hardwareRayTracing=false;device.Run(BackendType::Vulkan,context);}
    else device.Run(BackendType::Opengl,*window.GetRenderContextAsOpengl());
    try {ShadowAtlasContracts(device,vulkan);
        if(vulkan)Check(ShadowThread(device,[&]{return device.BackendDiagnostics()->ValidationErrorCount();})==0,"Vulkan shadow submission validation errors");
    } catch(...){device.StopAndRelease();device.returnSystem.DrainCallbacks();throw;}
    device.StopAndRelease();device.returnSystem.DrainCallbacks();
}
