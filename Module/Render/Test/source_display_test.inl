// Included inside the started-device test scope. Main includes <fstream>,
// <filesystem> and source_display_pass.h. No Model implementation dependency.
{
    PipelineDetail::ResourceBuilder displayResources(device);
    const auto displayOutput=displayResources.Texture(1,1,RHITextureFormat::RGBA32F);
    const auto displayTarget=displayResources.Target(displayOutput,{});
    const auto displayHdr=displayResources.Texture(1,1,RHITextureFormat::RGBA32F);
    const auto displayHdrTarget=displayResources.Target(displayHdr,{});
    const auto displayMesh=displayResources.FullscreenMesh();
    const auto displayPost=displayResources.Uniform(sizeof(PostProcessConstants));
    auto sourceCube=std::make_shared<DisplayColorCube>();
    {
        const auto relative=std::filesystem::path("Asset/Diana/cc-st30-100-display.lutbin");
        auto path=relative;
        if(!std::filesystem::is_regular_file(path))
            path=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path()/relative;
        std::ifstream input(path,std::ios::binary);
        Require(input.is_open(),"Missing real ST30 display LUT; run in repository or retain the source-relative Asset directory");
        std::array<char,8> magic{};std::array<std::uint32_t,4> header{};
        input.read(magic.data(),magic.size());input.read(reinterpret_cast<char*>(header.data()),sizeof(header));
        Require(bool(input)&&std::string(magic.data(),magic.size())=="DHSLUT01"&&
            header==std::array<std::uint32_t,4>{1,33,1089,4},"Real display LUT header/order changed");
        static_assert(sizeof(glm::vec4)==16);
        sourceCube->edge=33;sourceCube->texels.resize(33*33*33);
        const auto bytes=sourceCube->texels.size()*sizeof(glm::vec4);
        input.read(reinterpret_cast<char*>(sourceCube->texels.data()),std::streamsize(bytes));
        Require(bool(input)&&input.peek()==std::char_traits<char>::eof(),"Real display LUT payload length changed");
        std::uint64_t hash=14695981039346656037ull;
        const auto accumulateHash=[&](const void* data,std::size_t count){
            const auto* p=static_cast<const unsigned char*>(data);for(std::size_t i=0;i<count;++i)hash=(hash^p[i])*1099511628211ull;
        };
        accumulateHash(magic.data(),magic.size());accumulateHash(header.data(),sizeof(header));accumulateHash(sourceCube->texels.data(),bytes);
        // SHA256 efa6d8a0b0f1215f42c2593c0f0213ce13ce1ace074fd1ec7185f1360ac9d66e.
        Require(hash==0x2977b857fd57da4aull,"Real display LUT bytes no longer match independent goldens");
    }
    CreateRHITextureSpec cubeDesc;cubeDesc.width=33;cubeDesc.height=1089;cubeDesc.mipmaps=false;
    cubeDesc.textureDataStoreType=cubeDesc.textureUseType=RHITextureFormat::RGBA32F;
    cubeDesc.addressMode=RHIAddressMode::ClampToEdge;cubeDesc.data=sourceCube->texels.data();
    const auto displayCubeTexture=displayResources.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(cubeDesc,cb);});
    struct alignas(16) DisplayAudit {
        glm::vec4 input{0};
        glm::vec4 p0{1.2f,.18f,.5f,1.33f};
        glm::vec4 p1{1,.5216666666666667f,.41f,-4.222522070894527f};
        glm::vec4 p2{2.2027490136499788f,5.555555555555555f,-.036f,0};
        glm::vec4 mode{0,1,1,0};
    } displayAudit;
    static_assert(sizeof(DisplayAudit)==80);
    const auto displayAuditBuffer=displayResources.Uniform(sizeof(displayAudit));
    std::string displayFragment=R"GLSL(#version 450 core
