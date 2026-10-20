// Focused A/B recovery regression. The per-pixel 256-spp reference bypasses
// probe reconstruction but shares the completed single-bounce Surface Cache;
// it is an A/B transport reference, not path-traced ground truth.
void SurfaceCacheRecoveryQuality(MaterialLabModule& lab,const Options& options) {
    Check(options.vulkan,"Recovery quality needs Vulkan for the independent per-pixel reference");
    auto fixture=options;fixture.sealedGiRoom=true;fixture.sdfBanquet=false;
    fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);
    frame.width=options.renderSizeSet?options.benchmarkWidth:640;
    frame.height=options.renderSizeSet?options.benchmarkHeight:400;
    frame.effects.outputSize={0,0};frame.present=false;frame.screenProbeStudyHud=false;
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::None;
    auto& gi=frame.effects.surfaceCacheGi;auto& temporal=gi.temporal;
    // Isolate the legacy history curve; temporal/probe-imprint fixtures cover static convergence.
    temporal.staticConvergence=false;
    auto& spatial=gi.spatial;auto& placement=frame.effects.screenProbePlacement;auto& trace=placement.trace;
    gi.enabled=true;gi.useScreenProbes=true;gi.view=2;gi.probeDiagnostics=false;gi.probeOriginDiagnostics=false;
    gi.reconstructionDebug=0;gi.collectProbeCoverage=true;
    placement.enabled=true;placement.adaptive=true;placement.cornerRefinement=true;
    placement.baseSpacing=options.surfaceCacheProbeSpacing;placement.candidateCount=8;
    placement.freeze=false;placement.frameIndex=0;placement.reset=true;
    trace.rayResolution=options.surfaceCacheProbeRays;trace.cosineHemisphere=true;
    trace.samplingSeed=0;trace.maxSteps=512;trace.sdf.maxSteps=256;
    spatial.enabled=true;temporal.enabled=true;
    temporal.youngFilterRadius=3;temporal.youngFilterMaxFrames=4;temporal.youngFilterStrength=.75f;
    temporal.dynamicMaxFrames=4;temporal.dynamicClampSigma=2;temporal.dynamicSettleFrames=3;
    const auto spatialPreset=spatial;const auto temporalPreset=temporal;
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;
        placement.reset=false;temporal.reset=false;};
    const auto reset=[&]{frame.effects.cameraCut=true;placement.reset=true;temporal.reset=true;};
    const auto configure=[&](bool improved,unsigned phases){spatial=spatialPreset;temporal=temporalPreset;
        spatial.localAdaptiveNeighbors=improved;temporal.youngFilter=improved;
        temporal.retainHistoryOnObjectMotion=improved;temporal.directionPhases=phases;
        trace.samplingSeed=0;trace.rayResolution=options.surfaceCacheProbeRays;
        gi.useScreenProbes=true;placement.enabled=true;placement.frameIndex=0;reset();};
    const std::size_t pixels=std::size_t(frame.width)*frame.height;
    const glm::dvec3 luma(.2126,.7152,.0722);
    const auto& roomObjects=SealedGiRoomObjects();
    const auto movable=std::find_if(roomObjects.begin(),roomObjects.end(),[](const auto& object){return object.movable;});
    Check(movable!=roomObjects.end()&&movable->mesh==ShowcaseMesh::Box,"Recovery fixture needs its movable ceramic cube");
    const auto originalCubeInverse=glm::inverse(ProbePlacementTransform(*movable,ProbePlacementSceneSettings{}));
    const auto originalFootprint=[&](glm::vec3 p){const auto local=glm::vec3(originalCubeInverse*glm::vec4(p,1));
        return std::abs(local.x)<.96f&&std::abs(local.z)<.96f;};
    const auto originalCubeSurface=[&](glm::vec3 p){const auto local=glm::vec3(originalCubeInverse*glm::vec4(p,1));
        const auto a=glm::abs(local);
        return p.y>.04f&&a.x<1.005f&&a.y<1.005f&&a.z<1.005f&&std::max(a.x,std::max(a.y,a.z))>.99f;};
    const auto finite=[&](const auto& image,const char* label){
        Check(image.size()==pixels,std::string("Missing recovery ")+label+": "+lab.LastError());
        for(const auto& value:image)for(unsigned c=0;c<4;++c)Check(std::isfinite(value[c]),"Nonfinite recovery image");};
    const auto hash=[](const std::vector<glm::vec4>& values){
        std::uint64_t value=1469598103934665603ull;
        const auto* bytes=reinterpret_cast<const unsigned char*>(values.data());
        for(std::size_t i=0;i<values.size()*sizeof(glm::vec4);++i){value^=bytes[i];value*=1099511628211ull;}
        return value;};
    const auto energy=[](const auto& image){double sum=0;for(const auto& p:image)sum+=std::abs(double(p.x))+std::abs(double(p.y))+std::abs(double(p.z));return sum;};
    const auto save=[&](const auto& hdr,const std::string& label){if(options.screenshot.empty())return;
        FramePixels image;image.width=frame.width;image.height=frame.height;image.rgba.resize(pixels*4);
        for(std::size_t i=0;i<pixels;++i){for(unsigned c=0;c<3;++c){double value=4.*std::max(0.,double(hdr[i][c]));
            value=value/(1.+value);value=value<=.0031308?12.92*value:1.055*std::pow(value,1./2.4)-.055;
            image.rgba[i*4+c]=std::uint8_t(std::lround(std::clamp(value,0.,1.)*255.));}image.rgba[i*4+3]=255;}
        auto path=std::filesystem::path(options.screenshot);
        path.replace_filename(path.stem().string()+"_"+label+path.extension().string());Save(image,path.string());};

    // Both cameras aim at the same physical ceiling/right/back corner. The
    // corner ROI below is world-space, so changing image coverage cannot swap
    // in an unrelated, easier wall patch for the near-distance measurement.
    const glm::vec3 cornerTarget(3.55f,4.05f,-3.05f);
    const auto cornerCamera=[&](unsigned view,unsigned step){auto camera=SealedGiRoomCameraPreset(0,frame.OutputAspect());
        camera.position=view?glm::vec3(1.40f,3.05f,-1.05f):glm::vec3(-1.80f,1.60f,2.10f);
        camera.view=glm::lookAt(camera.position,cornerTarget,glm::vec3(0,1,0));
        const auto inverse=glm::inverse(camera.view);const auto right=glm::normalize(glm::vec3(inverse[0]));
        const auto forward=glm::normalize(-glm::vec3(inverse[2]));
        camera.position+=right*(.05f*float(step));
        const auto direction=glm::vec3(glm::rotate(glm::mat4(1),.015f*float(step),glm::vec3(0,1,0))*glm::vec4(forward,0));
        camera.view=glm::lookAt(camera.position,camera.position+direction,glm::vec3(0,1,0));return camera;};
    struct Pose {
        Render::PipelineCamera camera;float objectOffset=0;
        std::vector<glm::vec4> reference;
        std::vector<glm::vec3> world,normal;
        std::vector<std::uint8_t> valid,corner,staticReceiver;
        double uncertainty=0;std::size_t cornerPixels=0;
    };
    const auto captureGeometry=[&](Pose& pose){const auto sources=lab.ReadbackScreenProbeSources();
        finite(sources.sourceViewPositions,"source positions");finite(sources.sourceWorldNormals,"source normals");
        pose.world.resize(pixels);pose.normal.resize(pixels);pose.valid.assign(pixels,0);
        pose.corner.assign(pixels,0);pose.staticReceiver.assign(pixels,0);pose.cornerPixels=0;
        const auto inverse=glm::inverse(pose.camera.view);
        for(std::size_t i=0;i<pixels;++i){const auto p=sources.sourceViewPositions[i],n=sources.sourceWorldNormals[i];
            if(p.w<.5f||n.w<.5f||glm::dot(glm::vec3(n),glm::vec3(n))<.5f)continue;
            pose.valid[i]=1;const auto w=glm::vec3(inverse*glm::vec4(glm::vec3(p),1));
            const auto unit=glm::normalize(glm::vec3(n));pose.world[i]=w;pose.normal[i]=unit;
            const bool right=std::abs(w.x-4.f)<.015f&&unit.x<-.99f;
            const bool back=std::abs(w.z+3.5f)<.015f&&unit.z>.99f;
            const bool ceiling=std::abs(w.y-4.5f)<.015f&&unit.y<-.99f;
            pose.corner[i]=(right&&w.y>3.3f&&w.z< -2.3f)||(back&&w.x>2.8f&&w.y>3.3f)||
                (ceiling&&w.x>2.8f&&w.z< -2.3f);
            pose.cornerPixels+=pose.corner[i]!=0;
            pose.staticReceiver[i]=right||back||ceiling||
                (std::abs(w.y)<.015f&&unit.y>.99f)||
                (std::abs(w.x+4.f)<.015f&&unit.x>.99f)||
                (std::abs(w.z-3.5f)<.015f&&unit.z<-.99f);
        }
    };
    unsigned referencePoses=0;
    const auto reference=[&](Pose& pose,const char* label,unsigned step){
        frame.camera=pose.camera;frame.screenProbeStudyState.occluderOffset=pose.objectOffset;
        gi.useScreenProbes=true;placement.enabled=true;temporal.enabled=false;spatial.enabled=false;reset();render();
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
        while(lab.ProbeSdfQualityStatistics().pending){render();Check(std::chrono::steady_clock::now()<deadline,"Recovery SDF preparation timed out");}
        captureGeometry(pose);pose.reference.assign(pixels,glm::vec4(0));
        std::vector<glm::vec3> half(pixels,glm::vec3(0));
        gi.useScreenProbes=false;placement.enabled=false;gi.raysPerPixel=32;
        constexpr unsigned seeds=8;
        for(unsigned seed=0;seed<seeds;++seed){trace.samplingSeed=1009+73*seed;render();
            const auto image=lab.ReadbackSurfaceCacheRawGi();finite(image,"256-spp reference");
            for(std::size_t i=0;i<pixels;++i){pose.reference[i]+=image[i]/float(seeds);
                half[i]+=glm::vec3(image[i])*(seed<seeds/2?.25f:-.25f);}}
        double signal=0;std::size_t count=0;for(std::size_t i=0;i<pixels;++i)if(pose.valid[i]){
            const double delta=glm::dot(glm::dvec3(half[i]),luma);pose.uncertainty+=delta*delta;
            const double value=glm::dot(glm::dvec3(pose.reference[i]),luma);signal+=value*value;++count;}
        Check(count>100&&signal>1e-10,"Recovery reference has no visible indirect receivers");
        pose.uncertainty/=double(count);
        std::cout<<"SURFACE CACHE RECOVERY reference path="<<label<<" step="<<step<<" spp=256 seeds=8"
            <<" valid_pixels="<<count<<" corner_pixels="<<pose.cornerPixels
            <<" half_difference_mse="<<pose.uncertainty<<" signal_mse="<<signal/double(count)
            <<" shared_cache=1 path_truth=0"<<std::endl;
        ++referencePoses;
    };
    struct Error {double squared=0,dark=0,bright=0,bias=0,truth=0;std::uint64_t count=0;};
    struct Metrics {
        std::array<std::array<Error,3>,4> error{}; // full / corner / old floor / remote static; raw / spatial / temporal
        std::array<double,3> observedDelta{},residualDelta{};
        std::uint64_t observedPairs=0,residualPairs=0,frames=0,referenceFrames=0,historyUsed=0;
        double historySum=0;std::uint64_t historyPixels=0,reusedPixels=0,newPixels=0;
    };
    struct Sample {std::array<std::vector<glm::vec4>,3> image;std::vector<glm::vec4> history;
        std::vector<glm::vec3> world,normal;std::vector<std::uint8_t> valid;Render::PipelineCamera camera;
        const Pose* truth=nullptr;};
    const auto sample=[&](const Pose* truth){Sample result;
        result.image={lab.ReadbackSurfaceCacheRawGi(),lab.ReadbackSurfaceCacheSpatialGi(),lab.ReadbackSurfaceCacheTemporalGi()};
        result.history=lab.ReadbackSurfaceCacheTemporalHistory();result.camera=*frame.camera;result.truth=truth;
        for(const auto& image:result.image)finite(image,"measured GI");finite(result.history,"effective history counts");
        if(truth){result.world=truth->world;result.normal=truth->normal;result.valid=truth->valid;}
        else {Pose geometry;geometry.camera=result.camera;captureGeometry(geometry);result.world=std::move(geometry.world);
            result.normal=std::move(geometry.normal);result.valid=std::move(geometry.valid);}
        for(std::size_t i=0;i<pixels;++i){const auto count=result.history[i].w;
            Check(count>=0&&count<=float(temporal.maxFrames)+.001f,"Recovery history exceeded its configured frame cap");
            if(result.image[1][i].w<=0)Check(glm::length(glm::vec3(result.image[2][i]))<1e-7f,
                "Young recovery invented light for a wholly unreadable receiver");}
        return result;};
    const auto measure=[&](Metrics& metric,const Sample& current,const Sample* previous){
        ++metric.frames;metric.historyUsed+=lab.PipelineStatistics().surfaceCacheHistoryUsed;
        if(current.truth)++metric.referenceFrames;
        const auto previousVP=previous?previous->camera.projection*previous->camera.view:glm::mat4(1);
        for(std::size_t i=0;i<pixels;++i)if(current.valid[i]){
            const auto p=current.world[i],n=current.normal[i];const auto count=current.history[i].w;
            if(count>0){metric.historySum+=count;++metric.historyPixels;metric.reusedPixels+=count>1.01f;metric.newPixels+=count<=1.01f;}
            if(current.truth){const bool oldFloor=std::abs(p.y)<.015f&&n.y>.99f&&originalFootprint(p);
                const bool remote=current.truth->staticReceiver[i]&&glm::length(glm::vec2(p.x+.75f,p.z-1.15f))>1.4f&&
                    glm::length(glm::vec2(p.x-.45f,p.z-1.15f))>1.4f;
                const std::array<bool,4> roi{{true,current.truth->corner[i]!=0,oldFloor,remote}};
                const double truth=glm::dot(glm::dvec3(current.truth->reference[i]),luma);
                for(unsigned stage=0;stage<3;++stage){const double error=glm::dot(glm::dvec3(current.image[stage][i]),luma)-truth;
                    for(unsigned region=0;region<roi.size();++region)if(roi[region]){auto& e=metric.error[region][stage];
                        e.squared+=error*error;e.dark+=std::min(error,0.)*std::min(error,0.);e.bright+=std::max(error,0.)*std::max(error,0.);
                        e.bias+=error;e.truth+=truth*truth;++e.count;}}
            }
            if(!previous)continue;
            const auto clip=previousVP*glm::vec4(p,1);if(clip.w<=0)continue;
            const auto screen=glm::vec2(clip)/clip.w*.5f+.5f;
            const int x=int(std::floor(screen.x*frame.width)),y=int(std::floor(screen.y*frame.height));
            if(x<0||y<0||x>=int(frame.width)||y>=int(frame.height))continue;
            const auto j=std::size_t(y)*frame.width+unsigned(x);
            if(!previous->valid[j]||glm::dot(n,previous->normal[j])<.99f)continue;
            const float worldTolerance=std::max(.012f,.006f*glm::length(p-current.camera.position));
            if(glm::length(p-previous->world[j])>worldTolerance)continue;
            ++metric.observedPairs;
            const bool referenced=current.truth&&previous->truth;
            if(referenced)++metric.residualPairs;
            for(unsigned stage=0;stage<3;++stage){const double delta=glm::dot(glm::dvec3(current.image[stage][i])-glm::dvec3(previous->image[stage][j]),luma);
                metric.observedDelta[stage]+=delta*delta;
                if(referenced){const double physical=glm::dot(glm::dvec3(current.truth->reference[i])-glm::dvec3(previous->truth->reference[j]),luma);
                    metric.residualDelta[stage]+=(delta-physical)*(delta-physical);}}
        }
    };
    const std::array<const char*,3> stages{{"raw","spatial","temporal"}};
    const std::array<const char*,4> regions{{"full","corner","old_floor","remote_static"}};
    const auto report=[&](const char* path,const char* mode,const char* interval,const Metrics& metric){
        std::cout<<"SURFACE CACHE RECOVERY history path="<<path<<" mode="<<mode<<" interval="<<interval
            <<" frames="<<metric.frames<<" reference_frames="<<metric.referenceFrames<<" history_used_frames="<<metric.historyUsed
            <<" history_mean="<<metric.historySum/std::max(1.,double(metric.historyPixels))
            <<" reused_pixels="<<metric.reusedPixels<<" new_pixels="<<metric.newPixels<<std::endl;
        for(unsigned region=0;region<regions.size();++region)for(unsigned stage=0;stage<stages.size();++stage){const auto& e=metric.error[region][stage];
            if(!e.count)continue;std::cout<<"SURFACE CACHE RECOVERY error path="<<path<<" mode="<<mode<<" interval="<<interval
                <<" region="<<regions[region]<<" stage="<<stages[stage]<<" samples="<<e.count
                <<" luma_mse="<<e.squared/double(e.count)<<" relative_mse="<<e.squared/std::max(e.truth,1e-12)
                <<" dark_mse="<<e.dark/double(e.count)<<" bright_mse="<<e.bright/double(e.count)
                <<" signed_bias="<<e.bias/double(e.count)<<std::endl;}
        for(unsigned stage=0;stage<stages.size();++stage)std::cout<<"SURFACE CACHE RECOVERY motion path="<<path<<" mode="<<mode
            <<" interval="<<interval<<" stage="<<stages[stage]<<" observed_delta_mse="<<metric.observedDelta[stage]/std::max(1.,double(metric.observedPairs))
            <<" residual_delta_mse="<<metric.residualDelta[stage]/std::max(1.,double(metric.residualPairs))
            <<" observed_pairs="<<metric.observedPairs<<" reference_pairs="<<metric.residualPairs
            <<" observed_is_noise_variance=0"<<std::endl;
    };
    const auto grossRegression=[&](const Metrics& old,const Metrics& improved,double uncertainty,const char* label){
        // These are catastrophic-regression bounds, not a claim that every
        // dark ROI improved. Signed/dark/bright terms above remain visible.
        const auto& a=old.error[0][2];const auto& b=improved.error[0][2];
        Check(a.count>100&&b.count==a.count,"Recovery A/B measured different receiver populations");
        Check(b.squared/double(b.count)<=2.*a.squared/double(a.count)+4.*uncertainty+1e-8,
            std::string(label)+" doubled temporal error beyond reference uncertainty");
        Check(b.dark/double(b.count)<=2.*a.dark/double(a.count)+4.*uncertainty+1e-8,
            std::string(label)+" doubled dark error while concealing it with bright-error reduction");
    };

    constexpr std::array<unsigned,3> checkpoints{{0,1,6}};
    std::uint64_t fairnessFrames=0,totalCameraReused=0,totalCameraReusedPixels=0;
    for(unsigned view=0;view<2;++view){const char* path=view?"corner_near":"corner_far";
        std::array<Pose,3> poses;
        for(unsigned i=0;i<poses.size();++i){poses[i].camera=cornerCamera(view,checkpoints[i]);reference(poses[i],path,checkpoints[i]);
            Check(poses[i].cornerPixels>32,"Near/far reference missed the shared physical corner patch");}
        // Isolate direct light/shadow bands before attributing every visible
        // transition to GI. This display capture uses the same physical patch.
        if(!options.screenshot.empty()) {
            frame.camera=poses[0].camera;gi.view=0;gi.enabled=false;render();
            auto path=std::filesystem::path(options.screenshot);
            path.replace_filename(path.stem().string()+(view?"_near_direct":"_far_direct")+path.extension().string());
            Save(lab.Readback(),path.string());gi.enabled=true;gi.view=2;
        }
        std::array<std::uint64_t,11> baselineRaw{};std::uint64_t resetRaw=0;
        std::array<Metrics,2> moving,settled,stationary,cold;
        for(unsigned mode=0;mode<2;++mode){configure(mode!=0,8);frame.camera=poses[0].camera;frame.screenProbeStudyState.occluderOffset=0;
            for(unsigned warm=0;warm<16;++warm)render();
            auto previous=sample(&poses[0]);measure(stationary[mode],previous,nullptr);
            if(mode==0)baselineRaw[0]=hash(previous.image[0]);else {Check(hash(previous.image[0])==baselineRaw[0],"Near/far static A/B raw rays differ");++fairnessFrames;}
            frame.effects.cameraCut=true;render();auto restarted=sample(&poses[0]);measure(cold[mode],restarted,nullptr);
            if(mode==0)resetRaw=hash(restarted.image[0]);else {Check(hash(restarted.image[0])==resetRaw,"Reset A/B changed raw rays");++fairnessFrames;}
            Check(!lab.PipelineStatistics().surfaceCacheHistoryUsed,"Recovery reset case silently reused mature history");
            save(restarted.image[2],std::string(path)+(mode?"_reset_improved":"_reset_baseline"));
            previous=std::move(restarted);
            for(unsigned step=1;step<=6;++step){frame.camera=cornerCamera(view,step);render();
                const Pose* truth=step==1?&poses[1]:step==6?&poses[2]:nullptr;auto current=sample(truth);
                measure(moving[mode],current,&previous);
                if(mode==0)baselineRaw[step]=hash(current.image[0]);else {Check(hash(current.image[0])==baselineRaw[step],"Camera A/B changed raw ray phase or radiance");++fairnessFrames;}
                if(mode)totalCameraReused+=lab.PipelineStatistics().surfaceCacheHistoryUsed;
                previous=std::move(current);
            }
            for(unsigned stop=1;stop<=8;++stop){render();if(stop!=1&&stop!=4&&stop!=8)continue;
                auto current=sample(&poses[2]);measure(settled[mode],current,&previous);
                const unsigned index=stop==1?7:stop==4?8:9;
                if(mode==0)baselineRaw[index]=hash(current.image[0]);else {Check(hash(current.image[0])==baselineRaw[index],"Settled camera A/B raw rays differ");++fairnessFrames;}
                previous=std::move(current);
            }
            report(path,mode?"improved":"baseline","static",stationary[mode]);
            report(path,mode?"improved":"baseline","reset",cold[mode]);
            report(path,mode?"improved":"baseline","camera_move",moving[mode]);
            report(path,mode?"improved":"baseline","camera_settle",settled[mode]);
            save(previous.image[2],std::string(path)+(mode?"_improved":"_baseline"));
        }
        grossRegression(moving[0],moving[1],std::max(poses[1].uncertainty,poses[2].uncertainty),path);
        grossRegression(cold[0],cold[1],poses[0].uncertainty,path);
        grossRegression(settled[0],settled[1],poses[2].uncertainty,path);
        totalCameraReusedPixels+=moving[1].reusedPixels;
    }
    Check(totalCameraReused==12&&totalCameraReusedPixels>100,"Camera movement discarded otherwise compatible history");

    // Use one angular phase only in the object A/B. The old renderer restarts
    // its direction sequence at each transform edit, while retained history
    // continues it. A fixed phase removes that confound and gives identical raw
    // rays; this segment measures geometry response / ghosting, not phase noise.
    auto objectCamera=SealedGiRoomCameraPreset(0,frame.OutputAspect());
    // The ordinary inspection camera looks nearly horizontally, leaving the
    // cube's ground footprint below its frustum. This elevated fixed view sees
    // the vacated floor, moving cube and distant static floor/walls together.
    objectCamera.position={0,3.4f,3.05f};
    objectCamera.view=glm::lookAt(objectCamera.position,glm::vec3(0,.8f,.25f),glm::vec3(0,1,0));
    constexpr std::array<unsigned,3> objectCheckpoints{{0,1,4}};
    std::array<Pose,3> objectPoses;
    for(unsigned i=0;i<objectPoses.size();++i){objectPoses[i].camera=objectCamera;
        objectPoses[i].objectOffset=.30f*float(objectCheckpoints[i]);reference(objectPoses[i],"object",objectCheckpoints[i]);}
    const auto& before=objectPoses.front();const auto& after=objectPoses.back();
    double oldFloorDelta=0,remoteDelta=0;
    std::uint64_t oldFloorCount=0,remoteCount=0,visibleFloorCount=0,oldFootprintFloorCount=0;
    for(std::size_t i=0;i<pixels;++i)if(after.valid[i]&&after.staticReceiver[i]){
        const auto p=after.world[i],n=after.normal[i];
        const double delta=glm::dot(glm::dvec3(after.reference[i])-glm::dvec3(before.reference[i]),luma);
        if(std::abs(p.y)<.015f&&n.y>.99f){++visibleFloorCount;
            if(originalFootprint(p)){++oldFootprintFloorCount;
                // The previous sample must really have been the original cube,
                // not merely a nearby floor pixel with a noisy light estimate.
                if(before.valid[i]&&originalCubeSurface(before.world[i])&&glm::length(p-before.world[i])>.04f){
                    oldFloorDelta+=delta*delta;++oldFloorCount;}}}
        if(before.valid[i]&&before.staticReceiver[i]&&glm::length(p-before.world[i])<.002f&&
            glm::dot(n,before.normal[i])>.999f&&glm::length(glm::vec2(p.x+.75f,p.z-1.15f))>1.4f&&
            glm::length(glm::vec2(p.x-.45f,p.z-1.15f))>1.4f){remoteDelta+=delta*delta;++remoteCount;}
    }
    std::cout<<"SURFACE CACHE RECOVERY object_reference visible_floor_pixels="<<visibleFloorCount
        <<" original_footprint_floor_pixels="<<oldFootprintFloorCount
        <<" old_floor_pixels="<<oldFloorCount<<" old_floor_delta_mse="<<oldFloorDelta/std::max(1.,double(oldFloorCount))
        <<" remote_static_pixels="<<remoteCount<<" remote_delta_mse="<<remoteDelta/std::max(1.,double(remoteCount))
        <<" object_delta_x=1.2 old_cube_to_floor_required=1 paired_seeds=1 direction_phases=1 moving_body_only=0"<<std::endl;
    Check(oldFloorCount>10&&remoteCount>100&&oldFloorDelta>1e-12&&remoteDelta>1e-12,
        "Object fixture did not expose the old cube footprint and changed remote static illumination");
    std::array<std::uint64_t,8> objectRaw{};std::array<Metrics,2> objectMoving,objectSettled;
    std::uint64_t retainedFrames=0,retainedStaticPixels=0,baselineMoveResets=0;
    for(unsigned mode=0;mode<2;++mode){configure(mode!=0,1);frame.camera=objectCamera;frame.screenProbeStudyState.occluderOffset=0;
        for(unsigned warm=0;warm<16;++warm)render();auto previous=sample(&objectPoses[0]);
        if(mode==0)objectRaw[0]=hash(previous.image[0]);else {Check(hash(previous.image[0])==objectRaw[0],"Object warm A/B raw rays differ");++fairnessFrames;}
        for(unsigned step=1;step<=4;++step){frame.screenProbeStudyState.occluderOffset=.30f*float(step);render();
            const auto* truth=step==1?&objectPoses[1]:step==4?&objectPoses[2]:nullptr;auto current=sample(truth);
            measure(objectMoving[mode],current,&previous);
            if(mode==0){objectRaw[step]=hash(current.image[0]);baselineMoveResets+=!lab.PipelineStatistics().surfaceCacheHistoryUsed;}
            else {Check(hash(current.image[0])==objectRaw[step],"Fixed-phase object A/B raw rays differ");++fairnessFrames;
                retainedFrames+=lab.PipelineStatistics().surfaceCacheHistoryUsed;
                for(std::size_t i=0;i<pixels;++i)if(current.valid[i]&&current.history[i].w>1.01f){const auto p=current.world[i],n=current.normal[i];
                    if((std::abs(p.y)<.015f&&n.y>.99f)||(std::abs(p.z+3.5f)<.015f&&n.z>.99f)||
                        (std::abs(p.x-4.f)<.015f&&n.x<-.99f))++retainedStaticPixels;}}
            previous=std::move(current);
        }
        for(unsigned stop=1;stop<=12;++stop){render();if(stop!=1&&stop!=4&&stop!=12)continue;
            auto current=sample(&objectPoses[2]);measure(objectSettled[mode],current,&previous);
            const unsigned index=stop==1?5:stop==4?6:7;
            if(mode==0)objectRaw[index]=hash(current.image[0]);else {Check(hash(current.image[0])==objectRaw[index],"Object settling A/B raw rays differ");++fairnessFrames;}
            previous=std::move(current);
        }
        report("object",mode?"improved":"baseline","object_move",objectMoving[mode]);
        report("object",mode?"improved":"baseline","object_settle",objectSettled[mode]);
        save(previous.image[2],mode?"object_improved":"object_baseline");
    }
    Check(baselineMoveResets==4&&retainedFrames==4&&retainedStaticPixels>100,
        "Object retention did not reuse actual static-surface history, or the baseline failed to reset");
    grossRegression(objectMoving[0],objectMoving[1],std::max(objectPoses[1].uncertainty,objectPoses[2].uncertainty),"Object movement");
    grossRegression(objectSettled[0],objectSettled[1],objectPoses[2].uncertainty,"Object settling");

    // Production uses changing phases. Sample several independent base seeds
    // to measure that actual mode as well; raw phases intentionally differ
    // after the legacy reset, so this is not the matched-ray ablation above.
    std::array<Metrics,2> production;
    for(unsigned trial=0;trial<4;++trial)for(unsigned mode=0;mode<2;++mode) {
        configure(mode!=0,8);trace.samplingSeed=211+37*trial;
        frame.camera=objectCamera;frame.screenProbeStudyState.occluderOffset=0;
        for(unsigned warm=0;warm<12;++warm)render();auto previous=sample(&objectPoses[0]);
        for(unsigned step=1;step<=4;++step) {
            frame.screenProbeStudyState.occluderOffset=.30f*float(step);render();
            const Pose* truth=step==1?&objectPoses[1]:step==4?&objectPoses[2]:nullptr;
            auto current=sample(truth);measure(production[mode],current,&previous);previous=std::move(current);
        }
    }
    report("object_8phases_4seeds","baseline","object_move",production[0]);
    report("object_8phases_4seeds","improved","object_move",production[1]);
    std::cout<<"SURFACE CACHE RECOVERY production phases=8 base_seeds=4 matched_raw=0 reference_spp=256"<<std::endl;
    grossRegression(production[0],production[1],std::max(objectPoses[1].uncertainty,objectPoses[2].uncertainty),"Production object movement");

    // Explicitly exercise unavailable transport, rather than succeeding merely
    // because this well-covered room happened to have no unavailable rays.
    configure(true,8);frame.camera=objectCamera;trace.sdf.maxSteps=8;render();
    const auto sourceRays=lab.ReadbackScreenProbeTrace(false);
    const auto filteredRays=lab.ReadbackSurfaceCacheSpatialRays();
    Check(filteredRays.size()>=sourceRays.radianceStatus.size()*4,"Recovery filtered ray atlas is missing");
    std::uint64_t unknown=0;
    for(std::size_t i=0;i<sourceRays.radianceStatus.size();++i){const auto value=sourceRays.radianceStatus[i];
        const unsigned status=unsigned(std::round(value.w));const bool readable=status==1||status==5||status==9||status==10||status==13;
        if(readable)continue;if(status>0)++unknown;
        for(unsigned c=0;c<4;++c)Check(filteredRays[i*4][c]==value[c],"Recovery spatial filtering filled an unavailable ray");}
    Check(unknown>0,"Reduced-budget recovery diagnostic did not exercise unavailable transport");
    trace.sdf.maxSteps=256;reset();for(unsigned warm=0;warm<12;++warm)render();
    Check(energy(lab.ReadbackSurfaceCacheTemporalGi())>1e-5,"Recovery lamp-off guard lacks populated lit history");
    frame.effects.areaLights.lights[0].enabled=false;render();
    const auto darkRaw=lab.ReadbackSurfaceCacheRawGi(),darkSpatial=lab.ReadbackSurfaceCacheSpatialGi(),darkTemporal=lab.ReadbackSurfaceCacheTemporalGi();
    finite(darkRaw,"lamp-off raw");finite(darkSpatial,"lamp-off spatial");finite(darkTemporal,"lamp-off temporal");
    Check(!lab.PipelineStatistics().surfaceCacheHistoryUsed&&energy(darkRaw)<1e-5&&energy(darkSpatial)<1e-5&&energy(darkTemporal)<1e-5,
        "Recovery reused stale light or fabricated RGB after disabling the sole emitter");
    frame.effects.areaLights.lights[0].enabled=true;frame.screenProbeStudyState.occluderOffset=0;frame.camera=objectCamera;
    // Short same-process ABBA. No pixel, ray, counter or image readbacks occur
    // within either warm-up or timed batches; both variants stay resident.
    for(unsigned batch=0;batch<4;++batch){const bool improved=batch==1||batch==2;configure(improved,8);
        for(unsigned warm=0;warm<12;++warm)render();const auto timing=lab.MeasureFrames(frame,4);
        Check(std::isfinite(timing.gpuMilliseconds)&&timing.gpuMilliseconds>0,"Recovery performance batch has no valid GPU timestamp");
        std::cout<<"SURFACE CACHE RECOVERY perf batch="<<batch<<" mode="<<(improved?"improved":"baseline")
            <<" gpu_ms="<<timing.gpuMilliseconds<<" elapsed_ms="<<timing.elapsedMilliseconds
            <<" size="<<frame.width<<"x"<<frame.height<<" rays_per_probe="<<trace.rayResolution*trace.rayResolution
            <<" frames=4 readback=0 present=0"<<std::endl;
    }
    std::cout<<"SURFACE CACHE RECOVERY PASS reference_poses="<<referencePoses<<" reference_spp=256 matched_raw_frames="<<fairnessFrames
        <<" camera_history_frames="<<totalCameraReused<<" object_retained_frames="<<retainedFrames
        <<" object_static_reused_pixels="<<retainedStaticPixels<<" unavailable_preserved="<<unknown
        <<" lamp_off_rgb=0 reference_is_path_truth=0"<<std::endl;
}
