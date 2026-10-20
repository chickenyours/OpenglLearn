// Timestamp-only comparison. This fixture creates its own scene and never
// constructs the quality fixture's primary-ray oracle or reads image textures.
void SurfaceCacheProbeImprintPerformance(MaterialLabModule& lab,const Options& options) {
    Check(options.vulkan,"Probe-imprint performance requires Vulkan");
    Check(!options.validation,"Probe-imprint performance must run without --validation");
    auto fixture=options;fixture.sealedGiRoom=true;fixture.sdfBanquet=false;
    fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);
    frame.width=options.renderSizeSet?options.benchmarkWidth:1280;
    frame.height=options.renderSizeSet?options.benchmarkHeight:800;
    frame.effects.outputSize={0,0};frame.present=false;frame.screenProbeStudyHud=false;
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::None;
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;
    auto& trace=placement.trace;
    const bool convergence=options.surfaceCacheProbeImprintConvergence;
    const char* comparison=convergence?"static_convergence":"curved_reconstruction";
    const char* changedSettings=convergence?"temporal.staticConvergence":"curvatureAwareWeights,coherentProbeRisk";
    gi.enabled=true;gi.useScreenProbes=true;gi.view=0;gi.reconstructionDebug=0;
    gi.probeDiagnostics=false;gi.probeOriginDiagnostics=false;gi.collectProbeCoverage=false;
    gi.spatial.enabled=true;gi.temporal.enabled=true;gi.contact.enabled=true;
    gi.contact.referenceReceiver=false;gi.contact.raysPerPixel=8;
    placement.enabled=true;placement.adaptive=true;placement.cornerRefinement=true;
    placement.freeze=false;placement.frameIndex=0;placement.candidateCount=8;
    trace.cosineHemisphere=true;trace.rayResolution=8;trace.samplingSeed=0;
    trace.maxSteps=512;trace.sdf.maxSteps=256;
    auto camera=SealedGiRoomCameraPreset(0,frame.OutputAspect());
    camera.position={2.55f,1.42f,-1.9f};
    camera.view=glm::lookAt(camera.position,glm::vec3(1.48f,1.05f,-.72f),glm::vec3(0,1,0));
    frame.camera=camera;
    const auto baselineGi=gi;const auto baselinePlacement=placement;
    const auto configure=[&](unsigned variant){
        gi=baselineGi;placement=baselinePlacement;
        placement.curvatureAwareWeights=convergence||variant!=0;gi.contact.coherentProbeRisk=convergence||variant!=0;
        // Preserve the former 16-frame-warm comparison when this mode is off.
        // The convergence experiment instead changes this one setting only.
        gi.temporal.staticConvergence=convergence&&variant!=0;
        frame.effects.cameraCut=true;placement.reset=true;gi.temporal.reset=true;
    };
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());
        frame.effects.cameraCut=false;placement.reset=false;gi.temporal.reset=false;};
    // Exclude initial SDF preparation, first card lighting, shader creation and
    // the cached emitter shadow atlas. Every measured batch also warms itself.
    configure(1);render();
    const auto preparedDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(180);
    while(lab.ProbeSdfQualityStatistics().pending){render();
        Check(std::chrono::steady_clock::now()<preparedDeadline,"Probe-imprint performance SDF preparation timed out");}
    constexpr unsigned timedFrames=16,pairs=4,variants=2,scopes=3;
    const unsigned warmFrames=convergence?64:16;
    constexpr std::array<const char*,variants> names{{"previous","improved"}};
    constexpr std::array<const char*,scopes> scopeNames{{"frame","trace","filter"}};
    const auto mean=[](const auto& values){double sum=0;for(const auto v:values)sum+=v;return sum/double(values.size());};
    const auto quantile=[](std::vector<double> values,double fraction){
        Check(!values.empty(),"Probe-imprint performance has no quantile samples");
        std::sort(values.begin(),values.end());const double at=fraction*double(values.size()-1);
        const auto lower=std::size_t(at),upper=std::min(lower+1,values.size()-1);
        return values[lower]+(at-double(lower))*(values[upper]-values[lower]);
    };
    std::array<std::array<std::vector<double>,scopes>,variants> samples;
    std::array<std::array<double,pairs>,scopes> pairedDeltas{};
    std::cout<<"PROBE IMPRINT PERF setup view=contact_near eye=2.55,1.42,-1.9 target=1.48,1.05,-.72 size="
        <<frame.width<<'x'<<frame.height<<" comparison="<<comparison
        <<" warm_frames="<<warmFrames<<" measured_per_batch="<<timedFrames
        <<" paired_batches="<<pairs<<" order=AB_BA_AB_BA A=previous B=improved"
        <<" per_probe_rays=64 per_selected_receiver_rays=8 candidate_slots_per_tile="<<placement.candidateCount
        <<" base_spacing="<<placement.baseSpacing<<" active_probe_count_may_differ=1 mask_coverage_may_differ=1"
        <<" common_total_ray_budget=0 changed_settings="<<changedSettings
        <<" A_static_convergence=0 B_static_convergence="<<(convergence?1:0)
        <<" A_curved_weights="<<(convergence?1:0)<<" B_curved_weights=1"
        <<" A_coherent_probe_risk="<<(convergence?1:0)<<" B_coherent_probe_risk=1"
        <<" validation=0 present=0 image_readback=0 cpu_oracle=0 preparation_timed=0"<<std::endl;
    for(unsigned pair=0;pair<pairs;++pair){
        std::array<std::array<double,scopes>,variants> pairMeans{};
        for(unsigned slot=0;slot<variants;++slot){
            const unsigned variant=pair%2?1-slot:slot;
            configure(variant);for(unsigned warm=0;warm<warmFrames;++warm)render();
            Check(!lab.ProbeSdfQualityStatistics().pending,"Probe-imprint timing began before SDF preparation finished");
            Check(lab.PipelineStatistics().surfaceCacheHistoryUsed,"Probe-imprint performance lacks mature GI history");
            const auto timing=lab.MeasureFrames(frame,timedFrames,0.f);
            const std::array<const std::vector<double>*,scopes> batchSamples{{
                &timing.gpuFrameMilliseconds,&timing.contactTraceFrameMilliseconds,&timing.contactFilterFrameMilliseconds}};
            for(unsigned scope=0;scope<scopes;++scope){
                const auto& batch=*batchSamples[scope];
                // MeasureFrames rejects stale frame indices; requiring all 16
                // entries also rejects unavailable or partially missing scopes.
                Check(batch.size()==timedFrames,std::string("Probe-imprint timing lacks fresh ")+scopeNames[scope]+" GPU timestamps");
                for(const auto ms:batch)Check(std::isfinite(ms)&&ms>0,
                    std::string("Invalid probe-imprint ")+scopeNames[scope]+" GPU timestamp");
                pairMeans[variant][scope]=mean(batch);
                auto& all=samples[variant][scope];all.insert(all.end(),batch.begin(),batch.end());
            }
            Check(!lab.ProbeSdfQualityStatistics().pending,"SDF preparation entered a probe-imprint measured batch");
            std::cout<<"PROBE IMPRINT PERF batch pair="<<pair<<" slot="<<slot<<" variant="<<names[variant]
                <<" comparison="<<comparison<<" static_convergence="<<gi.temporal.staticConvergence
                <<" frames="<<timedFrames<<" full_gpu_mean_ms="<<pairMeans[variant][0]
                <<" trace_gpu_mean_ms="<<pairMeans[variant][1]<<" filter_gpu_mean_ms="<<pairMeans[variant][2]
                <<" warm_frames="<<warmFrames<<" fresh_frame_queries=1"<<std::endl;
        }
        for(unsigned scope=0;scope<scopes;++scope)pairedDeltas[scope][pair]=pairMeans[1][scope]-pairMeans[0][scope];
    }
    for(unsigned variant=0;variant<variants;++variant)for(unsigned scope=0;scope<scopes;++scope){
        const auto& values=samples[variant][scope];
        Check(values.size()==pairs*timedFrames,"Probe-imprint performance did not collect 64 fresh samples per scope/configuration");
        std::cout<<"PROBE IMPRINT PERF summary variant="<<names[variant]<<" scope="<<scopeNames[scope]<<" comparison="<<comparison
            <<" frames="<<values.size()<<" gpu_mean_ms="<<mean(values)<<" median_ms="<<quantile(values,.5)
            <<" p10_ms="<<quantile(values,.1)<<" p90_ms="<<quantile(values,.9)<<std::endl;
    }
    for(unsigned scope=0;scope<scopes;++scope){
        const auto delta=mean(pairedDeltas[scope]);double squares=0;
        for(const auto value:pairedDeltas[scope])squares+=(value-delta)*(value-delta);
        // Counterbalanced pair means are the independent units. Use t(3),
        // without treating the 64 correlated frames as independent trials.
        const auto halfWidth=3.182446305*std::sqrt(squares/double(pairs-1)/double(pairs));
        std::cout<<"PROBE IMPRINT PERF paired scope="<<scopeNames[scope]<<" comparison="<<comparison<<" improved_minus_previous_ms="<<delta
            <<" ci95_low_ms="<<delta-halfWidth<<" ci95_high_ms="<<delta+halfWidth
            <<" delta_percent="<<100*delta/mean(samples[0][scope])<<" independent_pairs="<<pairs
            <<" student_t_df=3 samples_per_variant="<<pairs*timedFrames<<std::endl;
    }
    std::cout<<"PROBE IMPRINT PERF PASS size="<<frame.width<<'x'<<frame.height<<" comparison="<<comparison<<" changed_settings="<<changedSettings
        <<" separate_frame_trace_filter_scopes=1 fresh_frame_queries=1 active_probe_count_may_differ=1"
        <<" mask_coverage_may_differ=1 common_total_ray_budget=0 validation=0 present=0 image_readback=0 cpu_oracle=0"<<std::endl;
}
