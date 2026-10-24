// Native round: accepted software versus hardware contact/cache intersection.
// Efficiency round: accepted native versus trace execution/upload scheduling;
// the estimator, ray budgets, history response and filtering stay unchanged.
namespace SurfaceCacheNativeFixture {
constexpr unsigned ScopeCount=15;
constexpr std::array<const char*,ScopeCount> ScopeNames{{"frame","screen_trace","sdf_trace","reconstruction","probe_spatial",
    "contact_trace","contact_filter","temporal","cache_lighting","receiver_motion","fresh_sampling","acceleration_build_nested","area_shadow","sdf_kernel","sdf_pack"}};
constexpr std::array<const char*,5> PathNames{{"static","camera","model","new_exposure","lighting"}};
constexpr std::array<const char*,2> VariantNames{{"accepted_software","native_contact_cache"}};
constexpr unsigned HostScopeCount=24;
constexpr std::array<const char*,HostScopeCount> HostScopeNames{{"frame_begin","command_encode","frame_end","deferred_copy_record",
    "texture_update_prepare","as_prepare","as_geometry_pack","as_retirement","queue_submit","fence_wait","swap_acquire","present_call",
    "timestamp_read","pipeline_create","sdf_scene_prepare","surface_cache_scene_prepare","other_scene_prepare","frame_queue",
    "render_thread","pipeline_resource_prepare","area_shadow_record","post_render_diagnostic_gap","diagnostic_service","completion_delivery_residual"}};
constexpr std::array<const char*,9> HostCounterNames{{"graphics_draw_calls","graphics_descriptor_allocations","graphics_uniform_snapshot_copies","as_builds",
    "as_storage_reuses","as_buffer_allocations","as_handle_creations","as_full_builds","as_updates"}};
constexpr std::array<const char*,16> CompactCounterNames{{"pending","indirect_x","indirect_y","indirect_z","classified_capacity",
    "inactive","screen_resolved","certified_hits","certification_misses_nonexclusive","certification_exhausted","rejected",
    "completed_no_field","consumed","mesh_march_calls","global_march_calls","overflow"}};
void CompactControl(const std::array<std::uint32_t,16>& c,std::uint32_t capacity) {
    Check(capacity>0&&c[4]==capacity&&c[0]<=capacity&&c[12]==c[0]&&c[15]==0,"SDF compact allocation/consumption/overflow contract failed");
    Check(c[1]==(c[0]+63)/64&&c[2]==1&&c[3]==1,"SDF compact indirect arguments do not cover exactly pending workgroups");
    Check(std::uint64_t(c[0])+c[5]+c[6]+c[7]+c[9]+c[10]+c[11]==capacity,"SDF compact classification lost or duplicated a physical ray record");
    Check(c[13]<=c[12]&&c[14]<=c[12]&&c[8]<=c[0]+c[11],"SDF compact physical march/certification counters are inconsistent");
}
// Queue IDs are read only in untimed quality tests. Validate independent launch
// prerequisites against the original screen atlas/placement, not queue order or
// a second invocation of the production compaction predicate. Strict complete
// packed-atlas parity below separately detects omissions and stale scatter data.
std::uint64_t CompactQueue(MaterialLabModule& lab,const ScreenProbeTraceReadback& screen,const ScreenProbePlacementReadback& probes,
    const std::vector<glm::vec4>& origins,bool safeOrigins,bool cacheOnly) {
    const auto data=lab.ReadbackProbeSdfCompaction();CompactControl(data.control,data.capacity);
    Check(screen.packed.size()==std::size_t(data.capacity)*4&&data.queue.size()==std::size_t(data.control[0])*12,
        "SDF compact queue/capacity differs from the full physical atlas");
    std::vector<std::uint8_t> seen(data.capacity,0);std::uint64_t certificationMisses=0;
    const auto vectorAt=[&](std::size_t at){return glm::vec4(std::bit_cast<float>(data.queue[at]),std::bit_cast<float>(data.queue[at+1]),
        std::bit_cast<float>(data.queue[at+2]),std::bit_cast<float>(data.queue[at+3]));};
    const auto finite=[](glm::vec4 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&std::isfinite(v.w);};
    for(std::size_t entry=0;entry<data.control[0];++entry){const auto at=entry*12;const unsigned ray=data.queue[at],miss=data.queue[at+1],status=data.queue[at+2];
        Check(ray<data.capacity&&!seen[ray]++&&miss<=1&&data.queue[at+3]==0,"SDF compact identity is out of range, duplicated or corrupt");
        Check(ray<std::size_t(screen.probeSlots)*screen.rayResolution*screen.rayResolution,"SDF compact queue contains probe capacity padding");
        const unsigned probe=ray/(screen.rayResolution*screen.rayResolution);const bool uniform=probe<probes.uniform.size();
        const auto slot=uniform?probe:probe-unsigned(probes.uniform.size());
        const auto& positions=uniform?probes.uniform:probes.spawned;const auto& normals=uniform?probes.uniformNormals:probes.spawnedNormals;
        Check(slot<positions.size()&&slot<normals.size(),"SDF compact identity lost original probe indexing");
        const auto position=positions[slot],normal=normals[slot],sourceDirection=screen.packed[std::size_t(ray)*4+3];
        const auto origin=vectorAt(at+4),direction=vectorAt(at+8);const auto sourceStatus=screen.packed[std::size_t(ray)*4].w;
        Check(sourceStatus>0&&sourceStatus==float(status)&&position.w>=.5f&&finite(position)&&finite(normal)&&finite(sourceDirection)&&
            glm::dot(glm::vec3(normal),glm::vec3(normal))>=.5f&&glm::dot(glm::vec3(sourceDirection),glm::vec3(sourceDirection))>=.5f,
            "SDF compact queued an inactive/nonfinite/invalid original ray");
        Check(finite(origin)&&finite(direction)&&direction.w>=0&&origin.w>=0&&
            glm::dot(glm::normalize(glm::vec3(sourceDirection)),glm::normalize(glm::vec3(normal)))>(cacheOnly?0.f:.02f),
            "SDF compact queued a rejected hemisphere or invalid saved launch state");
        Check(glm::length(glm::vec3(direction)-glm::normalize(glm::vec3(sourceDirection)))<=2e-6f,"SDF compact changed original ray direction or seeded by compact ordinal");
        if(safeOrigins){Check(probe<origins.size()&&origins[probe].w>0&&glm::vec3(origin)==glm::vec3(origins[probe]),"SDF compact changed certified original launch position");}
        if(status==1)Check(safeOrigins&&cacheOnly&&miss==1,"SDF compact queued a resolved screen hit without a certified miss");
        if(miss)Check(status==1&&safeOrigins&&cacheOnly,"SDF compact certification miss identity lost its original screen hit");
        certificationMisses+=miss;
    }
    Check(certificationMisses<=data.control[8],"SDF compact queue certification identities exceed actual classifier misses");
    return data.control[0];
}
double Mean(const std::vector<double>& a) {double sum=0;for(double v:a)sum+=v;return a.empty()?0.:sum/double(a.size());}
double Quantile(std::vector<double> a,double q) {if(a.empty())return 0.;std::sort(a.begin(),a.end());
    return a[std::min(a.size()-1,std::size_t(std::ceil(q*double(a.size())))-1)];}
double Median(std::vector<double> a) {if(a.empty())return 0.;std::sort(a.begin(),a.end());const auto n=a.size();return n%2?a[n/2]:.5*(a[n/2-1]+a[n/2]);}
template<class T> std::uint64_t Hash(const std::vector<T>& values) {std::uint64_t h=1469598103934665603ull;
    const auto* bytes=reinterpret_cast<const unsigned char*>(values.data());for(std::size_t i=0;i<values.size()*sizeof(T);++i){h^=bytes[i];h*=1099511628211ull;}return h;}
void Finite(const std::vector<glm::vec4>& values,const char* label) {Check(!values.empty(),std::string("Missing native parity ")+label);
    for(const auto& v:values)for(unsigned c=0;c<4;++c)Check(std::isfinite(v[c]),"Nonfinite native parity data");}
std::array<std::uint64_t,4> AreaDepthSignature(const AreaLightShadowDepthReadback& data) {
    Check(data.width>0&&data.height>0&&data.depth.size()==std::size_t(data.width)*data.height,"Missing or truncated area-light depth atlas");
    std::uint64_t covered=0;
    for(float value:data.depth){Check(std::isfinite(value)&&value>=0&&value<=1,"Nonfinite/out-of-range area-light depth sample");covered+=value<1.f;}
    Check(covered>0,"Area-light depth atlas became entirely clear");
    return {data.width,data.height,Hash(data.depth),covered};
}
std::uint64_t CompareAreaDepth(const std::vector<float>& before,const std::vector<float>& after) {
    Check(!before.empty()&&before.size()==after.size(),"Area-light depth allocation changed");
    for(std::size_t i=0;i<before.size();++i)if(std::bit_cast<std::uint32_t>(before[i])!=std::bit_cast<std::uint32_t>(after[i])){
        std::cout<<"SHADOW SUBMISSION depth_mismatch pixel="<<i<<" before="<<before[i]<<" after="<<after[i]<<std::endl;
        Check(false,"Area-light depth atlas changed exact FP32 bits");}
    return before.size();
}
FrameSettings Frame(const Options& options,bool quality) {
    auto fixture=options;fixture.sealedGiRoom=true;fixture.sdfBanquet=false;fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);frame.width=options.renderSizeSet?options.benchmarkWidth:quality?320:1280;
    frame.height=options.renderSizeSet?options.benchmarkHeight:quality?200:800;frame.effects.outputSize={0,0};
    frame.present=!quality&&options.benchmarkPresent;frame.screenProbeStudyHud=false;
    auto& gi=frame.effects.surfaceCacheGi;gi.view=0;gi.reconstructionDebug=0;gi.probeDiagnostics=false;gi.probeOriginDiagnostics=false;
    gi.collectProbeCoverage=false;gi.contact.collectSamplingDiagnostics=quality;
    // Preserve ordinary sealed-room settings rather than the older stress
    // fixture's larger screen/SDF step budgets.
    Check(gi.contact.movingReceiverRayMultiplier==2&&gi.contact.movingReceiverFullCurrent&&gi.temporal.reactiveShadows&&
        gi.temporal.reactiveMovingReceivers&&gi.contact.disocclusionSampling,"Native fixture requires the accepted full2 preset");
    return frame;
}
void LegacyMode(FrameSettings& frame,bool candidate,bool efficiencyRound=false,unsigned efficiencyMask=3,bool compactRound=false,bool shadowRound=false,bool sdfRound=false,bool reuseRound=false,unsigned architectureMask=0) {auto& gi=frame.effects.surfaceCacheGi;
    gi.contact.splitReceiverOrigins=false;gi.contact.reuseLightingLookup=false;gi.contact.fastLightingAddressing=false;
    gi.contact.traceDiagnosticPage=0;gi.contact.collectTraceTimings=false;
    gi.contact.collectTraceDiagnostics=false;
    gi.reuseClosestTraversal=false;gi.fastGeometryAddressing=false;
    frame.effects.screenProbePlacement.trace.sdf.reuseNativeCandidate=false;
    frame.effects.screenProbePlacement.trace.sdf.directSoftwareRefinement=false;
    gi.updateAccelerationStructures=false;gi.receiverSoftwareOrigins=false;gi.reuseOriginSoftwareCertification=false;
    frame.effects.screenProbePlacement.trace.sdf.nativeUploadPruning=false;
    reuseRound=reuseRound||architectureMask!=0;
    frame.effects.screenProbePlacement.trace.sdf.hardwareRayTracing=candidate&&bool(architectureMask&1);
    gi.hardwareOriginTracing=candidate&&bool(architectureMask&1);
    gi.spatial.radianceOnlyFiltering=candidate&&bool(architectureMask&2);
    const bool native=efficiencyRound||compactRound||shadowRound||sdfRound||reuseRound||candidate;gi.hardwareContactTracing=native;gi.hardwareCacheVisibility=native;
    // All historical rounds explicitly disable the next round's features.
    frame.effects.screenProbePlacement.trace.compactSdfRays=compactRound&&candidate&&(efficiencyMask&1);
    gi.reuseAccelerationStorage=compactRound&&candidate&&(efficiencyMask&2);
    frame.effects.screenProbePlacement.trace.sdf.fastAtlasAddressing=(sdfRound&&candidate)||reuseRound;
    frame.effects.screenProbePlacement.trace.sdf.reuseTraceSamples=reuseRound&&(candidate||architectureMask);
    const bool shadowCandidate=sdfRound||reuseRound||candidate,shadowMethod=shadowRound||sdfRound||reuseRound;
    auto& shadows=gi.areaShadows;
    shadows.reuseUniformSnapshotContents=shadowMethod&&shadowCandidate&&(efficiencyMask&1);
    shadows.reuseCasterPreparation=shadowMethod&&shadowCandidate&&(efficiencyMask&2);
    shadows.reuseAtlasStorage=shadowMethod&&shadowCandidate&&(efficiencyMask&4);
    shadows.cullCasters=shadowMethod&&shadowCandidate&&(efficiencyMask&8);
    if(compactRound||shadowRound||sdfRound||reuseRound){frame.effects.screenProbePlacement.trace.singleInvocation=false;
        frame.effects.screenProbePlacement.trace.singlePassOutputs=true;gi.deferResourceUploads=true;}
    else if(efficiencyRound){frame.effects.screenProbePlacement.trace.singleInvocation=false;
        frame.effects.screenProbePlacement.trace.singlePassOutputs=candidate&&(efficiencyMask&1);
        gi.deferResourceUploads=candidate&&(efficiencyMask&2);}}
