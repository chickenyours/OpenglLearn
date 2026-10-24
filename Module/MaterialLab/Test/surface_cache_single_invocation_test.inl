// Transport parity: compare actual legacy/candidate outputs, not a CPU copy of
// the ray marcher. Both variants use the same prepared scene, seed and phase.
void SurfaceCacheSingleInvocationQuality(MaterialLabModule& lab,const Options& options,bool outputsRound=false) {
    const char* label=outputsRound?"SINGLE PASS OUTPUTS":"SINGLE INVOCATION";
    auto fixture=options;fixture.sealedGiRoom=true;fixture.sdfBanquet=false;
    fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);
    frame.width=192;frame.height=128;frame.effects.outputSize={0,0};
    frame.present=false;frame.screenProbeStudyHud=false;frame.lightTime=0;
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::None;
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;auto& trace=placement.trace;
    gi.enabled=true;gi.useScreenProbes=true;gi.view=2;gi.reconstructionDebug=0;
    gi.probeDiagnostics=false;gi.probeOriginDiagnostics=false;gi.contact.enabled=false;
    gi.spatial.enabled=false;gi.temporal.enabled=false;gi.directionalReconstruction=true;
    placement.enabled=true;placement.adaptive=true;placement.freeze=false;placement.frameIndex=0;
    placement.baseSpacing=32;placement.candidateCount=8;
    trace.enabled=true;trace.showPanel=false;trace.cosineHemisphere=true;
    trace.maxSteps=512;trace.sdf.enabled=true;trace.sdf.maxSteps=256;
    trace.sdf.automaticQuality=false;trace.maxDistance=gi.maxDistance;
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());
        frame.effects.cameraCut=false;placement.reset=false;gi.temporal.reset=false;};
    const auto reset=[&](bool single){trace.singleInvocation=!outputsRound&&single;trace.singlePassOutputs=outputsRound&&single;
        frame.effects.cameraCut=true;placement.reset=true;gi.temporal.reset=true;};
    struct Snapshot {
        ScreenProbeTraceReadback screen,fallback;
        std::vector<glm::vec4> raw,spatial,temporal,history;
        Render::PipelineStatistics statistics;
    };
    const auto capture=[&]{Snapshot s;
        s.statistics=lab.PipelineStatistics();
        s.screen=lab.ReadbackScreenProbeTrace(false,true,true);
        Check(!s.screen.packed.empty(),lab.LastError());
        s.fallback=lab.ReadbackScreenProbeTrace(false,false,true);
        Check(!s.fallback.packed.empty(),lab.LastError());
        s.raw=lab.ReadbackSurfaceCacheRawGi();Check(!s.raw.empty(),lab.LastError());
        if(gi.spatial.enabled){s.spatial=lab.ReadbackSurfaceCacheSpatialGi();Check(!s.spatial.empty(),lab.LastError());}
        if(gi.temporal.enabled){s.temporal=lab.ReadbackSurfaceCacheTemporalGi();s.history=lab.ReadbackSurfaceCacheTemporalHistory();
            Check(!s.temporal.empty()&&!s.history.empty(),lab.LastError());}
        return s;};
    double maximumContinuousError=0,maximumRgbError=0;std::uint64_t exactMetadataChecks=0,packedTexels=0;
    const auto continuous=[&](float a,float b,bool rgb){
        Check(std::isfinite(a)&&std::isfinite(b),"Single-invocation parity contains a nonfinite value");
        const double error=std::abs(double(a)-double(b));
        // The same expressions compile in a different output/stage variant. Permit
        // only a small FP32 rounding envelope, not a percentage / image-noise
        // tolerance. Discrete branches and all public alpha stay exact below.
        const double tolerance=16.0*std::numeric_limits<float>::epsilon()*std::max({1.0,std::abs(double(a)),std::abs(double(b))});
        Check(error<=tolerance,"Single-invocation continuous channel exceeded the FP32 rounding envelope");
        maximumContinuousError=std::max(maximumContinuousError,error);if(rgb)maximumRgbError=std::max(maximumRgbError,error);
    };
    const auto exact=[&](float a,float b){
        Check(std::isfinite(a)&&std::isfinite(b)&&a==b,"Single-invocation changed status, metadata, alpha or history count");++exactMetadataChecks;};
    const auto comparePacked=[&](const auto& a,const auto& b){
        Check(a.size()==b.size()&&!a.empty()&&a.size()%4==0,"Single-invocation packed atlas changed extent");
        packedTexels+=a.size();
        for(std::size_t texel=0;texel<a.size();++texel)for(unsigned channel=0;channel<4;++channel){
            const auto part=texel%4;
            if((part==0&&channel==3)||part==2||(part==3&&channel==3))exact(a[texel][channel],b[texel][channel]);
            else continuous(a[texel][channel],b[texel][channel],part==0);
        }
    };
    const auto compareGi=[&](const auto& a,const auto& b){
        Check(a.size()==b.size(),"Single-invocation downstream GI changed extent");
        for(std::size_t pixel=0;pixel<a.size();++pixel){for(unsigned c=0;c<3;++c)continuous(a[pixel][c],b[pixel][c],true);
            exact(a[pixel].w,b[pixel].w);}
    };
    const auto compare=[&](const Snapshot& a,const Snapshot& b){
        Check(a.screen.atlasSize==b.screen.atlasSize&&a.screen.probeSlots==b.screen.probeSlots,
            "Single-invocation changed the ray allocation");
        comparePacked(a.screen.packed,b.screen.packed);comparePacked(a.fallback.packed,b.fallback.packed);
        compareGi(a.raw,b.raw);compareGi(a.spatial,b.spatial);compareGi(a.temporal,b.temporal);compareGi(a.history,b.history);
        Check(!a.statistics.screenProbeSingleInvocation&&!a.statistics.probeSdfSingleInvocation,"Legacy variant used the candidate path");
        Check(!a.statistics.screenProbeSinglePassOutputs&&!a.statistics.probeSdfSinglePassOutputs,"Legacy variant used four-output tracing");
        if(outputsRound) {
            Check(!b.statistics.screenProbeSingleInvocation&&!b.statistics.probeSdfSingleInvocation,"Four-output variant used geometry tracing");
            Check(b.statistics.screenProbeSinglePassOutputs&&b.statistics.probeSdfSinglePassOutputs,"Four-output candidate silently fell back");
        } else if(b.statistics.screenProbeSingleInvocationSupported)
            Check(b.statistics.screenProbeSingleInvocation&&b.statistics.probeSdfSingleInvocation,"Supported candidate silently fell back");
        else Check(!b.statistics.screenProbeSingleInvocation&&!b.statistics.probeSdfSingleInvocation,"Unsupported candidate did not fall back");
    };
    // Prepare assets before the first pair. SDF quality is fixed, and both
    // members of each pair render the exact same explicit placement phase.
    frame.camera=SealedGiRoomCameraPreset(0,frame.OutputAspect());trace.rayResolution=4;reset(false);render();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(180);
    while(lab.ProbeSdfQualityStatistics().pending){render();Check(std::chrono::steady_clock::now()<deadline,"Single-invocation SDF preparation timed out");}
    std::uint64_t geometryUpdates=0;bool supported=false;unsigned pairs=0;
    constexpr std::array<std::uint32_t,3> seeds{{0,29,0x13579u}};
    for(unsigned test=0;test<6;++test){
        frame.width=test%2?191:192;frame.height=test%2?123:128;
        frame.camera=SealedGiRoomCameraPreset(test%2,frame.OutputAspect());
        trace.rayResolution=test==5?16:(test%2?8:4);trace.samplingSeed=seeds[test/2];
        placement.candidateCount=test%2?4:8;gi.safeProbeOrigins=test!=5;
        frame.screenProbeStudyState.occluderOffset=test>=4?.17f:0.f;
        reset(false);render();const auto previous=capture();
        geometryUpdates+=previous.statistics.surfaceCacheGeometryUploadedTexels;
        reset(true);render();const auto candidate=capture();compare(previous,candidate);++pairs;
        supported=outputsRound||candidate.statistics.screenProbeSingleInvocationSupported;
        std::cout<<label<<" pair="<<test<<" seed="<<trace.samplingSeed<<" camera="<<test%2
            <<" ray_resolution="<<trace.rayResolution<<" safe_origins="<<gi.safeProbeOrigins
            <<" size="<<frame.width<<'x'<<frame.height<<" atlas="<<candidate.screen.atlasSize.x<<'x'<<candidate.screen.atlasSize.y
            <<" supported="<<supported<<" screen_actual="<<(outputsRound?candidate.statistics.screenProbeSinglePassOutputs:candidate.statistics.screenProbeSingleInvocation)
            <<" sdf_actual="<<(outputsRound?candidate.statistics.probeSdfSinglePassOutputs:candidate.statistics.probeSdfSingleInvocation)<<std::endl;
        if(!supported){std::cout<<"SINGLE INVOCATION SKIP unsupported_geometry_shader fallback_parity=passed experiment_verified=0"<<std::endl;return;}
    }
    Check(geometryUpdates>0,"Single-invocation fixture did not update dynamic geometry");
    if(outputsRound) {
        // The production transport remains active while downstream diagnostic
        // reconstruction substitutes a known response. These pairs also check
        // returning from each debug view rather than only its screenshot.
        frame.width=192;frame.height=128;trace.rayResolution=4;
        frame.camera=SealedGiRoomCameraPreset(0,frame.OutputAspect());gi.safeProbeOrigins=true;
        for(unsigned debug=1;debug<=4;++debug) {
            gi.reconstructionDebug=debug;trace.samplingSeed=seeds[debug%seeds.size()];
            reset(false);render();const auto a=capture();reset(true);render();const auto b=capture();compare(a,b);++pairs;
            std::cout<<label<<" debug="<<debug<<" transport_parity=1"<<std::endl;
        }
        gi.reconstructionDebug=0;
    }
    // The production 64-frame stationary history uses the same committed seeds
    // in both runs. This compares accumulated output as well as the last raw rays.
    frame.width=192;frame.height=128;frame.camera=SealedGiRoomCameraPreset(0,frame.OutputAspect());
    frame.screenProbeStudyState.occluderOffset=0;placement.candidateCount=8;gi.safeProbeOrigins=true;
    trace.rayResolution=4;trace.samplingSeed=29;gi.spatial.enabled=true;gi.temporal.enabled=true;
    gi.temporal.staticConvergence=true;gi.temporal.maxFrames=10;
    reset(false);for(unsigned frameIndex=0;frameIndex<64;++frameIndex)render();const auto previous=capture();
    reset(true);for(unsigned frameIndex=0;frameIndex<64;++frameIndex)render();const auto candidate=capture();
    compare(previous,candidate);++pairs;
    std::uint64_t sixtyFourPixels=0;for(const auto& pixel:candidate.history)sixtyFourPixels+=pixel.w>=63.99f;
    Check(sixtyFourPixels>100,"Single-invocation fixture did not exercise 64-frame stationary accumulation");
    std::uint64_t knownBlackPixels=0,unknownPixels=0;unsigned automaticFlagResets=0;
    if(outputsRound) {
        // A change to execution mode is a history compatibility change even
        // when the caller does not explicitly cut the camera or reset probes.
        for(bool enabled:{false,true}) {
            trace.singlePassOutputs=enabled;render();const auto history=lab.ReadbackSurfaceCacheTemporalHistory();
            Check(!history.empty(),"Missing single-pass compatibility history");std::uint64_t currentPixels=0;
            for(auto value:history){Check(value.w>=0&&value.w<=1.001f,"Single-pass mode toggle retained incompatible history");currentPixels+=value.w>.99f;}
            Check(currentPixels>100,"Single-pass compatibility reset did not accumulate current receivers");++automaticFlagResets;
        }
        const auto savedAreas=frame.effects.areaLights;const auto savedCache=gi.cache;
        const auto energy=[](const auto& image){double sum=0;for(auto value:image)sum+=glm::dot(glm::dvec3(value),glm::dvec3(value));return sum;};
        for(unsigned validity=0;validity<2;++validity) {
            frame.effects.areaLights=savedAreas;gi.cache=savedCache;
            if(validity==0)for(unsigned light=0;light<frame.effects.areaLights.count;++light)frame.effects.areaLights.lights[light].enabled=false;
            else {
                auto instances=lab.PreparedProbeSdfScene()->Instances();for(auto& instance:instances)instance.localToWorld[3].x+=50;
                Render::ProbeGlobalSdfSettings tiny;tiny.counts=glm::uvec3(2);
                const auto remote=Render::BuildProbeSdfScene(instances,tiny);gi.cache=Render::BuildSurfaceCache(*remote);
            }
            reset(false);render();const auto a=capture();reset(true);render();const auto b=capture();compare(a,b);++pairs;
            Check(energy(a.raw)<1e-8&&energy(b.raw)<1e-8&&energy(a.temporal)<1e-8&&energy(b.temporal)<1e-8,
                "Four-output tracing fabricated black or unknown cache energy");
            std::uint64_t pixels=0;for(auto value:b.raw)pixels+=validity==0?value.w>0:value.w<=0;
            Check(pixels>100,"Four-output validity fixture did not cover enough receiver pixels");
            if(validity==0)knownBlackPixels=pixels;else unknownPixels=pixels;
            std::cout<<label<<" validity="<<(validity==0?"known_black":"unknown_cache")<<" pixels="<<pixels<<" rgb_zero=1"<<std::endl;
        }
        frame.effects.areaLights=savedAreas;gi.cache=savedCache;
    }
    std::cout<<label<<" COMPLETE backend="<<(options.vulkan?"Vulkan":"OpenGL")<<" supported="<<supported
        <<" pairs="<<pairs<<" packed_texels="<<packedTexels<<" exact_metadata_checks="<<exactMetadataChecks
        <<" max_continuous_error="<<maximumContinuousError<<" max_rgb_error="<<maximumRgbError
        <<" geometry_uploaded_texels="<<geometryUpdates<<" static64_pixels="<<sixtyFourPixels
        <<" known_black_pixels="<<knownBlackPixels<<" unknown_pixels="<<unknownPixels
        <<" automatic_flag_resets="<<automaticFlagResets
        <<" padding_included=1 public_alpha_exact=1 transport_math_unchanged=1"<<std::endl;
}
