// Regression for bright granular strips at real geometric contacts. Regions
// come from visible world-space geometry, never from the contact risk mask or
// output brightness. The 1024-spp per-pixel reference shares the single-bounce
// Surface Cache; it is a transport comparison, not path-traced ground truth.
void SurfaceCacheContactNoiseQuality(MaterialLabModule& lab,const Options& options) {
    Check(options.vulkan,"Contact noise quality needs Vulkan for its per-pixel reference");
    auto fixture=options;fixture.sealedGiRoom=true;fixture.sdfBanquet=false;
    fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);
    frame.width=options.renderSizeSet?options.benchmarkWidth:480;
    frame.height=options.renderSizeSet?options.benchmarkHeight:300;
    frame.effects.outputSize={0,0};frame.present=false;frame.screenProbeStudyHud=false;
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::None;
    auto& gi=frame.effects.surfaceCacheGi;auto& temporal=gi.temporal;
    auto& spatial=gi.spatial;auto& placement=frame.effects.screenProbePlacement;auto& trace=placement.trace;
    gi.enabled=true;gi.useScreenProbes=true;gi.view=0;gi.reconstructionDebug=0;
    gi.probeDiagnostics=false;gi.probeOriginDiagnostics=false;gi.collectProbeCoverage=false;
    placement.enabled=true;placement.adaptive=true;placement.cornerRefinement=true;
    placement.freeze=false;placement.frameIndex=0;placement.candidateCount=8;
    trace.cosineHemisphere=true;trace.samplingSeed=0;trace.maxSteps=512;trace.sdf.maxSteps=256;
    spatial.enabled=true;temporal.enabled=true;temporal.directionPhases=8;gi.contact.enabled=true;
    const auto baselineGi=gi;const auto baselinePlacement=placement;
    const std::size_t pixels=std::size_t(frame.width)*frame.height;
    const glm::dvec3 luma(.2126,.7152,.0722);
    constexpr unsigned warmup=12,measuredFrames=16,referenceSeeds=32,referenceSpp=referenceSeeds*32;
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;
        placement.reset=false;temporal.reset=false;};
    const auto reset=[&]{frame.effects.cameraCut=true;placement.reset=true;temporal.reset=true;};
    const auto finite=[&](const auto& image,const char* label){
        Check(image.size()==pixels,std::string("Missing contact-noise ")+label+": "+lab.LastError());
        for(const auto& p:image)for(unsigned c=0;c<4;++c)Check(std::isfinite(p[c]),"Nonfinite contact-noise image");};
    const auto outputPath=[&](const std::string& label){auto path=std::filesystem::path(options.screenshot);
        path.replace_filename(path.stem().string()+"_"+label+path.extension().string());return path.string();};
    const auto save=[&](const auto& hdr,const std::string& label,bool diagnostic=false){if(options.screenshot.empty())return;
        FramePixels image;image.width=frame.width;image.height=frame.height;image.rgba.resize(pixels*4);
        for(std::size_t i=0;i<pixels;++i){for(unsigned c=0;c<3;++c){double value=std::max(0.,double(hdr[i][c]));
            if(!diagnostic){value*=4.;value=value/(1.+value);
                value=value<=.0031308?12.92*value:1.055*std::pow(value,1./2.4)-.055;}
            image.rgba[i*4+c]=std::uint8_t(std::lround(std::clamp(value,0.,1.)*255.));}image.rgba[i*4+3]=255;}
        Save(image,outputPath(label));};
    const auto hash=[](const auto& values){std::uint64_t value=1469598103934665603ull;
        const auto* bytes=reinterpret_cast<const unsigned char*>(values.data());
        for(std::size_t i=0;i<values.size()*sizeof(values[0]);++i){value^=bytes[i];value*=1099511628211ull;}return value;};
    const auto& objects=SealedGiRoomObjects();
    const auto oval=std::find_if(objects.begin(),objects.end(),[](const auto& o){return std::string(o.name)=="Porcelain upright oval";});
    const auto box=std::find_if(objects.begin(),objects.end(),[](const auto& o){return std::string(o.name)=="Porcelain tall prism";});
    Check(oval!=objects.end()&&box!=objects.end(),"Contact-noise fixture lacks the ellipsoid/prism contact");
    const auto ovalInverse=glm::inverse(ProbePlacementTransform(*oval,ProbePlacementSceneSettings{}));
    const auto boxInverse=glm::inverse(ProbePlacementTransform(*box,ProbePlacementSceneSettings{}));
    const auto ovalDistance=[&](glm::vec3 p){const auto local=glm::vec3(ovalInverse*glm::vec4(p,1));
        return (glm::length(local)-1.f)*std::min(oval->scale.x,std::min(oval->scale.y,oval->scale.z));};
    const auto boxDistance=[&](glm::vec3 p){const auto q=glm::abs(glm::vec3(boxInverse*glm::vec4(p,1)))*box->scale-box->scale;
        return glm::length(glm::max(q,glm::vec3(0)))+std::min(std::max(q.x,std::max(q.y,q.z)),0.f);};
    struct Pose {Render::PipelineCamera camera;std::vector<glm::vec4> reference;
        std::vector<glm::vec3> normal;std::vector<unsigned char> region;std::array<std::uint64_t,4> counts{};};
    const auto cameraAt=[&](unsigned view){auto camera=SealedGiRoomCameraPreset(0,frame.OutputAspect());
        camera.position=view?glm::vec3(3.15f,1.85f,-1.9f):glm::vec3(1.7f,3.10f,1.35f);
        const auto target=view?glm::vec3(1.45f,1.05f,-.65f):glm::vec3(3.65f,4.15f,3.15f);
        camera.view=glm::lookAt(camera.position,target,glm::vec3(0,1,0));return camera;};
    const auto geometry=[&](Pose& pose,unsigned view){const auto sources=lab.ReadbackScreenProbeSources();
        finite(sources.sourceViewPositions,"source positions");finite(sources.sourceWorldNormals,"source normals");
        pose.region.assign(pixels,0);pose.normal.assign(pixels,glm::vec3(0));pose.counts={};
        const auto inverse=glm::inverse(pose.camera.view);
        for(std::size_t i=0;i<pixels;++i){const auto p=sources.sourceViewPositions[i],n=sources.sourceWorldNormals[i];
            if(p.w<.5f||n.w<.5f||glm::dot(glm::vec3(n),glm::vec3(n))<.5f)continue;
            const auto w=glm::vec3(inverse*glm::vec4(glm::vec3(p),1));const auto unit=glm::normalize(glm::vec3(n));
            pose.normal[i]=unit;float distance=10;
            if(!view){const bool right=std::abs(w.x-4.f)<.015f&&unit.x<-.99f;
                const bool front=std::abs(w.z-3.5f)<.015f&&unit.z<-.99f;
                const bool ceiling=std::abs(w.y-4.5f)<.015f&&unit.y<-.99f;
                if(w.x>2.8f&&w.y>3.3f&&w.z>2.3f){
                    if(right)distance=std::min(4.5f-w.y,3.5f-w.z);
                    if(front)distance=std::min(4.5f-w.y,4.f-w.x);
                    if(ceiling)distance=std::min(4.f-w.x,3.5f-w.z);}}
            else {const float ellipse=ovalDistance(w),prism=boxDistance(w);
                if(std::abs(ellipse)<.015f)distance=prism;
                else if(std::abs(prism)<.015f)distance=ellipse;}
            unsigned char r=0;
            if(distance>=-.015f&&distance<.06f)r=1;
            else if(distance>=.06f&&distance<.35f)r=2;
            else if(distance>=.4f&&distance<.95f)r=3;
            pose.region[i]=r;if(r)++pose.counts[r];}
        Check(pose.counts[1]>10&&pose.counts[2]>30&&pose.counts[3]>30,
            "Contact-noise camera omitted near contact, outer band or interior receivers");
        std::vector<glm::vec4> mask(pixels,glm::vec4(0,0,0,1));for(std::size_t i=0;i<pixels;++i){
            if(pose.region[i])mask[i][pose.region[i]-1]=1;}
        save(mask,view?"objects_geometry_mask":"corner_geometry_mask",true);};
    struct Error {double squared=0,bright=0,dark=0,bias=0,value=0,truth=0,highPass=0,variance=0;
        std::uint64_t count=0,highPassCount=0,varianceCount=0,unknown=0;};
    struct Metrics {std::array<std::array<Error,4>,3> errors{};
        std::array<std::vector<double>,3> sum,squared;
        std::uint64_t selected=0,selectedKnown=0,unknownCenters=0,historyReused=0;
        Metrics(std::size_t count){for(unsigned s=0;s<3;++s){sum[s].assign(count,0);squared[s].assign(count,0);}}};
    const auto measure=[&](Metrics& metrics,const Pose& pose,const std::array<std::vector<glm::vec4>,3>& images){
        for(unsigned s=0;s<images.size();++s){const auto& image=images[s];finite(image,"measured GI");
            const auto intensity=[&](std::size_t i){return glm::dot(glm::dvec3(image[i]),luma);};
            const auto truth=[&](std::size_t i){return glm::dot(glm::dvec3(pose.reference[i]),luma);};
            for(unsigned y=0;y<frame.height;++y)for(unsigned x=0;x<frame.width;++x){const auto i=std::size_t(y)*frame.width+x;
                const auto r=pose.region[i];if(!r)continue;auto& e=metrics.errors[s][r];
                const double value=intensity(i),reference=truth(i),delta=value-reference;
                e.value+=value;e.truth+=reference;e.squared+=delta*delta;e.bias+=delta;
                e.bright+=std::max(delta,0.)*std::max(delta,0.);e.dark+=std::min(delta,0.)*std::min(delta,0.);
                ++e.count;e.unknown+=image[i].w<=0;metrics.sum[s][i]+=value;metrics.squared[s][i]+=value*value;
                constexpr unsigned radius=2;
                if(x<radius||y<radius||x+radius>=frame.width||y+radius>=frame.height)continue;
                const std::array<std::size_t,4> taps{{i-radius,i+radius,i-radius*frame.width,i+radius*frame.width}};
                bool compatible=true;for(const auto j:taps)compatible=compatible&&pose.region[j]==r&&
                    glm::dot(pose.normal[i],pose.normal[j])>.98f;
                if(!compatible)continue;
                double neighboringResidual=0;for(const auto j:taps)neighboringResidual+=(intensity(j)-truth(j))*.25;
                const double hp=delta-neighboringResidual;e.highPass+=hp*hp;++e.highPassCount;}}};
    constexpr std::array<const char*,3> variantNames{{"legacy","sampling_only","production"}};
    constexpr std::array<const char*,3> stageNames{{"contact_unfiltered","contact_filtered","temporal"}};
    constexpr std::array<const char*,4> regionNames{{"none","contact_0to6cm","band_6to35cm","interior_40to95cm"}};
    unsigned views=0,comparisons=0;
    for(unsigned viewIndex=0;viewIndex<2;++viewIndex){const std::string view=viewIndex?"objects":"corner";
        Pose pose;pose.camera=cameraAt(viewIndex);frame.camera=pose.camera;
        gi=baselineGi;placement=baselinePlacement;reset();render();
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(180);
        while(lab.ProbeSdfQualityStatistics().pending){render();Check(std::chrono::steady_clock::now()<deadline,"Contact-noise SDF preparation timed out");}
        geometry(pose,viewIndex);
        gi.useScreenProbes=false;placement.enabled=false;gi.raysPerPixel=32;spatial.enabled=false;temporal.enabled=false;reset();
        pose.reference.assign(pixels,glm::vec4(0));std::vector<glm::vec4> half(pixels,glm::vec4(0));
        for(unsigned seed=0;seed<referenceSeeds;++seed){trace.samplingSeed=1009+73*seed;render();
            const auto image=lab.ReadbackSurfaceCacheRawGi();finite(image,"1024-spp reference");
            for(std::size_t i=0;i<pixels;++i){pose.reference[i]+=image[i]/float(referenceSeeds);
                half[i]+=image[i]*((seed<referenceSeeds/2?2.f:-2.f)/float(referenceSeeds));}}
        std::array<double,4> uncertainty{};for(std::size_t i=0;i<pixels;++i)if(const auto r=pose.region[i]){
            const double value=glm::dot(glm::dvec3(half[i]),luma);uncertainty[r]+=value*value;}
        for(unsigned r=1;r<4;++r)std::cout<<"SURFACE CACHE CONTACT NOISE reference view="<<view<<" region="<<regionNames[r]
            <<" pixels="<<pose.counts[r]<<" spp="<<referenceSpp<<" seeds="<<referenceSeeds
            <<" half_difference_mse="<<uncertainty[r]/double(pose.counts[r])
            <<" shared_cache=1 path_truth=0"<<std::endl;
        save(pose.reference,view+"_reference");
        std::vector<std::uint64_t> baselineHashes;std::array<std::array<Error,4>,3> legacyErrors{};
        for(unsigned variant=0;variant<variantNames.size();++variant){gi=baselineGi;placement=baselinePlacement;
            gi.contact.filterPasses=variant==2?3:1;gi.contact.temporalDirections=variant!=0;
            gi.contact.coverageWeightedFilter=variant==2;
            gi.contact.geometryGuidedFilter=variant==2;
            gi.contact.screenContactDetection=variant==2;
            placement.frameIndex=0;trace.samplingSeed=0;reset();Metrics metric(pixels);
            std::array<std::vector<glm::vec4>,3> images;std::uint64_t firstContactHash=0;
            for(unsigned f=0;f<warmup+measuredFrames;++f){render();
                if(f<warmup)continue;
                const auto raw=lab.ReadbackSurfaceCacheRawGi();finite(raw,"upstream probe GI");
                const auto signature=hash(raw);
                if(variant==0)baselineHashes.push_back(signature);
                else Check(signature==baselineHashes[f-warmup],"Contact denoising changed the upstream probe sampling phase");
                images={lab.ReadbackSurfaceCacheContactUnfilteredGi(),lab.ReadbackSurfaceCacheContactGi(),lab.ReadbackSurfaceCacheTemporalGi()};
                if(f==warmup)firstContactHash=hash(images[0]);
                if(f==warmup+temporal.directionPhases){const bool repeated=firstContactHash==hash(images[0]);
                    Check(variant==0?repeated:!repeated,
                        "Contact ray sequence did not match its legacy or independent temporal sampling contract");
                    std::cout<<"SURFACE CACHE CONTACT NOISE phase view="<<view<<" variant="<<variantNames[variant]
                        <<" frame_a="<<warmup<<" frame_b="<<f<<" probe_cycle="<<temporal.directionPhases
                        <<" contact_repeated="<<repeated<<std::endl;}
                measure(metric,pose,images);
                const auto mask=lab.ReadbackSurfaceCacheContactMask();finite(mask,"contact risk mask");
                for(std::size_t i=0;i<pixels;++i){
                    Check(images[1][i].w==images[0][i].w,"Contact noise filter changed center availability");
                    if(mask[i].x<=.000001f)Check(images[1][i]==images[0][i],
                        "Contact noise filter changed an unselected receiver");
                    if(!pose.region[i])continue;
                    if(mask[i].w>.5f&&mask[i].x>.001f){++metric.selected;metric.selectedKnown+=images[0][i].w>0;}
                    if(images[0][i].w<=0){++metric.unknownCenters;
                        Check(images[1][i].w<=0&&glm::length(glm::vec3(images[1][i]))<1e-7f,
                            "Contact noise filter filled an unavailable center");}}
                metric.historyReused+=lab.PipelineStatistics().surfaceCacheHistoryUsed;}
            for(unsigned s=0;s<3;++s)for(std::size_t i=0;i<pixels;++i)if(const auto r=pose.region[i]){auto& e=metric.errors[s][r];
                e.variance+=std::max(0.,metric.squared[s][i]-metric.sum[s][i]*metric.sum[s][i]/double(measuredFrames))/double(measuredFrames-1);
                ++e.varianceCount;}
            for(unsigned s=0;s<3;++s)for(unsigned r=1;r<4;++r){const auto& e=metric.errors[s][r];
                std::cout<<"SURFACE CACHE CONTACT NOISE error view="<<view<<" variant="<<variantNames[variant]
                    <<" stage="<<stageNames[s]<<" region="<<regionNames[r]<<" pixel_frames="<<e.count
                    <<" unknown="<<e.unknown<<" luma_mse="<<e.squared/double(e.count)
                    <<" bright_mse="<<e.bright/double(e.count)<<" dark_mse="<<e.dark/double(e.count)
                    <<" signed_bias="<<e.bias/double(e.count)<<" mean="<<e.value/double(e.count)<<" reference_mean="<<e.truth/double(e.count)
                    <<" residual_hp_mse="<<e.highPass/double(std::max<std::uint64_t>(1,e.highPassCount))
                    <<" temporal_variance="<<e.variance/double(e.varianceCount)<<std::endl;}
            std::cout<<"SURFACE CACHE CONTACT NOISE selection view="<<view<<" variant="<<variantNames[variant]
                <<" selected_pixel_frames="<<metric.selected<<" selected_known="<<metric.selectedKnown
                <<" unknown_centers_preserved="<<metric.unknownCenters<<" mature_history_frames="<<metric.historyReused
                <<" filter_passes="<<gi.contact.filterPasses<<" temporal_directions="<<gi.contact.temporalDirections
                <<" coverage_weighted_filter="<<gi.contact.coverageWeightedFilter<<std::endl;
            Check(metric.selected>0&&metric.selectedKnown>0,"Contact-noise fixture did not exercise contact retracing");
            Check(metric.historyReused==measuredFrames,"Contact-noise mature sequence unexpectedly lost all GI history");
            if(variant==0)legacyErrors=metric.errors;
            if(variant==2){
                // Compare the same physical receiver set at each stage. The
                // wider contact band must improve in aggregate, while narrow
                // subregions retain modest tolerance for Monte Carlo noise.
                for(unsigned s=1;s<3;++s){const auto& before=legacyErrors[s];const auto& after=metric.errors[s];
                    const double beforeMse=before[1].squared+before[2].squared;
                    const double afterMse=after[1].squared+after[2].squared;
                    const double beforeHp=before[1].highPass+before[2].highPass;
                    const double afterHp=after[1].highPass+after[2].highPass;
                    Check(afterMse<beforeMse,"Contact denoising did not reduce aggregate contact-band MSE");
                    Check(afterHp<beforeHp,"Contact denoising did not reduce aggregate contact-band residual grain");
                    std::cout<<"SURFACE CACHE CONTACT NOISE aggregate view="<<view<<" stage="<<stageNames[s]
                        <<" region=contact_0to35cm mse_ratio="<<afterMse/std::max(beforeMse,1e-30)
                        <<" residual_hp_ratio="<<afterHp/std::max(beforeHp,1e-30)<<std::endl;}
                for(unsigned r=1;r<4;++r){const auto& before=legacyErrors[2][r];const auto& after=metric.errors[2][r];
                    const double tolerance=r==3?1.03:1.05;
                    Check(after.squared<=tolerance*before.squared,"Contact denoising exceeded a receiver-region MSE tolerance");
                    Check(after.bright<=tolerance*before.bright,"Contact denoising exceeded a receiver-region bright-error tolerance");
                    if(r==3)Check(after.highPass<=tolerance*before.highPass,"Contact denoising added grain to the interior control region");
                    if(r==1){const double biasIncrease=after.bias/double(after.count)-before.bias/double(before.count);
                        const double referenceMean=after.truth/double(after.count);
                        Check(biasIncrease<=.015*referenceMean,"Contact denoising brightened the narrow dark-contact region");}
                    std::cout<<"SURFACE CACHE CONTACT NOISE improvement view="<<view<<" region="<<regionNames[r]
                        <<" temporal_mse_ratio="<<after.squared/std::max(before.squared,1e-30)
                        <<" temporal_bright_mse_ratio="<<after.bright/std::max(before.bright,1e-30)
                        <<" temporal_hp_ratio="<<after.highPass/std::max(before.highPass,1e-30)
                        <<" temporal_variance_ratio="<<after.variance/std::max(before.variance,1e-30)<<std::endl;}}
            for(unsigned s=0;s<3;++s)save(images[s],view+"_"+variantNames[variant]+"_"+stageNames[s]);
            if(!options.screenshot.empty())Save(lab.Readback(),outputPath(view+"_"+variantNames[variant]+"_total"));
            if(variant==2){const auto origin=pose.camera;
                const auto forward=glm::normalize(-glm::vec3(glm::inverse(origin.view)[2]));
                for(unsigned step=1;step<=3;++step){auto camera=origin;
                    camera.position+=glm::vec3(.01f*float(step),0,0);
                    camera.view=glm::lookAt(camera.position,camera.position+forward,glm::vec3(0,1,0));
                    frame.camera=camera;render();
                    finite(lab.ReadbackSurfaceCacheContactGi(),"small-motion contact GI");
                    finite(lab.ReadbackSurfaceCacheTemporalGi(),"small-motion temporal GI");
                    const auto history=lab.ReadbackSurfaceCacheTemporalHistory();finite(history,"small-motion history");
                    const auto reused=std::count_if(history.begin(),history.end(),[](const auto& p){return p.w>1.5f;});
                    Check(lab.PipelineStatistics().surfaceCacheHistoryUsed&&reused>pixels/3,
                        "Contact denoising lost compatible history under a small camera translation");
                    std::cout<<"SURFACE CACHE CONTACT NOISE motion view="<<view<<" step="<<step
                        <<" reused_pixels="<<reused<<" reference_error_measured=0"<<std::endl;}
                frame.camera=pose.camera;}
            ++comparisons;}
        // Same-process ABBA, with all variants resident. Neither warm-up nor
        // timed batches read pixels, diagnostics, counters or ray results.
        if(viewIndex==0)for(unsigned batch=0;batch<4;++batch){const bool improved=batch==1||batch==2;
            gi=baselineGi;placement=baselinePlacement;frame.camera=pose.camera;
            gi.contact.filterPasses=improved?3:1;gi.contact.temporalDirections=improved;
            gi.contact.coverageWeightedFilter=improved;placement.frameIndex=0;trace.samplingSeed=0;
            reset();for(unsigned warm=0;warm<4;++warm)render();
            const auto timing=lab.MeasureFrames(frame,4);
            Check(std::isfinite(timing.gpuMilliseconds)&&timing.gpuMilliseconds>0,"Contact-noise performance batch lacks a valid GPU timestamp");
            std::cout<<"SURFACE CACHE CONTACT NOISE perf view="<<view<<" batch="<<batch
                <<" variant="<<(improved?"production":"legacy")<<" gpu_ms="<<timing.gpuMilliseconds
                <<" elapsed_ms="<<timing.elapsedMilliseconds<<" size="<<frame.width<<"x"<<frame.height
                <<" contact_rays="<<gi.contact.raysPerPixel<<" filter_passes="<<gi.contact.filterPasses
                <<" frames=4 warmup_frames=4 readback=0 present=0"<<std::endl;}
        ++views;}
    std::cout<<"SURFACE CACHE CONTACT NOISE PASS views="<<views<<" variants="<<comparisons
        <<" warmup_frames="<<warmup<<" measured_frames="<<measuredFrames<<" reference_spp="<<referenceSpp
        <<" geometry_only_regions=1 upstream_phase_matched=1 size="<<frame.width<<"x"<<frame.height<<std::endl;
}