const char* ModelUpdateVariant(unsigned mask) {
    constexpr std::array<const char*,32> names{{"accepted_round7","acceleration_update","pruned_native_uploads","update_pruned_uploads",
        "origin_certification_reuse","update_origin_reuse","pruned_uploads_origin_reuse","update_prune_origin_reuse",
        "software_receiver_origins","update_software_receiver_origins","pruned_uploads_software_receiver_origins","update_prune_software_receiver_origins",
        "origin_reuse_software_receiver_origins","update_origin_reuse_software_receiver_origins","pruned_uploads_origin_reuse_software_receiver_origins","optimized_model_resources",
        "retained_storage_build","invalid_as_policy_17","retained_build_pruned_uploads","invalid_as_policy_19",
        "retained_build_origin_reuse","invalid_as_policy_21","retained_build_prune_origin_reuse","invalid_as_policy_23",
        "retained_build_software_receiver_origins","invalid_as_policy_25","retained_build_prune_software_receiver_origins","invalid_as_policy_27",
        "retained_build_origin_reuse_software_receiver_origins","invalid_as_policy_29","retained_build_prune_origin_reuse_software_receiver_origins","invalid_as_policy_31"}};
    Check(mask<names.size()&&(mask&17)!=17,"Invalid or conflicting model update AS policy");return names[mask];
}
const char* QueryReuseVariant(unsigned mask) {
    constexpr std::array<const char*,16> names{{"accepted_round8","native_candidate_reuse","closest_bounds_reuse","candidate_closest_bounds_reuse",
        "fast_geometry_addressing","candidate_geometry_addressing","closest_bounds_geometry_addressing","combined_query_reuse",
        "direct_software_refinement","invalid_direct_candidate_9","direct_software_bounds_reuse","invalid_direct_candidate_11",
        "direct_software_geometry_addressing","invalid_direct_candidate_13","direct_software_bounds_geometry_addressing","invalid_direct_candidate_15"}};
    Check(mask>0&&mask<names.size()&&(mask&9)!=9,"Invalid or conflicting query-reuse mask");return names[mask];
}
const char* ReceiverVariant(unsigned mask) {
    constexpr std::array<const char*,16> names{{"accepted_round9","split_receiver_origins","receiver_lookup_reuse","receiver_origin_lookup",
        "receiver_fast_addressing","split_receiver_fast_addressing","receiver_lookup_fast_addressing","receiver_all",
        "receiver_software_origins","split_receiver_software_origins","receiver_lookup_software_origins","receiver_origin_lookup_software_origins",
        "receiver_fast_addressing_software_origins","split_receiver_fast_addressing_software_origins","receiver_lookup_fast_addressing_software_origins","receiver_all_software_origins"}};
    Check(mask>0&&mask<names.size(),"Receiver mask must select splitting=1, lookup reuse=2, fast lighting addressing=4 and/or software receiver origins=8");return names[mask];
}
void Mode(FrameSettings& frame,bool candidate,bool efficiencyRound=false,unsigned efficiencyMask=3,bool compactRound=false,bool shadowRound=false,bool sdfRound=false,bool reuseRound=false,unsigned architectureMask=0,unsigned modelUpdateMask=0,unsigned queryReuseMask=0,unsigned receiverMask=0) {
    if(receiverMask){
        ReceiverVariant(receiverMask);Mode(frame,true,false,3,false,false,false,false,0,0,12);
        auto& contact=frame.effects.surfaceCacheGi.contact;
        contact.splitReceiverOrigins=candidate&&bool(receiverMask&1);
        contact.reuseLightingLookup=candidate&&bool(receiverMask&2);
        contact.fastLightingAddressing=candidate&&bool(receiverMask&4);
        frame.effects.surfaceCacheGi.receiverSoftwareOrigins=candidate&&bool(receiverMask&8);return;
    }
    if(queryReuseMask){
        QueryReuseVariant(queryReuseMask);
        Mode(frame,true,false,3,false,false,false,false,0,3);
        frame.effects.screenProbePlacement.trace.sdf.reuseNativeCandidate=candidate&&bool(queryReuseMask&1);
        frame.effects.screenProbePlacement.trace.sdf.directSoftwareRefinement=candidate&&bool(queryReuseMask&8);
        frame.effects.surfaceCacheGi.reuseClosestTraversal=candidate&&bool(queryReuseMask&2);
        frame.effects.surfaceCacheGi.fastGeometryAddressing=candidate&&bool(queryReuseMask&4);return;
    }
    if(!modelUpdateMask){LegacyMode(frame,candidate,efficiencyRound,efficiencyMask,compactRound,shadowRound,sdfRound,reuseRound,architectureMask);return;}
    // The same accepted Round7 main/native-origin/radiance/shadow preset is
    // active in both groups. Only the selected Round8 execution controls differ.
    LegacyMode(frame,true,false,15,false,false,false,true,3);
    auto& gi=frame.effects.surfaceCacheGi;
    gi.updateAccelerationStructures=candidate&&bool(modelUpdateMask&1);
    gi.reuseAccelerationStorage=candidate&&bool(modelUpdateMask&16);
    frame.effects.screenProbePlacement.trace.sdf.nativeUploadPruning=candidate&&bool(modelUpdateMask&2);
    gi.reuseOriginSoftwareCertification=candidate&&bool(modelUpdateMask&4);
    gi.receiverSoftwareOrigins=candidate&&bool(modelUpdateMask&8);
}
void Reset(FrameSettings& frame) {frame.effects.cameraCut=true;frame.effects.screenProbePlacement.reset=true;frame.effects.surfaceCacheGi.temporal.reset=true;}
void Draw(MaterialLabModule& lab,FrameSettings& frame) {Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;
    frame.effects.screenProbePlacement.reset=false;frame.effects.surfaceCacheGi.temporal.reset=false;}
void LegacyEffective(MaterialLabModule& lab,bool candidate,bool efficiencyRound=false,unsigned efficiencyMask=3,bool compactRound=false,bool shadowRound=false,bool sdfRound=false,bool reuseRound=false,unsigned architectureMask=0,bool directMainRefinement=false) {const auto stats=lab.PipelineStatistics();
    reuseRound=reuseRound||architectureMask!=0;
    const bool nativeProbes=candidate&&bool(architectureMask&1);
    Check(stats.probeSdfHardwareRayTracing==(nativeProbes&&!directMainRefinement)&&
        stats.probeSdfDirectSoftwareRefinement==(nativeProbes&&directMainRefinement)&&stats.surfaceCacheHardwareOrigins==nativeProbes,
        "Requested hardware/direct probe and native safe-origin paths were not selected");
    Check(stats.surfaceCacheRadianceOnlyFiltering==(candidate&&bool(architectureMask&2)),
        "Requested radiance-only filter path was not selected");
    const bool native=efficiencyRound||compactRound||shadowRound||sdfRound||reuseRound||candidate;
    Check(stats.surfaceCacheHardwareContact==native&&stats.surfaceCacheHardwareVisibility==native,
        "Requested native/software contact/cache mode was not actually selected (native capability is required)");
    Check(nativeProbes||stats.probeSdfFastAtlasAddressing==((sdfRound&&candidate)||reuseRound),"Requested SDF atlas-addressing shader variant was not selected");
    Check(nativeProbes||stats.probeSdfReuseTraceSamples==(reuseRound&&(candidate||architectureMask)),"Requested SDF sample-reuse shader variant was not selected");
    if(shadowRound||sdfRound||reuseRound){
        Check(stats.screenProbeSinglePassOutputs&&stats.probeSdfSinglePassOutputs&&!stats.screenProbeSingleInvocation&&
            !stats.probeSdfSingleInvocation&&!stats.probeSdfCompactRays,"Shadow comparison lost accepted full-atlas MRT transport");
        const bool prepared=(sdfRound||reuseRound||candidate)&&(efficiencyMask&2),cull=(sdfRound||reuseRound||candidate)&&(efficiencyMask&8);
        if(stats.areaLightShadowsUpdated){
            Check(stats.areaLightShadowPasses>0&&stats.areaLightShadowCasterCount>0,"Shadow refresh did not draw any eligible caster set");
            Check(std::uint64_t(stats.areaLightShadowCasterDraws)+stats.areaLightShadowCulledCasters==
                std::uint64_t(stats.areaLightShadowCasterCount)*stats.areaLightShadowPasses,"Shadow submission lost or duplicated a caster-face");
            Check(stats.areaLightShadowCasterPreparationUsed==prepared,"Requested caster preparation mode was not selected");
            const auto uploads=prepared?stats.areaLightShadowCasterCount:stats.areaLightShadowCasterDraws;
            Check(stats.areaLightShadowMaterialPins==uploads&&stats.areaLightShadowCasterObjectBytes==std::uint64_t(uploads)*sizeof(glm::mat4),
                "Shadow caster pin/object upload accounting differs from actual preparation mode");
            if(!cull)Check(stats.areaLightShadowCulledCasters==0,"Full shadow baseline unexpectedly culled caster-faces");
        } else Check(stats.areaLightShadowPasses==0&&stats.areaLightShadowCasterDraws==0&&stats.areaLightShadowCulledCasters==0&&
            stats.areaLightShadowMaterialPins==0&&stats.areaLightShadowCasterObjectBytes==0&&stats.areaLightShadowCasterParameterBytes==0,
            "Cached shadow atlas repeated caster preparation or drawing");
    }
    else if(compactRound){const bool compact=candidate&&(efficiencyMask&1);
        Check(stats.screenProbeSinglePassOutputs&&stats.probeSdfSinglePassOutputs&&!stats.screenProbeSingleInvocation&&!stats.probeSdfSingleInvocation,
            "Compaction comparison lost accepted screen MRT transport");
        Check(stats.probeSdfCompactRays==compact,"Requested SDF compaction mode was not actually selected");
        if(!compact)Check(stats.probeSdfSinglePassOutputs,"Compaction baseline lost accepted SDF MRT transport");}
    else if(efficiencyRound){const bool single=candidate&&(efficiencyMask&1);
        Check(stats.screenProbeSinglePassOutputs==single&&stats.probeSdfSinglePassOutputs==single&&
            !stats.screenProbeSingleInvocation&&!stats.probeSdfSingleInvocation,
            "Requested probe efficiency mode was not actually selected (candidate capability is required)");}}
void Effective(MaterialLabModule& lab,bool candidate,bool efficiencyRound=false,unsigned efficiencyMask=3,bool compactRound=false,bool shadowRound=false,bool sdfRound=false,bool reuseRound=false,unsigned architectureMask=0,unsigned modelUpdateMask=0,unsigned queryReuseMask=0,unsigned receiverMask=0,bool expectedReceiverSoftwareOrigins=false) {
    if(receiverMask){
        ReceiverVariant(receiverMask);Effective(lab,true,false,3,false,false,false,false,0,0,12,0,candidate&&bool(receiverMask&8));
        const auto stats=lab.PipelineStatistics();
        Check(stats.surfaceCacheSplitReceiverOrigins==(candidate&&bool(receiverMask&1)),"Receiver origin split was not actually selected");
        Check(stats.surfaceCacheReceiverLookupReused==(candidate&&bool(receiverMask&2)),"Receiver lighting lookup reuse was not actually selected");
        Check(stats.surfaceCacheReceiverFastLightingAddressing==(candidate&&bool(receiverMask&4)),"Receiver fast lighting addressing was not actually selected");
        Check(stats.surfaceCacheSoftwareReceiverOrigins==(candidate&&bool(receiverMask&8)),"Receiver software origins were not actually selected");return;
    }
    if(queryReuseMask){
        QueryReuseVariant(queryReuseMask);
        const bool direct=candidate&&bool(queryReuseMask&8);
        LegacyEffective(lab,true,false,15,false,false,false,true,3,direct);
        const auto stats=lab.PipelineStatistics();
        Check(stats.probeSdfNativeUploadsPruned&&!stats.surfaceCacheReusedOriginCertification&&stats.surfaceCacheSoftwareReceiverOrigins==expectedReceiverSoftwareOrigins,
            "Query reuse changed accepted Round8 upload/origin policy");
        Check(stats.probeSdfNativeCandidateReused==(candidate&&bool(queryReuseMask&1)&&!direct),"Query reuse main native-candidate path was not selected");
        Check(stats.probeSdfDirectSoftwareRefinement==direct,"Query direct-software main refinement path was not selected");
        Check(stats.surfaceCacheClosestTraversalReused==(candidate&&bool(queryReuseMask&2)),"Query reuse nearest traversal path was not selected");
        Check(stats.surfaceCacheFastGeometryAddressing==(candidate&&bool(queryReuseMask&4)),"Query reuse geometry addressing path was not selected");return;
    }
    if(!modelUpdateMask){LegacyEffective(lab,candidate,efficiencyRound,efficiencyMask,compactRound,shadowRound,sdfRound,reuseRound,architectureMask);return;}
    LegacyEffective(lab,true,false,15,false,false,false,true,3);
    const auto stats=lab.PipelineStatistics();
    Check(stats.probeSdfNativeUploadsPruned==(candidate&&bool(modelUpdateMask&2)),"Model-update native upload pruning was not selected");
    Check(stats.surfaceCacheReusedOriginCertification==(candidate&&bool(modelUpdateMask&4)),"Model-update origin certification path was not selected");
    Check(stats.surfaceCacheSoftwareReceiverOrigins==(candidate&&bool(modelUpdateMask&8)),"Model-update receiver origin policy was not selected");
}
std::array<const std::vector<double>*,ScopeCount> Scopes(const FrameTiming& t) {return {{&t.gpuFrameMilliseconds,&t.probeScreenTraceFrameMilliseconds,
    &t.probeSdfTraceFrameMilliseconds,&t.probeReconstructionFrameMilliseconds,&t.probeSpatialFrameMilliseconds,&t.contactTraceFrameMilliseconds,
    &t.contactFilterFrameMilliseconds,&t.surfaceCacheTemporalFrameMilliseconds,&t.surfaceCacheLightingFrameMilliseconds,&t.receiverMotionFrameMilliseconds,
    &t.freshSamplingFrameMilliseconds,&t.accelerationBuildFrameMilliseconds,&t.areaShadowFrameMilliseconds,&t.sdfKernelFrameMilliseconds,&t.sdfPackFrameMilliseconds}};}
void Pose(FrameSettings& frame,const Render::PipelineCamera& base,unsigned path,unsigned i,glm::vec3 originalRadiance) {
    frame.camera=base;frame.screenProbeStudyState.occluderOffset=0;frame.effects.areaLights.lights[0].radiance=originalRadiance;
    const unsigned cycle=i%16,step=cycle<8?cycle+1:15-cycle;
    if(path==1||path==3){const auto inverse=glm::inverse(base.view);const auto right=glm::normalize(glm::vec3(inverse[0]));
        const auto forward=glm::normalize(-glm::vec3(inverse[2]));auto camera=base;
        camera.position+=right*((path==3?.10f:.0125f)*float(step));
        const auto direction=glm::vec3(glm::rotate(glm::mat4(1),(path==3?.025f:.006f)*float(step),glm::vec3(0,1,0))*glm::vec4(forward,0));
        camera.view=glm::lookAt(camera.position,camera.position+direction,glm::vec3(0,1,0));frame.camera=camera;}
    if(path==2)frame.screenProbeStudyState.occluderOffset=.15f*float(step);
    if(path==4)frame.effects.areaLights.lights[0].radiance=originalRadiance*(i%2?.8f:1.1f);
}
std::string OutputPath(const Options& options,const std::string& label) {auto p=std::filesystem::path(options.screenshot);
    p.replace_filename(p.stem().string()+"_"+label+p.extension().string());return p.string();}
void Tone(const Options& options,unsigned width,unsigned height,const std::vector<glm::vec4>& values,const std::string& label) {
    if(options.screenshot.empty())return;Check(values.size()==std::size_t(width)*height,"Invalid native parity image extent");
    FramePixels image;image.width=width;image.height=height;image.rgba.resize(values.size()*4);
    for(std::size_t i=0;i<values.size();++i){for(unsigned c=0;c<3;++c){double x=4*std::max(0.,double(values[i][c]));x=x/(1+x);
            x=x<=.0031308?12.92*x:1.055*std::pow(x,1/2.4)-.055;image.rgba[4*i+c]=std::uint8_t(std::lround(std::clamp(x,0.,1.)*255));}
        image.rgba[4*i+3]=255;}Save(image,OutputPath(options,label));
}
}

