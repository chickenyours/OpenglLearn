// Curved contact regression: compare the previous three-pass production path
// against the current receiver filter. Receiver masks are derived only from
// world-space geometry and actual object ownership, never image brightness.
void SurfaceCacheCurvedContactQuality(MaterialLabModule& lab,const Options& options) {
    Check(options.vulkan,"Curved contact quality/timing requires Vulkan");
    auto fixture=options;fixture.sealedGiRoom=true;fixture.sdfBanquet=false;
    fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);
    frame.width=options.renderSizeSet?options.benchmarkWidth:640;
    frame.height=options.renderSizeSet?options.benchmarkHeight:400;
    frame.effects.outputSize={0,0};frame.present=false;frame.screenProbeStudyHud=false;
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::None;
    auto& gi=frame.effects.surfaceCacheGi;auto& temporal=gi.temporal;
    auto& placement=frame.effects.screenProbePlacement;auto& trace=placement.trace;
    gi.enabled=true;gi.useScreenProbes=true;gi.view=0;gi.reconstructionDebug=0;
    gi.probeDiagnostics=false;gi.probeOriginDiagnostics=false;gi.collectProbeCoverage=false;
    gi.spatial.enabled=true;temporal.enabled=true;temporal.directionPhases=8;
    gi.contact.enabled=true;gi.contact.filterRadius=1;gi.contact.filterPasses=3;
    gi.contact.temporalDirections=true;gi.contact.coverageWeightedFilter=true;
    placement.enabled=true;placement.adaptive=true;placement.cornerRefinement=true;
    placement.freeze=false;placement.frameIndex=0;placement.candidateCount=8;
    trace.cosineHemisphere=true;trace.samplingSeed=0;trace.maxSteps=512;trace.sdf.maxSteps=256;
    const auto baselineGi=gi;const auto baselinePlacement=placement;
    const std::size_t pixels=std::size_t(frame.width)*frame.height;
    const glm::dvec3 luma(.2126,.7152,.0722);
    constexpr unsigned warmup=12,measuredFrames=12,bins=5,sides=2,stages=3;
    constexpr std::array<float,bins> limits{{.01f,.03f,.06f,.15f,.35f}};
    constexpr std::array<const char*,bins> binNames{{"0to1cm","1to3cm","3to6cm","6to15cm","15to35cm"}};
    constexpr std::array<const char*,sides> sideNames{{"ellipsoid","prism"}};
    constexpr std::array<const char*,stages> stageNames{{"unfiltered","filtered","temporal"}};
    constexpr std::array<const char*,2> variantNames{{"previous","improved"}};
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;
        placement.reset=false;temporal.reset=false;};
    const auto reset=[&]{frame.effects.cameraCut=true;placement.reset=true;temporal.reset=true;};
    const auto finite=[&](const auto& image,const char* label){
        Check(image.size()==pixels,std::string("Missing curved-contact ")+label+": "+lab.LastError());
        for(const auto& p:image)for(unsigned c=0;c<4;++c)Check(std::isfinite(p[c]),"Nonfinite curved-contact image");};
    const auto outputPath=[&](const std::string& label){auto path=std::filesystem::path(options.screenshot);
        path.replace_filename(path.stem().string()+"_"+label+path.extension().string());return path.string();};
    const auto save=[&](const auto& hdr,const std::string& label,bool diagnostic=false){if(options.screenshot.empty())return;
        FramePixels image;image.width=frame.width;image.height=frame.height;image.rgba.resize(pixels*4);
        for(std::size_t i=0;i<pixels;++i){for(unsigned c=0;c<3;++c){double value=std::max(0.,double(hdr[i][c]));
            if(!diagnostic){value*=4.;value=value/(1.+value);
                value=value<=.0031308?12.92*value:1.055*std::pow(value,1./2.4)-.055;}
            image.rgba[i*4+c]=std::uint8_t(std::lround(std::clamp(value,0.,1.)*255.));}image.rgba[i*4+3]=255;}
        Save(image,outputPath(label));};
    const auto hash=[](const auto& values){std::uint64_t result=1469598103934665603ull;
        const auto* bytes=reinterpret_cast<const unsigned char*>(values.data());
        for(std::size_t i=0;i<values.size()*sizeof(values[0]);++i){result^=bytes[i];result*=1099511628211ull;}return result;};
    const auto cameraAt=[&](unsigned view){auto camera=SealedGiRoomCameraPreset(0,frame.OutputAspect());
        constexpr std::array<glm::vec3,3> eyes{{{3.15f,1.85f,-1.9f},{2.55f,1.42f,-1.9f},{2.30f,1.10f,-1.80f}}};
        constexpr std::array<glm::vec3,3> targets{{{1.45f,1.05f,-.65f},{1.48f,1.05f,-.72f},{1.48f,.93f,-.72f}}};
        camera.position=eyes[view];camera.view=glm::lookAt(camera.position,targets[view],glm::vec3(0,1,0));return camera;};
    const auto& objects=SealedGiRoomObjects();
    const auto oval=std::find_if(objects.begin(),objects.end(),[](const auto& o){return std::string(o.name)=="Porcelain upright oval";});
    const auto box=std::find_if(objects.begin(),objects.end(),[](const auto& o){return std::string(o.name)=="Porcelain tall prism";});
    Check(oval!=objects.end()&&box!=objects.end(),"Curved-contact fixture lacks its ellipsoid/prism");
    const auto ovalInverse=glm::inverse(ProbePlacementTransform(*oval,ProbePlacementSceneSettings{}));
    const auto boxInverse=glm::inverse(ProbePlacementTransform(*box,ProbePlacementSceneSettings{}));
    const auto ovalApprox=[&](glm::vec3 world){const auto local=glm::vec3(ovalInverse*glm::vec4(world,1));
        return (glm::length(local)-1.f)*std::min(oval->scale.x,std::min(oval->scale.y,oval->scale.z));};
    const auto ovalDistance=[&](glm::vec3 world){
        // Solve the closest point on the analytic ellipsoid. Scaling a sphere
        // SDF by its smallest radius mislabels centimetre bins on the long axis.
        const auto p=glm::abs(glm::dvec3(ovalInverse*glm::vec4(world,1))*glm::dvec3(oval->scale));
        const auto a=glm::dvec3(oval->scale),a2=a*a;
        if(glm::dot(p/a,p/a)<=1.)return double(ovalApprox(world));
        const auto equation=[&](double lambda){const auto q=a*p/(a2+lambda);return glm::dot(q,q)-1.;};
        double low=0,high=std::max(1.,glm::length(a*p));while(equation(high)>0)high*=2;
        for(unsigned step=0;step<40;++step){const double middle=.5*(low+high);if(equation(middle)>0)low=middle;else high=middle;}
        return glm::length(p-a2*p/(a2+.5*(low+high)));};
    const auto boxDistance=[&](glm::vec3 p){const auto q=glm::abs(glm::vec3(boxInverse*glm::vec4(p,1)))*box->scale-box->scale;
        return double(glm::length(glm::max(q,glm::vec3(0)))+std::min(std::max(q.x,std::max(q.y,q.z)),0.f));};
    struct Pose {Render::PipelineCamera camera;std::vector<glm::vec4> reference,splitDifference;
        std::vector<glm::vec3> normal;std::vector<unsigned char> region;
        std::array<std::uint64_t,sides*bins> counts{};
        std::array<double,sides*bins> referenceFloor{},referenceHpFloor{};
        std::array<std::uint64_t,sides*bins> hpCounts{};};
    const auto compatibleTaps=[&](const Pose& pose,unsigned x,unsigned y,std::array<std::size_t,4>& taps){
        constexpr unsigned radius=2;if(x<radius||y<radius||x+radius>=frame.width||y+radius>=frame.height)return false;
        const auto i=std::size_t(y)*frame.width+x;
        taps={{i-radius,i+radius,i-radius*frame.width,i+radius*frame.width}};
        for(const auto j:taps)if(pose.region[j]!=pose.region[i]||glm::dot(pose.normal[i],pose.normal[j])<.98f)return false;
        return true;};
    const auto geometry=[&](Pose& pose,const std::string& view){const auto sources=lab.ReadbackScreenProbeSources();
        finite(sources.sourceViewPositions,"source positions");finite(sources.sourceWorldNormals,"source normals");
        pose.region.assign(pixels,0);pose.normal.assign(pixels,glm::vec3(0));pose.counts={};
        const auto inverse=glm::inverse(pose.camera.view);
        for(std::size_t i=0;i<pixels;++i){const auto p=sources.sourceViewPositions[i],n=sources.sourceWorldNormals[i];
            if(p.w<.5f||n.w<.5f||glm::dot(glm::vec3(n),glm::vec3(n))<.5f)continue;
            const auto world=glm::vec3(inverse*glm::vec4(glm::vec3(p),1));pose.normal[i]=glm::normalize(glm::vec3(n));
            const double ellipse=ovalApprox(world),prism=boxDistance(world);unsigned side=sides;double distance=10;
            if(std::abs(ellipse)<.015&&std::abs(ellipse)<=std::abs(prism)){side=0;distance=prism;}
            else if(std::abs(prism)<.015){side=1;distance=ovalDistance(world);}
            if(side==sides||distance<-.015||distance>=limits.back())continue;
            distance=std::max(distance,0.);unsigned bin=0;while(bin+1<bins&&distance>=limits[bin])++bin;
            const auto region=side*bins+bin;pose.region[i]=static_cast<unsigned char>(region+1);++pose.counts[region];}
        for(unsigned side=0;side<sides;++side){std::uint64_t count=0;for(unsigned b=0;b<bins;++b)count+=pose.counts[side*bins+b];
            Check(count>50,"Curved-contact camera omitted a receiver side");}
        std::vector<glm::vec4> mask(pixels,glm::vec4(0,0,0,1));
        for(std::size_t i=0;i<pixels;++i)if(pose.region[i]){const auto region=unsigned(pose.region[i]-1);
            mask[i][region/bins]=.35f+.65f*float(region%bins)/float(bins-1);}
        save(mask,view+"_geometry_mask",true);};
    struct Error {
        double squared=0,bright=0,dark=0,bias=0,value=0,truth=0,highPass=0,variance=0;
        double selectedSquared=0,selectedHighPass=0;
        std::array<double,2> halfSquared{},halfHighPass{};
        std::uint64_t count=0,hpCount=0,varianceCount=0,unknown=0,selected=0,selectedHpCount=0;
    };
    using Errors=std::array<std::array<Error,sides*bins>,stages>;
    struct Metrics {Errors error{};std::array<std::vector<double>,stages> sum,squared;
        std::array<std::vector<glm::vec4>,stages> mean;
        std::uint64_t unknownPreserved=0,maskZeroPreserved=0,historyFrames=0;
        explicit Metrics(std::size_t count){for(unsigned s=0;s<stages;++s){sum[s].assign(count,0);squared[s].assign(count,0);
            mean[s].assign(count,glm::vec4(0));}}
    };
    const auto measure=[&](Metrics& metric,const Pose& pose,const std::array<std::vector<glm::vec4>,stages>& images,
        const std::vector<glm::vec4>& mask){
        finite(mask,"geometry contact mask");for(unsigned s=0;s<stages;++s)finite(images[s],stageNames[s]);
        for(std::size_t i=0;i<pixels;++i){
            Check(images[1][i].w==images[0][i].w,"Curved contact filter changed center readability");
            if(mask[i].x<=.000001f){Check(images[1][i]==images[0][i],"Curved contact filter changed a mask-zero receiver");++metric.maskZeroPreserved;}
            if(images[0][i].w<=0){Check(images[1][i].w<=0&&glm::length(glm::vec3(images[1][i]))<1e-7f,
                "Curved contact filter filled an unknown center");++metric.unknownPreserved;}}
        for(unsigned s=0;s<stages;++s){const auto intensity=[&](std::size_t i){return glm::dot(glm::dvec3(images[s][i]),luma);};
            const auto truth=[&](std::size_t i){return glm::dot(glm::dvec3(pose.reference[i]),luma);};
            for(unsigned y=0;y<frame.height;++y)for(unsigned x=0;x<frame.width;++x){const auto i=std::size_t(y)*frame.width+x;
                metric.mean[s][i]+=images[s][i]/float(measuredFrames);
                if(!pose.region[i])continue;const unsigned r=pose.region[i]-1;auto& e=metric.error[s][r];
                const double value=intensity(i),reference=truth(i),delta=value-reference;
                const double split=glm::dot(glm::dvec3(pose.splitDifference[i]),luma);
                e.value+=value;e.truth+=reference;e.squared+=delta*delta;e.bias+=delta;
                for(unsigned half=0;half<2;++half){const double halfDelta=delta+(half?.5:-.5)*split;
                    e.halfSquared[half]+=halfDelta*halfDelta;}
                e.bright+=std::max(delta,0.)*std::max(delta,0.);e.dark+=std::min(delta,0.)*std::min(delta,0.);
                ++e.count;e.unknown+=images[s][i].w<=0;metric.sum[s][i]+=value;metric.squared[s][i]+=value*value;
                const bool selected=mask[i].x>.001f&&mask[i].w>.5f;
                if(selected){++e.selected;e.selectedSquared+=delta*delta;}
                std::array<std::size_t,4> taps{};if(!compatibleTaps(pose,x,y,taps))continue;
                double neighboringResidual=0,neighboringSplit=0;for(const auto j:taps){neighboringResidual+=(intensity(j)-truth(j))*.25;
                    neighboringSplit+=glm::dot(glm::dvec3(pose.splitDifference[j]),luma)*.25;}
                const double hp=delta-neighboringResidual;e.highPass+=hp*hp;++e.hpCount;
                const double splitHp=split-neighboringSplit;
                for(unsigned half=0;half<2;++half){const double halfHp=hp+(half?.5:-.5)*splitHp;
                    e.halfHighPass[half]+=halfHp*halfHp;}
                if(selected){e.selectedHighPass+=hp*hp;++e.selectedHpCount;}}}
        metric.historyFrames+=lab.PipelineStatistics().surfaceCacheHistoryUsed;};
    const auto finish=[&](Metrics& metric,const Pose& pose){
        for(unsigned s=0;s<stages;++s)for(std::size_t i=0;i<pixels;++i)if(pose.region[i]){auto& e=metric.error[s][pose.region[i]-1];
            e.variance+=std::max(0.,metric.squared[s][i]-metric.sum[s][i]*metric.sum[s][i]/double(measuredFrames))/double(measuredFrames-1);
            ++e.varianceCount;}};
    const auto average=[](double value,std::uint64_t count){return value/double(std::max<std::uint64_t>(1,count));};
    const auto report=[&](const Pose& pose,const Metrics& metric,const std::string& view,unsigned variant){
        for(unsigned s=0;s<stages;++s)for(unsigned r=0;r<sides*bins;++r){const auto& e=metric.error[s][r];if(!e.count)continue;
            const double floor=average(pose.referenceFloor[r],pose.counts[r]),hpFloor=average(pose.referenceHpFloor[r],pose.hpCounts[r]);
            std::cout<<"SURFACE CACHE CURVED CONTACT error view="<<view<<" variant="<<variantNames[variant]
                <<" stage="<<stageNames[s]<<" side="<<sideNames[r/bins]<<" region="<<binNames[r%bins]
                <<" pixels="<<pose.counts[r]<<" pixel_frames="<<e.count<<" selected="<<e.selected<<" unknown="<<e.unknown
                <<" luma_mse="<<average(e.squared,e.count)<<" reference_mse_floor="<<floor
                <<" corrected_mse="<<std::max(0.,average(e.squared,e.count)-floor)
                <<" bright_mse="<<average(e.bright,e.count)<<" dark_mse="<<average(e.dark,e.count)
                <<" signed_bias="<<average(e.bias,e.count)<<" mean="<<average(e.value,e.count)<<" reference_mean="<<average(e.truth,e.count)
                <<" residual_hp_mse="<<average(e.highPass,e.hpCount)<<" reference_hp_floor="<<hpFloor
                <<" corrected_hp_mse="<<std::max(0.,average(e.highPass,e.hpCount)-hpFloor)
                <<" temporal_variance="<<average(e.variance,e.varianceCount)
                <<" selected_mse="<<average(e.selectedSquared,e.selected)<<" selected_hp_mse="<<average(e.selectedHighPass,e.selectedHpCount)
                <<" unselected="<<e.count-e.selected<<" unselected_mse="<<average(e.squared-e.selectedSquared,e.count-e.selected)
                <<" unselected_hp_mse="<<average(e.highPass-e.selectedHighPass,e.hpCount-e.selectedHpCount)
                <<" reference_noise_subtraction=quarter_split_difference"<<std::endl;}
        std::cout<<"SURFACE CACHE CURVED CONTACT invariants view="<<view<<" variant="<<variantNames[variant]
            <<" unknown_centers_preserved="<<metric.unknownPreserved<<" mask_zero_preserved="<<metric.maskZeroPreserved
            <<" mature_history_frames="<<metric.historyFrames<<" alpha_exact=1"<<std::endl;};
    const auto configure=[&](unsigned variant){gi=baselineGi;placement=baselinePlacement;
        gi.contact.geometryGuidedFilter=variant!=0;gi.contact.screenContactDetection=variant!=0;gi.contact.referenceReceiver=false;
        placement.frameIndex=0;trace.samplingSeed=0;};
    const auto prepare=[&]{reset();render();const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(180);
        while(lab.ProbeSdfQualityStatistics().pending){render();Check(std::chrono::steady_clock::now()<deadline,"Curved-contact SDF preparation timed out");}};
    if(options.surfaceCacheCurvedContactPerformance){
        Check(!options.validation,"Curved-contact performance must run without --validation");
        // Resident pipelines, counterbalanced orders, common camera/ray budget.
        // Only GPU timestamps are read. No images or diagnostic counters are
        // read in measured batches, and presentation is disabled throughout.
        frame.camera=cameraAt(options.surfaceCacheCurvedContactView<3?options.surfaceCacheCurvedContactView:0);
        configure(1);prepare();
        constexpr unsigned cycles=6,timedFrames=16,timingWarmup=8;
        constexpr std::array<std::array<unsigned,3>,cycles> order{{{{0,1,2}},{{2,1,0}},{{1,2,0}},{{0,2,1}},{{2,0,1}},{{1,0,2}}}};
        constexpr std::array<const char*,3> names{{"filter_off","previous","improved"}};
        std::array<std::vector<double>,3> frameSamples,traceSamples,filterSamples;
        std::array<std::array<double,3>,cycles> frameBatch{},traceBatch{},filterBatch{};
        for(unsigned cycle=0;cycle<cycles;++cycle)for(unsigned place=0;place<3;++place){const unsigned variant=order[cycle][place];
            configure(variant==1?0:1);if(!variant)gi.contact.filterStrength=0;
            reset();for(unsigned warm=0;warm<timingWarmup;++warm)render();
            const auto timing=lab.MeasureFrames(frame,timedFrames);
            Check(timing.gpuFrameMilliseconds.size()==timedFrames,"Curved contact timing lacks completed per-frame GPU timestamps");
            Check(std::isfinite(timing.gpuMilliseconds)&&timing.gpuMilliseconds>0,"Curved contact timing lacks a valid GPU frame interval");
            Check(timing.contactTraceFrameMilliseconds.size()==timedFrames&&std::isfinite(timing.contactTraceGpuMilliseconds)&&
                timing.contactTraceGpuMilliseconds>0,"Curved contact timing lacks the isolated contact trace GPU scope");
            Check(timing.contactFilterFrameMilliseconds.size()==timedFrames&&std::isfinite(timing.contactFilterGpuMilliseconds)&&
                (variant?timing.contactFilterGpuMilliseconds>0:timing.contactFilterGpuMilliseconds>=0),
                "Curved contact timing lacks the isolated filter GPU scope");
            frameBatch[cycle][variant]=timing.gpuMilliseconds;traceBatch[cycle][variant]=timing.contactTraceGpuMilliseconds;
            filterBatch[cycle][variant]=timing.contactFilterGpuMilliseconds;
            frameSamples[variant].insert(frameSamples[variant].end(),timing.gpuFrameMilliseconds.begin(),timing.gpuFrameMilliseconds.end());
            traceSamples[variant].insert(traceSamples[variant].end(),timing.contactTraceFrameMilliseconds.begin(),timing.contactTraceFrameMilliseconds.end());
            filterSamples[variant].insert(filterSamples[variant].end(),timing.contactFilterFrameMilliseconds.begin(),timing.contactFilterFrameMilliseconds.end());
            std::cout<<"SURFACE CACHE CURVED CONTACT perf_batch cycle="<<cycle<<" order="<<place<<" variant="<<names[variant]
                <<" gpu_ms="<<timing.gpuMilliseconds<<" trace_gpu_ms="<<timing.contactTraceGpuMilliseconds
                <<" filter_gpu_ms="<<timing.contactFilterGpuMilliseconds
                <<" elapsed_ms="<<timing.elapsedMilliseconds<<" frames="<<timedFrames<<" warmup_frames="<<timingWarmup
                <<" size="<<frame.width<<"x"<<frame.height<<" contact_rays="<<gi.contact.raysPerPixel
                <<" common_ray_budget=1 mask_coverage_may_differ=1 filter_off_retains_resolve_copy=0 filter_off_empty_scope=1 validation=0 readback=0 present=0"<<std::endl;}
        const auto quantile=[](std::vector<double> values,double fraction){if(values.empty())return 0.;
            std::sort(values.begin(),values.end());const double at=fraction*double(values.size()-1);const auto lower=std::size_t(at);
            return values[lower]+(values[std::min(lower+1,values.size()-1)]-values[lower])*(at-double(lower));};
        const auto mean=[](const std::vector<double>& values){double sum=0;for(double v:values)sum+=v;return values.empty()?0.:sum/double(values.size());};
        for(unsigned variant=0;variant<3;++variant)std::cout<<"SURFACE CACHE CURVED CONTACT perf_summary variant="<<names[variant]
            <<" gpu_median_ms="<<quantile(frameSamples[variant],.5)<<" gpu_p10_ms="<<quantile(frameSamples[variant],.1)
            <<" gpu_p90_ms="<<quantile(frameSamples[variant],.9)<<" gpu_mean_ms="<<mean(frameSamples[variant])
            <<" trace_median_ms="<<quantile(traceSamples[variant],.5)<<" trace_p10_ms="<<quantile(traceSamples[variant],.1)
            <<" trace_p90_ms="<<quantile(traceSamples[variant],.9)<<" trace_mean_ms="<<mean(traceSamples[variant])
            <<" filter_median_ms="<<quantile(filterSamples[variant],.5)<<" filter_p10_ms="<<quantile(filterSamples[variant],.1)
            <<" filter_p90_ms="<<quantile(filterSamples[variant],.9)<<" filter_mean_ms="<<mean(filterSamples[variant])
            <<" frame_samples="<<frameSamples[variant].size()<<" trace_samples="<<traceSamples[variant].size()
            <<" filter_samples="<<filterSamples[variant].size()<<std::endl;
        for(unsigned baseline=0;baseline<2;++baseline)for(unsigned scope=0;scope<3;++scope){std::vector<double> difference;
            const auto& samples=scope==0?frameBatch:(scope==1?traceBatch:filterBatch);
            for(unsigned cycle=0;cycle<cycles;++cycle)difference.push_back(samples[cycle][2]-samples[cycle][baseline]);
            std::vector<double> bootstrap;bootstrap.reserve(4096);std::uint32_t random=0x83f123abu;
            for(unsigned sample=0;sample<4096;++sample){double sum=0;for(unsigned n=0;n<cycles;++n){random^=random<<13;random^=random>>17;random^=random<<5;sum+=difference[random%cycles];}bootstrap.push_back(sum/double(cycles));}
            std::cout<<"SURFACE CACHE CURVED CONTACT perf_delta baseline="<<names[baseline]<<" variant=improved scope="<<std::array<const char*,3>{{"frame","trace","filter"}}[scope]
                <<" paired_mean_ms="<<mean(difference)<<" paired_median_ms="<<quantile(difference,.5)
                <<" bootstrap95_low_ms="<<quantile(bootstrap,.025)<<" bootstrap95_high_ms="<<quantile(bootstrap,.975)
                <<" pairs="<<cycles<<" samples_per_variant="<<cycles*timedFrames<<" fresh_frame_queries=1"<<std::endl;}
        std::cout<<"SURFACE CACHE CURVED CONTACT PERFORMANCE PASS size="<<frame.width<<"x"<<frame.height
            <<" cycles="<<cycles<<" frames_per_batch="<<timedFrames<<" separate_trace_filter_scopes=1 common_ray_budget=1 mask_coverage_may_differ=1 filter_off_retains_resolve_copy=0 filter_off_empty_scope=1 validation=0 readback=0 present=0"<<std::endl;
        return;
    }
    unsigned testedViews=0;std::uint64_t totalMaskZero=0,totalUnknown=0,totalExcludedReference=0;
    for(unsigned viewIndex=0;viewIndex<3;++viewIndex){
        if(options.surfaceCacheCurvedContactView<3&&viewIndex!=options.surfaceCacheCurvedContactView)continue;
        const std::string view=std::array<const char*,3>{{"objects","near","detail"}}[viewIndex];
        Pose pose;pose.camera=cameraAt(viewIndex);frame.camera=pose.camera;configure(1);prepare();geometry(pose,view);
        // All reference rays use the production receiver launch, including its
        // anchoring/clearance checks. The former p+n*rayBias reference measured
        // a different estimator in precisely the darkest contact pixels.
        gi.contact.referenceReceiver=true;gi.contact.raysPerPixel=32;gi.contact.filterStrength=0;
        temporal.enabled=false;gi.spatial.enabled=false;reset();
        pose.reference.assign(pixels,glm::vec4(0));pose.splitDifference.assign(pixels,glm::vec4(0));
        const unsigned referenceSeeds=options.surfaceCacheCurvedContactReferenceSeeds;
        for(unsigned seed=0;seed<referenceSeeds;++seed){trace.samplingSeed=1009+73*seed;render();
            const auto image=lab.ReadbackSurfaceCacheContactUnfilteredGi();finite(image,"receiver-origin reference");
            for(std::size_t i=0;i<pixels;++i){pose.reference[i]+=image[i]/float(referenceSeeds);
                pose.splitDifference[i]+=image[i]*((seed<referenceSeeds/2?2.f:-2.f)/float(referenceSeeds));}}
        std::array<std::uint64_t,sides> excludedReference{};
        for(std::size_t i=0;i<pixels;++i)if(pose.region[i]&&pose.reference[i].w<=0){const unsigned r=pose.region[i]-1;
            --pose.counts[r];++excludedReference[r/bins];++totalExcludedReference;pose.region[i]=0;}
        for(unsigned side=0;side<sides;++side){std::uint64_t retained=0;
            for(unsigned b=0;b<bins;++b)retained+=pose.counts[side*bins+b];
            Check(retained>0,"Curved-contact quality ROI contains no safe-reference receiver on one object side");
            std::cout<<"SURFACE CACHE CURVED CONTACT reference_roi view="<<view<<" side="<<sideNames[side]
                <<" excluded_unknown_pixels="<<excludedReference[side]<<" retained_pixels="<<retained
                <<" reference_availability_required=1 global_unknown_invariants_separate=1"<<std::endl;}
        for(unsigned y=0;y<frame.height;++y)for(unsigned x=0;x<frame.width;++x){const auto i=std::size_t(y)*frame.width+x;
            if(!pose.region[i])continue;const unsigned r=pose.region[i]-1;
            const auto split=[&](std::size_t j){return glm::dot(glm::dvec3(pose.splitDifference[j]),luma);};
            const double value=split(i);pose.referenceFloor[r]+=.25*value*value;
            std::array<std::size_t,4> taps{};if(!compatibleTaps(pose,x,y,taps))continue;
            double neighborhood=0;for(const auto j:taps)neighborhood+=.25*split(j);
            const double hp=value-neighborhood;pose.referenceHpFloor[r]+=.25*hp*hp;++pose.hpCounts[r];}
        for(unsigned r=0;r<sides*bins;++r)std::cout<<"SURFACE CACHE CURVED CONTACT reference view="<<view
            <<" side="<<sideNames[r/bins]<<" region="<<binNames[r%bins]<<" pixels="<<pose.counts[r]
            <<" spp="<<referenceSeeds*32<<" split_spp="<<referenceSeeds*16
            <<" reference_mse_floor="<<average(pose.referenceFloor[r],pose.counts[r])
            <<" reference_hp_floor="<<average(pose.referenceHpFloor[r],pose.hpCounts[r])
            <<" shared_cache=1 production_safe_origin=1 path_truth=0"<<std::endl;
        save(pose.reference,view+"_reference");
        Errors previous{};std::vector<std::uint64_t> upstreamSignatures;
        std::vector<std::vector<glm::vec4>> receiverBaseline;receiverBaseline.reserve(measuredFrames);
        std::vector<std::vector<float>> baselineMasks;baselineMasks.reserve(measuredFrames);
        for(unsigned variant=0;variant<2;++variant){configure(variant);reset();Metrics metric(pixels);
            std::array<std::vector<glm::vec4>,stages> images;double maximumReceiverDifference=0;
            std::uint64_t receiverCompared=0,receiverMaskChanged=0;
            for(unsigned f=0;f<warmup+measuredFrames;++f){render();if(f<warmup)continue;
                const auto upstream=lab.ReadbackSurfaceCacheRawGi();finite(upstream,"upstream probe GI");
                images={lab.ReadbackSurfaceCacheContactUnfilteredGi(),lab.ReadbackSurfaceCacheContactGi(),lab.ReadbackSurfaceCacheTemporalGi()};
                const auto mask=lab.ReadbackSurfaceCacheContactMask();finite(mask,"receiver sequence mask");
                if(!variant){upstreamSignatures.push_back(hash(upstream));receiverBaseline.push_back(images[0]);
                    baselineMasks.emplace_back(pixels);for(std::size_t i=0;i<pixels;++i)baselineMasks.back()[i]=mask[i].x;}
                else {Check(upstreamSignatures[f-warmup]==hash(upstream),"Curved contact filtering changed the upstream probe sampling sequence");
                    const auto& before=receiverBaseline[f-warmup];
                    const auto& previousMask=baselineMasks[f-warmup];
                    Check(images[0].size()==before.size(),"Curved contact receiver image extent changed");
                    for(std::size_t i=0;i<pixels;++i){if(std::abs(mask[i].x-previousMask[i])>1e-6f){++receiverMaskChanged;continue;}
                        ++receiverCompared;for(unsigned c=0;c<4;++c)
                            maximumReceiverDifference=std::max(maximumReceiverDifference,std::abs(double(images[0][i][c])-before[i][c]));}
                    Check(maximumReceiverDifference<=1e-6,"Curved contact filtering changed actual receiver rays beyond float resolve tolerance");}
                measure(metric,pose,images,mask);}
            finish(metric,pose);report(pose,metric,view,variant);
            std::cout<<"SURFACE CACHE CURVED CONTACT receiver_sequence view="<<view<<" variant="<<variantNames[variant]
                <<" maximum_rgba_difference="<<maximumReceiverDifference<<" tolerance=1e-6 upstream_hash_exact=1"
                <<" equal_mask_compared="<<receiverCompared<<" changed_mask_excluded="<<receiverMaskChanged<<std::endl;
            Check(metric.historyFrames==measuredFrames,"Curved-contact mature sequence lost compatible GI history");
            totalMaskZero+=metric.maskZeroPreserved;totalUnknown+=metric.unknownPreserved;
            for(unsigned s=0;s<stages;++s)save(metric.mean[s],view+"_"+variantNames[variant]+"_"+stageNames[s]+"_mean");
            if(!options.screenshot.empty())Save(lab.Readback(),outputPath(view+"_"+variantNames[variant]+"_total"));
            if(!variant){previous=metric.error;continue;}
            for(unsigned s=1;s<stages;++s){double beforeMse=0,afterMse=0,beforeHp=0,afterHp=0,floor=0,hpFloor=0,beforeVariance=0,afterVariance=0;
                std::array<double,2> beforeHalfMse{},afterHalfMse{},beforeHalfHp{},afterHalfHp{};
                std::uint64_t mseCount=0,hpCount=0;
                for(unsigned r=0;r<sides*bins;++r){beforeMse+=previous[s][r].squared;afterMse+=metric.error[s][r].squared;
                    beforeHp+=previous[s][r].highPass;afterHp+=metric.error[s][r].highPass;
                    floor+=pose.referenceFloor[r]*double(measuredFrames);hpFloor+=pose.referenceHpFloor[r]*double(measuredFrames);
                    beforeVariance+=previous[s][r].variance;afterVariance+=metric.error[s][r].variance;
                    mseCount+=metric.error[s][r].count;hpCount+=metric.error[s][r].hpCount;
                    for(unsigned half=0;half<2;++half){beforeHalfMse[half]+=previous[s][r].halfSquared[half];afterHalfMse[half]+=metric.error[s][r].halfSquared[half];
                        beforeHalfHp[half]+=previous[s][r].halfHighPass[half];afterHalfHp[half]+=metric.error[s][r].halfHighPass[half];}}
                std::cout<<"SURFACE CACHE CURVED CONTACT improvement view="<<view<<" stage="<<stageNames[s]<<" region=0to35cm"
                    <<" raw_mse_ratio="<<afterMse/std::max(beforeMse,1e-30)<<" raw_hp_ratio="<<afterHp/std::max(beforeHp,1e-30)
                    <<" corrected_mse_ratio="<<std::max(0.,afterMse-floor)/std::max(1e-30,beforeMse-floor)
                    <<" corrected_hp_ratio="<<std::max(0.,afterHp-hpFloor)/std::max(1e-30,beforeHp-hpFloor)
                    <<" temporal_variance_ratio="<<afterVariance/std::max(beforeVariance,1e-30)
                    <<" reference_floor_fraction="<<floor/std::max(beforeMse,1e-30)
                    <<" reference_hp_floor_fraction="<<hpFloor/std::max(beforeHp,1e-30)
                    <<" corrected_mse_identifiable="<<(beforeMse>1.25*floor)
                    <<" corrected_hp_identifiable="<<(beforeHp>1.25*hpFloor)<<std::endl;
                // Both outputs share each independent half reference. Its
                // additive noise floor cancels in the expected paired delta;
                // report the two deltas directly without changing guards.
                for(unsigned half=0;half<2;++half)std::cout<<"SURFACE CACHE CURVED CONTACT paired_half_reference view="<<view
                    <<" stage="<<stageNames[s]<<" region=0to35cm half="<<(half?"second":"first")<<" reference_spp="<<referenceSeeds*16
                    <<" pixel_frames="<<mseCount<<" hp_pixel_frames="<<hpCount
                    <<" previous_mse="<<average(beforeHalfMse[half],mseCount)<<" improved_mse="<<average(afterHalfMse[half],mseCount)
                    <<" mse_after_minus_before="<<average(afterHalfMse[half]-beforeHalfMse[half],mseCount)
                    <<" mse_ratio="<<afterHalfMse[half]/std::max(beforeHalfMse[half],1e-30)
                    <<" previous_hp_mse="<<average(beforeHalfHp[half],hpCount)<<" improved_hp_mse="<<average(afterHalfHp[half],hpCount)
                    <<" hp_after_minus_before="<<average(afterHalfHp[half]-beforeHalfHp[half],hpCount)
                    <<" hp_ratio="<<afterHalfHp[half]/std::max(beforeHalfHp[half],1e-30)
                    <<" same_geometry_roi=1 same_hp_operator=1 shared_reference_pair=1"<<std::endl;
                Check(afterMse<=beforeMse*1.02,"Curved contact filter increased aggregate receiver MSE");
                Check(afterHp<=beforeHp*1.02,"Curved contact filter increased aggregate residual grain");}
            for(unsigned side=0;side<sides;++side){
                double beforeNear=0,afterNear=0,referenceNear=0,beforeOuter=0,afterOuter=0,referenceOuter=0;
                std::uint64_t nearCount=0,outerCount=0;
                for(unsigned b=0;b<bins;++b){const unsigned r=side*bins+b;const auto& before=previous[2][r];const auto& after=metric.error[2][r];
                    if(b<2){beforeNear+=before.value;afterNear+=after.value;referenceNear+=after.truth;nearCount+=after.count;}
                    if(b==bins-1){beforeOuter+=before.value;afterOuter+=after.value;referenceOuter+=after.truth;outerCount+=after.count;}}
                if(!nearCount||!outerCount)continue;
                beforeNear=average(beforeNear,nearCount);afterNear=average(afterNear,nearCount);referenceNear=average(referenceNear,nearCount);
                beforeOuter=average(beforeOuter,outerCount);afterOuter=average(afterOuter,outerCount);referenceOuter=average(referenceOuter,outerCount);
                const double beforeContrast=beforeOuter-beforeNear,afterContrast=afterOuter-afterNear,referenceContrast=referenceOuter-referenceNear;
                const double beforeCoreError=std::abs(beforeNear-referenceNear),afterCoreError=std::abs(afterNear-referenceNear);
                const double coreOvershootDelta=std::max(0.,afterNear-referenceNear)-std::max(0.,beforeNear-referenceNear);
                std::cout<<"SURFACE CACHE CURVED CONTACT detail view="<<view<<" side="<<sideNames[side]
                    <<" near_region=0to3cm outer_region=15to35cm previous_near="<<beforeNear<<" improved_near="<<afterNear
                    <<" reference_near="<<referenceNear<<" previous_contrast="<<beforeContrast<<" improved_contrast="<<afterContrast
                    <<" reference_contrast="<<referenceContrast<<" previous_absolute_core_error="<<beforeCoreError
                    <<" improved_absolute_core_error="<<afterCoreError<<" core_overshoot_delta="<<coreOvershootDelta
                    <<" near_pixel_frames="<<nearCount<<" outer_pixel_frames="<<outerCount<<std::endl;
                // Judge the dark core against the same safe-origin reference:
                // correcting an overly dark estimate is valid, while added
                // bright overshoot or absolute bias must remain bounded.
                const double detailTolerance=.03*std::max(referenceNear,std::abs(referenceContrast));
                Check(coreOvershootDelta<=detailTolerance,"Curved contact filter increased bright contact-core error");
                Check(afterCoreError<=beforeCoreError+detailTolerance,"Curved contact filter increased absolute contact-core error");
                if(beforeContrast>0)Check(afterContrast>=.9*beforeContrast-detailTolerance,
                    "Curved contact filter erased the dark-contact profile");}
            const auto origin=pose.camera;const auto forward=glm::normalize(-glm::vec3(glm::inverse(origin.view)[2]));
            for(unsigned step=1;step<=3;++step){auto camera=origin;camera.position+=glm::vec3(.005f*float(step),0,0);
                camera.view=glm::lookAt(camera.position,camera.position+forward,glm::vec3(0,1,0));frame.camera=camera;render();
                finite(lab.ReadbackSurfaceCacheContactGi(),"small-motion contact GI");finite(lab.ReadbackSurfaceCacheTemporalGi(),"small-motion temporal GI");
                const auto history=lab.ReadbackSurfaceCacheTemporalHistory();finite(history,"small-motion history");
                const auto reused=std::count_if(history.begin(),history.end(),[](const auto& p){return p.w>1.5f;});
                Check(lab.PipelineStatistics().surfaceCacheHistoryUsed&&reused>pixels/3,"Curved contact filtering lost compatible history under small camera motion");
                std::cout<<"SURFACE CACHE CURVED CONTACT motion view="<<view<<" step="<<step<<" reused_pixels="<<reused<<" reference_error_measured=0"<<std::endl;}
            frame.camera=pose.camera;
        }
        ++testedViews;
    }
    unsigned resetChecks=0;
    for(unsigned parameter=0;parameter<5;++parameter){frame.camera=cameraAt(0);configure(1);reset();
        for(unsigned warm=0;warm<3;++warm)render();Check(lab.PipelineStatistics().surfaceCacheHistoryUsed,"Curved contact history-reset guard lacks populated history");
        if(parameter==0)gi.contact.geometryGuidedFilter=false;
        if(parameter==1)gi.contact.referenceReceiver=true;
        if(parameter==2)gi.contact.screenContactDetection=false;
        if(parameter==3)gi.contact.guideMaxRadius=gi.contact.guideMaxRadius==4?3:4;
        if(parameter==4)gi.contact.guideHitAngleDegrees=gi.contact.guideHitAngleDegrees==10?12:10;
        render();Check(!lab.PipelineStatistics().surfaceCacheHistoryUsed,"Curved contact configuration reused incompatible GI history");++resetChecks;}
    // One fixed-seed current-frame linearity check and emitter-off history
    // invalidation guard exercise the new filter without adding timed readback.
    frame.camera=cameraAt(0);configure(1);temporal.enabled=false;reset();render();
    const auto lit=lab.ReadbackSurfaceCacheContactGi(),litMask=lab.ReadbackSurfaceCacheContactMask();
    frame.effects.areaLights.lights[0].radiance*=2.f;reset();render();
    const auto doubled=lab.ReadbackSurfaceCacheContactGi(),doubledMask=lab.ReadbackSurfaceCacheContactMask();
    finite(lit,"linearity baseline");finite(doubled,"linearity doubled");double maximumLinearity=0;
    for(std::size_t i=0;i<pixels;++i){Check(litMask[i]==doubledMask[i],"Curved contact risk mask depends on light intensity");
        for(unsigned c=0;c<3;++c)maximumLinearity=std::max(maximumLinearity,std::abs(double(doubled[i][c])-2.*lit[i][c]));}
    Check(maximumLinearity<1e-4,"Curved contact filtering broke light intensity linearity");
    frame.effects.areaLights.lights[0].radiance*=.5f;configure(1);reset();for(unsigned warm=0;warm<12;++warm)render();
    frame.effects.areaLights.lights[0].enabled=false;render();
    const auto dark=lab.ReadbackSurfaceCacheContactGi(),darkTemporal=lab.ReadbackSurfaceCacheTemporalGi();
    finite(dark,"emitter-off contact");finite(darkTemporal,"emitter-off temporal");double maximumDark=0;
    for(std::size_t i=0;i<pixels;++i)for(unsigned c=0;c<3;++c)
        maximumDark=std::max({maximumDark,double(std::abs(dark[i][c])),double(std::abs(darkTemporal[i][c]))});
    Check(maximumDark<1e-6&&!lab.PipelineStatistics().surfaceCacheHistoryUsed,"Curved contact retained stale light after disabling the emitter");
    std::cout<<"SURFACE CACHE CURVED CONTACT linearity maximum_rgb_error="<<maximumLinearity<<" mask_unchanged=1"<<std::endl;
    std::cout<<"SURFACE CACHE CURVED CONTACT emitter_off maximum_rgb="<<maximumDark<<" history_used=0"<<std::endl;
    std::cout<<"SURFACE CACHE CURVED CONTACT history_parameter_resets="<<resetChecks<<std::endl;
    frame.effects.areaLights.lights[0].enabled=true;
    std::cout<<"SURFACE CACHE CURVED CONTACT PASS views="<<testedViews<<" variants=2 bins=5 receiver_sides=2"
        <<" warmup_frames="<<warmup<<" measured_frames="<<measuredFrames<<" reference_spp="<<options.surfaceCacheCurvedContactReferenceSeeds*32
        <<" alpha_exact=1 unknown_preserved="<<totalUnknown<<" mask_zero_preserved="<<totalMaskZero
        <<" reference_unknown_excluded="<<totalExcludedReference
        <<" upstream_phase_matched=1 equal_mask_receiver_rays_matched=1 mask_coverage_may_differ=1 size="<<frame.width<<"x"<<frame.height<<std::endl;
}
