// Motion tuning uses the same camera path, underlying phase sequence and
// per-pose reference for every candidate. A raw image difference is not a
// noise metric: the image legitimately changes as the camera moves.
void SurfaceCacheMotionQuality(MaterialLabModule& lab,const Options& options) {
    auto fixture=options;fixture.sdfBanquet=false;fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);
    frame.width=options.renderSizeSet?options.benchmarkWidth:320;frame.height=options.renderSizeSet?options.benchmarkHeight:200;
    frame.effects.outputSize={0,0};frame.present=false;frame.screenProbeStudyHud=false;
    frame.camera=ProbePlacementCameraPreset(0,frame.OutputAspect());
    auto& gi=frame.effects.surfaceCacheGi;auto& temporal=gi.temporal;
    // Isolate the legacy history curve; temporal/probe-imprint fixtures cover static convergence.
    temporal.staticConvergence=false;
    // Legacy geometry edits require a full reset; temporal/recovery fixtures cover certified motion reuse and its four-frame cap.
    temporal.retainHistoryOnObjectMotion=false;
    auto& placement=frame.effects.screenProbePlacement;auto& trace=placement.trace;
    // The compact fixture stresses 16-ray noise. An explicit render size
    // validates the viewer at its configured angular resolution (64 by default).
    const unsigned motionRayResolution=options.renderSizeSet?options.surfaceCacheProbeRays:4u;
    gi.enabled=true;gi.useScreenProbes=true;gi.view=2;gi.probeDiagnostics=false;
    // Evaluate final-gather history against probe/spatial readability and the raw reference.
    // Actual-receiver retracing is covered by contact, temporal and probe-imprint fixtures.
    gi.contact.enabled=false;
    gi.spatial.enabled=true;gi.spatial.passes=3;gi.spatial.halfKernelSize=1;
    placement.baseSpacing=std::clamp(unsigned(std::round(32.*double(frame.height)/800.)),4u,128u);
    placement.candidateCount=8;placement.frameIndex=0;placement.adaptive=true;
    placement.freeze=false;placement.reset=true;trace.rayResolution=4;trace.samplingSeed=0;
    trace.maxSteps=512;trace.sdf.maxSteps=256;
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;placement.reset=false;temporal.reset=false;};
    // References and candidates must use the same completed distance-field
    // assets, including when a caller requests an asynchronously baked tier.
    temporal.enabled=false;gi.spatial.enabled=false;
    const auto sourceDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
    do{render();Check(std::chrono::steady_clock::now()<sourceDeadline,"Motion fixture SDF preparation timed out");}
    while(lab.ProbeSdfQualityStatistics().pending);
    const auto pixels=std::size_t(frame.width)*frame.height;
    const float motionThresholdScale=float(frame.height)/800.f;
    const glm::dvec3 luminance(.2126,.7152,.0722);
    const auto finite=[&](const auto& image,const char* label){Check(image.size()==pixels,std::string("Missing ")+label+": "+lab.LastError());
        for(const auto& p:image)for(unsigned c=0;c<4;++c)
            if(!std::isfinite(p[c]))throw std::runtime_error(std::string("Nonfinite ")+label);};
    const auto maximumDifference=[](const auto& a,const auto& b){Check(a.size()==b.size(),"Motion comparison changed extent");double difference=0;
        const auto* pa=a.data();const auto* pb=b.data();
        for(std::size_t i=0;i<a.size();++i)difference=std::max(difference,glm::length(glm::dvec3(pa[i])-glm::dvec3(pb[i])));return difference;};
    const auto save=[&](const std::string& label){if(options.screenshot.empty())return;auto path=std::filesystem::path(options.screenshot);
        path.replace_filename(path.stem().string()+"_"+label+path.extension().string());Save(lab.Readback(),path.string());};
    const auto surfaceId=[](glm::vec3 p,glm::vec3 n){
        if(std::abs(p.y)<.002f&&n.y>.999f)return 0;
        if(std::abs(p.y-5)<.002f&&n.y<-.999f)return 1;
        if(std::abs(p.z+3.5f)<.002f&&n.z>.999f)return 2;
        if(std::abs(p.x+5)<.002f&&n.x>.999f)return 3;
        if(std::abs(p.x-5)<.002f&&n.x<-.999f)return 4;
        return -1;
    };
    struct Pose {
        Render::PipelineCamera camera;
        std::vector<glm::vec4> reference,coverage;
        std::vector<glm::vec3> world,normals;
        std::vector<int> surface;
        // Byte masks avoid vector<bool>'s iterator-debug lock on every lookup
        // in the full-resolution neighborhood scan.
        std::vector<std::uint8_t> valid,interior,edge;
        std::size_t edgeCount=0;
    };
    struct Candidate {
        const char* name;unsigned frames;float moving,threshold;bool clamp;unsigned phases;
        float distance=.01f,normal=45,sigma=3;
        bool edge=false;float edgeFrames=2;
        bool slowClamp=false;
    };
    const std::array<Candidate,15> candidates{{
        {"old",10,2,1,true,8},{"balanced",10,8,3,false,8},
        {"persistent",16,12,3,false,8},{"uncapped",16,16,3,false,8},
        {"balanced_clamp",10,8,3,true,8},{"persistent_16phases",16,12,3,false,16},
        {"balanced_tight",10,8,3,false,8,.003f,25,3},
        {"stable_tight",16,12,3,false,16,.003f,25,3},
        {"stable_tighter",16,12,3,false,16,.001f,25,3},
        {"balanced_clamp6",10,8,3,true,8,.01f,45,6},
        {"balanced_tight_clamp6",10,6,3,true,8,.003f,25,6},
        {"stable_tight_clamp6",16,12,3,true,16,.003f,25,6},
        {"balanced_edges",10,8,3,true,8,.01f,45,6,true,2,true},
        {"stable_edges",16,12,3,true,16,.01f,45,6,true,2,true},
        {"balanced_edges_no_global_clamp",10,8,3,false,8,.01f,45,3,true,2,true}
    }};
    // Default regressions compare the original and shipped presets at either
    // extent. The explicit tuning entry retains the focused yaw exploration.
    Render::SurfaceCacheTemporalSettings shipped;shipped.staticConvergence=false;shipped.retainHistoryOnObjectMotion=false;
    unsigned selected=unsigned(candidates.size());
    for(unsigned i=0;i<candidates.size();++i){const auto& c=candidates[i];
        if(c.frames==shipped.maxFrames&&c.moving==shipped.movingMaxFrames&&c.threshold==shipped.motionThresholdPixels&&
            c.clamp==shipped.neighborhoodClamp&&c.phases==shipped.directionPhases&&c.distance==shipped.distanceThreshold&&
            c.normal==shipped.normalThresholdDegrees&&c.sigma==shipped.clampSigma&&
            c.edge==shipped.edgeProtection&&c.edgeFrames==shipped.edgeMaxFrames&&c.slowClamp==shipped.clampSlowMotion)selected=i;}
    Check(selected<candidates.size(),"The shipped temporal preset is missing from the motion comparison");
    std::vector<unsigned> activeCandidates=options.surfaceCacheMotionTune?
        std::vector<unsigned>{0,12,13,14}:std::vector<unsigned>{0,selected};
    if(std::find(activeCandidates.begin(),activeCandidates.end(),selected)==activeCandidates.end())activeCandidates.push_back(selected);
    if(!options.surfaceCacheMotionTune&&candidates[selected].phases!=8)
        activeCandidates.push_back(12); // independent eight-phase raw-signal fairness control
    struct Metrics {
        double rawSquared=0,spatialSquared=0,temporalSquared=0,referenceEnergy=0;
        double rawFlicker=0,spatialFlicker=0,temporalFlicker=0;
        double historySum=0,motionPixels=0;std::uint64_t count=0,pairs=0,fastPairs=0,historyPixels=0,aboveTwo=0,newPixels=0;
        double edgeSquared=0,rawEdgeSquared=0,spatialEdgeSquared=0;std::vector<float> edgeAbsolute;
        void Merge(const Metrics& b){rawSquared+=b.rawSquared;spatialSquared+=b.spatialSquared;temporalSquared+=b.temporalSquared;referenceEnergy+=b.referenceEnergy;
            rawFlicker+=b.rawFlicker;spatialFlicker+=b.spatialFlicker;temporalFlicker+=b.temporalFlicker;
            historySum+=b.historySum;motionPixels+=b.motionPixels;count+=b.count;pairs+=b.pairs;fastPairs+=b.fastPairs;historyPixels+=b.historyPixels;aboveTwo+=b.aboveTwo;newPixels+=b.newPixels;
            edgeSquared+=b.edgeSquared;rawEdgeSquared+=b.rawEdgeSquared;spatialEdgeSquared+=b.spatialEdgeSquared;
            edgeAbsolute.insert(edgeAbsolute.end(),b.edgeAbsolute.begin(),b.edgeAbsolute.end());}
    };
    std::array<Metrics,15> allMoving,allSettled;
    const auto report=[&](const char* path,const Candidate& candidate,const char* interval,Metrics& m){
        Check(m.count>1000&&m.pairs>1000&&m.historyPixels>1000&&!m.edgeAbsolute.empty(),"Motion metric has insufficient stable / edge receivers");
        const auto at=std::size_t(.95*double(m.edgeAbsolute.size()-1));std::nth_element(m.edgeAbsolute.begin(),m.edgeAbsolute.begin()+at,m.edgeAbsolute.end());
        std::cout<<"SURFACE CACHE MOTION path="<<path<<" candidate="<<candidate.name<<" interval="<<interval
            <<" max_frames="<<candidate.frames<<" moving_frames="<<candidate.moving<<" motion_threshold="<<candidate.threshold
            <<" applied_motion_threshold="<<candidate.threshold*motionThresholdScale<<" clamp="<<candidate.clamp<<" phases="<<candidate.phases
            <<" distance_threshold="<<candidate.distance<<" normal_threshold="<<candidate.normal<<" clamp_sigma="<<candidate.sigma
            <<" edge_protection="<<candidate.edge<<" edge_frames="<<candidate.edgeFrames
            <<" clamp_slow_motion="<<candidate.slowClamp
            <<" raw_mse="<<m.rawSquared/double(m.count)<<" spatial_mse="<<m.spatialSquared/double(m.count)<<" temporal_mse="<<m.temporalSquared/double(m.count)
            <<" raw_error_delta_mse="<<m.rawFlicker/double(m.pairs)<<" spatial_error_delta_mse="<<m.spatialFlicker/double(m.pairs)<<" temporal_error_delta_mse="<<m.temporalFlicker/double(m.pairs)
            <<" raw_edge_mse="<<m.rawEdgeSquared/double(m.edgeAbsolute.size())<<" spatial_edge_mse="<<m.spatialEdgeSquared/double(m.edgeAbsolute.size())
            <<" edge_mse="<<m.edgeSquared/double(m.edgeAbsolute.size())<<" edge_p95_abs="<<m.edgeAbsolute[at]
            <<" history_mean="<<m.historySum/double(m.historyPixels)<<" history_above2="<<double(m.aboveTwo)/double(m.historyPixels)
            <<" new_fraction="<<double(m.newPixels)/double(m.historyPixels)<<" motion_pixels="<<m.motionPixels/double(m.pairs)<<" fast_fraction="<<double(m.fastPairs)/double(m.pairs)
            <<" reference_mean="<<m.referenceEnergy/double(m.count)<<" pixels="<<m.count<<" pairs="<<m.pairs<<std::endl;
    };
    const auto base=ProbePlacementCameraPreset(0,frame.OutputAspect());
    const auto inverseBase=glm::inverse(base.view);
    const auto forward=glm::normalize(-glm::vec3(inverseBase[2])),right=glm::normalize(glm::vec3(inverseBase[0]));
    const std::array<const char*,3> paths{"yaw","translation","combined"};
    double fairnessError=0;std::uint64_t fairnessFrames=0;
    for(unsigned pathIndex=0;pathIndex<(options.surfaceCacheMotionTune?1u:unsigned(paths.size()));++pathIndex) {
        // Eight poses out, eight poses back, eight stationary frames. Only
        // nine distinct poses need expensive independent references.
        std::array<Pose,9> poses;
        for(unsigned step=0;step<poses.size();++step){auto& pose=poses[step];pose.camera=base;
            const float amount=float(step);
            const float yaw=pathIndex==1?0.f:amount*(pathIndex==0?.005f:.0045f);
            const auto direction=glm::vec3(glm::rotate(glm::mat4(1),yaw,glm::vec3(0,1,0))*glm::vec4(forward,0));
            pose.camera.position=base.position+right*(pathIndex==0?0.f:amount*(pathIndex==1?.025f:.018f));
            pose.camera.view=glm::lookAt(pose.camera.position,pose.camera.position+direction,glm::vec3(0,1,0));
            frame.camera=pose.camera;temporal.enabled=false;gi.spatial.enabled=false;trace.rayResolution=4;trace.samplingSeed=0;placement.reset=true;
            render();const auto gbuffer=lab.ReadbackScreenProbeTrace();
            pose.coverage=lab.ReadbackSurfaceCacheProbeCoverage();finite(pose.coverage,"reference geometric coverage");
            Check(gbuffer.sourceViewPositions.size()==pixels&&gbuffer.sourceWorldNormals.size()==pixels,"Motion reference lost its GBuffer");
            pose.world.resize(pixels);pose.normals.resize(pixels);pose.surface.assign(pixels,-1);
            pose.valid.assign(pixels,false);pose.interior.assign(pixels,false);pose.edge.assign(pixels,false);
            const auto inverseView=glm::inverse(pose.camera.view);
            for(std::size_t i=0;i<pixels;++i){const auto p=gbuffer.sourceViewPositions[i],n=gbuffer.sourceWorldNormals[i];
                if(p.w<.5f||n.w<.5f||glm::dot(glm::vec3(n),glm::vec3(n))<.5f)continue;
                pose.valid[i]=true;pose.world[i]=glm::vec3(inverseView*glm::vec4(glm::vec3(p),1));pose.normals[i]=glm::normalize(glm::vec3(n));
                pose.surface[i]=surfaceId(pose.world[i],pose.normals[i]);}
            const auto* validMask=pose.valid.data();const auto* surfaces=pose.surface.data();
            const auto* worldPoints=pose.world.data();const auto* worldNormals=pose.normals.data();
            const auto* geometricCoverage=pose.coverage.data();auto* interiorMask=pose.interior.data();auto* edgeMask=pose.edge.data();
            for(unsigned y=3;y+3<frame.height;++y)for(unsigned x=3;x+3<frame.width;++x){const auto i=std::size_t(y)*frame.width+x;
                if(!validMask[i])continue;const int centerSurface=surfaces[i];const auto centerPoint=worldPoints[i],centerNormal=worldNormals[i];
                bool interior=centerSurface>=0,edge=false;
                for(int dy=-3;dy<=3;++dy)for(int dx=-3;dx<=3;++dx){const auto at=std::size_t(int(y)+dy)*frame.width+unsigned(int(x)+dx);
                    if(!validMask[at]||surfaces[at]!=centerSurface)interior=false;
                    if(std::abs(dx)>2||std::abs(dy)>2)continue;
                    if(!validMask[at]||glm::dot(centerNormal,worldNormals[at])<.8f||
                        std::abs(glm::dot(worldPoints[at]-centerPoint,centerNormal))>.01f)edge=true;}
                interiorMask[i]=interior&&geometricCoverage[i].w>.5f;edgeMask[i]=edge&&geometricCoverage[i].w>.5f;
                pose.edgeCount+=edgeMask[i]!=0;}
            pose.reference.assign(pixels,glm::vec4(0));trace.rayResolution=16;
            constexpr unsigned referenceSamples=4;
            for(unsigned seed=100;seed<100+referenceSamples;++seed){trace.samplingSeed=seed;render();const auto image=lab.ReadbackSurfaceCacheRawGi();finite(image,"motion high-ray reference");
                for(std::size_t i=0;i<pixels;++i)pose.reference[i]+=image[i]/float(referenceSamples);}
        }
        std::cout<<"SURFACE CACHE MOTION reference_ready path="<<paths[pathIndex]<<" unique_poses=9 high_ray_seeds=4 rays=256"<<std::endl;
        std::array<std::vector<glm::vec4>,24> baselineRaw;
        Metrics pathOldMoving;
        for(const auto candidateIndex:activeCandidates){const auto candidate=candidates[candidateIndex];
            gi.spatial.enabled=true;trace.rayResolution=motionRayResolution;trace.samplingSeed=0;temporal.enabled=true;
            temporal.maxFrames=candidate.frames;temporal.movingMaxFrames=candidate.moving;
            temporal.motionThresholdPixels=candidate.threshold*motionThresholdScale;temporal.neighborhoodClamp=candidate.clamp;temporal.directionPhases=candidate.phases;
            temporal.distanceThreshold=candidate.distance;temporal.normalThresholdDegrees=candidate.normal;temporal.clampSigma=candidate.sigma;
            temporal.edgeProtection=candidate.edge;temporal.edgeMaxFrames=candidate.edgeFrames;
            temporal.clampSlowMotion=candidate.slowClamp;
            frame.camera=poses[0].camera;frame.effects.cameraCut=true;temporal.reset=true;placement.reset=true;
            for(unsigned warm=0;warm<24;++warm)render();
            std::vector<float> previousRawError(pixels),previousSpatialError(pixels),previousTemporalError(pixels);
            unsigned previousPose=0;Metrics moving,settled;
            const auto makeErrors=[&](const auto& raw,const auto& spatial,const auto& accumulated,const Pose& pose,
                auto& rawError,auto& spatialError,auto& temporalError){
                const auto* r=raw.data();const auto* s=spatial.data();const auto* a=accumulated.data();const auto* reference=pose.reference.data();
                auto* re=rawError.data();auto* se=spatialError.data();auto* te=temporalError.data();
                for(std::size_t i=0;i<pixels;++i){re[i]=float(glm::dot(glm::dvec3(r[i])-glm::dvec3(reference[i]),luminance));
                    se[i]=float(glm::dot(glm::dvec3(s[i])-glm::dvec3(reference[i]),luminance));
                    te[i]=float(glm::dot(glm::dvec3(a[i])-glm::dvec3(reference[i]),luminance));}
            };
            auto previousRaw=lab.ReadbackSurfaceCacheRawGi(),previousSpatial=lab.ReadbackSurfaceCacheSpatialGi(),previousAccumulated=lab.ReadbackSurfaceCacheTemporalGi();
            finite(previousRaw,"warm raw GI");finite(previousSpatial,"warm spatial GI");finite(previousAccumulated,"warm accumulated GI");
            std::vector<std::uint8_t> previousReadable(pixels);for(std::size_t i=0;i<pixels;++i)previousReadable[i]=previousSpatial[i].w>0;
            makeErrors(previousRaw,previousSpatial,previousAccumulated,poses[0],previousRawError,previousSpatialError,previousTemporalError);
            for(unsigned frameIndex=0;frameIndex<24;++frameIndex){const unsigned poseIndex=frameIndex<8?frameIndex+1:frameIndex<16?15-frameIndex:0;
                const auto& pose=poses[poseIndex];const auto& previous=poses[previousPose];frame.camera=pose.camera;render();
                const auto raw=lab.ReadbackSurfaceCacheRawGi(),spatial=lab.ReadbackSurfaceCacheSpatialGi(),accumulated=lab.ReadbackSurfaceCacheTemporalGi(),history=lab.ReadbackSurfaceCacheTemporalHistory();
                finite(raw,"moving raw GI");finite(spatial,"moving spatial GI");finite(accumulated,"moving accumulated GI");finite(history,"moving history counts");
                if(candidateIndex==0)baselineRaw[frameIndex]=raw;
                else if(candidate.phases==8){fairnessError=std::max(fairnessError,maximumDifference(raw,baselineRaw[frameIndex]));++fairnessFrames;}
                std::vector<float> rawError(pixels),spatialError(pixels),temporalError(pixels);
                makeErrors(raw,spatial,accumulated,pose,rawError,spatialError,temporalError);
                auto& metric=frameIndex<16?moving:settled;
                const auto edgeStart=metric.edgeAbsolute.size();metric.edgeAbsolute.resize(edgeStart+pose.edgeCount);
                std::size_t edgeWrite=edgeStart;auto* edgeErrors=metric.edgeAbsolute.data();
                const auto previousVp=previous.camera.projection*previous.camera.view;
                for(std::size_t i=0;i<pixels;++i){
                    Check(history[i].w>=0&&history[i].w<=float(candidate.frames)+.001f,"Motion history count exceeded configured cap");
                    if(!pose.valid[i]||pose.coverage[i].w<.5f||spatial[i].w<=0){
                        Check(glm::length(glm::vec3(accumulated[i]))<1e-6f&&history[i].w==0,"Motion history filled background, unsupported geometry or unreadable current GI");continue;}
                    if(pose.edge[i]){metric.edgeSquared+=double(temporalError[i])*temporalError[i];
                        metric.rawEdgeSquared+=double(rawError[i])*rawError[i];metric.spatialEdgeSquared+=double(spatialError[i])*spatialError[i];
                        edgeErrors[edgeWrite++]=std::abs(temporalError[i]);}
                    if(!pose.interior[i])continue;
                    ++metric.count;metric.rawSquared+=double(rawError[i])*rawError[i];metric.spatialSquared+=double(spatialError[i])*spatialError[i];
                    metric.temporalSquared+=double(temporalError[i])*temporalError[i];metric.referenceEnergy+=glm::dot(glm::dvec3(pose.reference[i]),luminance);
                    ++metric.historyPixels;metric.historySum+=history[i].w;if(history[i].w>2.001f)++metric.aboveTwo;if(history[i].w<=1.001f)++metric.newPixels;
                    // Independently reproject the same world point into the
                    // prior pose, checking all four same-plane interiors.
                    const auto clip=previousVp*glm::vec4(pose.world[i],1);if(clip.w<=0)continue;
                    const auto xy=(glm::vec2(clip)/clip.w*.5f+.5f)*glm::vec2(frame.width,frame.height)-.5f;
                    const auto origin=glm::ivec2(glm::floor(xy));if(origin.x<0||origin.y<0||origin.x+1>=int(frame.width)||origin.y+1>=int(frame.height))continue;
                    const auto fraction=glm::fract(xy);glm::dvec3 oldError(0);bool compatible=true;
                    for(unsigned corner=0;corner<4;++corner){const unsigned dx=corner%2,dy=corner/2;const auto at=std::size_t(origin.y+int(dy))*frame.width+unsigned(origin.x+int(dx));
                        if(!previousReadable[at]||!previous.interior[at]||previous.surface[at]!=pose.surface[i]||glm::dot(previous.normals[at],pose.normals[i])<.995f||
                            std::abs(glm::dot(previous.world[at]-pose.world[i],pose.normals[i]))>.003f){compatible=false;break;}
                        const double weight=(dx?fraction.x:1-fraction.x)*(dy?fraction.y:1-fraction.y);
                        oldError+=glm::dvec3(previousRawError[at],previousSpatialError[at],previousTemporalError[at])*weight;
                    }
                    if(!compatible)continue;const glm::dvec3 delta=glm::dvec3(rawError[i],spatialError[i],temporalError[i])-oldError;
                    ++metric.pairs;metric.rawFlicker+=delta.x*delta.x;metric.spatialFlicker+=delta.y*delta.y;metric.temporalFlicker+=delta.z*delta.z;
                    const auto pixel=glm::vec2(float(i%frame.width),float(i/frame.width));const float motionPixels=glm::length(xy-pixel);metric.motionPixels+=motionPixels;
                    if(motionPixels>temporal.motionThresholdPixels)++metric.fastPairs;
                }
                metric.edgeAbsolute.resize(edgeWrite);
                if(frameIndex==7)save(std::string(paths[pathIndex])+"_"+candidate.name+"_peak");
                if(frameIndex==23)save(std::string(paths[pathIndex])+"_"+candidate.name+"_settled");
                previousRawError=std::move(rawError);previousSpatialError=std::move(spatialError);previousTemporalError=std::move(temporalError);previousPose=poseIndex;
                for(std::size_t i=0;i<pixels;++i)previousReadable[i]=spatial[i].w>0;
            }
            report(paths[pathIndex],candidate,"moving",moving);report(paths[pathIndex],candidate,"settled",settled);
            if(candidateIndex==0)pathOldMoving=moving;
            if(candidateIndex==selected&&candidateIndex!=0) {
                Check(moving.temporalSquared/double(moving.count)<pathOldMoving.temporalSquared/double(pathOldMoving.count),
                    std::string(paths[pathIndex])+" tuned motion history increased planar-interior reference error");
                Check(moving.temporalFlicker/double(moving.pairs)<pathOldMoving.temporalFlicker/double(pathOldMoving.pairs),
                    std::string(paths[pathIndex])+" tuned motion history failed to reduce aligned error fluctuation");
                Check(moving.edgeSquared/double(moving.edgeAbsolute.size())<=1.15*pathOldMoving.edgeSquared/double(pathOldMoving.edgeAbsolute.size()),
                    std::string(paths[pathIndex])+" tuned motion history increased geometric-boundary reference error");
                const auto percentile=[](const auto& metric){return metric.edgeAbsolute[std::size_t(.95*double(metric.edgeAbsolute.size()-1))];};
                Check(percentile(moving)<=1.15*percentile(pathOldMoving),
                    std::string(paths[pathIndex])+" tuned motion history increased geometric-boundary error outliers");
            }
            allMoving[candidateIndex].Merge(moving);allSettled[candidateIndex].Merge(settled);
        }
    }
    for(const auto candidate:activeCandidates){report("ALL",candidates[candidate],"moving",allMoving[candidate]);report("ALL",candidates[candidate],"settled",allSettled[candidate]);}
    Check(fairnessFrames>0&&fairnessError<2e-5,"Motion tuning candidates did not use the same raw direction phases");
    // Exploration still reports failures of the currently shipped candidate;
    // it must never count a quieter but smeared signal as a successful preset.
    const auto& old=allMoving[0];
    for(const auto candidate:std::array<unsigned,1>{selected}) {
        const auto& tuned=allMoving[candidate];
        Check(tuned.temporalSquared/double(tuned.count)<old.temporalSquared/double(old.count),
            "Tuned motion history failed to reduce error against the independent reference");
        Check(tuned.temporalFlicker/double(tuned.pairs)<.75*old.temporalFlicker/double(old.pairs),
            "Tuned motion history did not meaningfully reduce aligned sampling-error fluctuation");
        Check(tuned.edgeSquared/double(tuned.edgeAbsolute.size())<=1.15*old.edgeSquared/double(old.edgeAbsolute.size()),
            "Tuned motion history concealed excessive geometric-boundary error");
        const auto percentile=[](const auto& m){return m.edgeAbsolute[std::size_t(.95*double(m.edgeAbsolute.size()-1))];};
        Check(percentile(tuned)<=1.15*percentile(old),"Tuned motion history increased silhouette error outliers");
    }
    Check(allMoving[0].referenceEnergy/double(allMoving[0].count)>1e-4,"Motion fixture has no meaningful indirect lighting");
    if(options.renderSizeSet&&!options.surfaceCacheMotionTune) {
        // Measure continuous motion independently of all diagnostic readbacks.
        // Every case warms at the same pose, then submits twelve distinct
        // moving frames; MeasureFrames(frame,1) preserves the GI history chain.
        const std::array<const char*,3> names{"old","shipped_no_edge","shipped_edge"};
        for(unsigned mode=0;mode<names.size();++mode) {
            temporal=shipped;
            if(mode==0){const auto& c=candidates[0];temporal.maxFrames=c.frames;temporal.movingMaxFrames=c.moving;
                temporal.motionThresholdPixels=c.threshold;temporal.neighborhoodClamp=c.clamp;temporal.directionPhases=c.phases;
                temporal.distanceThreshold=c.distance;temporal.normalThresholdDegrees=c.normal;temporal.clampSigma=c.sigma;
                temporal.edgeProtection=c.edge;temporal.edgeMaxFrames=c.edgeFrames;temporal.clampSlowMotion=c.slowClamp;}
            else temporal.edgeProtection=mode==2;
            temporal.motionThresholdPixels*=motionThresholdScale;
            temporal.enabled=true;trace.rayResolution=motionRayResolution;trace.samplingSeed=0;
            frame.camera=base;frame.effects.cameraCut=true;temporal.reset=true;placement.reset=true;
            for(unsigned warm=0;warm<24;++warm)render();
            frame.effects.cameraCut=false;temporal.reset=false;placement.reset=false;
            double gpu=0,elapsed=0;
            constexpr unsigned measuredFrames=12;
            for(unsigned step=1;step<=measuredFrames;++step){auto camera=base;
                camera.position=base.position+right*(.02f*float(step));
                const auto direction=glm::vec3(glm::rotate(glm::mat4(1),.005f*float(step),glm::vec3(0,1,0))*glm::vec4(forward,0));
                camera.view=glm::lookAt(camera.position,camera.position+direction,glm::vec3(0,1,0));frame.camera=camera;
                const auto timing=lab.MeasureFrames(frame,1);Check(timing.gpuMilliseconds>0,lab.LastError());
                Check(std::isfinite(timing.gpuMilliseconds)&&std::isfinite(timing.elapsedMilliseconds)&&timing.elapsedMilliseconds>0,
                    "Invalid continuous-motion GPU timing");
                gpu+=timing.gpuMilliseconds;elapsed+=timing.elapsedMilliseconds;
            }
            std::cout<<"SURFACE CACHE MOTION PERF mode="<<names[mode]<<" size="<<frame.width<<'x'<<frame.height
                <<" rays="<<motionRayResolution*motionRayResolution<<" spacing="<<placement.baseSpacing
                <<" max_frames="<<temporal.maxFrames<<" moving_frames="<<temporal.movingMaxFrames<<" phases="<<temporal.directionPhases
                <<" edge_protection="<<temporal.edgeProtection<<" gpu_ms="<<gpu/measuredFrames<<" elapsed_ms="<<elapsed/measuredFrames
                <<" measured_frames="<<measuredFrames<<" warmup=24 readback=0 present=0"<<std::endl;
        }
        temporal=shipped;temporal.motionThresholdPixels*=motionThresholdScale;
        frame.camera=base;frame.effects.cameraCut=true;temporal.reset=true;placement.reset=true;render();render();
    }
    // Switching the edge rule must not mix history from two estimators.
    const bool previousEdgeProtection=temporal.edgeProtection;
    temporal.edgeProtection=!previousEdgeProtection;render();
    Check(!lab.PipelineStatistics().surfaceCacheHistoryUsed,"Edge-protection edit reused incompatible motion history");
    for(const auto& p:lab.ReadbackSurfaceCacheTemporalHistory())Check(p.w<=1.001f,"Edge-protection edit retained old frame counts");
    temporal.edgeProtection=previousEdgeProtection;render();render();
    Check(lab.PipelineStatistics().surfaceCacheHistoryUsed,"Restored edge protection failed to resume accumulation");
    frame.effects.shadows.sunIntensity=0;render();
    auto accumulated=lab.ReadbackSurfaceCacheTemporalGi();finite(accumulated,"unlit moving history");
    for(const auto& p:accumulated)Check(glm::length(glm::vec3(p))<1e-6f,"Light-off retained stale motion history energy");
    Check(!lab.PipelineStatistics().surfaceCacheHistoryUsed,"Light-off reused old moving history");
    // Restore and reuse lighting history before editing geometry, so the
    // geometry assertion cannot pass merely because the light also changed.
    frame.effects.shadows.sunIntensity=3;render();render();
    Check(lab.PipelineStatistics().surfaceCacheHistoryUsed,"Restored light failed to resume motion history");
    frame.screenProbeStudyState.occluderOffset=.2f;render();
    const auto history=lab.ReadbackSurfaceCacheTemporalHistory();finite(history,"edited motion history");
    Check(!lab.PipelineStatistics().surfaceCacheHistoryUsed,"Geometry edit retained old moving history");
    for(const auto& p:history)Check(p.w<=1.001f,"Geometry edit retained old effective frame counts");
    std::cout<<"Surface Cache continuous motion passed: raw_phase_fairness_max="<<fairnessError<<" aligned_frames="<<fairnessFrames
        <<" size="<<frame.width<<'x'<<frame.height<<" spacing="<<placement.baseSpacing
        <<" rays="<<motionRayResolution*motionRayResolution<<" threshold_scale="<<motionThresholdScale
        <<" warmup=24 motion=16 settle=8 reference=4x256; tuned-noise and geometry guards passed"<<std::endl;
}
