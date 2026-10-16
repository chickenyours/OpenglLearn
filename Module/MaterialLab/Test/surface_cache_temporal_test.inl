// GPU checks use observed radiance, geometry and effective counts. They do not
// reproduce the reprojection or random-number algorithms on the CPU.
void SurfaceCacheTemporalQuality(MaterialLabModule& lab,const Options& options) {
    auto fixture=options;fixture.sdfBanquet=false;fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);
    frame.width=256;frame.height=192;frame.effects.outputSize={0,0};frame.present=false;frame.screenProbeStudyHud=false;
    frame.camera=ProbePlacementCameraPreset(0,frame.OutputAspect());
    auto& gi=frame.effects.surfaceCacheGi;auto& temporal=gi.temporal;
    auto& placement=frame.effects.screenProbePlacement;auto& trace=placement.trace;
    gi.enabled=true;gi.useScreenProbes=true;gi.view=2;gi.probeDiagnostics=false;
    temporal.enabled=true;temporal.maxFrames=8;temporal.directionPhases=8;temporal.neighborhoodClamp=false;
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
    const auto checkedHistory=[&]{auto history=lab.ReadbackSurfaceCacheTemporalHistory();finite(history,"GI temporal history");
        for(const auto& p:history)Check(p.w>=0&&p.w<=float(temporal.maxFrames)+1e-4f,"GI effective frame count exceeded its configured cap");return history;};
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
    temporal.neighborhoodClamp=true;render();
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
    if(!options.screenshot.empty()){for(unsigned i=0;i<12;++i)render();Save(lab.Readback(),options.screenshot);}
    std::cout<<"Surface Cache temporal accumulation passed: varying probe phases, reduced static variance, bounded counts, camera reprojection reused="
        <<reused<<" newly_exposed="<<newPixels<<", light/geometry/settings/camera-cut resets; final-image TAA disabled.\n";
}
