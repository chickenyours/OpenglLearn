// Include in strand_render_gpu_test.cpp inside its RHI fixture, after the
// standard target/draw setup. Uses the existing device/Require/Submit/OnThread
// helpers, but owns all resources and a separate StrandRenderPass.
{
    using namespace PipelineDetail;
    ResourceBuilder pairedBuilder(device);
    const auto pairedColor=pairedBuilder.Texture(Size,Size,RHITextureFormat::RGBA32F);
    const auto pairedDepth=pairedBuilder.Texture(Size,Size,RHITextureFormat::Depth32F);
    const auto pairedOpaque=pairedBuilder.Texture(Size,Size,RHITextureFormat::Depth32F);
    const auto pairedTarget=pairedBuilder.Target(pairedColor,pairedDepth);
    const auto pairedOpaqueTarget=pairedBuilder.Target({},pairedOpaque);
    const auto pairedMsColor=pairedBuilder.Texture(Size,Size,RHITextureFormat::RGBA16F,4);
    const auto pairedMsDepth=pairedBuilder.Texture(Size,Size,RHITextureFormat::Depth32F,4);
    const auto pairedMsTarget=pairedBuilder.Target(pairedMsColor,pairedMsDepth);
    const auto pairedMsResolve=pairedBuilder.Texture(Size,Size,RHITextureFormat::RGBA16F);
    const auto pairedMsResolveTarget=pairedBuilder.Target(pairedMsResolve,{});
    const auto pairedOwner=pairedBuilder.Finish();
    StrandRenderPass pairedPass(device);
    StrandPassView pairedView;pairedView.cameraPosition={0,0,2};pairedView.frontLayerTransparency=true;
    pairedView.frontDepthGroups=3;pairedView.pairedLayerTransparency=true;
    pairedView.lighting.lightColors.fill(glm::vec4(0));pairedView.lighting.ambientAndExposure=glm::vec4(0);
    const glm::vec3 pairedBackground(.01f,.02f,.03f);
    const auto pairedDraw=[&](const std::vector<StrandRenderItem>& entries,bool enabled,bool multisample=false,float opaqueZ=1.f) {
        pairedView.pairedLayerTransparency=enabled;
        auto encoder=device.BeginFrame({0,Size,Size});Require(encoder.KeepAlive(pairedOwner),"Pin paired-transparency fixtures");
        RHICommand::SetRenderTarget opaque;opaque.target=pairedOpaqueTarget;opaque.width=opaque.height=Size;
        opaque.clearFlags=RHICommand::ClearDepth;opaque.clearDepth=opaqueZ;
        Require(encoder.SetRenderTarget(opaque),"Clear independent paired opaque depth");
        RHICommand::SetRenderTarget scene;scene.target=multisample?pairedMsTarget:pairedTarget;scene.width=scene.height=Size;
        scene.clearFlags=RHICommand::ClearColor|RHICommand::ClearDepth;scene.clearColor=glm::vec4(pairedBackground,1);scene.clearDepth=opaqueZ;
        Require(encoder.SetRenderTarget(scene),"Clear paired strand scene");
        pairedView.opaqueDepth=multisample?RenderResourceHandle<RHITextureSpec>{}:pairedOpaque;
        const StrandPassTarget destination{scene.target,multisample?pairedMsColor:pairedColor,multisample?pairedMsDepth:pairedDepth,Size,Size,multisample?4u:1u};
        pairedPass.Record(encoder,entries,destination,pairedView);
        if(multisample)Require(encoder.ResolveRenderTarget({pairedMsTarget,pairedMsResolveTarget,true,false}),"Resolve paired fallback scene");
        Submit(device,encoder);
        auto values=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(multisample?pairedMsResolve:pairedColor);});
        Require(values.size()==Size*Size*4,"Paired scene readback dimensions");
        for(float value:values)Require(std::isfinite(value),"Paired scene contains a nonfinite value");
        return values;
    };
    const auto pairedPixel=[](const std::vector<float>& values){const auto at=((Size/2)*Size+Size/2)*4;return glm::vec3(values[at],values[at+1],values[at+2]);};
    const auto pairedCompare=[&](glm::vec3 actual,glm::vec3 expected,float tolerance,const char* message){
        if(glm::any(glm::greaterThan(glm::abs(actual-expected),glm::vec3(tolerance))))
            std::cerr<<message<<" actual="<<actual.x<<','<<actual.y<<','<<actual.z<<" expected="<<expected.x<<','<<expected.y<<','<<expected.z<<'\n';
        Require(glm::all(glm::lessThanEqual(glm::abs(actual-expected),glm::vec3(tolerance))),message);
    };
    StrandMesh pairedMesh(device);
    const std::array<StrandVertex,2> pairedVertices{{{{0,-.8f,0,.1f},{0,1,0,0}},{{0,.8f,0,.1f},{0,1,0,1}}}};
    const std::array<std::uint32_t,2> pairedLine{0,1};Require(pairedMesh.Update(pairedVertices,pairedLine),pairedMesh.LastError());
    const auto pairedLayer=[&](glm::vec3 rgb,float alpha,float z) {
        StrandRenderItem entry;entry.geometry=pairedMesh.Snapshot();entry.guides=true;
        entry.material.rootColor=entry.material.tipColor=rgb;entry.material.opacity=alpha;entry.material.areaPreservingCoverage=true;
        entry.model[3].z=z;return entry;
    };
    const auto pairedWeight=[](const StrandRenderItem& entry){const float a=entry.material.opacity,z=entry.model[3].z*.5f+.5f;
        return a*std::clamp(a*4.f+.01f,.01f,4.f)*std::clamp(.05f+10.f*(1.f-z)*(1.f-z),.05f,1.f);};
    const auto pairedWeighted=[&](std::span<const StrandRenderItem> entries){glm::vec3 sum(0);float weight=0,transmission=1;
        for(const auto& entry:entries){const float w=pairedWeight(entry);sum+=w*entry.material.rootColor;weight+=w;transmission*=1-entry.material.opacity;}
        return std::pair{weight>0?sum/weight:glm::vec3(0),1-transmission};};
    const auto pairedOrdered=[&](const std::vector<StrandRenderItem>& entries,std::uint32_t selected){glm::vec3 sum(0);float transmission=1;
        for(std::size_t i=0;i<std::min<std::size_t>(selected,entries.size());++i){sum+=transmission*entries[i].material.opacity*entries[i].material.rootColor;transmission*=1-entries[i].material.opacity;}
        if(entries.size()>selected){const auto [rgb,alpha]=pairedWeighted(std::span(entries).subspan(selected));sum+=transmission*alpha*rgb;transmission*=1-alpha;}
        return sum+transmission*pairedBackground;};
    float pairedMaximumOrdinaryDifference=0;
    for(std::uint32_t groups:{2u,3u,7u}) {
        pairedView.frontDepthGroups=groups;
        std::vector<StrandRenderItem> entries;
        for(std::uint32_t i=0;i<groups+2;++i)entries.push_back(pairedLayer({.08f+.06f*i,.5f-.025f*i,.03f+.04f*i},.22f+.025f*i,-.8f+.08f*i));
        const auto legacy=pairedDraw(entries,false),paired=pairedDraw(entries,true);
        pairedCompare(pairedPixel(paired),pairedOrdered(entries,groups),.003f,"Paired depth groups/tail differ from independent analytic over");
        pairedMaximumOrdinaryDifference=std::max(pairedMaximumOrdinaryDifference,MaximumDifference(legacy,paired));
        Require(MaximumDifference(legacy,paired)<.004f,"Paired regular lighting/coverage differs excessively from separate passes");
        Require(pairedPass.PairedLayerTransparencyActive()&&pairedPass.PassCount()==3*groups+2&&
            pairedPass.TransparencyRasterPassCount()==2*groups+1,"Paired transparency pass accounting");
        std::reverse(entries.begin(),entries.end());
        Require(MaximumDifference(paired,pairedDraw(entries,true))<.003f,"Paired ordinary layers depend on submission order");
    }
    pairedView.frontDepthGroups=3;
    const auto equal=pairedLayer({.7f,.2f,.1f},.2f,-.25f);
    std::vector<StrandRenderItem> ties(5,equal);
    const float tiesAlpha=1-std::pow(.8f,5.f);
    pairedCompare(pairedPixel(pairedDraw(ties,true)),equal.material.rootColor*tiesAlpha+pairedBackground*(1-tiesAlpha),.003f,"Paired equal-depth group lost complete product coverage");
    auto tiedSecond=equal;tiedSecond.material.rootColor=tiedSecond.material.tipColor={.05f,.6f,1.f};tiedSecond.material.opacity=.35f;
    const std::vector<StrandRenderItem> mixed{equal,tiedSecond};const auto [mixedRgb,mixedAlpha]=pairedWeighted(mixed);
    const auto mixedResult=pairedDraw(mixed,true);
    pairedCompare(pairedPixel(mixedResult),mixedRgb*mixedAlpha+pairedBackground*(1-mixedAlpha),.003f,"Paired mixed ties changed the explicit weighted-group convention");
    Require(MaximumDifference(mixedResult,pairedDraw({tiedSecond,equal},true))<.003f,"Paired mixed ties depend on submission order");
    // Distinct representable window depths must not be widened into one group.
    const float closeDepth=.375f,nextDepth=std::nextafter(closeDepth,1.f);
    const auto closeFront=pairedLayer({.05f,.04f,.02f},.95f,closeDepth*2-1),closeRear=pairedLayer({1,1,1},.95f,nextDepth*2-1);
    pairedCompare(pairedPixel(pairedDraw({closeRear,closeFront},true)),pairedOrdered({closeFront,closeRear},3),.003f,"Paired Equal merged distinct one-ULP depths");
    // Low depth weights used to retain revealage but discard all group RGB at
    // accum.a<=1e-5. The physical alpha here is above the raster cutoff.
    const auto thin=pairedLayer({.2f,.55f,.9f},.002f,.8f);
    const auto thinLegacy=pairedDraw({thin},false),thinPaired=pairedDraw({thin},true);
    const auto thinExpected=thin.material.rootColor*thin.material.opacity+pairedBackground*(1-thin.material.opacity);
    pairedCompare(pairedPixel(thinPaired),thinExpected,.000015f,"FP32 paired thin layer lost color while retaining coverage");
    Require(glm::length(pairedPixel(thinPaired)-thinExpected)<glm::length(pairedPixel(thinLegacy)-thinExpected)*.1f,
        "Thin-layer negative control did not expose the legacy RGB cutoff");
    // A long coplanar group overflows only the old FP16 RGB accumulator, not
    // the per-fragment source color or final HDR output. Keep it one draw.
    StrandMesh denseMesh(device);std::vector<std::uint32_t> denseLines;
    for(unsigned i=0;i<512;++i){denseLines.push_back(0);denseLines.push_back(1);}
    Require(denseMesh.Update(pairedVertices,denseLines),denseMesh.LastError());
    auto dense=pairedLayer({1000,.5f,.125f},.2f,-.25f);dense.geometry=denseMesh.Snapshot();
    const auto densePaired=pairedDraw({dense},true),denseLegacy=pairedDraw({dense},false);
    pairedCompare(pairedPixel(densePaired),dense.material.rootColor,.6f,"FP32 coplanar HDR group lost bounded weighted color");
    Require(std::abs(pairedPixel(denseLegacy).r-1000.f)>100.f,"HDR group negative control did not exercise the FP16 accumulator overflow");
    // Independent opaque clipping and cleared-depth sentinel are preserved.
    pairedCompare(pairedPixel(pairedDraw({thin},true,false,.5f)),pairedBackground,.00001f,"Paired tail ignored opaque depth");
    auto farSentinel=thin;farSentinel.model[3].z=1;
    pairedCompare(pairedPixel(pairedDraw({farSentinel},true)),pairedBackground,.00001f,"Empty paired selected-depth sentinel accepted far-plane fragments");
    pairedView.outputTemporalData=true;pairedView.outputMotionData=true;
    const auto motionPaired=pairedDraw({equal,tiedSecond},true);
    const auto temporal=pairedPass.TemporalTextures();Require(temporal.resolvedColor.IsValid()&&temporal.coverage.IsValid(),"Paired hair temporal color/coverage missing");
    const auto covered=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(temporal.coverage);});
    Require(std::abs(covered[((Size/2)*Size+Size/2)*4]-mixedAlpha)<.003f,"Paired temporal coverage differs from group product");
    pairedView.outputTemporalData=pairedView.outputMotionData=false;
    Require(MaximumDifference(motionPaired,pairedDraw({equal,tiedSecond},true))<.001f,"Paired separate hair color path changed the scene composite");
    const auto msLegacy=pairedDraw({equal,tiedSecond},false,true),msRequested=pairedDraw({equal,tiedSecond},true,true);
    Require(MaximumDifference(msLegacy,msRequested)==0.f&&!pairedPass.PairedLayerTransparencyActive()&&pairedPass.PassCount()==3&&
        pairedPass.MultisampleTransparencyFallback(),"Paired MSAA request did not preserve the legacy weighted fallback");
    pairedView.frontLayerTransparency=false;
    Require(MaximumDifference(pairedDraw({equal},false),pairedDraw({equal},true))==0.f&&!pairedPass.PairedLayerTransparencyActive(),
        "Paired flag changed a disabled layered-transparency path");
    pairedPass.Shutdown();
    std::cout<<"Paired strand MRT: 2/3/7 depth groups+tail, ties/product coverage, one-ULP depth separation, early depth/no-feedback, thin RGB recovery, 512-fiber HDR stability, temporal identity and exact MSAA/disabled fallback passed; ordinary maxAbs="
        <<pairedMaximumOrdinaryDifference<<"; transparency passes=3N+2, groom traversals=2N+1, FP32 scratch +8B/pixel\n";
}
