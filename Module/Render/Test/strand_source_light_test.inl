// Included inside strand_render_gpu_test.cpp's started-device scope.
// The first fixture calls the actual source-light sampler and checks a small
// set of hand-evaluated cases. The second exercises the production closure;
// radiance ratios do not depend on a copied implementation of that closure.
{
    using namespace PipelineDetail;
    ResourceBuilder sourceProbeBuilder(device);
    const auto sourceProbeColor=sourceProbeBuilder.Texture(1,1,RHITextureFormat::RGBA32F);
    const auto sourceProbeTarget=sourceProbeBuilder.Target(sourceProbeColor,{});
    const auto sourceProbeConstants=sourceProbeBuilder.Uniform(sizeof(StrandSourceDirectConstants));
    const auto sourceProbePosition=sourceProbeBuilder.Uniform(sizeof(glm::vec4));
    const auto sourceProbeMesh=sourceProbeBuilder.FullscreenMesh();
    const auto sourceProbePipeline=sourceProbeBuilder.FullscreenPipeline(FullscreenVertexShader(),
        std::string("#version 450 core\n")+StrandSourceDirectGLSL()+R"GLSL(
layout(std140,binding=0) uniform ProbePosition {vec4 probePosition;};
layout(location=0) out vec4 outColor;
void main(){vec3 L,incident;bool sampleAccepted=StrandSourceDirectSample(int(probePosition.w+.5),probePosition.xyz,L,incident);
    outColor=vec4(incident,sampleAccepted?1.0:0.0);}
)GLSL");
    const std::array<glm::vec4,2> sourceProbeIesValues{glm::vec4(.25f),glm::vec4(.75f)};
    CreateRHITextureSpec sourceProbeIesDesc;sourceProbeIesDesc.width=1;sourceProbeIesDesc.height=2;
    sourceProbeIesDesc.mipmaps=false;sourceProbeIesDesc.data=sourceProbeIesValues.data();
    sourceProbeIesDesc.textureDataStoreType=sourceProbeIesDesc.textureUseType=RHITextureFormat::RGBA32F;
    sourceProbeIesDesc.filterMode=RHIFilterMode::Nearest;sourceProbeIesDesc.addressMode=RHIAddressMode::ClampToEdge;
    const auto sourceProbeIes=sourceProbeBuilder.Create<RHITextureSpec>([&](auto callback){device.async_CreateTexture(sourceProbeIesDesc,callback);});
    const auto sourceProbeOwner=sourceProbeBuilder.Finish();
    StrandSourceDirectSettings sourceProbeSettings;sourceProbeSettings.enabled=true;sourceProbeSettings.count=1;
    auto& sourceProbeLight=sourceProbeSettings.lights[0];
    sourceProbeLight.positionRange={0,0,2,4};sourceProbeLight.directionFalloff={0,0,-1,1};
    sourceProbeLight.attenuation={0,0,1,0};sourceProbeLight.colorTolerance={.8f,.4f,.2f,StrandPackSourceHalfPair(0,0)};
    sourceProbeLight.extended={StrandPackSourceHalfPair(2,1),0,0,0};
    const auto sourceProbeOriginal=sourceProbeSettings;
    unsigned sourceProbeDraws=0,sourceProbeIndex=0;
    const auto sourceProbeDraw=[&](glm::vec3 position=glm::vec3(0)){
        const auto constants=MakeStrandSourceDirectConstants(sourceProbeSettings);
        auto encoder=device.BeginFrame({0,1,1});Require(encoder.KeepAlive(sourceProbeOwner),"Pin source attenuation probe");
        RHICommand::SetRenderTarget clear;clear.target=sourceProbeTarget;clear.width=clear.height=1;
        clear.clearFlags=RHICommand::ClearColor;clear.clearColor=glm::vec4(0);
        Require(encoder.SetRenderTarget(clear)&&encoder.BindPipeline(sourceProbePipeline)&&encoder.BindMesh(sourceProbeMesh),
            "Bind source attenuation probe");
        // Frame uniform uploads have a bounded inline payload. Preserve the
        // full 64-light std140 allocation instead of truncating the probe UBO.
        const auto bytes=reinterpret_cast<const std::byte*>(&constants);
        for(std::size_t offset=0;offset<sizeof(constants);offset+=256)
            Require(encoder.UpdateUniformBufferBytes(sourceProbeConstants,bytes+offset,
                static_cast<std::uint32_t>(std::min<std::size_t>(256,sizeof(constants)-offset)),static_cast<std::uint32_t>(offset)),
                "Upload complete source-light std140 constants");
        const glm::vec4 p(position,float(sourceProbeIndex));
        Require(encoder.UpdateUniformBufferBytes(sourceProbePosition,&p,sizeof(p),0)&&
            encoder.BindUniformBuffer(sourceProbeConstants,StrandSourceDirectBinding)&&encoder.BindUniformBuffer(sourceProbePosition,0)&&
            encoder.BindTexture(sourceProbeIes,StrandSourceIesTextureSlot)&&encoder.DrawIndexed({3}),"Draw source attenuation probe");
        Submit(device,encoder);++sourceProbeDraws;
        const auto values=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(sourceProbeColor);});
        Require(values.size()==4,"Source attenuation probe readback size");
        for(float v:values)Require(std::isfinite(v),"Source attenuation probe is nonfinite");
        return glm::vec4(values[0],values[1],values[2],values[3]);
    };
    const auto sourceProbeExpected=[&](glm::vec4 actual,glm::vec3 rgb,bool active,const char* message){
        if(glm::any(glm::greaterThan(glm::abs(glm::vec3(actual)-rgb),glm::vec3(3e-6f))))
            std::cerr<<message<<" actual="<<actual.x<<','<<actual.y<<','<<actual.z<<" expected="<<rgb.x<<','<<rgb.y<<','<<rgb.z<<'\n';
        Require(glm::all(glm::lessThanEqual(glm::abs(glm::vec3(actual)-rgb),glm::vec3(3e-6f)))&&actual.w==(active?1.f:0.f),message);
    };
    const glm::vec3 sourceProbeRgb(.8f,.4f,.2f);
    sourceProbeExpected(sourceProbeDraw(),sourceProbeRgb*(9.f/16.f),true,
        "Source R=4,r=2 exponent2 fade is not the hand-known 9/16 incident factor");
    sourceProbeLight.positionRange.z=1;
    sourceProbeExpected(sourceProbeDraw(),sourceProbeRgb*(225.f/256.f),true,"Source range fade at quarter radius is not225/256");
    sourceProbeLight.positionRange.z=4;
    sourceProbeExpected(sourceProbeDraw(),glm::vec3(0),false,"Source range boundary leaked incident radiance");
    sourceProbeLight.positionRange.z=4.25f;
    sourceProbeExpected(sourceProbeDraw(),glm::vec3(0),false,"Source range rejection leaked a light outside its range");
    sourceProbeSettings=sourceProbeOriginal;
    sourceProbeLight.extended.x=StrandPackSourceHalfPair(1,1);
    sourceProbeExpected(sourceProbeDraw(),sourceProbeRgb*.25f,true,"Packed source falloff exponent1 was not consumed");
    sourceProbeSettings=sourceProbeOriginal;
    // Raw native half words, not the production pair-packing helper:
    // c1=.5(0x3800),c2=.25(0x3400), exponent2(0x4000),c0=.75(0x3a00).
    // At r2: denominator=.75+1+1=11/4; factor=(9/16)/(11/4)=9/44.
    sourceProbeLight.colorTolerance.w=std::bit_cast<float>(std::uint32_t(0x34003800));
    sourceProbeLight.extended.x=std::bit_cast<float>(std::uint32_t(0x3a004000));
    sourceProbeExpected(sourceProbeDraw(),sourceProbeRgb*(9.f/44.f),true,
        "Native half coefficient order, UBO112 stride or polynomial attenuation differs from9/44");
    sourceProbeSettings=sourceProbeOriginal;
    sourceProbeLight.colorTolerance.w=StrandPackSourceHalfPair(0,1);
    sourceProbeLight.extended={StrandPackSourceHalfPair(2,2),2,0,0};
    sourceProbeLight.positionRange.z=.5f;
    sourceProbeExpected(sourceProbeDraw(),sourceProbeRgb*.5f,true,"Source start-distance near plateau was not constant");
    sourceProbeLight.positionRange.z=1.5f;
    sourceProbeExpected(sourceProbeDraw(),sourceProbeRgb*.5f,true,"Source start distance incorrectly changed the inner polynomial/fade");
    sourceProbeLight.positionRange.z=3;
    sourceProbeExpected(sourceProbeDraw(),sourceProbeRgb*(3.f/16.f),true,"Source start-distance shifted coordinate did not produce3/16");
    sourceProbeSettings=sourceProbeOriginal;
    sourceProbeLight.attenuation.z=0;sourceProbeLight.attenuation.w=1;
    const std::array<float,3> sourceConeCosines{.25f,.5f,.75f};
    const std::array<float,3> sourceConeResponse{5.f/32.f,.5f,27.f/32.f};
    for(unsigned i=0;i<3;++i){
        const float cosine=sourceConeCosines[i];sourceProbeLight.directionFalloff={-std::sqrt(1-cosine*cosine),0,-cosine,1};
        sourceProbeExpected(sourceProbeDraw(),sourceProbeRgb*((9.f/16.f)*sourceConeResponse[i]),true,
            "Source cone did not follow the independent quarter/half/three-quarter cubic values");
    }
    sourceProbeLight.directionFalloff={0,0,1,1};
    sourceProbeExpected(sourceProbeDraw(),glm::vec3(0),false,"Source back-facing cone leaked radiance");
    sourceProbeLight.directionFalloff={0,0,-1,2};sourceProbeLight.attenuation={0,0,1,1};
    sourceProbeExpected(sourceProbeDraw(),sourceProbeRgb*(9.f/32.f),true,
        "Source cone omitted the inner clamp before division by falloff2");
    sourceProbeSettings=sourceProbeOriginal;
    sourceProbeLight.positionRange={1,-2,5,4};
    const auto sourceUnits=sourceProbeDraw({1,-2,3});
    sourceProbeLight.positionRange={100,-200,500,4};sourceProbeSettings.distanceToSourceMetres=.01f;
    sourceProbeExpected(sourceProbeDraw({100,-200,300}),glm::vec3(sourceUnits),true,
        "Source attenuation changed under a100x host unit conversion with inverse physical-distance scale");
    sourceProbeSettings=sourceProbeOriginal;
    sourceProbeLight.unusedIntensityScale={3,5,7,11};
    sourceProbeExpected(sourceProbeDraw(),sourceProbeRgb*(9.f/16.f),true,
        "Already-converted native RGB was incorrectly scaled by the unused producer intensity field");
    auto sourceIesTable=std::make_shared<StrandTextureTable>();sourceIesTable->width=1;sourceIesTable->height=2;
    sourceIesTable->texels.assign(sourceProbeIesValues.begin(),sourceProbeIesValues.end());
    sourceProbeSettings.iesTable=sourceIesTable;sourceProbeLight.shadowIes.y=0;
    sourceProbeExpected(sourceProbeDraw(),sourceProbeRgb*(9.f/64.f),true,"Source IES row0 did not multiply by its quarter response");
    sourceProbeLight.shadowIes.y=1;
    sourceProbeExpected(sourceProbeDraw(),sourceProbeRgb*(27.f/64.f),true,"Source IES row1 did not multiply by its three-quarter response");
    sourceProbeSettings.iesTable.reset();
    sourceProbeExpected(sourceProbeDraw(),sourceProbeRgb*(9.f/16.f),true,"Declared unit-IES study still sampled a bound IES texture");
    sourceProbeSettings=sourceProbeOriginal;sourceProbeSettings.count=2;
    sourceProbeSettings.lights[1]=sourceProbeOriginal.lights[0];sourceProbeSettings.lights[1].positionRange.z=1;
    sourceProbeSettings.lights[1].colorTolerance={0,.3f,.7f,StrandPackSourceHalfPair(0,0)};sourceProbeIndex=1;
    sourceProbeExpected(sourceProbeDraw(),glm::vec3(0,.3f,.7f)*(225.f/256.f),true,
        "Source second-light UBO array stride is not the native112 bytes");
    sourceProbeSettings.count=MaxStrandSourceLights;
    for(auto& light:sourceProbeSettings.lights)light=sourceProbeOriginal.lights[0];
    sourceProbeSettings.lights.back().colorTolerance={.2f,.4f,.8f,StrandPackSourceHalfPair(0,0)};
    sourceProbeIndex=MaxStrandSourceLights-1;
    sourceProbeExpected(sourceProbeDraw(),glm::vec3(.2f,.4f,.8f)*(9.f/16.f),true,
        "Source final-light coefficients were truncated or indexed with the wrong std140 stride");
    std::cout<<"Source light sampling: "<<sourceProbeDraws<<" hand-evaluated range/start/half/cone/unit/IES cases passed\n";
}
{
    using namespace PipelineDetail;
    ResourceBuilder sourceRenderBuilder(device);
    const auto sourceRenderColor=sourceRenderBuilder.Texture(Size,Size,RHITextureFormat::RGBA32F);
    const auto sourceRenderDepth=sourceRenderBuilder.Texture(Size,Size,RHITextureFormat::Depth32F);
    const auto sourceRenderOpaque=sourceRenderBuilder.Texture(Size,Size,RHITextureFormat::Depth32F);
    const auto sourceRenderTarget=sourceRenderBuilder.Target(sourceRenderColor,sourceRenderDepth);
    const auto sourceRenderOpaqueTarget=sourceRenderBuilder.Target({},sourceRenderOpaque);
    const auto sourceRenderOwner=sourceRenderBuilder.Finish();
    StrandRenderPass sourceRenderPass(device);StrandPassView sourceRenderView;
    sourceRenderView.cameraPosition={0,0,1000000};sourceRenderView.opaqueDepth=sourceRenderOpaque;
    sourceRenderView.outputStraightColor=true;sourceRenderView.frontLayerTransparency=true;
    sourceRenderView.frontDepthGroups=3;sourceRenderView.pairedLayerTransparency=true;
    sourceRenderView.lighting.lightColors.fill(glm::vec4(0));sourceRenderView.lighting.ambientAndExposure=glm::vec4(0);
    sourceRenderView.debugMode=StrandDebugMode::Direct;
    // A far punctual light has effectively constant L across this finite
    // fixture, so fragment vs source endpoint evaluations differ only by the
    // declared source mini-float precision, not by a different lighting rig.
    sourceRenderView.sourceDirect.enabled=true;sourceRenderView.sourceDirect.count=1;
    auto& sourceRenderLight=sourceRenderView.sourceDirect.lights[0];
    sourceRenderLight.positionRange={0,0,10000,20000};sourceRenderLight.directionFalloff={0,0,-1,1};
    sourceRenderLight.attenuation={0,0,1,0};sourceRenderLight.colorTolerance={.8f,.4f,.2f,StrandPackSourceHalfPair(0,0)};
    sourceRenderLight.extended={StrandPackSourceHalfPair(2,1),0,0,0};
    const auto sourceRenderOriginal=sourceRenderView.sourceDirect;
    StrandMesh sourceRenderMesh(device);
    const std::array<StrandVertex,2> sourceRenderPoints{{{{0,-.7f,0,.08f},{0,1,0,0}},{{0,.7f,0,.08f},{0,1,0,1}}}};
    const std::array<std::uint32_t,2> sourceRenderLine{0,1};
    Require(sourceRenderMesh.Update(sourceRenderPoints,sourceRenderLine),sourceRenderMesh.LastError());
    StrandRenderItem sourceRenderItem;sourceRenderItem.geometry=sourceRenderMesh.Snapshot();
    sourceRenderItem.material.opacity=.65f;sourceRenderItem.material.physicalScattering=true;
    sourceRenderItem.material.sourceDirectScattering=true;sourceRenderItem.material.useNativeMomentLut=false;
    sourceRenderItem.material.rootColor=sourceRenderItem.material.tipColor={.31f,.12f,.025f};
    unsigned sourceRenderDraws=0;
    const auto sourceRenderDraw=[&]{
        auto encoder=device.BeginFrame({0,Size,Size});Require(encoder.KeepAlive(sourceRenderOwner),"Pin source-light integration scene");
        RHICommand::SetRenderTarget clear;clear.target=sourceRenderOpaqueTarget;clear.width=clear.height=Size;
        clear.clearFlags=RHICommand::ClearDepth;clear.clearDepth=1;
        Require(encoder.SetRenderTarget(clear),"Clear source-light independent opaque depth");
        clear.target=sourceRenderTarget;clear.clearFlags=RHICommand::ClearColor|RHICommand::ClearDepth;clear.clearColor=glm::vec4(0);
        Require(encoder.SetRenderTarget(clear),"Clear source-light integration scene");
        sourceRenderPass.Record(encoder,std::span(&sourceRenderItem,1),{sourceRenderTarget,sourceRenderColor,sourceRenderDepth,Size,Size,1},sourceRenderView);
        Submit(device,encoder);++sourceRenderDraws;
        const auto image=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(sourceRenderColor);});
        Require(image.size()==Size*Size*4,"Source-light integration readback size");
        for(float v:image)Require(std::isfinite(v),"Source-light integration produced a nonfinite component");
        return image;
    };
    const auto sourceRenderRatio=[&](const std::vector<float>& reference,const std::vector<float>& actual,float ratio,float relative,const char* message){
        unsigned covered=0;
        for(std::size_t p=0;p<reference.size();p+=4){
            Require(std::abs(reference[p+3]-actual[p+3])<1e-5f,"Source light input changed hair opacity/coverage");
            if(reference[p+3]<.4f)continue;++covered;
            for(unsigned c=0;c<3;++c){const float expected=reference[p+c]*ratio;
                Require(std::abs(actual[p+c]-expected)<=.0001f+relative*std::max(std::abs(actual[p+c]),std::abs(expected)),message);}
        }
        Require(covered>100,"Source-light integration ratio fixture has no covered pixels");
    };
    std::array<std::vector<float>,2> sourceRenderBaselines;
    for(unsigned segment=0;segment<2;++segment){
        sourceRenderItem.material.sourceSegmentShading=segment!=0;
        sourceRenderView.sourceDirect=sourceRenderOriginal;
        const auto baseline=sourceRenderDraw();sourceRenderBaselines[segment]=baseline;
        const auto center=(std::size_t(Size/2)*Size+Size/2)*4;
        Require(baseline[center]>.0005f,"Source light branch integration fixture has no direct illumination");
        sourceRenderLight.extended.x=StrandPackSourceHalfPair(2,2);
        sourceRenderRatio(baseline,sourceRenderDraw(),.5f,.003f,"c0 doubling did not halve the production source direct closure");
        sourceRenderView.sourceDirect=sourceRenderOriginal;
        sourceRenderLight.attenuation={0,0,0,.5f};
        sourceRenderRatio(baseline,sourceRenderDraw(),.5f,.003f,"Half-angle cone did not halve fragment/segment direct radiance");
        sourceRenderView.sourceDirect=sourceRenderOriginal;
        sourceRenderLight.extended.y=15000;
        sourceRenderRatio(baseline,sourceRenderDraw(),16.f/9.f,.04f,"Source inner start-distance plateau disagrees between fragment/segment closures");
        sourceRenderView.sourceDirect=sourceRenderOriginal;
        sourceRenderLight.positionRange.w=9000;
        sourceRenderRatio(baseline,sourceRenderDraw(),0.f,0.f,"Source range rejection was not shared by fragment and segment closures");
        sourceRenderView.sourceDirect=sourceRenderOriginal;
        sourceRenderLight.unusedIntensityScale={3,5,7,11};
        Require(MaximumDifference(baseline,sourceRenderDraw())==0,"Source production closure applied the unused photometric intensity field twice");
        sourceRenderView.sourceDirect=sourceRenderOriginal;
        sourceRenderItem.model=glm::scale(glm::mat4(1),glm::vec3(100));
        sourceRenderView.viewProjection=glm::scale(glm::mat4(1),glm::vec3(.01f));
        sourceRenderView.cameraPosition={0,0,100000000};sourceRenderLight.positionRange.z=1000000;
        sourceRenderView.sourceDirect.distanceToSourceMetres=.01f;
        sourceRenderRatio(baseline,sourceRenderDraw(),1.f,.04f,"Production source lighting lost invariance under100x host-unit normalization");
        sourceRenderItem.model=glm::mat4(1);sourceRenderView.viewProjection=glm::mat4(1);
        sourceRenderView.cameraPosition={0,0,1000000};sourceRenderView.sourceDirect=sourceRenderOriginal;
        // Exercise a nonzero old rig so 'disabled identity' cannot pass by
        // comparing two black outputs. Source mode replaces the punctual loop.
        sourceRenderView.sourceDirect.enabled=false;sourceRenderView.debugMode=StrandDebugMode::Beauty;
        sourceRenderView.lighting.lightPositions[0]={.2f,.1f,2,1};sourceRenderView.lighting.lightColors[0]={.4f,.3f,.2f,.01f};
        sourceRenderView.lighting.ambientAndExposure={.03f,.02f,.01f,0};
        const auto disabled=sourceRenderDraw();sourceRenderView.sourceDirect.enabled=true;
        const auto enabled=sourceRenderDraw();
        Require(MaximumDifference(disabled,enabled)>.001f,"Source enable/disable branch identity fixture is vacuous");
        sourceRenderView.sourceDirect.enabled=false;
        Require(MaximumDifference(disabled,sourceRenderDraw())==0,"Disabled source direct input did not exactly restore the original Beauty pixels");
        sourceRenderView.lighting.lightColors.fill(glm::vec4(0));sourceRenderView.lighting.ambientAndExposure=glm::vec4(0);
        sourceRenderView.debugMode=StrandDebugMode::Direct;
    }
    sourceRenderRatio(sourceRenderBaselines[0],sourceRenderBaselines[1],1.f,.04f,
        "Fragment/segment source-direct inputs differ beyond source endpoint mini-float precision in a constant-L fixture");
    {
        // Source calcShading deliberately splits positions: V uses START,
        // whereas local radiance/direction/range and DS rays use END. Two
        // non-overlapping colored lights distinguish the illumination point
        // without evaluating any source attenuation formula in this test.
        const glm::vec3 end(.015625f,.515625f,.35f),start=end-glm::vec3(0,.9f,.5f);
        const glm::vec3 tangent=glm::normalize(end-start),camera(1,-.3f,.7f);
        const std::array<StrandVertex,2> points{{{glm::vec4(start,.08f),glm::vec4(tangent,0)},
            {glm::vec4(end,.08f),glm::vec4(tangent,1)}}};
        StrandMesh splitMesh(device);Require(splitMesh.Update(points,sourceRenderLine),splitMesh.LastError());
        sourceRenderItem.geometry=splitMesh.Snapshot();sourceRenderItem.material.sourceSegmentShading=true;
        sourceRenderView.sourceDirect=sourceRenderOriginal;sourceRenderView.sourceDirect.count=2;
        sourceRenderView.cameraPosition=camera;
        auto& red=sourceRenderView.sourceDirect.lights[0];red.positionRange=glm::vec4(end+glm::vec3(.12f,0,.18f),.3f);
        red.colorTolerance={.8f,0,0,StrandPackSourceHalfPair(0,0)};
        auto& blue=sourceRenderView.sourceDirect.lights[1];blue=red;
        blue.positionRange=glm::vec4(start+glm::vec3(.12f,0,.18f),.3f);blue.colorTolerance={0,0,.8f,StrandPackSourceHalfPair(0,0)};
        sourceRenderDraw();
        const auto atlas=sourceRenderPass.SegmentShadingDebugTextures();
        const auto shades=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(atlas.color);});
        Require(shades.size()>=8&&shades[7]==1.f,"Split P oracle has no initialized END shade");
        const glm::vec3 actual(shades[4],shades[5],shades[6]);
        Require(actual.r>1e-5f&&actual.g==0.f&&actual.b==0.f,
            "Source illumination used START: its blue-only range was sampled instead of the red-only END range");
        // END is at an exact pixel center. A tiny constant-depth fragment
        // fixture supplies the same known T at that position. Its reference
        // camera C+(END-START) gives (referenceCamera-END)==(C-START).
        // This is an independent BRDF-input oracle, not a production camera
        // adjustment, and the fixed test projection keeps the sampled P exact.
        const std::array<StrandVertex,2> local{{
            {glm::vec4(end-glm::vec3(0,.04f,0),.08f),glm::vec4(tangent,0)},
            {glm::vec4(end+glm::vec3(0,.04f,0),.08f),glm::vec4(tangent,1)}}};
        StrandMesh localMesh(device);Require(localMesh.Update(local,sourceRenderLine),localMesh.LastError());
        sourceRenderItem.geometry=localMesh.Snapshot();sourceRenderItem.material.sourceSegmentShading=false;
        sourceRenderView.cameraPosition=camera+(end-start);
        const auto referenceImage=sourceRenderDraw();const auto pixel=std::size_t(48*Size+32)*4;
        const glm::vec3 expected(referenceImage[pixel],referenceImage[pixel+1],referenceImage[pixel+2]);
        Require(referenceImage[pixel+3]>.6f&&glm::length(expected)>1e-4f,"Split view-origin reference is not covered/lit");
        Require(glm::length(actual-expected)<=.04f*glm::length(expected)+1e-5f,
            "Source view direction was not computed from START independently of END illumination");
        sourceRenderView.cameraPosition=camera;
        const auto wrongImage=sourceRenderDraw();
        const glm::vec3 wrong(wrongImage[pixel],wrongImage[pixel+1],wrongImage[pixel+2]);
        Require(glm::length(actual-wrong)>3.f*glm::length(actual-expected)+.0001f,
            "Split view-origin negative control cannot distinguish END-view from the required START-view direction");
    }
    sourceRenderPass.Shutdown();
    std::cout<<"Source direct production input: "<<sourceRenderDraws<<" fragment/segment draws; coefficient/cone/start/range/unit ratios and disabled Beauty identity passed\n";
}