void SurfaceCacheNativePerformance(MaterialLabModule& lab,const Options& options,bool efficiencyRound=false,bool compactRound=false,bool shadowRound=false,bool sdfRound=false,bool reuseRound=false,unsigned architectureMask=0,unsigned modelUpdateMask=0,unsigned queryReuseMask=0,unsigned receiverMask=0) {
    using namespace SurfaceCacheNativeFixture;
    if(receiverMask){ReceiverVariant(receiverMask);queryReuseMask=12;}
    reuseRound=reuseRound||architectureMask!=0||modelUpdateMask!=0||queryReuseMask!=0;
    const bool architectureNative=bool(architectureMask&1)||modelUpdateMask!=0||queryReuseMask!=0;
    const bool executionRound=efficiencyRound||compactRound||shadowRound||sdfRound||reuseRound;
    const bool shadowMethod=shadowRound||sdfRound||reuseRound;
    const unsigned comparisonMask=shadowMethod?15u:3u;
    const auto& variantNames=receiverMask?std::array<const char*,2>{{"accepted_round9",ReceiverVariant(receiverMask)}}:queryReuseMask?std::array<const char*,2>{{"accepted_round8",QueryReuseVariant(queryReuseMask)}}:modelUpdateMask?std::array<const char*,2>{{"accepted_round7",ModelUpdateVariant(modelUpdateMask)}}:architectureMask?std::array<const char*,2>{{"accepted_round6",architectureMask==1?"native_probes_origins":architectureMask==2?"radiance_filter":"native_and_radiance"}}:reuseRound?std::array<const char*,2>{{"accepted_sdf_addressing","optimized_sdf_reuse"}}:sdfRound?std::array<const char*,2>{{"accepted_shadow_bundle","optimized_sdf_addressing"}}:shadowRound?std::array<const char*,2>{{"accepted_default_shadow","optimized_shadow_submission"}}:compactRound?std::array<const char*,2>{{"accepted_mrt_deferred","optimized_compact_reuse"}}:executionRound?std::array<const char*,2>{{"accepted_native","optimized_trace_upload"}}:VariantNames;
    const char* tag=receiverMask?"RECEIVER GI":queryReuseMask?"QUERY REUSE GI":modelUpdateMask?"MODEL UPDATE GI":architectureMask?"ARCHITECTURE GI":reuseRound?"SDF REUSE GI":sdfRound?"SDF EFFICIENCY GI":shadowRound?"SHADOW SUBMISSION GI":compactRound?"COMPACTION GI":executionRound?"EFFICIENCY GI":"NATIVE GI";
    Check(options.vulkan&&!options.softwareRays&&!options.validation,"Native A/B performance requires capability-enabled Vulkan without validation or --software-rays");
    const unsigned measured=options.surfaceCacheDynamicFrames,warm=64,transitionWarm=16;
    Check(measured>=16&&measured%16==0,"Native A/B performance requires complete 16-frame trajectories");
    auto frame=Frame(options,false);const auto base=SurfaceCacheFreshSamplingCamera(2,frame.OutputAspect(),true);
    const glm::vec3 originalRadiance=frame.effects.areaLights.lights[0].radiance;
    struct Samples {std::array<std::vector<double>,ScopeCount> gpu;std::array<std::vector<double>,5> wall;
        std::array<std::vector<double>,HostScopeCount> host;
        std::array<std::uint64_t,9> hostCounters{};std::array<std::uint64_t,16> compactCounters{};
        std::uint64_t compactCapacity=0;unsigned compactFrames=0;
        std::uint64_t rays=0,attributes=0,geometry=0,regions=0,lit=0,areaPasses=0;
        unsigned presents=0,history=0,hardwareContact=0,hardwareVisibility=0,singleScreen=0,singleSdf=0,fastSdf=0,reusedSdf=0;
        unsigned nativeProbeFrames=0,nativeOriginFrames=0,radianceFilterFrames=0;
        unsigned nativeUploadPrunedFrames=0,reusedOriginCertificationFrames=0,softwareReceiverOriginFrames=0,asUpdateRequestedFrames=0;std::uint64_t spatialFilterTexels=0,spatialTargetPeakBytes=0;
        unsigned asStorageBuildRequestedFrames=0,mainCandidateReuseFrames=0,closestTraversalReuseFrames=0,fastGeometryAddressingFrames=0,directSoftwareRefinementFrames=0;
        unsigned receiverOriginSplitFrames=0,receiverLookupReuseFrames=0,receiverFastLightingAddressingFrames=0;
        std::array<std::uint64_t,8> uploads{};
        std::array<std::uint64_t,9> snapshots{};std::array<std::uint64_t,15> shadows{};};
    std::array<std::array<Samples,5>,2> total;
    const auto append=[](Samples& target,const Samples& source){for(unsigned s=0;s<ScopeCount;++s)target.gpu[s].insert(target.gpu[s].end(),source.gpu[s].begin(),source.gpu[s].end());
        for(unsigned s=0;s<5;++s)target.wall[s].insert(target.wall[s].end(),source.wall[s].begin(),source.wall[s].end());
        target.rays+=source.rays;target.attributes+=source.attributes;target.geometry+=source.geometry;target.regions+=source.regions;
        target.lit+=source.lit;target.areaPasses+=source.areaPasses;target.presents+=source.presents;target.history+=source.history;
        target.hardwareContact+=source.hardwareContact;target.hardwareVisibility+=source.hardwareVisibility;
        target.singleScreen+=source.singleScreen;target.singleSdf+=source.singleSdf;target.fastSdf+=source.fastSdf;target.reusedSdf+=source.reusedSdf;
        target.nativeProbeFrames+=source.nativeProbeFrames;target.nativeOriginFrames+=source.nativeOriginFrames;target.radianceFilterFrames+=source.radianceFilterFrames;
        target.nativeUploadPrunedFrames+=source.nativeUploadPrunedFrames;target.reusedOriginCertificationFrames+=source.reusedOriginCertificationFrames;
        target.softwareReceiverOriginFrames+=source.softwareReceiverOriginFrames;target.asUpdateRequestedFrames+=source.asUpdateRequestedFrames;
        target.asStorageBuildRequestedFrames+=source.asStorageBuildRequestedFrames;
        target.mainCandidateReuseFrames+=source.mainCandidateReuseFrames;target.closestTraversalReuseFrames+=source.closestTraversalReuseFrames;
        target.fastGeometryAddressingFrames+=source.fastGeometryAddressingFrames;
        target.directSoftwareRefinementFrames+=source.directSoftwareRefinementFrames;
        target.receiverOriginSplitFrames+=source.receiverOriginSplitFrames;target.receiverLookupReuseFrames+=source.receiverLookupReuseFrames;
        target.receiverFastLightingAddressingFrames+=source.receiverFastLightingAddressingFrames;
        target.spatialFilterTexels+=source.spatialFilterTexels;target.spatialTargetPeakBytes=std::max(target.spatialTargetPeakBytes,source.spatialTargetPeakBytes);
        for(unsigned s=0;s<HostScopeCount;++s)target.host[s].insert(target.host[s].end(),source.host[s].begin(),source.host[s].end());
        for(unsigned c=0;c<HostCounterNames.size();++c)target.hostCounters[c]+=source.hostCounters[c];
        for(unsigned c=0;c<16;++c)target.compactCounters[c]+=source.compactCounters[c];
        target.compactCapacity+=source.compactCapacity;target.compactFrames+=source.compactFrames;
        for(unsigned c=0;c<9;++c)if(c==6)target.snapshots[c]=std::max(target.snapshots[c],source.snapshots[c]);else target.snapshots[c]+=source.snapshots[c];
        for(unsigned c=0;c<15;++c)if(c==1||c==13||c==14)target.shadows[c]=std::max(target.shadows[c],source.shadows[c]);else target.shadows[c]+=source.shadows[c];
        for(unsigned c=0;c<7;++c)target.uploads[c]+=source.uploads[c];target.uploads[7]=std::max(target.uploads[7],source.uploads[7]);};
    const auto report=[&](const Samples& a,unsigned variant,unsigned path,int trial,const char* variantOverride=nullptr){
        const char* variantName=variantOverride?variantOverride:variantNames[variant];
        for(unsigned s=0;s<ScopeCount;++s)std::cout<<tag<<" PERF scope variant="<<variantName<<" path="<<PathNames[path]<<" trial="<<trial
            <<" name="<<ScopeNames[s]<<" samples="<<a.gpu[s].size()<<" executed="<<!a.gpu[s].empty()<<" mean_ms="<<Mean(a.gpu[s])
            <<" median_ms="<<Median(a.gpu[s])<<" p95_ms="<<Quantile(a.gpu[s],.95)<<" nested="<<(s==11||s>=13)<<std::endl;
        constexpr std::array<const char*,5> names{{"render_submit_present","event_render_loop","scene_prepare","frame_record_and_upload","submit_gpu_present_wait"}};
        for(unsigned s=0;s<5;++s)std::cout<<tag<<" PERF wall variant="<<variantName<<" path="<<PathNames[path]<<" trial="<<trial
            <<" name="<<names[s]<<" samples="<<a.wall[s].size()<<" mean_ms="<<Mean(a.wall[s])<<" median_ms="<<Median(a.wall[s])<<" p95_ms="<<Quantile(a.wall[s],.95)
            <<" includes_frame_completion_wait="<<(s==0||s==1||s==4)<<" may_wait_upload_callbacks="<<(s==3)<<std::endl;
        std::cout<<tag<<" PERF work variant="<<variantName<<" path="<<PathNames[path]<<" trial="<<trial
            <<" completed_render_calls="<<a.wall[0].size()<<" present_calls="<<a.presents<<" presented_render_loop_fps="<<(frame.present?1000./Mean(a.wall[1]):0.)
            <<" physical_display_timestamps=0 hardware_contact_frames="<<a.hardwareContact<<" hardware_visibility_frames="<<a.hardwareVisibility
            <<" mrt_single_screen_frames="<<a.singleScreen<<" mrt_single_sdf_frames="<<a.singleSdf<<" fast_sdf_addressing_frames="<<a.fastSdf<<" reused_sdf_samples_frames="<<a.reusedSdf
            <<" native_probe_frames="<<a.nativeProbeFrames<<" native_origin_frames="<<a.nativeOriginFrames<<" radiance_filter_frames="<<a.radianceFilterFrames
            <<" native_upload_pruned_frames="<<a.nativeUploadPrunedFrames<<" reused_origin_certification_frames="<<a.reusedOriginCertificationFrames
            <<" software_receiver_origin_frames="<<a.softwareReceiverOriginFrames<<" as_update_requested_frames="<<a.asUpdateRequestedFrames
            <<" as_storage_build_requested_frames="<<a.asStorageBuildRequestedFrames
            <<" main_candidate_reuse_frames="<<a.mainCandidateReuseFrames<<" closest_traversal_reuse_frames="<<a.closestTraversalReuseFrames
            <<" fast_geometry_addressing_frames="<<a.fastGeometryAddressingFrames<<" direct_software_refinement_frames="<<a.directSoftwareRefinementFrames
            <<" receiver_origin_split_frames="<<a.receiverOriginSplitFrames<<" receiver_lookup_reuse_frames="<<a.receiverLookupReuseFrames
            <<" receiver_fast_lighting_addressing_frames="<<a.receiverFastLightingAddressingFrames
            <<" spatial_filter_fragment_texels="<<a.spatialFilterTexels<<" spatial_ping_pong_peak_bytes="<<a.spatialTargetPeakBytes
            <<" history_used_frames="<<a.history<<" probe_ray_budget="<<a.rays<<" attribute_uploaded_texels="<<a.attributes
            <<" geometry_uploaded_texels="<<a.geometry<<" upload_regions="<<a.regions<<" cache_relit_texels="<<a.lit<<" area_shadow_passes="<<a.areaPasses<<std::endl;
        if(executionRound)std::cout<<tag<<" PERF uploads variant="<<variantName<<" path="<<PathNames[path]<<" trial="<<trial
            <<" physical_regions="<<a.uploads[0]<<" physical_bytes="<<a.uploads[1]<<" deferred_regions="<<a.uploads[2]<<" deferred_bytes="<<a.uploads[3]
            <<" dedicated_immediate_submissions="<<a.uploads[4]<<" dedicated_immediate_fence_waits="<<a.uploads[5]
            <<" staging_allocations="<<a.uploads[6]<<" maximum_staging_capacity_bytes="<<a.uploads[7]
            <<" mandatory_frame_fence_not_counted_as_dedicated_upload_wait=1 counters_captured_with_existing_timing_callback=1"<<std::endl;
        if(compactRound||shadowRound||sdfRound||reuseRound){for(unsigned s=0;s<HostScopeCount;++s)std::cout<<tag<<" PERF host variant="<<variantName<<" path="<<PathNames[path]<<" trial="<<trial
                <<" name="<<HostScopeNames[s]<<" samples="<<a.host[s].size()<<" mean_ms="<<Mean(a.host[s])<<" median_ms="<<Median(a.host[s])
                <<" p95_ms="<<Quantile(a.host[s],.95)<<" nested_intervals_not_additive=1"<<std::endl;
            std::cout<<tag<<" PERF host_work variant="<<variantName<<" path="<<PathNames[path]<<" trial="<<trial;
            for(unsigned c=0;c<HostCounterNames.size();++c)std::cout<<' '<<HostCounterNames[c]<<'='<<a.hostCounters[c];std::cout<<std::endl;
            if(compactRound){std::cout<<tag<<" PERF compact_work variant="<<variantName<<" path="<<PathNames[path]<<" trial="<<trial
                <<" actual_compact_frames="<<a.compactFrames<<" physical_capacity_sum="<<a.compactCapacity;
            for(unsigned c=0;c<16;++c)std::cout<<' '<<CompactCounterNames[c]<<'='<<a.compactCounters[c];
            std::cout<<" dispatched_thread_slots="<<64*a.compactCounters[1]<<" inactive_dispatch_tail_slots="<<64*a.compactCounters[1]-a.compactCounters[0]
                <<" allocator_read_with_existing_completion_callback=1 standalone_gpu_readback_submit=0 baseline_march_calls_not_instrumented=1"
                <<" draw_descriptor_ubo_counters_graphics_only=1 compute_descriptor_ubo_counts_not_included=1"<<std::endl;}}
        if(shadowMethod){
            constexpr std::array<const char*,9> names{{"enabled_frames","content_lookups","content_reuses","content_comparisons","content_hash_collisions",
                "graphics_copied_bytes","maximum_ring_used_bytes","descriptor_cache_hits","descriptor_writes"}};
            std::cout<<tag<<" PERF uniform_work variant="<<variantName<<" path="<<PathNames[path]<<" trial="<<trial;
            for(unsigned c=0;c<names.size();++c)std::cout<<' '<<names[c]<<'='<<a.snapshots[c];
            std::cout<<" graphics_only_copies_and_descriptors=1 ring_includes_padding_and_optional_compute=1 cross_frame_content_reuse=0"<<std::endl;
            constexpr std::array<const char*,15> shadowNames{{"passes","maximum_atlas_bytes","updated_frames","cache_hit_frames","prepared_frames","eligible_casters",
                "actual_draws","culled_caster_faces","caster_parameter_bytes","caster_object_bytes","material_pins","atlas_allocations","atlas_reuses",
                "maximum_retained_pool_bytes","maximum_retained_pool_entries"}};
            std::cout<<tag<<" PERF shadow_work variant="<<variantName<<" path="<<PathNames[path]<<" trial="<<trial;
            for(unsigned c=0;c<shadowNames.size();++c)std::cout<<' '<<shadowNames[c]<<'='<<a.shadows[c];
            std::cout<<" culled_counter_per_face_not_unique_objects=1 view_uploads_excluded_from_caster_bytes=1 depth_readbacks_in_timing=0"<<std::endl;}};
#ifdef NDEBUG
    constexpr const char* build="Release_NDEBUG";
#else
    constexpr const char* build="Debug_not_acceptance";
#endif
    std::cout<<tag<<" PERF setup build="<<build<<" size="<<frame.width<<'x'<<frame.height<<" order=0,1,1,0 warm_per_trial="<<warm
        <<" transition_warm_per_path="<<transitionWarm<<" transition_warm_uses_trajectory="<<executionRound
        <<" measured_per_path="<<measured<<" paths=5 present="<<frame.present
        <<" image_readbacks=0 cpu_oracle=0 validation=0 application_fps_limit=0 hud=0 probe_estimator_unchanged=1"
        <<" probe_rays="<<frame.effects.screenProbePlacement.trace.rayResolution*frame.effects.screenProbePlacement.trace.rayResolution
        <<" screen_steps="<<frame.effects.screenProbePlacement.trace.maxSteps<<" sdf_steps="<<frame.effects.screenProbePlacement.trace.sdf.maxSteps
        <<" receiver_rays="<<frame.effects.surfaceCacheGi.contact.raysPerPixel<<" moving_multiplier=2 moving_full_current=1"
        <<" efficiency_round="<<efficiencyRound<<" compaction_round="<<compactRound<<" shadow_submission_round="<<shadowRound<<" sdf_efficiency_round="<<sdfRound<<" sdf_reuse_round="<<reuseRound<<" fast_sdf_addressing_both_groups="<<reuseRound<<" hardware_contact_and_cache_both_groups="<<executionRound
        <<" accepted_mrt_and_deferred_uploads_both_groups="<<(compactRound||shadowRound||sdfRound||reuseRound)<<" sdf_scope_includes_compaction_clear_classify_and_indirect_scatter="<<compactRound
        <<" sdf_kernel_contains_screen_certification_refinement_and_cache_lookup=1 kernel_pack_nested_in_sdf_total=1 gpu_atomic_instrumentation=0"
        <<" receiver_mask="<<receiverMask<<" receiver_fast_lighting_addressing="<<bool(receiverMask&4)<<" receiver_software_origins="<<bool(receiverMask&8)<<" accepted_round9_query_mask="<<(receiverMask?12:0)
        <<" round9_preset_both_groups="<<bool(receiverMask)<<" planned_receiver_origin_pass="<<bool(receiverMask&1)
        <<" planned_receiver_draw_delta_per_frame="<<((receiverMask&1)?1:0)<<" planned_receiver_descriptor_delta_per_frame="<<((receiverMask&1)?3:0)
        <<" contact_trace_timings=0 receiver_diagnostic_page=0"
        <<" model_update_mask="<<modelUpdateMask<<" query_reuse_mask="<<queryReuseMask<<" planned_query_descriptor_changes="<<(!receiverMask&&bool(queryReuseMask&8))<<" round8_resource_preset_both_groups="<<bool(queryReuseMask)<<" main_triangle_profile_both_groups="<<bool(queryReuseMask)
        <<" main_profile_bypasses_sdf_march="<<architectureNative<<" architecture_mask="<<architectureMask<<" ray_budget_unchanged=1 native_bypasses_sdf_march="<<(architectureNative&&!(queryReuseMask&8))
        <<" shadow_bundle_both_groups="<<(sdfRound||reuseRound)<<" event_loop_cadence_not_physical_scanout=1 gpu_frame_excludes_present_copy=1 confidence_interval=0"<<std::endl;
    const auto measure=[&](Samples& samples,bool candidate,unsigned path,unsigned i,unsigned efficiencyMask=3){Pose(frame,base,path,i,originalRadiance);
        const auto begin=std::chrono::steady_clock::now();glfwPollEvents();const auto timing=lab.MeasureFrames(frame,1,0);
        const double loop=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        frame.effects.cameraCut=false;frame.effects.screenProbePlacement.reset=false;frame.effects.surfaceCacheGi.temporal.reset=false;
        Effective(lab,candidate,executionRound,efficiencyMask,compactRound,shadowRound,sdfRound,reuseRound,architectureMask,modelUpdateMask,queryReuseMask,receiverMask);const auto source=Scopes(timing);
        for(unsigned s=0;s<ScopeCount;++s){Check(source[s]->size()<=1,"Duplicate native A/B GPU timing");for(double v:*source[s]){
                Check(std::isfinite(v)&&v>=0,"Invalid native A/B GPU timing");samples.gpu[s].push_back(v);}}
        Check(samples.gpu[0].size()==i+1,"Native A/B GPU frame timestamp missing");
        samples.wall[0].push_back(timing.elapsedMilliseconds);samples.wall[1].push_back(loop);
        samples.wall[2].insert(samples.wall[2].end(),timing.scenePreparationFrameMilliseconds.begin(),timing.scenePreparationFrameMilliseconds.end());
        samples.wall[3].insert(samples.wall[3].end(),timing.frameRecordingFrameMilliseconds.begin(),timing.frameRecordingFrameMilliseconds.end());
        samples.wall[4].insert(samples.wall[4].end(),timing.submitWaitFrameMilliseconds.begin(),timing.submitWaitFrameMilliseconds.end());
        const auto stats=lab.PipelineStatistics();samples.rays+=stats.surfaceCacheProbeRayBudget;samples.attributes+=stats.surfaceCacheAttributeUploadedTexels;
        samples.geometry+=stats.surfaceCacheGeometryUploadedTexels;samples.regions+=stats.surfaceCacheUploadRegions;samples.lit+=stats.surfaceCacheUpdatedTexels;
        samples.areaPasses+=stats.areaLightShadowPasses;samples.presents+=frame.present;samples.history+=stats.surfaceCacheHistoryUsed;
        samples.hardwareContact+=stats.surfaceCacheHardwareContact;samples.hardwareVisibility+=stats.surfaceCacheHardwareVisibility;
        samples.singleScreen+=stats.screenProbeSinglePassOutputs;samples.singleSdf+=stats.probeSdfSinglePassOutputs;samples.fastSdf+=stats.probeSdfFastAtlasAddressing;samples.reusedSdf+=stats.probeSdfReuseTraceSamples;
        samples.nativeProbeFrames+=stats.probeSdfHardwareRayTracing;samples.nativeOriginFrames+=stats.surfaceCacheHardwareOrigins;samples.radianceFilterFrames+=stats.surfaceCacheRadianceOnlyFiltering;
        samples.nativeUploadPrunedFrames+=stats.probeSdfNativeUploadsPruned;samples.reusedOriginCertificationFrames+=stats.surfaceCacheReusedOriginCertification;
        samples.softwareReceiverOriginFrames+=stats.surfaceCacheSoftwareReceiverOrigins;samples.asUpdateRequestedFrames+=frame.effects.surfaceCacheGi.updateAccelerationStructures;
        samples.asStorageBuildRequestedFrames+=frame.effects.surfaceCacheGi.reuseAccelerationStorage;
        samples.mainCandidateReuseFrames+=stats.probeSdfNativeCandidateReused;samples.closestTraversalReuseFrames+=stats.surfaceCacheClosestTraversalReused;
        samples.fastGeometryAddressingFrames+=stats.surfaceCacheFastGeometryAddressing;
        samples.directSoftwareRefinementFrames+=stats.probeSdfDirectSoftwareRefinement;
        samples.receiverOriginSplitFrames+=stats.surfaceCacheSplitReceiverOrigins;samples.receiverLookupReuseFrames+=stats.surfaceCacheReceiverLookupReused;
        samples.receiverFastLightingAddressingFrames+=stats.surfaceCacheReceiverFastLightingAddressing;
        samples.spatialFilterTexels+=stats.surfaceCacheSpatialFilterTexels;samples.spatialTargetPeakBytes=std::max(samples.spatialTargetPeakBytes,stats.surfaceCacheSpatialRayTargetBytes);
        if(executionRound){Check(timing.textureUploadStatistics.size()==1,"Missing same-frame texture upload statistics");
            const auto& uploads=timing.textureUploadStatistics.front();for(unsigned c=0;c<7;++c)samples.uploads[c]+=uploads[c];
            samples.uploads[7]=std::max(samples.uploads[7],uploads[7]);}
        if(compactRound||shadowRound||sdfRound||reuseRound){Check(timing.backendHostFrameMilliseconds.size()==1&&timing.backendHostFrameStatistics.size()==1&&
                timing.preparationDetailFrameMilliseconds.size()==1&&timing.frameQueueFrameMilliseconds.size()==1&&
                timing.renderThreadFrameMilliseconds.size()==1&&timing.pipelineHostFrameMilliseconds.size()==1&&
                timing.postRenderDiagnosticFrameMilliseconds.size()==1&&timing.diagnosticServiceFrameMilliseconds.size()==1&&
                timing.completionDeliveryFrameMilliseconds.size()==1,"Missing completed-frame host attribution");
            const auto push=[&](unsigned at,double value){Check(std::isfinite(value)&&value>=0,"Invalid completed-frame host attribution");samples.host[at].push_back(value);};
            for(unsigned c=0;c<14;++c)push(c,timing.backendHostFrameMilliseconds.front()[c]);
            for(unsigned c=0;c<3;++c)push(14+c,timing.preparationDetailFrameMilliseconds.front()[c]);
            push(17,timing.frameQueueFrameMilliseconds.front());push(18,timing.renderThreadFrameMilliseconds.front());
            for(unsigned c=0;c<2;++c)push(19+c,timing.pipelineHostFrameMilliseconds.front()[c]);
            push(21,timing.postRenderDiagnosticFrameMilliseconds.front());push(22,timing.diagnosticServiceFrameMilliseconds.front());
            push(23,timing.completionDeliveryFrameMilliseconds.front());
            for(unsigned c=0;c<HostCounterNames.size();++c)samples.hostCounters[c]+=timing.backendHostFrameStatistics.front()[c];
            if(stats.probeSdfCompactRays){Check(timing.probeSdfCompactCounters.size()==1,"Missing completed-frame GPU compaction counters");
                const auto& counters=timing.probeSdfCompactCounters.front();CompactControl(counters,stats.probeSdfCompactCapacity);
                for(unsigned c=0;c<16;++c)samples.compactCounters[c]+=counters[c];samples.compactCapacity+=stats.probeSdfCompactCapacity;++samples.compactFrames;}
            else {Check(timing.probeSdfCompactCounters.size()==1,"Missing baseline completion diagnostic record");
                for(auto value:timing.probeSdfCompactCounters.front())Check(value==0,"Full SDF baseline exposed stale GPU compaction counters");}}
        if(shadowMethod){Check(timing.uniformSnapshots.size()==1&&timing.shadowSubmission.size()==1,"Missing same-frame shadow submission diagnostics");
            const auto& snapshots=timing.uniformSnapshots.front();
            Check(snapshots[0]==std::uint64_t((sdfRound||reuseRound||candidate)&&bool(efficiencyMask&1)),"Requested uniform content snapshot path was not actually selected");
            for(unsigned c=0;c<9;++c)if(c==6)samples.snapshots[c]=std::max(samples.snapshots[c],snapshots[c]);else samples.snapshots[c]+=snapshots[c];
            const auto& shadows=timing.shadowSubmission.front();
            for(unsigned c=0;c<15;++c)if(c==1||c==13||c==14)samples.shadows[c]=std::max(samples.shadows[c],shadows[c]);else samples.shadows[c]+=shadows[c];
        }
        if(sdfRound||reuseRound)Check(timing.sdfKernelFrameMilliseconds.size()==1&&timing.sdfPackFrameMilliseconds.size()==1,
            "SDF efficiency measurement lost its kernel/pack GPU subscopes");
        Check(!lab.ProbeSdfQualityStatistics().pending,"Native A/B measured an unprepared SDF scene");};
    constexpr std::array<unsigned,4> order{{0,1,1,0}};
    for(unsigned trial=0;trial<order.size();++trial){const unsigned variant=order[trial];Mode(frame,variant!=0,executionRound,comparisonMask,compactRound,shadowRound,sdfRound,reuseRound,architectureMask,modelUpdateMask,queryReuseMask,receiverMask);Pose(frame,base,0,0,originalRadiance);Reset(frame);
        const auto coldBegin=std::chrono::steady_clock::now();Draw(lab,frame);
        const auto readyDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
        while(lab.ProbeSdfQualityStatistics().pending){Draw(lab,frame);Check(std::chrono::steady_clock::now()<readyDeadline,"Native A/B scene preparation timed out");}
        Effective(lab,variant!=0,executionRound,comparisonMask,compactRound,shadowRound,sdfRound,reuseRound,architectureMask,modelUpdateMask,queryReuseMask,receiverMask);
        std::cout<<tag<<" PERF cold variant="<<variantNames[variant]<<" trial="<<trial<<" unmeasured_ms="
            <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-coldBegin).count()<<" includes_compile_scene_and_as=1"<<std::endl;
        Reset(frame);for(unsigned f=0;f<warm;++f){glfwPollEvents();Draw(lab,frame);}
        for(unsigned path=0;path<PathNames.size();++path){Pose(frame,base,0,0,originalRadiance);
            if(path){Reset(frame);for(unsigned f=0;f<transitionWarm;++f){
                    if(executionRound)Pose(frame,base,path,f,originalRadiance);
                    glfwPollEvents();Draw(lab,frame);}}
            Samples samples;
            for(unsigned i=0;i<measured;++i)measure(samples,variant!=0,path,i,comparisonMask);
            append(total[variant][path],samples);report(samples,variant,path,int(trial));}
    }
    for(unsigned variant=0;variant<2;++variant)for(unsigned path=0;path<PathNames.size();++path)report(total[variant][path],variant,path,-1);
    for(unsigned path=0;path<PathNames.size();++path){const auto& a=total[0][path];const auto& b=total[1][path];
        Check(a.rays==b.rays&&a.lit==b.lit&&a.attributes==b.attributes&&a.geometry==b.geometry&&a.areaPasses==b.areaPasses,
            "Native A/B changed probe budget, cache update work, geometry uploads or area-shadow draw work");
        if(executionRound){Check((modelUpdateMask&2)||(a.uploads[0]==b.uploads[0]&&a.uploads[1]==b.uploads[1]),"Upload efficiency changed physical texture upload work");
            if(!compactRound&&!shadowRound&&!sdfRound&&!reuseRound){Check(a.uploads[2]==0,"Accepted upload baseline unexpectedly deferred uploads");
                if(path==2)Check(b.uploads[2]>0&&b.uploads[5]<a.uploads[5],"Model trajectory did not exercise deferred uploads and fewer dedicated fence waits");}
            else if(path==2)Check(a.uploads[2]>0&&b.uploads[2]>0&&a.uploads[5]==0&&b.uploads[5]==0,
                "Compaction comparison did not retain accepted deferred uploads in both groups");}
        if(compactRound){Check(a.hostCounters[3]==b.hostCounters[3],"Compaction/reuse comparison changed acceleration build work");
            Check(a.compactFrames==0&&b.compactFrames==2*measured,"Compaction comparison did not exercise the requested actual path in every frame");
            Check(a.hostCounters[4]==0,"Accepted acceleration baseline unexpectedly reused storage");
            if(path==2)Check(b.hostCounters[3]>0&&b.hostCounters[4]==b.hostCounters[3]&&b.hostCounters[5]==0&&b.hostCounters[6]==0,
                "Warmed fixed-topology model trajectory did not reuse all acceleration storage without new allocations/handles");}
        if(shadowRound){
            Check(a.snapshots[0]==0&&b.snapshots[0]==2*measured,"Shadow comparison did not select the requested snapshot mode in every measured frame");
            Check(a.hostCounters[3]==b.hostCounters[3]&&a.hostCounters[4]==0&&b.hostCounters[4]==0,
                "Shadow submission comparison changed acceleration build/reuse policy");
            Check(a.compactFrames==0&&b.compactFrames==0,"Shadow submission comparison unexpectedly enabled sparse SDF tracing");
            if(path==2){Check(a.shadows[2]==2*measured&&b.shadows[2]==2*measured&&a.shadows[4]==0&&b.shadows[4]==2*measured,
                    "Model trajectory did not refresh both atlases with candidate preparation");
                Check(a.shadows[7]==0&&b.shadows[7]>0&&b.shadows[6]<a.shadows[6]&&b.shadows[8]<a.shadows[8]&&b.shadows[9]<a.shadows[9],
                    "Shadow candidate did not exercise conservative culling and fewer caster uploads");
                Check(b.snapshots[2]>0&&b.snapshots[5]<a.snapshots[5],"Shadow snapshot candidate did not reuse content or reduce graphics copy bytes");
                Check(a.shadows[11]==a.shadows[2]&&a.shadows[12]==0&&b.shadows[11]==0&&b.shadows[12]==b.shadows[2],
                    "Warmed shadow atlas pool still allocated or reused an invalid baseline target");}
        }
        if((sdfRound||reuseRound)&&!modelUpdateMask&&!queryReuseMask){
            Check(architectureNative||(a.fastSdf==(reuseRound?2*measured:0)&&b.fastSdf==2*measured),"SDF efficiency comparison did not select the actual addressing mode for every frame");
            Check(architectureMask? (a.reusedSdf==2*measured&&(architectureNative||b.reusedSdf==2*measured)) : (a.reusedSdf==0&&b.reusedSdf==(reuseRound?2*measured:0)),"SDF reuse comparison did not select the actual shader variant for every frame");
            if(reuseRound&&!architectureMask)for(unsigned c=0;c<3;++c)Check(a.hostCounters[c]==b.hostCounters[c],"SDF sample reuse changed graphics draw/descriptor/uniform copy work");
            Check(a.snapshots[0]==2*measured&&b.snapshots[0]==2*measured&&a.compactFrames==0&&b.compactFrames==0,
                "SDF efficiency comparison lost the accepted snapshot policy or enabled compact tracing");
            for(unsigned c=3;c<7;++c)Check(a.hostCounters[c]==b.hostCounters[c],"SDF efficiency changed acceleration work/storage policy");
            Check(a.hostCounters[4]==0,"SDF efficiency baseline unexpectedly reused acceleration storage");
            for(unsigned c=0;c<11;++c)Check(a.shadows[c]==b.shadows[c],"SDF efficiency changed accepted shadow caster/pass/parameter work");
            Check(architectureMask||a.snapshots[5]==b.snapshots[5],"SDF efficiency changed graphics uniform copy bytes");
            if(path==2)Check(a.shadows[4]==2*measured&&b.shadows[4]==2*measured&&a.shadows[11]==0&&b.shadows[11]==0&&
                a.shadows[12]==2*measured&&b.shadows[12]==2*measured,"SDF efficiency did not retain warmed atlas preparation/storage reuse in both groups");
        }
        if(modelUpdateMask){
            const auto count=2*measured;
            Check(a.nativeProbeFrames==count&&b.nativeProbeFrames==count&&a.nativeOriginFrames==count&&b.nativeOriginFrames==count&&
                a.radianceFilterFrames==count&&b.radianceFilterFrames==count,"Model update changed accepted Round7 native/filter modes");
            Check(a.spatialFilterTexels==b.spatialFilterTexels&&a.spatialTargetPeakBytes==b.spatialTargetPeakBytes,
                "Model update changed filter capacity/storage");
            Check(a.nativeUploadPrunedFrames==0&&b.nativeUploadPrunedFrames==((modelUpdateMask&2)?count:0)&&
                a.reusedOriginCertificationFrames==0&&b.reusedOriginCertificationFrames==((modelUpdateMask&4)?count:0)&&
                a.softwareReceiverOriginFrames==0&&b.softwareReceiverOriginFrames==((modelUpdateMask&8)?count:0)&&
                a.asUpdateRequestedFrames==0&&b.asUpdateRequestedFrames==((modelUpdateMask&1)?count:0),"Model update execution controls were not exercised");
            Check(a.asStorageBuildRequestedFrames==0&&b.asStorageBuildRequestedFrames==((modelUpdateMask&16)?count:0),"Retained AS BUILD policy was not selected");
            Check(a.hostCounters[0]==b.hostCounters[0]&&a.hostCounters[3]==b.hostCounters[3],"Model update changed graphics draws or AS scene refresh count");
            for(unsigned c=0;c<11;++c)Check(a.shadows[c]==b.shadows[c],"Model update changed accepted shadow caster/pass work");
            if(path==2){
                Check(a.hostCounters[7]==count&&a.hostCounters[8]==0,"Model update baseline did not use full AS BUILD");
                if(modelUpdateMask&1)Check(b.hostCounters[7]==0&&b.hostCounters[8]==count&&b.hostCounters[4]==count&&
                    b.hostCounters[5]==0&&b.hostCounters[6]==0,"Warmed fixed-topology candidate did not UPDATE retained AS without allocation");
                else if(modelUpdateMask&16)Check(b.hostCounters[7]==count&&b.hostCounters[8]==0&&b.hostCounters[4]==count&&
                    b.hostCounters[5]==0&&b.hostCounters[6]==0,"Warmed retained BUILD still allocated acceleration storage");
                else Check(b.hostCounters[7]==a.hostCounters[7]&&b.hostCounters[8]==0&&b.hostCounters[4]==0,"Unselected AS policy changed");
                if(modelUpdateMask&2)Check(b.uploads[1]<a.uploads[1],"Native upload pruning did not reduce physical moved-scene upload bytes");
            }
        }
        if(queryReuseMask&&!receiverMask){
            const auto count=2*measured;
            Check(a.nativeProbeFrames==count&&b.nativeProbeFrames==((queryReuseMask&8)?0:count)&&
                a.directSoftwareRefinementFrames==0&&b.directSoftwareRefinementFrames==((queryReuseMask&8)?count:0)&&
                a.nativeOriginFrames==count&&b.nativeOriginFrames==count&&a.radianceFilterFrames==count&&b.radianceFilterFrames==count,
                "Query reuse changed accepted main triangle profile/native origins/filter modes");
            Check(a.nativeUploadPrunedFrames==count&&b.nativeUploadPrunedFrames==count&&a.asUpdateRequestedFrames==count&&b.asUpdateRequestedFrames==count&&
                a.reusedOriginCertificationFrames==0&&b.reusedOriginCertificationFrames==0&&a.softwareReceiverOriginFrames==0&&b.softwareReceiverOriginFrames==0&&
                a.asStorageBuildRequestedFrames==0&&b.asStorageBuildRequestedFrames==0,"Query reuse changed accepted Round8 resource/origin policies");
            Check(a.mainCandidateReuseFrames==0&&b.mainCandidateReuseFrames==((queryReuseMask&1)?count:0)&&
                a.closestTraversalReuseFrames==0&&b.closestTraversalReuseFrames==((queryReuseMask&2)?count:0)&&
                a.fastGeometryAddressingFrames==0&&b.fastGeometryAddressingFrames==((queryReuseMask&4)?count:0),"Query reuse execution selections were not exercised");
            Check(a.history==b.history&&a.spatialFilterTexels==b.spatialFilterTexels&&a.spatialTargetPeakBytes==b.spatialTargetPeakBytes,
                "Query reuse changed history/filter work or storage");
            for(unsigned c=0;c<HostCounterNames.size();++c){
                if((queryReuseMask&8)&&c==1)Check(b.hostCounters[c]+count==a.hostCounters[c],
                    "Direct refinement did not remove exactly one AS descriptor allocation per measured frame");
                else Check(a.hostCounters[c]==b.hostCounters[c],"Query reuse changed graphics/AS resource work");
            }
            for(unsigned c=0;c<7;++c)Check(a.uploads[c]==b.uploads[c],"Query reuse changed physical upload work");
            for(unsigned c=0;c<15;++c)Check(a.shadows[c]==b.shadows[c],"Query reuse changed accepted shadow work");
            if(path==2)Check(a.hostCounters[7]==0&&a.hostCounters[8]==count&&a.hostCounters[4]==count&&a.hostCounters[5]==0&&a.hostCounters[6]==0,
                "Query reuse did not retain warmed Round8 AS UPDATE without allocation");
        }
        if(receiverMask){
            const auto count=2*measured;const unsigned split=(receiverMask&1)?1u:0u;
            Check(a.nativeProbeFrames==0&&b.nativeProbeFrames==0&&a.directSoftwareRefinementFrames==count&&b.directSoftwareRefinementFrames==count&&
                a.nativeOriginFrames==count&&b.nativeOriginFrames==count&&a.radianceFilterFrames==count&&b.radianceFilterFrames==count,
                "Receiver optimization changed accepted Round9 main/origin/filter modes");
            Check(a.nativeUploadPrunedFrames==count&&b.nativeUploadPrunedFrames==count&&a.asUpdateRequestedFrames==count&&b.asUpdateRequestedFrames==count&&
                a.reusedOriginCertificationFrames==0&&b.reusedOriginCertificationFrames==0&&a.softwareReceiverOriginFrames==0&&b.softwareReceiverOriginFrames==((receiverMask&8)?count:0)&&
                a.asStorageBuildRequestedFrames==0&&b.asStorageBuildRequestedFrames==0,"Receiver optimization changed accepted resource/origin policies");
            Check(a.mainCandidateReuseFrames==0&&b.mainCandidateReuseFrames==0&&a.closestTraversalReuseFrames==0&&b.closestTraversalReuseFrames==0&&
                a.fastGeometryAddressingFrames==count&&b.fastGeometryAddressingFrames==count,"Receiver optimization changed accepted Round9 query policy");
            Check(a.receiverOriginSplitFrames==0&&b.receiverOriginSplitFrames==split*count&&a.receiverLookupReuseFrames==0&&
                b.receiverLookupReuseFrames==((receiverMask&2)?count:0)&&a.receiverFastLightingAddressingFrames==0&&
                b.receiverFastLightingAddressingFrames==((receiverMask&4)?count:0),"Receiver execution selections were not exercised");
            Check(a.history==b.history&&a.spatialFilterTexels==b.spatialFilterTexels&&a.spatialTargetPeakBytes==b.spatialTargetPeakBytes,
                "Receiver optimization changed history/filter work or storage");
            for(unsigned c=0;c<HostCounterNames.size();++c){
                const unsigned extra=split*(c==0?1u:c==1?3u:0u);
                Check(b.hostCounters[c]==a.hostCounters[c]+extra*count,"Receiver optimization violated its exact disclosed draw/descriptor/AS plan");
            }
            for(unsigned c=0;c<7;++c)Check(a.uploads[c]==b.uploads[c],"Receiver optimization changed physical upload work");
            for(unsigned c=0;c<15;++c)Check(a.shadows[c]==b.shadows[c],"Receiver optimization changed accepted shadow work");
            if(path==2)Check(a.hostCounters[7]==0&&a.hostCounters[8]==count&&a.hostCounters[4]==count&&a.hostCounters[5]==0&&a.hostCounters[6]==0,
                "Receiver optimization lost warmed AS UPDATE without allocation");
        }
        if(architectureMask){Check(a.nativeProbeFrames==0&&a.nativeOriginFrames==0&&a.radianceFilterFrames==0,"Architecture baseline enabled candidate work");
            Check(b.nativeProbeFrames==((architectureMask&1)?2*measured:0)&&b.nativeOriginFrames==((architectureMask&1)?2*measured:0)&&
                b.radianceFilterFrames==((architectureMask&2)?2*measured:0),"Architecture candidate did not run every requested feature in every measured frame");
            Check((architectureMask&2)?b.spatialFilterTexels*4==a.spatialFilterTexels:b.spatialFilterTexels==a.spatialFilterTexels,
                "Architecture changed spatial filter fragment capacity outside its planned atlas split");}
        std::cout<<tag<<" PERF comparison path="<<PathNames[path]<<" gpu_mean_before="<<Mean(a.gpu[0])<<" gpu_mean_after="<<Mean(b.gpu[0])
            <<" gpu_mean_delta_ms="<<Mean(b.gpu[0])-Mean(a.gpu[0])<<" render_loop_mean_before="<<Mean(a.wall[1])<<" render_loop_mean_after="<<Mean(b.wall[1])
            <<" paired_work_exact="<<(receiverMask?!(receiverMask&1):(!modelUpdateMask&&!(queryReuseMask&8)))<<" estimator_work_exact=1 planned_model_resource_changes="<<bool(modelUpdateMask)
            <<" planned_query_descriptor_changes="<<(!receiverMask&&bool(queryReuseMask&8))
            <<" planned_receiver_origin_pass="<<bool(receiverMask&1)<<" planned_receiver_draw_delta_per_frame="<<((receiverMask&1)?1:0)
            <<" planned_receiver_descriptor_delta_per_frame="<<((receiverMask&1)?3:0)<<" diagnostic_speedup_not_asserted=1"<<std::endl;}
    std::cout<<tag<<" PERF PASS trials_per_mode=2 samples_per_mode_path="<<2*measured<<" paths=5 present="<<frame.present
        <<" completed_present_calls="<<(frame.present?order.size()*PathNames.size()*measured:0)<<" measured_readbacks=0 physical_scanout_fps_claim=0"<<std::endl;
    if(executionRound&&!sdfRound&&!reuseRound){
        // Attribution only: the primary before/after numbers above remain the
        // balanced two-mode ABBA. These two separately warmed model trajectories
        // identify trace versus upload cost, with fewer samples and no CI claim.
        const std::vector<std::pair<const char*,unsigned>> cases=shadowRound?
            std::vector<std::pair<const char*,unsigned>>{{"snapshot_only",1},{"caster_preparation_only",2},{"atlas_storage_only",4},{"caster_culling_only",8}}:
            compactRound?std::vector<std::pair<const char*,unsigned>>{{"compact_only",1},{"reuse_only",2}}:
            std::vector<std::pair<const char*,unsigned>>{{"trace_only",1},{"upload_only",2}};
        constexpr unsigned diagnosticFrames=16;
        std::cout<<tag<<" ABLATION setup path=model cases="<<cases.size()<<" measured_per_case="<<diagnosticFrames
            <<" warm_per_case="<<warm+transitionWarm<<" warm_static_per_case="<<warm<<" warm_model_per_case="<<transitionWarm
            <<" separate_from_primary_abba=1 confidence_interval=0"<<std::endl;
        for(unsigned test=0;test<cases.size();++test){const unsigned mask=cases[test].second;
            Mode(frame,true,true,mask,compactRound,shadowRound,sdfRound,reuseRound,architectureMask,modelUpdateMask,queryReuseMask,receiverMask);Pose(frame,base,0,0,originalRadiance);Reset(frame);
            for(unsigned f=0;f<warm+transitionWarm;++f){
                Pose(frame,base,f<warm?0u:2u,f<warm?f:f-warm,originalRadiance);
                glfwPollEvents();Draw(lab,frame);}
            Samples samples;for(unsigned i=0;i<diagnosticFrames;++i)measure(samples,true,2,i,mask);
            report(samples,1,2,-2,cases[test].first);
        }
        std::cout<<tag<<" ABLATION PASS measured_present_calls="<<(frame.present?cases.size()*diagnosticFrames:0)
            <<" primary_abba_samples_unchanged=1 work_equal_to_primary_not_asserted=1"<<std::endl;
    }
    // A separate, explicitly untimed ordinary-renderer sequence. No reference,
    // geometry oracle or receiver diagnostic replay runs in these HD captures.
    if(!options.screenshot.empty()) {
        auto preview=Frame(options,false);preview.width=1280;preview.height=800;preview.present=false;preview.effects.outputSize={0,0};
        preview.effects.surfaceCacheGi.contact.collectSamplingDiagnostics=false;preview.screenProbeStudyHud=false;
        const auto camera=SurfaceCacheFreshSamplingCamera(2,preview.OutputAspect(),true);
        FramePixels softwareStatic;unsigned rendererReadbacks=0,stageReadbacks=0;std::uint64_t staticDifferentPixels=0,staticDifferentChannels=0;unsigned staticMaximum=0;
        constexpr std::array<unsigned,7> checkpoints{{64,65,72,81,88,97,112}};
        std::cout<<tag<<" PREVIEW setup size=1280x800 static=64 continuous_model=32 settle=16 timed=0 cpu_oracle=0 sampling_diagnostic_replay=0 fps_claim=0"
            <<" extra_canonical_warm_per_mode="<<(reuseRound?64:0)<<" history_reset_after_extra_warm="<<reuseRound
            <<" canonical_original_radiance_and_pose="<<reuseRound<<std::endl;
        for(unsigned variant=0;variant<2;++variant){Mode(preview,variant!=0,executionRound,comparisonMask,compactRound,shadowRound,sdfRound,reuseRound,architectureMask,modelUpdateMask,queryReuseMask,receiverMask);preview.camera=camera;preview.screenProbeStudyState.occluderOffset=0;Reset(preview);
            if(reuseRound){
                // Both modes enter the counted sequence from the same stable
                // emitter/pose state; these extra frames are untimed and unsaved.
                const auto canonicalRadiance=Frame(options,false).effects.areaLights.lights[0].radiance;
                Pose(preview,camera,0,0,canonicalRadiance);
                for(unsigned f=0;f<64;++f){glfwPollEvents();Draw(lab,preview);}
                Effective(lab,variant!=0,executionRound,comparisonMask,compactRound,shadowRound,sdfRound,reuseRound,architectureMask,modelUpdateMask,queryReuseMask,receiverMask);Reset(preview);
                std::cout<<tag<<" PREVIEW canonical_warm variant="<<variantNames[variant]<<" extra_frames=64 original_radiance=1 original_pose=1 reset_after_warm=1 timed=0 readbacks=0"<<std::endl;
            }
            for(unsigned f=0;f<112;++f){preview.screenProbeStudyState.occluderOffset=0;
                if(f>=64&&f<96){const unsigned cycle=(f-64)%16,step=cycle<8?cycle+1:15-cycle;preview.screenProbeStudyState.occluderOffset=.15f*float(step);}
                glfwPollEvents();Draw(lab,preview);Effective(lab,variant!=0,executionRound,comparisonMask,compactRound,shadowRound,sdfRound,reuseRound,architectureMask,modelUpdateMask,queryReuseMask,receiverMask);
                if(std::find(checkpoints.begin(),checkpoints.end(),f+1)==checkpoints.end())continue;
                const auto label=std::string("preview_")+variantNames[variant]+"_frame"+std::to_string(f+1);
                const auto image=lab.Readback();++rendererReadbacks;Save(image,OutputPath(options,label+"_total_renderer_ldr"));
                const auto contact=lab.ReadbackSurfaceCacheContactGi(),temporal=lab.ReadbackSurfaceCacheTemporalGi();stageReadbacks+=2;
                Finite(contact,"HD preview contact");Finite(temporal,"HD preview temporal");
                Tone(options,preview.width,preview.height,contact,label+"_contact_tone4");Tone(options,preview.width,preview.height,temporal,label+"_temporal_tone4");
                if(f==63){if(!variant)softwareStatic=image;else {
                        Check(image.width==softwareStatic.width&&image.height==softwareStatic.height&&image.rgba.size()==softwareStatic.rgba.size(),"Native HD preview static extent changed");
                        for(std::size_t i=0;i<image.rgba.size();i+=4){bool changed=false;for(unsigned c=0;c<4;++c){const unsigned a=softwareStatic.rgba[i+c],b=image.rgba[i+c];
                                const unsigned difference=a>b?a-b:b-a;staticMaximum=std::max(staticMaximum,difference);staticDifferentChannels+=difference>0;changed=changed||difference>0;}
                            staticDifferentPixels+=changed;}}}
                std::cout<<tag<<" PREVIEW image variant="<<variantNames[variant]<<" frame="<<f+1<<" hardware_contact="<<lab.PipelineStatistics().surfaceCacheHardwareContact
                    <<" hardware_visibility="<<lab.PipelineStatistics().surfaceCacheHardwareVisibility<<" path=\""<<OutputPath(options,label+"_total_renderer_ldr")<<"\" timed=0"<<std::endl;}}
        std::cout<<tag<<" PREVIEW static_compare frame=64 different_pixels="<<staticDifferentPixels<<" different_channels="<<staticDifferentChannels
            <<" maximum_u8_channel_difference="<<staticMaximum<<" compared_pixels="<<std::uint64_t(preview.width)*preview.height<<" diagnostic_only=1 pixel_perfect_assertion=0"<<std::endl;
        // Restore this round's baseline at an explicit history reset.
        Mode(preview,false,executionRound,comparisonMask,compactRound,shadowRound,sdfRound,reuseRound,architectureMask,modelUpdateMask,queryReuseMask,receiverMask);preview.camera=camera;preview.screenProbeStudyState.occluderOffset=0;Reset(preview);Draw(lab,preview);
        std::cout<<tag<<" PREVIEW PASS renderer_readbacks="<<rendererReadbacks<<" stage_readbacks="<<stageReadbacks
            <<" atlas_readbacks=0 cpu_oracle=0 sampling_diagnostic_replay=0 timing_samples=0 no_fps_claim=1 restored_baseline_mode=1"
            <<" restored_software_mode="<<!executionRound<<std::endl;
    }
}

