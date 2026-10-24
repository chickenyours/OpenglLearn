// Include inside strand_render_gpu_test.cpp's started-device scope. This
// independent fixture reads linear float targets, before exposure/display/TAA.
// It tests component identities rather than scoring a chosen visual tint.
{
    using namespace PipelineDetail;
    ResourceBuilder debugBuilder(device);
    const auto debugColor=debugBuilder.Texture(Size,Size,RHITextureFormat::RGBA32F);
    const auto debugDepth=debugBuilder.Texture(Size,Size,RHITextureFormat::Depth32F);
    const auto debugOpaque=debugBuilder.Texture(Size,Size,RHITextureFormat::Depth32F);
    const auto debugTarget=debugBuilder.Target(debugColor,debugDepth);
    const auto debugOpaqueTarget=debugBuilder.Target({},debugOpaque);
    const auto debugOwner=debugBuilder.Finish();
    StrandRenderPass debugPass(device);
    StrandPassView debugView;debugView.cameraPosition={0,0,2};
    debugView.frontLayerTransparency=true;debugView.frontDepthGroups=3;
    debugView.pairedLayerTransparency=true;debugView.opaqueDepth=debugOpaque;
    debugView.outputStraightColor=true;
    debugView.lighting.lightColors.fill(glm::vec4(0));
    debugView.lighting.ambientAndExposure={.04f,.05f,.06f,0};
    debugView.lighting.lightPositions[0]={-.6f,.4f,1.5f,1};
    debugView.lighting.lightColors[0]={.15f,.1f,.05f,0};
    debugView.sunDirection={-.7f,.15f,-1};debugView.sunColor={1.f,.8f,.6f};debugView.sunIntensity=.1f;
    debugView.indirect.enabled=true;debugView.indirect.coefficients.fill(glm::vec3(.03f,.025f,.02f));
    StrandMesh debugMesh(device);
    const std::array<StrandVertex,2> debugPoints{{{{0,-.8f,0,.1f},{0,1,0,0}},{{0,.8f,0,.1f},{0,1,0,1}}}};
    const std::array<std::uint32_t,2> debugLine{0,1};
    const std::array<StrandColorCoordinate,2> debugCoordinates{{{{.42f,.25f}},{{.42f,.25f}}}};
    Require(debugMesh.Update(debugPoints,debugLine,debugCoordinates),debugMesh.LastError());
    auto debugGradient=std::make_shared<StrandTextureTable>();debugGradient->width=1;debugGradient->height=3;
    const glm::vec3 debugBase(.31f,.12f,.025f);
    debugGradient->texels.assign(3,glm::vec4(debugBase,1));
    StrandRenderItem debugItem;debugItem.geometry=debugMesh.Snapshot();
    debugItem.material.rootColor=debugItem.material.tipColor={.18f,.06f,.009f};
    debugItem.material.sourceColor=true;debugItem.material.gradientTable=debugGradient;
    debugItem.material.opacity=.7f;debugItem.material.physicalScattering=true;
    debugItem.material.sourceDirectScattering=true;debugItem.material.useNativeMomentLut=false;
    debugItem.material.dualScatteringIbl=true;debugItem.material.volumeSkyTransmission=true;
    debugItem.material.multipleScattering=.65f;debugItem.material.backscatterScale=.7f;
    const auto debugDraw=[&](StrandDebugMode mode) {
        debugView.debugMode=mode;
        auto encoder=device.BeginFrame({0,Size,Size});Require(encoder.KeepAlive(debugOwner),"Pin strand diagnostic fixture");
        RHICommand::SetRenderTarget clear;clear.target=debugOpaqueTarget;clear.width=clear.height=Size;
        clear.clearFlags=RHICommand::ClearDepth;clear.clearDepth=1;
        Require(encoder.SetRenderTarget(clear),"Clear independent diagnostic opaque depth");
        clear.target=debugTarget;clear.clearFlags=RHICommand::ClearColor|RHICommand::ClearDepth;clear.clearColor=glm::vec4(0);
        Require(encoder.SetRenderTarget(clear),"Clear diagnostic float scene");
        const std::array<StrandRenderItem,1> entries{debugItem};
        debugPass.Record(encoder,entries,{debugTarget,debugColor,debugDepth,Size,Size,1},debugView);
        Submit(device,encoder);
        auto values=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(debugColor);});
        Require(values.size()==Size*Size*4,"Strand diagnostic readback dimensions");
        for(float value:values)Require(std::isfinite(value),"Strand diagnostic produced NaN or infinity");
        return values;
    };
    const auto debugPixel=[](const std::vector<float>& values){const auto p=((Size/2)*Size+Size/2)*4;return glm::vec3(values[p],values[p+1],values[p+2]);};
    const auto debugClose=[&](glm::vec3 actual,glm::vec3 expected,float tolerance,const char* message){
        if(glm::any(glm::greaterThan(glm::abs(actual-expected),glm::vec3(tolerance))))
            std::cerr<<message<<" actual="<<actual.x<<','<<actual.y<<','<<actual.z<<" expected="<<expected.x<<','<<expected.y<<','<<expected.z<<'\n';
        Require(glm::all(glm::lessThanEqual(glm::abs(actual-expected),glm::vec3(tolerance))),message);
    };
    std::array<std::vector<float>,15> debugImages;
    std::array<glm::vec3,15> debugValues;
    for(std::uint32_t mode=0;mode<15;++mode) {
        debugImages[mode]=debugDraw(static_cast<StrandDebugMode>(mode));debugValues[mode]=debugPixel(debugImages[mode]);
        for(std::size_t alpha=3;alpha<debugImages[0].size();alpha+=4)
            Require(std::abs(debugImages[mode][alpha]-debugImages[0][alpha])<.000001f,
                "A strand diagnostic changed physical coverage or transparency");
    }
    // FP16 front-color storage may quantize each component independently.
    // These are sums of scene-linear isolated-hair colors, not displayed PNGs.
    debugClose(debugValues[0],debugValues[2]+debugValues[3],.0015f,"Beauty != direct + indirect diagnostic");
    debugClose(debugValues[2],debugValues[4]+debugValues[5],.0015f,"Source direct != ballistic + multiple residual");
    debugClose(debugValues[3],debugValues[6]+debugValues[7],.0015f,"Source IBL != ballistic + multiple residual");
    debugClose(debugValues[4]+debugValues[6],debugValues[8]+debugValues[9]+debugValues[10],.0015f,
        "Visible source ballistic != R + TT + TRT diagnostics");
    Require(glm::length(debugValues[2])>1e-5f&&glm::length(debugValues[3])>1e-5f&&
        glm::length(debugValues[5]+debugValues[7])>1e-5f,"Diagnostic decomposition fixture is vacuous");
    debugClose(debugValues[1],debugBase,.0005f,"Base diagnostic did not consume the actual source gradient/HSV color");
    debugClose(debugValues[11],glm::vec3(0),.000001f,"Disabled volume has a fabricated sun optical path");
    debugClose(debugValues[12],glm::vec3(0),.000001f,"Disabled volume has fabricated IBL optical paths");
    debugClose(debugValues[13],glm::vec3(1),.000001f,"Disabled blockers are not unoccluded in the diagnostic");
    // With a constant ambient cube the actual producer response is known:
    // every unit direction returns the same coefficient. Apply the existing
    // authored albedo blend, without adding a fake neutral environment.
    const glm::vec3 debugIncident(.07f,.075f,.08f);
    const auto debugIncidentLuma=glm::dot(debugIncident,glm::vec3(.2126f,.7152f,.0722f));
    const auto debugIncidentExpected=glm::mix(debugIncident,glm::vec3(debugIncidentLuma)*debugBase,debugItem.material.albedoBlendRate)*debugItem.material.indirectLightingMultiplier;
    debugClose(debugValues[14],debugIncidentExpected,.0001f,"Incident diagnostic differs from the consumed source ambient cube/albedo adapter");
    Require(MaximumDifference(debugImages[0],debugDraw(StrandDebugMode::Beauty))<.000001f,
        "Switching diagnostics contaminated restored Beauty");
    debugItem.material.volumePointIbl=true;
    Require(MaximumDifference(debugImages[0],debugDraw(StrandDebugMode::Beauty))<.000001f,
        "Per-point IBL changed Beauty with no active volume");
    debugItem.material.volumePointIbl=false;
    {
        // Exercise all four COLOR feature variants through the production
        // StrandRenderPass, including an actually allocated spatial field.
        // The earlier volume-off identity never reaches point variants 2/3.
        const auto savedMaterial=debugItem.material;
        const bool savedTemporal=debugView.outputTemporalData;
        debugItem.material.spatialVolume=true;
        debugItem.material.volumeResolution=8;
        debugView.outputTemporalData=true;
        const auto voxelBeauty=debugDraw(StrandDebugMode::Beauty);
        const auto volumeHandles=debugPass.VolumeDebugTextures();
        const auto targetHandles=debugPass.TemporalTextures();
        Require(volumeHandles.density.IsValid()&&volumeHandles.pathsAmbient.IsValid()&&
            targetHandles.nearestDepth.IsValid()&&targetHandles.coverage.IsValid()&&targetHandles.compositeDepth.IsValid(),
            "Color feature-variant fixture did not allocate a real volume and private targets");
        Require(debugPass.VolumeStatistics().updated==1&&debugPass.VolumeStatistics().passes>0,
            "Active color feature-variant volume fixture is vacuous");
        const auto stable=[&](const std::vector<float>& values){
            const auto volume=debugPass.VolumeDebugTextures();const auto targets=debugPass.TemporalTextures();
            Require(debugPass.VolumeStatistics().passes==0&&debugPass.VolumeStatistics().updated==0,
                "Switching color shader features rebuilt the unchanged density/path field");
            Require(volume.density==volumeHandles.density&&volume.paths==volumeHandles.paths&&
                volume.pathsExtra==volumeHandles.pathsExtra&&volume.pathsAmbient==volumeHandles.pathsAmbient,
                "Switching color shader features replaced cached volume texture handles");
            Require(targets.nearestDepth==targetHandles.nearestDepth&&targets.coverage==targetHandles.coverage&&
                targets.compositeDepth==targetHandles.compositeDepth,
                "Switching color shader features replaced private front/temporal targets");
            for(std::size_t alpha=3;alpha<values.size();alpha+=4)
                Require(std::abs(values[alpha]-voxelBeauty[alpha])<.000001f,
                    "A compiled color feature variant changed physical coverage");
        };
        const auto activeDraw=[&](StrandDebugMode mode){auto values=debugDraw(mode);stable(values);return values;};
        const auto linearSum=[&](const std::vector<float>& beauty,const std::vector<float>& direct,
            const std::vector<float>& indirect){
            for(std::size_t pixel=0;pixel<beauty.size();pixel+=4)for(unsigned channel=0;channel<3;++channel){
                const float expected=direct[pixel+channel]+indirect[pixel+channel];
                // Each separate front-color result passes FP16 storage.
                // Low fixture radiance avoids the production HDR clamp.
                const float tolerance=.0025f+.0025f*std::max(std::abs(expected),std::abs(beauty[pixel+channel]));
                Require(std::abs(beauty[pixel+channel]-expected)<=tolerance,
                    "Active compiled Beauty != direct + indirect over the complete image");
            }
        };
        const auto voxelDirect=activeDraw(StrandDebugMode::Direct);
        const auto voxelIndirect=activeDraw(StrandDebugMode::Indirect);
        const auto voxelCounts=activeDraw(StrandDebugMode::IblPathCounts);
        linearSum(voxelBeauty,voxelDirect,voxelIndirect);
        debugItem.material.volumePointIbl=true;
        const auto pointBeauty=activeDraw(StrandDebugMode::Beauty);
        const auto pointDirect=activeDraw(StrandDebugMode::Direct);
        const auto pointIndirect=activeDraw(StrandDebugMode::Indirect);
        const auto pointCounts=activeDraw(StrandDebugMode::IblPathCounts);
        linearSum(pointBeauty,pointDirect,pointIndirect);
        Require(glm::length(debugPixel(voxelCounts))>1e-5f&&glm::length(debugPixel(pointCounts))>1e-5f,
            "Active source-volume/point diagnostic consumed fabricated zero paths");
        Require(MaximumDifference(voxelCounts,pointCounts)>1e-6f,
            "Active point variant did not query the actual shading point instead of cached voxel paths");
        Require(MaximumDifference(pointBeauty,activeDraw(StrandDebugMode::Beauty))<.000001f,
            "Returning from point diagnostics changed the same point Beauty pixels");
        debugItem.material.volumePointIbl=false;
        Require(MaximumDifference(voxelBeauty,activeDraw(StrandDebugMode::Beauty))<.000001f,
            "Point-to-voxel or diagnostic-to-Beauty switch contaminated restored Beauty");
        debugItem.material=savedMaterial;debugView.outputTemporalData=savedTemporal;
        std::cout<<"Strand four color variants: active8^3 voxel/point x Beauty/debug; full-image direct+indirect, finite/alpha, stable volume/targets and Beauty restore passed\n";
    }
    const auto debugSavedLights=debugView.lighting;const auto debugSavedSun=debugView.sunIntensity;
    debugView.lighting.lightColors.fill(glm::vec4(0));debugView.sunIntensity=0;
    debugClose(debugPixel(debugDraw(StrandDebugMode::Direct)),glm::vec3(0),.000001f,
        "Direct diagnostic retains energy after all direct lights are disabled");
    debugClose(debugPixel(debugDraw(StrandDebugMode::Beauty)),debugValues[3],.0001f,
        "Removing direct lights did not isolate the same indirect component");
    debugClose(debugPixel(debugDraw(StrandDebugMode::BaseColor)),debugBase,.0005f,
        "Base diagnostic changed with the light setup");
    debugView.lighting=debugSavedLights;debugView.sunIntensity=debugSavedSun;
    debugView.lighting.ambientAndExposure=glm::vec4(0);debugView.indirect.enabled=false;
    debugClose(debugPixel(debugDraw(StrandDebugMode::Indirect)),glm::vec3(0),.000001f,
        "Indirect diagnostic retains energy after its actual producer is disabled");
    debugClose(debugPixel(debugDraw(StrandDebugMode::Beauty)),debugValues[2],.0001f,
        "Removing the indirect producer did not isolate the same direct component");
    debugPass.Shutdown();
    std::cout<<"Strand diagnostics: Beauty/direct/indirect and source ballistic R/TT/TRT identities, source base/incident, N/G disable, coverage/history-independent mode restore passed\n";
}
