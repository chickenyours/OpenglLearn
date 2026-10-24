// Included inside strand_render_gpu_test main after its draw helper exists.
{
    const auto motionSavedView=view;
    view=PipelineDetail::StrandPassView{};
    view.cameraPosition=view.previousCameraPosition={0,0,1000000};
    view.outputTemporalData=true;view.outputMotionData=true;view.motionHistoryValid=true;
    view.frontLayerTransparency=true;view.frontDepthGroups=2;
    view.currentUnjitteredViewProjection=view.previousUnjitteredViewProjection=glm::mat4(1);
    StrandMesh motionMesh(device);
    const std::array<std::uint32_t,2> motionLine{0,1},motionReversed{1,0};
    const std::array<StrandVertex,2> motionOld{{
        {{0,-.65f,-.4f,4.f/float(Size)},{0,1,0,0}},
        {{0, .65f,-.4f,4.f/float(Size)},{0,1,0,1}}
    }};
    auto motionCurrent=motionOld;for(auto& p:motionCurrent)p.positionRadius.x+=.125f;
    Require(motionMesh.Update(motionOld,motionLine),motionMesh.LastError());
    Require(!motionMesh.Snapshot()->PreviousInputMatched(),"First strand upload fabricated prior input");
    Require(motionMesh.Update(motionCurrent,motionLine,{},motionOld),motionMesh.LastError());
    Require(motionMesh.Snapshot()->PreviousInputMatched(),"Explicit matched previous strand input rejected");
    StrandRenderItem motionItem;motionItem.geometry=motionMesh.Snapshot();motionItem.guides=true;
    motionItem.previousTransformValid=true;motionItem.material.opacity=.6f;
    motionItem.material.rootColor=motionItem.material.tipColor={.7f,.2f,.1f};
    const auto motionRead=[&](RenderResourceHandle<RHITextureSpec> texture) {
        Require(texture.IsValid(),"Missing strand motion/color output");
        return OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(texture);});
    };
    float motionMaximumError=0;
    const auto motionCheck=[&](glm::vec2 delta,float expectedDepth) {
        const auto outputs=pass.TemporalTextures();
        const auto previous=motionRead(outputs.motion),coverage=motionRead(outputs.coverage);
        std::uint32_t accepted=0;
        for(std::uint32_t y=0;y<Size;++y)for(std::uint32_t x=0;x<Size;++x) {
            const auto i=std::size_t(y*Size+x)*4;
            if(coverage[i]<.2f)continue;
            Require(previous[i+3]>.99f,"Matched nearest-group strand history invalid");
            const glm::vec2 expected=(glm::vec2(x,y)+.5f)/float(Size)+delta;
            const float error=std::max({std::abs(previous[i]-expected.x),std::abs(previous[i+1]-expected.y),
                std::abs(previous[i+2]-expectedDepth)});
            motionMaximumError=std::max(motionMaximumError,error);
            Require(error<.00005f,"Previous ribbon UV/depth does not match known geometry/camera motion");
            ++accepted;
        }
        Require(accepted>50,"Motion fixture failed to cover enough independent pixels");
    };
    const auto motionCheckInvalid=[&] {
        const auto outputs=pass.TemporalTextures();const auto previous=motionRead(outputs.motion),coverage=motionRead(outputs.coverage);
        std::uint32_t covered=0;
        for(std::size_t i=0;i<previous.size();i+=4) {
            Require(previous[i+3]==0,"Invalid strand history leaked an accepted motion vector");
            if(coverage[i]>.2f)++covered;
        }
        Require(covered>50,"Invalid motion fixture lost its current visible hair");
    };
    const auto motionFirstImage=draw({motionItem});motionCheck({-.0625f,0},.3f);
    Require(pass.MotionPassCount()==2&&pass.ColourResolvePassCount()==1&&pass.TemporalPassCount()==4,
        "Strand motion/color auxiliary pass accounting");
    {
        const auto outputs=pass.TemporalTextures();const auto hairColor=motionRead(outputs.resolvedColor),coverage=motionRead(outputs.coverage);
        for(std::size_t i=0;i<hairColor.size();i+=4)if(coverage[i]>.2f) {
            Require(std::abs(hairColor[i+3]-coverage[i])<.001f,"Independent hair color and complete coverage disagree");
            for(int c=0;c<3;++c) {
                Require(std::abs(hairColor[i+c]-motionItem.material.rootColor[c])<.001f,"Resolved hair RGB is not straight scene-linear color");
                const float expected=.025f*(1.f-hairColor[i+3])+hairColor[i+c]*hairColor[i+3];
                Require(std::abs(motionFirstImage[i+c]-expected)<.001f,"Scene composite does not use the resolved hair layer exactly once");
            }
        }
    }
    view.motionHistoryValid=false;const auto motionResetImage=draw({motionItem});motionCheckInvalid();
    Require(MaximumDifference(motionFirstImage,motionResetImage)==0,"History reset changed current strand color");
    view.motionHistoryValid=true;auto motionInvalidTransform=motionItem;motionInvalidTransform.previousTransformValid=false;
    draw({motionInvalidTransform});motionCheckInvalid();
    draw({motionItem,motionInvalidTransform});motionCheckInvalid(); // all tied alpha, not just valid alpha
    StrandMesh motionTieMesh(device);auto motionTieOld=motionOld;
    for(auto& p:motionTieOld)p.positionRadius.x=-.125f;
    Require(motionTieMesh.Update(motionTieOld,motionLine),motionTieMesh.LastError());
    Require(motionTieMesh.Update(motionCurrent,motionLine,{},motionTieOld),motionTieMesh.LastError());
    auto motionTieItem=motionItem;motionTieItem.geometry=motionTieMesh.Snapshot();
    draw({motionItem,motionTieItem});motionCheck({-.09375f,0},.3f);
    const auto motionTies=motionRead(pass.TemporalTextures().motion);
    draw({motionTieItem,motionItem});Require(MaximumDifference(motionTies,motionRead(pass.TemporalTextures().motion))<.00001f,
        "Nearest equal-depth motion depends on submission order");
    // Same local positions, moving object transform.
    Require(motionMesh.Update(motionOld,motionLine,{},motionOld),motionMesh.LastError());
    motionItem.geometry=motionMesh.Snapshot();motionItem.model=glm::translate(glm::mat4(1),glm::vec3(.125f,0,0));
    draw({motionItem});motionCheck({-.0625f,0},.3f);
    // Current camera / projection moves while explicit previous geometry stays.
    motionItem.model=glm::mat4(1);view.viewProjection=glm::translate(glm::mat4(1),glm::vec3(.125f,0,0));
    draw({motionItem});motionCheck({-.0625f,0},.3f);
    // Verify raw jittered previous UV rather than stable/un-jittered UV.
    view.viewProjection=glm::translate(glm::mat4(1),glm::vec3(.03125f,0,0));
    view.previousViewProjection=glm::translate(glm::mat4(1),glm::vec3(-.015625f,0,0));
    draw({motionItem});motionCheck({-.0234375f,0},.3f);
    view.viewProjection=view.previousViewProjection=glm::mat4(1);
    auto motionDepthCurrent=motionOld;for(auto& p:motionDepthCurrent)p.positionRadius.z=-.6f;
    Require(motionMesh.Update(motionDepthCurrent,motionLine,{},motionOld),motionMesh.LastError());
    motionItem.geometry=motionMesh.Snapshot();draw({motionItem});motionCheck({0,0},.3f);
    // Missing previous input, changed connectivity and changed input count each
    // preserve a valid current upload but conservatively reject its history.
    Require(motionMesh.Update(motionCurrent,motionLine),motionMesh.LastError());
    Require(!motionMesh.Snapshot()->PreviousInputMatched(),"Missing previous input was accepted");
    motionItem.geometry=motionMesh.Snapshot();draw({motionItem});motionCheckInvalid();
    Require(motionMesh.Update(motionCurrent,motionReversed,{},motionOld),motionMesh.LastError());
    Require(!motionMesh.Snapshot()->PreviousInputMatched(),"Changed connectivity accepted previous topology");
    motionItem.geometry=motionMesh.Snapshot();draw({motionItem});motionCheckInvalid();
    Require(motionMesh.Update(motionCurrent,motionLine,{},std::span<const StrandVertex>(motionOld.data(),1)),motionMesh.LastError());
    Require(!motionMesh.Snapshot()->PreviousInputMatched(),"Mismatched previous point count was accepted");
    motionItem.geometry=motionMesh.Snapshot();draw({motionItem});motionCheckInvalid();
    Require(motionMesh.Update(motionCurrent,motionLine,{},motionOld),motionMesh.LastError());
    motionItem.geometry=motionMesh.Snapshot();const auto motionBeforeFailure=motionItem.geometry;
    auto motionNonfinite=motionOld;motionNonfinite[0].positionRadius.x=std::numeric_limits<float>::quiet_NaN();
    Require(!motionMesh.Update(motionCurrent,motionLine,{},motionNonfinite),"Nonfinite previous stream accepted");
    Require(motionMesh.Snapshot()==motionBeforeFailure,"Rejected previous upload mutated the published snapshot");
    draw({motionItem});motionCheck({-.0625f,0},.3f);
    // Recording and dropping work does not create a private previous frame.
    // The caller re-publishes the same last-submitted previous input afterward.
    {
        auto dropped=device.BeginFrame({0,Size,Size});
        pass.Record(dropped,std::span<const StrandRenderItem>(&motionItem,1),passTarget,view);
        Require(dropped.End(false),"End dropped strand motion frame");
    }
    pass.InvalidateVolume();draw({motionItem});motionCheck({-.0625f,0},.3f);
    // Disabling motion returns the legacy cost/output contract without a
    // second hair-color target or stale motion handle.
    view.outputMotionData=false;draw({motionItem});
    Require(pass.MotionPassCount()==0&&pass.ColourResolvePassCount()==0&&pass.TemporalPassCount()==1&&
        !pass.TemporalTextures().motion.IsValid()&&!pass.TemporalTextures().resolvedColor.IsValid(),
        "Disabled motion retained extra work or stale temporal outputs");
    std::cout<<"Real strand motion: maximum known UV/depth error="<<motionMaximumError
        <<"; geometry/model/camera/jitter/ties/reset/topology/drop PASS\n";
    view=motionSavedView;
}
