// Untimed ordinary-atlas parity. Non-1024 texture widths and signed-index
// fallback require the separate low-level shader contract; these prepared
// MaterialLab scene atlases use the existing fixed 1024-pixel width.
void SurfaceCacheSdfEfficiencyContracts(MaterialLabModule& lab,const Options& options,bool reuseRound=false) {
    using namespace SurfaceCacheNativeFixture;
    const bool addressingRound=!reuseRound;
    const char* tag=reuseRound?"SDF REUSE CONTRACT":"SDF EFFICIENCY CONTRACT";
    Check(options.vulkan&&!options.softwareRays,"SDF execution optimization contracts require capability-enabled Vulkan");
    auto frame=Frame(options,true);frame.present=false;frame.width=192;frame.height=128;
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;auto& trace=placement.trace;
    gi.contact.collectSamplingDiagnostics=false;gi.spatial.enabled=false;
    placement.baseSpacing=32;placement.candidateCount=8;trace.rayResolution=4;trace.samplingSeed=29;
    gi.areaShadows.tileResolution=64;frame.effects.areaLights.samplesPerAxis=4;
    frame.camera=SealedGiRoomCameraPreset(0,frame.OutputAspect());
    Mode(frame,false,true,15,false,false,addressingRound,reuseRound);Reset(frame);Draw(lab,frame);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
    while(lab.ProbeSdfQualityStatistics().pending){Draw(lab,frame);
        Check(std::chrono::steady_clock::now()<deadline,"SDF execution optimization contract scene preparation timed out");}
    std::uint64_t packedTexels=0,packedChannels=0,depthPixels=0;
    std::uint64_t screenHints=0,nonzeroScreen=0,finalMeshRecords=0,finalGlobalRecords=0;
    unsigned pairs=0,resizes=0,toggleHistoryChecks=0;glm::uvec2 previousSize(0);
    const auto compare=[&](const auto& before,const auto& after,unsigned test,const char* label){
        Check(!before.empty()&&before.size()==after.size()&&before.size()%4==0,"SDF execution optimization changed complete physical packed capacity");
        packedTexels+=before.size();
        for(std::size_t texel=0;texel<before.size();++texel)for(unsigned channel=0;channel<4;++channel){
            const float a=before[texel][channel],b=after[texel][channel];
            Check(std::isfinite(a)&&std::isfinite(b),"SDF execution optimization produced nonfinite packed output");
            if(std::bit_cast<std::uint32_t>(a)!=std::bit_cast<std::uint32_t>(b)){
                std::ostringstream detail;
                detail<<tag<<" packed_mismatch test="<<test<<" stage="<<label<<" texel="<<texel<<" channel="<<channel
                    <<" before_bits=0x"<<std::hex<<std::bit_cast<std::uint32_t>(a)<<" after_bits=0x"<<std::bit_cast<std::uint32_t>(b)<<std::dec
                    <<std::setprecision(12)<<" before="<<a<<" after="<<b<<" abs_diff="<<std::abs(double(a)-double(b));
                std::cout<<detail.str()<<std::endl;
                const auto ray=texel/4;const unsigned raysPerProbe=trace.rayResolution*trace.rayResolution;
                for(unsigned part=0;part<4;++part){std::ostringstream record;
                    record<<tag<<" packed_ray test="<<test<<" stage="<<label<<" ray="<<ray
                        <<" probe="<<ray/raysPerProbe<<" ray_in_probe="<<ray%raysPerProbe<<" record="<<part<<std::setprecision(12);
                    record<<" before_values=(";for(unsigned c=0;c<4;++c)record<<(c?",":"")<<before[ray*4+part][c];
                    record<<") after_values=(";for(unsigned c=0;c<4;++c)record<<(c?",":"")<<after[ray*4+part][c];
                    record<<") before_bits=("<<std::hex;
                    for(unsigned c=0;c<4;++c)record<<(c?",0x":"0x")<<std::bit_cast<std::uint32_t>(before[ray*4+part][c]);
                    record<<") after_bits=(";for(unsigned c=0;c<4;++c)record<<(c?",0x":"0x")<<std::bit_cast<std::uint32_t>(after[ray*4+part][c]);
                    record<<')';std::cout<<record.str()<<std::endl;}
                Check(false,"SDF execution optimization changed exact packed FP32 bits");}
            ++packedChannels;}};
    for(unsigned test=0;test<12;++test){
        frame.width=test<3?192:test<9?191:160;frame.height=test<3?128:test<9?123:101;
        trace.rayResolution=test<3||test==11?4:test<8?8:16;
        trace.samplingSeed=test<6?29:0x13579u;
        trace.sdf.useMesh=test!=2&&test!=3;trace.sdf.useGlobal=test!=1&&test!=3;
        trace.sdf.meshTraceDistance=test==7?0.f:test==8?.04f:2.f;
        trace.sdf.maxSteps=test==6?8:192;trace.maxSteps=test==5?8:128;
        gi.safeProbeOrigins=test!=4;frame.screenProbeStudyState.occluderOffset=test>=6?.17f:0.f;
        frame.camera=SealedGiRoomCameraPreset(test==10?1:0,frame.OutputAspect());
        Mode(frame,false,true,15,false,false,addressingRound,reuseRound);Reset(frame);Draw(lab,frame);Effective(lab,false,true,15,false,false,addressingRound,reuseRound);
        const auto screenBefore=lab.ReadbackScreenProbeTrace(false,true,true),sdfBefore=lab.ReadbackScreenProbeTrace(false,false,true);
        const auto depthBefore=lab.ReadbackAreaLightShadowDepth();const auto depthSignature=AreaDepthSignature(depthBefore);
        Mode(frame,true,true,15,false,false,addressingRound,reuseRound);Reset(frame);Draw(lab,frame);Effective(lab,true,true,15,false,false,addressingRound,reuseRound);
        const auto screenAfter=lab.ReadbackScreenProbeTrace(false,true,true),sdfAfter=lab.ReadbackScreenProbeTrace(false,false,true);
        const auto depthAfter=lab.ReadbackAreaLightShadowDepth();
        compare(screenBefore.packed,screenAfter.packed,test,"screen");compare(sdfBefore.packed,sdfAfter.packed,test,"finalSDF");
        Check(AreaDepthSignature(depthAfter)==depthSignature,"SDF execution optimization changed direct-shadow extent/signature");
        depthPixels+=CompareAreaDepth(depthBefore.depth,depthAfter.depth);++pairs;
        if(previousSize.x&&previousSize!=glm::uvec2(frame.width,frame.height))++resizes;previousSize={frame.width,frame.height};
        for(std::size_t ray=0;ray<screenAfter.packed.size()/4;++ray){
            const int screenStatus=int(std::lround(screenAfter.packed[ray*4].w));
            const int finalStatus=int(std::lround(sdfAfter.packed[ray*4].w));
            screenHints+=screenStatus==1;nonzeroScreen+=screenStatus!=0;
            finalMeshRecords+=finalStatus==9||finalStatus==11;finalGlobalRecords+=finalStatus==10||finalStatus==12;
        }
        if(test==0){
            // These execution variants keep the estimator/history identity.
            // Toggle the tested optimization both ways without a reset.
            for(unsigned toggle=0;toggle<2;++toggle){const bool candidate=toggle!=0;
                Mode(frame,candidate,true,15,false,false,addressingRound,reuseRound);Draw(lab,frame);Effective(lab,candidate,true,15,false,false,addressingRound,reuseRound);
                Check(lab.PipelineStatistics().surfaceCacheHistoryUsed,"Pure SDF execution toggle discarded valid GI history");
                AreaDepthSignature(lab.ReadbackAreaLightShadowDepth());++toggleHistoryChecks;}
        }
        std::cout<<tag<<" pair="<<test<<" size="<<frame.width<<'x'<<frame.height<<" ray_resolution="<<trace.rayResolution
            <<" mesh="<<trace.sdf.useMesh<<" global="<<trace.sdf.useGlobal<<" safe_origins="<<gi.safeProbeOrigins
            <<" reuse_round="<<reuseRound<<" fast_addressing_both_groups="<<reuseRound
            <<" actual_reuse_variant="<<lab.PipelineStatistics().probeSdfReuseTraceSamples<<" full_packed_ray_capacity="<<sdfAfter.packed.size()/4<<" exact_bits=1 depth_bit_exact=1 shadow_bundle_both_groups=1 timed=0"<<std::endl;
    }
    Check(pairs==12&&resizes>=2&&toggleHistoryChecks==2&&packedTexels>0&&depthPixels>0,
        "SDF execution optimization contracts did not exercise resizing, full packed parity and history-preserving toggles");
    Mode(frame,false,true,15,false,false,addressingRound,reuseRound);Reset(frame);Draw(lab,frame);
    std::cout<<tag<<" PASS pairs="<<pairs<<" packed_texels="<<packedTexels<<" exact_fp32_channels="<<packedChannels
        <<" depth_pixels="<<depthPixels<<" render_extent_changes="<<resizes<<" history_toggle_checks="<<toggleHistoryChecks
        <<" screen_hint_hit_records="<<screenHints<<" nonzero_screen_status_records="<<nonzeroScreen
        <<" final_mesh_status_records="<<finalMeshRecords<<" final_global_status_records="<<finalGlobalRecords
        <<" production_scene_atlas_width=1024 non1024_fallback_not_covered_here=1 status_not_physical_march_or_fetch_count=1 timing_claim=0"<<std::endl;
}