// Numerical parity against the already accepted software image is the quality
// target here, not a new independent physical-lighting reference. Geometry ROIs
// use the double-precision triangle oracle, never a production retrace mask.
void SurfaceCacheNativeQuality(MaterialLabModule& lab,const Options& options,bool efficiencyRound=false,bool compactRound=false,bool shadowRound=false,bool sdfRound=false,bool reuseRound=false,unsigned architectureMask=0,unsigned modelUpdateMask=0,unsigned queryReuseMask=0,unsigned receiverMask=0) {
    using namespace SurfaceCacheNativeFixture;
    if(receiverMask){ReceiverVariant(receiverMask);queryReuseMask=12;}
    reuseRound=reuseRound||architectureMask!=0||modelUpdateMask!=0||queryReuseMask!=0;
    const bool architectureNative=bool(architectureMask&1)||modelUpdateMask!=0||queryReuseMask!=0,strictArchitecture=!architectureNative;
    const bool executionRound=efficiencyRound||compactRound||shadowRound||sdfRound||reuseRound;
    const bool shadowMethod=shadowRound||sdfRound||reuseRound;
    const unsigned comparisonMask=shadowMethod?15u:3u;
    const auto& variantNames=receiverMask?std::array<const char*,2>{{"accepted_round9",ReceiverVariant(receiverMask)}}:queryReuseMask?std::array<const char*,2>{{"accepted_round8",QueryReuseVariant(queryReuseMask)}}:modelUpdateMask?std::array<const char*,2>{{"accepted_round7",ModelUpdateVariant(modelUpdateMask)}}:architectureMask?std::array<const char*,2>{{"accepted_round6",architectureMask==1?"native_probes_origins":architectureMask==2?"radiance_filter":"native_and_radiance"}}:reuseRound?std::array<const char*,2>{{"accepted_sdf_addressing","optimized_sdf_reuse"}}:sdfRound?std::array<const char*,2>{{"accepted_shadow_bundle","optimized_sdf_addressing"}}:shadowRound?std::array<const char*,2>{{"accepted_default_shadow","optimized_shadow_submission"}}:compactRound?std::array<const char*,2>{{"accepted_mrt_deferred","optimized_compact_reuse"}}:executionRound?std::array<const char*,2>{{"accepted_native","optimized_trace_upload"}}:VariantNames;
    const char* tag=receiverMask?"RECEIVER GI":queryReuseMask?"QUERY REUSE GI":modelUpdateMask?"MODEL UPDATE GI":architectureMask?"ARCHITECTURE GI":reuseRound?"SDF REUSE GI":sdfRound?"SDF EFFICIENCY GI":shadowRound?"SHADOW SUBMISSION GI":compactRound?"COMPACTION GI":executionRound?"EFFICIENCY GI":"NATIVE GI";
    using SurfaceCacheGeometryFixture::Oracle;
    Check(options.vulkan&&!options.softwareRays,"Native parity requires capability-enabled Vulkan without --software-rays");
    auto frame=Frame(options,true);const auto accepted=frame.effects.surfaceCacheGi;
    const auto placement=frame.effects.screenProbePlacement;const auto pixels=std::size_t(frame.width)*frame.height;
    constexpr unsigned frames=112,stages=2,regions=4,phases=5;
    constexpr std::array<const char*,stages> stageNames{{"contact_filtered","temporal"}};
    constexpr std::array<const char*,regions> regionNames{{"all_geometry","contact_0to70cm","moving_cube","cube_left_face"}};
    constexpr std::array<const char*,phases> phaseNames{{"static64","continuous16","separation_hold8","return_hold8","settle16"}};
    const auto& objects=SealedGiRoomObjects();std::vector<SealedGiRoomMaterial> materials;
    unsigned cube=unsigned(objects.size()),oval=cube,prism=cube,lowOval=cube;
    for(unsigned i=0;i<objects.size();++i){materials.push_back(SealedGiRoomObjectMaterial(i));
        if(objects[i].movable)cube=i;if(std::string(objects[i].name)=="Porcelain upright oval")oval=i;
        if(std::string(objects[i].name)=="Porcelain tall prism")prism=i;if(std::string(objects[i].name)=="Porcelain low oval")lowOval=i;}
    Check(cube<objects.size()&&oval<objects.size()&&prism<objects.size()&&lowOval<objects.size(),"Native parity lacks its contact geometry");
    const auto boxDistance=[](glm::vec3 p,const glm::mat4& inverse,glm::vec3 scale){const auto q=glm::abs(glm::vec3(inverse*glm::vec4(p,1)))*scale-scale;
        return double(glm::length(glm::max(q,glm::vec3(0)))+std::min(std::max(q.x,std::max(q.y,q.z)),0.f));};
    const auto ovalDistance=[](glm::vec3 p,const glm::mat4& inverse,glm::vec3 scale){return double((glm::length(glm::vec3(inverse*glm::vec4(p,1)))-1)*std::min({scale.x,scale.y,scale.z}));};
    const auto prismInverse=glm::inverse(ProbePlacementTransform(objects[prism],{})),ovalInverse=glm::inverse(ProbePlacementTransform(objects[oval],{}));
    const auto lowOvalInverse=glm::inverse(ProbePlacementTransform(objects[lowOval],{}));
    const auto cubeInverse=glm::inverse(ProbePlacementTransform(objects[cube],{}));
    const auto leftNormal=glm::normalize(glm::vec3(glm::transpose(cubeInverse)*glm::vec4(-1,0,0,0)));
    const glm::dvec3 luma(.2126,.7152,.0722);
    struct Geometry {std::uint64_t key=0;std::vector<std::uint8_t> roi,band;std::vector<glm::vec3> world,normal;};
    struct Snapshot {std::array<std::vector<glm::vec4>,stages> image;std::vector<glm::vec4> atlas;
        std::array<std::vector<glm::vec4>,2> packed;
        std::vector<std::array<std::uint8_t,4>> samplingDebug;
        std::array<std::uint64_t,13> signature{};
        std::array<std::uint64_t,4> shadowSignature{};std::vector<float> shadowDepth;
        unsigned geometry=0;std::uint64_t rays=0,readable=0,fresh=0,moving=0;};
    struct Error {double square=0,energy=0,maximum=0,gradientBefore=0,gradientAfter=0,gradientDifference=0,coreBefore=0,coreAfter=0,shellBefore=0,shellAfter=0,historyResidualBefore=0,historyResidualAfter=0;
        std::uint64_t count=0,pairs=0,core=0,shell=0,exact=0;};
    const auto mean=[](double sum,std::uint64_t count){return sum/double(std::max<std::uint64_t>(1,count));};
    const auto phaseAt=[](unsigned f){return f<64?0u:f<80?1u:f<88?2u:f<96?3u:4u;};
    const auto captureAtlas=[](unsigned f){return f==0||f==63||(f>=64&&f<96)||f==111;};
    std::uint64_t exactGeometryFrames=0,coverageExactPixels=0,metadataEntries=0,totalSelected=0,totalMoving=0,totalRays=0,blackPixels=0,unknownPixels=0;
    double worstRelativeRmse=0,atlasSquare=0,atlasEnergy=0,atlasMax=0,packedMaximumError=0;
    std::uint64_t atlasCount=0,packedTexels=0,packedExactMetadataChannels=0;
    std::uint64_t compactIdentityFrames=0,compactIdentityRays=0;
    std::uint64_t nativeOracleRays=0,nativeOracleHits=0,nativeOracleMisses=0;
    std::uint64_t nativeOracleScreenIntervals=0,nativeOracleMeshIntervals=0,nativeOracleGlobalIntervals=0;
    std::uint64_t nativeOracleEmptyIntervals=0,nativeOracleCoverageLimitedMisses=0;
    double nativeOracleMaximumDistanceError=0,nativeOracleMaximumEndpointError=0;
    std::uint64_t shadowSignatureFrames=0,shadowDepthFrames=0,shadowDepthPixels=0;
    constexpr std::array<unsigned,7> shadowCheckpoints{{64,65,72,81,88,97,112}};
    std::uint32_t compactMinimumPending=std::numeric_limits<std::uint32_t>::max(),compactMaximumPending=0;
    const auto comparePacked=[&](const auto& before,const auto& after,unsigned view,unsigned f,unsigned kind){
        Check(before.size()==after.size()&&!before.empty()&&before.size()%4==0,"Probe efficiency changed packed trace allocation");
        packedTexels+=before.size();
        for(std::size_t texel=0;texel<before.size();++texel)for(unsigned channel=0;channel<4;++channel){
            const double a=before[texel][channel],b=after[texel][channel];
            Check(std::isfinite(a)&&std::isfinite(b),"Probe efficiency packed trace became nonfinite");
            const bool exact=(texel%4==0&&channel==3)||texel%4==2||(texel%4==3&&channel==3);
            const double error=std::abs(a-b),tolerance=exact?0.:16.*std::numeric_limits<float>::epsilon()*std::max({1.,std::abs(a),std::abs(b)});
            if(error>tolerance)std::cout<<tag<<" QUALITY packed_mismatch view="<<view<<" frame="<<f+1<<" kind="<<(kind?"sdf":"screen")
                <<" texel="<<texel<<" channel="<<channel<<" before="<<a<<" after="<<b<<" tolerance="<<tolerance<<std::endl;
            Check(error<=tolerance,"Probe efficiency changed exact status/metadata or exceeded its FP32 trace envelope");
            if(shadowMethod&&strictArchitecture)Check(error==0,"Shadow submission changed exact packed ray output");
            packedMaximumError=std::max(packedMaximumError,error);packedExactMetadataChannels+=exact;
        }};
    // Reuse the existing view selector for focused reproduction: 0/1 select
    // the contact camera, 2 selects the moving receiver, default 3 runs both.
    const unsigned beginView=options.surfaceCacheDynamicView<3?(options.surfaceCacheDynamicView==2?1u:0u):0u;
    const unsigned endView=options.surfaceCacheDynamicView<3?beginView+1:2u;
    for(unsigned view=beginView;view<endView;++view){const auto base=SurfaceCacheFreshSamplingCamera(view?2:1,frame.OutputAspect(),view!=0);
        std::vector<Snapshot> baseline;baseline.reserve(frames);std::vector<Geometry> geometry;
        std::vector<std::pair<float,std::shared_ptr<Oracle>>> oracles;
        const auto oracleAt=[&](float offset){for(const auto& value:oracles)if(value.first==offset)return value.second;
            auto posed=objects;posed[cube].position.x+=offset;auto oracle=std::make_shared<Oracle>();oracle->Initialize(posed,materials);
            oracles.emplace_back(offset,oracle);return oracle;};
        const auto poseAt=[&](unsigned f){frame.camera=base;frame.screenProbeStudyState.occluderOffset=0;
            if(f>=64&&f<80){const unsigned cycle=f-64,step=cycle<8?cycle+1:15-cycle;
                if(view)frame.screenProbeStudyState.occluderOffset=.15f*float(step);
                else {const auto inverse=glm::inverse(base.view);const auto right=glm::normalize(glm::vec3(inverse[0])),forward=glm::normalize(-glm::vec3(inverse[2]));
                    auto camera=base;camera.position+=right*(.025f*float(step));
                    const auto direction=glm::vec3(glm::rotate(glm::mat4(1),.006f*float(step),glm::vec3(0,1,0))*glm::vec4(forward,0));
                    camera.view=glm::lookAt(camera.position,camera.position+direction,glm::vec3(0,1,0));frame.camera=camera;}}
            if(f>=80&&f<88)frame.screenProbeStudyState.occluderOffset=1.2f;};
        const auto geometryAt=[&](const ScreenProbeTraceReadback& sources){const std::uint64_t key=Hash(sources.sourceViewPositions)^(Hash(sources.sourceWorldNormals)<<1);
            for(unsigned i=0;i<geometry.size();++i)if(geometry[i].key==key)return i;
            Geometry value;value.key=key;value.roi.assign(pixels,0);value.band.assign(pixels,0);value.world.resize(pixels);value.normal.resize(pixels);
            const auto oracle=oracleAt(frame.screenProbeStudyState.occluderOffset);const auto inverse=glm::dmat4(glm::inverse(frame.camera->view));
            ProbePlacementSceneSettings moving;moving.occluderOffset=frame.screenProbeStudyState.occluderOffset;
            const auto movingCubeInverse=glm::inverse(ProbePlacementTransform(objects[cube],moving));
            for(std::size_t i=0;i<pixels;++i){const auto vp=sources.sourceViewPositions[i],nd=sources.sourceWorldNormals[i];
                if(vp.w<.5f||nd.w<.5f||glm::dot(glm::vec3(nd),glm::vec3(nd))<.5f)continue;
                const auto world=glm::dvec3(inverse*glm::dvec4(glm::dvec3(vp),1)),ray=world-glm::dvec3(frame.camera->position);const double length=glm::length(ray);
                if(length<=0)continue;const auto hit=oracle->Nearest(glm::dvec3(frame.camera->position),ray/length,length+.005);
                if(hit.triangle<0||std::abs(hit.t-length)>.003)continue;const auto owner=oracle->triangles[unsigned(hit.triangle)].object;
                const auto p=glm::vec3(world),n=glm::normalize(glm::vec3(nd));value.world[i]=p;value.normal[i]=n;unsigned roi=1;
                double gap=-1;if(owner==oval)gap=boxDistance(p,prismInverse,objects[prism].scale);
                else if(owner==prism)gap=ovalDistance(p,ovalInverse,objects[oval].scale);
                if(owner==lowOval)gap=boxDistance(p,movingCubeInverse,objects[cube].scale);
                if(owner==cube)gap=ovalDistance(p,lowOvalInverse,objects[lowOval].scale);
                if(owner==0&&n.y>.99f)gap=boxDistance(p,movingCubeInverse,objects[cube].scale);
                if(gap>=-.015&&gap<.70){roi|=2;value.band[i]=gap<.03?1:gap<.35?2:3;}
                if(owner==cube){roi|=4;if(glm::dot(n,leftNormal)>.999f)roi|=8;}value.roi[i]=std::uint8_t(roi);}
            geometry.push_back(std::move(value));return unsigned(geometry.size()-1);};
        const auto sample=[&](unsigned f){Snapshot s;s.image={lab.ReadbackSurfaceCacheContactGi(),lab.ReadbackSurfaceCacheTemporalGi()};
            for(unsigned stage=0;stage<stages;++stage){Finite(s.image[stage],stageNames[stage]);Check(s.image[stage].size()==pixels,"Native parity image size changed");}
            if(captureAtlas(f)){s.atlas=lab.ReadbackSurfaceCacheAtlas();Finite(s.atlas,"card atlas");}
            auto rays=lab.ReadbackScreenProbeTrace(true,false,executionRound);const auto risk=lab.ReadbackSurfaceCacheContactProbeRisk();
            ScreenProbeTraceReadback screenRays;
            if(executionRound){screenRays=lab.ReadbackScreenProbeTrace(false,true,true);s.packed[1]=std::move(rays.packed);}
            const auto mask=lab.ReadbackSurfaceCacheContactMask(),fresh=lab.ReadbackSurfaceCacheFreshSampling(),sampling=lab.ReadbackSurfaceCacheReceiverSampling();
            const auto origins=lab.ReadbackSurfaceCacheProbeOrigins();const auto probes=lab.ReadbackScreenProbePlacement();
            if(architectureNative&&(lab.PipelineStatistics().probeSdfHardwareRayTracing||lab.PipelineStatistics().probeSdfDirectSoftwareRefinement)){
                // Rotating original ray IDs give every direction class coverage
                // without making this untimed CPU test a second full renderer.
                // This BVH is built independently from double-precision scene
                // triangles, not from the production packed geometry/query.
                const auto oracle=oracleAt(frame.screenProbeStudyState.occluderOffset);
                for(std::size_t ray=f%64;ray<rays.radianceStatus.size();ray+=64){
                    const auto probe=ray/64;if(probe>=origins.size()||origins[probe].w<=0)continue;
                    const auto originalStatus=screenRays.packed[ray*4].w;
                    if(originalStatus<=0)continue;
                    const auto direction=glm::dvec3(rays.rayDirectionDepthFetches[ray]);
                    if(glm::dot(direction,direction)<.5)continue;
                    const auto position=glm::dvec3(origins[probe]);
                    const auto n=probe<probes.uniformNormals.size()?probes.uniformNormals[probe]:
                        probes.spawnedNormals[probe-probes.uniformNormals.size()];
                    if(glm::dot(glm::normalize(direction),glm::normalize(glm::dvec3(n)))<=0)continue;
                    const int status=int(std::lround(rays.radianceStatus[ray].w));
                    // Invalid origins and hemisphere rejections are launch
                    // policy, not traced misses; they have their own contracts.
                    if(status==0||status==16||status==5)continue;
                    const auto normalized=glm::normalize(direction);
                    const auto& settings=frame.effects.screenProbePlacement.trace;
                    const float maximum=frame.effects.surfaceCacheGi.maxDistance;
                    const float nearEnd=std::clamp(settings.sdf.meshTraceDistance,0.f,maximum);
                    SurfaceCacheGeometryFixture::Hit hit;hit.t=maximum;unsigned intervals=0;
                    const auto interval=[&](float minimum,float upper,std::uint64_t& count){
                        if(upper<=std::max(minimum,1e-5f))return;
                        ++count;++intervals;const auto value=oracle->Nearest(position,normalized,upper,std::max(double(minimum),1e-5));
                        if(value.triangle>=0&&(hit.triangle<0||value.t<hit.t))hit=value;
                    };
                    // The geometry reference is independent. Its query windows
                    // must nevertheless match this feature's explicit coverage:
                    // native tracing retains mesh/GDF ranges, not infinite rays.
                    if(originalStatus==1&&frame.effects.surfaceCacheGi.safeProbeOrigins){
                        const auto screenHit=screenRays.packed[ray*4+1];
                        const float depth=std::max(-(frame.camera->view*glm::vec4(glm::vec3(screenHit),1)).z,0.f);
                        const float tolerance=std::max(settings.thickness+settings.relativeThickness*depth,4.f*settings.sdf.hitEpsilon);
                        interval(0.f,std::min(maximum,std::max(screenHit.w,0.f)+tolerance),nativeOracleScreenIntervals);
                    }
                    if(settings.sdf.useMesh)interval(0.f,nearEnd,nativeOracleMeshIntervals);
                    if(settings.sdf.useGlobal){
                        const auto scene=lab.PreparedProbeSdfScene();const auto& grid=scene->Settings();
                        glm::vec3 low=grid.origin,high=grid.origin+grid.spacing*glm::vec3(grid.counts-glm::uvec3(1));bool ready=true;
                        if(const auto& clipmaps=scene->Clipmaps()){
                            ready=false;const auto& levels=clipmaps->Levels();
                            for(std::size_t level=levels.size();level>0;--level)if(levels[level-1].generation){
                                const auto& outer=levels[level-1];low=outer.origin+outer.spacing;
                                high=outer.origin+outer.spacing*(glm::vec3(outer.counts)-glm::vec3(2));ready=true;break;}
                        }
                        float intervalNear=settings.sdf.useMesh?nearEnd:0.f,intervalFar=maximum;
                        const auto origin=glm::vec3(position),d=glm::vec3(normalized);bool intersects=ready;
                        for(unsigned axis=0;axis<3&&intersects;++axis){
                            if(std::abs(d[axis])<1e-8f){if(origin[axis]<low[axis]||origin[axis]>high[axis])intersects=false;continue;}
                            const float a=(low[axis]-origin[axis])/d[axis],b=(high[axis]-origin[axis])/d[axis];
                            intervalNear=std::max(intervalNear,std::min(a,b));intervalFar=std::min(intervalFar,std::max(a,b));if(intervalNear>intervalFar)intersects=false;
                        }
                        if(intersects)interval(intervalNear,intervalFar,nativeOracleGlobalIntervals);
                    }
                    nativeOracleEmptyIntervals+=intervals==0;
                    const bool found=status==1||status==9||status==10||status==11||status==12;
                    ++nativeOracleRays;
                    if(found){++nativeOracleHits;Check(hit.triangle>=0,"Hardware probe reported a hit where independent geometry is empty");
                        const auto endpoint=glm::dvec3(rays.hitWorldDistance[ray]);const double distance=rays.hitWorldDistance[ray].w;
                        const double distanceError=std::abs(distance-hit.t),endpointError=glm::length(endpoint-(position+glm::normalize(direction)*hit.t));
                        nativeOracleMaximumDistanceError=std::max(nativeOracleMaximumDistanceError,distanceError);
                        nativeOracleMaximumEndpointError=std::max(nativeOracleMaximumEndpointError,endpointError);
                        if(distanceError>3e-4||endpointError>3e-4)std::cout<<tag<<" QUALITY native_oracle_mismatch view="<<view<<" frame="<<f+1<<" ray="<<ray
                            <<" status="<<status<<" distance_error="<<distanceError<<" endpoint_error="<<endpointError<<std::endl;
                        Check(distanceError<=3e-4&&endpointError<=3e-4,"Hardware probe endpoint skipped independent nearest geometry");
                    } else {++nativeOracleMisses;Check(hit.triangle<0,"Hardware probe missed independent geometry within its configured coverage intervals");
                        nativeOracleCoverageLimitedMisses+=oracle->Nearest(position,normalized,maximum,1e-5).triangle>=0;}
                }
            }
            if(compactRound&&lab.PipelineStatistics().probeSdfCompactRays){const auto pending=CompactQueue(lab,screenRays,probes,origins,
                    frame.effects.surfaceCacheGi.safeProbeOrigins,frame.effects.surfaceCacheGi.enabled);
                ++compactIdentityFrames;compactIdentityRays+=pending;compactMinimumPending=std::min(compactMinimumPending,std::uint32_t(pending));
                compactMaximumPending=std::max(compactMaximumPending,std::uint32_t(pending));}
            if(executionRound)s.packed[0]=std::move(screenRays.packed);
            Finite(rays.hitWorldDistance,"probe hits");Finite(rays.hitPixelStepsMip,"probe owner/steps");Finite(rays.rayDirectionDepthFetches,"probe directions");
            Finite(rays.sourceViewPositions,"receiver positions");Finite(rays.sourceWorldNormals,"receiver normals");Finite(mask,"contact mask");Finite(sampling,"receiver N replay");
            Check(rays.rayResolution==8&&rays.radianceStatus.size()==std::size_t(rays.probeSlots)*64&&sampling.size()==pixels,"Native parity changed probe/sample layout");
            std::vector<float> statuses;statuses.reserve(rays.radianceStatus.size());for(auto value:rays.radianceStatus){for(unsigned c=0;c<4;++c)Check(std::isfinite(value[c]),"Nonfinite native source radiance");statuses.push_back(value.w);}
            s.signature={Hash(rays.hitWorldDistance),Hash(rays.hitPixelStepsMip),Hash(rays.rayDirectionDepthFetches),Hash(statuses),Hash(risk),Hash(mask),
                fresh.empty()?0:SurfaceCacheFreshSamplingGeometryHash(fresh),Hash(probes.uniform),Hash(probes.spawned),Hash(origins),
                Hash(rays.sourceViewPositions),Hash(rays.sourceWorldNormals),Hash(sampling)};
            s.geometry=geometryAt(rays);metadataEntries+=rays.radianceStatus.size();s.samplingDebug.reserve(pixels);
            for(std::size_t i=0;i<pixels;++i){const auto d=sampling[i];const unsigned actual=unsigned(std::lround(d.y)),readable=unsigned(std::lround(d.w));
                Check(d.z==8&&d.y==float(actual)&&d.w==float(readable)&&actual<=32&&actual%8==0&&readable<=actual,"Native parity changed actual-N/readable denominator");
                s.samplingDebug.push_back({std::uint8_t(d.x>.5f),std::uint8_t(actual),8,std::uint8_t(readable)});
                const auto plan=SurfaceCacheFreshSamplingPlan(fresh.empty()?glm::vec4(0,8,0,0):fresh[i],mask[i].x,8,2,true);
                Check(actual==0||actual==plan.total,"Native actual N differed from geometric/moving plan");s.rays+=actual;s.readable+=readable;s.fresh+=d.x>.5f;s.moving+=plan.movingEligible;
                if(s.image[0][i].w<=0)Check(glm::dot(glm::vec3(s.image[0][i]),glm::vec3(s.image[0][i]))==0,"Native contact filled unknown current");
                Check(s.image[1][i].w==std::clamp(s.image[0][i].w,0.f,1.f),"Native temporal changed current readability");}
            if(shadowMethod){const auto depth=lab.ReadbackAreaLightShadowDepth();s.shadowSignature=AreaDepthSignature(depth);
                if(std::find(shadowCheckpoints.begin(),shadowCheckpoints.end(),f+1)!=shadowCheckpoints.end())s.shadowDepth=depth.depth;}
            return s;};
        std::array<std::array<std::array<Error,regions>,stages>,phases> errors{};
        std::cout<<tag<<" QUALITY setup view="<<view<<" size="<<frame.width<<'x'<<frame.height<<" frames="<<frames
            <<" static=64 continuous=16 separated_hold=8 return_hold=8 settle=16 baseline="<<variantNames[0]<<" reference=accepted_output_not_path_truth"
            <<" moving_multiplier=2 full_current=1 probe_sdf_gdf_math_unchanged="<<(!architectureNative||modelUpdateMask!=0||queryReuseMask!=0)<<" independent_geometry_roi=1 rgb_relative_rmse_limit=.01"
            <<" receiver_mask="<<receiverMask<<" receiver_fast_lighting_addressing="<<bool(receiverMask&4)<<" receiver_software_origins="<<bool(receiverMask&8)<<" accepted_round9_query_mask="<<(receiverMask?12:0)
        <<" round9_preset_both_groups="<<bool(receiverMask)<<" planned_receiver_origin_pass="<<bool(receiverMask&1)
        <<" planned_receiver_draw_delta_per_frame="<<((receiverMask&1)?1:0)<<" planned_receiver_descriptor_delta_per_frame="<<((receiverMask&1)?3:0)
        <<" contact_trace_timings=0 receiver_diagnostic_page=0"
        <<" model_update_mask="<<modelUpdateMask<<" query_reuse_mask="<<queryReuseMask<<" planned_query_descriptor_changes="<<(!receiverMask&&bool(queryReuseMask&8))<<" round8_resource_preset_both_groups="<<bool(queryReuseMask)<<" main_triangle_profile_both_groups="<<bool(queryReuseMask)
        <<" main_profile_bypasses_sdf_march="<<architectureNative<<" architecture_mask="<<architectureMask<<" final_trace_internal_exact="<<strictArchitecture
            <<" probe_rays="<<placement.trace.rayResolution*placement.trace.rayResolution<<" screen_steps="<<placement.trace.maxSteps
            <<" sdf_steps="<<placement.trace.sdf.maxSteps<<" hardware_contact_and_cache_both_groups="<<executionRound
            <<" packed_trace_check="<<(executionRound&&!architectureNative)<<" packed_continuous_envelope_float_epsilon_multiple=16 packed_discrete_metadata_exact="<<!architectureNative<<std::endl;
        for(unsigned variant=0;variant<2;++variant){frame.effects.surfaceCacheGi=accepted;frame.effects.screenProbePlacement=placement;Mode(frame,variant!=0,executionRound,comparisonMask,compactRound,shadowRound,sdfRound,reuseRound,architectureMask,modelUpdateMask,queryReuseMask,receiverMask);poseAt(0);Reset(frame);
            for(unsigned f=0;f<frames;++f){poseAt(f);Draw(lab,frame);Effective(lab,variant!=0,executionRound,comparisonMask,compactRound,shadowRound,sdfRound,reuseRound,architectureMask,modelUpdateMask,queryReuseMask,receiverMask);auto current=sample(f);
                totalSelected+=current.fresh;totalMoving+=current.moving;totalRays+=current.rays;
                if(!variant)baseline.push_back(std::move(current));
                else {const auto& before=baseline[f];
                    if(shadowMethod){Check(current.shadowSignature==before.shadowSignature,"Shadow submission changed complete depth atlas signature or coverage");++shadowSignatureFrames;
                        if(!current.shadowDepth.empty()){shadowDepthPixels+=CompareAreaDepth(before.shadowDepth,current.shadowDepth);++shadowDepthFrames;}}

                    if(executionRound&&!architectureNative)for(unsigned kind=0;kind<2;++kind)comparePacked(before.packed[kind],current.packed[kind],view,f,kind);
                    const auto externalSignature=[&](const Snapshot& value){auto result=value.signature;if(architectureNative)for(unsigned field:{0u,1u,2u,3u,4u,9u,12u})result[field]=0;return result;};
                    if(externalSignature(current)!=externalSignature(before)){
                        constexpr std::array<const char*,13> fields{{"probe_hit_world_distance","probe_owner_steps","probe_direction_fetches","probe_status",
                            "contact_probe_risk","contact_mask","geometry_exposure","uniform_probe_positions","adaptive_probe_positions","safe_probe_origins",
                            "receiver_view_positions","receiver_world_normals","receiver_sampling_actualN_readable"}};
                        for(unsigned field=0;field<fields.size();++field)if(current.signature[field]!=before.signature[field])
                            std::cout<<tag<<" QUALITY signature_mismatch view="<<view<<" frame="<<f+1<<" field="<<fields[field]
                                <<" before="<<before.signature[field]<<" after="<<current.signature[field]<<std::endl;
                        std::array<std::uint64_t,4> components{};std::uint64_t changedPixels=0;
                        for(std::size_t i=0;i<current.samplingDebug.size();++i){const auto& a=before.samplingDebug[i];const auto& b=current.samplingDebug[i];
                            if(a==b)continue;++changedPixels;for(unsigned c=0;c<4;++c)components[c]+=a[c]!=b[c];
                            if(changedPixels<=16)std::cout<<tag<<" QUALITY sampling_mismatch view="<<view<<" frame="<<f+1<<" pixel="<<i
                                <<" before="<<unsigned(a[0])<<','<<unsigned(a[1])<<','<<unsigned(a[2])<<','<<unsigned(a[3])
                                <<" after="<<unsigned(b[0])<<','<<unsigned(b[1])<<','<<unsigned(b[2])<<','<<unsigned(b[3])<<std::endl;}
                        std::cout<<tag<<" QUALITY mismatch_totals view="<<view<<" frame="<<f+1
                            <<" actualN_before="<<before.rays<<" actualN_after="<<current.rays<<" readable_before="<<before.readable<<" readable_after="<<current.readable
                            <<" fresh_before="<<before.fresh<<" fresh_after="<<current.fresh<<" moving_before="<<before.moving<<" moving_after="<<current.moving
                            <<" sampling_changed_pixels="<<changedPixels<<" selected_component_changes="<<components[0]<<" actualN_component_changes="<<components[1]
                            <<" base_component_changes="<<components[2]<<" readable_component_changes="<<components[3]<<std::endl;}
                    Check(externalSignature(current)==externalSignature(before),"Native changed receiver geometry, placement, contact mask or exposure");
                    if(architectureNative){Check(before.rays==current.rays&&before.fresh==current.fresh&&before.moving==current.moving,
                            "Native architecture changed receiver ray budgets or geometric selection");
                        for(std::size_t i=0;i<current.samplingDebug.size();++i)for(unsigned c=0;c<3;++c)
                            Check(current.samplingDebug[i][c]==before.samplingDebug[i][c],"Native architecture changed a per-pixel receiver sampling plan");}
                    Check(current.geometry==before.geometry,"Native changed geometry-defined ROI");++exactGeometryFrames;const auto& g=geometry[current.geometry];
                    for(unsigned stage=0;stage<stages;++stage)for(std::size_t i=0;i<pixels;++i){Check(architectureNative||current.image[stage][i].w==before.image[stage][i].w,"Native changed current/output coverage");coverageExactPixels+=current.image[stage][i].w==before.image[stage][i].w;
                        if(shadowMethod&&strictArchitecture)Check(current.image[stage][i]==before.image[stage][i],"Shadow submission changed exact contact/temporal RGBA");
                        const auto a=glm::dvec3(before.image[stage][i]),b=glm::dvec3(current.image[stage][i]),difference=b-a;
                        for(unsigned region=0;region<regions;++region)if(g.roi[i]&(1u<<region)){auto& e=errors[phaseAt(f)][stage][region];
                            e.square+=glm::dot(difference,difference)/3.;e.energy+=glm::dot(a,a)/3.;e.maximum=std::max(e.maximum,std::max({std::abs(difference.x),std::abs(difference.y),std::abs(difference.z)}));
                            ++e.count;e.exact+=before.image[stage][i]==current.image[stage][i];
                            if(stage==1){const auto residualBefore=a-glm::dvec3(before.image[0][i]),residualAfter=b-glm::dvec3(current.image[0][i]);
                                e.historyResidualBefore+=glm::dot(residualBefore,residualBefore)/3.;e.historyResidualAfter+=glm::dot(residualAfter,residualAfter)/3.;}
                            if(region==1){if(g.band[i]==1){e.coreBefore+=glm::dot(a,luma);e.coreAfter+=glm::dot(b,luma);++e.core;}
                                if(g.band[i]==2){e.shellBefore+=glm::dot(a,luma);e.shellAfter+=glm::dot(b,luma);++e.shell;}}
                            if(i%frame.width+1<frame.width&&i+1<pixels&&(g.roi[i+1]&(1u<<region))&&glm::dot(g.normal[i],g.normal[i+1])>.99f&&glm::length(g.world[i+1]-g.world[i])<.05f){
                                const auto da=glm::dvec3(before.image[stage][i+1])-a,db=glm::dvec3(current.image[stage][i+1])-b;
                                e.gradientBefore+=glm::dot(da,da)/3.;e.gradientAfter+=glm::dot(db,db)/3.;e.gradientDifference+=glm::dot(db-da,db-da)/3.;++e.pairs;}}}
                    if(!current.atlas.empty()){Check(current.atlas.size()==before.atlas.size(),"Native changed card atlas layout");for(std::size_t i=0;i<current.atlas.size();++i){
                            if(shadowMethod&&strictArchitecture)Check(current.atlas[i]==before.atlas[i],"Shadow submission changed exact card radiance/validity");
                            Check(current.atlas[i].w==before.atlas[i].w,"Native changed card validity");const auto a=glm::dvec3(before.atlas[i]),d=glm::dvec3(current.atlas[i])-a;
                            atlasSquare+=glm::dot(d,d)/3.;atlasEnergy+=glm::dot(a,a)/3.;atlasMax=std::max(atlasMax,std::max({std::abs(d.x),std::abs(d.y),std::abs(d.z)}));++atlasCount;}}}
                const bool save=f==63||f==64||f==71||f==79||f==80||f==87||f==88||f==95||f==111;
                if(save&&!options.screenshot.empty()){const auto label=std::string("view")+std::to_string(view)+"_"+variantNames[variant]+"_frame"+std::to_string(f+1);
                    Save(lab.Readback(),OutputPath(options,label+"_total_renderer_ldr"));const auto& value=variant?current:baseline.back();
                    Tone(options,frame.width,frame.height,value.image[0],label+"_contact_tone4");Tone(options,frame.width,frame.height,value.image[1],label+"_temporal_tone4");}}
            // Both effective backends must preserve known black and unknown.
            // The light edit resets valid history; receiver motion then tests
            // the additional current-ray path, not just a cold zero frame.
            frame.camera=base;frame.screenProbeStudyState.occluderOffset=0;frame.effects.areaLights.lights[0].enabled=false;Draw(lab,frame);
            const auto energy=[](const auto& values){double sum=0;for(auto v:values)sum+=glm::dot(glm::dvec3(v),glm::dvec3(v));return sum;};
            auto contact=lab.ReadbackSurfaceCacheContactGi(),temporal=lab.ReadbackSurfaceCacheTemporalGi();Finite(contact,"black contact");Finite(temporal,"black temporal");
            std::uint64_t black=0;for(auto v:contact)black+=v.w>0;Check(black>100&&energy(contact)<1e-8&&energy(temporal)<1e-8,"Native lost readable black or retained stale light");blackPixels+=black;
            // These cameras sit inside an opaque sealed room. A broken depth
            // prepass/capture contract must not become a shared A/B baseline.
            if(executionRound)Check(black==pixels,"Sealed-room capture lost receiver coverage before the optimization comparison");
            frame.screenProbeStudyState.occluderOffset=.15f;Draw(lab,frame);Check(energy(lab.ReadbackSurfaceCacheContactGi())<1e-8&&energy(lab.ReadbackSurfaceCacheTemporalGi())<1e-8,"Native moving current rays lost black");
            frame.screenProbeStudyState.occluderOffset=0;frame.effects.areaLights.lights[0].enabled=true;Draw(lab,frame);
            auto instances=lab.PreparedProbeSdfScene()->Instances();for(auto& instance:instances)instance.localToWorld[3].x+=50;
            Render::ProbeGlobalSdfSettings tiny;tiny.counts=glm::uvec3(2);const auto remote=Render::BuildProbeSdfScene(instances,tiny);
            frame.effects.surfaceCacheGi.cache=Render::BuildSurfaceCache(*remote);Draw(lab,frame);Draw(lab,frame);
            contact=lab.ReadbackSurfaceCacheContactGi();temporal=lab.ReadbackSurfaceCacheTemporalGi();std::uint64_t unknown=0;for(auto v:contact)unknown+=v.w<=0;
            Check(unknown>100&&energy(contact)<1e-8&&energy(temporal)<1e-8,"Native fabricated unknown-cache lighting");unknownPixels+=unknown;
            frame.screenProbeStudyState.occluderOffset=.15f;Draw(lab,frame);Check(energy(lab.ReadbackSurfaceCacheContactGi())<1e-8&&energy(lab.ReadbackSurfaceCacheTemporalGi())<1e-8,"Native moving current rays fabricated unknown lighting");
            std::cout<<tag<<" QUALITY validity view="<<view<<" variant="<<variantNames[variant]<<" readable_black="<<black<<" unknown="<<unknown<<" moving_black_zero=1 moving_unknown_zero=1"<<std::endl;
            frame.effects.surfaceCacheGi.cache=accepted.cache;frame.effects.areaLights.lights[0].enabled=true;}
        for(unsigned phase=0;phase<phases;++phase)for(unsigned stage=0;stage<stages;++stage)for(unsigned region=0;region<regions;++region){const auto& e=errors[phase][stage][region];if(!e.count)continue;
            const double rms=std::sqrt(mean(e.square,e.count)),relative=rms/std::max(std::sqrt(mean(e.energy,e.count)),1e-6);worstRelativeRmse=std::max(worstRelativeRmse,relative);
            std::cout<<tag<<" QUALITY parity view="<<view<<" phase="<<phaseNames[phase]<<" stage="<<stageNames[stage]<<" region="<<regionNames[region]
                <<" pixel_frames="<<e.count<<" rgb_mse="<<mean(e.square,e.count)<<" relative_rmse="<<relative<<" maximum_abs_rgb="<<e.maximum<<" exact_pixel_frames="<<e.exact
                <<" gradient_before="<<mean(e.gradientBefore,e.pairs)<<" gradient_after="<<mean(e.gradientAfter,e.pairs)<<" gradient_difference_mse="<<mean(e.gradientDifference,e.pairs)
                <<" contact_core_pixels="<<e.core<<" contact_shell_pixels="<<e.shell<<" contact_contrast_before="<<mean(e.shellBefore,e.shell)-mean(e.coreBefore,e.core)
                <<" contact_contrast_after="<<mean(e.shellAfter,e.shell)-mean(e.coreAfter,e.core)
                <<" history_to_current_residual_before="<<mean(e.historyResidualBefore,e.count)<<" history_to_current_residual_after="<<mean(e.historyResidualAfter,e.count)
                <<" accepted_output_reference=1 software_output_reference="<<!executionRound<<" path_truth=0"<<std::endl;
            if((architectureMask||modelUpdateMask||queryReuseMask)&&region==1&&e.core&&e.shell){const double contrastBefore=mean(e.shellBefore,e.shell)-mean(e.coreBefore,e.core),contrastAfter=mean(e.shellAfter,e.shell)-mean(e.coreAfter,e.core);
                Check(std::abs(contrastAfter-contrastBefore)<=.02*std::max(std::abs(contrastBefore),.005),"Architecture changed contact-core/shell contrast by more than its two-percent envelope");}
            if((architectureMask||modelUpdateMask||queryReuseMask)&&stage==1&&region==1&&(phase==2||phase==3)){
                Check(mean(e.historyResidualAfter,e.count)<=1.05*mean(e.historyResidualBefore,e.count)+1e-7,
                    "Architecture increased separation/return history-to-current residual beyond its five-percent envelope");}}
    }
    const double atlasRelative=std::sqrt(mean(atlasSquare,atlasCount))/std::max(std::sqrt(mean(atlasEnergy,atlasCount)),1e-6);
    std::cout<<tag<<" QUALITY summary geometry_metadata_exact_frames="<<exactGeometryFrames<<" expected_exact_frames="<<(endView-beginView)*frames
        <<" coverage_exact_pixel_frames="<<coverageExactPixels<<" source_metadata_entries_checked="<<metadataEntries<<" selected_geometry_pixel_frames="<<totalSelected
        <<" selected_moving_pixel_frames="<<totalMoving<<" actual_receiver_rays="<<totalRays<<" readable_black="<<blackPixels<<" unknown_pixels="<<unknownPixels
        <<" worst_region_phase_relative_rmse="<<worstRelativeRmse<<" atlas_relative_rmse="<<atlasRelative<<" atlas_rgb_mse="<<mean(atlasSquare,atlasCount)
        <<" atlas_maximum_abs_rgb="<<atlasMax<<" accepted_reference_not_path_truth=1"
        <<" packed_trace_texels="<<packedTexels<<" packed_exact_metadata_channels="<<packedExactMetadataChannels
        <<" packed_maximum_continuous_abs_error="<<packedMaximumError<<std::endl;
    if(compactRound)std::cout<<tag<<" QUALITY compact_identities frames="<<compactIdentityFrames<<" unique_original_ray_ids="<<compactIdentityRays
        <<" minimum_pending="<<compactMinimumPending<<" maximum_pending="<<compactMaximumPending
        <<" independent_original_launch_checks=1 complete_physical_atlas_parity=1 independent_triangle_certification_oracle=0 gpu_readbacks_untimed=1"<<std::endl;
    if(shadowMethod){Check(shadowSignatureFrames==(endView-beginView)*frames&&shadowDepthFrames==(endView-beginView)*shadowCheckpoints.size(),
            "Shadow quality did not inspect every depth signature and all seven direct-depth checkpoints");
        std::cout<<tag<<" QUALITY depth exact_signature_frames="<<shadowSignatureFrames<<" direct_bit_exact_frames="<<shadowDepthFrames
            <<" compared_depth_pixels="<<shadowDepthPixels<<" complete_atlas_including_clear_pixels=1 hash_not_pixel_compare_for_other_frames=1 depth_readbacks_untimed=1"<<std::endl;}
    Check(exactGeometryFrames==(endView-beginView)*frames&&totalSelected>0&&totalMoving>0,"Native parity did not exercise exact geometry and current sampling");
    Check(architectureNative||!executionRound||(packedTexels>0&&packedExactMetadataChannels>0),"Probe efficiency packed trace checks were not exercised");
    if(compactRound)Check(compactIdentityFrames==(endView-beginView)*frames&&compactIdentityRays>0,"Compaction quality did not validate every candidate queue");
    Check(worstRelativeRmse<=.01&&atlasRelative<=.01,"Native contact/cache exceeded the one-percent relative RMS parity gate");
    if(architectureNative){Check(nativeOracleRays>0&&nativeOracleHits>0,"Native architecture did not exercise independent geometry endpoint checks");
        std::cout<<tag<<" QUALITY native_geometry rays="<<nativeOracleRays<<" hits="<<nativeOracleHits<<" misses="<<nativeOracleMisses
            <<" max_distance_error="<<nativeOracleMaximumDistanceError<<" max_endpoint_error="<<nativeOracleMaximumEndpointError
            <<" screen_intervals="<<nativeOracleScreenIntervals<<" mesh_intervals="<<nativeOracleMeshIntervals<<" global_intervals="<<nativeOracleGlobalIntervals
            <<" no_eligible_interval_rays="<<nativeOracleEmptyIntervals<<" coverage_limited_misses="<<nativeOracleCoverageLimitedMisses
            <<" limit_metres=.0003 original_id_rotating_stride=64 independent_double_triangles=1 untimed=1"<<std::endl;}
    if(architectureNative)std::cout<<tag<<" QUALITY PASS: receiver geometry/placement/exposure/actual-N exact; sampled endpoints agree with independent triangles within .3 mm; contact contrast and separation/return residual guarded; RGB within 1% of accepted output; no physical-lighting groundtruth claim."<<std::endl;
    else std::cout<<tag<<" QUALITY PASS: geometry/owner/status/actual-N/coverage exact; known-black/unknown protected; current and temporal RGB within 1% relative RMS of accepted output; no independent-groundtruth improvement claim."<<std::endl;
}
