// Independent GI95 coverage of the same six-prop animation used by H.
// The historical GI90/GI94 paths, references and acceptance gates are untouched.
namespace SurfaceCacheAllObjectMotionFixture {
using namespace SurfaceCacheBudgetFixture;
constexpr double StepSeconds=1./60.;
constexpr unsigned MotionFrames=96,ReferenceFrames=64;
using Objects=std::vector<ProbePlacementObject>;
constexpr std::array<const char*,2> ViewNames{{"alias_front","alias_rear"}};
Render::PipelineCamera MotionCamera(unsigned view,float aspect) {
    Check(view<ViewNames.size(),"GI95 artifact camera is out of range");
    constexpr std::array<glm::vec3,2> eyes{{{-2.9f,2.7f,-3.2f},{3.05f,2.8f,-3.1f}}};
    constexpr std::array<glm::vec3,2> targets{{{-.4f,.65f,-.1f},{0,.9f,0}}};
    auto camera=SealedGiRoomCameraPreset(0,aspect);camera.position=eyes[view];
    camera.nearPlane=.04f;camera.farPlane=24;
    camera.view=glm::lookAt(camera.position,targets[view],glm::vec3(0,1,0));
    camera.projection=glm::perspective(glm::radians(55.f),aspect,camera.nearPlane,camera.farPlane);
    return camera;
}

FrameSettings MakeMotionFrame(const Options& options) {
    auto frame=Make(options,21,false);
    auto objects=SealedGiRoomObjects();SealedRoomMotion::Sync(frame,objects);
    Check(frame.sealedGiRoomStudy&&frame.sealedGiRoomPropMotion&&!frame.probeSceneOverride,
        "GI95 requires stable sealed-room identity and the public instance-transform palette");
    return frame;
}
void VerifyPose(const FrameSettings& frame,const Objects& objects) {
    const auto& initial=SealedGiRoomObjects();
    Check(objects.size()==initial.size()&&frame.sealedGiRoomPropMotion&&frame.screenProbeStudyState.occluderOffset==0,
        "GI95 changed the scene identity or legacy single-cube offset");
    for(unsigned index=0;index<objects.size();++index) {
        const auto& object=objects[index];const auto& base=initial[index];
        Check(object.position.y==base.position.y&&object.scale==base.scale&&object.yaw==base.yaw&&
            object.tilt==base.tilt&&object.tiltAxis==base.tiltAxis,"GI95 ground animation changed object height or shape");
        if(!SealedRoomMotion::IsProp(index))
            Check(object.position==base.position,"GI95 moved room architecture or the ceiling lamp");
        else Check(frame.sealedGiRoomPropOffsets[index-SealedRoomMotion::FirstProp]==object.position-base.position,
            "GI95 transform palette differs from the interactive H pose");
        const auto actual=SealedGiRoomObjectTransform(index,frame.screenProbeStudyState,frame.sealedGiRoomPropOffsets);
        auto expectedObject=object;expectedObject.movable=false;
        const auto expected=ProbePlacementTransform(expectedObject,{});
        for(unsigned column=0;column<4;++column)
            Check(glm::length(actual[column]-expected[column])<1e-6f,"GI95 public transform does not match its requested pose");
    }
}
void Advance(FrameSettings& frame,Objects& objects,unsigned step) {
    const auto previous=objects;
    SealedRoomMotion::Advance(objects,double(step)*StepSeconds,double(step+1)*StepSeconds);
    SealedRoomMotion::Sync(frame,objects);VerifyPose(frame,objects);
    for(unsigned index=SealedRoomMotion::FirstProp;index<objects.size();++index)
        Check(glm::length(objects[index].position-previous[index].position)>1e-8f,
            "GI95 failed to continuously move every porcelain prop");
}
void VerifyProducer(MaterialLabModule& lab) {
    const auto& stats=lab.PipelineStatistics();
    Check(stats.surfaceCacheUeLumenHigh&&stats.surfaceCacheNativeScreenProbeTransport&&
        stats.probeSdfHardwareRayTracing&&!stats.probeSdfDirectSoftwareRefinement&&stats.probeSdfNativeUploadsPruned&&
        stats.surfaceCacheHardwareOrigins&&stats.surfaceCacheProbeOriginsCollected&&
        stats.screenProbeSinglePassOutputs&&stats.screenProbeHzbLevels>0&&stats.surfaceCacheProbeRayBudget>0,
        "GI95 lost the intended UE High native screen-probe/HZB producer");
}
void Performance(MaterialLabModule& lab,const Options& options) {
#ifndef NDEBUG
    Check(false,"GI95 motion performance requires a Release NDEBUG executable");
#endif
    Check(options.vulkan&&!options.softwareRays&&!options.validation&&options.gi90Profile==21&&options.benchmarkPresent,
        "GI95 motion timing requires native Vulkan Release, profile21 and --benchmark-present");
    auto frame=MakeMotionFrame(options);auto objects=SealedGiRoomObjects();
    Check(frame.width==1280&&frame.height==800,"GI95 motion timing requires 1280x800 internal rendering");
    const auto n=options.surfaceCacheDynamicFrames;
    std::cout<<"GI95 SETUP build=Release_NDEBUG scene=sealed_gi_room path=all_props moving_ids=19,20,21,22,23,24"
        <<" static_ids=0..18 internal=1280x800 present=1 epochs=2 measured="<<n
        <<" static_warm=64 motion_warm=16 fixed_step_seconds="<<StepSeconds
        <<" motion_continuous_across_epochs=1 readbacks_in_timing=0 frame_generation=0 target_fps=90"
        <<" original_quality_gates_unchanged=1"<<std::endl;
    frame.effects.surfaceCacheGi.contact.collectTraceTimings=true;
    Reset(frame);for(unsigned f=0;f<64;++f)Draw(lab,frame);
    for(unsigned f=0;f<16;++f){Advance(frame,objects,f);Draw(lab,frame);}
    unsigned motionStep=16;std::vector<double> gpuSamples,loopSamples;
    for(unsigned epoch=0;epoch<2;++epoch)for(unsigned f=0;f<n;++f,++motionStep) {
        Advance(frame,objects,motionStep);
        const auto start=std::chrono::steady_clock::now();glfwPollEvents();const auto t=lab.MeasureFrames(frame,1,0);
        const double loop=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        frame.effects.cameraCut=false;frame.effects.screenProbePlacement.reset=false;frame.effects.surfaceCacheGi.temporal.reset=false;
        Check(t.gpuFrameMilliseconds.size()==1&&t.backendHostFrameStatistics.size()==1&&t.uniformSnapshots.size()==1,
            "GI95 missing completed-frame timing");VerifyProducer(lab);
        const double gpu=t.gpuFrameMilliseconds.front();
        Check(std::isfinite(gpu)&&gpu>0&&std::isfinite(loop)&&loop>0,"GI95 invalid completed-frame timing");
        gpuSamples.push_back(gpu);loopSamples.push_back(loop);
        const auto& stats=lab.PipelineStatistics();const auto& sceneStats=lab.PreparedProbeSdfScene()->Statistics();
        std::cout<<"GI95 SAMPLE profile=21 path=all_props epoch="<<epoch<<" frame="<<f<<" motion_frame="<<motionStep
            <<" seconds="<<double(motionStep+1)*StepSeconds<<" moving_props=6 static_objects=19 pose_verified=1"
            <<" loop="<<loop<<" gpu="<<gpu<<" present="<<frame.present
            <<" capacity="<<stats.surfaceCacheProbeRayBudget<<" ue_lumen_high="<<stats.surfaceCacheUeLumenHigh
            <<" sdf_cpu_voxels="<<sceneStats.updatedVoxels<<" geometry_transform="<<sceneStats.geometryTransformMilliseconds
            <<" geometry_snapshot="<<sceneStats.geometrySnapshotMilliseconds<<" geometry_refit="<<sceneStats.geometryRefitMilliseconds
            <<" card_lighting_updated_texels="<<stats.surfaceCacheUpdatedTexels
            <<" card_lighting_written_texels="<<stats.surfaceCacheLightingWrittenTexels
            <<" receiver_motion_draws="<<stats.surfaceCacheReceiverMotionDraws
            <<" receiver_motion_static_owners="<<stats.surfaceCacheReceiverMotionStaticOwners;
        const auto scopes=Scopes(t);for(unsigned at=1;at<ScopeCount;++at)
            if(!scopes[at]->empty())std::cout<<' '<<ScopeNames[at]<<'='<<scopes[at]->front();
        if(!t.ueRadianceCacheFrameMilliseconds.empty())std::cout<<" ue_radiance_cache="<<t.ueRadianceCacheFrameMilliseconds.front();
        if(!t.ueProbeImportanceFrameMilliseconds.empty())std::cout<<" ue_probe_importance="<<t.ueProbeImportanceFrameMilliseconds.front();
        Check(t.architectureFrameMilliseconds.size()==1,"GI95 missing completed-frame architecture timings");
        const auto cachedDirect=t.architectureFrameMilliseconds.front()[6];
        if(std::isfinite(cachedDirect))std::cout<<" cached_direct="<<cachedDirect;
        for(unsigned at=0;at<HostCounterNames.size();++at)
            std::cout<<' '<<HostCounterNames[at]<<'='<<t.backendHostFrameStatistics.front()[at];
        if(!t.backendHostFrameMilliseconds.empty())std::cout<<" as_prepare="<<t.backendHostFrameMilliseconds.front()[5]
            <<" as_pack="<<t.backendHostFrameMilliseconds.front()[6]<<" command_encode="<<t.backendHostFrameMilliseconds.front()[1]
            <<" fence="<<t.backendHostFrameMilliseconds.front()[9];
        std::cout<<" prepare="<<t.scenePreparationFrameMilliseconds.front()<<" record="<<t.frameRecordingFrameMilliseconds.front()<<std::endl;
    }
    std::cout<<"GI95 PERFORMANCE COMPLETE path=all_props completed_samples="<<gpuSamples.size()
        <<" continuous_motion_frames="<<motionStep<<" moving_props=6 static_objects=19 pose_verified=1"
        <<" gpu_mean_ms="<<Mean(gpuSamples)<<" gpu_p95_ms="<<Quantile(gpuSamples,.95)
        <<" loop_mean_ms="<<Mean(loopSamples)<<" loop_p95_ms="<<Quantile(loopSamples,.95)
        <<" completed_loop_fps="<<1000./Mean(loopSamples)<<" readbacks_in_timing=0 physical_scanout_fps_claim=0"<<std::endl;
}

struct Snapshot {
    FrameSettings frame;
    std::vector<glm::vec4> gi,history,positions,normals,beforeAo,direct;
    std::vector<std::size_t> pixels;
    std::string label;
};
bool Roi(glm::vec4 position,glm::vec4 normal) {
    if(position.w<=0||normal.w<=0)return false;
    Check(position.w==std::floor(position.w),"GI95 receiver owner is not an integer");
    const auto owner=unsigned(position.w)-1;
    Check(owner<SealedGiRoomObjects().size(),"GI95 receiver owner is out of range");
    return owner==0||SealedRoomMotion::IsProp(owner);
}
Snapshot Capture(MaterialLabModule& lab,const FrameSettings& frame,const std::string& label) {
    Snapshot result;result.frame=frame;result.label=label;result.gi=lab.ReadbackSurfaceCacheTemporalGi();
    result.history=lab.ReadbackSurfaceCacheTemporalHistory();const auto source=lab.ReadbackScreenProbeSources();
    result.positions=source.sourceViewPositions;result.normals=source.sourceWorldNormals;
    const auto receiver=lab.ReadbackSurfaceReceiverCapture();result.direct=lab.ReadbackSurfaceCacheDirectSource();
    const auto count=std::size_t(frame.width)*frame.height;
    Check(result.gi.size()==count&&result.history.size()==count&&result.positions.size()==count&&result.normals.size()==count&&
        receiver.reflectance.size()==count&&result.direct.size()==count,"GI95 artifact stage extent mismatch");
    Finite(result.gi,"GI95 final GI");Finite(result.history,"GI95 history");Finite(result.positions,"GI95 positions");
    Finite(result.normals,"GI95 normals");Finite(receiver.reflectance,"GI95 reflectance");Finite(result.direct,"GI95 cached direct source");
    result.beforeAo.resize(count);
    for(std::size_t i=0;i<count;++i){result.beforeAo[i]=glm::vec4(glm::vec3(result.history[i])*
        glm::max(glm::vec3(receiver.reflectance[i]),glm::vec3(0)),result.gi[i].w);
        if(Roi(result.positions[i],result.normals[i]))result.pixels.push_back(i);}
    Check(!result.pixels.empty(),"GI95 artifact ROI is empty");return result;
}
void SaveSnapshot(MaterialLabModule& lab,const Options& options,const Snapshot& shot,std::ostream& manifest,
    const char* phase,unsigned view,unsigned phaseFrame,bool& first) {
    using namespace SurfaceCacheArtifactFixture;
    const auto& frame=shot.frame;const auto count=shot.pixels.size();
    const auto stride=std::max<std::size_t>(1,(count+FloatRecordLimit-1)/FloatRecordLimit);
    using Record=std::array<glm::vec4,7>;static_assert(sizeof(Record)==28*sizeof(float));
    std::vector<Record> records;std::array<std::uint64_t,25> ownerPixels{};
    records.reserve(std::min<std::size_t>(count,FloatRecordLimit));const auto inverseView=glm::inverse(frame.camera->view);
    for(const auto pixel:shot.pixels)++ownerPixels[unsigned(shot.positions[pixel].w)-1];
    for(std::size_t ordinal=0;ordinal<count;ordinal+=stride){const auto pixel=shot.pixels[ordinal];
        const auto position=shot.positions[pixel];const auto world=inverseView*glm::vec4(glm::vec3(position),1);
        records.push_back({glm::vec4(float(pixel%frame.width),float(pixel/frame.width),position.w,shot.history[pixel].w),
            glm::vec4(glm::vec3(world),1),shot.normals[pixel],shot.gi[pixel],shot.beforeAo[pixel],shot.history[pixel],shot.direct[pixel]});}
    const auto total=OutputPath(options,shot.label+"_total");Save(frame.present?lab.ReadbackDisplay():lab.Readback(),total);
    Tone(options,frame.width,frame.height,shot.gi,shot.label+"_indirect");
    Tone(options,frame.width,frame.height,shot.beforeAo,shot.label+"_before_ao");
    Tone(options,frame.width,frame.height,shot.direct,shot.label+"_direct_source");
    auto binary=std::filesystem::path(OutputPath(options,shot.label));binary.replace_extension(".f32");
    const std::array<std::uint32_t,8> header{{0x31414947u,1u,frame.width,frame.height,std::uint32_t(records.size()),28u,std::uint32_t(count),0u}};
    std::ofstream stream(binary,std::ios::binary);stream.write(reinterpret_cast<const char*>(header.data()),sizeof(header));
    stream.write(reinterpret_cast<const char*>(records.data()),std::streamsize(records.size()*sizeof(Record)));
    stream.close();Check(bool(stream),"Cannot write GI95 artifact float records");
    if(!first)manifest<<",\n";first=false;
    manifest<<"{\"view\":"<<view<<",\"view_name\":"<<JsonString(ViewNames[view])
        <<",\"phase\":"<<JsonString(phase)<<",\"phase_frame\":"<<phaseFrame
        <<",\"roi_pixels\":"<<count<<",\"float_records\":"<<records.size()<<",\"float_stride\":"<<stride
        <<",\"float_file\":"<<JsonString(binary.generic_string())<<",\"total_png\":"<<JsonString(std::filesystem::path(total).generic_string());
    for(const auto* suffix:{"indirect","before_ao","direct_source"})manifest<<",\""<<suffix<<"_png\":"
        <<JsonString(std::filesystem::path(OutputPath(options,shot.label+"_"+suffix)).generic_string());
    manifest<<",\"view_matrix_column_major\":";MatrixJson(manifest,frame.camera->view);
    manifest<<",\"projection_column_major\":";MatrixJson(manifest,frame.camera->projection);
    manifest<<",\"prop_offsets\":[";for(unsigned prop=0;prop<frame.sealedGiRoomPropOffsets.size();++prop){if(prop)manifest<<',';
        const auto value=frame.sealedGiRoomPropOffsets[prop];manifest<<'['<<value.x<<','<<value.y<<','<<value.z<<']';}
    manifest<<"],\"owner_pixels\":[";for(unsigned owner=0;owner<ownerPixels.size();++owner){if(owner)manifest<<',';manifest<<ownerPixels[owner];}
    manifest<<"]}";
    std::cout<<"GI95 ARTIFACT SNAPSHOT view="<<view<<" phase="<<phase<<" phase_frame="<<phaseFrame
        <<" roi_pixels="<<count<<" float_records="<<records.size()<<" moving_props=6 static_objects=19 untimed=1"<<std::endl;
}
void CompareReference(const Snapshot& motion,const Snapshot& reference,unsigned view,unsigned phaseFrame) {
    Check(motion.positions==reference.positions&&motion.normals==reference.normals,
        "GI95 frozen-pose reference changed current receiver ownership or geometry");
    std::array<double,25> squared{},energy{};std::array<std::uint64_t,25> counts{};
    for(const auto pixel:motion.pixels){const auto owner=unsigned(motion.positions[pixel].w)-1;
        const auto delta=glm::dvec3(motion.gi[pixel])-glm::dvec3(reference.gi[pixel]);
        squared[owner]+=glm::dot(delta,delta);energy[owner]+=glm::dot(glm::dvec3(reference.gi[pixel]),glm::dvec3(reference.gi[pixel]));++counts[owner];}
    for(unsigned owner=0;owner<counts.size();++owner)if(counts[owner])
        std::cout<<"GI95 FROZEN_REFERENCE view="<<view<<" motion_frame="<<phaseFrame<<" owner="<<owner+1
            <<" pixels="<<counts[owner]<<" same_pose_owner_geometry=1 reference_frames="<<ReferenceFrames
            <<" rms="<<std::sqrt(squared[owner]/double(counts[owner]*3))
            <<" nrmse="<<std::sqrt(squared[owner]/std::max(energy[owner],1e-20))
            <<" diagnostic_only=1 acceptance_threshold=none motion_pixel_difference_is_noise=0"<<std::endl;
}
void Artifacts(MaterialLabModule& lab,const Options& options) {
    using namespace SurfaceCacheArtifactFixture;
    Check(options.vulkan&&!options.softwareRays&&options.gi90Profile==21&&!options.screenshot.empty(),
        "GI95 artifacts require native UE High and --screenshot as an output prefix");
    const auto directory=std::filesystem::path(options.screenshot).parent_path();
    if(!directory.empty())std::filesystem::create_directories(directory);
    std::ostringstream manifest;manifest<<std::setprecision(9);
    manifest<<"{\"format\":\"GI95 all-object motion artifacts v1\",\"diagnostic_only\":true,"
        "\"acceptance_thresholds_changed\":false,\"motion_helper\":\"SealedRoomMotion::Advance+Sync, shared with H\","
        "\"moving_ids_zero_based\":[19,20,21,22,23,24],\"static_ids_zero_based\":[0,18],"
        "\"static_id_encoding\":\"inclusive range\",\"fixed_step_seconds\":"<<StepSeconds
        <<",\"warm_frames\":64,\"static_frames\":8,\"motion_frames\":96,\"settle_frames\":64,\"frozen_reference_frames\":64,"
        "\"float_record_limit\":"<<FloatRecordLimit<<",\"binary_layout\":\"little-endian uint32[8], then float32[records][28]\","
        "\"binary_fields\":\"pixel_x,y,owner,history_count; world_xyz,1; normal_xyzw; final_reflected_gi_rgba; "
        "reflected_gi_before_ao_rgba; unmodulated_history_rgba; cached_direct_source_rgba\","
        "\"direct_source_semantics\":\"production cached diffuse plus primary specular/emissive; excludes GI; not pure direct diffuse\","
        "\"float_sampling\":\"uniform ordinal over all visible floor and prop receivers\",\"snapshots\":[\n";
    bool first=true;
    for(unsigned view=0;view<2;++view) {
        auto frame=MakeMotionFrame(options);frame.present=options.benchmarkPresent;
        frame.camera=MotionCamera(view,frame.OutputAspect());auto objects=SealedGiRoomObjects();VerifyPose(frame,objects);
        Reset(frame);for(unsigned f=0;f<64;++f)Draw(lab,frame);VerifyProducer(lab);
        const auto label=[&](const char* phase,unsigned f){return "gi95_motion_"+std::string(ViewNames[view])+"_"+phase+"_f"+std::to_string(f);};
        std::vector<glm::vec4> staticPositions,staticNormals;std::vector<double> mean,m2;std::vector<std::size_t> noisePixels;
        for(unsigned f=0;f<StaticSamples;++f){Draw(lab,frame);auto shot=Capture(lab,frame,label("static",f));
            if(!f){staticPositions=shot.positions;staticNormals=shot.normals;noisePixels=shot.pixels;mean.resize(noisePixels.size());m2.resize(noisePixels.size());}
            Check(shot.positions==staticPositions&&shot.normals==staticNormals,"GI95 static noise measurement changed receiver pose");
            for(std::size_t p=0;p<noisePixels.size();++p){const double l=glm::dot(glm::dvec3(shot.gi[noisePixels[p]]),glm::dvec3(.2126,.7152,.0722));
                const double delta=l-mean[p];mean[p]+=delta/double(f+1);m2[p]+=delta*(l-mean[p]);}
            if(f==0||f==StaticSamples-1)SaveSnapshot(lab,options,shot,manifest,"static",view,f,first);
        }
        double variance=0,luma=0;for(std::size_t p=0;p<mean.size();++p){luma+=mean[p];variance+=m2[p]/double(StaticSamples-1);}
        std::cout<<"GI95 ARTIFACT STATIC view="<<view<<" samples="<<StaticSamples<<" roi_pixels="<<noisePixels.size()
            <<" mean_luminance="<<luma/double(mean.size())<<" temporal_rms="<<std::sqrt(variance/double(mean.size()))
            <<" same_pose_owner_geometry=1 motion_noise_claim=0 diagnostic_only=1"<<std::endl;
        std::vector<Snapshot> movingShots;constexpr std::array<unsigned,4> checkpoints{{0,31,63,95}};
        for(unsigned f=0;f<MotionFrames;++f){Advance(frame,objects,f);Draw(lab,frame);VerifyProducer(lab);
            if(std::find(checkpoints.begin(),checkpoints.end(),f)!=checkpoints.end()){
                movingShots.push_back(Capture(lab,frame,label("all_props",f)));
                SaveSnapshot(lab,options,movingShots.back(),manifest,"all_props",view,f,first);
                const auto receiverMotion=lab.ReadbackSurfaceReceiverMotion();unsigned movingOwners=0;
                for(unsigned owner=0;owner<objects.size();++owner){const bool moving=receiverMotion.states[owner]>1.5f;
                    Check(moving==SealedRoomMotion::IsProp(owner),"GI95 actual receiver-motion palette omitted a prop or moved room architecture");
                    movingOwners+=moving;}
                std::cout<<"GI95 RECEIVER_MOTION view="<<view<<" motion_frame="<<f<<" moving_owners="<<movingOwners
                    <<" raster_draws="<<receiverMotion.rasterDraws<<" static_owners="<<receiverMotion.staticOwners
                    <<" actual_palette_verified=1 untimed=1"<<std::endl;
                // Later frozen-pose comparisons need only GI and exact current
                // geometry; release the three exported auxiliary image stages.
                movingShots.back().history={};movingShots.back().beforeAo={};movingShots.back().direct={};
            }}
        for(unsigned f=0;f<64;++f)Draw(lab,frame);
        SaveSnapshot(lab,options,Capture(lab,frame,label("paused",63)),manifest,"paused",view,63,first);
        for(unsigned checkpoint=0;checkpoint<movingShots.size();++checkpoint){auto reference=movingShots[checkpoint].frame;
            Reset(reference);for(unsigned f=0;f<ReferenceFrames;++f)Draw(lab,reference);
            const auto shot=Capture(lab,reference,label("frozen_reference",checkpoints[checkpoint]));
            SaveSnapshot(lab,options,shot,manifest,"frozen_reference",view,checkpoints[checkpoint],first);
            CompareReference(movingShots[checkpoint],shot,view,checkpoints[checkpoint]);
        }
    }
    manifest<<"\n]}\n";auto output=std::filesystem::path(options.screenshot);
    output.replace_filename(output.stem().string()+"_motion_artifacts.json");
    std::ofstream stream(output);stream<<manifest.str();stream.close();Check(bool(stream),"Cannot write GI95 motion artifact manifest");
    std::cout<<"GI95 ARTIFACTS COMPLETE views=2 snapshots=22 png=88 float_files=22 moving_props=6 static_objects=19"
        <<" diagnostic_only=1 original_quality_gates_unchanged=1 motion_pixel_difference_is_noise=0"<<std::endl;
}

struct VisibilityAtlasStats {
    std::uint64_t count=0,finite=0,nonfinite=0,captureValid=0,legacy=0,encoded=0,unknown=0,other=0;
    std::uint64_t encodedBlack=0,encodedV0=0,encodedDecodeV0=0,encodedPositive=0,encodedVPositive=0,rgbBlack=0;
    float alphaMin=std::numeric_limits<float>::max(),alphaMax=std::numeric_limits<float>::lowest();
    glm::vec3 rgbMin{std::numeric_limits<float>::max()},rgbMax{std::numeric_limits<float>::lowest()};
    void Add(glm::vec4 value,bool capture=false) {
        ++count;captureValid+=capture;
        for(unsigned c=0;c<4;++c)if(!std::isfinite(value[c])){++nonfinite;return;}
        ++finite;alphaMin=std::min(alphaMin,value.w);alphaMax=std::max(alphaMax,value.w);
        rgbMin=glm::min(rgbMin,glm::vec3(value));rgbMax=glm::max(rgbMax,glm::vec3(value));
        const bool black=glm::vec3(value)==glm::vec3(0);rgbBlack+=black;
        if(value.w<.5f)++unknown;
        else if(value.w<1.5f)++legacy;
        else if(value.w>=2&&value.w<=3){++encoded;encodedBlack+=black;encodedV0+=value.w==2;
            encodedDecodeV0+=black||value.w==2;encodedPositive+=glm::any(glm::greaterThan(glm::vec3(value),glm::vec3(0)));
            encodedVPositive+=value.w>2;}
        else ++other;
    }
    void Json(std::ostream& out,bool atlas=true) const {
        out<<"{\"texels\":"<<count<<",\"finite\":"<<finite<<",\"nonfinite\":"<<nonfinite
            <<",\"rgb_exact_black\":"<<rgbBlack<<",\"alpha_min\":"<<(finite?alphaMin:0)
            <<",\"alpha_max\":"<<(finite?alphaMax:0)<<",\"rgb_min\":[";
        for(unsigned c=0;c<3;++c){if(c)out<<',';out<<(finite?rgbMin[c]:0);}out<<"],\"rgb_max\":[";
        for(unsigned c=0;c<3;++c){if(c)out<<',';out<<(finite?rgbMax[c]:0);}out<<']';
        if(atlas)out<<",\"capture_valid\":"<<captureValid<<",\"legacy_alpha_0p5_to_1p5\":"<<legacy
            <<",\"encoded_alpha_2_to_3\":"<<encoded<<",\"unknown_alpha_lt_0p5\":"<<unknown<<",\"alpha_other\":"<<other
            <<",\"encoded_rgb_exact_black\":"<<encodedBlack<<",\"encoded_alpha_v0\":"<<encodedV0
            <<",\"encoded_decoded_v0\":"<<encodedDecodeV0<<",\"encoded_rgb_positive\":"<<encodedPositive
            <<",\"encoded_alpha_v_positive\":"<<encodedVPositive;
        out<<'}';
    }
};
struct PrimaryFlagsStats {
    std::uint64_t count=0,finite=0,nonfinite=0,invalidMask=0,anchorLegacy=0,anchorEncoded=0,anchorUnknown=0,anchorOther=0,anchorExactZero=0;
    std::uint64_t vZero=0,vPositive=0,vNegative=0,vAboveOne=0,reconstructTrue=0,reconstructNonbinary=0,maskBMismatch=0;
    std::array<std::uint64_t,32> masks{};
    std::array<std::uint64_t,4> positiveVBins{};
    float anchorMin=std::numeric_limits<float>::max(),anchorMax=std::numeric_limits<float>::lowest();
    float vMin=std::numeric_limits<float>::max(),vMax=std::numeric_limits<float>::lowest();
    void Add(glm::vec4 value) {
        ++count;for(unsigned c=0;c<4;++c)if(!std::isfinite(value[c])){++nonfinite;return;}
        ++finite;anchorMin=std::min(anchorMin,value.x);anchorMax=std::max(anchorMax,value.x);
        anchorExactZero+=value.x==0;
        vMin=std::min(vMin,value.y);vMax=std::max(vMax,value.y);
        if(value.x<.5f)++anchorUnknown;else if(value.x<1.5f)++anchorLegacy;
        else if(value.x>=2&&value.x<=3)++anchorEncoded;else ++anchorOther;
        vZero+=value.y==0;vPositive+=value.y>0;vNegative+=value.y<0;vAboveOne+=value.y>1;
        if(value.y>0&&value.y<=1)++positiveVBins[std::min(3u,unsigned(std::ceil(value.y*4))-1)];
        reconstructTrue+=value.z>.5f;reconstructNonbinary+=value.z!=0&&value.z!=1;
        if(value.w<0||value.w>31||value.w!=std::floor(value.w)){++invalidMask;return;}
        const auto mask=unsigned(value.w);++masks[mask];maskBMismatch+=(bool(mask&16)!=(value.z>.5f));
    }
    void Json(std::ostream& out) const {
        out<<"{\"pixels\":"<<count<<",\"finite\":"<<finite<<",\"nonfinite\":"<<nonfinite<<",\"invalid_mask\":"<<invalidMask
            <<",\"anchor_alpha_legacy\":"<<anchorLegacy<<",\"anchor_alpha_encoded\":"<<anchorEncoded
            <<",\"anchor_alpha_unknown_lt_0p5\":"<<anchorUnknown<<",\"anchor_alpha_other\":"<<anchorOther
            <<",\"anchor_alpha_exact_zero\":"<<anchorExactZero
            <<",\"anchor_alpha_min\":"<<(finite?anchorMin:0)<<",\"anchor_alpha_max\":"<<(finite?anchorMax:0)
            <<",\"interpolated_v_min\":"<<(finite?vMin:0)<<",\"interpolated_v_max\":"<<(finite?vMax:0)
            <<",\"interpolated_v_exact_zero\":"<<vZero<<",\"interpolated_v_positive\":"<<vPositive
            <<",\"interpolated_v_negative\":"<<vNegative<<",\"interpolated_v_above_one\":"<<vAboveOne
            <<",\"reconstruct_true\":"<<reconstructTrue<<",\"reconstruct_b_nonbinary\":"<<reconstructNonbinary
            <<",\"reconstruct_mask_b_mismatch\":"<<maskBMismatch<<",\"positive_v_quartile_bins\":[";
        for(unsigned bin=0;bin<positiveVBins.size();++bin){if(bin)out<<',';out<<positiveVBins[bin];}
        out<<"],\"mask_histogram\":[";for(unsigned mask=0;mask<masks.size();++mask){if(mask)out<<',';out<<masks[mask];}out<<"]}";
    }
};
void PrimaryFlagsAudit(MaterialLabModule& lab,const FrameSettings& normal,unsigned view,const char* phase,unsigned phaseFrame,
    const ScreenProbeTraceReadback& normalSources,std::ostream& output) {
    auto diagnostic=normal;diagnostic.effects.surfaceCacheGi.contact.collectSamplingDiagnostics=true;
    Draw(lab,diagnostic);
    const auto flags=lab.ReadbackSurfaceCacheDirectSource();Check(lab.LastError().empty(),lab.LastError());
    const auto sources=lab.ReadbackScreenProbeSources();Check(lab.LastError().empty(),lab.LastError());
    Check(flags.size()==std::size_t(normal.width)*normal.height&&sources.sourceViewPositions.size()==flags.size(),
        "GI95 primary-flags readback extent mismatch");
    const bool geometrySame=sources.sourceViewPositions==normalSources.sourceViewPositions&&
        sources.sourceWorldNormals==normalSources.sourceWorldNormals;
    PrimaryFlagsStats full;std::vector<PrimaryFlagsStats> owners(SealedGiRoomObjects().size());
    for(std::size_t pixel=0;pixel<flags.size();++pixel){full.Add(flags[pixel]);const auto owner=sources.sourceViewPositions[pixel].w;
        if(owner>=1&&owner<=float(owners.size())&&owner==std::floor(owner))owners[unsigned(owner)-1].Add(flags[pixel]);}
    output<<",\"gpu_primary_flags\":{\"diagnostic_mode_requested\":true,\"rgb_is_not_scene_color\":true,"
        "\"rgb_semantics\":\"R actual anchor lighting alpha; G interpolated V; B reconstruct 0/1\","
        "\"alpha_mask_bits\":\"valid 1; encoded 2; singleRectangle 4; rectangleCount==1 8; reconstruct 16\","
        "\"positive_v_bins\":\"(0,.25],(.25,.5],(.5,.75],(.75,1]\",\"same_current_receiver_geometry\":"<<geometrySame
        <<",\"full_frame\":";full.Json(output);output<<",\"objects\":[";
    for(unsigned owner=0;owner<owners.size();++owner){if(owner)output<<',';
        output<<"{\"object_zero_based\":"<<owner<<",\"stats\":";owners[owner].Json(output);output<<'}';
        if(SealedRoomMotion::IsProp(owner))std::cout<<"GI95 PRIMARY FLAGS OBJECT view="<<view<<" phase="<<phase<<" phase_frame="<<phaseFrame
            <<" object="<<owner<<" pixels="<<owners[owner].count<<" anchor_encoded="<<owners[owner].anchorEncoded
            <<" v_zero="<<owners[owner].vZero<<" v_positive="<<owners[owner].vPositive
            <<" reconstruct="<<owners[owner].reconstructTrue<<" mask31="<<owners[owner].masks[31]
            <<" invalid_mask="<<owners[owner].invalidMask<<" b_nonbinary="<<owners[owner].reconstructNonbinary<<" untimed=1"<<std::endl;
    }
    output<<"]}";
    std::cout<<"GI95 PRIMARY FLAGS view="<<view<<" phase="<<phase<<" phase_frame="<<phaseFrame<<" pixels="<<full.count
        <<" reconstruct="<<full.reconstructTrue<<" mask31="<<full.masks[31]<<" anchor_encoded="<<full.anchorEncoded
        <<" invalid_mask="<<full.invalidMask<<" same_geometry="<<geometrySame<<" diagnostic_rgb_not_color=1 untimed=1"<<std::endl;
}
void VisibilityAuditSnapshot(MaterialLabModule& lab,const FrameSettings& frame,unsigned view,const char* phase,unsigned phaseFrame,
    std::ostream& output,bool& first) {
    using namespace SurfaceCacheArtifactFixture;
    const auto cache=lab.PreparedSurfaceCache();Check(bool(cache),"GI95 visibility audit has no prepared Surface Cache");
    const auto atlas=lab.ReadbackSurfaceCacheAtlas();Check(lab.LastError().empty(),lab.LastError());
    const auto direct=lab.ReadbackSurfaceCacheDirectSource();Check(lab.LastError().empty(),lab.LastError());
    const auto sources=lab.ReadbackScreenProbeSources();Check(lab.LastError().empty(),lab.LastError());
    Check(atlas.size()>=cache->TexelCount()&&direct.size()==std::size_t(frame.width)*frame.height&&
        sources.sourceViewPositions.size()==direct.size(),"GI95 visibility audit readback extent mismatch");
    const auto& attributes=cache->Attributes();const auto& metadata=cache->Metadata();
    const auto objectCount=cache->CardCount()/6;const auto pageTexels=cache->Settings().pageResolution*cache->Settings().pageResolution;
    std::vector<VisibilityAtlasStats> objectAtlas(objectCount),objectSource(objectCount);
    VisibilityAtlasStats full,assigned,padding,source;
    for(std::size_t index=0;index<atlas.size();++index){const bool captured=index<cache->TexelCount()&&attributes[index].w>=.5f;
        full.Add(atlas[index],captured);
        if(index>=cache->TexelCount()){padding.Add(atlas[index]);continue;}
        assigned.Add(atlas[index],captured);const auto card=unsigned(index)/pageTexels;
        const auto owner=unsigned(metadata[card*Render::SurfaceCacheCardTexels+7].w);
        Check(owner<objectAtlas.size(),"GI95 visibility audit card owner is out of range");objectAtlas[owner].Add(atlas[index],captured);
    }
    for(std::size_t index=0;index<direct.size();++index){source.Add(direct[index]);const auto owner=sources.sourceViewPositions[index].w;
        if(owner>=1&&owner<=float(objectCount)&&owner==std::floor(owner))objectSource[unsigned(owner)-1].Add(direct[index]);}
    unsigned rectangleCount=0,pointCount=0;
    for(unsigned index=0;index<frame.effects.areaLights.count;++index){const auto& light=frame.effects.areaLights.lights[index];
        rectangleCount+=light.enabled&&glm::any(glm::greaterThan(light.radiance,glm::vec3(0)));}
    for(const auto color:frame.effects.lighting.lightColors)pointCount+=glm::any(glm::greaterThan(glm::vec3(color),glm::vec3(0)));
    const auto spot=Render::MakeSpotLightConstants(frame.effects.spotLight);
    const bool spotPositive=glm::any(glm::greaterThan(glm::vec3(spot.intensityInner),glm::vec3(0)));
    const bool singleRectangle=frame.effects.shadows.sunIntensity<=0&&!spotPositive&&!pointCount;
    if(!first)output<<",\n";first=false;
    output<<"{\"view\":"<<view<<",\"view_name\":"<<JsonString(ViewNames[view])<<",\"phase\":"<<JsonString(phase)
        <<",\"phase_frame\":"<<phaseFrame<<",\"view_matrix_column_major\":";MatrixJson(output,frame.camera->view);
    output<<",\"projection_column_major\":";MatrixJson(output,frame.camera->projection);
    output<<",\"cache\":{\"texel_count\":"<<cache->TexelCount()<<",\"plane_texels\":"<<cache->PlaneTexels()
        <<",\"readback_texels\":"<<atlas.size()<<",\"card_count\":"<<cache->CardCount()<<",\"page_resolution\":"<<cache->Settings().pageResolution
        <<",\"local_capture_attributes\":"<<cache->Settings().localCaptureAttributes<<",\"capture_shading_normals\":"<<cache->Settings().captureShadingNormals
        <<"},\"cpu_light_eligibility\":{\"sun_intensity\":"<<frame.effects.shadows.sunIntensity<<",\"point_positive_count\":"<<pointCount
        <<",\"spot_enabled\":"<<frame.effects.spotLight.enabled<<",\"spot_positive_after_constants\":"<<spotPositive
        <<",\"area_declared_count\":"<<frame.effects.areaLights.count<<",\"area_enabled_positive_count\":"<<rectangleCount
        <<",\"single_rectangle_condition\":"<<singleRectangle<<",\"eligible_before_runtime_sample_gates\":"<<(singleRectangle&&rectangleCount==1)
        <<",\"ue_lumen_high\":"<<frame.effects.surfaceCacheGi.ueLumenHigh<<",\"cached_direct_diffuse\":"<<frame.effects.surfaceCacheGi.cachedDirectDiffuse
        <<",\"cache_bilinear\":"<<frame.effects.surfaceCacheGi.cacheBilinear<<",\"areas\":[";
    for(unsigned index=0;index<frame.effects.areaLights.count;++index){if(index)output<<',';const auto& light=frame.effects.areaLights.lights[index];
        output<<"{\"enabled\":"<<light.enabled<<",\"two_sided\":"<<light.twoSided<<",\"radiance\":["<<light.radiance.x<<','<<light.radiance.y<<','<<light.radiance.z<<"]}";}
    output<<"]},\"atlas_full\":";full.Json(output);output<<",\"atlas_assigned\":";assigned.Json(output);
    output<<",\"atlas_padding\":";padding.Json(output);output<<",\"source_rgba_full\":";source.Json(output,false);
    output<<",\"objects\":[";
    for(unsigned owner=0;owner<objectCount;++owner){if(owner)output<<',';
        output<<"{\"object_zero_based\":"<<owner<<",\"name\":"<<JsonString(SealedGiRoomObjects().at(owner).name)<<",\"atlas\":";
        objectAtlas[owner].Json(output);output<<",\"source_rgba\":";objectSource[owner].Json(output,false);output<<'}';
        if(SealedRoomMotion::IsProp(owner))std::cout<<"GI95 VISIBILITY OBJECT view="<<view<<" phase="<<phase<<" phase_frame="<<phaseFrame
            <<" object="<<owner<<" texels="<<objectAtlas[owner].count<<" legacy="<<objectAtlas[owner].legacy
            <<" encoded="<<objectAtlas[owner].encoded<<" unknown="<<objectAtlas[owner].unknown<<" other="<<objectAtlas[owner].other
            <<" encoded_black="<<objectAtlas[owner].encodedBlack<<" encoded_alpha_v0="<<objectAtlas[owner].encodedV0
            <<" encoded_rgb_positive="<<objectAtlas[owner].encodedPositive<<" untimed=1"<<std::endl;
    }
    output<<"],\"gpu_primary_flags\":{\"available\":false,\"reason\":\"normal production shader; see isolated Renderer70/71 audit evidence\"}}";
    std::cout<<"GI95 VISIBILITY ATLAS view="<<view<<" phase="<<phase<<" phase_frame="<<phaseFrame<<" texels="<<full.count
        <<" legacy="<<full.legacy<<" encoded="<<full.encoded<<" unknown="<<full.unknown<<" other="<<full.other
        <<" alpha_min="<<full.alphaMin<<" alpha_max="<<full.alphaMax<<" single_rectangle="<<singleRectangle
        <<" rectangle_count="<<rectangleCount<<" cpu_eligible="<<(singleRectangle&&rectangleCount==1)<<" untimed=1"<<std::endl;
}
void VisibilityAudit(MaterialLabModule& lab,const Options& options) {
    Check(options.vulkan&&!options.softwareRays&&options.gi90Profile==21,"GI95 visibility audit requires native UE High");
    std::ostringstream output;output<<std::setprecision(9)<<"{\"format\":\"GI95 visibility audit v1\",\"diagnostic_only\":true,"
        "\"acceptance_thresholds_changed\":false,\"fps_metrics\":false,\"atlas_alpha_semantics\":\"0 unknown; legacy [0.5,1.5); encoded [2,3] stores 2+V; metadata is not receiver readability\","
        "\"encoded_decoded_v0_semantics\":\"encoded RGB exactly black or alpha exactly 2, matching UECardDecodeVisibility\","
        "\"source_alpha_semantics\":\"cached-direct SceneColor alpha; not atlas visibility metadata\",\"snapshots\":[\n";
    bool first=true;
    for(unsigned view=0;view<2;++view){auto frame=MakeMotionFrame(options);frame.present=options.benchmarkPresent;
        frame.camera=MotionCamera(view,frame.OutputAspect());auto objects=SealedGiRoomObjects();Reset(frame);
        for(unsigned f=0;f<64;++f)Draw(lab,frame);
        for(unsigned f=0;f<8;++f)Draw(lab,frame);VerifyProducer(lab);
        VisibilityAuditSnapshot(lab,frame,view,"static",7,output,first);
        for(unsigned f=0;f<32;++f){Advance(frame,objects,f);Draw(lab,frame);}VerifyProducer(lab);
        VisibilityAuditSnapshot(lab,frame,view,"all_props",31,output,first);
    }
    output<<"\n]}\n";auto path=std::filesystem::path(options.screenshot.empty()?"build/gi95_visibility_audit.json":options.screenshot);
    if(!options.screenshot.empty())path.replace_filename(path.stem().string()+"_visibility_audit.json");
    if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path);stream<<output.str();stream.close();Check(bool(stream),"Cannot write GI95 visibility audit JSON");
    std::cout<<"GI95 VISIBILITY AUDIT COMPLETE snapshots=4 rendered_frames=208 diagnostic_draws=0 png=0 fps_metrics=0 untimed=1 json="<<path.generic_string()<<std::endl;
}
} // namespace SurfaceCacheAllObjectMotionFixture
