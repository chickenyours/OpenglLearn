// Include after surface_cache_geometry_quality_test.inl and the production
// dynamic fixture helpers. Independent full-path A/B for fresh current rays,
// shadow-history response, and rigid receiver lighting response. Reference/oracle
// work and GPU readbacks are never timed.
Render::PipelineCamera SurfaceCacheFreshSamplingCamera(unsigned view,float aspect,bool movingReceiver) {
    auto camera=SealedGiRoomCameraPreset(0,aspect);
    const std::array<glm::vec3,3> eyes{{{1.28f,1.30f,-2.20f},{2.55f,1.42f,-1.90f},{0,3.4f,3.05f}}};
    const std::array<glm::vec3,3> targets{{{1.28f,1.10f,-1.15f},{1.48f,1.05f,-.72f},{0,.8f,.25f}}};
    camera.position=movingReceiver&&view==2?glm::vec3(-3.35f,1.35f,2.65f):eyes[view];
    const auto target=movingReceiver&&view==2?glm::vec3(-.95f,.43f,1.15f):targets[view];
    camera.view=glm::lookAt(camera.position,target,glm::vec3(0,1,0));return camera;
}

struct SurfaceCacheFreshSamplingBudget {
    unsigned geometry=8,moving=8,total=8;bool movingTagged=false,movingEligible=false,fullCurrent=false,outsideRisk=false;
};
SurfaceCacheFreshSamplingBudget SurfaceCacheFreshSamplingPlan(glm::vec4 classification,float originalMask,unsigned base,unsigned movingMultiplier,bool movingFullCurrent) {
    for(unsigned c=0;c<4;++c)Check(std::isfinite(classification[c]),"Nonfinite independent receiver-budget classification");
    const int tag=int(std::lround(classification.z));
    Check(classification.z==float(tag)&&tag>=0&&(tag&~29)==0&&(tag&12)!=12,"Invalid independent moving receiver-budget tag");
    const unsigned taggedMultiplier=tag&8?4u:tag&4?2u:1u;
    Check(taggedMultiplier==1||taggedMultiplier==movingMultiplier,"Moving receiver tag exceeded its configured multiplier");
    const bool fullCurrent=(tag&16)!=0;
    Check(!fullCurrent||(movingFullCurrent&&taggedMultiplier>1),"Full-current moving tag had no matching enabled moving budget");
    SurfaceCacheFreshSamplingBudget plan;plan.geometry=base;
    if(classification.x>.000001f){const unsigned geometry=unsigned(std::lround(classification.y));
        Check(classification.y==float(geometry)&&geometry>=base&&geometry<=4*base&&geometry%base==0,"Invalid independent geometric exposure budget");
        plan.geometry=geometry;}
    plan.movingTagged=taggedMultiplier>1;plan.fullCurrent=fullCurrent;
    plan.movingEligible=plan.movingTagged&&(fullCurrent||originalMask>=.25f);plan.outsideRisk=plan.movingEligible&&originalMask<.25f;
    plan.moving=plan.movingEligible?base*taggedMultiplier:base;plan.total=std::max(plan.geometry,plan.moving);return plan;
}
std::uint64_t SurfaceCacheFreshSamplingGeometryHash(const std::vector<glm::vec4>& values) {
    std::uint64_t result=1469598103934665603ull;
    for(auto value:values){for(unsigned c=0;c<4;++c)Check(std::isfinite(value[c]),"Nonfinite geometric exposure hash input");value.z=float(int(std::lround(value.z))&1);const auto* bytes=reinterpret_cast<const unsigned char*>(&value);
        for(std::size_t i=0;i<sizeof(value);++i){result^=bytes[i];result*=1099511628211ull;}}
    return result;
}

