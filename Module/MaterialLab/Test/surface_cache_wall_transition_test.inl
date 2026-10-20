// Lightweight, stage-separated diagnosis of stationary wall/ceiling bands.
// This is deliberately not a reference-quality or improvement threshold test.
void SurfaceCacheWallTransitionTest(MaterialLabModule& lab,const Options& options) {
    Check(options.vulkan,"Wall-transition diagnostics require Vulkan");
    auto fixture=options;fixture.sealedGiRoom=true;fixture.sdfBanquet=false;fixture.sdfClipmaps=false;
    fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);
    frame.width=options.renderSizeSet?options.benchmarkWidth:480;
    frame.height=options.renderSizeSet?options.benchmarkHeight:300;
    frame.effects.outputSize={0,0};frame.present=false;frame.screenProbeStudyHud=false;
    frame.effects.areaLights.stratifiedSamples=!options.surfaceCacheWallTransitionLegacy;
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;
    gi.enabled=true;gi.view=0;gi.reconstructionDebug=0;gi.areaLightShadows=true;
    gi.probeDiagnostics=false;gi.probeOriginDiagnostics=false;gi.collectProbeCoverage=false;
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::None;
    const auto originalGi=gi;const auto originalPlacement=placement;
    if(options.surfaceCacheWallTransitionPerformance){
        Check(!options.validation,"Wall-transition GPU timing requires --validation to be omitted");
        auto camera=SealedGiRoomCameraPreset(0,frame.OutputAspect());
        camera.view=glm::lookAt(camera.position,glm::vec3(2.7f,4.25f,-.8f),glm::vec3(0,1,0));frame.camera=camera;
        frame.effects.cameraCut=true;placement.reset=true;gi.temporal.reset=true;
        Check(lab.Render(frame),lab.LastError());
        frame.effects.cameraCut=false;placement.reset=false;gi.temporal.reset=false;
        const auto preparedDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(180);
        while(lab.ProbeSdfQualityStatistics().pending){Check(lab.Render(frame),lab.LastError());
            Check(std::chrono::steady_clock::now()<preparedDeadline,"Wall-transition performance SDF preparation timed out");}
        // Pattern changes invalidate card lighting. Cover the complete card
        // update cycle and mature GI history before starting timestamp samples.
        constexpr unsigned warm=16,count=16,pairs=4;
        const auto mean=[](const auto& values){double total=0;for(const auto v:values)total+=v;return total/double(values.size());};
        const auto quantile=[](std::vector<double> values,double q){std::sort(values.begin(),values.end());
            const auto position=q*double(values.size()-1);const auto lower=std::size_t(position),upper=std::min(lower+1,values.size()-1);
            return values[lower]+(position-double(lower))*(values[upper]-values[lower]);};
        std::cout<<"WALL TRANSITION PERF setup view=corner_lamp size="<<frame.width<<'x'<<frame.height
            <<" warm_frames="<<warm<<" measured_per_batch="<<count<<" paired_batches="<<pairs
            <<" order=AB_BA_AB_BA A=legacy B=stratified validation=0 present=0 readback=0 samples_per_axis="
            <<frame.effects.areaLights.samplesPerAxis<<" common_ray_and_atlas_budget=1\n";
        for(unsigned mode=0;mode<2;++mode){
            const char* modeName=mode?"direct_only":"gi_on";
            gi=originalGi;placement=originalPlacement;gi.enabled=mode==0;gi.areaShadows.enabled=true;
            if(mode){placement.enabled=false;placement.trace.enabled=false;}
            std::array<std::vector<double>,2> samples;
            std::array<double,pairs> pairedDeltas{};
            for(unsigned pair=0;pair<pairs;++pair){std::array<double,2> pairMean{};
                for(unsigned slot=0;slot<2;++slot){const unsigned variant=pair%2?1-slot:slot;
                    frame.effects.areaLights.stratifiedSamples=variant!=0;
                    frame.effects.cameraCut=true;placement.reset=true;gi.temporal.reset=true;
                    for(unsigned w=0;w<warm;++w){Check(lab.Render(frame),lab.LastError());
                        frame.effects.cameraCut=false;placement.reset=false;gi.temporal.reset=false;}
                    const auto timing=lab.MeasureFrames(frame,count);
                    Check(timing.gpuFrameMilliseconds.size()==count,"Wall-transition timing lost fresh per-frame GPU samples");
                    for(const auto ms:timing.gpuFrameMilliseconds)Check(std::isfinite(ms)&&ms>0,"Invalid wall-transition GPU timestamp");
                    pairMean[variant]=mean(timing.gpuFrameMilliseconds);
                    samples[variant].insert(samples[variant].end(),timing.gpuFrameMilliseconds.begin(),timing.gpuFrameMilliseconds.end());
                    std::cout<<"WALL TRANSITION PERF batch mode="<<modeName<<" pair="<<pair<<" slot="<<slot
                        <<" variant="<<(variant?"stratified":"legacy")<<" frames="<<count<<" full_gpu_mean_ms="<<pairMean[variant]<<'\n';}
                pairedDeltas[pair]=pairMean[1]-pairMean[0];}
            for(unsigned variant=0;variant<2;++variant)std::cout<<"WALL TRANSITION PERF summary mode="<<modeName
                <<" variant="<<(variant?"stratified":"legacy")<<" frames="<<samples[variant].size()
                <<" full_gpu_mean_ms="<<mean(samples[variant])<<" median_ms="<<quantile(samples[variant],.5)
                <<" p10_ms="<<quantile(samples[variant],.1)<<" p90_ms="<<quantile(samples[variant],.9)<<'\n';
            const auto delta=mean(pairedDeltas);double squares=0;for(const auto d:pairedDeltas)squares+=(d-delta)*(d-delta);
            // Four counterbalanced pair means are the independent units; a
            // t(3) interval avoids pretending 64 correlated frames are 64 trials.
            const auto halfWidth=3.182446305*std::sqrt(squares/double(pairs-1)/double(pairs));
            std::cout<<"WALL TRANSITION PERF paired mode="<<modeName<<" stratified_minus_legacy_ms="<<delta
                <<" ci95_low_ms="<<delta-halfWidth<<" ci95_high_ms="<<delta+halfWidth
                <<" delta_percent="<<100*delta/mean(samples[0])<<" independent_pairs="<<pairs<<'\n';
        }
        std::cout<<"WALL TRANSITION PERF passed: matched sampling-pattern cost only; existing contact GI settings unchanged\n";
        return;
    }
    const std::size_t pixels=std::size_t(frame.width)*frame.height;
    constexpr unsigned warmFrames=24,measureFrames=8,stageCount=6,roiCount=6;
    constexpr std::array<const char*,stageCount> stages{{"total_ldr","contact_unfiltered_hdr","contact_filtered_hdr","temporal_gi_hdr","probe_raw_gi_hdr","contact_mask"}};
    constexpr std::array<const char*,roiCount> roiNames{{"none","right_wall","front_wall","ceiling","left_wall","back_wall"}};
    constexpr std::array<const char*,3> viewNames{{"corner_close","corner_wide","corner_lamp"}};
    const glm::dvec3 luma(.2126,.7152,.0722);
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;
        placement.reset=false;gi.temporal.reset=false;};
    const auto reset=[&]{frame.effects.cameraCut=true;placement.reset=true;gi.temporal.reset=true;};
    const auto finite=[&](const auto& image,const char* label){Check(image.size()==pixels,
        std::string("Missing wall-transition ")+label+": "+lab.LastError());
        for(const auto& p:image)for(unsigned c=0;c<4;++c)Check(std::isfinite(p[c]),"Nonfinite wall-transition image");};
    const auto path=[&](const std::string& label){auto p=std::filesystem::path(options.screenshot);
        p.replace_filename(p.stem().string()+"_"+label+".png");return p.string();};
    const auto saveHdr=[&](const auto& hdr,const std::string& label,bool diagnostic=false){if(options.screenshot.empty())return;
        finite(hdr,label.c_str());FramePixels image;image.width=frame.width;image.height=frame.height;image.rgba.resize(pixels*4);
        for(std::size_t i=0;i<pixels;++i){for(unsigned c=0;c<3;++c){double v=std::max(0.,double(hdr[i][c]));
            if(!diagnostic){v*=4;v=v/(1+v);v=v<=.0031308?12.92*v:1.055*std::pow(v,1/2.4)-.055;}
            image.rgba[i*4+c]=std::uint8_t(std::lround(std::clamp(v,0.,1.)*255));}image.rgba[i*4+3]=255;}
        Save(image,path(label));};
    const auto ldr=[&](const FramePixels& image){Check(image.width==frame.width&&image.height==frame.height&&image.rgba.size()==pixels*4,
        std::string("Missing wall-transition total/direct image: ")+lab.LastError());
        std::vector<glm::vec4> values(pixels,glm::vec4(1));for(std::size_t i=0;i<pixels;++i)
            for(unsigned c=0;c<3;++c)values[i][c]=image.rgba[4*i+c]/255.f;return values;};
    struct Aggregate {double mean=0,hp=0,variance=0,selected=0;std::uint64_t count=0,hpCount=0;};
    struct Sequence {std::array<Aggregate,roiCount> regions{};std::vector<double> sum,squared;
        explicit Sequence(std::size_t n):sum(n,0),squared(n,0){};};
    for(unsigned view=0;view<3;++view){
        gi=originalGi;placement=originalPlacement;
        auto camera=SealedGiRoomCameraPreset(0,frame.OutputAspect());
        glm::vec3 target(3.65f,4.15f,3.15f);
        if(view==0)camera.position={1.7f,3.10f,1.35f};
        if(view==1)target={3.1f,4.15f,-1.8f};
        if(view==2)target={2.7f,4.25f,-.8f};
        camera.view=glm::lookAt(camera.position,target,glm::vec3(0,1,0));frame.camera=camera;
        const std::string prefix=viewNames[view];reset();for(unsigned n=0;n<warmFrames;++n)render();
        std::cout<<"WALL TRANSITION camera view="<<prefix<<" eye="<<camera.position.x<<','<<camera.position.y<<','<<camera.position.z
            <<" target="<<target.x<<','<<target.y<<','<<target.z<<" size="<<frame.width<<'x'<<frame.height
            <<" warm_frames="<<warmFrames<<" measure_frames="<<measureFrames
            <<" stratified_samples="<<frame.effects.areaLights.stratifiedSamples<<" gi_tone_exposure=4 total_direct=renderer_ldr\n";
        const auto sources=lab.ReadbackScreenProbeSources();finite(sources.sourceViewPositions,"positions");finite(sources.sourceWorldNormals,"normals");
        std::vector<unsigned char> roi(pixels,0);std::vector<glm::vec3> normals(pixels,glm::vec3(0));
        std::vector<glm::vec4> positionImage(pixels,glm::vec4(0)),normalImage(pixels,glm::vec4(0)),roiImage(pixels,glm::vec4(0));
        std::array<std::uint64_t,roiCount> roiPixels{};const auto inverse=glm::inverse(camera.view);
        const std::array<glm::vec3,roiCount> colors{{{0,0,0},{1,0,0},{0,1,0},{0,0,1},{1,1,0},{0,1,1}}};
        for(std::size_t i=0;i<pixels;++i){const auto p=sources.sourceViewPositions[i],n=sources.sourceWorldNormals[i];
            if(p.w<.5f||n.w<.5f||glm::dot(glm::vec3(n),glm::vec3(n))<.5f)continue;
            const auto w=glm::vec3(inverse*glm::vec4(glm::vec3(p),1));const auto unit=glm::normalize(glm::vec3(n));normals[i]=unit;
            positionImage[i]=glm::vec4((w-SealedGiRoomMinimum)/(SealedGiRoomMaximum-SealedGiRoomMinimum),1);
            normalImage[i]=glm::vec4(unit*.5f+.5f,1);
            unsigned char region=0;
            if(w.y>3.f&&std::abs(w.x-4.f)<.015f&&unit.x<-.99f)region=1;
            else if(w.y>3.f&&std::abs(w.z-3.5f)<.015f&&unit.z<-.99f)region=2;
            else if(std::abs(w.y-4.5f)<.015f&&unit.y<-.99f)region=3;
            else if(w.y>3.f&&std::abs(w.x+4.f)<.015f&&unit.x>.99f)region=4;
            else if(w.y>3.f&&std::abs(w.z+3.5f)<.015f&&unit.z>.99f)region=5;
            roi[i]=region;++roiPixels[region];roiImage[i]=glm::vec4(colors[region],1);}
        Check(roiPixels[1]+roiPixels[2]+roiPixels[3]+roiPixels[4]+roiPixels[5]>pixels/20,"Wall-transition view omitted wall/ceiling receivers");
        saveHdr(positionImage,prefix+"_source_world_position_normalized",true);saveHdr(normalImage,prefix+"_source_world_normal",true);
        saveHdr(roiImage,prefix+"_roi_R_right_G_front_B_ceiling_Y_left_C_back",true);
        for(unsigned r=1;r<roiCount;++r)std::cout<<"WALL TRANSITION roi view="<<prefix<<" surface="<<roiNames[r]<<" pixels="<<roiPixels[r]<<'\n';
        const auto measure=[&](Sequence& stats,const auto& image,const auto& mask){finite(image,"measured stage");
            for(unsigned y=0;y<frame.height;++y)for(unsigned x=0;x<frame.width;++x){const auto i=std::size_t(y)*frame.width+x;const auto r=roi[i];if(!r)continue;
                auto& a=stats.regions[r];const auto value=glm::dot(glm::dvec3(image[i]),luma);
                ++a.count;a.mean+=value;a.selected+=mask[i].x>.001f;stats.sum[i]+=value;stats.squared[i]+=value*value;
                if(x<2||y<2||x+2>=frame.width||y+2>=frame.height)continue;
                const std::array<std::size_t,4> taps{{i-2,i+2,i-2*frame.width,i+2*frame.width}};bool compatible=true;
                for(const auto j:taps)compatible=compatible&&roi[j]==r&&glm::dot(normals[i],normals[j])>.98f;
                if(!compatible)continue;double neighbors=0;for(const auto j:taps)neighbors+=glm::dot(glm::dvec3(image[j]),luma)*.25;
                const auto hp=value-neighbors;a.hp+=hp*hp;++a.hpCount;}};
        const auto report=[&](const char* label,const Sequence& stats){for(unsigned r=1;r<roiCount;++r){const auto& a=stats.regions[r];if(!a.count)continue;
            double variance=0;for(std::size_t i=0;i<pixels;++i)if(roi[i]==r)
                variance+=std::max(0.,stats.squared[i]-stats.sum[i]*stats.sum[i]/measureFrames)/(measureFrames-1);
            std::cout<<"WALL TRANSITION stats view="<<prefix<<" stage="<<label<<" surface="<<roiNames[r]
                <<" samples="<<a.count<<" finite=1 mean_luma="<<a.mean/a.count
                <<" spatial_hp_energy="<<a.hp/std::max<std::uint64_t>(1,a.hpCount)
                <<" stationary_variance="<<variance/std::max<std::uint64_t>(1,roiPixels[r])
                <<" selected_ratio="<<a.selected/a.count<<'\n';}};
        std::vector<Sequence> baseline;for(unsigned s=0;s<stageCount;++s)baseline.emplace_back(pixels);
        std::array<std::vector<glm::vec4>,stageCount> images;
        std::vector<glm::vec4> baselineMask;
        for(unsigned n=0;n<measureFrames;++n){render();const auto total=lab.Readback();images[0]=ldr(total);
            images[1]=lab.ReadbackSurfaceCacheContactUnfilteredGi();images[2]=lab.ReadbackSurfaceCacheContactGi();
            images[3]=lab.ReadbackSurfaceCacheTemporalGi();images[4]=lab.ReadbackSurfaceCacheRawGi();images[5]=lab.ReadbackSurfaceCacheContactMask();
            finite(images[5],"mask");for(unsigned s=0;s<stageCount;++s)measure(baseline[s],images[s],images[5]);
            if(n+1==measureFrames&&!options.screenshot.empty())Save(total,path(prefix+"_total_renderer_ldr"));}
        baselineMask=images[5];for(unsigned s=0;s<stageCount;++s){report(stages[s],baseline[s]);
            if(s)saveHdr(images[s],prefix+"_"+stages[s]+(s==5?"_diagnostic_linear":"_tone4"),s==5);}
        for(unsigned variant=0;variant<2;++variant){gi.enabled=false;gi.areaShadows.enabled=variant==0;
            placement.enabled=false;placement.trace.enabled=false;
            const char* label=variant?"direct_no_area_shadows_ldr":"direct_area_shadows_ldr";
            reset();for(unsigned n=0;n<warmFrames;++n)render();Sequence stats(pixels);
            for(unsigned n=0;n<measureFrames;++n){render();const auto image=lab.Readback();const auto values=ldr(image);measure(stats,values,baselineMask);
                if(n+1==measureFrames&&!options.screenshot.empty())Save(image,path(prefix+"_"+label));}
            report(label,stats);}
    }
    gi=originalGi;placement=originalPlacement;reset();for(unsigned n=0;n<4;++n)render();
    Check(lab.PipelineStatistics().surfaceCacheHistoryUsed,"Wall-transition cache guard lacks mature GI history");
    const auto atlasBytes=lab.PipelineStatistics().areaLightShadowBytes;
    frame.effects.areaLights.stratifiedSamples=!frame.effects.areaLights.stratifiedSamples;
    render();const auto changed=lab.PipelineStatistics();
    Check(changed.areaLightShadowsUpdated&&!changed.areaLightShadowCacheUsed&&changed.areaLightShadowBytes==atlasBytes,
        "Emitter sampling change failed to replace a same-size shadow atlas");
    Check(!changed.surfaceCacheHistoryUsed,"Emitter sampling change reused incompatible GI history");
    render();Check(lab.PipelineStatistics().areaLightShadowCacheUsed&&!lab.PipelineStatistics().areaLightShadowsUpdated,
        "Stable emitter samples failed to reuse the shadow atlas");
    gi.areaShadows.enabled=false;render();
    Check(lab.PipelineStatistics().areaLightShadowPasses==0,"Disabled runtime area shadows still rendered an atlas");
    gi.areaShadows.enabled=true;render();
    Check(lab.PipelineStatistics().areaLightShadowCacheUsed&&!lab.PipelineStatistics().areaLightShadowsUpdated,
        "Runtime area-shadow toggle discarded a compatible atlas");
    std::cout<<"WALL TRANSITION cache guards same_atlas_bytes="<<atlasBytes
        <<" pattern_change_rebuild=1 pattern_change_history_reset=1 stable_atlas_reuse=1 runtime_toggle_atlas_reuse=1\n";
    std::cout<<"WALL TRANSITION diagnostic passed: 3 views; total/direct LDR, independent GI HDR stages; no quality-improvement assertion\n";
}
