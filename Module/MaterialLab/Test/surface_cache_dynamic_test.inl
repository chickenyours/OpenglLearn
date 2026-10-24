// Include after surface_cache_geometry_quality_test.inl. Its double-precision
// primary-ray oracle supplies object ownership independently of GI masks.
// The reference traces the actual safe receiver, sharing the completed
// single-bounce Surface Cache; it is not path-traced ground truth.
void SurfaceCacheDynamicQuality(MaterialLabModule& lab,const Options& options) {
    using SurfaceCacheGeometryFixture::Oracle;
    Check(options.vulkan,"Dynamic GI quality requires Vulkan");
    auto fixture=options;fixture.sealedGiRoom=true;fixture.sdfBanquet=false;fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);frame.width=options.renderSizeSet?options.benchmarkWidth:320;
    frame.height=options.renderSizeSet?options.benchmarkHeight:200;frame.effects.outputSize={0,0};frame.present=false;frame.screenProbeStudyHud=false;
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::None;
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;auto& trace=placement.trace;
    gi.enabled=true;gi.useScreenProbes=true;gi.view=0;gi.reconstructionDebug=0;gi.probeDiagnostics=false;gi.probeOriginDiagnostics=false;
    gi.safeProbeOrigins=true;gi.rasterSourcePositions=true;gi.directionalReconstruction=true;gi.cacheBilinear=true;gi.collectProbeCoverage=true;
    gi.spatial={};gi.temporal={};gi.contact={};gi.temporal.staticConvergence=true;gi.temporal.retainHistoryOnObjectMotion=true;
    placement.enabled=true;placement.adaptive=true;placement.cornerRefinement=true;placement.curvatureRefinement=true;
    placement.curvatureAwareWeights=true;placement.continuousInterpolation=true;placement.freeze=false;placement.frameIndex=0;
    placement.baseSpacing=32;placement.candidateCount=8;trace.enabled=true;trace.showPanel=false;trace.cosineHemisphere=true;
    trace.rayResolution=8;trace.samplingSeed=0;trace.maxSteps=512;trace.sdf.enabled=true;trace.sdf.maxSteps=256;trace.maxDistance=gi.maxDistance;
    const auto baselineGi=gi;const auto baselinePlacement=placement;
    // Two contiguous laps reuse the same nine independently referenced poses.
    // No extra reference rays are needed to expose sustained motion behavior.
    constexpr unsigned stages=6,regions=6,segments=6,warmFrames=64,movingFrames=32,settleFrames=16,poseCount=9;
    constexpr unsigned totalFrames=warmFrames+movingFrames+settleFrames;
    constexpr std::array<const char*,stages> stageNames{{"probe_raw","probe_spatial","contact_unfiltered","contact_filtered","temporal","history_accumulation"}};
    const std::array<const char*,regions> regionNames{{"all_geometry","upright_oval","contact_0to70cm","remote_planar","movable_cube",
        options.surfaceCacheDynamicRecoveryTest?"mover_swept_floor":"vacated_floor"}};
    constexpr std::array<const char*,segments> segmentNames{{"static64_current","moving","settling","moving_sustained_lap2","settling_first8","settling_after8"}};
    constexpr std::array<const char*,3> viewNames{{"oval_camera_stop_start","contact_camera_stop_start","cube_fixed_camera"}};
    constexpr std::array<const char*,10> variantNames{{"shipped","consistent_spatial","local_camera_history","motion_stability","motion_stability_filtered","motion_stability_recovery",
        "current_stability","object_reprojection","disocclusion_filter","object_reprojection_disocclusion_filter"}};
    const unsigned baselineVariant=options.surfaceCacheDynamicRecoveryTest?6:0;
    constexpr std::array<unsigned,2> periods{{8,32}};
    const auto pixels=std::size_t(frame.width)*frame.height;const glm::dvec3 luma(.2126,.7152,.0722);
    const auto mean=[](double sum,std::uint64_t count){return sum/double(std::max<std::uint64_t>(1,count));};
    const auto square=[](glm::dvec3 value){return glm::dot(value,value)/3.;};
    const auto finiteAny=[&](const auto& values,const char* label){Check(!values.empty(),std::string("Missing dynamic GI ")+label+": "+lab.LastError());
        for(const auto& p:values)for(unsigned c=0;c<4;++c)Check(std::isfinite(p[c]),"Nonfinite dynamic GI data");};
    const auto finite=[&](const auto& values,const char* label){finiteAny(values,label);Check(values.size()==pixels,std::string("Wrong dynamic GI extent: ")+label);};
    const auto hash=[](const auto& values){std::uint64_t result=1469598103934665603ull;
        const auto* bytes=reinterpret_cast<const unsigned char*>(values.data());
        for(std::size_t i=0;i<values.size()*sizeof(values[0]);++i){result^=bytes[i];result*=1099511628211ull;}return result;};
    const auto energy=[](const auto& values){double sum=0;for(const auto& p:values)sum+=glm::length(glm::dvec3(p));return sum;};
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;placement.reset=false;gi.temporal.reset=false;};
    const auto reset=[&]{frame.effects.cameraCut=true;placement.reset=true;gi.temporal.reset=true;};
    const auto ready=[&]{const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
        while(lab.ProbeSdfQualityStatistics().pending){render();Check(std::chrono::steady_clock::now()<deadline,"Dynamic GI SDF preparation timed out");}};
    const auto outputPath=[&](const std::string& label){auto p=std::filesystem::path(options.screenshot);
        p.replace_filename(p.stem().string()+"_"+label+p.extension().string());return p.string();};
    const auto saveTone=[&](const auto& values,const std::string& label){if(options.screenshot.empty())return;
        FramePixels image;image.width=frame.width;image.height=frame.height;image.rgba.resize(pixels*4);
        for(std::size_t i=0;i<pixels;++i){for(unsigned c=0;c<3;++c){double value=4*std::max(0.,double(values[i][c]));value=value/(1+value);
            value=value<=.0031308?12.92*value:1.055*std::pow(value,1/2.4)-.055;
            image.rgba[4*i+c]=std::uint8_t(std::lround(std::clamp(value,0.,1.)*255));}image.rgba[4*i+3]=255;}Save(image,outputPath(label));};
    const auto configure=[&](unsigned variant){gi=baselineGi;placement=baselinePlacement;gi.temporal.motionConsistentSpatialReuse=variant==1;
        gi.temporal.localCameraHistory=variant==2;
        gi.temporal.motionStability=variant>=3;gi.temporal.motionSpatialFilter=variant==4;gi.temporal.motionDisocclusionRecovery=variant==5;
        gi.temporal.objectMotionReprojection=variant==7||variant==9;gi.temporal.disocclusionFilter=variant==8||variant==9;
        trace.samplingSeed=0;placement.frameIndex=0;gi.contact.referenceReceiver=false;};

    const auto& roomObjects=SealedGiRoomObjects();unsigned ovalIndex=unsigned(roomObjects.size()),prismIndex=ovalIndex,cubeIndex=ovalIndex;
    std::vector<SealedGiRoomMaterial> materials;for(unsigned i=0;i<roomObjects.size();++i){materials.push_back(SealedGiRoomObjectMaterial(i));
        if(std::string(roomObjects[i].name)=="Porcelain upright oval")ovalIndex=i;
        if(std::string(roomObjects[i].name)=="Porcelain tall prism")prismIndex=i;
        if(roomObjects[i].movable)cubeIndex=i;}
    Check(ovalIndex<roomObjects.size()&&prismIndex<roomObjects.size()&&cubeIndex<roomObjects.size(),"Dynamic GI fixture lacks its ellipsoid/prism/movable cube");
    const auto& oval=roomObjects[ovalIndex];const auto& prism=roomObjects[prismIndex];const auto& cube=roomObjects[cubeIndex];
    const auto ovalInverse=glm::inverse(ProbePlacementTransform(oval,{})),prismInverse=glm::inverse(ProbePlacementTransform(prism,{}));
    const auto originalCubeInverse=glm::inverse(ProbePlacementTransform(cube,{}));
    std::array<glm::mat4,poseCount> sweptCubeInverses;
    for(unsigned step=0;step<poseCount;++step){ProbePlacementSceneSettings moved;moved.occluderOffset=.15f*float(step);
        sweptCubeInverses[step]=glm::inverse(ProbePlacementTransform(cube,moved));}
    const auto boxDistance=[](glm::vec3 p,const glm::mat4& inverse,glm::vec3 scale){const auto q=glm::abs(glm::vec3(inverse*glm::vec4(p,1)))*scale-scale;
        return double(glm::length(glm::max(q,glm::vec3(0)))+std::min(std::max(q.x,std::max(q.y,q.z)),0.f));};
    const auto ovalDistance=[&](glm::vec3 world){const auto a=glm::dvec3(oval.scale),a2=a*a;
        const auto p=glm::abs(glm::dvec3(ovalInverse*glm::vec4(world,1))*a);
        if(glm::dot(p/a,p/a)<=1.)return (glm::length(p/a)-1.)*std::min({a.x,a.y,a.z});
        const auto equation=[&](double t){const auto q=a*p/(a2+t);return glm::dot(q,q)-1.;};
        double low=0,high=std::max(1.,glm::length(a*p));while(equation(high)>0)high*=2;
        for(unsigned step=0;step<40;++step){const double mid=.5*(low+high);if(equation(mid)>0)low=mid;else high=mid;}
        return glm::length(p-a2*p/(a2+.5*(low+high)));};
    struct Pose {Render::PipelineCamera camera;float objectOffset=0;
        std::vector<glm::vec4> reference,split;std::vector<glm::vec3> world,normal;
        std::vector<int> owner;std::vector<std::uint8_t> roi,band;std::array<std::uint64_t,regions> counts{};};
    const auto setPose=[&](const Pose& pose){frame.camera=pose.camera;frame.screenProbeStudyState.occluderOffset=pose.objectOffset;};
    const auto captureGeometry=[&](Pose& pose,unsigned view){auto objects=roomObjects;objects[cubeIndex].position.x+=pose.objectOffset;
        Oracle oracle;oracle.Initialize(objects,materials);const auto sources=lab.ReadbackScreenProbeSources();
        finite(sources.sourceViewPositions,"reference positions");finite(sources.sourceWorldNormals,"reference normals");
        pose.world.resize(pixels);pose.normal.resize(pixels);pose.owner.assign(pixels,-1);pose.roi.assign(pixels,0);pose.band.assign(pixels,0);
        const auto inverse=glm::dmat4(glm::inverse(pose.camera.view));
        ProbePlacementSceneSettings moving;moving.occluderOffset=pose.objectOffset;const auto cubeInverse=glm::inverse(ProbePlacementTransform(cube,moving));
        std::uint64_t rejected=0;
        for(std::size_t i=0;i<pixels;++i){const auto vp=sources.sourceViewPositions[i],nd=sources.sourceWorldNormals[i];
            if(vp.w<.5f||nd.w<.5f||glm::dot(glm::vec3(nd),glm::vec3(nd))<.5f)continue;
            const auto w=glm::dvec3(inverse*glm::dvec4(glm::dvec3(vp),1)),ray=w-glm::dvec3(pose.camera.position);const double distance=glm::length(ray);
            if(!(distance>0))continue;const auto hit=oracle.Nearest(glm::dvec3(pose.camera.position),ray/distance,distance+.005);
            if(hit.triangle<0||std::abs(hit.t-distance)>.003){++rejected;continue;}
            const int owner=int(oracle.triangles[unsigned(hit.triangle)].object);const auto p=glm::vec3(w),n=glm::normalize(glm::vec3(nd));
            pose.owner[i]=owner;pose.world[i]=p;pose.normal[i]=n;std::uint8_t mask=1;
            if(owner==int(ovalIndex))mask|=2;
            double gap=-1;if(owner==int(ovalIndex))gap=boxDistance(p,prismInverse,prism.scale);
            else if(owner==int(prismIndex))gap=ovalDistance(p);
            if(view==2&&owner==0&&n.y>.99f)gap=boxDistance(p,cubeInverse,cube.scale);
            if(gap>=-.015&&gap<.70){mask|=4;pose.band[i]=gap<.03?1:gap<.35?2:3;}
            const bool planar=owner>=0&&owner<=8&&std::max({std::abs(n.x),std::abs(n.y),std::abs(n.z)})>.999f;
            if(planar&&ovalDistance(p)>.8&&boxDistance(p,prismInverse,prism.scale)>.8&&boxDistance(p,cubeInverse,cube.scale)>.8)mask|=8;
            if(owner==int(cubeIndex))mask|=16;
            const auto local=glm::vec3(originalCubeInverse*glm::vec4(p,1));
            if(owner==0&&n.y>.99f){bool footprint=std::abs(local.x)<.96f&&std::abs(local.z)<.96f;
                if(options.surfaceCacheDynamicRecoveryTest)for(const auto& inverse:sweptCubeInverses){const auto swept=glm::vec3(inverse*glm::vec4(p,1));
                    footprint=footprint||(std::abs(swept.x)<.96f&&std::abs(swept.z)<.96f);}
                if(footprint)mask|=32;}
            pose.roi[i]=mask;for(unsigned r=0;r<regions;++r)pose.counts[r]+=(mask&(1u<<r))!=0;}
        std::cout<<"DYNAMIC GI geometry view="<<viewNames[view]<<" offset="<<pose.objectOffset<<" oracle_rejected="<<rejected
            <<" geometry_roi=1 production_mask_selects_roi=0";for(unsigned r=0;r<regions;++r)std::cout<<' '<<regionNames[r]<<'='<<pose.counts[r];std::cout<<std::endl;};
    const auto reference=[&](Pose& pose,unsigned view,unsigned step){configure(0);setPose(pose);
        gi.temporal.enabled=false;gi.spatial.enabled=false;gi.contact.referenceReceiver=true;gi.contact.raysPerPixel=32;gi.contact.filterStrength=0;
        trace.samplingSeed=1009;reset();render();ready();captureGeometry(pose,view);
        // Exclude cache preparation and relighting from the independent seeds.
        for(unsigned f=0;f<4;++f)render();pose.reference.assign(pixels,glm::vec4(0));pose.split.assign(pixels,glm::vec4(0));
        for(unsigned seed=0;seed<8;++seed){trace.samplingSeed=1009+73*seed;render();const auto value=lab.ReadbackSurfaceCacheContactUnfilteredGi();finite(value,"safe-receiver reference");
            for(std::size_t i=0;i<pixels;++i){pose.reference[i]+=value[i]/8.f;pose.split[i]+=value[i]*(seed<4?.25f:-.25f);}}
        std::uint64_t unknown=0,retained=0;double floor=0;
        for(std::size_t i=0;i<pixels;++i)if(pose.roi[i]){if(!(pose.reference[i].w>0)){
                for(unsigned r=0;r<regions;++r)pose.counts[r]-=(pose.roi[i]&(1u<<r))!=0;pose.roi[i]=0;++unknown;
            }else{++retained;floor+=.25*square(glm::dvec3(pose.split[i]));}}
        Check(retained>100,"Dynamic GI geometry reference contains too few readable receivers");
        std::cout<<"DYNAMIC GI reference view="<<viewNames[view]<<" step="<<step<<" spp=256 split_spp=128 reference_rgb_floor="<<mean(floor,retained)
            <<" retained="<<retained<<" excluded_unknown="<<unknown<<" warm_frames=4 warm_rays_excluded=1 safe_receiver_origin=1 shared_cache=1 path_truth=0"<<std::endl;
        if(step==0||step==8)saveTone(pose.reference,std::string(viewNames[view])+"_pose"+std::to_string(step)+"_reference_tone4");};

    struct Sample {std::array<std::vector<glm::vec4>,stages> image;std::vector<glm::vec4> history,mask;std::vector<std::uint8_t> exposureAge;
        std::array<std::uint64_t,12> upstream{};std::array<std::uint64_t,stages+1> staticHash{};const Pose* pose=nullptr;
        std::uint64_t unknown=0,maskZero=0,selected=0,readable=0,mature=0,metadata=0,extended=0;double historySum=0,maxHistory=0;bool historyUsed=false;};
    const auto sample=[&](const Pose& pose){Sample value;value.pose=&pose;
        value.exposureAge.assign(pixels,0);
        value.image={lab.ReadbackSurfaceCacheRawGi(),lab.ReadbackSurfaceCacheSpatialGi(),lab.ReadbackSurfaceCacheContactUnfilteredGi(),
            lab.ReadbackSurfaceCacheContactGi(),lab.ReadbackSurfaceCacheTemporalGi()};
        value.history=lab.ReadbackSurfaceCacheTemporalHistory();value.mask=lab.ReadbackSurfaceCacheContactMask();
        value.image[5]=value.history; // Actual GPU EMA before the optional output filter; RGB only enters quality metrics.
        for(unsigned s=0;s<stages;++s){finite(value.image[s],stageNames[s]);value.staticHash[s]=hash(value.image[s]);}
        finite(value.history,"history");finite(value.mask,"contact mask");value.staticHash[stages]=hash(value.history);
        const auto rays=lab.ReadbackScreenProbeTrace(false);const auto filtered=lab.ReadbackSurfaceCacheSpatialRays();
        const auto coverage=lab.ReadbackSurfaceCacheProbeCoverage(),risk=lab.ReadbackSurfaceCacheContactProbeRisk();const auto probes=lab.ReadbackScreenProbePlacement();
        finite(coverage,"probe coverage");finiteAny(risk,"probe contact risk");finiteAny(filtered,"spatial ray atlas");
        finiteAny(rays.radianceStatus,"source ray radiance");finiteAny(rays.hitWorldDistance,"source ray hit distances");
        finiteAny(rays.hitPixelStepsMip,"source ray hit metadata");finiteAny(rays.rayDirectionDepthFetches,"source ray directions");
        const auto rayCount=rays.radianceStatus.size();Check(rays.rayResolution==8&&rayCount==std::size_t(rays.probeSlots)*64&&filtered.size()>=rayCount*4&&
            rays.hitWorldDistance.size()==rayCount&&rays.hitPixelStepsMip.size()==rayCount&&rays.rayDirectionDepthFetches.size()==rayCount,
            "Dynamic GI lost its 64-ray probe-major atlas contract");
        for(std::size_t i=0;i<rayCount;++i){const std::array<glm::vec4,3> metadata{{rays.hitWorldDistance[i],rays.hitPixelStepsMip[i],rays.rayDirectionDepthFetches[i]}};
            Check(filtered[i*4].w==rays.radianceStatus[i].w,"Dynamic spatial filtering changed a raw ray status");
            for(unsigned part=0;part<3;++part)Check(filtered[i*4+part+1]==metadata[part],"Dynamic spatial filtering changed exact raw ray metadata");
            const int status=int(std::lround(rays.radianceStatus[i].w));const bool readable=status==1||status==5||status==9||status==10||status==13;
            if(!readable)Check(filtered[i*4]==rays.radianceStatus[i],"Dynamic spatial filtering filled an unavailable source ray");++value.metadata;}
        value.upstream={hash(value.image[0]),hash(rays.radianceStatus),hash(rays.hitWorldDistance),hash(rays.hitPixelStepsMip),hash(rays.rayDirectionDepthFetches),
            hash(coverage),hash(risk),hash(value.mask),hash(probes.uniform),hash(probes.uniformNormals),hash(probes.spawned),hash(probes.spawnedNormals)};
        for(std::size_t i=0;i<pixels;++i){const auto& unfiltered=value.image[2][i];const auto& current=value.image[3][i];const auto& accumulated=value.image[4][i];
            Check(unfiltered.w==current.w,"Dynamic contact filter changed center readability");
            Check(accumulated.w==std::clamp(current.w,0.f,1.f),"Dynamic temporal resolve changed center readability");
            const double count=value.history[i].w;Check(count>=0&&count<=64.001,"Dynamic history exceeded the stationary 64-frame cap");
            value.maxHistory=std::max(value.maxHistory,count);value.mature+=count>63.9;
            value.extended+=count>double(gi.temporal.maxFrames)+.01;
            if(!(current.w>0)){++value.unknown;Check(accumulated.w==0&&count==0&&glm::length(glm::vec3(accumulated))<1e-7f,
                    "Dynamic history filled an unavailable current receiver");}
            else{++value.readable;value.historySum+=count;}
            if(value.mask[i].x<=1e-6f){++value.maskZero;Check(unfiltered==current&&unfiltered==value.image[1][i],"Dynamic contact changed a mask-zero receiver");}
            value.selected+=value.mask[i].w>.5f&&value.mask[i].x>.001f;}
        value.historyUsed=lab.PipelineStatistics().surfaceCacheHistoryUsed;return value;};
    struct ExposureError {double mse=0,floor=0,bright=0,dark=0,bias=0;std::uint64_t count=0;};
    struct Metric {double mse=0,floor=0,gradient=0,gradientFloor=0,alignedDelta=0,alignedFloor=0,referenceDelta=0,bright=0,dark=0,newHistoryMse=0;
        glm::dvec3 bias{0};std::array<std::array<double,2>,2> phase{},phaseFloor{};
        std::array<ExposureError,3> exposure{};double establishedMse=0;std::uint64_t establishedCount=0;
        double core=0,shell=0,coreTruth=0,shellTruth=0,disocclusionMse=0,disocclusionBright=0,disocclusionBias=0;
        double worstFrameMse=0,worstFrameAbsBias=0,signalGradient=0,truthGradient=0;
        std::uint64_t count=0,pairs=0,alignedPairs=0,coreCount=0,shellCount=0,newHistoryCount=0,disocclusionCount=0,frames=0;
        unsigned worstFrame=0;};
    using Errors=std::array<std::array<Metric,regions>,stages>;
    struct Phase {std::array<glm::dvec3,32> sum,split;std::array<std::uint64_t,32> count{};
        Phase(){sum.fill(glm::dvec3(0));split.fill(glm::dvec3(0));}};
    struct FrameMetric {Metric error;std::array<std::array<Phase,2>,2> phase{};glm::dvec3 splitSum{0};};
    const auto measure=[&](Errors& result,Sample& current,const Sample* previous,unsigned frameIndex,Errors* additional=nullptr){const auto& pose=*current.pose;
        std::array<std::array<FrameMetric,regions>,stages> perFrame{};
        const auto previousVP=previous?previous->pose->camera.projection*previous->pose->camera.view:glm::mat4(1);
        for(unsigned y=0;y<frame.height;++y)for(unsigned x=0;x<frame.width;++x){const auto i=std::size_t(y)*frame.width+x;if(!pose.roi[i])continue;
            const auto truth=glm::dvec3(pose.reference[i]),split=glm::dvec3(pose.split[i]);
            std::size_t aligned=pixels;bool geometryDisoccluded=false;
            // The fixture mover is a rigid world-X translation. Follow the
            // same material point using the independently specified old pose,
            // then verify the prior primary-ray owner, normal and world point.
            // This is an oracle metric, not production velocity or raw delta.
            if(previous&&(options.surfaceCacheDynamicRecoveryTest||pose.owner[i]!=int(cubeIndex))){auto previousWorld=pose.world[i];
                if(pose.owner[i]==int(cubeIndex))previousWorld.x+=previous->pose->objectOffset-pose.objectOffset;
                const auto clip=previousVP*glm::vec4(previousWorld,1);
                if(clip.w>0){const auto uv=glm::vec2(clip)/clip.w*.5f+.5f;
                    if(uv.x>=0&&uv.y>=0&&uv.x<1&&uv.y<1){const auto j=std::size_t(unsigned(uv.y*frame.height))*frame.width+unsigned(uv.x*frame.width);
                        const auto& old=*previous->pose;const float tolerance=std::max(.012f,.006f*glm::length(pose.world[i]-pose.camera.position));
                        // A conservative, geometry-only disocclusion subset:
                        // the previous visible primary-ray owner at this
                        // projection was a different object. It does not use
                        // temporal count or a production recovery mask, and
                        // makes no claim about self-disocclusion on one mesh.
                        geometryDisoccluded=pose.owner[i]!=int(cubeIndex)&&old.owner[j]>=0&&old.owner[j]!=pose.owner[i];
                        if(old.roi[j]&&old.owner[j]==pose.owner[i]&&glm::dot(old.normal[j],pose.normal[i])>.99f&&glm::length(old.world[j]-previousWorld)<tolerance)aligned=j;}}}
            unsigned exposureAge=geometryDisoccluded?1:aligned<pixels&&previous->exposureAge[aligned]?
                std::min(255u,unsigned(previous->exposureAge[aligned])+1):0;
            current.exposureAge[i]=std::uint8_t(exposureAge);
            for(unsigned s=0;s<stages;++s){const auto value=glm::dvec3(current.image[s][i]),residual=value-truth;
                for(unsigned r=0;r<regions;++r)if(pose.roi[i]&(1u<<r)){auto& a=perFrame[s][r];auto& m=a.error;
                    m.mse+=square(residual);m.floor+=.25*square(split);m.bias+=residual;++m.count;a.splitSum+=split;
                    m.bright+=square(glm::max(residual,glm::dvec3(0)));m.dark+=square(glm::min(residual,glm::dvec3(0)));
                    if(current.history[i].w<=1.01f){m.newHistoryMse+=square(residual);++m.newHistoryCount;}
                    if(geometryDisoccluded){m.disocclusionMse+=square(residual);m.disocclusionBright+=square(glm::max(residual,glm::dvec3(0)));
                        m.disocclusionBias+=glm::dot(residual,luma);++m.disocclusionCount;}
                    if(exposureAge&&exposureAge<=8){auto& e=m.exposure[exposureAge==1?0:exposureAge<=4?1:2];
                        e.mse+=square(residual);e.floor+=.25*square(split);e.bright+=square(glm::max(residual,glm::dvec3(0)));
                        e.dark+=square(glm::min(residual,glm::dvec3(0)));e.bias+=glm::dot(residual,luma);++e.count;}
                    else if(aligned<pixels){m.establishedMse+=square(residual);++m.establishedCount;}
                    for(unsigned p=0;p<periods.size();++p)for(unsigned axis=0;axis<2;++axis){auto& phase=a.phase[p][axis];const unsigned bin=(axis?y:x)%periods[p];
                        phase.sum[bin]+=residual;phase.split[bin]+=split;++phase.count[bin];}
                    if(pose.band[i]==1){m.core+=glm::dot(value,luma);m.coreTruth+=glm::dot(truth,luma);++m.coreCount;}
                    if(pose.band[i]==3){m.shell+=glm::dot(value,luma);m.shellTruth+=glm::dot(truth,luma);++m.shellCount;}
                    for(unsigned axis=0;axis<2;++axis){if((!axis&&x+1>=frame.width)||(axis&&y+1>=frame.height))continue;const auto j=i+(axis?frame.width:1);
                        if(!(pose.roi[j]&(1u<<r))||pose.owner[j]!=pose.owner[i]||glm::dot(pose.normal[i],pose.normal[j])<.985f||glm::length(pose.world[j]-pose.world[i])>.06f)continue;
                        const auto neighborResidual=glm::dvec3(current.image[s][j])-glm::dvec3(pose.reference[j]);
                        m.gradient+=square(neighborResidual-residual);m.gradientFloor+=.25*square(glm::dvec3(pose.split[j])-split);
                        m.signalGradient+=square(glm::dvec3(current.image[s][j])-value);m.truthGradient+=square(glm::dvec3(pose.reference[j])-truth);++m.pairs;}
                    if(aligned<pixels){const auto& old=*previous->pose;const auto referenceChange=truth-glm::dvec3(old.reference[aligned]);
                        const auto oldResidual=glm::dvec3(previous->image[s][aligned])-glm::dvec3(old.reference[aligned]);
                        m.alignedDelta+=square(residual-oldResidual);m.alignedFloor+=.25*square(split-glm::dvec3(old.split[aligned]));
                        m.referenceDelta+=square(referenceChange);++m.alignedPairs;}}}
        }
        for(unsigned s=0;s<stages;++s)for(unsigned r=0;r<regions;++r){auto& a=perFrame[s][r];auto& m=a.error;
            const auto bias=m.bias/double(std::max<std::uint64_t>(1,m.count)),splitBias=a.splitSum/double(std::max<std::uint64_t>(1,m.count));
            for(unsigned p=0;p<periods.size();++p)for(unsigned axis=0;axis<2;++axis){const auto& phase=a.phase[p][axis];
                for(unsigned bin=0;bin<periods[p];++bin)if(phase.count[bin]){
                    m.phase[p][axis]+=square(phase.sum[bin]/double(phase.count[bin])-bias)*double(phase.count[bin]);
                    m.phaseFloor[p][axis]+=.25*square(phase.split[bin]/double(phase.count[bin])-splitBias)*double(phase.count[bin]);}
            }
            const auto accumulate=[&](Metric& total){for(unsigned p=0;p<periods.size();++p)for(unsigned axis=0;axis<2;++axis){
                    total.phase[p][axis]+=m.phase[p][axis];total.phaseFloor[p][axis]+=m.phaseFloor[p][axis];}
                total.mse+=m.mse;total.floor+=m.floor;total.bias+=m.bias;total.count+=m.count;total.gradient+=m.gradient;total.gradientFloor+=m.gradientFloor;total.pairs+=m.pairs;
                total.bright+=m.bright;total.dark+=m.dark;total.newHistoryMse+=m.newHistoryMse;total.newHistoryCount+=m.newHistoryCount;
                total.alignedDelta+=m.alignedDelta;total.alignedFloor+=m.alignedFloor;total.referenceDelta+=m.referenceDelta;total.alignedPairs+=m.alignedPairs;
                total.core+=m.core;total.shell+=m.shell;total.coreTruth+=m.coreTruth;total.shellTruth+=m.shellTruth;total.coreCount+=m.coreCount;total.shellCount+=m.shellCount;
                total.disocclusionMse+=m.disocclusionMse;total.disocclusionBright+=m.disocclusionBright;total.disocclusionBias+=m.disocclusionBias;total.disocclusionCount+=m.disocclusionCount;
                for(unsigned bin=0;bin<m.exposure.size();++bin){const auto& e=m.exposure[bin];auto& t=total.exposure[bin];
                    t.mse+=e.mse;t.floor+=e.floor;t.bright+=e.bright;t.dark+=e.dark;t.bias+=e.bias;t.count+=e.count;}
                total.establishedMse+=m.establishedMse;total.establishedCount+=m.establishedCount;
                total.signalGradient+=m.signalGradient;total.truthGradient+=m.truthGradient;
                if(m.count){++total.frames;const double frameMse=mean(m.mse,m.count);
                    if(frameMse>total.worstFrameMse){total.worstFrameMse=frameMse;total.worstFrame=frameIndex;}
                    total.worstFrameAbsBias=std::max(total.worstFrameAbsBias,std::abs(glm::dot(bias,luma)));}};
            accumulate(result[s][r]);if(additional)accumulate((*additional)[s][r]);}
    };
    const auto report=[&](const Errors& errors,unsigned view,unsigned variant,unsigned segment){
        for(unsigned s=0;s<stages;++s)for(unsigned r=0;r<regions;++r){const auto& m=errors[s][r];if(!m.count)continue;const auto bias=m.bias/double(m.count);
            std::cout<<"DYNAMIC GI quality view="<<viewNames[view]<<" variant="<<variantNames[variant]<<" segment="<<segmentNames[segment]
                <<" stage="<<stageNames[s]<<" region="<<regionNames[r]<<" pixel_frames="<<m.count<<" rgb_mse="<<mean(m.mse,m.count)
                <<" reference_rgb_floor="<<mean(m.floor,m.count)<<" corrected_rgb_mse="<<std::max(0.,mean(m.mse-m.floor,m.count))
                <<" bright_rgb_mse="<<mean(m.bright,m.count)<<" dark_rgb_mse="<<mean(m.dark,m.count)
                <<" new_history_rgb_mse="<<mean(m.newHistoryMse,m.newHistoryCount)<<" new_history_pixel_frames="<<m.newHistoryCount
                <<" geometry_disocclusion_mse="<<mean(m.disocclusionMse,m.disocclusionCount)<<" geometry_disocclusion_bright_mse="<<mean(m.disocclusionBright,m.disocclusionCount)
                <<" geometry_disocclusion_luma_bias="<<mean(m.disocclusionBias,m.disocclusionCount)<<" geometry_disocclusion_pixel_frames="<<m.disocclusionCount
                <<" worst_frame_mse="<<m.worstFrameMse<<" worst_frame="<<m.worstFrame<<" worst_frame_abs_luma_bias="<<m.worstFrameAbsBias
                <<" bias_rgb="<<bias.x<<','<<bias.y<<','<<bias.z<<" residual_gradient_mse="<<mean(m.gradient,m.pairs)<<" gradient_pairs="<<m.pairs
                <<" reference_gradient_floor="<<mean(m.gradientFloor,m.pairs)<<" aligned_residual_delta_mse="<<mean(m.alignedDelta,m.alignedPairs)
                <<" aligned_reference_delta_mse="<<mean(m.referenceDelta,m.alignedPairs)<<" aligned_reference_floor="<<mean(m.alignedFloor,m.alignedPairs)
                <<" aligned_pairs="<<m.alignedPairs<<" static_receiver_alignment_only="<<!options.surfaceCacheDynamicRecoveryTest
                <<" rigid_translation_alignment="<<options.surfaceCacheDynamicRecoveryTest;
            if(options.surfaceCacheDynamicRecoveryTest){constexpr std::array<const char*,3> labels{{"exposure1","exposure2to4","exposure5to8"}};
                for(unsigned bin=0;bin<m.exposure.size();++bin){const auto& e=m.exposure[bin];std::cout<<' '<<labels[bin]<<"_mse="<<mean(e.mse,e.count)
                    <<' '<<labels[bin]<<"_floor="<<mean(e.floor,e.count)<<' '<<labels[bin]<<"_bright="<<mean(e.bright,e.count)
                    <<' '<<labels[bin]<<"_dark="<<mean(e.dark,e.count)<<' '<<labels[bin]<<"_luma_bias="<<mean(e.bias,e.count)
                    <<' '<<labels[bin]<<"_pixels="<<e.count;}
                std::cout<<" established_receiver_mse="<<mean(m.establishedMse,m.establishedCount)<<" established_receiver_pixels="<<m.establishedCount;}
            std::cout<<" signal_gradient_mse="<<mean(m.signalGradient,m.pairs)<<" reference_signal_gradient_mse="<<mean(m.truthGradient,m.pairs);
            for(unsigned p=0;p<periods.size();++p)for(unsigned axis=0;axis<2;++axis)std::cout<<" phase"<<periods[p]<<(axis?'y':'x')<<'='<<mean(m.phase[p][axis],m.count)
                <<" phase"<<periods[p]<<(axis?'y':'x')<<"_floor="<<mean(m.phaseFloor[p][axis],m.count);
            std::cout<<" core_pixels="<<m.coreCount<<" shell_pixels="<<m.shellCount;
            if(m.coreCount&&m.shellCount)std::cout<<" shell_minus_core_luma="<<mean(m.shell,m.shellCount)-mean(m.core,m.coreCount)
                <<" reference_shell_minus_core_luma="<<mean(m.shellTruth,m.shellCount)-mean(m.coreTruth,m.coreCount)
                <<" core_luma_bias="<<mean(m.core-m.coreTruth,m.coreCount)<<" shell_luma_bias="<<mean(m.shell-m.shellTruth,m.shellCount)
                <<" contact_contrast_error="<<(mean(m.shell,m.shellCount)-mean(m.core,m.coreCount))-(mean(m.shellTruth,m.shellCount)-mean(m.coreTruth,m.coreCount));
            std::cout<<" current_gpu_temporal=1 cpu_temporal_average=0 phase_definition=per_frame_regional_bias_removed_conditional_phase_energy spectral_proof=0 confidence_interval=0\n";}};

    const unsigned beginView=options.surfaceCacheDynamicView<3?options.surfaceCacheDynamicView:0;
    const unsigned endView=options.surfaceCacheDynamicView<3?beginView+1:3;
    for(unsigned view=beginView;view<endView;++view){std::array<Pose,poseCount> poses;
        const std::array<glm::vec3,3> eyes{{{1.28f,1.30f,-2.20f},{2.55f,1.42f,-1.90f},{0,3.4f,3.05f}}};
        const std::array<glm::vec3,3> targets{{{1.28f,1.10f,-1.15f},{1.48f,1.05f,-.72f},{0,.8f,.25f}}};
        auto base=SealedGiRoomCameraPreset(0,frame.OutputAspect());base.position=eyes[view];base.view=glm::lookAt(base.position,targets[view],glm::vec3(0,1,0));
        const auto inverse=glm::inverse(base.view);const auto right=glm::normalize(glm::vec3(inverse[0])),forward=glm::normalize(-glm::vec3(inverse[2]));
        const float motionScale=options.surfaceCacheDynamicFast?3.f:1.f;
        for(unsigned step=0;step<poseCount;++step){auto& pose=poses[step];pose.camera=base;
            if(view<2){pose.camera.position+=right*((view?.015f:.0125f)*float(step)*motionScale);
                const auto direction=glm::vec3(glm::rotate(glm::mat4(1),(view?.003f:.0025f)*float(step)*motionScale,glm::vec3(0,1,0))*glm::vec4(forward,0));
                pose.camera.view=glm::lookAt(pose.camera.position,pose.camera.position+direction,glm::vec3(0,1,0));
            }else pose.objectOffset=.15f*float(step);reference(pose,view,step);}
        // A mismatched, remote cache forces unavailable transport independently
        // of the legal known-black lamp-off measurement below.
        auto unrelatedInstances=lab.PreparedProbeSdfScene()->Instances();for(auto& instance:unrelatedInstances)instance.localToWorld[3].x+=50;
        Render::ProbeGlobalSdfSettings tinyCache;tinyCache.counts=glm::uvec3(2);
        const auto unrelatedScene=Render::BuildProbeSdfScene(unrelatedInstances,tinyCache);const auto unrelatedCache=Render::BuildSurfaceCache(*unrelatedScene);
        std::vector<std::array<std::uint64_t,12>> upstream;std::vector<std::array<std::uint64_t,stages+1>> staticHashes,outputHashes;
        std::vector<std::uint64_t> unfilteredHistoryHashes;std::array<Errors,segments> shipped{},historyOnly{};
        std::cout<<"DYNAMIC GI setup view="<<viewNames[view]<<" size="<<frame.width<<'x'<<frame.height<<" unique_reference_poses="<<poseCount
            <<" warm="<<warmFrames<<" moving="<<movingFrames<<" settle="<<settleFrames<<" motion_laps=2 sustained_lap_has_no_stationary_frame=1"
            <<" camera_motion_scale="<<motionScale
            <<" probe_rays=64 receiver_rays=8 placement_spacing=32 placement_phase=0 base_seed=0 static_convergence=1 contact=1 retain_object_motion=1"
            <<" independent_candidates=motionConsistentSpatialReuse,localCameraHistory,motionStability,motionSpatialFilter,motionDisocclusionRecovery"
            <<" variants_available="<<variantNames.size()<<" older_candidates_skipped="<<(options.surfaceCacheMotionStabilityTest||options.surfaceCacheMotionRecoveryTest)
            <<" recovery_test="<<options.surfaceCacheMotionRecoveryTest
            <<" dynamic_recovery_test="<<options.surfaceCacheDynamicRecoveryTest<<" comparison_baseline="<<variantNames[baselineVariant]
            <<" object_transform_oracle="<<options.surfaceCacheDynamicRecoveryTest<<" swept_floor_union_poses="<<(options.surfaceCacheDynamicRecoveryTest?poseCount:1)
            <<" frames_cli_applies_to_performance_only=1"<<std::endl;
        for(unsigned variant=0;variant<variantNames.size();++variant){
            if(options.surfaceCacheDynamicRecoveryTest){if(variant<6||(options.surfaceCacheDynamicRecoveryObjectOnlyTest&&variant>7))continue;}
            else {if(variant>=6)continue;
                if((options.surfaceCacheMotionStabilityTest||options.surfaceCacheMotionRecoveryTest)&&(variant==1||variant==2))continue;
                if(options.surfaceCacheMotionRecoveryTest&&variant==4)continue;
                if(options.surfaceCacheMotionStabilityTest&&!options.surfaceCacheMotionRecoveryTest&&(variant==4||variant==5))continue;}
            configure(variant);setPose(poses[0]);reset();std::array<Errors,segments> errors{};
            Sample previous;std::uint64_t unknown=0,metadata=0,maskZero=0,selected=0,historyUsed=0,readable=0,mature=0,movingExtended=0,movingReadable=0;
            double historySum=0,movingHistorySum=0,movingMaxHistory=0;
            for(unsigned f=0;f<totalFrames;++f){unsigned poseIndex=0,segment=0;
                if(f>=warmFrames&&f<warmFrames+movingFrames){const unsigned move=(f-warmFrames)%16;poseIndex=move<8?move+1:15-move;segment=1;}
                else if(f>=warmFrames+movingFrames)segment=2;
                setPose(poses[poseIndex]);render();auto current=sample(poses[poseIndex]);
                if(variant==3)unfilteredHistoryHashes.push_back(current.staticHash[stages]);
                if(variant==4)Check(unfilteredHistoryHashes[f]==current.staticHash[stages],"Motion output filter fed back into temporal history or its sample count");
                if(variant==baselineVariant){upstream.push_back(current.upstream);outputHashes.push_back(current.staticHash);if(f<warmFrames)staticHashes.push_back(current.staticHash);}
                else{Check(upstream[f]==current.upstream,"Dynamic A/B changed raw rays, placement, coverage, geometry risk or contact masks");
                    if(f<warmFrames)Check(staticHashes[f]==current.staticHash,"Dynamic A/B changed the reset/static 64-frame sequence");
                    if(variant==2&&f==warmFrames)Check(outputHashes[f]==current.staticHash,"Local camera history changed the first-motion estimator transition frame");
                    if(variant==2&&view==2)Check(outputHashes[f]==current.staticHash,"Local camera history changed a fixed-camera object-motion sequence");
                    if((variant==3||variant==5)&&view==2)Check(outputHashes[f]==current.staticHash,"Camera-only stability or recovery changed a fixed-camera object-motion sequence");}
                if(f==0)Check(!current.historyUsed,"Dynamic reset retained previous variant history");
                else Check(current.historyUsed,"Complete shipped dynamic sequence unexpectedly lost certified compatible history");
                if(segment==1){double cap=view==2?double(gi.temporal.dynamicMaxFrames):double(gi.temporal.maxFrames);
                    if(variant==2&&view<2&&f>warmFrames)cap=std::min(64.,2.*double(gi.temporal.maxFrames));
                    if(variant>=3&&view<2)cap=std::min(64.,std::max(32.,double(gi.temporal.maxFrames)));
                    Check(current.maxHistory<=cap+.001,"Moving GI exceeded its responsive/local bounded history cap");movingExtended+=current.extended;
                    movingReadable+=current.readable;movingHistorySum+=current.historySum;movingMaxHistory=std::max(movingMaxHistory,current.maxHistory);}
                if(view==2&&segment==2&&f-warmFrames-movingFrames<gi.temporal.dynamicSettleFrames)
                    Check(current.maxHistory<=double(gi.temporal.dynamicMaxFrames)+.001,"Moving cube settling lost its dynamic history cap");
                // The static diagnostic is the actual frame-64 texture. All
                // moving/settling frames are measured without CPU accumulation.
                if(f+1>=warmFrames){Errors* additional=segment==1&&f>=warmFrames+16?&errors[3]:
                        segment==2?(f-warmFrames-movingFrames<8?&errors[4]:&errors[5]):nullptr;
                    measure(errors[segment],current,f?&previous:nullptr,f+1,additional);}
                unknown+=current.unknown;metadata+=current.metadata;maskZero+=current.maskZero;selected+=current.selected;
                historyUsed+=current.historyUsed;readable+=current.readable;historySum+=current.historySum;if(f+1==warmFrames)mature=current.mature;
                if(!options.screenshot.empty()&&(f+1==warmFrames||f+1==warmFrames+24||f+1==warmFrames+movingFrames+1||f+1==totalFrames)){
                    const std::string label=std::string(viewNames[view])+"_"+variantNames[variant]+"_frame"+std::to_string(f+1);
                    Save(lab.Readback(),outputPath(label+"_total_renderer_ldr"));saveTone(current.image[1],label+"_probe_spatial_tone4");
                    saveTone(current.image[3],label+"_contact_filtered_tone4");saveTone(current.image[4],label+"_temporal_tone4");}
                previous=std::move(current);}
            for(unsigned segment=0;segment<segments;++segment)report(errors[segment],view,variant,segment);
            std::cout<<"DYNAMIC GI invariants view="<<viewNames[view]<<" variant="<<variantNames[variant]<<" frames="<<totalFrames<<" static_sequence_frames="<<warmFrames
                <<" upstream_exact_frames="<<(variant!=baselineVariant?upstream.size():0)<<" static_exact_frames="<<(variant!=baselineVariant?staticHashes.size():0)
                <<" raw_ray_metadata_preserved="<<metadata<<" unknown_current_preserved="<<unknown<<" mask_zero_preserved="<<maskZero
                <<" selected_receiver_pixel_frames="<<selected<<" history_used_frames="<<historyUsed<<" history_mean="<<mean(historySum,readable)
                <<" frame64_mature_pixels="<<mature<<" source_sequence_aligned=1 geometry_roi_independent=1"<<std::endl;
            if(variant==4)std::cout<<"DYNAMIC GI output_filter_history view="<<viewNames[view]<<" compared_frames="<<unfilteredHistoryHashes.size()
                <<" exact_history_rgba=1 final_output_allowed_to_change=1"<<std::endl;
            std::cout<<"DYNAMIC GI local_history view="<<viewNames[view]<<" variant="<<variantNames[variant]
                <<" enabled="<<gi.temporal.localCameraHistory<<" motion_consistent_spatial="<<gi.temporal.motionConsistentSpatialReuse
                <<" motion_stability="<<gi.temporal.motionStability<<" motion_spatial_filter="<<gi.temporal.motionSpatialFilter
                <<" motion_disocclusion_recovery="<<gi.temporal.motionDisocclusionRecovery
                <<" object_motion_reprojection="<<gi.temporal.objectMotionReprojection<<" disocclusion_filter="<<gi.temporal.disocclusionFilter
                <<" moving_extended_pixel_frames="<<movingExtended<<" local_history_limit="<<std::min(64u,2u*gi.temporal.maxFrames)
                <<" camera_history_limit="<<(variant>=3?std::min(64u,std::max(32u,gi.temporal.maxFrames)):variant==2?std::min(64u,2u*gi.temporal.maxFrames):gi.temporal.maxFrames)
                <<" moving_history_mean="<<mean(movingHistorySum,movingReadable)<<" moving_history_max="<<movingMaxHistory
                <<" first_motion_old_cap="<<(variant==2)<<" object_path_unchanged="<<(variant==2&&view==2)
                <<" legacy_local_photometric_heuristic="<<(variant==2)<<std::endl;
            if(variant==baselineVariant)shipped=errors;
            if(variant==3)historyOnly=errors;
            // One compact row per independent geometry ROI makes the tradeoff
            // visible without selecting a favorable stage or a smooth frame.
            // MSE/bright bias on geometry-confirmed newly visible receivers
            // specifically guard against accepting a stale but smooth history.
            const auto percent=[](double prior,double value){return 100.*(value-prior)/std::max(std::abs(prior),1e-20);};
            for(unsigned r=0;r<regions;++r){const auto& m=errors[1][4][r];const auto& old=shipped[1][4][r];if(!m.count)continue;
                const auto& sustained=errors[3][4][r];const auto& oldSustained=shipped[3][4][r];
                const auto& settling=errors[2][4][r];const auto& oldSettling=shipped[2][4][r];
                const auto& fade=errors[4][4][r];const auto& afterFade=errors[5][4][r];
                const double mse=mean(m.mse,m.count),priorMse=mean(old.mse,old.count);
                std::cout<<"DYNAMIC GI SUMMARY view="<<viewNames[view]<<" variant="<<variantNames[variant]<<" region="<<regionNames[r]
                    <<" baseline="<<variantNames[baselineVariant]
                    <<" moving_frames="<<m.frames<<" moving_rgb_mse="<<mse<<" moving_mse_change_percent="<<percent(priorMse,mse)
                    <<" sustained_rgb_mse="<<mean(sustained.mse,sustained.count)
                    <<" sustained_mse_change_percent="<<percent(mean(oldSustained.mse,oldSustained.count),mean(sustained.mse,sustained.count))
                    <<" settling_rgb_mse="<<mean(settling.mse,settling.count)
                    <<" settling_mse_change_percent="<<percent(mean(oldSettling.mse,oldSettling.count),mean(settling.mse,settling.count))
                    <<" settling_first8_mse="<<mean(fade.mse,fade.count)<<" settling_after8_mse="<<mean(afterFade.mse,afterFade.count)
                    <<" moving_aligned_delta="<<mean(m.alignedDelta,m.alignedPairs)<<" aligned_pairs="<<m.alignedPairs
                    <<" aligned_delta_change_percent="<<percent(mean(old.alignedDelta,old.alignedPairs),mean(m.alignedDelta,m.alignedPairs))
                    <<" moving_worst_frame_mse="<<m.worstFrameMse<<" moving_worst_frame="<<m.worstFrame
                    <<" worst_frame_mse_change_percent="<<percent(old.worstFrameMse,m.worstFrameMse)
                    <<" moving_bright_mse="<<mean(m.bright,m.count)<<" moving_dark_mse="<<mean(m.dark,m.count)
                    <<" moving_luma_bias="<<glm::dot(m.bias,luma)/double(m.count)<<" moving_max_abs_frame_luma_bias="<<m.worstFrameAbsBias
                    <<" geometry_disocclusion_mse="<<mean(m.disocclusionMse,m.disocclusionCount)
                    <<" geometry_disocclusion_bright_mse="<<mean(m.disocclusionBright,m.disocclusionCount)
                    <<" geometry_disocclusion_luma_bias="<<mean(m.disocclusionBias,m.disocclusionCount)<<" geometry_disocclusion_pixels="<<m.disocclusionCount
                    <<" new_history_mse="<<mean(m.newHistoryMse,m.newHistoryCount)<<" new_history_pixels="<<m.newHistoryCount
                    <<" moving_phase32xy="<<mean(m.phase[1][0]+m.phase[1][1],m.count)
                    <<" moving_phase32xy_floor="<<mean(m.phaseFloor[1][0]+m.phaseFloor[1][1],m.count)
                    <<" history_accumulation_mse="<<mean(errors[1][5][r].mse,errors[1][5][r].count)
                    <<" contact_contrast_valid="<<(m.coreCount>0&&m.shellCount>0);
                if(m.coreCount&&m.shellCount)std::cout<<" contact_contrast_error="<<mean(m.shell-m.shellTruth,m.shellCount)-mean(m.core-m.coreTruth,m.coreCount)
                    <<" core_luma_bias="<<mean(m.core-m.coreTruth,m.coreCount)<<" shell_luma_bias="<<mean(m.shell-m.shellTruth,m.shellCount);
                if(variant==4)std::cout<<" output_filter_incremental_mse_percent="<<percent(mean(historyOnly[1][4][r].mse,historyOnly[1][4][r].count),mse);
                if(variant==5)std::cout<<" recovery_incremental_mse_percent="<<percent(mean(historyOnly[1][4][r].mse,historyOnly[1][4][r].count),mse)
                    <<" recovery_incremental_disocclusion_mse_percent="<<percent(mean(historyOnly[1][4][r].disocclusionMse,historyOnly[1][4][r].disocclusionCount),mean(m.disocclusionMse,m.disocclusionCount));
                if(options.surfaceCacheDynamicRecoveryTest){constexpr std::array<const char*,3> labels{{"exposure1","exposure2to4","exposure5to8"}};
                    for(unsigned bin=0;bin<m.exposure.size();++bin){const auto& e=m.exposure[bin];const auto& b=old.exposure[bin];
                        std::cout<<' '<<labels[bin]<<"_mse="<<mean(e.mse,e.count)<<' '<<labels[bin]<<"_mse_change_percent="<<percent(mean(b.mse,b.count),mean(e.mse,e.count))
                            <<' '<<labels[bin]<<"_floor="<<mean(e.floor,e.count)<<' '<<labels[bin]<<"_bright="<<mean(e.bright,e.count)
                            <<' '<<labels[bin]<<"_dark="<<mean(e.dark,e.count)<<' '<<labels[bin]<<"_luma_bias="<<mean(e.bias,e.count)
                            <<' '<<labels[bin]<<"_pixels="<<e.count;}
                    std::cout<<" established_receiver_mse="<<mean(m.establishedMse,m.establishedCount)<<" established_receiver_pixels="<<m.establishedCount
                        <<" exposure_followup=geometry_world_aligned_no_production_count";}
                std::cout<<" disocclusion_definition=previous_different_oracle_owner_static_receiver current_gpu_output=1 improvement_assertion=0"<<std::endl;}
            if(variant!=baselineVariant)for(unsigned segment=0;segment<segments;++segment)for(unsigned r=0;r<regions;++r){const auto& before=shipped[segment][4][r];const auto& after=errors[segment][4][r];if(!before.count||!after.count)continue;
                const double oldMse=mean(before.mse,before.count),newMse=mean(after.mse,after.count);
                std::cout<<"DYNAMIC GI comparison view="<<viewNames[view]<<" variant="<<variantNames[variant]<<" segment="<<segmentNames[segment]<<" region="<<regionNames[r]
                    <<" baseline="<<variantNames[baselineVariant]
                    <<" shipped_temporal_mse="<<oldMse<<" candidate_temporal_mse="<<newMse<<" mse_change_percent="<<100*(newMse-oldMse)/std::max(oldMse,1e-20)
                    <<" shipped_aligned_delta="<<mean(before.alignedDelta,before.alignedPairs)<<" candidate_aligned_delta="<<mean(after.alignedDelta,after.alignedPairs)
                    <<" diagnostic_only=1 candidate_improvement_assertion=0"<<std::endl;}

            frame.effects.areaLights.lights[0].enabled=false;render();const auto black=sample(poses[0]);
            Check(!black.historyUsed&&black.readable>100&&energy(black.image[0])<1e-5&&energy(black.image[3])<1e-5&&energy(black.image[4])<1e-5,
                "Dynamic GI lamp-off edit lost readable black or retained old lighting");
            frame.effects.areaLights.lights[0].enabled=true;render();gi.cache=unrelatedCache;render();
            Check(!lab.PipelineStatistics().surfaceCacheHistoryUsed,"Dynamic GI unavailable cache reused valid-cache history");render();const auto unavailable=sample(poses[0]);
            Check(unavailable.unknown>0&&energy(unavailable.image[0])<1e-5&&energy(unavailable.image[3])<1e-5&&energy(unavailable.image[4])<1e-5,
                "Dynamic GI unavailable cache acquired fabricated transport");
            std::cout<<"DYNAMIC GI unavailable_black view="<<viewNames[view]<<" variant="<<variantNames[variant]<<" readable_black="<<black.readable
                <<" unavailable_current="<<unavailable.unknown<<" black_temporal_energy="<<energy(black.image[4])<<" unavailable_temporal_energy="<<energy(unavailable.image[4])
                <<" post_contact_unknown_contract=1"<<std::endl;gi.cache=baselineGi.cache;
        }
    }
    std::cout<<"Surface Cache full-path dynamic quality passed finite/unknown/black/metadata/static-equality contracts; quality changes are diagnostic, not an improvement assertion."<<std::endl;
}