void SurfaceCacheFreshSamplingQuality(MaterialLabModule& lab,const Options& options) {
    const bool shadowMode=options.surfaceCacheShadowHistoryTest;
    const bool movingReceiver=shadowMode&&options.surfaceCacheMovingReceiverTest;
    Check(options.surfaceCacheMovingReceiverRayMultiplier==1||options.surfaceCacheMovingReceiverRayMultiplier==2||options.surfaceCacheMovingReceiverRayMultiplier==4,"Moving receiver ray multiplier must be 1, 2 or 4");
    using SurfaceCacheGeometryFixture::Oracle;
    Check(options.vulkan,"Fresh sampling GI quality requires Vulkan");
    Check(options.surfaceCacheFreshSamplingMultiplier>=1&&options.surfaceCacheFreshSamplingMultiplier<=4,"Fresh sampling multiplier must be 1..4");
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
    constexpr unsigned stages=6,regions=8,segments=6,warmFrames=64,movingFrames=32,settleFrames=16,poseCount=9;
    constexpr unsigned totalFrames=warmFrames+movingFrames+settleFrames;
    constexpr std::array<const char*,stages> stageNames{{"probe_raw","probe_spatial","contact_unfiltered","contact_filtered","temporal","history_accumulation"}};
    constexpr std::array<const char*,regions> regionNames{{"all_geometry","upright_oval","contact_0to70cm","remote_planar","movable_cube","mover_swept_floor","cube_left_face","low_oval"}};
    constexpr std::array<const char*,segments> segmentNames{{"static64_current","moving","settling","moving_sustained_lap2","settling_first8","settling_after8"}};
    constexpr std::array<const char*,3> viewNames{{"oval_camera_stop_start","contact_camera_stop_start","cube_fixed_camera"}};
    const std::array<const char*,2> variantNames{{movingReceiver?"rigid_receiver_legacy":shadowMode?"fresh_current_rays":"current_object_stability",movingReceiver?"reactive_moving_receiver":shadowMode?"reactive_shadow_history":"fresh_current_rays"}};
    constexpr unsigned baselineVariant=0;
    constexpr std::array<unsigned,2> periods{{8,32}};
    const auto pixels=std::size_t(frame.width)*frame.height;const glm::dvec3 luma(.2126,.7152,.0722);
    const auto mean=[](double sum,std::uint64_t count){return sum/double(std::max<std::uint64_t>(1,count));};
    const auto square=[](glm::dvec3 value){return glm::dot(value,value)/3.;};
    const auto finiteAny=[&](const auto& values,const char* label){Check(!values.empty(),std::string("Missing fresh sampling GI ")+label+": "+lab.LastError());
        for(const auto& p:values)for(unsigned c=0;c<4;++c)Check(std::isfinite(p[c]),"Nonfinite fresh sampling GI data");};
    const auto finite=[&](const auto& values,const char* label){finiteAny(values,label);Check(values.size()==pixels,std::string("Wrong fresh sampling GI extent: ")+label);};
    const auto hash=[](const auto& values){std::uint64_t result=1469598103934665603ull;
        const auto* bytes=reinterpret_cast<const unsigned char*>(values.data());
        for(std::size_t i=0;i<values.size()*sizeof(values[0]);++i){result^=bytes[i];result*=1099511628211ull;}return result;};
    const auto energy=[](const auto& values){double sum=0;for(const auto& p:values)sum+=glm::length(glm::dvec3(p));return sum;};
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;placement.reset=false;gi.temporal.reset=false;};
    const auto reset=[&]{frame.effects.cameraCut=true;placement.reset=true;gi.temporal.reset=true;};
    const auto ready=[&]{const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
        while(lab.ProbeSdfQualityStatistics().pending){render();Check(std::chrono::steady_clock::now()<deadline,"Fresh sampling GI SDF preparation timed out");}};
    const auto outputPath=[&](const std::string& label){auto p=std::filesystem::path(options.screenshot);
        p.replace_filename(p.stem().string()+"_"+label+p.extension().string());return p.string();};
    const auto saveTone=[&](const auto& values,const std::string& label){if(options.screenshot.empty())return;
        FramePixels image;image.width=frame.width;image.height=frame.height;image.rgba.resize(pixels*4);
        for(std::size_t i=0;i<pixels;++i){for(unsigned c=0;c<3;++c){double value=4*std::max(0.,double(values[i][c]));value=value/(1+value);
            value=value<=.0031308?12.92*value:1.055*std::pow(value,1/2.4)-.055;
            image.rgba[4*i+c]=std::uint8_t(std::lround(std::clamp(value,0.,1.)*255));}image.rgba[4*i+3]=255;}Save(image,outputPath(label));};
    const auto configure=[&](unsigned variant){gi=baselineGi;placement=baselinePlacement;
        gi.temporal.motionConsistentSpatialReuse=false;gi.temporal.localCameraHistory=false;
        gi.temporal.motionStability=true;gi.temporal.motionSpatialFilter=false;gi.temporal.motionDisocclusionRecovery=false;
        gi.temporal.objectMotionReprojection=true;gi.temporal.disocclusionFilter=false;
        gi.contact.disocclusionSampling=shadowMode||variant==1;gi.temporal.reactiveShadows=shadowMode&&(movingReceiver||variant==1);gi.temporal.reactiveMovingReceivers=!movingReceiver||variant==1;gi.contact.movingReceiverRayMultiplier=movingReceiver&&variant?options.surfaceCacheMovingReceiverRayMultiplier:1;gi.contact.movingReceiverFullCurrent=movingReceiver&&variant&&options.surfaceCacheMovingReceiverFullCurrent;gi.contact.disocclusionRayMultiplier=options.surfaceCacheFreshSamplingMultiplier;
        gi.contact.collectSamplingDiagnostics=true;
        gi.contact.raysPerPixel=8;trace.samplingSeed=0;placement.frameIndex=0;gi.contact.referenceReceiver=false;};

    const auto& roomObjects=SealedGiRoomObjects();unsigned ovalIndex=unsigned(roomObjects.size()),prismIndex=ovalIndex,cubeIndex=ovalIndex,lowOvalIndex=ovalIndex;
    std::vector<SealedGiRoomMaterial> materials;for(unsigned i=0;i<roomObjects.size();++i){materials.push_back(SealedGiRoomObjectMaterial(i));
        if(std::string(roomObjects[i].name)=="Porcelain upright oval")ovalIndex=i;
        if(std::string(roomObjects[i].name)=="Porcelain tall prism")prismIndex=i;
        if(std::string(roomObjects[i].name)=="Porcelain low oval")lowOvalIndex=i;
        if(roomObjects[i].movable)cubeIndex=i;}
    Check(ovalIndex<roomObjects.size()&&prismIndex<roomObjects.size()&&cubeIndex<roomObjects.size()&&lowOvalIndex<roomObjects.size(),"Fresh sampling GI fixture lacks its ellipsoid/prism/movable cube");
    const auto& oval=roomObjects[ovalIndex];const auto& prism=roomObjects[prismIndex];const auto& cube=roomObjects[cubeIndex];
    const auto ovalInverse=glm::inverse(ProbePlacementTransform(oval,{})),prismInverse=glm::inverse(ProbePlacementTransform(prism,{}));
    const auto originalCubeInverse=glm::inverse(ProbePlacementTransform(cube,{}));
    const auto lowOvalInverse=glm::inverse(ProbePlacementTransform(roomObjects[lowOvalIndex],{}));
    std::array<glm::mat4,poseCount> sweptCubeInverses;
    for(unsigned step=0;step<poseCount;++step){ProbePlacementSceneSettings moved;moved.occluderOffset=.15f*float(step);
        sweptCubeInverses[step]=glm::inverse(ProbePlacementTransform(cube,moved));}
    const auto boxDistance=[](glm::vec3 p,const glm::mat4& inverse,glm::vec3 scale){const auto q=glm::abs(glm::vec3(inverse*glm::vec4(p,1)))*scale-scale;
        return double(glm::length(glm::max(q,glm::vec3(0)))+std::min(std::max(q.x,std::max(q.y,q.z)),0.f));};
    const auto ellipsoidDistance=[](glm::vec3 world,const glm::mat4& inverse,glm::vec3 scale){const auto a=glm::dvec3(scale),a2=a*a;
        const auto p=glm::abs(glm::dvec3(inverse*glm::vec4(world,1))*a);
        if(glm::dot(p/a,p/a)<=1.)return (glm::length(p/a)-1.)*std::min({a.x,a.y,a.z});
        const auto equation=[&](double t){const auto q=a*p/(a2+t);return glm::dot(q,q)-1.;};
        double low=0,high=std::max(1.,glm::length(a*p));while(equation(high)>0)high*=2;
        for(unsigned step=0;step<40;++step){const double mid=.5*(low+high);if(equation(mid)>0)low=mid;else high=mid;}
        return glm::length(p-a2*p/(a2+.5*(low+high)));};
    const auto ovalDistance=[&](glm::vec3 world){return ellipsoidDistance(world,ovalInverse,oval.scale);};
    const auto lowOvalDistance=[&](glm::vec3 world){return ellipsoidDistance(world,lowOvalInverse,roomObjects[lowOvalIndex].scale);};
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
            if(movingReceiver&&owner==int(lowOvalIndex))gap=boxDistance(p,cubeInverse,cube.scale);
            if(movingReceiver&&owner==int(cubeIndex))gap=lowOvalDistance(p);
            if(gap>=-.015&&gap<.70){mask|=4;pose.band[i]=gap<.03?1:gap<.35?2:3;}
            const bool planar=owner>=0&&owner<=8&&std::max({std::abs(n.x),std::abs(n.y),std::abs(n.z)})>.999f;
            if(planar&&ovalDistance(p)>.8&&boxDistance(p,prismInverse,prism.scale)>.8&&boxDistance(p,cubeInverse,cube.scale)>.8)mask|=8;
            if(owner==int(cubeIndex)){mask|=16;
                const auto leftNormal=glm::normalize(glm::vec3(glm::transpose(originalCubeInverse)*glm::vec4(-1,0,0,0)));
                if(glm::dot(n,leftNormal)>.999f)mask|=64;}
            if(owner==int(lowOvalIndex))mask|=128;
            const auto local=glm::vec3(originalCubeInverse*glm::vec4(p,1));
            if(owner==0&&n.y>.99f){bool footprint=std::abs(local.x)<.96f&&std::abs(local.z)<.96f;
                for(const auto& inverse:sweptCubeInverses){const auto swept=glm::vec3(inverse*glm::vec4(p,1));
                    footprint=footprint||(std::abs(swept.x)<.96f&&std::abs(swept.z)<.96f);}
                if(footprint)mask|=32;}
            pose.roi[i]=mask;for(unsigned r=0;r<regions;++r)pose.counts[r]+=(mask&(1u<<r))!=0;}
        std::cout<<"FRESH SAMPLING GI geometry view="<<viewNames[view]<<" offset="<<pose.objectOffset<<" oracle_rejected="<<rejected
            <<" geometry_roi=1 production_mask_selects_roi=0";for(unsigned r=0;r<regions;++r)std::cout<<' '<<regionNames[r]<<'='<<pose.counts[r];std::cout<<std::endl;};
    const auto reference=[&](Pose& pose,unsigned view,unsigned step){configure(0);setPose(pose);
        gi.temporal.enabled=false;gi.spatial.enabled=false;gi.contact.referenceReceiver=true;gi.contact.raysPerPixel=32;gi.contact.filterStrength=0;
        gi.contact.collectSamplingDiagnostics=false;
        trace.samplingSeed=1009;reset();render();ready();captureGeometry(pose,view);
        // Exclude cache preparation and relighting from the independent seeds.
        for(unsigned f=0;f<4;++f)render();pose.reference.assign(pixels,glm::vec4(0));pose.split.assign(pixels,glm::vec4(0));
        for(unsigned seed=0;seed<8;++seed){trace.samplingSeed=1009+73*seed;render();const auto value=lab.ReadbackSurfaceCacheContactUnfilteredGi();finite(value,"safe-receiver reference");
            for(std::size_t i=0;i<pixels;++i){pose.reference[i]+=value[i]/8.f;pose.split[i]+=value[i]*(seed<4?.25f:-.25f);}}
        std::uint64_t unknown=0,retained=0;double floor=0;
        for(std::size_t i=0;i<pixels;++i)if(pose.roi[i]){if(!(pose.reference[i].w>0)){
                for(unsigned r=0;r<regions;++r)pose.counts[r]-=(pose.roi[i]&(1u<<r))!=0;pose.roi[i]=0;++unknown;
            }else{++retained;floor+=.25*square(glm::dvec3(pose.split[i]));}}
        Check(retained>100,"Fresh sampling GI geometry reference contains too few readable receivers");
        std::cout<<"FRESH SAMPLING GI reference view="<<viewNames[view]<<" step="<<step<<" spp=256 split_spp=128 reference_rgb_floor="<<mean(floor,retained)
            <<" retained="<<retained<<" excluded_unknown="<<unknown<<" warm_frames=4 warm_rays_excluded=1 safe_receiver_origin=1 shared_cache=1 path_truth=0"<<std::endl;
        if(step==0||step==8)saveTone(pose.reference,std::string(viewNames[view])+"_pose"+std::to_string(step)+"_reference_tone4");};

    struct Sample {std::array<std::vector<glm::vec4>,stages> image;std::vector<glm::vec4> history,mask,fresh,sampling;std::vector<std::uint8_t> exposureAge;
        std::array<std::uint64_t,13> upstream{};std::array<std::uint64_t,stages+1> staticHash{};const Pose* pose=nullptr;
        std::uint64_t unknown=0,maskZero=0,selected=0,readable=0,mature=0,metadata=0,extended=0;
        std::uint64_t freshSelected=0,tracedReceivers=0,receiverRays=0,receiverExtraRays=0,classifierFresh=0,geometryClassificationHash=0;
        std::uint64_t movingTagged=0,movingSelected=0,movingTraced=0,geometryExtraRays=0,movingExtraRays=0;
        std::uint64_t outsideMovingSelected=0,outsideMovingRays=0,outsideMovingExtraRays=0;
        double historySum=0,maxHistory=0;bool historyUsed=false;};
    const auto sample=[&](const Pose& pose){Sample value;value.pose=&pose;
        value.exposureAge.assign(pixels,0);
        value.image={lab.ReadbackSurfaceCacheRawGi(),lab.ReadbackSurfaceCacheSpatialGi(),lab.ReadbackSurfaceCacheContactUnfilteredGi(),
            lab.ReadbackSurfaceCacheContactGi(),lab.ReadbackSurfaceCacheTemporalGi()};
        value.history=lab.ReadbackSurfaceCacheTemporalHistory();value.mask=lab.ReadbackSurfaceCacheContactMask();
        value.fresh=lab.ReadbackSurfaceCacheFreshSampling();value.sampling=lab.ReadbackSurfaceCacheReceiverSampling();
        finite(value.sampling,"GPU receiver-loop replay");value.geometryClassificationHash=SurfaceCacheFreshSamplingGeometryHash(value.fresh);
        if(!value.fresh.empty()){Check(gi.contact.disocclusionSampling,"Disabled fresh sampling exposed a stale classifier");
            finite(value.fresh,"GPU fresh classifier");}
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
            "Fresh sampling GI lost its 64-ray probe-major atlas contract");
        for(std::size_t i=0;i<rayCount;++i){const std::array<glm::vec4,3> metadata{{rays.hitWorldDistance[i],rays.hitPixelStepsMip[i],rays.rayDirectionDepthFetches[i]}};
            Check(filtered[i*4].w==rays.radianceStatus[i].w,"Dynamic spatial filtering changed a raw ray status");
            for(unsigned part=0;part<3;++part)Check(filtered[i*4+part+1]==metadata[part],"Dynamic spatial filtering changed exact raw ray metadata");
            const int status=int(std::lround(rays.radianceStatus[i].w));const bool readable=status==1||status==5||status==9||status==10||status==13;
            if(!readable)Check(filtered[i*4]==rays.radianceStatus[i],"Dynamic spatial filtering filled an unavailable source ray");++value.metadata;}
        value.upstream={hash(value.image[0]),hash(rays.radianceStatus),hash(rays.hitWorldDistance),hash(rays.hitPixelStepsMip),hash(rays.rayDirectionDepthFetches),
            hash(coverage),hash(risk),hash(value.mask),hash(probes.uniform),hash(probes.uniformNormals),hash(probes.spawned),hash(probes.spawnedNormals),
            lab.PipelineStatistics().surfaceCacheProbeRayBudget};
        for(std::size_t i=0;i<pixels;++i){const auto& unfiltered=value.image[2][i];const auto& current=value.image[3][i];const auto& accumulated=value.image[4][i];
            const auto diagnostic=value.sampling[i];
            const unsigned actual=unsigned(std::lround(diagnostic.y)),base=unsigned(std::lround(diagnostic.z)),readable=unsigned(std::lround(diagnostic.w));
            Check(diagnostic.x>=0&&diagnostic.x<=1&&diagnostic.y>=0&&diagnostic.z==8&&diagnostic.w>=0&&
                diagnostic.y==float(actual)&&diagnostic.w==float(readable)&&readable<=actual&&actual<=32&&actual%8==0,
                "GPU receiver-loop replay lost whole batches or the actual-N/readable denominator contract");
            const bool fresh=diagnostic.x>.5f;
            const auto classification=value.fresh.empty()?glm::vec4(0,float(base),0,0):value.fresh[i];
            const auto plan=SurfaceCacheFreshSamplingPlan(classification,value.mask[i].x,base,gi.contact.movingReceiverRayMultiplier,gi.contact.movingReceiverFullCurrent);
            Check(actual==0||actual==plan.total,"GPU receiver budget did not equal the maximum geometric/moving current plan");
            if(fresh){Check(gi.contact.disocclusionSampling&&!value.fresh.empty(),"Fresh rays were selected without an active GPU classifier");
                Check(classification.x>0&&classification.x<=1&&classification.w>.5f&&classification.y>=8&&classification.y<=32&&
                    classification.y<=float(base*gi.contact.disocclusionRayMultiplier),"Selected GPU receiver budget did not match its fresh classifier");
                ++value.freshSelected;}
            else Check(classification.x<=.000001f,"Receiver replay omitted geometric exposure selection");
            if(plan.movingTagged&&pose.owner.size()==pixels&&pose.owner[i]>=0)Check(pose.owner[i]==int(cubeIndex),"Moving current-ray tag selected a static receiver");
            value.movingTagged+=plan.movingTagged;value.movingSelected+=plan.movingEligible;value.movingTraced+=plan.movingEligible&&actual>0;
            value.geometryExtraRays+=actual>0?plan.geometry-base:0;value.movingExtraRays+=actual>plan.geometry?actual-plan.geometry:0;
            value.outsideMovingSelected+=plan.outsideRisk;value.outsideMovingRays+=plan.outsideRisk?actual:0;
            value.outsideMovingExtraRays+=plan.outsideRisk&&actual>plan.geometry?actual-plan.geometry:0;
            if(!value.fresh.empty()){const auto classification=value.fresh[i];
                const int taggedAge=int(std::lround(classification.w));
                Check(classification.x>=0&&classification.x<=1&&classification.w==float(taggedAge)&&
                    (taggedAge==0||(taggedAge>=1&&taggedAge<=7&&taggedAge%2==1)),
                    "Invalid GPU fresh classification confidence/exposure-age tag");
                if(taggedAge)Check(std::abs(classification.x-float(taggedAge/2)/3.f)<1e-6f,
                    "Fresh sampling confidence did not follow independent exposure age");
                value.classifierFresh+=classification.x>0;}
            value.receiverRays+=actual;value.receiverExtraRays+=actual>base?actual-base:0;value.tracedReceivers+=actual>0;
            if(actual){const double mask=std::max({double(value.mask[i].x),value.fresh.empty()?0.:double(value.fresh[i].x),plan.fullCurrent?1.:0.});
                const double expected=(1.-std::clamp(mask,0.,1.))*double(value.image[1][i].w)+std::clamp(mask,0.,1.)*double(readable)/double(actual);
                Check(std::abs(double(unfiltered.w)-expected)<2e-6,
                    "Receiver readability used a planned/base budget instead of actual current N");}
            Check(unfiltered.w==current.w,"Dynamic contact filter changed center readability");
            Check(accumulated.w==std::clamp(current.w,0.f,1.f),"Dynamic temporal resolve changed center readability");
            const double count=value.history[i].w;Check(count>=0&&count<=64.001,"Dynamic history exceeded the stationary 64-frame cap");
            value.maxHistory=std::max(value.maxHistory,count);value.mature+=count>63.9;
            value.extended+=count>double(gi.temporal.maxFrames)+.01;
            if(!(current.w>0)){++value.unknown;Check(accumulated.w==0&&count==0&&glm::length(glm::vec3(accumulated))<1e-7f,
                    "Dynamic history filled an unavailable current receiver");}
            else{++value.readable;value.historySum+=count;}
            if(value.mask[i].x<=1e-6f&&!fresh&&!plan.fullCurrent){++value.maskZero;Check(unfiltered==current&&unfiltered==value.image[1][i],"Fresh sampling changed an unselected mask-zero receiver");}
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
    struct ShadowMetric {double mse=0,floor=0,dark=0,bright=0,extraDark=0,extraLag=0,projection=0,releaseSquare=0;std::uint64_t count=0;};
    using ShadowErrors=std::array<std::array<ShadowMetric,stages>,6>;
    std::array<ShadowErrors,2> shadowErrors{},shadowNearErrors{};
    std::array<std::array<ShadowErrors,2>,2> movingShadowErrors{};
    const Pose* shadowAnchor=nullptr;unsigned shadowAge=0,shadowVariant=0;
    // Follow a known rigid cube material point independently of production
    // velocity/history data. Four matching primary-ray owners on one planar
    // face guard against sampling an occluder or an edge in the old image.
    const auto referenceAtMaterialPoint=[&](const Pose& pose,std::size_t i,const Pose& anchor,glm::dvec3& referenceValue,glm::dvec3& splitValue){
        if(pose.owner[i]!=int(cubeIndex))return false;
        auto anchorWorld=pose.world[i];anchorWorld.x+=anchor.objectOffset-pose.objectOffset;
        const auto clip=anchor.camera.projection*anchor.camera.view*glm::vec4(anchorWorld,1);
        if(!(clip.w>0))return false;const auto pixel=(glm::vec2(clip)/clip.w*.5f+.5f)*glm::vec2(frame.width,frame.height)-.5f;
        const int x=int(std::floor(pixel.x)),y=int(std::floor(pixel.y));
        if(x<0||y<0||x+1>=int(frame.width)||y+1>=int(frame.height))return false;
        const auto fraction=glm::fract(pixel);glm::dvec3 world(0);referenceValue=splitValue=glm::dvec3(0);
        for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx){const auto j=std::size_t(y+int(dy))*frame.width+unsigned(x+int(dx));
            if(anchor.owner[j]!=int(cubeIndex)||anchor.reference[j].w<=0||glm::dot(anchor.normal[j],pose.normal[i])<.999f)return false;
            const double weight=(dx?fraction.x:1-fraction.x)*(dy?fraction.y:1-fraction.y);
            world+=glm::dvec3(anchor.world[j])*weight;referenceValue+=glm::dvec3(anchor.reference[j])*weight;splitValue+=glm::dvec3(anchor.split[j])*weight;}
        return glm::length(world-glm::dvec3(anchorWorld))<.002;
    };
    const auto measure=[&](Errors& result,Sample& current,const Sample* previous,unsigned frameIndex,Errors* additional=nullptr){const auto& pose=*current.pose;
        // Keep the extended ROI diagnostics off the Windows thread stack.
        std::vector<std::array<FrameMetric,regions>> perFrame(stages);
        const auto previousVP=previous?previous->pose->camera.projection*previous->pose->camera.view:glm::mat4(1);
        for(unsigned y=0;y<frame.height;++y)for(unsigned x=0;x<frame.width;++x){const auto i=std::size_t(y)*frame.width+x;if(!pose.roi[i])continue;
            const auto truth=glm::dvec3(pose.reference[i]),split=glm::dvec3(pose.split[i]);
            bool releasedShadow=false;double release=0;
            bool changedMovingShadow=false;double movingChange=0;
            if(movingReceiver&&shadowAnchor&&shadowAge){glm::dvec3 oldReference,oldSplit;
                if(referenceAtMaterialPoint(pose,i,*shadowAnchor,oldReference,oldSplit)){
                    const double now=glm::dot(truth,luma),before=glm::dot(oldReference,luma);
                    const double a=glm::dot(split,luma),b=glm::dot(oldSplit,luma);
                    movingChange=now-before;const double uncertainty=.5*std::sqrt(a*a+b*b);
                    changedMovingShadow=std::abs(movingChange)>std::max({.00001,.005*std::max(std::abs(now),std::abs(before)),2.*uncertainty});}}
            if(shadowAnchor&&shadowAge&&pose.owner[i]==0&&shadowAnchor->owner[i]==0&&
                glm::dot(pose.normal[i],shadowAnchor->normal[i])>.999f&&
                glm::length(pose.world[i]-shadowAnchor->world[i])<.002f&&shadowAnchor->reference[i].w>0) {
                // A reference-defined lighting change on an always-visible
                // floor receiver. Geometry disocclusion, production masks,
                // history counts and candidate RGB do not select this ROI.
                const double now=glm::dot(truth,luma),before=glm::dot(glm::dvec3(shadowAnchor->reference[i]),luma);
                const double a=glm::dot(split,luma),c=glm::dot(glm::dvec3(shadowAnchor->split[i]),luma);
                const double uncertainty=.5*std::sqrt(a*a+c*c);
                release=now-before;
                releasedShadow=release>std::max({.00001,.005*std::max(std::abs(now),std::abs(before)),2.*uncertainty});
            }
            std::size_t aligned=pixels;bool geometryDisoccluded=false;
            // The fixture mover is a rigid world-X translation. Follow the
            // same material point using the independently specified old pose,
            // then verify the prior primary-ray owner, normal and world point.
            // This is an oracle metric, not production velocity or raw delta.
            if(previous){auto previousWorld=pose.world[i];
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
                if(changedMovingShadow){const unsigned bin=shadowAge==1?0:shadowAge==2?1:shadowAge<=4?2:shadowAge<=8?3:shadowAge<=16?4:5;
                    const double error=glm::dot(residual,luma),direction=movingChange>0?1.:-1.;
                    const double extraLag=std::max(direction*glm::dot(glm::dvec3(current.image[3][i])-value,luma),0.);
                    const auto add=[&](ShadowMetric& m){m.mse+=square(residual);m.floor+=.25*square(split);
                        m.dark+=std::min(error,0.)*std::min(error,0.);m.bright+=std::max(error,0.)*std::max(error,0.);
                        m.extraLag+=extraLag*extraLag;m.projection+=-error*movingChange;m.releaseSquare+=movingChange*movingChange;++m.count;};
                    add(movingShadowErrors[shadowVariant][0][bin][s]);
                    if(pose.roi[i]&64)add(movingShadowErrors[shadowVariant][1][bin][s]);}
                if(releasedShadow) {
                    const unsigned bin=shadowAge==1?0:shadowAge==2?1:shadowAge<=4?2:shadowAge<=8?3:shadowAge<=16?4:5;
                    const double error=glm::dot(residual,luma);
                    const double extra=std::max(glm::dot(glm::dvec3(current.image[3][i])-value,luma),0.);
                    const auto addShadow=[&](ShadowMetric& m){m.mse+=square(residual);m.floor+=.25*square(split);
                        m.dark+=std::min(error,0.)*std::min(error,0.);m.bright+=std::max(error,0.)*std::max(error,0.);m.extraDark+=extra*extra;
                        m.projection+=-error*release;m.releaseSquare+=release*release;++m.count;};
                    addShadow(shadowErrors[shadowVariant][bin][s]);
                    const double nearGap=std::min(boxDistance(pose.world[i],sweptCubeInverses[pose.objectOffset>.6f?8:0],cube.scale),
                        boxDistance(pose.world[i],sweptCubeInverses[shadowAnchor->objectOffset>.6f?8:0],cube.scale));
                    if(nearGap<.70)addShadow(shadowNearErrors[shadowVariant][bin][s]);
                }
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
            std::cout<<"FRESH SAMPLING GI quality view="<<viewNames[view]<<" variant="<<variantNames[variant]<<" segment="<<segmentNames[segment]
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
                <<" aligned_pairs="<<m.alignedPairs<<" static_receiver_alignment_only=0 rigid_translation_alignment=1";
            {constexpr std::array<const char*,3> labels{{"exposure1","exposure2to4","exposure5to8"}};
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

    const unsigned beginView=options.surfaceCacheDynamicView<3?options.surfaceCacheDynamicView:shadowMode?2:0;
    const unsigned endView=options.surfaceCacheDynamicView<3||shadowMode?beginView+1:3;
    for(unsigned view=beginView;view<endView;++view){std::array<Pose,poseCount> poses;
        const bool holdShadows=shadowMode&&view==2&&!options.surfaceCacheShadowHistoryContinuous;
        shadowErrors={};shadowNearErrors={};movingShadowErrors={};shadowAnchor=nullptr;shadowAge=0;
        auto base=SurfaceCacheFreshSamplingCamera(view,frame.OutputAspect(),movingReceiver);
        const auto inverse=glm::inverse(base.view);const auto right=glm::normalize(glm::vec3(inverse[0])),forward=glm::normalize(-glm::vec3(inverse[2]));
        const float motionScale=options.surfaceCacheDynamicFast?3.f:1.f;
        for(unsigned step=0;step<poseCount;++step){auto& pose=poses[step];pose.camera=base;
            if(view<2){pose.camera.position+=right*((view?.015f:.0125f)*float(step)*motionScale);
                const auto direction=glm::vec3(glm::rotate(glm::mat4(1),(view?.003f:.0025f)*float(step)*motionScale,glm::vec3(0,1,0))*glm::vec4(forward,0));
                pose.camera.view=glm::lookAt(pose.camera.position,pose.camera.position+direction,glm::vec3(0,1,0));
            }else pose.objectOffset=.15f*float(step);
            if(!holdShadows||step==0||step==8)reference(pose,view,step);}
        // A mismatched, remote cache forces unavailable transport independently
        // of the legal known-black lamp-off measurement below.
        auto unrelatedInstances=lab.PreparedProbeSdfScene()->Instances();for(auto& instance:unrelatedInstances)instance.localToWorld[3].x+=50;
        Render::ProbeGlobalSdfSettings tinyCache;tinyCache.counts=glm::uvec3(2);
        const auto unrelatedScene=Render::BuildProbeSdfScene(unrelatedInstances,tinyCache);const auto unrelatedCache=Render::BuildSurfaceCache(*unrelatedScene);
        std::vector<std::array<std::uint64_t,13>> upstream;std::vector<std::array<std::uint64_t,stages+1>> staticHashes;
        std::vector<std::array<std::uint64_t,2>> currentContactHashes;std::vector<std::uint64_t> geometryClassificationHashes;
        std::array<Errors,segments> shipped{};
        std::cout<<"FRESH SAMPLING GI setup view="<<viewNames[view]<<" size="<<frame.width<<'x'<<frame.height<<" unique_reference_poses="<<(holdShadows?2:poseCount)
            <<" warm="<<warmFrames<<" moving="<<movingFrames<<" settle="<<settleFrames<<" motion_laps="<<(holdShadows?0:2)
            <<" hold_after_separation="<<holdShadows<<" separated_hold_frames="<<(holdShadows?16:0)<<" returned_hold_frames="<<(holdShadows?32:0)
            <<" sustained_lap_has_no_stationary_frame="<<!holdShadows
            <<" camera_motion_scale="<<motionScale
            <<" probe_rays=64 receiver_rays=8 placement_spacing=32 placement_phase=0 base_seed=0 static_convergence=1 contact=1 retain_object_motion=1"
            <<" shadow_history_test="<<shadowMode<<" moving_receiver_fixture="<<movingReceiver<<" variants_available=2 comparison_baseline="<<variantNames[baselineVariant]
            <<" disocclusion_multiplier="<<options.surfaceCacheFreshSamplingMultiplier<<" moving_receiver_candidate_multiplier="<<options.surfaceCacheMovingReceiverRayMultiplier<<" moving_receiver_full_mode="<<options.surfaceCacheMovingReceiverFullCurrent<<" receiver_extra_rays_are_current=1"
            <<" object_transform_oracle=1 swept_floor_union_poses="<<poseCount
            <<" frames_cli_applies_to_performance_only=1 sampling_diagnostic_replay=1 main_pass_hardware_counter=0"<<std::endl;
        for(unsigned variant=0;variant<variantNames.size();++variant){
            configure(variant);setPose(poses[0]);reset();std::array<Errors,segments> errors{};shadowVariant=variant;
            struct Budget {std::uint64_t pixels=0,classifier=0,selected=0,traced=0,rays=0,extra=0;
                std::uint64_t movingTagged=0,movingSelected=0,movingTraced=0,geometryExtra=0,movingExtra=0;
                std::uint64_t outsideMovingSelected=0,outsideMovingRays=0,outsideMovingExtra=0;
                std::array<std::uint64_t,3> exposure{},exposureSelected{},exposureRays{};};
            std::array<Budget,segments> budget{};
            Sample previous;std::uint64_t unknown=0,metadata=0,maskZero=0,selected=0,historyUsed=0,readable=0,mature=0,movingExtended=0,movingReadable=0;
            double historySum=0,movingHistorySum=0,movingMaxHistory=0;unsigned contactExactFrames=0,classificationExactFrames=0;
            for(unsigned f=0;f<totalFrames;++f){unsigned poseIndex=0,segment=0;
                if(f>=warmFrames&&f<warmFrames+movingFrames){const unsigned move=(f-warmFrames)%16;poseIndex=move<8?move+1:15-move;segment=1;}
                else if(f>=warmFrames+movingFrames)segment=2;
                if(holdShadows&&f>=warmFrames) {
                    const bool separated=f<warmFrames+16;poseIndex=separated?8:0;
                    shadowAnchor=&poses[separated?0:8];shadowAge=f-(separated?warmFrames:warmFrames+16)+1;
                } else if(movingReceiver&&segment==1&&previous.pose){shadowAnchor=previous.pose;shadowAge=1;}
                else {shadowAnchor=nullptr;shadowAge=0;}
                setPose(poses[poseIndex]);render();auto current=sample(poses[poseIndex]);
                if(variant==baselineVariant){upstream.push_back(current.upstream);if(f<warmFrames)staticHashes.push_back(current.staticHash);
                    if(shadowMode)currentContactHashes.push_back({current.staticHash[2],current.staticHash[3]});
                    if(movingReceiver)geometryClassificationHashes.push_back(current.geometryClassificationHash);}
                else{Check(upstream[f]==current.upstream,"Fresh sampling A/B changed raw rays, placement, coverage, geometry risk or contact masks");
                    if(shadowMode&&(!movingReceiver||options.surfaceCacheMovingReceiverRayMultiplier==1||f<warmFrames)){
                        Check(currentContactHashes[f]==std::array<std::uint64_t,2>{current.staticHash[2],current.staticHash[3]},
                            "Reactive shadow history changed a protected current receiver measurement");++contactExactFrames;}
                    if(movingReceiver){Check(geometryClassificationHashes[f]==current.geometryClassificationHash,
                            "Moving current rays changed exposure X/Y/W, geometric freshness bit or independent age");++classificationExactFrames;}
                    if(f<warmFrames)Check(staticHashes[f]==current.staticHash,"Fresh sampling A/B changed the reset/static 64-frame sequence");}
                if(f==0)Check(!current.historyUsed,"Fresh sampling reset retained previous variant history");
                else Check(current.historyUsed,"Complete fresh-sampling dynamic sequence unexpectedly lost certified compatible history");
                if(segment==1){double cap=view==2?double(gi.temporal.dynamicMaxFrames):double(gi.temporal.maxFrames);
                    if(view<2)cap=std::min(64.,std::max(32.,double(gi.temporal.maxFrames)));
                    if(holdShadows) {
                        const unsigned sinceChange=(f-warmFrames)%16;
                        cap=sinceChange<=gi.temporal.dynamicSettleFrames?double(gi.temporal.dynamicMaxFrames):64.;
                    }
                    Check(current.maxHistory<=cap+.001,"Moving GI exceeded its responsive/local bounded history cap");movingExtended+=current.extended;
                    movingReadable+=current.readable;movingHistorySum+=current.historySum;movingMaxHistory=std::max(movingMaxHistory,current.maxHistory);}
                if(view==2&&!holdShadows&&segment==2&&f-warmFrames-movingFrames<gi.temporal.dynamicSettleFrames)
                    Check(current.maxHistory<=double(gi.temporal.dynamicMaxFrames)+.001,"Moving cube settling lost its dynamic history cap");
                // The static diagnostic is the actual frame-64 texture. All
                // moving/settling frames are measured without CPU accumulation.
                if(f+1>=warmFrames){Errors* additional=segment==1&&f>=warmFrames+16?&errors[3]:
                        segment==2?(f-warmFrames-movingFrames<8?&errors[4]:&errors[5]):nullptr;
                    measure(errors[segment],current,f?&previous:nullptr,f+1,additional);}
                if(f<warmFrames)Check(current.freshSelected==0&&current.receiverExtraRays==0,
                    "Fresh sampling added rays to the cold static 64-frame sequence");
                auto& b=budget[segment];b.pixels+=pixels;b.classifier+=current.classifierFresh;b.selected+=current.freshSelected;
                b.traced+=current.tracedReceivers;b.rays+=current.receiverRays;b.extra+=current.receiverExtraRays;
                b.movingTagged+=current.movingTagged;b.movingSelected+=current.movingSelected;b.movingTraced+=current.movingTraced;
                b.geometryExtra+=current.geometryExtraRays;b.movingExtra+=current.movingExtraRays;
                b.outsideMovingSelected+=current.outsideMovingSelected;b.outsideMovingRays+=current.outsideMovingRays;b.outsideMovingExtra+=current.outsideMovingExtraRays;
                Check(current.receiverExtraRays==current.geometryExtraRays+current.movingExtraRays,"Geometric/moving extra-ray accounting overlapped or lost rays");
                for(std::size_t i=0;i<pixels;++i)if(current.exposureAge[i]&&current.exposureAge[i]<=8){
                    const auto bin=current.exposureAge[i]==1?0:current.exposureAge[i]<=4?1:2;
                    ++b.exposure[bin];b.exposureSelected[bin]+=current.sampling[i].x>.5f;
                    b.exposureRays[bin]+=std::uint64_t(std::lround(current.sampling[i].y));}
                unknown+=current.unknown;metadata+=current.metadata;maskZero+=current.maskZero;selected+=current.selected;
                historyUsed+=current.historyUsed;readable+=current.readable;historySum+=current.historySum;if(f+1==warmFrames)mature=current.mature;
                if(!options.screenshot.empty()&&(f+1==warmFrames||f+1==warmFrames+24||f+1==warmFrames+movingFrames+1||f+1==totalFrames||
                    (holdShadows&&(f+1==65||f+1==66||f+1==68||f+1==72||f+1==80||f+1==81||f+1==82||f+1==84)))){
                    const std::string label=std::string(viewNames[view])+"_"+variantNames[variant]+"_frame"+std::to_string(f+1);
                    Save(lab.Readback(),outputPath(label+"_total_renderer_ldr"));saveTone(current.image[1],label+"_probe_spatial_tone4");
                    saveTone(current.image[3],label+"_contact_filtered_tone4");saveTone(current.image[4],label+"_temporal_tone4");}
                previous=std::move(current);}
            for(unsigned segment=0;segment<segments;++segment)report(errors[segment],view,variant,segment);
            if(holdShadows) {
                constexpr std::array<const char*,6> ages{{"1","2","3to4","5to8","9to16","17to32"}};
                for(unsigned region=0;region<2;++region)for(unsigned age=0;age<6;++age)for(unsigned stage=0;stage<stages;++stage) {
                    const auto& data=region?shadowNearErrors:shadowErrors;
                    const auto& m=data[variant][age][stage];if(!m.count)continue;
                    std::cout<<"SHADOW HISTORY release region="<<(region?"near_mover_0to70cm":"all_visible_floor")<<" variant="<<variantNames[variant]<<" age="<<ages[age]<<" stage="<<stageNames[stage]
                        <<" pixel_frames="<<m.count<<" rgb_mse="<<mean(m.mse,m.count)<<" reference_floor="<<mean(m.floor,m.count)
                        <<" dark_luma_mse="<<mean(m.dark,m.count)<<" bright_luma_mse="<<mean(m.bright,m.count)
                        <<" temporal_extra_dark_mse="<<mean(m.extraDark,m.count)<<" old_shadow_projection="<<m.projection/std::max(m.releaseSquare,1e-20)
                        <<" always_visible_floor=1 reference_change_selects_roi=1 production_mask_selects_roi=0 confidence_interval=0"<<std::endl;
                    if(variant) {const auto& old=data[0][age][stage];
                        const auto percent=[](double a,double b){return 100.*(b-a)/std::max(std::abs(a),1e-20);};
                        std::cout<<"SHADOW HISTORY comparison region="<<(region?"near_mover_0to70cm":"all_visible_floor")<<" age="<<ages[age]<<" stage="<<stageNames[stage]<<" pixel_frames="<<m.count
                            <<" rgb_mse_change_percent="<<percent(mean(old.mse,old.count),mean(m.mse,m.count))
                            <<" dark_mse_change_percent="<<percent(mean(old.dark,old.count),mean(m.dark,m.count))
                            <<" temporal_extra_dark_change_percent="<<percent(mean(old.extraDark,old.count),mean(m.extraDark,m.count))
                            <<" old_shadow_projection_before="<<old.projection/std::max(old.releaseSquare,1e-20)
                            <<" old_shadow_projection_after="<<m.projection/std::max(m.releaseSquare,1e-20)<<" improvement_assertion=0"<<std::endl;}
                }
                if(!movingReceiver)Check(shadowErrors[variant][0][4].count>100,"Shadow release fixture has too few always-visible reference-confirmed floor receivers");
            }
            if(movingReceiver){
                constexpr std::array<const char*,6> ages{{"1","2","3to4","5to8","9to16","17to32"}};
                for(unsigned region=0;region<2;++region)for(unsigned age=0;age<6;++age)for(unsigned stage=0;stage<stages;++stage){
                    const auto& m=movingShadowErrors[variant][region][age][stage];if(!m.count)continue;
                    std::cout<<"MOVING RECEIVER SHADOW quality region="<<(region?"cube_left_face_changed_lighting":"cube_changed_lighting")
                        <<" variant="<<variantNames[variant]<<" mode="<<(holdShadows?"separation_hold":"continuous_per_step")<<" age="<<ages[age]<<" stage="<<stageNames[stage]
                        <<" pixel_frames="<<m.count<<" rgb_mse="<<mean(m.mse,m.count)<<" reference_floor="<<mean(m.floor,m.count)
                        <<" dark_luma_mse="<<mean(m.dark,m.count)<<" bright_luma_mse="<<mean(m.bright,m.count)
                        <<" temporal_extra_signed_lag_mse="<<mean(m.extraLag,m.count)<<" old_shadow_projection="<<m.projection/std::max(m.releaseSquare,1e-20)
                        <<" material_point_alignment=rigid_world_x_bilinear_reference primary_owner_verified=1 world_tolerance_m=.002 reference_change_selects_roi=1"
                        <<" both_lighting_change_directions=1 production_mask_selects_roi=0 confidence_interval=0"<<std::endl;
                    if(variant){const auto& old=movingShadowErrors[0][region][age][stage];
                        const auto percent=[](double a,double b){return 100.*(b-a)/std::max(std::abs(a),1e-20);};
                        std::cout<<"MOVING RECEIVER SHADOW comparison region="<<(region?"cube_left_face_changed_lighting":"cube_changed_lighting")
                            <<" mode="<<(holdShadows?"separation_hold":"continuous_per_step")<<" age="<<ages[age]<<" stage="<<stageNames[stage]<<" pixel_frames="<<m.count
                            <<" rgb_mse_change_percent="<<percent(mean(old.mse,old.count),mean(m.mse,m.count))
                            <<" temporal_extra_signed_lag_change_percent="<<percent(mean(old.extraLag,old.count),mean(m.extraLag,m.count))
                            <<" old_shadow_projection_before="<<old.projection/std::max(old.releaseSquare,1e-20)
                            <<" old_shadow_projection_after="<<m.projection/std::max(m.releaseSquare,1e-20)<<" improvement_assertion=0"<<std::endl;}}
                Check(movingShadowErrors[variant][0][0][4].count>32,"Moving receiver fixture lacks reference-confirmed material-point lighting changes");
            }
            for(unsigned segment=0;segment<3;++segment){const auto& b=budget[segment];
                std::cout<<"FRESH SAMPLING GI budget view="<<viewNames[view]<<" variant="<<variantNames[variant]<<" segment="<<segmentNames[segment]
                    <<" pixel_frames="<<b.pixels<<" classifier_pixel_frames="<<b.classifier<<" selected_pixel_frames="<<b.selected
                    <<" selected_fraction="<<mean(double(b.selected),b.pixels)<<" traced_receivers="<<b.traced
                    <<" actual_receiver_rays="<<b.rays<<" extra_receiver_rays="<<b.extra
                    <<" moving_tagged_pixel_frames="<<b.movingTagged<<" moving_selected_pixel_frames="<<b.movingSelected<<" moving_traced_receivers="<<b.movingTraced
                    <<" geometric_extra_receiver_rays="<<b.geometryExtra<<" moving_incremental_extra_receiver_rays="<<b.movingExtra
                    <<" mask_outside_moving_selected="<<b.outsideMovingSelected<<" mask_outside_moving_rays="<<b.outsideMovingRays<<" mask_outside_moving_incremental_extra_rays="<<b.outsideMovingExtra
                    <<" extra_attribution=geometry_first_then_incremental_moving moving_receiver_multiplier="<<gi.contact.movingReceiverRayMultiplier<<" full_mode="<<gi.contact.movingReceiverFullCurrent
                    <<" budget_source=gpu_same_loop_diagnostic_replay hardware_counter=0";
                constexpr std::array<const char*,3> labels{{"exposure1","exposure2to4","exposure5to8"}};
                for(unsigned bin=0;bin<3;++bin)std::cout<<' '<<labels[bin]<<"_pixels="<<b.exposure[bin]
                    <<' '<<labels[bin]<<"_selected="<<b.exposureSelected[bin]<<' '<<labels[bin]<<"_actual_rays="<<b.exposureRays[bin];
                std::cout<<std::endl;}
            if(variant==1&&options.surfaceCacheFreshSamplingMultiplier>1)Check(budget[1].selected>0&&budget[1].geometryExtra>0,
                "Fresh sampling quality motion sequence never exercised extra geometric exposure rays");
            if(movingReceiver&&variant&&options.surfaceCacheMovingReceiverRayMultiplier>1)Check(budget[1].movingSelected>0&&budget[1].movingExtra>0,
                "Moving receiver quality sequence never exercised independent extra current rays");
            if(movingReceiver&&variant&&options.surfaceCacheMovingReceiverFullCurrent&&options.surfaceCacheMovingReceiverRayMultiplier>1)
                Check(budget[1].outsideMovingSelected>0&&budget[1].outsideMovingExtra>0,"Full-current moving fixture never traced beyond its original risk-mask selection");
            std::cout<<"FRESH SAMPLING GI invariants view="<<viewNames[view]<<" variant="<<variantNames[variant]<<" frames="<<totalFrames<<" static_sequence_frames="<<warmFrames
                <<" upstream_exact_frames="<<(variant!=baselineVariant?upstream.size():0)<<" static_exact_frames="<<(variant!=baselineVariant?staticHashes.size():0)
                <<" current_contact_exact_frames="<<contactExactFrames<<" geometry_exposure_exact_frames="<<classificationExactFrames<<" moving_receiver_multiplier="<<gi.contact.movingReceiverRayMultiplier<<" full_mode="<<gi.contact.movingReceiverFullCurrent<<" raw_ray_metadata_preserved="<<metadata<<" unknown_current_preserved="<<unknown<<" mask_zero_preserved="<<maskZero
                <<" selected_receiver_pixel_frames="<<selected<<" history_used_frames="<<historyUsed<<" history_mean="<<mean(historySum,readable)
                <<" frame64_mature_pixels="<<mature<<" source_sequence_aligned=1 geometry_roi_independent=1"<<std::endl;
            std::cout<<"FRESH SAMPLING GI configuration view="<<viewNames[view]<<" variant="<<variantNames[variant]
                <<" motion_stability=1 object_motion_reprojection=1 disocclusion_filter=0"
                <<" reactive_shadows="<<gi.temporal.reactiveShadows<<" reactive_moving_receivers="<<gi.temporal.reactiveMovingReceivers<<" moving_receiver_multiplier="<<gi.contact.movingReceiverRayMultiplier<<" full_mode="<<gi.contact.movingReceiverFullCurrent<<" disocclusion_sampling="<<gi.contact.disocclusionSampling<<" multiplier="<<gi.contact.disocclusionRayMultiplier
                <<" moving_history_mean="<<mean(movingHistorySum,movingReadable)<<" moving_history_max="<<movingMaxHistory<<std::endl;
            if(variant==baselineVariant)shipped=errors;

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
                std::cout<<"FRESH SAMPLING GI SUMMARY view="<<viewNames[view]<<" variant="<<variantNames[variant]<<" region="<<regionNames[r]
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
                {constexpr std::array<const char*,3> labels{{"exposure1","exposure2to4","exposure5to8"}};
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
                std::cout<<"FRESH SAMPLING GI comparison view="<<viewNames[view]<<" variant="<<variantNames[variant]<<" segment="<<segmentNames[segment]<<" region="<<regionNames[r]
                    <<" baseline="<<variantNames[baselineVariant]
                    <<" shipped_temporal_mse="<<oldMse<<" candidate_temporal_mse="<<newMse<<" mse_change_percent="<<100*(newMse-oldMse)/std::max(oldMse,1e-20)
                    <<" shipped_aligned_delta="<<mean(before.alignedDelta,before.alignedPairs)<<" candidate_aligned_delta="<<mean(after.alignedDelta,after.alignedPairs)
                    <<" diagnostic_only=1 candidate_improvement_assertion=0"<<std::endl;}

            frame.effects.areaLights.lights[0].enabled=false;render();const auto black=sample(poses[0]);
            Check(!black.historyUsed&&black.readable>100&&energy(black.image[0])<1e-5&&energy(black.image[3])<1e-5&&energy(black.image[4])<1e-5,
                "Fresh sampling GI lamp-off edit lost readable black or retained old lighting");
            // Exercise the additional current-ray path with legal black, not
            // only the cold/reset path on which fresh classification is off.
            setPose(poses[1]);render();const auto movingBlack=sample(poses[1]);
            Check(movingBlack.readable>100&&energy(movingBlack.image[3])<1e-5&&energy(movingBlack.image[4])<1e-5,
                "Fresh current rays lost readable black during actual motion");
            setPose(poses[0]);frame.effects.areaLights.lights[0].enabled=true;render();gi.cache=unrelatedCache;render();
            Check(!lab.PipelineStatistics().surfaceCacheHistoryUsed,"Fresh sampling GI unavailable cache reused valid-cache history");render();const auto unavailable=sample(poses[0]);
            Check(unavailable.unknown>0&&energy(unavailable.image[0])<1e-5&&energy(unavailable.image[3])<1e-5&&energy(unavailable.image[4])<1e-5,
                "Fresh sampling GI unavailable cache acquired fabricated transport");
            setPose(poses[1]);render();const auto movingUnavailable=sample(poses[1]);
            Check(movingUnavailable.unknown>0&&energy(movingUnavailable.image[3])<1e-5&&energy(movingUnavailable.image[4])<1e-5,
                "Fresh current rays fabricated unavailable transport during actual motion");
            std::cout<<"FRESH SAMPLING GI unavailable_black view="<<viewNames[view]<<" variant="<<variantNames[variant]<<" readable_black="<<black.readable
                <<" unavailable_current="<<unavailable.unknown<<" black_temporal_energy="<<energy(black.image[4])<<" unavailable_temporal_energy="<<energy(unavailable.image[4])
                <<" moving_black_selected="<<movingBlack.freshSelected<<" moving_black_extra_rays="<<movingBlack.receiverExtraRays
                <<" moving_unavailable_selected="<<movingUnavailable.freshSelected<<" moving_unavailable_extra_rays="<<movingUnavailable.receiverExtraRays
                <<" post_contact_unknown_contract=1"<<std::endl;gi.cache=baselineGi.cache;
        }
        // Exposure events are geometric motion, not an alias for the optional
        // static-convergence policy or a temporal frame-count configuration.
        for(unsigned policy=0;policy<2;++policy) {
            configure(1);setPose(poses[0]);gi.temporal.staticConvergence=policy!=0;
            if(policy)gi.temporal.maxFrames=1;
            reset();render();render();
            Check(!lab.PipelineStatistics().surfaceCacheFreshSamplingCollected,
                "An unchanged camera/model triggered fresh sampling under a nondefault history policy");
            const auto diagnostic=lab.ReadbackSurfaceCacheReceiverSampling();finite(diagnostic,"stationary policy sampling replay");
            for(const auto& d:diagnostic)Check(d.x==0&&(d.y==0||d.y==8),
                "Stationary history policy added receiver rays without an exposure event");
        }
        std::cout<<"FRESH SAMPLING GI stationary_policy view="<<viewNames[view]
            <<" static_convergence_disabled_extra_rays=0 max_frames_one_extra_rays=0"<<std::endl;
    }
    std::cout<<"Surface Cache fresh-current-sampling quality passed finite/unknown/black/metadata/static-equality contracts; quality changes are diagnostic, not an improvement assertion."<<std::endl;
}

// Timing uses production draws only. A separate untimed GPU-loop replay below
// reports actual launched current rays without contaminating timestamp samples.
void SurfaceCacheFreshSamplingPerformance(MaterialLabModule& lab,const Options& options) {
    const bool shadowMode=options.surfaceCacheShadowHistoryPerformance;
    const bool movingReceiver=shadowMode&&options.surfaceCacheMovingReceiverTest;
    Check(options.surfaceCacheMovingReceiverRayMultiplier==1||options.surfaceCacheMovingReceiverRayMultiplier==2||options.surfaceCacheMovingReceiverRayMultiplier==4,"Moving receiver ray multiplier must be 1, 2 or 4");
    Check(options.vulkan&&options.softwareRays&&!options.validation,"Fresh sampling performance requires Vulkan software rays without validation");
    const unsigned measured=options.surfaceCacheDynamicFrames,warm=64,settle=16;
    Check(measured>=16&&measured%16==0,"Fresh sampling performance needs complete 16-frame laps");
    Check(options.surfaceCacheFreshSamplingMultiplier>=1&&options.surfaceCacheFreshSamplingMultiplier<=4,"Fresh sampling multiplier must be 1..4");
    auto fixture=options;fixture.sealedGiRoom=true;fixture.sdfBanquet=false;fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);frame.width=options.renderSizeSet?options.benchmarkWidth:1280;
    frame.height=options.renderSizeSet?options.benchmarkHeight:800;frame.effects.outputSize={0,0};frame.present=false;frame.screenProbeStudyHud=false;
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::None;
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;auto& trace=placement.trace;
    gi.enabled=true;gi.useScreenProbes=true;gi.view=0;gi.reconstructionDebug=0;gi.probeDiagnostics=false;gi.probeOriginDiagnostics=false;
    gi.collectProbeCoverage=false;gi.safeProbeOrigins=true;gi.rasterSourcePositions=true;gi.directionalReconstruction=true;gi.cacheBilinear=true;
    gi.spatial={};gi.temporal={};gi.contact={};gi.temporal.staticConvergence=true;gi.temporal.motionStability=true;
    gi.temporal.motionConsistentSpatialReuse=false;gi.temporal.localCameraHistory=false;gi.temporal.motionSpatialFilter=false;
    gi.temporal.motionDisocclusionRecovery=false;gi.temporal.retainHistoryOnObjectMotion=true;gi.contact.raysPerPixel=8;gi.contact.referenceReceiver=false;
    gi.contact.collectSamplingDiagnostics=false;gi.contact.disocclusionRayMultiplier=options.surfaceCacheFreshSamplingMultiplier;
    placement.enabled=true;placement.adaptive=true;placement.cornerRefinement=true;placement.curvatureRefinement=true;
    placement.curvatureAwareWeights=true;placement.continuousInterpolation=true;placement.baseSpacing=32;placement.candidateCount=8;
    placement.freeze=false;placement.frameIndex=0;trace.enabled=true;trace.cosineHemisphere=true;trace.singleInvocation=false;
    trace.rayResolution=8;trace.samplingSeed=0;trace.maxDistance=gi.maxDistance;trace.maxSteps=512;trace.sdf.enabled=true;trace.sdf.maxSteps=256;
    const auto reset=[&]{frame.effects.cameraCut=true;placement.reset=true;gi.temporal.reset=true;};
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;placement.reset=false;gi.temporal.reset=false;};
    constexpr unsigned scopeCount=11;
    constexpr std::array<const char*,scopeCount> scopeNames{{"frame","screen_trace","sdf_trace","reconstruction","probe_spatial","contact_trace","contact_filter","temporal","cache_lighting","receiver_motion","fresh_sampling"}};
    constexpr std::array<const char*,3> viewNames{{"oval_camera","contact_camera","cube_fixed_camera"}};
    const std::array<const char*,2> names{{movingReceiver?"rigid_receiver_legacy":shadowMode?"fresh_current_rays":"current_object_stability",movingReceiver?"reactive_moving_receiver":shadowMode?"reactive_shadow_history":"fresh_current_rays"}};
    constexpr std::array<unsigned,4> order{{0,1,1,0}};
    struct Samples {std::array<std::vector<double>,scopeCount> scope;std::vector<double> wall;
        std::uint64_t attributes=0,metadata=0,geometry=0,regions=0,lit=0,rays=0;unsigned history=0;};
    const auto mean=[](const auto& a){double sum=0;for(double v:a)sum+=v;return a.empty()?0.:sum/double(a.size());};
    const auto median=[](std::vector<double> a){if(a.empty())return 0.;std::sort(a.begin(),a.end());const auto n=a.size();
        return n%2?a[n/2]:.5*(a[n/2-1]+a[n/2]);};
    const auto p95=[](std::vector<double> a){if(a.empty())return 0.;std::sort(a.begin(),a.end());return a[std::size_t(std::ceil(.95*double(a.size())))-1];};
    const auto append=[](Samples& total,const Samples& a){for(unsigned s=0;s<scopeCount;++s)
            total.scope[s].insert(total.scope[s].end(),a.scope[s].begin(),a.scope[s].end());
        total.wall.insert(total.wall.end(),a.wall.begin(),a.wall.end());total.attributes+=a.attributes;total.metadata+=a.metadata;
        total.geometry+=a.geometry;total.regions+=a.regions;total.lit+=a.lit;total.rays+=a.rays;total.history+=a.history;};
    const auto report=[&](const Samples& a,unsigned view,unsigned variant,unsigned segment,int trial){
        for(unsigned s=0;s<scopeCount;++s)std::cout<<"FRESH SAMPLING PERF scope view="<<viewNames[view]<<" variant="<<names[variant]
            <<" segment="<<(segment?"stopping16":"moving")<<" trial="<<trial<<" name="<<scopeNames[s]<<" samples="<<a.scope[s].size()
            <<" executed="<<!a.scope[s].empty()<<" mean_ms="<<mean(a.scope[s])<<" median_ms="<<median(a.scope[s])<<" p95_ms="<<p95(a.scope[s])<<std::endl;
        std::cout<<"FRESH SAMPLING PERF wall view="<<viewNames[view]<<" variant="<<names[variant]<<" segment="<<(segment?"stopping16":"moving")
            <<" trial="<<trial<<" samples="<<a.wall.size()<<" mean_ms="<<mean(a.wall)<<" median_ms="<<median(a.wall)<<" p95_ms="<<p95(a.wall)
            <<" object_motion_reprojection=1 disocclusion_filter=0 disocclusion_sampling="<<(shadowMode||variant==1)<<" reactive_shadows="<<(shadowMode&&(movingReceiver||variant==1))<<" reactive_moving_receivers="<<(!movingReceiver||variant==1)<<" moving_receiver_multiplier="<<(movingReceiver&&variant?options.surfaceCacheMovingReceiverRayMultiplier:1)<<" full_mode="<<(movingReceiver&&variant&&options.surfaceCacheMovingReceiverFullCurrent)
            <<" history_used_frames="<<a.history<<" probe_ray_budget="<<a.rays<<" attribute_uploaded_texels="<<a.attributes
            <<" metadata_uploaded_texels="<<a.metadata<<" geometry_uploaded_texels="<<a.geometry<<" upload_regions="<<a.regions
            <<" cache_relit_texels="<<a.lit<<" motion_stability=1 extra_image_readbacks=0 cpu_oracle_samples=0 confidence_interval=0"<<std::endl;};
#ifdef NDEBUG
    constexpr const char* build="Release_NDEBUG";
#else
    constexpr const char* build="Debug";
#endif
    const unsigned first=options.surfaceCacheDynamicView<3?options.surfaceCacheDynamicView:shadowMode?2:0,last=options.surfaceCacheDynamicView<3||shadowMode?first+1:3;
    for(unsigned view=first;view<last;++view){auto base=SurfaceCacheFreshSamplingCamera(view,frame.OutputAspect(),movingReceiver);
        const auto inverse=glm::inverse(base.view);const auto right=glm::normalize(glm::vec3(inverse[0])),forward=glm::normalize(-glm::vec3(inverse[2]));
        const float scale=options.surfaceCacheDynamicFast?3.f:1.f,translation=(view==1?.015f:.0125f)*scale,yaw=(view==1?.003f:.0025f)*scale;
        gi.temporal.objectMotionReprojection=true;gi.temporal.disocclusionFilter=false;gi.contact.disocclusionSampling=shadowMode;gi.temporal.reactiveShadows=false;frame.camera=base;frame.screenProbeStudyState.occluderOffset=0;
        reset();render();const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
        while(lab.ProbeSdfQualityStatistics().pending){render();Check(std::chrono::steady_clock::now()<deadline,"Fresh sampling performance SDF preparation timed out");}
        std::array<std::array<Samples,2>,2> combined;std::vector<std::array<std::uint64_t,6>> baselineWork;
        std::cout<<"FRESH SAMPLING PERF setup build="<<build<<" size="<<frame.width<<'x'<<frame.height<<" view="<<viewNames[view]
            <<" shadow_history_performance="<<shadowMode<<" moving_receiver_fixture="<<movingReceiver<<" order=0,1,1,0 warm_per_trial="<<warm<<" measured_moving_per_trial="<<measured<<" measured_stopping_per_trial="<<settle
            <<" camera_translation_step="<<(view==2?0:translation)<<" camera_yaw_step="<<(view==2?0:yaw)<<" object_translation_step="<<(view==2?.15f:0)
            <<" probe_rays=64 receiver_rays=8 receiver_multiplier="<<options.surfaceCacheFreshSamplingMultiplier<<" moving_receiver_candidate_multiplier="<<options.surfaceCacheMovingReceiverRayMultiplier<<" moving_receiver_full_mode="<<options.surfaceCacheMovingReceiverFullCurrent<<" spacing=32 motion_stability=1 object_dynamic_cap="<<gi.temporal.dynamicMaxFrames
            <<" static_convergence=1 contact=1 temporal=1 single_invocation=0 software_bvh=1 validation=0 present=0"
            <<" no_image_readbacks=1 cpu_oracle_samples=0 confidence_interval=0 gpu_excludes_cpu_wait=1 wall_includes_submit_wait=1"<<std::endl;
        for(unsigned trial=0;trial<order.size();++trial){const unsigned variant=order[trial];
            gi.temporal.objectMotionReprojection=true;gi.temporal.disocclusionFilter=false;gi.contact.disocclusionSampling=shadowMode||variant==1;gi.temporal.reactiveShadows=shadowMode&&(movingReceiver||variant==1);gi.temporal.reactiveMovingReceivers=!movingReceiver||variant==1;gi.contact.movingReceiverRayMultiplier=movingReceiver&&variant?options.surfaceCacheMovingReceiverRayMultiplier:1;gi.contact.movingReceiverFullCurrent=movingReceiver&&variant&&options.surfaceCacheMovingReceiverFullCurrent;
            frame.camera=base;frame.screenProbeStudyState.occluderOffset=0;reset();for(unsigned f=0;f<warm;++f)render();
            std::array<Samples,2> samples;
            for(unsigned f=0;f<measured+settle;++f){const bool moving=f<measured;const unsigned cycle=f%16,step=moving?(cycle<8?cycle+1:15-cycle):0;
                frame.camera=base;if(view==2)frame.screenProbeStudyState.occluderOffset=.15f*float(step);
                else {frame.camera->position+=right*(translation*float(step));const auto direction=glm::vec3(glm::rotate(glm::mat4(1),yaw*float(step),glm::vec3(0,1,0))*glm::vec4(forward,0));
                    frame.camera->view=glm::lookAt(frame.camera->position,frame.camera->position+direction,glm::vec3(0,1,0));}
                const auto timing=lab.MeasureFrames(frame,1,0.f);const auto stats=lab.PipelineStatistics();auto& sample=samples[moving?0:1];
                const std::array<const std::vector<double>*,scopeCount> source{{&timing.gpuFrameMilliseconds,&timing.probeScreenTraceFrameMilliseconds,
                    &timing.probeSdfTraceFrameMilliseconds,&timing.probeReconstructionFrameMilliseconds,&timing.probeSpatialFrameMilliseconds,
                    &timing.contactTraceFrameMilliseconds,&timing.contactFilterFrameMilliseconds,&timing.surfaceCacheTemporalFrameMilliseconds,&timing.surfaceCacheLightingFrameMilliseconds,
                    &timing.receiverMotionFrameMilliseconds,&timing.freshSamplingFrameMilliseconds}};
                for(unsigned s=0;s<scopeCount;++s){Check(source[s]->size()<2,"Duplicate fresh sampling GPU timestamp");
                    if(s<8)Check(source[s]->size()==1,std::string("Missing fresh sampling GPU stage ")+scopeNames[s]);
                    for(double v:*source[s]){Check(std::isfinite(v)&&v>0,"Invalid fresh sampling GPU timestamp");sample.scope[s].push_back(v);}}
                Check(source[9]->size()==unsigned(gi.temporal.objectMotionReprojection),
                    "Receiver-motion GPU scope was missing or executed in a non-object variant");
                Check(source[10]->size()==unsigned(stats.surfaceCacheFreshSamplingCollected),
                    "Fresh-classification GPU scope did not match its actual execution");
                if(!gi.contact.disocclusionSampling)Check(source[10]->empty(),"Disabled fresh sampling ran its classifier");
                Check(std::isfinite(timing.elapsedMilliseconds)&&timing.elapsedMilliseconds>0,"Invalid fresh sampling wall timestamp");sample.wall.push_back(timing.elapsedMilliseconds);
                Check(stats.surfaceCacheHistoryUsed&&stats.surfaceCacheContactCollected&&stats.surfaceCacheSpatialPasses>0&&stats.surfaceCacheTemporalPasses>0&&
                    !lab.ProbeSdfQualityStatistics().pending,"Fresh sampling performance lost compatible history or prepared full GI");
                Check(!stats.surfaceCacheReceiverSamplingCollected,"Timed fresh-sampling production frames executed diagnostic replay rays");
                if(view==2&&moving){const auto cache=lab.PreparedSurfaceCache();Check(bool(cache)&&source[8]->size()==1&&stats.surfaceCacheLightingUpdated&&
                        stats.surfaceCacheUpdatedTexels==cache->TexelCount(),"Fresh sampling object performance omitted full current cache relighting");}
                else Check(source[8]->empty()&&!stats.surfaceCacheLightingUpdated&&stats.surfaceCacheUpdatedTexels==0&&stats.surfaceCacheAttributeUploadedTexels==0&&
                    stats.surfaceCacheMetadataUploadedTexels==0&&stats.surfaceCacheGeometryUploadedTexels==0&&stats.surfaceCacheUploadRegions==0,
                    "Fresh sampling camera/stop performance unexpectedly relit or uploaded cache");
                const std::array<std::uint64_t,6> work{{stats.surfaceCacheProbeRayBudget,stats.surfaceCacheAttributeUploadedTexels,stats.surfaceCacheMetadataUploadedTexels,
                    stats.surfaceCacheGeometryUploadedTexels,stats.surfaceCacheUploadRegions,stats.surfaceCacheUpdatedTexels}};
                if(trial==0)baselineWork.push_back(work);else Check(work==baselineWork[f],"Fresh sampling A/B changed probe budget or cache work");
                sample.rays+=work[0];sample.attributes+=work[1];sample.metadata+=work[2];sample.geometry+=work[3];sample.regions+=work[4];sample.lit+=work[5];++sample.history;
            }
            for(unsigned segment=0;segment<2;++segment){report(samples[segment],view,variant,segment,int(trial));append(combined[variant][segment],samples[segment]);}
        }
        for(unsigned variant=0;variant<combined.size();++variant)for(unsigned segment=0;segment<2;++segment){
            constexpr bool receiverMotion=true;
            const auto& samples=combined[variant][segment];const unsigned expected=2*(segment?settle:measured);
            Check(samples.scope[9].size()==(receiverMotion?expected:0),"Receiver-motion GPU sample coverage did not match the symmetric trials");
            if(receiverMotion)Check(samples.scope[9].size()>=32,"Receiver-motion GPU cost needs at least 32 fresh executed samples per segment");
            report(samples,view,variant,segment,-1);}
        // Reproduce the exact pose/committed-seed sequence independently of the
        // timestamp trials. The diagnostic shader executes the same gather
        // loop and counts each attempted ray; it is not a hardware counter of
        // the preceding production draw, nor a CPU classification estimate.
        struct Budget {std::uint64_t pixels=0,valid=0,classified=0,selected=0,traced=0,rays=0,extra=0;
            std::uint64_t movingTagged=0,movingSelected=0,movingTraced=0,geometryExtra=0,movingExtra=0;
            std::uint64_t outsideMovingSelected=0,outsideMovingRays=0,outsideMovingExtra=0;};
        std::array<std::array<Budget,2>,2> diagnosticBudget{};std::vector<std::uint64_t> baselineGeometryClassification;
        for(unsigned variant=0;variant<2;++variant){gi.contact.disocclusionSampling=shadowMode||variant==1;gi.temporal.reactiveShadows=shadowMode&&(movingReceiver||variant==1);gi.temporal.reactiveMovingReceivers=!movingReceiver||variant==1;gi.contact.movingReceiverRayMultiplier=movingReceiver&&variant?options.surfaceCacheMovingReceiverRayMultiplier:1;gi.contact.movingReceiverFullCurrent=movingReceiver&&variant&&options.surfaceCacheMovingReceiverFullCurrent;gi.contact.collectSamplingDiagnostics=false;
            frame.camera=base;frame.screenProbeStudyState.occluderOffset=0;reset();
            for(unsigned f=0;f<warm+measured+settle;++f){unsigned step=0;const bool moving=f>=warm&&f<warm+measured;
                if(moving){const unsigned cycle=(f-warm)%16;step=cycle<8?cycle+1:15-cycle;}
                // Diagnostic collection does not enter lighting/history keys.
                // Warm ordinary production frames once, then replay only the
                // measured trajectory; do not spend 64 static full-image reads.
                if(f==warm)gi.contact.collectSamplingDiagnostics=true;
                frame.camera=base;frame.screenProbeStudyState.occluderOffset=view==2?.15f*float(step):0;
                if(view<2){frame.camera->position+=right*(translation*float(step));
                    const auto direction=glm::vec3(glm::rotate(glm::mat4(1),yaw*float(step),glm::vec3(0,1,0))*glm::vec4(forward,0));
                    frame.camera->view=glm::lookAt(frame.camera->position,frame.camera->position+direction,glm::vec3(0,1,0));}
                render();if(f<warm)continue;
                const auto replay=lab.ReadbackSurfaceCacheReceiverSampling();const auto classification=lab.ReadbackSurfaceCacheFreshSampling();
                const auto contactMask=lab.ReadbackSurfaceCacheContactMask();
                const auto pixels=std::size_t(frame.width)*frame.height;
                Check(replay.size()==pixels&&contactMask.size()==pixels&&lab.PipelineStatistics().surfaceCacheReceiverSamplingCollected,
                    "Missing independent GPU receiver-loop budget replay");
                Check(classification.empty()||((shadowMode||variant==1)&&classification.size()==pixels),"Fresh budget replay exposed missing/stale classification");
                if(movingReceiver){const auto exposureHash=SurfaceCacheFreshSamplingGeometryHash(classification);
                    if(!variant)baselineGeometryClassification.push_back(exposureHash);
                    else Check(baselineGeometryClassification[f-warm]==exposureHash,"Timed trajectory replay changed geometric exposure X/Y/W or geometry bit");}
                Budget current;current.pixels=pixels;
                for(std::size_t i=0;i<pixels;++i){const auto d=replay[i];for(unsigned c=0;c<4;++c)
                        Check(std::isfinite(d[c]),"Nonfinite independent GPU receiver sampling diagnostic");
                    const int rays=int(std::lround(d.y)),readable=int(std::lround(d.w));const bool selected=d.x>.5f;
                    Check(d.x>=0&&d.x<=1&&d.z==8&&rays>=0&&rays<=32&&rays%8==0&&d.y==float(rays)&&
                        readable>=0&&readable<=rays&&d.w==float(readable),"Invalid actual-N/readable count from the GPU gather-loop replay");
                    const auto classified=classification.empty()?glm::vec4(0,8,0,0):classification[i];
                    Check(std::isfinite(contactMask[i].x),"Nonfinite original contact mask in receiver-budget replay");
                    const auto plan=SurfaceCacheFreshSamplingPlan(classified,contactMask[i].x,8,gi.contact.movingReceiverRayMultiplier,gi.contact.movingReceiverFullCurrent);
                    Check(rays==0||unsigned(rays)==plan.total,"Replay actual N did not equal the maximum geometric/moving current budget");
                    if(selected){Check((shadowMode||variant==1)&&!classification.empty()&&classified.x>0&&classified.w>.5f,
                            "Fresh replay selection disagreed with its GPU classifier");++current.selected;}
                    else Check(classified.x<=.000001f,"Receiver replay omitted geometric exposure selection");
                    current.movingTagged+=plan.movingTagged;current.movingSelected+=plan.movingEligible;current.movingTraced+=plan.movingEligible&&rays>0;
                    current.geometryExtra+=rays>0?plan.geometry-8:0;current.movingExtra+=unsigned(rays)>plan.geometry?unsigned(rays)-plan.geometry:0;
                    current.outsideMovingSelected+=plan.outsideRisk;current.outsideMovingRays+=plan.outsideRisk?unsigned(rays):0;
                    current.outsideMovingExtra+=plan.outsideRisk&&unsigned(rays)>plan.geometry?unsigned(rays)-plan.geometry:0;
                    current.rays+=unsigned(rays);current.extra+=rays>8?unsigned(rays-8):0;current.traced+=rays>0;
                    if(!classification.empty()){for(unsigned c=0;c<4;++c)Check(std::isfinite(classification[i][c]),"Nonfinite fresh budget classifier");
                        const int ageTag=int(std::lround(classification[i].w));
                        Check(classification[i].w==float(ageTag)&&(ageTag==0||(ageTag>=1&&ageTag<=7&&ageTag%2==1)),
                            "GPU fresh replay returned an invalid independent exposure-age tag");
                        current.valid+=classification[i].w>.5f;current.classified+=classification[i].x>0;}}
                auto& total=diagnosticBudget[variant][moving?0:1];total.pixels+=current.pixels;total.valid+=current.valid;total.classified+=current.classified;
                total.selected+=current.selected;total.traced+=current.traced;total.rays+=current.rays;total.extra+=current.extra;
                total.movingTagged+=current.movingTagged;total.movingSelected+=current.movingSelected;total.movingTraced+=current.movingTraced;
                total.geometryExtra+=current.geometryExtra;total.movingExtra+=current.movingExtra;
                total.outsideMovingSelected+=current.outsideMovingSelected;total.outsideMovingRays+=current.outsideMovingRays;total.outsideMovingExtra+=current.outsideMovingExtra;
                Check(current.extra==current.geometryExtra+current.movingExtra,"Replay geometric/moving extra-ray accounting lost or duplicated work");}
            gi.contact.collectSamplingDiagnostics=false;
        }
        for(unsigned variant=0;variant<2;++variant)for(unsigned segment=0;segment<2;++segment){const auto& b=diagnosticBudget[variant][segment];
            std::cout<<"FRESH SAMPLING PERF budget view="<<viewNames[view]<<" variant="<<names[variant]<<" segment="<<(segment?"stopping16":"moving")
                <<" frames="<<(segment?settle:measured)<<" pixel_frames="<<b.pixels<<" valid_geometry_pixel_frames="<<b.valid
                <<" classifier_pixel_frames="<<b.classified<<" selected_pixel_frames="<<b.selected<<" selected_fraction="<<double(b.selected)/double(std::max<std::uint64_t>(1,b.pixels))
                <<" traced_receivers="<<b.traced<<" actual_receiver_rays="<<b.rays<<" extra_receiver_rays="<<b.extra
                <<" moving_tagged_pixel_frames="<<b.movingTagged<<" moving_selected_pixel_frames="<<b.movingSelected<<" moving_traced_receivers="<<b.movingTraced
                <<" geometric_extra_receiver_rays="<<b.geometryExtra<<" moving_incremental_extra_receiver_rays="<<b.movingExtra
                <<" mask_outside_moving_selected="<<b.outsideMovingSelected<<" mask_outside_moving_rays="<<b.outsideMovingRays<<" mask_outside_moving_incremental_extra_rays="<<b.outsideMovingExtra
                <<" moving_receiver_multiplier="<<(movingReceiver&&variant?options.surfaceCacheMovingReceiverRayMultiplier:1)<<" full_mode="<<(movingReceiver&&variant&&options.surfaceCacheMovingReceiverFullCurrent)<<" extra_attribution=geometry_first_then_incremental_moving"
                <<" sampling_source=gpu_same_loop_diagnostic_replay hardware_counter=0 timed=0 cpu_budget_guess=0"<<std::endl;}
        if(options.surfaceCacheFreshSamplingMultiplier>1)Check(diagnosticBudget[1][0].selected>0&&diagnosticBudget[1][0].geometryExtra>0,"Fresh performance motion sequence did not exercise extra geometric exposure rays");
        if(movingReceiver&&options.surfaceCacheMovingReceiverRayMultiplier>1)Check(diagnosticBudget[1][0].movingSelected>0&&diagnosticBudget[1][0].movingExtra>0,
            "Moving receiver performance replay never exercised independent extra current rays");
        if(movingReceiver&&options.surfaceCacheMovingReceiverFullCurrent&&options.surfaceCacheMovingReceiverRayMultiplier>1)
            Check(diagnosticBudget[1][0].outsideMovingSelected>0&&diagnosticBudget[1][0].outsideMovingExtra>0,"Full-current performance replay never traced outside its original risk mask");
        std::cout<<"FRESH SAMPLING PERF PASS view="<<viewNames[view]<<" timed_frames="<<order.size()*(measured+settle)<<" static_warm_frames="<<order.size()*warm
            <<" trials_per_variant=2 cache_work_exact=1 geometric_exposure_exact_frames="<<(movingReceiver?baselineGeometryClassification.size():0)<<" timed_image_readbacks=0 untimed_gpu_budget_replay=1 cpu_oracle_samples=0 no_interactive_fps_claim=1"<<std::endl;
    }
}

// Actual renderer captures are separate from timing and budget diagnostics.
void SurfaceCacheFreshSamplingPreview(MaterialLabModule& lab,const Options& options) {
    const bool shadowMode=options.surfaceCacheShadowHistoryPreview;
    const bool movingReceiver=shadowMode&&options.surfaceCacheMovingReceiverTest;
    Check(options.surfaceCacheMovingReceiverRayMultiplier==1||options.surfaceCacheMovingReceiverRayMultiplier==2||options.surfaceCacheMovingReceiverRayMultiplier==4,"Moving receiver ray multiplier must be 1, 2 or 4");
    Check(!options.screenshot.empty(),"Fresh sampling preview requires --screenshot with an output PNG prefix");
    Check(options.vulkan&&options.softwareRays,"Fresh sampling preview requires Vulkan software rays");
    Check(options.surfaceCacheFreshSamplingMultiplier>=1&&options.surfaceCacheFreshSamplingMultiplier<=4,"Fresh sampling multiplier must be 1..4");
    auto fixture=options;fixture.sealedGiRoom=true;fixture.sdfBanquet=false;fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);frame.width=options.renderSizeSet?options.benchmarkWidth:1280;
    frame.height=options.renderSizeSet?options.benchmarkHeight:800;frame.effects.outputSize={0,0};frame.present=false;frame.screenProbeStudyHud=false;
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::None;
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;auto& trace=placement.trace;
    gi.enabled=true;gi.useScreenProbes=true;gi.view=0;gi.reconstructionDebug=0;
    gi.probeDiagnostics=false;gi.probeOriginDiagnostics=false;gi.collectProbeCoverage=false;
    gi.safeProbeOrigins=true;gi.rasterSourcePositions=true;gi.directionalReconstruction=true;gi.cacheBilinear=true;
    gi.spatial={};gi.temporal={};gi.contact={};gi.temporal.staticConvergence=true;gi.temporal.retainHistoryOnObjectMotion=true;
    gi.temporal.motionConsistentSpatialReuse=false;gi.temporal.localCameraHistory=false;gi.contact.referenceReceiver=false;gi.contact.raysPerPixel=8;
    placement.enabled=true;placement.adaptive=true;placement.cornerRefinement=true;placement.curvatureRefinement=true;
    placement.curvatureAwareWeights=true;placement.continuousInterpolation=true;placement.baseSpacing=32;
    placement.freeze=false;placement.frameIndex=0;placement.candidateCount=8;
    trace.enabled=true;trace.cosineHemisphere=true;trace.singleInvocation=false;trace.rayResolution=8;trace.samplingSeed=0;
    trace.maxSteps=512;trace.sdf.enabled=true;trace.sdf.maxSteps=256;
    const unsigned view=options.surfaceCacheDynamicView<3?options.surfaceCacheDynamicView:shadowMode?2:1;
    constexpr std::array<const char*,3> views{{"oval_close","contact_near","cube_fixed_camera"}};
    const std::array<const char*,2> names{{movingReceiver?"rigid_receiver_legacy":shadowMode?"fresh_current_rays":"current_object_stability",movingReceiver?"reactive_moving_receiver":shadowMode?"reactive_shadow_history":"fresh_current_rays"}};
    const char* backend=options.vulkan?"Vulkan":"OpenGL";
    auto base=SurfaceCacheFreshSamplingCamera(view,frame.OutputAspect(),movingReceiver);
    const bool holdShadows=shadowMode&&view==2&&!options.surfaceCacheShadowHistoryContinuous;
    const auto inverse=glm::inverse(base.view);const auto right=glm::normalize(glm::vec3(inverse[0])),forward=glm::normalize(-glm::vec3(inverse[2]));
    const float scale=options.surfaceCacheDynamicFast?3.f:1.f;
    const float translation=(view==1?.015f:.0125f)*scale,yaw=(view==1?.003f:.0025f)*scale;
    std::array<Render::PipelineCamera,9> poses;
    for(unsigned step=0;step<poses.size();++step){auto camera=base;
        if(view<2){camera.position+=right*(translation*float(step));
            const auto direction=glm::vec3(glm::rotate(glm::mat4(1),yaw*float(step),glm::vec3(0,1,0))*glm::vec4(forward,0));
            camera.view=glm::lookAt(camera.position,camera.position+direction,glm::vec3(0,1,0));}poses[step]=camera;}
    const auto reset=[&]{frame.effects.cameraCut=true;placement.reset=true;gi.temporal.reset=true;};
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;placement.reset=false;gi.temporal.reset=false;};
    const auto configure=[&](unsigned variant){gi.enabled=true;placement.enabled=true;gi.temporal.motionStability=true;
        gi.temporal.motionSpatialFilter=false;gi.temporal.motionDisocclusionRecovery=false;
        gi.temporal.objectMotionReprojection=true;gi.temporal.disocclusionFilter=false;
        gi.contact.disocclusionSampling=shadowMode||variant==1;gi.temporal.reactiveShadows=shadowMode&&(movingReceiver||variant==1);gi.temporal.reactiveMovingReceivers=!movingReceiver||variant==1;gi.contact.movingReceiverRayMultiplier=movingReceiver&&variant?options.surfaceCacheMovingReceiverRayMultiplier:1;gi.contact.movingReceiverFullCurrent=movingReceiver&&variant&&options.surfaceCacheMovingReceiverFullCurrent;gi.contact.disocclusionRayMultiplier=options.surfaceCacheFreshSamplingMultiplier;
        gi.contact.collectSamplingDiagnostics=false;};
    configure(0);
    frame.camera=poses[0];frame.screenProbeStudyState.occluderOffset=0;reset();render();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
    while(lab.ProbeSdfQualityStatistics().pending){render();Check(std::chrono::steady_clock::now()<deadline,"Fresh sampling preview SDF preparation timed out");}
    std::vector<std::uint8_t> staticPixels;unsigned images=0,stageImages=0,directImages=0;
    const auto saveStage=[&](const auto& values,unsigned variant,unsigned f,const char* stage){
        const auto pixels=std::size_t(frame.width)*frame.height;
        Check(values.size()==pixels,std::string("Missing full-resolution moving receiver preview stage ")+stage+": "+lab.LastError());
        FramePixels image;image.width=frame.width;image.height=frame.height;image.rgba.resize(pixels*4);
        for(std::size_t i=0;i<pixels;++i){for(unsigned c=0;c<4;++c)Check(std::isfinite(values[i][c]),"Nonfinite moving receiver preview stage");
            for(unsigned c=0;c<3;++c){double value=4*std::max(0.,double(values[i][c]));value=value/(1+value);
                value=value<=.0031308?12.92*value:1.055*std::pow(value,1/2.4)-.055;
                image.rgba[4*i+c]=std::uint8_t(std::lround(std::clamp(value,0.,1.)*255));}image.rgba[4*i+3]=255;}
        auto path=std::filesystem::path(options.screenshot);
        path.replace_filename(path.stem().string()+"_"+backend+"_"+views[view]+"_"+names[variant]+"_frame"+std::to_string(f)+"_"+stage+"_tone4.png");
        Save(image,path.string());++stageImages;
        std::cout<<"FRESH SAMPLING PREVIEW stage_image backend="<<backend<<" variant="<<names[variant]<<" frame="<<f<<" stage="<<stage
            <<" mapping=linear_times4_reinhard_srgb screen_target=1 path=\""<<path.string()<<"\""<<std::endl;
    };
    std::cout<<"FRESH SAMPLING PREVIEW setup backend="<<backend<<" size="<<frame.width<<'x'<<frame.height<<" view="<<views[view]
        <<" variants="<<names[0]<<','<<names[1]<<" shadow_history_preview="<<shadowMode<<" moving_receiver_fixture="<<movingReceiver<<" moving_receiver_candidate_multiplier="<<options.surfaceCacheMovingReceiverRayMultiplier<<" moving_receiver_full_mode="<<options.surfaceCacheMovingReceiverFullCurrent
        <<" fresh_sampling_multiplier="<<options.surfaceCacheFreshSamplingMultiplier<<" warm=64 moving="<<(holdShadows?0:32)<<" stop="<<(holdShadows?0:16)
        <<" laps="<<(holdShadows?0:2)<<" separated_hold_frames="<<(holdShadows?16:0)<<" returned_hold_frames="<<(holdShadows?32:0)<<" probe_rays=64 receiver_rays=8 spacing=32"
        <<" camera_step_metres="<<(view==2?0:translation)<<" yaw_step_radians="<<(view==2?0:yaw)
        <<" object_step_metres="<<(view==2?(holdShadows?1.2f:.15f):0)<<" hold_after_separation="<<holdShadows
        <<" sequence_frames="<<(holdShadows?"65,66,68,72,80,81,82,84,88,96,104,112":"81..88")<<" present=0"
        <<" cpu_oracle_samples=0 atlas_readbacks=0 timing_measurements=0 no_fps_claim=1"<<std::endl;
    for(unsigned variant=0;variant<names.size();++variant){configure(variant);
        frame.camera=poses[0];frame.screenProbeStudyState.occluderOffset=0;reset();unsigned historyFrames=0,variantImages=0,variantStageImages=0,variantDirectImages=0;
        for(unsigned f=1;f<=112;++f){unsigned pose=0;
            if(f>64&&f<=96){const unsigned cycle=(f-65)%16;pose=cycle<8?cycle+1:15-cycle;}
            if(holdShadows&&f>64)pose=f<=80?8:0;
            frame.camera=poses[pose];frame.screenProbeStudyState.occluderOffset=view==2?.15f*float(pose):0;
            render();const auto stats=lab.PipelineStatistics();
            Check(!lab.ProbeSdfQualityStatistics().pending&&stats.surfaceCacheContactCollected&&stats.surfaceCacheSpatialPasses>0&&
                stats.surfaceCacheTemporalPasses>0,"Fresh sampling preview lost prepared full production GI");
            Check(stats.surfaceCacheHistoryUsed==(f>1),"Fresh sampling preview reset or lost compatible history");historyFrames+=stats.surfaceCacheHistoryUsed;
            // Frame 88 is shared by the selected still and consecutive sequence.
            const bool capture=holdShadows?
                (f==64||f==65||f==66||f==68||f==72||f==80||f==81||f==82||f==84||f==88||f==96||f==104||f==112):
                (f==64||f==72||f==88||f==97||f==104||f==112||(f>=81&&f<=88));
            if(capture){const auto pixels=lab.Readback();
                Check(pixels.width==frame.width&&pixels.height==frame.height&&pixels.rgba.size()==std::size_t(frame.width)*frame.height*4,
                    std::string("Missing full-resolution fresh sampling preview: ")+lab.LastError());
                if(f==64){if(variant==0)staticPixels=pixels.rgba;
                    else Check(pixels.rgba==staticPixels,"Fresh sampling preview changed the static frame-64 renderer output");}
                auto path=std::filesystem::path(options.screenshot);
                path.replace_filename(path.stem().string()+"_"+backend+"_"+views[view]+"_"+names[variant]+"_frame"+std::to_string(f)+"_total_renderer_ldr.png");
                Save(pixels,path.string());++images;++variantImages;
                std::cout<<"FRESH SAMPLING PREVIEW image backend="<<backend<<" variant="<<names[variant]<<" frame="<<f<<" pose="<<pose
                    <<" path=\""<<path.string()<<"\""<<std::endl;
                if(movingReceiver&&(f==81||f==84||f==88)){
                    saveStage(lab.ReadbackSurfaceCacheSpatialGi(),variant,f,"probe_spatial");
                    saveStage(lab.ReadbackSurfaceCacheContactGi(),variant,f,"contact_filtered");
                    saveStage(lab.ReadbackSurfaceCacheTemporalGi(),variant,f,"temporal");variantStageImages+=3;
                }
            }
        }
        // Capture current direct lighting only after the complete GI sequence.
        // These frames cannot feed history used by the measured/captured path;
        // the following variant restores GI and performs its own reset/warmup.
        if(movingReceiver){const bool savedPlacementEnabled=placement.enabled;gi.enabled=false;placement.enabled=false;
            for(unsigned pose:std::array<unsigned,3>{{1,4,8}}){frame.camera=poses[pose];frame.screenProbeStudyState.occluderOffset=.15f*float(pose);render();
                const auto stats=lab.PipelineStatistics();
                Check(stats.surfaceCacheProbeRayBudget==0,"Direct-only moving receiver preview traced Surface Cache GI rays");
                const auto pixels=lab.Readback();
                Check(pixels.width==frame.width&&pixels.height==frame.height&&pixels.rgba.size()==std::size_t(frame.width)*frame.height*4,
                    std::string("Missing direct-only moving receiver preview: ")+lab.LastError());
                auto path=std::filesystem::path(options.screenshot);
                path.replace_filename(path.stem().string()+"_"+backend+"_"+views[view]+"_"+names[variant]+"_direct_only_pose"+std::to_string(pose)+"_total_renderer_ldr.png");
                Save(pixels,path.string());++directImages;++variantDirectImages;
                std::cout<<"FRESH SAMPLING PREVIEW direct_image backend="<<backend<<" variant="<<names[variant]<<" pose="<<pose
                    <<" surface_cache_rays="<<stats.surfaceCacheProbeRayBudget<<" gi_enabled=0 probe_placement_enabled=0 captured_after_sequence=1 path=\""<<path.string()<<"\""<<std::endl;
            }
            placement.enabled=savedPlacementEnabled;
        }
        Check(variantDirectImages==(movingReceiver?3u:0u),"Moving receiver preview did not capture all direct-only poses");
        Check(variantStageImages==(movingReceiver?9u:0u),"Moving receiver preview did not capture all current/temporal stage images");
        Check(variantImages==13,"Fresh sampling preview did not capture its complete still/sequence set");
        std::cout<<"FRESH SAMPLING PREVIEW variant="<<names[variant]<<" frames=112 history_used_frames="<<historyFrames
            <<" motion_stability="<<gi.temporal.motionStability<<" motion_spatial_filter="<<gi.temporal.motionSpatialFilter
            <<" motion_disocclusion_recovery="<<gi.temporal.motionDisocclusionRecovery
            <<" object_motion_reprojection="<<gi.temporal.objectMotionReprojection<<" disocclusion_filter="<<gi.temporal.disocclusionFilter
            <<" reactive_shadows="<<gi.temporal.reactiveShadows<<" reactive_moving_receivers="<<gi.temporal.reactiveMovingReceivers<<" moving_receiver_multiplier="<<gi.contact.movingReceiverRayMultiplier<<" full_mode="<<gi.contact.movingReceiverFullCurrent<<" disocclusion_sampling="<<gi.contact.disocclusionSampling<<" multiplier="<<gi.contact.disocclusionRayMultiplier
            <<" renderer_readbacks="<<variantImages<<" stage_image_readbacks="<<variantStageImages<<" direct_image_readbacks="<<variantDirectImages<<" static_frame64_exact=1"<<std::endl;
    }
    Check(images==26,"Fresh sampling preview did not capture both variants");
    Check(stageImages==(movingReceiver?18u:0u),"Moving receiver preview lost its independent stage image count");
    Check(directImages==(movingReceiver?6u:0u),"Moving receiver preview lost its independent direct-only image count");
    std::cout<<"FRESH SAMPLING PREVIEW PASS backend="<<backend<<" renderer_readbacks="<<images<<" stage_image_readbacks="<<stageImages<<" direct_image_readbacks="<<directImages
        <<" atlas_readbacks=0 cpu_oracle_samples=0 timing_measurements=0 no_fps_claim=1"<<std::endl;
}
