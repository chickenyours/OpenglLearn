// Untimed attribution. One replay writes page0 and all six counter pages in
// the same draw, using three exactly packed additional RGBA32F attachments.
void SurfaceCacheReceiverDiagnostics(MaterialLabModule& lab,const Options& options) {
    using namespace SurfaceCacheNativeFixture;
    Check(options.vulkan&&!options.softwareRays,"Receiver diagnostics require capability-enabled Vulkan");
    const unsigned mask=options.surfaceCacheReceiverMask;ReceiverVariant(mask);
    constexpr unsigned pages=7,warm=16,canonicalWarm=64;
    const unsigned preflightCount=mask>=4?5u:4u;
    const std::array<unsigned,5> preflightModes{{0,1,2,3,mask}};
    Check(options.surfaceCacheReceiverDiagnosticPath<=5&&options.surfaceCacheReceiverDiagnosticMode<=2,"Invalid focused receiver diagnostic selection");
    const unsigned firstPath=options.surfaceCacheReceiverDiagnosticPath<5?options.surfaceCacheReceiverDiagnosticPath:0;
    const unsigned endPath=options.surfaceCacheReceiverDiagnosticPath<5?firstPath+1:5;
    const unsigned firstVariant=options.surfaceCacheReceiverDiagnosticMode<2?options.surfaceCacheReceiverDiagnosticMode:0;
    const unsigned endVariant=options.surfaceCacheReceiverDiagnosticMode<2?firstVariant+1:2;
    const bool focused=firstPath!=0||endPath!=5||firstVariant!=0||endVariant!=2;
    auto frame=Frame(options,false);frame.width=1280;frame.height=800;frame.present=options.benchmarkPresent;
    const auto base=SurfaceCacheFreshSamplingCamera(2,frame.OutputAspect(),true);
    const glm::vec3 radiance=frame.effects.areaLights.lights[0].radiance;
    const std::size_t pixels=std::size_t(frame.width)*frame.height;
    constexpr std::array<const char*,pages> pageNames{{"selected_actual_base_readable","exhausted_miss_backface_cache_readable",
        "origin_native_accepted_miss_edge","origin_invalid_software_certification_retry",
        "gather_native_accepted_miss_edge","gather_invalid_software_certification_retry","mask_geometry_origin_reserved"}};
    std::cout<<"RECEIVER DIAGNOSTICS setup size=1280x800 modes="<<endVariant-firstVariant<<" paths="<<endPath-firstPath<<" pages=7 warm_per_case="<<warm
        <<" focused="<<focused<<" selected_path="<<options.surfaceCacheReceiverDiagnosticPath<<" selected_mode="<<options.surfaceCacheReceiverDiagnosticMode
        <<" canonical_warm_per_case="<<canonicalWarm<<" canonical_model_offset=0 canonical_original_camera_radiance=1 pending_ready_before_trajectory=1 reset_after_canonical_warm=1"
        <<" same_frame_mrt=1 mrt_color_targets=4 packed_targets=3 packing_base=4096 individual_counter_limit=4096 capture_frames_per_case=1 source_hashes_single_read=1 preflight_probe_evidence=1"
        <<" accepted_round9_query_mask=12 receiver_software_origins="<<bool(mask&8)<<" receiver_mask="<<mask<<" present="<<frame.present
        <<" untimed=1 same_target_readback_api=1 page0_unchanged=1 atomics=0 single_replay_per_case=1"
        <<" origin_pages_replay_original_safety_even_with_split=1 production_origin_cost_not_inferred=1"<<std::endl;
    // Confirm the exact host/resource plan before the expensive replay and
    // dense study. Print every observed value before any paired-delta guard.
    std::array<std::array<std::uint64_t,9>,5> preflightHost{};
    std::array<std::array<std::uint64_t,8>,5> preflightUploads{};
    std::array<std::array<std::uint64_t,5>,5> preflightSignatures{};
    std::array<std::vector<glm::vec4>,5> preflightMasks;
    std::array<ScreenProbePlacementReadback,5> preflightPlacements;
    std::vector<glm::vec4> baselineSourcePositions,baselineSourceNormals;
    constexpr std::array<const char*,4> probeGroupNames{{"uniform","uniform_normals","spawned","spawned_normals"}};
    const auto probeGroups=[](const ScreenProbePlacementReadback& value){return std::array<const std::vector<glm::vec4>*,4>{{
        &value.uniform,&value.uniformNormals,&value.spawned,&value.spawnedNormals}};};
    const auto priorPrecision=std::cout.precision(9);
    const auto logCpuFloatingPoint=[](unsigned mode,const char* phase){
        unsigned mxcsr=0;bool mxcsrAvailable=false;
#if defined(_M_IX86) || defined(_M_X64) || defined(__SSE__)
        mxcsr=_mm_getcsr();mxcsrAvailable=true;
#endif
        std::cout<<"RECEIVER DIAGNOSTICS preflight_cpu_fp mode="<<mode<<" phase="<<phase
            <<" rounding_mode="<<std::fegetround()<<" mxcsr="<<mxcsr<<" mxcsr_available="<<mxcsrAvailable
            <<" environment_modified=0 timing_claim=0"<<std::endl;
    };
    for(unsigned mode=0;mode<preflightCount;++mode){
        const unsigned preflightMask=preflightModes[mode];
        Mode(frame,true,false,15,false,false,false,true,0,0,12,preflightMask);
        frame.effects.surfaceCacheGi.contact.collectSamplingDiagnostics=false;
        frame.effects.surfaceCacheGi.contact.collectTraceDiagnostics=false;
        frame.effects.surfaceCacheGi.contact.collectTraceTimings=false;
        Pose(frame,base,0,0,radiance);Reset(frame);Draw(lab,frame);
        const auto readyDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
        while(lab.ProbeSdfQualityStatistics().pending){Draw(lab,frame);Check(std::chrono::steady_clock::now()<readyDeadline,"Receiver preflight preparation timed out");}
        for(unsigned i=0;i<canonicalWarm;++i){Pose(frame,base,0,0,radiance);glfwPollEvents();Draw(lab,frame);}
        Check(!lab.ProbeSdfQualityStatistics().pending,"Receiver preflight canonical warm ended with pending SDF resources");
        Reset(frame);
        std::uint64_t preflightHistoryMask=0,preflightRelightMask=0;unsigned preflightCommittedSequence=0;
        for(unsigned i=0;i<warm;++i){Pose(frame,base,2,i,radiance);glfwPollEvents();Draw(lab,frame);const auto stats=lab.PipelineStatistics();
            preflightHistoryMask|=std::uint64_t(stats.surfaceCacheHistoryUsed)<<i;preflightRelightMask|=std::uint64_t(stats.surfaceCacheLightingUpdated)<<i;
            preflightCommittedSequence=stats.surfaceCacheHistoryUsed?preflightCommittedSequence+1:1;}
        Pose(frame,base,2,7,radiance);glfwPollEvents();logCpuFloatingPoint(preflightMask,"before_measure");
        const auto timing=lab.MeasureFrames(frame,1,0);logCpuFloatingPoint(preflightMask,"after_measure");
        frame.effects.cameraCut=false;frame.effects.screenProbePlacement.reset=false;frame.effects.surfaceCacheGi.temporal.reset=false;
        Effective(lab,true,false,15,false,false,false,true,0,0,12,preflightMask);
        Check(timing.backendHostFrameStatistics.size()==1&&timing.textureUploadStatistics.size()==1,"Receiver preflight lacks same-frame resource counters");
        preflightHost[mode]=timing.backendHostFrameStatistics.front();preflightUploads[mode]=timing.textureUploadStatistics.front();
        const auto source=lab.ReadbackScreenProbeSources();const auto fresh=lab.ReadbackSurfaceCacheFreshSampling();
        const auto risk=lab.ReadbackSurfaceCacheContactMask();const bool freshAvailable=lab.PipelineStatistics().surfaceCacheFreshSamplingCollected;
        Check(source.sourceViewPositions.size()==pixels&&source.sourceWorldNormals.size()==pixels&&risk.size()==pixels&&
            (freshAvailable?fresh.size()==pixels:fresh.empty()),"Receiver preflight source extent disagrees with actual collection");
        preflightSignatures[mode]={Hash(source.sourceViewPositions),Hash(source.sourceWorldNormals),Hash(risk),freshAvailable?Hash(fresh):0u,std::uint64_t(freshAvailable)};
        preflightMasks[mode]=risk;
        if(!mode){baselineSourcePositions=source.sourceViewPositions;baselineSourceNormals=source.sourceWorldNormals;}
        const auto probeRisk=lab.ReadbackSurfaceCacheContactProbeRisk(),probeOrigins=lab.ReadbackSurfaceCacheProbeOrigins();
        const auto probePlacement=lab.ReadbackScreenProbePlacement();const auto probeRays=lab.ReadbackScreenProbeTrace(false,false,false);
        preflightPlacements[mode]=probePlacement;
        std::vector<float> rayStatuses;rayStatuses.reserve(probeRays.radianceStatus.size());for(const auto& ray:probeRays.radianceStatus)rayStatuses.push_back(ray.w);
        std::cout<<"RECEIVER DIAGNOSTICS preflight mode="<<preflightMask<<" receiver_mask="<<preflightMask<<" warm_model_frames="<<warm
            <<" path=model preflight=1 replay=0 contact_trace_timings=0 timing_claim=0 canonical_warm_frames="<<canonicalWarm<<" canonical_model_offset=0 reset_after_canonical=1"
            <<" trajectory_history_mask="<<preflightHistoryMask<<" trajectory_relight_mask="<<preflightRelightMask
            <<" inferred_temporal_sequence="<<(lab.PipelineStatistics().surfaceCacheHistoryUsed?preflightCommittedSequence:0u)<<" probe_phase="<<probePlacement.frameIndex;
        for(unsigned c=0;c<HostCounterNames.size();++c)std::cout<<' '<<HostCounterNames[c]<<'='<<preflightHost[mode][c];
        constexpr std::array<const char*,8> uploadNames{{"physical_regions","physical_bytes","deferred_regions","deferred_bytes",
            "dedicated_immediate_submissions","dedicated_immediate_fence_waits","staging_allocations","maximum_staging_capacity_bytes"}};
        for(unsigned c=0;c<uploadNames.size();++c)std::cout<<' '<<uploadNames[c]<<'='<<preflightUploads[mode][c];
        std::cout<<" positions_hash="<<preflightSignatures[mode][0]<<" normals_hash="<<preflightSignatures[mode][1]<<" mask_hash="<<preflightSignatures[mode][2]
            <<" fresh_hash="<<preflightSignatures[mode][3]<<" fresh_available="<<preflightSignatures[mode][4];
        std::cout<<" probe_risk_hash="<<Hash(probeRisk)<<" probe_origins_hash="<<Hash(probeOrigins)<<" probe_uniform_hash="<<Hash(probePlacement.uniform)
            <<" probe_spawned_hash="<<Hash(probePlacement.spawned)<<" probe_status_hash="<<Hash(rayStatuses)<<" ray_hit_geometry_hash="<<Hash(probeRays.hitWorldDistance)
            <<" ray_hit_source_hash="<<Hash(probeRays.hitPixelStepsMip)<<" ray_direction_hash="<<Hash(probeRays.rayDirectionDepthFetches);
        std::cout<<std::endl;
        for(unsigned stage=0;stage<2;++stage){
            const auto& constants=stage?probePlacement.placementConstants:probePlacement.placementUniformConstants;
            const auto words=std::bit_cast<std::array<std::uint32_t,100>>(constants);
            std::cout<<"RECEIVER DIAGNOSTICS preflight_placement_constants mode="<<preflightMask<<" stage="<<(stage?"final":"uniform")
                <<" constants_source=committed_cpu bytes="<<sizeof(constants)<<" constants_hash="<<Hash(std::vector<Render::ScreenProbePlacementConstants>{constants})
                <<" placement_image="<<probePlacement.placementImage<<" base_sampling_seed="<<probePlacement.baseSamplingSeed
                <<" ray_sampling_seed="<<probePlacement.raySamplingSeed<<" gi_temporal_sequence="<<probePlacement.giTemporalSequence
                <<" gi_temporal_sequence_available="<<probePlacement.giTemporalSequenceAvailable
                <<" inverse_view_hash="<<Hash(std::vector<glm::mat4>{constants.inverseView})<<" inverse_projection_hash="<<Hash(std::vector<glm::mat4>{constants.inverseProjection})
                <<" view_projection_hash="<<Hash(std::vector<glm::mat4>{constants.viewProjection})<<" view_hash="<<Hash(std::vector<glm::mat4>{constants.view})
                <<" phase="<<constants.screen.z<<" tracing_mode="<<constants.tracing.w<<" refinement_flags="<<constants.refinement.x;
            for(unsigned word=0;word<words.size();++word)std::cout<<" word_"<<word<<'='<<words[word];
            std::cout<<" raw_format=uint32_little_endian timing_claim=0"<<std::endl;
        }
        const auto groups=probeGroups(preflightPlacements[mode]),referenceGroups=probeGroups(preflightPlacements[0]);
        for(unsigned group=0;group<groups.size();++group){
            const auto& actual=*groups[group];const auto& reference=*referenceGroups[group];
            std::vector<float> xyz,alpha;xyz.reserve(actual.size()*3);alpha.reserve(actual.size());
            for(const auto& value:actual){xyz.push_back(value.x);xyz.push_back(value.y);xyz.push_back(value.z);alpha.push_back(value.w);}
            std::cout<<"RECEIVER DIAGNOSTICS preflight_probe_hash mode="<<preflightMask<<" group="<<probeGroupNames[group]
                <<" entries="<<actual.size()<<" full_hash="<<Hash(actual)<<" xyz_hash="<<Hash(xyz)<<" w_hash="<<Hash(alpha)<<" timing_claim=0"<<std::endl;
            if(!mode)continue;
            Check(actual.size()==reference.size(),"Receiver preflight probe evidence extent changed");
            std::array<std::uint64_t,4> different{};std::array<std::size_t,4> first{{actual.size(),actual.size(),actual.size(),actual.size()}};
            glm::vec4 maximumDelta(0);
            for(std::size_t i=0;i<actual.size();++i)for(unsigned c=0;c<4;++c){
                if(std::bit_cast<std::uint32_t>(actual[i][c])!=std::bit_cast<std::uint32_t>(reference[i][c])){
                    ++different[c];first[c]=std::min(first[c],i);}
                maximumDelta[c]=std::max(maximumDelta[c],std::abs(actual[i][c]-reference[i][c]));
            }
            std::cout<<"RECEIVER DIAGNOSTICS preflight_probe_difference mode="<<preflightMask<<" baseline_mode=0 group="<<probeGroupNames[group]<<" entries="<<actual.size();
            constexpr std::array<const char*,4> components{{"x","y","z","w"}};
            for(unsigned c=0;c<4;++c){std::cout<<" bitdiff_"<<components[c]<<'='<<different[c]<<" maximum_abs_"<<components[c]<<'='<<maximumDelta[c]
                    <<" first_"<<components[c]<<"_entry="<<first[c];
                if(first[c]<actual.size())std::cout<<" first_"<<components[c]<<"_reference="<<reference[first[c]][c]<<" first_"<<components[c]<<"_actual="<<actual[first[c]][c];}
            std::cout<<" strict_input_gate_unchanged=1 timing_claim=0"<<std::endl;
            const auto firstXyz=std::min({first[0],first[1],first[2]});
            if(firstXyz<actual.size()&&(group==0||group==2)){
                const auto before=reference[firstXyz],after=actual[firstXyz];
                const auto dump=[&](const char* side,glm::vec4 value,const std::vector<glm::vec4>& positions,const std::vector<glm::vec4>& normals){
                    const auto clip=base.projection*base.view*glm::vec4(glm::vec3(value),1);
                    const auto projected=clip.w>0?(glm::vec2(clip)/clip.w*.5f+.5f)*glm::vec2(frame.width,frame.height):glm::vec2(-1);
                    const int x=int(std::floor(projected.x)),y=int(std::floor(projected.y));
                    std::cout<<"RECEIVER DIAGNOSTICS preflight_probe_source mode="<<preflightMask<<" group="<<probeGroupNames[group]<<" entry="<<firstXyz<<" side="<<side
                        <<" projected_x="<<projected.x<<" projected_y="<<projected.y<<" pixel_x="<<x<<" pixel_y="<<y;
                    if(x>=0&&y>=0&&x<int(frame.width)&&y<int(frame.height)){const auto index=std::size_t(y)*frame.width+unsigned(x);
                        const auto p=positions[index],n=normals[index],world=glm::inverse(base.view)*glm::vec4(glm::vec3(p),1);
                        std::cout<<" source_x="<<p.x<<" source_y="<<p.y<<" source_z="<<p.z<<" source_w="<<p.w
                            <<" normal_x="<<n.x<<" normal_y="<<n.y<<" normal_z="<<n.z<<" normal_w="<<n.w
                            <<" cpu_world_x="<<world.x<<" cpu_world_y="<<world.y<<" cpu_world_z="<<world.z;}
                    std::cout<<" probe_x="<<value.x<<" probe_y="<<value.y<<" probe_z="<<value.z<<" probe_w="<<value.w<<" timing_claim=0"<<std::endl;
                };
                dump("reference",before,baselineSourcePositions,baselineSourceNormals);dump("actual",after,source.sourceViewPositions,source.sourceWorldNormals);
            }
        }
    }
    for(unsigned mode=0;mode<preflightCount;++mode){
        const unsigned preflightMask=preflightModes[mode];
        if(mode){
            std::uint64_t differentRgba=0,differentRgb=0,activeChanged=0,riskChanged=0,validChanged=0,firstDifference=pixels;
            std::uint64_t activeBefore=0,activeAfter=0,riskBefore=0,riskAfter=0;glm::vec4 maximumDelta(0);
            for(std::size_t pixel=0;pixel<pixels;++pixel){const auto a=preflightMasks[0][pixel],b=preflightMasks[mode][pixel];bool rgba=false,rgb=false;
                for(unsigned c=0;c<4;++c){const bool changed=std::bit_cast<std::uint32_t>(a[c])!=std::bit_cast<std::uint32_t>(b[c]);rgba|=changed;if(c<3)rgb|=changed;
                    maximumDelta[c]=std::max(maximumDelta[c],std::abs(b[c]-a[c]));}
                differentRgba+=rgba;differentRgb+=rgb;if(rgba&&firstDifference==pixels)firstDifference=pixel;
                activeChanged+=(a.x>.000001f)!=(b.x>.000001f);riskChanged+=(a.x>=.25f)!=(b.x>=.25f);validChanged+=(a.w>.5f)!=(b.w>.5f);
                activeBefore+=a.x>.000001f;activeAfter+=b.x>.000001f;riskBefore+=a.x>=.25f;riskAfter+=b.x>=.25f;
            }
            std::cout<<"RECEIVER DIAGNOSTICS preflight_mask_difference mode="<<preflightMask<<" baseline_mode=0 pixels="<<pixels
                <<" different_rgba_pixels="<<differentRgba<<" different_rgb_pixels="<<differentRgb<<" active_threshold_changed="<<activeChanged
                <<" risk_quarter_threshold_changed="<<riskChanged<<" valid_threshold_changed="<<validChanged<<" active_before="<<activeBefore<<" active_after="<<activeAfter
                <<" risk_before="<<riskBefore<<" risk_after="<<riskAfter<<" maximum_abs_x="<<maximumDelta.x<<" maximum_abs_y="<<maximumDelta.y
                <<" maximum_abs_z="<<maximumDelta.z<<" maximum_abs_w="<<maximumDelta.w<<" first_difference_pixel="<<firstDifference;
            if(firstDifference<pixels){const auto a=preflightMasks[0][firstDifference],b=preflightMasks[mode][firstDifference];
                std::cout<<" first_reference_x="<<a.x<<" first_reference_y="<<a.y<<" first_reference_z="<<a.z<<" first_reference_w="<<a.w
                    <<" first_actual_x="<<b.x<<" first_actual_y="<<b.y<<" first_actual_z="<<b.z<<" first_actual_w="<<b.w;}
            std::cout<<" exact_hash_gate_retained=1 timing_claim=0"<<std::endl;
        }
    }
    for(unsigned mode=0;mode<preflightCount;++mode){
        const unsigned preflightMask=preflightModes[mode];
        const unsigned split=(preflightMask&1)?1u:0u;
        for(unsigned c=0;c<HostCounterNames.size();++c){const unsigned extra=split*(c==0?1u:c==1?3u:0u);
            Check(preflightHost[mode][c]==preflightHost[0][c]+extra,"Receiver preflight violates the exact disclosed draw/descriptor/AS plan; raw values precede this guard");}
        for(unsigned c=0;c<preflightUploads[0].size();++c)Check(preflightUploads[mode][c]==preflightUploads[0][c],"Receiver preflight changed actual upload/staging work");
        Check(preflightHost[mode][3]==1&&preflightHost[mode][4]==1&&preflightHost[mode][5]==0&&preflightHost[mode][6]==0&&
            preflightHost[mode][7]==0&&preflightHost[mode][8]==1,"Receiver preflight lost allocation-free AS UPDATE");
        Check(preflightSignatures[mode]==preflightSignatures[0],"Receiver preflight model warm produced unequal geometry/mask/fresh states; raw hashes precede this guard");
    }
    std::cout<<"RECEIVER DIAGNOSTICS preflight_PASS modes="<<preflightCount<<" model_warm=16 samples_per_mode=1 preflight=1 timing_claim=0 exact_draw_delta=1 exact_descriptor_delta=3 other_host_counters_exact=1 uploads_exact=1 model_source_mask_fresh_exact=1"<<std::endl;
    unsigned frameCase=0;
    std::array<std::vector<std::uint32_t>,5> baselineReceiverSafety;
    for(unsigned variant=firstVariant;variant<endVariant;++variant){
        for(unsigned path=firstPath;path<endPath;++path,++frameCase){
            Mode(frame,variant!=0,false,15,false,false,false,true,0,0,0,mask);
            frame.effects.surfaceCacheGi.contact.collectSamplingDiagnostics=false;
            frame.effects.surfaceCacheGi.contact.collectTraceDiagnostics=false;
            Pose(frame,base,0,0,radiance);Reset(frame);Draw(lab,frame);
            unsigned preparationFrames=1;const auto readyDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
            while(lab.ProbeSdfQualityStatistics().pending){Draw(lab,frame);++preparationFrames;
                Check(std::chrono::steady_clock::now()<readyDeadline,"Receiver canonical diagnostic preparation timed out");}
            for(unsigned i=0;i<canonicalWarm;++i){Pose(frame,base,0,0,radiance);glfwPollEvents();Draw(lab,frame);}
            Check(!lab.ProbeSdfQualityStatistics().pending,"Receiver canonical warm ended with pending SDF resources");
            std::cout<<"RECEIVER DIAGNOSTICS warm variant="<<(variant?ReceiverVariant(mask):"accepted_round9")<<" path="<<PathNames[path]
                <<" frame_case="<<frameCase<<" canonical_frames="<<canonicalWarm<<" preparation_frames="<<preparationFrames
                <<" pending=0 model_offset=0 original_camera_radiance=1 reset_next=1 timed=0"<<std::endl;
            Pose(frame,base,path,0,radiance);Reset(frame);
            std::uint64_t historyMask=0,pendingMask=0,relightMask=0;unsigned committedSequence=0;
            for(unsigned i=0;i<warm;++i){Pose(frame,base,path,i,radiance);glfwPollEvents();Draw(lab,frame);const auto stats=lab.PipelineStatistics();
                historyMask|=std::uint64_t(stats.surfaceCacheHistoryUsed)<<i;pendingMask|=std::uint64_t(lab.ProbeSdfQualityStatistics().pending)<<i;
                relightMask|=std::uint64_t(stats.surfaceCacheLightingUpdated)<<i;committedSequence=stats.surfaceCacheHistoryUsed?committedSequence+1:1;}
            frame.effects.surfaceCacheGi.contact.collectSamplingDiagnostics=true;
            frame.effects.surfaceCacheGi.contact.collectTraceDiagnostics=true;
            frame.effects.surfaceCacheGi.contact.traceDiagnosticPage=0;
            Pose(frame,base,path,7,radiance);glfwPollEvents();Draw(lab,frame);
            Effective(lab,variant!=0,false,15,false,false,false,true,0,0,0,mask);
            const auto stats=lab.PipelineStatistics();
            Check(stats.surfaceCacheReceiverTraceDiagnosticsCollected,"Receiver same-frame MRT diagnostics were not actually collected");
            const auto source=lab.ReadbackScreenProbeSources();const auto risk=lab.ReadbackSurfaceCacheContactMask();
            const bool freshAvailable=stats.surfaceCacheFreshSamplingCollected;const auto fresh=lab.ReadbackSurfaceCacheFreshSampling();
            Check(source.sourceViewPositions.size()==pixels&&source.sourceWorldNormals.size()==pixels&&risk.size()==pixels,
                "Receiver diagnostic source geometry/mask extent is incomplete");
            Check(freshAvailable?fresh.size()==pixels:fresh.empty(),"Receiver diagnostic fresh extent disagrees with the actual collected flag");
            const std::array<std::uint64_t,5> signature{{Hash(source.sourceViewPositions),Hash(source.sourceWorldNormals),Hash(risk),
                freshAvailable?Hash(fresh):0u,std::uint64_t(freshAvailable)}};
            const auto probes=lab.ReadbackScreenProbePlacement();
            std::array<std::vector<glm::vec4>,pages> values;
            values[0]=lab.ReadbackSurfaceCacheReceiverSampling();Check(values[0].size()==pixels,"Receiver diagnostic page0 extent changed");
            const auto packed=lab.ReadbackSurfaceCacheContactTraceDiagnostics();
            for(unsigned page=1;page<pages;++page)values[page].resize(pixels);
            for(unsigned target=0;target<packed.size();++target){
                Check(packed[target].size()==pixels,"Receiver diagnostic packed target extent changed");
                std::uint64_t sum=0;std::uint32_t maximum=0;
                for(std::size_t pixel=0;pixel<pixels;++pixel)for(unsigned c=0;c<4;++c){
                    const float value=packed[target][pixel][c];
                    const bool exact=std::isfinite(value)&&value>=0&&value<16777216.f&&value==std::floor(value);
                    if(!exact)std::cout<<"RECEIVER DIAGNOSTICS packed_invalid frame_case="<<frameCase<<" target="<<target+1<<" pixel="<<pixel
                        <<" channel="<<c<<" value="<<value<<" exact_integer_limit=16777216"<<std::endl;
                    Check(exact,"Receiver packed diagnostic is outside exact integer [0,2^24)");
                    const auto word=std::uint32_t(value);sum+=word;maximum=std::max(maximum,word);
                    const unsigned page=1+2*target+c/2,component=2*(c%2);
                    values[page][pixel][component]=float(word%4096u);
                    values[page][pixel][component+1]=float(word/4096u);
                }
                std::cout<<"RECEIVER DIAGNOSTICS packed variant="<<(variant?ReceiverVariant(mask):"accepted_round9")<<" path="<<PathNames[path]
                    <<" frame_case="<<frameCase<<" target="<<target+1<<" pixels="<<pixels<<" packed_hash="<<Hash(packed[target])
                    <<" sum_packed_words="<<sum<<" maximum_packed_word="<<maximum<<" packing_base=4096 exact_pair_decode=1 same_frame_mrt=1 untimed=1"<<std::endl;
            }
            std::array<std::array<std::uint64_t,4>,pages> totals{};
            for(unsigned page=0;page<pages;++page){
                for(const auto& value:values[page])for(unsigned c=0;c<4;++c){
                    Check(std::isfinite(value[c])&&value[c]>=0&&value[c]<16777216.f&&value[c]==std::floor(value[c]),"Receiver diagnostic counter is not an exact finite nonnegative integer");
                    if(page)Check(value[c]<4096.f,"Receiver diagnostic individual count exceeds the packing base");
                    totals[page][c]+=std::uint64_t(value[c]);
                }
                std::cout<<"RECEIVER DIAGNOSTICS page variant="<<(variant?ReceiverVariant(mask):"accepted_round9")<<" path="<<PathNames[path]
                    <<" frame_case="<<frameCase<<" page="<<page<<" channels="<<pageNames[page]<<" pixels="<<pixels<<" x="<<totals[page][0]<<" y="<<totals[page][1]
                    <<" z="<<totals[page][2]<<" w="<<totals[page][3]<<" positions_hash="<<signature[0]<<" normals_hash="<<signature[1]
                    <<" mask_hash="<<signature[2]<<" fresh_hash="<<signature[3]<<" fresh_available="<<signature[4]
                    <<" probe_phase="<<probes.frameIndex<<" trajectory_history_mask="<<historyMask<<" trajectory_pending_mask="<<pendingMask
                    <<" trajectory_relight_mask="<<relightMask<<" inferred_temporal_sequence="<<(stats.surfaceCacheHistoryUsed?committedSequence:0u)
                    <<" capture_pose_index=7 replay_hash="<<Hash(values[page])<<" same_frame_mrt=1 source_hashes_single_read=1 untimed=1"<<std::endl;
            }
            std::uint64_t unknown=0;
            if((mask&8)&&!variant)baselineReceiverSafety[path].resize(pixels);
            for(std::size_t pixel=0;pixel<pixels;++pixel){
                const auto plan=values[0][pixel],outcome=values[1][pixel],state=values[6][pixel];
                Check(plan.x<=1&&plan.z==8&&plan.y<=32&&unsigned(plan.y)%8==0&&plan.w<=plan.y,"Receiver diagnostics changed the actual-N/readable contract");
                Check(outcome.y+outcome.z+outcome.w==plan.w&&outcome.x+plan.w<=plan.y,"Receiver diagnostic terminal/readability accounting disagrees");
                const auto pixelUnknown=std::uint32_t(plan.y-outcome.x-plan.w);unknown+=pixelUnknown;
                Check(state.x<=1&&state.y<=state.x&&state.z<=state.y&&state.w==0&&((plan.y>0)==(state.z>0)),
                    "Receiver diagnostic mask/geometry/origin accounting disagrees");
                if((mask&8)&&firstVariant==0){
                    const auto safety=std::uint32_t(plan.x)|(std::uint32_t(plan.y)<<1)|(std::uint32_t(plan.w)<<7)|(pixelUnknown<<13)|
                        (std::uint32_t(state.x)<<19)|(std::uint32_t(state.y)<<20)|(std::uint32_t(state.z)<<21)|(std::uint32_t(outcome.x)<<22);
                    if(!variant)baselineReceiverSafety[path][pixel]=safety;
                    else Check(baselineReceiverSafety[path].size()==pixels&&baselineReceiverSafety[path][pixel]==safety,
                        "Receiver software origins changed per-pixel actual-N/readability/unknown or launch safety");
                }
                for(unsigned phase=0;phase<2;++phase){const auto low=values[2+2*phase][pixel],high=values[3+2*phase][pixel];
                    Check(low.y+low.z+low.w+high.x<=low.x&&high.w<=high.y,"Receiver helper query/retry accounting is inconsistent");}
            }
            std::cout<<"RECEIVER DIAGNOSTICS accounting variant="<<(variant?ReceiverVariant(mask):"accepted_round9")<<" path="<<PathNames[path]
                <<" frame_case="<<frameCase<<" pixels="<<pixels<<" actual_rays="<<totals[0][1]<<" readable_rays="<<totals[0][3]<<" cache_unknown_rays="<<unknown
                <<" exhausted_rays="<<totals[1][0]<<" active_pixels="<<totals[6][0]<<" valid_pixels="<<totals[6][1]<<" launched_pixels="<<totals[6][2]
                <<" same_frame_mrt=1 source_hashes_single_read=1 receiver_software_origin_safety_exact="<<bool((mask&8)&&firstVariant==0)<<" exact_integer_accounting=1 timed=0"<<std::endl;
        }
    }
    std::cout<<"RECEIVER DIAGNOSTICS PASS modes="<<endVariant-firstVariant<<" paths="<<endPath-firstPath<<" pages=7 page_rows="<<frameCase*pages
        <<" accounting_rows="<<frameCase<<" warm_rows="<<frameCase<<" packed_rows="<<frameCase*3<<" captured_frames="<<frameCase
        <<" same_frame_mrt=1 focused="<<focused<<" preflight_modes="<<preflightCount<<" untimed=1 quality_claim=0 performance_claim=0"<<std::endl;
    std::cout.precision(priorPrecision);
}
