// Included in the started-device scope of strand_render_gpu_test.cpp.
// These oracles use known endpoint colors, projective geometry and independent
// explicit-time scenes. They do not reimplement the production hair closure.
{
    using namespace PipelineDetail;
    ResourceBuilder segmentResources(device);
    const auto segmentColor=segmentResources.Texture(Size,Size,RHITextureFormat::RGBA32F);
    const auto segmentDepth=segmentResources.Texture(Size,Size,RHITextureFormat::Depth32F);
    const auto segmentOpaque=segmentResources.Texture(Size,Size,RHITextureFormat::Depth32F);
    const auto segmentTarget=segmentResources.Target(segmentColor,segmentDepth);
    const auto segmentOpaqueTarget=segmentResources.Target({},segmentOpaque);
    const auto segmentOwner=segmentResources.Finish();
    StrandRenderPass segmentPass(device);
    StrandPassView segmentView;
    segmentView.cameraPosition=segmentView.previousCameraPosition={0,0,3};
    segmentView.frontLayerTransparency=true;segmentView.frontDepthGroups=3;
    segmentView.pairedLayerTransparency=true;segmentView.opaqueDepth=segmentOpaque;
    segmentView.outputStraightColor=true;segmentView.debugMode=StrandDebugMode::BaseColor;
    segmentView.lighting.lightColors.fill(glm::vec4(0));
    segmentView.lighting.ambientAndExposure={.1f,.1f,.1f,0};
    segmentView.sunDirection={-.45f,.1f,-1};segmentView.sunIntensity=.4f;
    const std::array<std::uint32_t,6> segmentLines{0,1,1,2,2,3};
    std::array<StrandVertex,4> segmentPoints{{
        {{0,-.9f,0,.08f},{0,1,0,0}},{{0,-.6f,0,.08f},{0,1,0,.2f}},
        {{0,.6f,0,.08f},{0,1,0,.8f}},{{0,.9f,0,.08f},{0,1,0,1}}
    }};
    std::array<StrandColorCoordinate,4> segmentCoordinates{{{{.5f,.25f}},{{0,.25f}},{{1,.25f}},{{.5f,.25f}}}};
    StrandMesh segmentMesh(device);
    Require(segmentMesh.Update(segmentPoints,segmentLines,segmentCoordinates),segmentMesh.LastError());
    StrandRenderItem segmentItem;segmentItem.geometry=segmentMesh.Snapshot();
    segmentItem.material.opacity=.67f;segmentItem.material.sourceColor=true;
    auto segmentGradient=std::make_shared<StrandTextureTable>();segmentGradient->width=5;segmentGradient->height=3;
    for(unsigned row=0;row<3;++row)for(float c:{.125f,.5f,.875f,.5f,.125f})
        segmentGradient->texels.emplace_back(c,c,c,1);
    segmentItem.material.gradientTable=segmentGradient;
    const auto segmentRead=[&](RenderResourceHandle<RHITextureSpec> texture){
        Require(texture.IsValid(),"Segment fixture requested an absent RHI texture");
        return OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(texture);});
    };
    unsigned segmentDraws=0;
    const auto segmentDraw=[&](StrandRenderPass& pass){
        auto encoder=device.BeginFrame({0,Size,Size});Require(encoder.KeepAlive(segmentOwner),"Pin segment oracle resources");
        RHICommand::SetRenderTarget clear;clear.target=segmentOpaqueTarget;clear.width=clear.height=Size;
        clear.clearFlags=RHICommand::ClearDepth;clear.clearDepth=1;
        Require(encoder.SetRenderTarget(clear),"Clear segment independent opaque depth");
        clear.target=segmentTarget;clear.clearFlags=RHICommand::ClearColor|RHICommand::ClearDepth;clear.clearColor=glm::vec4(0);
        Require(encoder.SetRenderTarget(clear),"Clear segment scene-linear target");
        pass.Record(encoder,std::span(&segmentItem,1),{segmentTarget,segmentColor,segmentDepth,Size,Size,1},segmentView);
        Submit(device,encoder);++segmentDraws;
        auto image=segmentRead(segmentColor);Require(image.size()==Size*Size*4,"Segment target readback dimensions");
        for(float v:image)Require(std::isfinite(v),"Segment rendering produced nonfinite radiance/coverage");
        return image;
    };
    const auto segmentPixel=[](const std::vector<float>& image,unsigned x=Size/2,unsigned y=Size/2){
        const auto p=std::size_t(y*Size+x)*4;return glm::vec4(image[p],image[p+1],image[p+2],image[p+3]);
    };
    const auto segmentSameAlpha=[&](const std::vector<float>& a,const std::vector<float>& b){
        Require(a.size()==b.size(),"Segment/fragment coverage dimensions differ");
        for(std::size_t p=3;p<a.size();p+=4)
            Require(std::abs(a[p]-b[p])<1e-6f,"Segment shading changed analytic coverage or transparency");
    };
    const auto segmentSameColor=[&](glm::vec3 actual,glm::vec3 expected,float tolerance,const char* message){
        if(glm::any(glm::greaterThan(glm::abs(actual-expected),glm::vec3(tolerance))))
            std::cerr<<message<<" actual="<<actual.x<<','<<actual.y<<','<<actual.z<<" expected="<<expected.x<<','<<expected.y<<','<<expected.z<<'\n';
        Require(glm::all(glm::lessThanEqual(glm::abs(actual-expected),glm::vec3(tolerance))),message);
    };
    const auto segmentDefault=segmentDraw(segmentPass);
    segmentItem.material.sourceSegmentShading=false;
    Require(MaximumDifference(segmentDefault,segmentDraw(segmentPass))==0,"Explicit fragment mode changed legacy default pixels");
    segmentItem.material.sourceSegmentShading=true;
    const auto segmentNonlinear=segmentDraw(segmentPass);
    segmentSameAlpha(segmentDefault,segmentNonlinear);
    segmentSameColor(glm::vec3(segmentPixel(segmentNonlinear)),glm::vec3(.125f),.001f,
        "Segment mode sampled nonlinear gradient at the raster midpoint instead of endpoint shading");
    Require(segmentPixel(segmentDefault).r>.75f,"Nonlinear endpoint fixture cannot distinguish fragment sampling");
    {
        // Source stores the shade of outgoing segment i at point i+1 and
        // copies the first shade to the chain root. This is deliberately NOT
        // the ordinary per-point appearance-coordinate convention.
        const auto debug=segmentPass.SegmentShadingDebugTextures();
        const auto shades=segmentRead(debug.color);
        Require(debug.width*debug.height>=4&&shades.size()==std::size_t(debug.width)*debug.height*4,
            "Segment producer debug image dimensions");
        for(unsigned point=0;point<4;++point){
            const auto p=std::size_t(point)*4;const float expected=point<3?.125f:.875f;
            segmentSameColor({shades[p],shades[p+1],shades[p+2]},glm::vec3(expected),.0001f,
                "Segment endpoint shade index/next-point source arc contract differs");
            Require(shades[p+3]==1.f,"A valid source endpoint shade is not marked initialized");
        }
        const auto stats=segmentPass.SegmentShadingStatistics();
        Require(stats.passes==1&&(stats.points==3||stats.points==4)&&stats.outputPoints==4,
            "Segment producer was repeated per transparency layer instead of once per item");
        Require(stats.computePasses+stats.rasterPasses==1&&stats.points==(stats.computePasses?3u:4u),
            "Segment compute/fullscreen producer evaluation statistics differ from actual topology");
    }
    segmentItem.material.sourceSegmentShading=false;
    Require(MaximumDifference(segmentDefault,segmentDraw(segmentPass))==0,
        "Segment variant contaminated restored legacy fragment shading");
    segmentItem.material.sourceSegmentShading=true;
    segmentView.pairedLayerTransparency=false;
    const auto segmentLegacyLayers=segmentDraw(segmentPass);
    Require(MaximumDifference(segmentNonlinear,segmentLegacyLayers)<.002f,"Segment color is not shared by legacy and paired groups");
    Require(segmentPass.SegmentShadingStatistics().passes==1,"Legacy groups dispatched expensive segment shading repeatedly");
    segmentView.pairedLayerTransparency=true;
    {
        // Visibility is complete here, so line-list ordering must not change
        // an otherwise identical directed chain or introduce write races.
        const std::array<std::uint32_t,6> shuffled{2,3,0,1,1,2};
        StrandMesh shuffledMesh(device);Require(shuffledMesh.Update(segmentPoints,shuffled,segmentCoordinates),shuffledMesh.LastError());
        const auto originalGeometry=segmentItem.geometry;segmentItem.geometry=shuffledMesh.Snapshot();
        Require(MaximumDifference(segmentNonlinear,segmentDraw(segmentPass))==0,
            "Directed source-chain shading depends on line-list submission order");
        segmentItem.geometry=originalGeometry;
    }
    {
        // Inputs are exactly representable in half. These are halfway between
        // adjacent native R/G six-mantissa-bit and B five-mantissa-bit values,
        // so the expected bit truncation is known without copying a packer.
        const auto savedMaterial=segmentItem.material;
        segmentItem.material.sourceColor=false;
        segmentItem.material.rootColor=segmentItem.material.tipColor={.302734375f,.2021484375f,.1005859375f};
        const auto quantized=segmentDraw(segmentPass);
        segmentSameColor(glm::vec3(segmentPixel(quantized)),{.30078125f,.201171875f,.099609375f},.0001f,
            "Segment endpoint RGB did not retain the original positive RGB11/11/10 truncation");
        segmentItem.material=savedMaterial;
    }
    {
        // Equal-length orthogonal chords have a 45-degree centripetal tangent
        // at the joint. Compare the source endpoint against a uniformly lit
        // fragment with that independently known direction. The forward-chord
        // direction is a deliberate negative control, not the expected value.
        const auto savedItem=segmentItem;const auto savedView=segmentView;
        std::array<StrandVertex,4> bent{{
            {{-.4f,0,0,.08f},{0,1,0,0}},{{0,0,0,.08f},{0,1,0,.3f}},
            {{0,.4f,0,.08f},{0,1,0,.6f}},{{0,.8f,0,.08f},{0,1,0,1}}
        }};
        StrandMesh bentMesh(device);Require(bentMesh.Update(bent,segmentLines),bentMesh.LastError());
        segmentItem.geometry=bentMesh.Snapshot();segmentItem.material.sourceColor=false;
        segmentItem.material.rootColor=segmentItem.material.tipColor={.3f,.2f,.1f};
        segmentItem.material.physicalScattering=segmentItem.material.sourceDirectScattering=true;
        segmentItem.material.useNativeMomentLut=false;segmentItem.material.longitudinalRoughness=.35f;
        segmentView.debugMode=StrandDebugMode::Beauty;segmentView.cameraPosition={0,0,1000000};
        segmentView.lighting.ambientAndExposure=glm::vec4(0);segmentView.sunDirection={-1,0,-.1f};segmentView.sunIntensity=1;
        segmentDraw(segmentPass);const auto shades=segmentRead(segmentPass.SegmentShadingDebugTextures().color);
        const glm::vec3 actual(shades[8],shades[9],shades[10]);
        const std::array<std::uint32_t,2> line{0,1};
        const glm::vec3 bisector=glm::normalize(glm::vec3(1,1,0));
        std::array<StrandVertex,2> reference{{{{0,-.4f,0,.08f},glm::vec4(bisector,0)},{{0,.4f,0,.08f},glm::vec4(bisector,1)}}};
        StrandMesh referenceMesh(device);Require(referenceMesh.Update(reference,line),referenceMesh.LastError());
        segmentItem.geometry=referenceMesh.Snapshot();segmentItem.material.sourceSegmentShading=false;
        const glm::vec3 expected(segmentPixel(segmentDraw(segmentPass)));
        for(auto& point:reference)point.tangentArc=glm::vec4(0,1,0,point.tangentArc.w);
        Require(referenceMesh.Update(reference,line),referenceMesh.LastError());segmentItem.geometry=referenceMesh.Snapshot();
        const glm::vec3 wrong(segmentPixel(segmentDraw(segmentPass)));
        Require(glm::length(expected)>1e-4f,"Centripetal tangent closure oracle has no light");
        Require(glm::length(actual-expected)<=.035f*glm::length(expected)+1e-4f,
            "Segment producer did not use the START joint's independent centripetal bisector");
        Require(glm::length(actual-wrong)>3.f*glm::length(actual-expected)+.001f,
            "Centripetal tangent negative control cannot distinguish a forward-chord substitution");
        segmentItem=savedItem;segmentView=savedView;
    }
    {
        auto linearGradient=std::make_shared<StrandTextureTable>();linearGradient->width=2;linearGradient->height=3;
        for(unsigned row=0;row<3;++row){linearGradient->texels.emplace_back(.125f,.125f,.125f,1);linearGradient->texels.emplace_back(.75f,.75f,.75f,1);}
        segmentItem.material.gradientTable=linearGradient;
        auto perspectivePoints=segmentPoints;
        perspectivePoints[0].positionRadius={0,-.95f,-1,.08f};perspectivePoints[1].positionRadius={0,-.65f,-1,.08f};
        perspectivePoints[2].positionRadius={0,.65f,-3,.08f};perspectivePoints[3].positionRadius={0,.95f,-3,.08f};
        auto perspectiveCoordinates=segmentCoordinates;perspectiveCoordinates[3].gradientRandom.x=1.f;
        Require(segmentMesh.Update(perspectivePoints,segmentLines,perspectiveCoordinates),segmentMesh.LastError());segmentItem.geometry=segmentMesh.Snapshot();
        segmentView.cameraPosition={0,0,0};segmentView.viewProjection=glm::perspectiveRH_ZO(glm::radians(60.f),1.f,.1f,10.f);
        const auto perspectiveImage=segmentDraw(segmentPass);
        const glm::dvec4 a=glm::dmat4(segmentView.viewProjection)*glm::dvec4(glm::vec3(perspectivePoints[1].positionRadius),1);
        const glm::dvec4 b=glm::dmat4(segmentView.viewProjection)*glm::dvec4(glm::vec3(perspectivePoints[2].positionRadius),1);
        const double sampleNdcY=2.*(double(Size/2)+.5)/Size-1.;
        const double lambda=(sampleNdcY-a.y/a.w)/(b.y/b.w-a.y/a.w);
        Require(lambda>.1&&lambda<.9,"Projective oracle sample escaped the interior segment");
        const double expected=((1-lambda)*.125/a.w+lambda*.75/b.w)/((1-lambda)/a.w+lambda/b.w);
        const double screenLinear=(1-lambda)*.125+lambda*.75;
        Require(std::abs(expected-screenLinear)>.1,"Perspective and screen-linear negative control are indistinguishable");
        segmentSameColor(glm::vec3(segmentPixel(perspectiveImage)),glm::vec3(float(expected)),.0025f,
            "Segment endpoint colors did not use hardware perspective-correct interpolation");
        segmentItem.material.sourceSegmentShading=false;const auto fragmentPerspective=segmentDraw(segmentPass);
        segmentSameAlpha(perspectiveImage,fragmentPerspective);
        segmentItem.material.sourceSegmentShading=true;
    }
    // A second pass has never seen the preceding scene state. Compare the
    // reused producer against that independent reference after all relevant
    // input mutations. Both execute the real closure; no CPU closure mirror.
    segmentView=StrandPassView{};segmentView.cameraPosition=segmentView.previousCameraPosition={0,0,3};
    segmentView.frontLayerTransparency=true;segmentView.frontDepthGroups=3;segmentView.pairedLayerTransparency=true;
    segmentView.opaqueDepth=segmentOpaque;segmentView.outputStraightColor=true;
    segmentView.sunDirection={-.45f,.1f,-1};segmentView.sunIntensity=.4f;
    segmentView.lighting.lightColors.fill(glm::vec4(0));segmentView.lighting.ambientAndExposure={.04f,.03f,.02f,0};
    segmentItem.material.gradientTable=segmentGradient;segmentItem.material.physicalScattering=true;
    segmentItem.material.sourceDirectScattering=true;segmentItem.material.useNativeMomentLut=false;
    Require(segmentMesh.Update(segmentPoints,segmentLines,segmentCoordinates),segmentMesh.LastError());segmentItem.geometry=segmentMesh.Snapshot();
    const auto segmentReferenceCheck=[&](const char* message){
        const auto reused=segmentDraw(segmentPass);StrandRenderPass fresh(device);const auto reference=segmentDraw(fresh);
        Require(MaximumDifference(reused,reference)<1e-6f,message);
        const auto stats=segmentPass.SegmentShadingStatistics();
        Require(stats.passes==1&&(stats.points==3||stats.points==4)&&stats.outputPoints==4,
            "A segment input update reused an unexecuted/stale producer");
        fresh.Shutdown();return reused;
    };
    const auto segmentBeauty=segmentReferenceCheck("Initial segment closure differs from a fresh pass");
    Require(segmentPixel(segmentBeauty).a>.6f&&glm::length(glm::vec3(segmentPixel(segmentBeauty)))>1e-4f,
        "Source segment Beauty update fixture has no covered/illuminated center");
    segmentPoints[1].positionRadius.x=.09f;
    Require(segmentMesh.Update(segmentPoints,segmentLines,segmentCoordinates),segmentMesh.LastError());segmentItem.geometry=segmentMesh.Snapshot();
    segmentReferenceCheck("Position/centripetal-tangent update retained stale endpoint colors");
    {
        const auto previousShades=segmentRead(segmentPass.SegmentShadingDebugTextures().color);
        for(auto& point:segmentPoints)point.tangentArc=glm::vec4(1,0,0,point.tangentArc.w);
        Require(segmentMesh.Update(segmentPoints,segmentLines,segmentCoordinates),segmentMesh.LastError());segmentItem.geometry=segmentMesh.Snapshot();
        segmentDraw(segmentPass);
        const auto changedShades=segmentRead(segmentPass.SegmentShadingDebugTextures().color);
        Require(previousShades.size()>=16&&changedShades.size()>=16,"Tangent source-shade readback is truncated");
        for(unsigned value=0;value<16;++value)Require(previousShades[value]==changedShades[value],
            "Source segment shading used supplied raster tangents instead of the P-neighbor centripetal derivative");
        for(auto& point:segmentPoints)point.tangentArc=glm::vec4(0,1,0,point.tangentArc.w);
        Require(segmentMesh.Update(segmentPoints,segmentLines,segmentCoordinates),segmentMesh.LastError());segmentItem.geometry=segmentMesh.Snapshot();
    }
    segmentItem.material.roughness=.42f;segmentItem.material.azimuthalRoughness=.55f;
    segmentReferenceCheck("Material change retained stale segment endpoint colors");
    segmentView.cameraPosition={.6f,.2f,2};
    segmentReferenceCheck("Camera change retained stale segment view-dependent shading");
    segmentItem.model=glm::translate(glm::mat4(1),glm::vec3(.05f,.02f,-.1f));
    segmentReferenceCheck("Model transform change retained stale world-space segment shading");
    segmentView.sunDirection={.65f,-.15f,-1};segmentView.sunIntensity=.25f;
    segmentReferenceCheck("Direct-light change retained stale segment endpoint colors");
    {
        segmentView.debugMode=StrandDebugMode::BaseColor;segmentDraw(segmentPass);
        segmentView.debugMode=StrandDebugMode::Beauty;
        segmentReferenceCheck("Segment Beauty/debug variant switch retained the wrong producer closure");
        segmentItem.material.sourceSegmentShading=false;const auto fragmentBeauty=segmentDraw(segmentPass);
        segmentItem.material.sourceSegmentShading=true;segmentDraw(segmentPass);
        segmentItem.material.sourceSegmentShading=false;
        Require(MaximumDifference(fragmentBeauty,segmentDraw(segmentPass))==0,
            "Segment Beauty variant contaminated the restored original fragment Beauty");
        segmentItem.material.sourceSegmentShading=true;
    }
    {
        // Motion belongs to geometry, not to the location of the expensive
        // color evaluation. Its full independent texture must stay identical.
        auto old=segmentPoints,current=segmentPoints;for(auto& p:current)p.positionRadius.x+=.125f;
        Require(segmentMesh.Update(old,segmentLines,segmentCoordinates)&&segmentMesh.Update(current,segmentLines,segmentCoordinates,old),segmentMesh.LastError());
        segmentItem.geometry=segmentMesh.Snapshot();segmentItem.model=segmentItem.previousModel=glm::mat4(1);
        segmentItem.previousTransformValid=true;segmentView.outputTemporalData=segmentView.outputMotionData=true;
        segmentView.motionHistoryValid=true;segmentView.previousCameraPosition=segmentView.cameraPosition;
        segmentItem.material.sourceSegmentShading=false;const auto fragment=segmentDraw(segmentPass);
        const auto fragmentMotion=segmentRead(segmentPass.TemporalTextures().motion);
        const auto fragmentCoverage=segmentRead(segmentPass.TemporalTextures().coverage);
        segmentItem.material.sourceSegmentShading=true;const auto sampled=segmentDraw(segmentPass);
        segmentSameAlpha(fragment,sampled);
        Require(MaximumDifference(fragmentMotion,segmentRead(segmentPass.TemporalTextures().motion))==0&&
            MaximumDifference(fragmentCoverage,segmentRead(segmentPass.TemporalTextures().coverage))==0,
            "Segment color moved/deformed the nearest-depth motion or coverage outputs");
        segmentView.outputTemporalData=segmentView.outputMotionData=false;
        // Each real shutter sample must shade its own physical-time P/T and
        // interpolated camera/model, not just move a single current color.
        segmentView.sunIntensity=0;segmentView.lighting.ambientAndExposure=glm::vec4(0);
        segmentView.lighting.lightPositions[0]={-.1f,.6f,.2f,1};
        segmentView.lighting.lightColors[0]={.1f,.08f,.06f,.01f};
        StrandShutterPass shutter(device);
        auto encoder=device.BeginFrame({0,Size,Size});Require(encoder.KeepAlive(segmentOwner),"Pin segment shutter oracle");
        shutter.Record(encoder,std::span(&segmentItem,1),{segmentTarget,segmentColor,segmentDepth,Size,Size,1},segmentView,1.f,false);
        Submit(device,encoder);
        const auto timeTextures=shutter.SampleTextures();
        glm::vec3 firstTimeShade(0),lastTimeShade(0);
        for(unsigned sample=0;sample<4;++sample){
            const auto timeImage=segmentRead(timeTextures[sample]);const float t=(float(sample)+.5f)*.25f;
            std::array<StrandVertex,4> explicitPoints=current;
            for(unsigned i=0;i<4;++i)explicitPoints[i].positionRadius=glm::mix(old[i].positionRadius,current[i].positionRadius,t);
            StrandMesh explicitMesh(device);Require(explicitMesh.Update(explicitPoints,segmentLines,segmentCoordinates),explicitMesh.LastError());
            const auto savedGeometry=segmentItem.geometry;segmentItem.geometry=explicitMesh.Snapshot();
            const auto expected=segmentDraw(segmentPass);segmentItem.geometry=savedGeometry;
            const auto shades=segmentRead(segmentPass.SegmentShadingDebugTextures().color);
            if(sample==0)firstTimeShade={shades[8],shades[9],shades[10]};
            if(sample==3)lastTimeShade={shades[8],shades[9],shades[10]};
            Require(MaximumDifference(timeImage,expected)<.006f,"Shutter sample retained current-only segment P/T/color");
        }
        Require(glm::length(firstTimeShade-lastTimeShade)>.01f,
            "Shutter color fixture has no measurable moving-point radiance change");
        shutter.Shutdown();
    }
    {
        const auto savedItem=segmentItem;
        for(const auto& topology:std::array<std::array<std::uint32_t,4>,2>{{{{0,1,0,2}},{{0,1,1,0}}}}){
            StrandMesh invalidMesh(device);Require(invalidMesh.Update(segmentPoints,topology,segmentCoordinates),invalidMesh.LastError());
            segmentItem.geometry=invalidMesh.Snapshot();segmentItem.material.sourceSegmentShading=true;
            auto dropped=device.BeginFrame({0,Size,Size});bool rejected=false;
            try {segmentPass.Record(dropped,std::span(&segmentItem,1),{segmentTarget,segmentColor,segmentDepth,Size,Size,1},segmentView);}
            catch(const std::invalid_argument& error){rejected=std::string(error.what()).find("nonbranching acyclic")!=std::string::npos;}
            Require(dropped.End(false),"End deliberately rejected source-topology frame");segmentPass.InvalidateVolume();
            Require(rejected,"Branch/cycle source topology was accepted with potentially competing endpoint writes");
            segmentItem.material.sourceSegmentShading=false;segmentDraw(segmentPass);
        }
        segmentItem=savedItem;
    }
    segmentPass.Shutdown();
    std::cout<<"Source segment shading: "<<segmentDraws<<" scene-linear draws; next-arc/root-copy indices, nonlinear endpoints, perspective vs screen-linear, alpha/legacy identity, producer reuse, geometry/material/camera/light variants, motion and physical shutter passed\n";
}
