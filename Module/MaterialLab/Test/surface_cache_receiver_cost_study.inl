// Balanced receiver attribution on the accepted Round9 mask12 profile.
// Extra stage timing markers are enabled only here; replay/readbacks remain off.
void SurfaceCacheReceiverCostStudy(MaterialLabModule& lab,const Options& options) {
    using namespace SurfaceCacheNativeFixture;
    Check(options.vulkan&&!options.softwareRays&&!options.validation&&options.benchmarkPresent,
        "Receiver cost study requires capability-enabled Release Vulkan, real presentation and no validation");
#ifndef NDEBUG
    Check(false,"Receiver cost study requires a Release build");
#endif
    const unsigned requestedMask=options.surfaceCacheReceiverMask;ReceiverVariant(requestedMask);
    const bool twoMode=requestedMask>=4;
    constexpr unsigned blocks=8,measured=32,warm=16,initialWarm=64;
    const unsigned modeCount=twoMode?2u:4u,epochs=twoMode?4u:8u;
    const std::array<unsigned,8> order{{0,1,twoMode?1u:2u,twoMode?0u:3u,3,2,1,0}};
    const std::array<unsigned,4> masks{{0,twoMode?requestedMask:1u,2,3}};
    const std::array<const char*,4> names{{"accepted_round9",ReceiverVariant(masks[1]),ReceiverVariant(masks[2]),ReceiverVariant(masks[3])}};
    constexpr std::array<const char*,17> metricNames{{"probe_kernel_gpu_ms","probe_total_gpu_ms","contact_gpu_ms","frame_gpu_ms",
        "screen_gpu_ms","probe_spatial_gpu_ms","loop_ms","scene_ms","record_ms","submit_wait_ms","host_encode_ms","host_fence_ms","host_as_prepare_ms","contact_risk_gpu_ms","contact_mask_gpu_ms","contact_origins_gpu_ms","contact_gather_gpu_ms"}};
    constexpr std::array<const char*,22> workNames{{"probe_ray_budget","attribute_uploaded_texels","geometry_uploaded_texels",
        "logical_upload_regions","cache_relit_texels","area_shadow_passes","physical_upload_regions","physical_upload_bytes",
        "deferred_upload_regions","deferred_upload_bytes","dedicated_upload_submissions","dedicated_upload_waits",
        "as_operations","as_full_builds","as_updates","as_storage_reuses","as_buffer_allocations","as_handle_creations",
        "graphics_draw_calls","graphics_uniform_snapshot_copies","staging_allocations","graphics_descriptor_allocations"}};
    struct Sample {
        unsigned block=0,epoch=0,mode=0,index=0;
        std::array<double,17> metrics{};
        std::array<std::uint64_t,22> work{};
    };
    std::vector<Sample> samples;samples.reserve(blocks*epochs*measured);
    std::array<std::array<std::vector<double>,17>,4> totals;
    std::array<std::array<std::uint64_t,22>,4> totalWork{};
    auto frame=Frame(options,false);const auto base=SurfaceCacheFreshSamplingCamera(2,frame.OutputAspect(),true);
    const glm::vec3 radiance=frame.effects.areaLights.lights[0].radiance;
    const auto unixMilliseconds=[] {return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();};
    const auto select=[&](unsigned mode) {
        // Both groups retain the accepted Round9 AS UPDATE/upload-pruning
        // preset. Only one independently selected query/addressing policy varies.
        Mode(frame,true,false,15,false,false,false,true,0,0,12,masks[mode]);
        frame.effects.surfaceCacheGi.contact.collectTraceTimings=true;
        Check(frame.effects.surfaceCacheGi.updateAccelerationStructures&&!frame.effects.surfaceCacheGi.reuseAccelerationStorage&&
            frame.effects.screenProbePlacement.trace.sdf.nativeUploadPruning&&
            !frame.effects.surfaceCacheGi.reuseOriginSoftwareCertification&&frame.effects.surfaceCacheGi.receiverSoftwareOrigins==bool(masks[mode]&8),
            "Receiver study changed accepted Round9 AS/upload/origin policy");
    };
    const auto effective=[&](unsigned mode) {
        Effective(lab,true,false,15,false,false,false,true,0,0,12,masks[mode]);
        const auto stats=lab.PipelineStatistics();
        Check(!stats.probeSdfHardwareRayTracing&&stats.probeSdfDirectSoftwareRefinement&&stats.surfaceCacheHardwareOrigins&&stats.surfaceCacheRadianceOnlyFiltering&&
            stats.surfaceCacheHardwareContact&&stats.surfaceCacheHardwareVisibility&&stats.probeSdfNativeUploadsPruned,
            "Receiver study lost accepted Round9 native/filter/resource paths");
        Check(!stats.probeSdfNativeCandidateReused&&!stats.surfaceCacheClosestTraversalReused&&
            stats.surfaceCacheFastGeometryAddressing&&stats.surfaceCacheSplitReceiverOrigins==bool(masks[mode]&1)&&stats.surfaceCacheReceiverLookupReused==bool(masks[mode]&2)&&
            stats.surfaceCacheReceiverFastLightingAddressing==bool(masks[mode]&4)&&!stats.surfaceCacheReusedOriginCertification&&
            stats.surfaceCacheSoftwareReceiverOrigins==bool(masks[mode]&8),"Receiver study changed an unrequested execution selector");
        Check(!lab.ProbeSdfQualityStatistics().pending,"Receiver study measured an unprepared SDF scene");
    };
    const auto one=[](const std::vector<double>& values,const char* label) {
        Check(values.size()==1&&std::isfinite(values.front())&&values.front()>=0,
            std::string("Receiver study missing/nonfinite same-frame timing: ")+label);
        return values.front();
    };
    const auto precision=std::cout.precision(9);
    std::cout<<"RECEIVER COST STUDY setup build=Release_NDEBUG size="<<frame.width<<'x'<<frame.height
        <<" path=model modes="<<modeCount<<" selected_receiver_mask="<<requestedMask<<" two_mode_study="<<twoMode<<" blocks="<<blocks<<" epochs_per_block="<<epochs<<" measured_per_epoch="<<measured
        <<" model_warm_per_epoch="<<warm<<" initial_model_warm_per_mode="<<initialWarm<<" order="<<(twoMode?"0,1,1,0":"0,1,2,3,3,2,1,0")
        <<" present="<<frame.present<<" validation=0 hud=0 application_fps_limit=0 image_readbacks=0 cpu_oracle=0 gpu_atomic_instrumentation=0"
        <<" raw_frame_output_after_measurement=1 main_native_both_groups=0 direct_software_all_modes=1 geometry_addressing_all_modes=1 radiance_filter_all_modes=1 accepted_round9_query_mask=12 contact_trace_timings=1 diagnostic_pages=0 exact_planned_draw_descriptors=1"
        <<" as_policy=update upload_pruning=1 geometry_and_sampling_math_unchanged=1"
        <<" probe_rays="<<frame.effects.screenProbePlacement.trace.rayResolution*frame.effects.screenProbePlacement.trace.rayResolution
        <<" receiver_rays="<<frame.effects.surfaceCacheGi.contact.raysPerPixel
        <<" paired_block_units=8 frame_independence_claim=0 timestamp_unit=unix_ms telemetry_alignment=epoch_only"<<std::endl;
    for(unsigned mode=0;mode<modeCount;++mode)
        std::cout<<"RECEIVER COST STUDY mode mode="<<mode<<" name="<<names[mode]<<" receiver_mask="<<masks[mode]
            <<" receiver_origin_split="<<bool(masks[mode]&1)<<" receiver_lookup_reuse="<<bool(masks[mode]&2)<<" receiver_fast_lighting_addressing="<<bool(masks[mode]&4)<<" receiver_software_origins="<<bool(masks[mode]&8)
            <<" planned_draw_delta="<<bool(masks[mode]&1)<<" planned_descriptor_delta="<<((masks[mode]&1)?3:0)<<std::endl;
    for(unsigned mode=0;mode<modeCount;++mode) {
        select(mode);Pose(frame,base,2,15,radiance);Reset(frame);
        std::cout<<"RECEIVER COST STUDY prewarm_start mode="<<mode<<" unix_ms="<<unixMilliseconds()<<std::endl;
        Draw(lab,frame);unsigned preparationFrames=1;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
        while(lab.ProbeSdfQualityStatistics().pending) {
            Draw(lab,frame);++preparationFrames;
            Check(std::chrono::steady_clock::now()<deadline,"Receiver cost study scene preparation timed out");
        }
        effective(mode);Reset(frame);
        for(unsigned i=0;i<initialWarm;++i){Pose(frame,base,2,i,radiance);glfwPollEvents();Draw(lab,frame);}
        effective(mode);
        std::cout<<"RECEIVER COST STUDY prewarm_end mode="<<mode<<" unix_ms="<<unixMilliseconds()
            <<" initial_model_frames="<<initialWarm<<" preparation_frames="<<preparationFrames<<" timed=0"<<std::endl;
    }
    for(unsigned block=0;block<blocks;++block) {
        std::array<std::uint64_t,22> referenceWork{};
        for(unsigned epoch=0;epoch<epochs;++epoch) {
            const unsigned mode=order[epoch];select(mode);Reset(frame);
            for(unsigned i=0;i<warm;++i){Pose(frame,base,2,i,radiance);glfwPollEvents();Draw(lab,frame);}
            effective(mode);
            std::cout<<"RECEIVER COST STUDY epoch_start block="<<block<<" epoch="<<epoch<<" mode="<<mode
                <<" unix_ms="<<unixMilliseconds()<<" model_warm_frames="<<warm<<std::endl;
            std::array<std::vector<double>,17> epochMetrics;std::array<std::uint64_t,22> epochWork{};
            for(unsigned i=0;i<measured;++i) {
                Pose(frame,base,2,i,radiance);
                const auto begin=std::chrono::steady_clock::now();glfwPollEvents();const auto timing=lab.MeasureFrames(frame,1,0);
                const double loop=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
                frame.effects.cameraCut=false;frame.effects.screenProbePlacement.reset=false;frame.effects.surfaceCacheGi.temporal.reset=false;
                effective(mode);const auto stats=lab.PipelineStatistics();
                Check(timing.backendHostFrameMilliseconds.size()==1&&timing.backendHostFrameStatistics.size()==1&&
                    timing.textureUploadStatistics.size()==1,"Receiver study missing same-frame host/upload counters");
                const auto scopes=Scopes(timing);const auto& host=timing.backendHostFrameMilliseconds.front();
                const auto& counters=timing.backendHostFrameStatistics.front();const auto& uploads=timing.textureUploadStatistics.front();
                Sample sample;sample.block=block;sample.epoch=epoch;sample.mode=mode;sample.index=i;
                sample.metrics={one(*scopes[13],"main probe native kernel"),one(*scopes[2],"main probe total"),one(*scopes[5],"contact"),one(*scopes[0],"GPU frame"),
                    one(*scopes[1],"screen trace"),one(*scopes[4],"probe spatial"),loop,
                    one(timing.scenePreparationFrameMilliseconds,"scene"),one(timing.frameRecordingFrameMilliseconds,"record"),
                    one(timing.submitWaitFrameMilliseconds,"submit wait"),host[1],host[9],host[5],one(timing.contactRiskFrameMilliseconds,"contact risk"),one(timing.contactMaskFrameMilliseconds,"contact mask"),
                    (masks[mode]&1)?one(timing.contactOriginsFrameMilliseconds,"receiver origin prepass"):0.0,one(timing.contactGatherFrameMilliseconds,"contact gather")};
                for(double v:sample.metrics)Check(std::isfinite(v)&&v>=0,"Receiver study nonfinite/negative timing");
                sample.work={stats.surfaceCacheProbeRayBudget,stats.surfaceCacheAttributeUploadedTexels,stats.surfaceCacheGeometryUploadedTexels,
                    stats.surfaceCacheUploadRegions,stats.surfaceCacheUpdatedTexels,stats.areaLightShadowPasses,uploads[0],uploads[1],
                    uploads[2],uploads[3],uploads[4],uploads[5],counters[3],counters[7],counters[8],counters[4],counters[5],counters[6],
                    counters[0],counters[2],uploads[6],counters[1]};
                Check(counters[3]==1&&counters[7]==0&&counters[8]==1&&counters[4]==1&&counters[5]==0&&counters[6]==0,
                    "Receiver study did not retain one allocation-free Round9 AS UPDATE per moved frame");
                for(unsigned m=0;m<metricNames.size();++m){epochMetrics[m].push_back(sample.metrics[m]);totals[mode][m].push_back(sample.metrics[m]);}
                for(unsigned w=0;w<workNames.size();++w){epochWork[w]+=sample.work[w];totalWork[mode][w]+=sample.work[w];}
                samples.push_back(std::move(sample));
            }
            const auto end=unixMilliseconds();
            auto comparable=epochWork;comparable[18]-=(masks[mode]&1)?measured:0;comparable[21]-=(masks[mode]&1)?3*measured:0;
            if(epoch==0)referenceWork=comparable;
            else Check(comparable==referenceWork,"Receiver study changed matched ray/cache/shadow/upload/AS/draw work within a block");
            std::cout<<"RECEIVER COST STUDY epoch_end block="<<block<<" epoch="<<epoch<<" mode="<<mode
                <<" unix_ms="<<end<<" samples="<<measured<<" estimator_resource_work_exact=1 planned_origin_pass_accounted=1"<<std::endl;
            for(unsigned m=0;m<metricNames.size();++m)
                std::cout<<"RECEIVER COST STUDY epoch_metric block="<<block<<" epoch="<<epoch<<" mode="<<mode<<" name="<<metricNames[m]
                    <<" samples="<<epochMetrics[m].size()<<" mean_ms="<<Mean(epochMetrics[m])<<" median_ms="<<Median(epochMetrics[m])
                    <<" p95_ms="<<Quantile(epochMetrics[m],.95)<<std::endl;
        }
    }
    // Emit raw records only after all timed epochs. These rows permit a
    // paired-block analysis without treating serial frames as independent.
    for(const auto& sample:samples) {
        std::cout<<"RECEIVER COST STUDY sample block="<<sample.block<<" epoch="<<sample.epoch<<" mode="<<sample.mode<<" index="<<sample.index;
        for(unsigned m=0;m<metricNames.size();++m)std::cout<<' '<<metricNames[m]<<'='<<sample.metrics[m];
        for(unsigned w=0;w<workNames.size();++w)std::cout<<' '<<workNames[w]<<'='<<sample.work[w];
        std::cout<<" main_native=0 direct_software_refinement=1 probe_origins_native=1 radiance_filter=1 contact_native=1 cache_native=1 reused_certification=0 software_receiver_origins="<<bool(masks[sample.mode]&8)
            <<" upload_pruning=1 as_update_requested=1 main_candidate_reuse=0 closest_traversal_reuse=0 fast_geometry_addressing=1"
            <<" receiver_origin_split="<<bool(masks[sample.mode]&1)<<" receiver_lookup_reuse="<<bool(masks[sample.mode]&2)<<" receiver_fast_lighting_addressing="<<bool(masks[sample.mode]&4)<<" present=1"<<'\n';
    }
    for(unsigned mode=0;mode<modeCount;++mode) {
        for(unsigned m=0;m<metricNames.size();++m)
            std::cout<<"RECEIVER COST STUDY mode_metric mode="<<mode<<" name="<<metricNames[m]<<" samples="<<totals[mode][m].size()
                <<" mean_ms="<<Mean(totals[mode][m])<<" median_ms="<<Median(totals[mode][m])<<" p95_ms="<<Quantile(totals[mode][m],.95)<<std::endl;
        std::cout<<"RECEIVER COST STUDY mode_work mode="<<mode;
        for(unsigned w=0;w<workNames.size();++w)std::cout<<' '<<workNames[w]<<'='<<totalWork[mode][w];
        std::cout<<std::endl;
    }
    auto normalizedTotalWork=totalWork;for(unsigned mode=0;mode<modeCount;++mode){normalizedTotalWork[mode][18]-=(masks[mode]&1)?512:0;normalizedTotalWork[mode][21]-=(masks[mode]&1)?1536:0;}
    Check(samples.size()==512*modeCount,"Receiver study incomplete total samples");
    for(unsigned mode=0;mode<modeCount;++mode)Check(totals[mode][0].size()==512&&normalizedTotalWork[mode]==normalizedTotalWork[0],
        "Receiver study incomplete per-mode samples or mismatched complete work");
    std::cout<<"RECEIVER COST STUDY PASS samples="<<samples.size()<<" per_mode=512 blocks=8 epochs="<<blocks*epochs<<" present_calls="<<samples.size()
        <<" matched_estimator_resource_work=1 planned_origin_pass_accounted=1 readbacks=0 cpu_oracle=0 paired_block_units=8 frame_independence_claim=0"
        <<" physical_scanout_claim=0 quality_claim=0"<<std::endl;
    std::cout.precision(precision);
}
