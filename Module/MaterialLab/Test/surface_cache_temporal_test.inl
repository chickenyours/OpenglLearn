// GPU checks use observed radiance, geometry and effective counts. They do not
// reproduce the reprojection or random-number algorithms on the CPU.
void SurfaceCacheTemporalQuality(MaterialLabModule& lab,const Options& options) {
    auto fixture=options;fixture.sdfBanquet=false;fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;fixture.sealedGiRoom=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);
    frame.width=256;frame.height=192;frame.effects.outputSize={0,0};frame.present=false;frame.screenProbeStudyHud=false;
    frame.camera=ProbePlacementCameraPreset(0,frame.OutputAspect());
    auto& gi=frame.effects.surfaceCacheGi;auto& temporal=gi.temporal;
    gi.contact.enabled=false; // Isolate final-gather history from actual-receiver contact retracing.
    gi.spatial.enabled=false;
    auto& placement=frame.effects.screenProbePlacement;auto& trace=placement.trace;
    gi.enabled=true;gi.useScreenProbes=true;gi.view=2;gi.probeDiagnostics=false;
    temporal.enabled=true;temporal.maxFrames=8;temporal.directionPhases=8;temporal.staticConvergence=false;temporal.neighborhoodClamp=false;
    temporal.edgeProtection=false;temporal.clampSigma=3;temporal.clampSlowMotion=false; // Keep the original accumulation baseline independent.
    temporal.youngFilter=false;temporal.retainHistoryOnObjectMotion=false; // Legacy reset/raw-equality contract, new motion path has a separate fixture.
    placement.baseSpacing=24;placement.frameIndex=0;placement.adaptive=true;placement.freeze=false;placement.reset=true;
    trace.rayResolution=4;trace.samplingSeed=0;trace.maxSteps=512;trace.sdf.maxSteps=256;
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;placement.reset=false;temporal.reset=false;};
    const auto finite=[&](const auto& data,const char* label){
        Check(data.size()==std::size_t(frame.width)*frame.height,std::string("Missing or wrong-sized ")+label+": "+lab.LastError());
        for(const auto& pixel:data)for(unsigned channel=0;channel<4;++channel)
            Check(std::isfinite(pixel[channel]),std::string("Nonfinite ")+label);
    };
    const auto maxDifference=[](const auto& a,const auto& b){
        Check(a.size()==b.size(),"Temporal radiance comparison changed extent");double error=0;
        for(std::size_t i=0;i<a.size();++i)error=std::max(error,glm::length(glm::dvec3(a[i])-glm::dvec3(b[i])));return error;
    };
    const auto energy=[](const auto& data){double sum=0;for(const auto& p:data)sum+=p.x+p.y+p.z;return sum;};
    const auto checkedHistory=[&](float allowedCap=0.f){auto history=lab.ReadbackSurfaceCacheTemporalHistory();finite(history,"GI temporal history");
        const float cap=allowedCap>0?allowedCap:float(temporal.maxFrames);
        for(const auto& p:history)Check(p.w>=0&&p.w<=cap+1e-4f,"GI effective frame count exceeded its configured cap");return history;};
    const auto expectNewHistory=[&](const char* label){
        Check(lab.PipelineStatistics().surfaceCacheTemporalPasses>0&&!lab.PipelineStatistics().surfaceCacheHistoryUsed,
            std::string(label)+" reused invalid GI history");
        const auto raw=lab.ReadbackSurfaceCacheRawGi(),accumulated=lab.ReadbackSurfaceCacheTemporalGi();finite(raw,"raw GI");finite(accumulated,"accumulated GI");
        Check(maxDifference(raw,accumulated)<3e-5,std::string(label)+" did not initialize GI history from the current frame");
        const auto history=checkedHistory();unsigned valid=0;for(const auto& p:history)if(p.w>0){++valid;Check(std::abs(p.w-1)<1e-4f,std::string(label)+" retained an old accumulated frame count");}
        Check(valid>1000,std::string(label)+" produced too few valid receiver pixels");
    };
    render();expectNewHistory("First frame");const auto first=lab.ReadbackSurfaceCacheRawGi();
    Check(energy(first)>1e-3,"Temporal fixture has no indirect light");
    const auto firstDirections=lab.ReadbackScreenProbeTrace(false).rayDirectionDepthFetches;
    render();const auto next=lab.ReadbackSurfaceCacheRawGi();
    const auto secondDirections=lab.ReadbackScreenProbeTrace(false).rayDirectionDepthFetches;
    Check(lab.PipelineStatistics().surfaceCacheHistoryUsed,"Static second frame did not reuse GI history");
    Check(maxDifference(first,next)>1e-4&&maxDifference(firstDirections,secondDirections)>.05,
        "Temporal probe rays did not change direction realization between frames");
    for(unsigned i=0;i<16;++i)render();
    const std::size_t count=std::size_t(frame.width)*frame.height;
    std::vector<glm::dvec3> rawSum(count,glm::dvec3(0)),rawSquare(count,glm::dvec3(0));
    std::vector<glm::dvec3> accumulatedSum(count,glm::dvec3(0)),accumulatedSquare(count,glm::dvec3(0));
    std::vector<glm::vec4> lastHistory;
    constexpr unsigned samples=16;
    for(unsigned sample=0;sample<samples;++sample) {
        render();const auto raw=lab.ReadbackSurfaceCacheRawGi(),accumulated=lab.ReadbackSurfaceCacheTemporalGi();
        finite(raw,"changing raw GI");finite(accumulated,"static accumulated GI");lastHistory=checkedHistory();
        for(std::size_t i=0;i<count;++i){const glm::dvec3 r(raw[i]),a(accumulated[i]);rawSum[i]+=r;rawSquare[i]+=r*r;accumulatedSum[i]+=a;accumulatedSquare[i]+=a*a;}
    }
    double rawVariance=0,accumulatedVariance=0,rawMeanEnergy=0,accumulatedMeanEnergy=0;unsigned stable=0;
    for(std::size_t i=0;i<count;++i)if(lastHistory[i].w>float(temporal.maxFrames)-.01f){
        const auto r=glm::max(rawSquare[i]/double(samples)-glm::pow(rawSum[i]/double(samples),glm::dvec3(2)),glm::dvec3(0));
        const auto a=glm::max(accumulatedSquare[i]/double(samples)-glm::pow(accumulatedSum[i]/double(samples),glm::dvec3(2)),glm::dvec3(0));
        rawVariance+=r.x+r.y+r.z;accumulatedVariance+=a.x+a.y+a.z;
        rawMeanEnergy+=(rawSum[i].x+rawSum[i].y+rawSum[i].z)/samples;
        accumulatedMeanEnergy+=(accumulatedSum[i].x+accumulatedSum[i].y+accumulatedSum[i].z)/samples;++stable;
    }
    Check(stable>1000&&rawVariance>1e-6,"Temporal variance fixture lacks stable noisy pixels");
    Check(accumulatedVariance<rawVariance*.5,"GI accumulation failed to reduce static frame-to-frame sampling variance");
    const double meanEnergyError=std::abs(accumulatedMeanEnergy-rawMeanEnergy)/std::max(rawMeanEnergy,1e-8);
    Check(meanEnergyError<.03,"Static temporal stability concealed an appreciable mean-lighting bias");
    std::cout<<"Surface Cache temporal static raw_variance="<<rawVariance/stable<<" accumulated_variance="<<accumulatedVariance/stable
        <<" reduction="<<1-accumulatedVariance/rawVariance<<" mean_energy_error="<<meanEnergyError<<" stable_pixels="<<stable<<std::endl;
    const auto save=[&](const char* label){if(options.screenshot.empty())return;
        auto path=std::filesystem::path(options.screenshot);path.replace_filename(path.stem().string()+"_"+label+path.extension().string());Save(lab.Readback(),path.string());};
    save("accumulated");gi.view=5;render();save("history");gi.view=2;

    frame.effects.cameraCut=true;render();expectNewHistory("Camera cut");
    temporal.reset=true;render();expectNewHistory("Explicit reset");
    temporal.maxFrames=1;render();expectNewHistory("One-frame setting");
    for(unsigned i=0;i<3;++i){render();const auto raw=lab.ReadbackSurfaceCacheRawGi(),accumulated=lab.ReadbackSurfaceCacheTemporalGi();
        Check(maxDifference(raw,accumulated)<3e-5,"One accumulated frame differs from raw GI");checkedHistory();}
    temporal.maxFrames=8;render();expectNewHistory("Frame-count change");render();
    temporal.directionPhases=1;render();expectNewHistory("Direction-phase change");const auto fixedPhase=lab.ReadbackSurfaceCacheRawGi();
    render();Check(maxDifference(fixedPhase,lab.ReadbackSurfaceCacheRawGi())<1e-7,"One temporal direction phase did not preserve the same raw realization");
    temporal.directionPhases=8;render();expectNewHistory("Direction phases restored");
    temporal.enabled=false;render();Check(lab.PipelineStatistics().surfaceCacheTemporalPasses==0&&!lab.PipelineStatistics().surfaceCacheHistoryUsed,"Disabled GI accumulation still used history");
    const auto disabled=lab.ReadbackSurfaceCacheRawGi();render();Check(maxDifference(disabled,lab.ReadbackSurfaceCacheRawGi())<1e-7,"Disabled temporal GI lost deterministic fixed-seed sampling");
    gi.view=5;render();Check(lab.PipelineStatistics().surfaceCacheRays==0&&lab.PipelineStatistics().surfaceCacheTemporalPasses==0,
        "Disabled history inspection traced unused probe rays");gi.view=2;
    temporal.enabled=true;render();expectNewHistory("Re-enabled accumulation");render();
    gi.enabled=false;render();gi.enabled=true;render();expectNewHistory("GI off/on");render();
    gi.view=1;render();Check(lab.PipelineStatistics().surfaceCacheTemporalPasses==0,"Cache inspection accumulated unused GI");
    gi.view=2;render();expectNewHistory("Cache-view round trip");render();
    trace.samplingSeed=19;render();expectNewHistory("Sampling seed change");render();
    trace.rayResolution=8;render();expectNewHistory("Ray count change");render();
    placement.baseSpacing=32;placement.reset=true;render();expectNewHistory("Probe spacing change");render();
    frame.width=264;frame.height=200;frame.camera=ProbePlacementCameraPreset(0,frame.OutputAspect());render();expectNewHistory("Resize");
    frame.width=256;frame.height=192;frame.camera=ProbePlacementCameraPreset(0,frame.OutputAspect());
    trace.samplingSeed=0;trace.rayResolution=4;placement.baseSpacing=24;placement.reset=true;render();expectNewHistory("Fixture restored");
    frame.effects.shadows.sunIntensity=0;render();expectNewHistory("Light disabled");
    Check(energy(lab.ReadbackSurfaceCacheRawGi())<1e-7&&energy(lab.ReadbackSurfaceCacheTemporalGi())<1e-7,"Old lit GI leaked after disabling the light");
    frame.effects.shadows.sunIntensity=6;render();expectNewHistory("Light doubled");
    auto scaled=first;for(auto& p:scaled){p.x*=2;p.y*=2;p.z*=2;}
    Check(maxDifference(scaled,lab.ReadbackSurfaceCacheTemporalGi())<5e-5,"Light edit retained stale history or broke linear GI");
    frame.effects.shadows.sunIntensity=3;render();expectNewHistory("Light restored");render();
    frame.screenProbeStudyState.occluderOffset=.6f;render();expectNewHistory("Moving geometry");render();
    frame.screenProbeStudyState.occluderOffset=0;render();expectNewHistory("Geometry restored");
    render();Check(lab.SetSurfaceCacheReceiverAlbedoForTest(0,glm::vec3(.325f)),lab.LastError());render();expectNewHistory("Receiver material changed");
    render();Check(lab.SetSurfaceCacheReceiverAlbedoForTest(0,glm::vec3(.65f)),lab.LastError());render();expectNewHistory("Receiver material restored");
    auto& spot=frame.effects.spotLight;spot.enabled=true;spot.position={0,2,4};spot.direction={0,-.1f,-1};spot.intensity=glm::vec3(30);
    render();expectNewHistory("Spot enabled");render();render();
    spot.position.x+=.3f;render();expectNewHistory("Spot translated");render();
    spot.direction={.15f,-.2f,-1};render();expectNewHistory("Spot rotated");render();
    spot.enabled=false;render();expectNewHistory("Spot disabled");

    // A modest translation should retain compatible world-space receivers,
    // while newly exposed pixels must start at one frame.
    for(unsigned i=0;i<10;++i)render();
    // Exercise the deliberately aggressive fast-update path independently of
    // the interactive preset, which may retain more history during camera motion.
    temporal.neighborhoodClamp=true;temporal.movingMaxFrames=2;temporal.motionThresholdPixels=1;render();
    for(unsigned i=0;i<8;++i)render();
    auto camera=*frame.camera;const auto translation=glm::vec3(.12f,0,0);
    camera.position+=translation;camera.view=camera.view*glm::translate(glm::mat4(1),-translation);frame.camera=camera;
    render();const auto movingHistory=checkedHistory();unsigned reused=0,newPixels=0;float movingMaximum=0;
    for(const auto& p:movingHistory)if(p.w>0){if(p.w>1.01f)++reused;else ++newPixels;movingMaximum=std::max(movingMaximum,p.w);}
    Check(lab.PipelineStatistics().surfaceCacheHistoryUsed&&reused>1000,"Camera translation discarded all compatible GI history");
    Check(newPixels>10,"Camera translation did not reject disoccluded or incompatible GI history");
    Check(movingMaximum<=temporal.movingMaxFrames+1e-4f,"Moving receivers retained more than the fast-update history cap");
    frame.effects.cameraCut=true;render();expectNewHistory("Translated camera cut");
    gi.useScreenProbes=false;placement.enabled=false;render();
    Check(lab.PipelineStatistics().surfaceCacheTemporalPasses==0&&!lab.PipelineStatistics().surfaceCacheHistoryUsed,"Pixel reference unexpectedly used adaptive-probe GI history");
    gi.useScreenProbes=true;placement.enabled=true;render();expectNewHistory("Probe gather restored");
    // GI history owns its reprojection matrices independently of optional
    // final-image TAA jitter. The interactive GI study still defaults to None.
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::TAA;render();expectNewHistory("TAA enabled");
    render();Check(lab.PipelineStatistics().surfaceCacheHistoryUsed&&lab.PipelineStatistics().historyUsed,
        "Final-image TAA jitter discarded GI history or failed to keep separate TAA history");
    std::cout<<"Surface Cache temporal TAA coexistence passed\n";
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::None;render();expectNewHistory("TAA disabled");

    // Separate the certified object-motion contract from the legacy reset
    // checks above. The camera is fixed: screen-motion heuristics alone cannot
    // accelerate a changed shadow on an otherwise stationary receiver.
    frame.camera=ProbePlacementCameraPreset(0,frame.OutputAspect());frame.effects.cameraCut=true;
    temporal.retainHistoryOnObjectMotion=true;temporal.youngFilter=false;
    temporal.neighborhoodClamp=false;temporal.edgeProtection=false;temporal.clampSlowMotion=false;
    temporal.maxFrames=8;temporal.movingMaxFrames=8;temporal.motionThresholdPixels=3;
    temporal.dynamicMaxFrames=4;temporal.dynamicClampSigma=2;temporal.dynamicSettleFrames=3;
    trace.sdf.automaticQuality=false;trace.sdf.meshQuality=1;
    frame.screenProbeStudyState.occluderOffset=0;
    render();expectNewHistory("Object-motion fixture initialized");
    for(unsigned i=0;i<12;++i)render();
    const auto stationarySource=lab.ReadbackScreenProbeSources();
    finite(stationarySource.sourceViewPositions,"object-motion source positions");
    finite(stationarySource.sourceWorldNormals,"object-motion source normals");
    const auto stationaryHistory=checkedHistory();
    unsigned matureBeforeMotion=0;for(const auto& p:stationaryHistory)if(p.w>7.9f)++matureBeforeMotion;
    Check(matureBeforeMotion>1000,"Object-motion fixture did not accumulate a mature starting history");

    // CPU scene/card updates occur before pipeline validation. A rejected
    // frame must leave the last completed GPU history and copied motion
    // snapshot intact, even though the shared SDF scene has already changed.
    frame.screenProbeStudyState.occluderOffset=.6f;
    const auto validDeltaSeconds=frame.effects.deltaSeconds;frame.effects.deltaSeconds=0;
    Check(!lab.Render(frame),"Invalid motion frame unexpectedly submitted");
    frame.effects.deltaSeconds=validDeltaSeconds;
    const auto afterRejectedFrame=checkedHistory();
    for(std::size_t i=0;i<stationaryHistory.size();++i)
        Check(glm::length(afterRejectedFrame[i]-stationaryHistory[i])<1e-7f,"Rejected frame modified completed GI history");

    const auto checkDynamicHistory=[&](const char* label,bool requireNewPixels) {
        Check(lab.PipelineStatistics().surfaceCacheHistoryUsed,std::string(label)+" discarded certified movable history");
        const auto history=checkedHistory();const auto source=lab.ReadbackScreenProbeSources();
        finite(source.sourceViewPositions,"moving source positions");finite(source.sourceWorldNormals,"moving source normals");
        unsigned staticReused=0,geometricallyReset=0;float maximum=0;
        for(std::size_t i=0;i<history.size();++i)if(history[i].w>0) {
            maximum=std::max(maximum,history[i].w);
            const auto before=stationarySource.sourceViewPositions[i],after=source.sourceViewPositions[i];
            const auto beforeNormal=stationarySource.sourceWorldNormals[i],afterNormal=source.sourceWorldNormals[i];
            if(after.w<.5f||afterNormal.w<.5f)continue;
            const bool sameSurface=before.w>.5f&&beforeNormal.w>.5f&&
                glm::length(glm::vec3(before)-glm::vec3(after))<.0001f&&
                glm::dot(glm::vec3(beforeNormal),glm::vec3(afterNormal))>.99f;
            if(sameSurface&&history[i].w>1.01f)++staticReused;
            const bool changedSurface=before.w<.5f||beforeNormal.w<.5f||
                glm::length(glm::vec3(before)-glm::vec3(after))>.1f||
                glm::dot(glm::vec3(beforeNormal),glm::vec3(afterNormal))<.9f;
            if(changedSurface&&history[i].w<=1.001f)++geometricallyReset;
        }
        Check(staticReused>1000,std::string(label)+" failed to reuse stationary world-space receivers");
        Check(maximum<=temporal.dynamicMaxFrames+1e-4f,std::string(label)+" exceeded the explicit dynamic-lighting frame cap");
        if(requireNewPixels)Check(geometricallyReset>10,std::string(label)+" failed to reject changed/disoccluded geometry");
        std::cout<<"Surface Cache temporal object_motion phase="<<label<<" stationary_reused="<<staticReused
            <<" geometry_reset="<<geometricallyReset<<" maximum_frames="<<maximum<<std::endl;
    };
    render();checkDynamicHistory("first_move_after_rejected_frame",true);
    frame.screenProbeStudyState.occluderOffset=.7f;render();checkDynamicHistory("continuous_move",true);
    for(unsigned settle=0;settle<temporal.dynamicSettleFrames;++settle) {
        render();checkDynamicHistory(("settle_"+std::to_string(settle+1)).c_str(),false);
    }
    render();const auto settledHistory=checkedHistory();unsigned resumedLongHistory=0;
    for(const auto& p:settledHistory)if(p.w>temporal.dynamicMaxFrames+.01f)++resumedLongHistory;
    Check(lab.PipelineStatistics().surfaceCacheHistoryUsed&&resumedLongHistory>1000,
        "Object-motion settling did not restore ordinary long history after its configured successful-frame interval");

    // A simultaneous movable transform cannot certify unrelated edits. These
    // changes must retain the immediate full-reset and known-black contracts.
    frame.screenProbeStudyState.occluderOffset+=.05f;frame.effects.shadows.sunIntensity=0;
    render();expectNewHistory("Motion plus light disabled");
    Check(energy(lab.ReadbackSurfaceCacheRawGi())<1e-7&&energy(lab.ReadbackSurfaceCacheTemporalGi())<1e-7,
        "Certified motion retained stale light after disabling the light");
    frame.effects.shadows.sunIntensity=3;render();expectNewHistory("Object-motion light restored");render();
    frame.screenProbeStudyState.occluderOffset+=.05f;
    Check(lab.SetSurfaceCacheReceiverAlbedoForTest(0,glm::vec3(.325f)),lab.LastError());
    render();expectNewHistory("Motion plus receiver material changed");
    Check(lab.SetSurfaceCacheReceiverAlbedoForTest(0,glm::vec3(.65f)),lab.LastError());
    render();expectNewHistory("Object-motion receiver material restored");render();
    frame.screenProbeStudyState.occluderOffset+=.05f;trace.samplingSeed=19;
    render();expectNewHistory("Motion plus seed changed");render();
    frame.screenProbeStudyState.occluderOffset+=.05f;frame.effects.cameraCut=true;
    render();expectNewHistory("Motion plus camera cut");render();
    temporal.retainHistoryOnObjectMotion=false;render();expectNewHistory("Legacy object reset restored");render();
    frame.screenProbeStudyState.occluderOffset+=.1f;render();expectNewHistory("Legacy moving geometry still resets");
    std::cout<<"Surface Cache certified object motion passed: fixed-camera reuse, geometry rejection, dynamic cap, "
        <<temporal.dynamicSettleFrames<<" successful settling frames, rejected-frame preservation, strict light/material/seed/cut resets\n";

    // The new stationary mode advances committed ray realizations independently
    // of the legacy phase count. Only an exactly unchanged camera and settled
    // scene may retain 64 frames; ordinary and certified-motion caps still apply.
    frame.camera=ProbePlacementCameraPreset(0,frame.OutputAspect());frame.effects.cameraCut=true;
    frame.screenProbeStudyState.occluderOffset=0;trace.samplingSeed=0;
    temporal.directionPhases=1;temporal.maxFrames=8;temporal.movingMaxFrames=2;
    temporal.retainHistoryOnObjectMotion=true;render();expectNewHistory("Static-convergence legacy fixture restored");
    temporal.staticConvergence=true;render();expectNewHistory("Static convergence enabled");
    const auto convergenceFirstDirections=lab.ReadbackScreenProbeTrace(false).rayDirectionDepthFetches;
    const auto convergenceFirstRaw=lab.ReadbackSurfaceCacheRawGi();
    for(unsigned i=0;i<8;++i)render();
    const auto convergenceEighthDirections=lab.ReadbackScreenProbeTrace(false).rayDirectionDepthFetches;
    Check(maxDifference(convergenceFirstDirections,convergenceEighthDirections)>.05&&
        maxDifference(convergenceFirstRaw,lab.ReadbackSurfaceCacheRawGi())>1e-4,
        "Static convergence repeated the legacy one/eight-phase ray realization");
    const auto growingHistory=checkedHistory(64);unsigned growingPixels=0;
    for(const auto& p:growingHistory)growingPixels+=p.w>float(temporal.maxFrames)+.5f;
    Check(growingPixels>1000,"Stationary convergence never grew beyond the ordinary frame cap");
    for(unsigned i=8;i<72;++i)render();
    const auto convergedHistory=checkedHistory(64);unsigned sixtyFourPixels=0;
    for(const auto& p:convergedHistory)sixtyFourPixels+=p.w>63.9f;
    Check(lab.PipelineStatistics().surfaceCacheHistoryUsed&&sixtyFourPixels>1000,
        "Stationary convergence did not accumulate a real 64-frame history");

    // This is far below the fast screen-motion threshold. Exact camera motion
    // alone must lower mature 64-frame histories to the ordinary eight-frame cap.
    auto convergenceCamera=*frame.camera;const auto tinyTranslation=glm::vec3(.0001f,0,0);
    convergenceCamera.position+=tinyTranslation;
    convergenceCamera.view=convergenceCamera.view*glm::translate(glm::mat4(1),-tinyTranslation);
    frame.camera=convergenceCamera;render();const auto tinyMotionHistory=checkedHistory();
    unsigned ordinaryCapPixels=0;for(const auto& p:tinyMotionHistory)ordinaryCapPixels+=p.w>7.9f;
    Check(lab.PipelineStatistics().surfaceCacheHistoryUsed&&ordinaryCapPixels>1000,
        "Tiny camera motion retained the static cap or incorrectly used the fast-motion cap");
    frame.camera=ProbePlacementCameraPreset(0,frame.OutputAspect());render();render();render();
    const auto resumedStaticHistory=checkedHistory(64);unsigned resumedStaticPixels=0;
    for(const auto& p:resumedStaticHistory)resumedStaticPixels+=p.w>float(temporal.maxFrames)+.5f;
    Check(resumedStaticPixels>1000,"Stopping the camera failed to resume stationary convergence");
    frame.screenProbeStudyState.occluderOffset=.6f;render();
    checkDynamicHistory("static_convergence_object_motion",true);

    // A lighting edit invalidates even a long static history. Readable black
    // remains a real measurement and can itself accumulate without stale RGB.
    frame.effects.shadows.sunIntensity=0;render();expectNewHistory("Static-convergence light disabled");render();
    const auto blackRaw=lab.ReadbackSurfaceCacheRawGi(),blackAccumulated=lab.ReadbackSurfaceCacheTemporalGi();
    const auto blackHistory=checkedHistory(64);finite(blackRaw,"static-convergence black raw GI");finite(blackAccumulated,"static-convergence black GI");
    unsigned readableBlackPixels=0;
    for(std::size_t i=0;i<blackRaw.size();++i)if(blackRaw[i].w>0) {
        Check(blackAccumulated[i].w==blackRaw[i].w&&blackHistory[i].w>=1,
            "Stationary convergence lost known-black readability or its measurement count");
        readableBlackPixels+=blackHistory[i].w>1.01f;
    }
    Check(readableBlackPixels>1000&&energy(blackRaw)<1e-7&&energy(blackAccumulated)<1e-7,
        "Stationary convergence invented light or discarded readable black measurements");

    // Unrelated captures force unavailable transport rather than known black.
    // Unknown current receivers must never inherit light, alpha or frame count.
    frame.effects.shadows.sunIntensity=3;render();expectNewHistory("Static-convergence light restored");render();
    auto unrelatedInstances=lab.PreparedProbeSdfScene()->Instances();
    for(auto& instance:unrelatedInstances)instance.localToWorld[3].x+=50;
    Render::ProbeGlobalSdfSettings tinyCache;tinyCache.counts=glm::uvec3(2);
    const auto unrelatedScene=Render::BuildProbeSdfScene(unrelatedInstances,tinyCache);
    gi.cache=Render::BuildSurfaceCache(*unrelatedScene);render();
    Check(lab.PipelineStatistics().surfaceCacheTemporalPasses>0&&!lab.PipelineStatistics().surfaceCacheHistoryUsed,
        "Static-convergence unavailable cache reused valid-cache history");
    const auto initialUnavailableRaw=lab.ReadbackSurfaceCacheRawGi(),initialUnavailableGi=lab.ReadbackSurfaceCacheTemporalGi();
    finite(initialUnavailableRaw,"initial unavailable raw GI");finite(initialUnavailableGi,"initial unavailable accumulated GI");
    Check(maxDifference(initialUnavailableRaw,initialUnavailableGi)<3e-5,
        "Static-convergence unavailable cache did not initialize from the current measurement");
    const auto initialUnavailableHistory=checkedHistory(64);
    for(const auto& p:initialUnavailableHistory)if(p.w>0)Check(std::abs(p.w-1)<1e-4f,
        "Static-convergence unavailable cache retained an old accumulated count");
    render();
    const auto unknownRaw=lab.ReadbackSurfaceCacheRawGi(),unknownAccumulated=lab.ReadbackSurfaceCacheTemporalGi();
    const auto unknownHistory=checkedHistory(64);finite(unknownRaw,"static-convergence unavailable raw GI");finite(unknownAccumulated,"static-convergence unavailable GI");
    const auto unavailableRays=lab.ReadbackScreenProbeTrace(false);unsigned unknownRays=0,unknownPixels=0;
    for(const auto& ray:unavailableRays.radianceStatus){const int status=int(std::lround(ray.w));
        if(status>0&&status!=1&&status!=5&&status!=9&&status!=10&&status!=13)++unknownRays;}
    for(std::size_t i=0;i<unknownRaw.size();++i)if(!(unknownRaw[i].w>0)) {
        ++unknownPixels;Check(unknownAccumulated[i].w==0&&unknownHistory[i].w==0&&
            glm::length(glm::vec3(unknownAccumulated[i]))<1e-7f,
            "Stationary convergence filled an unavailable current receiver from history");
    }
    Check(unknownRays>100&&unknownPixels>0&&energy(unknownRaw)<1e-5&&energy(unknownAccumulated)<1e-5,
        "Stationary convergence did not preserve the forced unavailable-transport contract");
    gi.cache.reset();render();expectNewHistory("Static-convergence cache restored");
    temporal.staticConvergence=false;render();expectNewHistory("Static convergence disabled");
    std::cout<<"Surface Cache stationary convergence passed: phase_count=1 eighth_frame_new_rays=1 growing_pixels="<<growingPixels
        <<" sixty_four_frame_pixels="<<sixtyFourPixels<<" tiny_motion_ordinary_cap_pixels="<<ordinaryCapPixels
        <<" resumed_static_pixels="<<resumedStaticPixels<<" object_motion_cap="<<temporal.dynamicMaxFrames
        <<" readable_black_pixels="<<readableBlackPixels<<" unavailable_rays="<<unknownRays<<" unavailable_current_pixels="<<unknownPixels<<std::endl;
    if(!options.screenshot.empty()){for(unsigned i=0;i<12;++i)render();Save(lab.Readback(),options.screenshot);}
    std::cout<<"Surface Cache temporal accumulation passed: varying probe phases, reduced static variance, bounded counts, camera reprojection reused="
        <<reused<<" newly_exposed="<<newPixels<<", light/geometry/settings/camera-cut resets; final-image TAA disabled.\n";
}