layout(std140,binding=8) uniform DisplayAuditData {vec4 auditInput,auditP0,auditP1,auditP2,auditMode;};
layout(binding=3) uniform sampler2D auditCube;
layout(location=0) out vec4 outColor;
)GLSL";
    displayFragment+=PipelineDetail::SourceDisplayGLSL();displayFragment+=R"GLSL(
void main(){
    int mode=int(auditMode.x);vec3 value;
    if(mode==0)value=SourceDisplayThreeSection(auditInput.rgb,auditP0,auditP1,auditP2);
    else if(mode==1)value=SourceDisplayEncodeCct(auditInput.rgb);
    else if(mode==2)value=SourceDisplayDecodeCct(auditInput.rgb);
    else if(mode==3)value=SourceDisplaySampleCube(auditCube,auditInput.rgb,33);
    else if(mode==4)value=SourceDisplayApplyCube(auditCube,auditInput.rgb,33);
    else value=SourceDisplaySrgb(auditInput.rgb,auditMode.y,auditMode.z,auditMode.w);
    outColor=vec4(value,1);
}
)GLSL";
    const auto displayAuditPipeline=displayResources.FullscreenPipeline(FullscreenVertexShader(),displayFragment);
    const auto displayOwner=displayResources.Finish();
    const auto readDisplay=[&]{
        const auto image=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(displayOutput);});
        Require(image.size()==4,"Source display RGBA32F readback size");
        for(float value:image)Require(std::isfinite(value),"Nonfinite source display result");
        return glm::vec3(image[0],image[1],image[2]);
    };
    const auto checkDisplay=[&](glm::vec3 actual,glm::vec3 expected,const char* label,float absolute=3e-5f,float relative=2e-5f){
        for(int channel=0;channel<3;++channel){
            const float error=std::abs(actual[channel]-expected[channel]);
            if(error>std::max(absolute,relative*std::abs(expected[channel])))
                std::cerr<<label<<" channel="<<channel<<" actual="<<actual[channel]<<" independent="<<expected[channel]<<'\n';
            Require(error<=std::max(absolute,relative*std::abs(expected[channel])),label);
        }
    };
    const auto displayHelper=[&](int mode,glm::vec3 value){
        displayAudit.input=glm::vec4(value,1);displayAudit.mode.x=float(mode);
        auto encoder=device.BeginFrame({0,1,1});Require(encoder.KeepAlive(displayOwner),"Pin source display helper resources");
        Require(encoder.SetRenderTarget({displayTarget,1,1,RHICommand::ClearColor})&&
            encoder.UpdateUniformBufferBytes(displayAuditBuffer,&displayAudit,sizeof(displayAudit),0)&&encoder.BindUniformBuffer(displayAuditBuffer,8)&&
            encoder.BindTexture(displayCubeTexture,3)&&encoder.BindPipeline(displayAuditPipeline)&&encoder.BindMesh(displayMesh)&&encoder.DrawIndexed({3}),
            "Draw source display helper oracle");
        Submit(device,encoder);return readDisplay();
    };
    // Hard-coded independent source SSA/reference results. No CPU call into
    // SourceDisplayGLSL or the Model display helper participates in the oracle.
    checkDisplay(displayHelper(0,{0,.018f,.1799f}),{0,.007780294031960817f,.17987999998797868f},"Source three-section toe/begin-left mismatch");
    checkDisplay(displayHelper(0,{.18f,.1801f,.5215666666666667f}),{.18f,.18012f,.5898800000000001f},"Source three-section linear boundaries mismatch");
    checkDisplay(displayHelper(0,{.5216666666666667f,.5217666666666667f,4}),{.59f,.5901199824407375f,.9999844598867307f},"Source three-section shoulder boundary/tail mismatch");
    displayAudit.p2.w=1;
    checkDisplay(displayHelper(0,{.001f,.18f,4}),{.001f,.18f,4},"Source HDR three-section bypass changed values");displayAudit.p2.w=0;
    checkDisplay(displayHelper(1,{0,.001f,.0078125f}),{.072905533015728f,.08344577044248581f,.1552511379122734f},"Source ACEScct linear encoding mismatch");
    checkDisplay(displayHelper(1,{.0078124f,.0078126f,.18f}),{.15525008388853073f,.1552522062765713f,.4135884056544351f},"Source ACEScct branch-boundary encoding mismatch");
    checkDisplay(displayHelper(1,{1,4,32}),{.5547945195858333f,.668949767393336f,.8401826391045901f},"Source ACEScct logarithmic encoding mismatch");
    checkDisplay(displayHelper(2,{.155250145362854f,.155251145362854f,.155252145362854f}),
        {.007812405865819484f,.0078124993003368766f,.00781259417542728f},"Source ACEScct decoding toe threshold mismatch",2e-6f);
    checkDisplay(displayHelper(2,{1.4679952396621704f,1.4679962396621704f,1.4679972396621704f}),
        {65503.16533719102f,65504,65504},"Source ACEScct half-limit decode mismatch",.1f);
    checkDisplay(displayHelper(5,{.0031307f,.0031308000907301903f,.0031309f}),
        {.04044864423885346f,.04044993741109515f,.04045117879887772f},"Source sRGB threshold mismatch",2e-6f);
    displayAudit.mode.y=1.2f;displayAudit.mode.z=.9f;displayAudit.mode.w=.02f;
    checkDisplay(displayHelper(5,{.0031308000907301903f,.18f,.7f}),
        {.039166549459717474f,.37570178505175933f,.7650381800680124f},"Source sRGB output adjustment mismatch");
    displayAudit.mode={0,1,1,0};
    // Unequal XYZ plus fractional coordinates exercise all eight real texels,
    // x=red / y=green / z=blue and slice seams in the flattened 33x1089 table.
    checkDisplay(displayHelper(3,{.1640625f,.546875f,.7421875f}),{0,.5281829833984375f,.7636260986328125f},"Real cube eight-corner interpolation or axis order mismatch");
    checkDisplay(displayHelper(3,{.3081091332467957f,.3651867571505471f,.45565265066825855f}),
        {.2830655705812333f,.38146479701525293f,.5009862114706984f},"Real cube unequal-channel sample mismatch");
    checkDisplay(displayHelper(3,{.6118721434895846f,.47934200495804985f,.3651867571505471f}),
        {.6592621037617946f,.4986066904455499f,.38413911618373914f},"Real cube second asymmetric sample mismatch");
    checkDisplay(displayHelper(3,{-.3f,1.2f,.5f}),{0,.9736328125f,.63134765625f},"Real cube edge clamp crossed a Z slice");
    checkDisplay(displayHelper(3,{1,1,1}),{.99072265625f,.99267578125f,.9990234375f},"Real cube high corner mismatch");
    checkDisplay(displayHelper(4,{.05f,.1f,.3f}),{.03688833989442884f,.12185718449530684f,.520250486360808f},"Real cube encode/sample/decode order mismatch");
    PipelineDetail::SourceDisplayPass sourceDisplay(device);SourceDisplaySettings displaySettings;displaySettings.enabled=true;
    PostProcessConstants displayPostData;displayPostData.screen.w=0;displayPostData.output.x=1;
    const auto displayPass=[&](glm::vec3 hdr){
        auto encoder=device.BeginFrame({0,1,1});Require(encoder.KeepAlive(displayOwner),"Pin source display pass test targets");
        Require(encoder.SetRenderTarget({displayHdrTarget,1,1,RHICommand::ClearColor,glm::vec4(hdr,1)})&&
            encoder.SetRenderTarget({displayTarget,1,1,RHICommand::ClearColor})&&
            encoder.UpdateUniformBufferBytes(displayPost,&displayPostData,sizeof(displayPostData),0)&&encoder.BindUniformBuffer(displayPost,5),
            "Prepare source display pass target and post UBO");
        sourceDisplay.Record(encoder,displayHdr,{},displayPostData,displaySettings);Submit(device,encoder);return readDisplay();
    };
    displaySettings.curve2.w=1;
    const auto displayWithoutCube=displayPass({.0031307f,.0031308000907301903f,.0031309f});
    checkDisplay(displayWithoutCube,{.04044864423885346f,.04044993741109515f,.04045117879887772f},"No-cube production source display changed sRGB");
    displaySettings.displayGamma=1.2f;displaySettings.convertToLimit=.9f;displaySettings.lowerLimit=.02f;displayPostData.output.x=2;
    checkDisplay(displayPass({.001565400045365095f,.09f,.35f}),{.039166549459717474f,.37570178505175933f,.7650381800680124f},
        "Source display pass exposure/output adjustments were applied more than once");
    displaySettings.displayGamma=1;displaySettings.convertToLimit=1;displaySettings.lowerLimit=0;displayPostData.output.x=1;
    displaySettings.curve0=displayAudit.p0;displaySettings.curve1=displayAudit.p1;displaySettings.curve2=displayAudit.p2;
    checkDisplay(displayPass({.018f,.18f,1}),{.08447642187464643f,.46135611292258993f,.9541705227143469f},"Source display pass no-cube curve/OETF order mismatch");
    displaySettings.cube=sourceCube;
    checkDisplay(displayPass({.018f,.18f,1}),{0,.5038410477593556f,1},"Source display pass curve/cube/OETF order mismatch",6e-5f);
    displaySettings.curve2.w=1;
    checkDisplay(displayPass({.05f,.1f,.3f}),{.21176135555458242f,.38389141808225413f,.7485402711088636f},"Source display pass real cube channel order mismatch",6e-5f);
    checkDisplay(displayPass({0,0,0}),{.011289643897442941f,.022648320646551527f,.022711209431148693f},"Source display pass lost original cube black lift",6e-5f);
    checkDisplay(displayPass({2,.4f,.1f}),{1,.7389234160776329f,.3898708452683941f},"Source display pass cube HDR/clamped output mismatch",6e-5f);
    displaySettings.cube.reset();checkDisplay(displayPass({.0031307f,.0031308000907301903f,.0031309f}),displayWithoutCube,
        "Removing the display cube did not restore no-cube identity");
    {
        // Clear values deliberately reject Inf in RHI. Upload float texture
        // data instead, exercising shader input validation without invalid commands.
        PipelineDetail::ResourceBuilder invalidRadiance(device);
        const glm::vec4 input(std::numeric_limits<float>::infinity(),.18f,.1f,1);
        CreateRHITextureSpec spec;spec.width=spec.height=1;spec.mipmaps=false;
        spec.textureDataStoreType=spec.textureUseType=RHITextureFormat::RGBA32F;spec.data=&input;
        const auto texture=invalidRadiance.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(spec,cb);});
        const auto lifetime=invalidRadiance.Finish();auto encoder=device.BeginFrame({0,1,1});
        Require(encoder.KeepAlive(lifetime)&&encoder.KeepAlive(displayOwner)&&encoder.SetRenderTarget({displayTarget,1,1,RHICommand::ClearColor})&&
            encoder.UpdateUniformBufferBytes(displayPost,&displayPostData,sizeof(displayPostData),0)&&encoder.BindUniformBuffer(displayPost,5),"Prepare nonfinite display texture fixture");
        sourceDisplay.Record(encoder,texture,{},displayPostData,displaySettings);Submit(device,encoder);
        checkDisplay(readDisplay(),{1,1,1},"Source display nonfinite input guard failed");
    }
    sourceDisplay.Reset();
    std::cout<<"Source display: original three-section boundaries/HDR bypass, ACEScct encode/decode/half limit, sRGB threshold/output adjustment, real 33^3 XYZ eight-texel cube, no-cube and full Render/RHI pass order passed\n";
}
