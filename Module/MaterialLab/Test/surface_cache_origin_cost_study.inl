// Performance attribution only. No new shader, ray budget, origin predicate,
// readback or GPU diagnostic is introduced by this test driver.
void SurfaceCacheOriginCostStudy(MaterialLabModule& lab,const Options& options) {
    using namespace SurfaceCacheNativeFixture;
    Check(options.vulkan&&!options.softwareRays&&!options.validation&&options.benchmarkPresent,
        "Origin cost study requires capability-enabled Release Vulkan, real presentation and no validation");
#ifndef NDEBUG
    Check(false,"Origin cost study requires a Release build");
#endif
    constexpr unsigned blocks=8,epochs=6,measured=32,warm=16,initialWarm=64;
    constexpr std::array<unsigned,epochs> order{{0,1,2,2,1,0}};
    constexpr std::array<unsigned,3> masks{{0,4,8}};
    constexpr std::array<const char*,3> names{{"legacy_native_origins","reused_origin_certification","software_receiver_origins"}};
    constexpr std::array<const char*,10> metricNames{{"contact_gpu_ms","frame_gpu_ms","probe_gpu_ms","loop_ms","scene_ms",
        "record_ms","submit_wait_ms","host_encode_ms","host_fence_ms","host_as_prepare_ms"}};
    constexpr std::array<const char*,21> workNames{{"probe_ray_budget","attribute_uploaded_texels","geometry_uploaded_texels",
        "logical_upload_regions","cache_relit_texels","area_shadow_passes","physical_upload_regions","physical_upload_bytes",
        "deferred_upload_regions","deferred_upload_bytes","dedicated_upload_submissions","dedicated_upload_waits",
        "as_operations","as_full_builds","as_updates","as_storage_reuses","as_buffer_allocations","as_handle_creations",
        "graphics_draw_calls","graphics_uniform_snapshot_copies","staging_allocations"}};
    struct Sample {
        unsigned block=0,epoch=0,mode=0,index=0;
        std::array<double,10> metrics{};
        std::array<std::uint64_t,21> work{};
    };
    std::vector<Sample> samples;samples.reserve(blocks*epochs*measured);
    std::array<std::array<std::vector<double>,10>,3> totals;
    std::array<std::array<std::uint64_t,21>,3> totalWork{};
    auto frame=Frame(options,false);const auto base=SurfaceCacheFreshSamplingCamera(2,frame.OutputAspect(),true);
    const glm::vec3 radiance=frame.effects.areaLights.lights[0].radiance;
    const auto unixMilliseconds=[] {return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();};
    const auto select=[&](unsigned mode) {
        // The mask15 control fixes all Round7 paths while disabling every new
        // execution selector. Masks4/8 only change the origin implementation.
        Mode(frame,mode!=0,false,15,false,false,false,true,0,mode?masks[mode]:15);
        Check(!frame.effects.surfaceCacheGi.updateAccelerationStructures&&!frame.effects.surfaceCacheGi.reuseAccelerationStorage&&
            !frame.effects.screenProbePlacement.trace.sdf.nativeUploadPruning,"Origin study changed AS/upload policy");
    };
    const auto effective=[&](unsigned mode) {
        Effective(lab,mode!=0,false,15,false,false,false,true,0,mode?masks[mode]:15);
        const auto stats=lab.PipelineStatistics();
        Check(stats.probeSdfHardwareRayTracing&&stats.surfaceCacheHardwareOrigins&&stats.surfaceCacheRadianceOnlyFiltering&&
            stats.surfaceCacheHardwareContact&&stats.surfaceCacheHardwareVisibility,"Origin study lost accepted Round7 tracing/filtering");
    };
    const auto one=[](const std::vector<double>& values,const char* label) {
        Check(values.size()==1&&std::isfinite(values.front())&&values.front()>=0,
            std::string("Origin study missing/nonfinite same-frame timing: ")+label);
        return values.front();
    };
    const auto precision=std::cout.precision(9);
    std::cout<<"ORIGIN COST STUDY setup build=Release_NDEBUG size="<<frame.width<<'x'<<frame.height
        <<" path=model modes=3 blocks="<<blocks<<" epochs_per_block="<<epochs<<" measured_per_epoch="<<measured
        <<" model_warm_per_epoch="<<warm<<" initial_model_warm_per_mode="<<initialWarm<<" order=0,1,2,2,1,0"
        <<" present="<<frame.present<<" validation=0 hud=0 application_fps_limit=0 image_readbacks=0 cpu_oracle=0 gpu_atomic_instrumentation=0"
        <<" raw_frame_output_after_measurement=1 main_native_both_groups=1 radiance_filter_all_modes=1"
        <<" as_policy=full_build upload_pruning=0 geometry_and_sampling_math_unchanged=1"
        <<" probe_rays="<<frame.effects.screenProbePlacement.trace.rayResolution*frame.effects.screenProbePlacement.trace.rayResolution
        <<" receiver_rays="<<frame.effects.surfaceCacheGi.contact.raysPerPixel
        <<" paired_block_units=8 frame_independence_claim=0 timestamp_unit=unix_ms telemetry_alignment=epoch_only"<<std::endl;
    for(unsigned mode=0;mode<names.size();++mode)
        std::cout<<"ORIGIN COST STUDY mode mode="<<mode<<" name="<<names[mode]<<" origin_mask="<<masks[mode]
            <<" reused_certification="<<(mode==1)<<" software_receiver_origins="<<(mode==2)<<std::endl;
    for(unsigned mode=0;mode<names.size();++mode) {
        select(mode);Pose(frame,base,2,15,radiance);Reset(frame);
        std::cout<<"ORIGIN COST STUDY prewarm_start mode="<<mode<<" unix_ms="<<unixMilliseconds()<<std::endl;
        Draw(lab,frame);unsigned preparationFrames=1;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
        while(lab.ProbeSdfQualityStatistics().pending) {
            Draw(lab,frame);++preparationFrames;
            Check(std::chrono::steady_clock::now()<deadline,"Origin cost study scene preparation timed out");
        }
        effective(mode);Reset(frame);
        for(unsigned i=0;i<initialWarm;++i){Pose(frame,base,2,i,radiance);glfwPollEvents();Draw(lab,frame);}
        effective(mode);
        std::cout<<"ORIGIN COST STUDY prewarm_end mode="<<mode<<" unix_ms="<<unixMilliseconds()
            <<" initial_model_frames="<<initialWarm<<" preparation_frames="<<preparationFrames<<" timed=0"<<std::endl;
    }
    for(unsigned block=0;block<blocks;++block) {
        std::array<std::uint64_t,21> referenceWork{};
        for(unsigned epoch=0;epoch<epochs;++epoch) {
            const unsigned mode=order[epoch];select(mode);Reset(frame);
            for(unsigned i=0;i<warm;++i){Pose(frame,base,2,i,radiance);glfwPollEvents();Draw(lab,frame);}
            effective(mode);
            std::cout<<"ORIGIN COST STUDY epoch_start block="<<block<<" epoch="<<epoch<<" mode="<<mode
                <<" unix_ms="<<unixMilliseconds()<<" model_warm_frames="<<warm<<std::endl;
            std::array<std::vector<double>,10> epochMetrics;std::array<std::uint64_t,21> epochWork{};
            for(unsigned i=0;i<measured;++i) {
                Pose(frame,base,2,i,radiance);
                const auto begin=std::chrono::steady_clock::now();glfwPollEvents();const auto timing=lab.MeasureFrames(frame,1,0);
                const double loop=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
                frame.effects.cameraCut=false;frame.effects.screenProbePlacement.reset=false;frame.effects.surfaceCacheGi.temporal.reset=false;
                effective(mode);const auto stats=lab.PipelineStatistics();
                Check(timing.backendHostFrameMilliseconds.size()==1&&timing.backendHostFrameStatistics.size()==1&&
                    timing.textureUploadStatistics.size()==1,"Origin study missing same-frame host/upload counters");
                const auto scopes=Scopes(timing);const auto& host=timing.backendHostFrameMilliseconds.front();
                const auto& counters=timing.backendHostFrameStatistics.front();const auto& uploads=timing.textureUploadStatistics.front();
                Sample sample;sample.block=block;sample.epoch=epoch;sample.mode=mode;sample.index=i;
                sample.metrics={one(*scopes[5],"contact"),one(*scopes[0],"GPU frame"),one(*scopes[2],"main probe"),loop,
                    one(timing.scenePreparationFrameMilliseconds,"scene"),one(timing.frameRecordingFrameMilliseconds,"record"),
                    one(timing.submitWaitFrameMilliseconds,"submit wait"),host[1],host[9],host[5]};
                for(double v:sample.metrics)Check(std::isfinite(v)&&v>=0,"Origin study nonfinite/negative timing");
                sample.work={stats.surfaceCacheProbeRayBudget,stats.surfaceCacheAttributeUploadedTexels,stats.surfaceCacheGeometryUploadedTexels,
                    stats.surfaceCacheUploadRegions,stats.surfaceCacheUpdatedTexels,stats.areaLightShadowPasses,uploads[0],uploads[1],
                    uploads[2],uploads[3],uploads[4],uploads[5],counters[3],counters[7],counters[8],counters[4],counters[5],counters[6],
                    counters[0],counters[2],uploads[6]};
                Check(counters[3]==1&&counters[7]==1&&counters[8]==0&&counters[4]==0&&counters[5]==6&&counters[6]==2,
                    "Origin study did not retain one complete allocating AS BUILD per moved frame");
                Check(stats.surfaceCacheReusedOriginCertification==(mode==1)&&stats.surfaceCacheSoftwareReceiverOrigins==(mode==2)&&
                    !stats.probeSdfNativeUploadsPruned,"Origin study changed unrelated execution selection");
                for(unsigned m=0;m<metricNames.size();++m){epochMetrics[m].push_back(sample.metrics[m]);totals[mode][m].push_back(sample.metrics[m]);}
                for(unsigned w=0;w<workNames.size();++w){epochWork[w]+=sample.work[w];totalWork[mode][w]+=sample.work[w];}
                samples.push_back(std::move(sample));
            }
            const auto end=unixMilliseconds();
            if(epoch==0)referenceWork=epochWork;
            else Check(epochWork==referenceWork,"Origin study changed matched ray/cache/shadow/upload/AS/draw work within a block");
            std::cout<<"ORIGIN COST STUDY epoch_end block="<<block<<" epoch="<<epoch<<" mode="<<mode
                <<" unix_ms="<<end<<" samples="<<measured<<" estimator_resource_work_exact=1"<<std::endl;
            for(unsigned m=0;m<metricNames.size();++m)
                std::cout<<"ORIGIN COST STUDY epoch_metric block="<<block<<" epoch="<<epoch<<" mode="<<mode<<" name="<<metricNames[m]
                    <<" samples="<<epochMetrics[m].size()<<" mean_ms="<<Mean(epochMetrics[m])<<" median_ms="<<Median(epochMetrics[m])
                    <<" p95_ms="<<Quantile(epochMetrics[m],.95)<<std::endl;
        }
    }
    // Emit raw records only after all timed epochs. These rows permit a
    // paired-block analysis without treating serial frames as independent.
    for(const auto& sample:samples) {
        std::cout<<"ORIGIN COST STUDY sample block="<<sample.block<<" epoch="<<sample.epoch<<" mode="<<sample.mode<<" index="<<sample.index;
        for(unsigned m=0;m<metricNames.size();++m)std::cout<<' '<<metricNames[m]<<'='<<sample.metrics[m];
        for(unsigned w=0;w<workNames.size();++w)std::cout<<' '<<workNames[w]<<'='<<sample.work[w];
        std::cout<<" main_native=1 probe_origins_native=1 radiance_filter=1 contact_native=1 cache_native=1 reused_certification="<<(sample.mode==1)
            <<" software_receiver_origins="<<(sample.mode==2)<<" present=1"<<'\n';
    }
    for(unsigned mode=0;mode<names.size();++mode) {
        for(unsigned m=0;m<metricNames.size();++m)
            std::cout<<"ORIGIN COST STUDY mode_metric mode="<<mode<<" name="<<metricNames[m]<<" samples="<<totals[mode][m].size()
                <<" mean_ms="<<Mean(totals[mode][m])<<" median_ms="<<Median(totals[mode][m])<<" p95_ms="<<Quantile(totals[mode][m],.95)<<std::endl;
        std::cout<<"ORIGIN COST STUDY mode_work mode="<<mode;
        for(unsigned w=0;w<workNames.size();++w)std::cout<<' '<<workNames[w]<<'='<<totalWork[mode][w];
        std::cout<<std::endl;
    }
    Check(samples.size()==1536&&totals[0][0].size()==512&&totals[1][0].size()==512&&totals[2][0].size()==512&&
        totalWork[0]==totalWork[1]&&totalWork[0]==totalWork[2],"Origin study incomplete samples or mismatched complete work");
    std::cout<<"ORIGIN COST STUDY PASS samples="<<samples.size()<<" per_mode=512 blocks=8 epochs=48 present_calls="<<samples.size()
        <<" matched_estimator_resource_work=1 readbacks=0 cpu_oracle=0 paired_block_units=8 frame_independence_claim=0"
        <<" physical_scanout_claim=0 quality_claim=0"<<std::endl;
    std::cout.precision(precision);
}
