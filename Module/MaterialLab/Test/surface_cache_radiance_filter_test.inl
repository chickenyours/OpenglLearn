// Execution-only spatial atlas split. The candidate writes one radiance/status
// texel per ray and reads the producer's immutable metadata in place. Explicit
// diagnostic readbacks restore the public four-texel record on the CPU.
void SurfaceCacheRadianceFilterContracts(MaterialLabModule& lab,const Options& options) {
    using namespace SurfaceCacheNativeFixture;
    Check(options.vulkan&&!options.softwareRays,"Radiance-only spatial contracts require capability-enabled Vulkan");
    auto frame=Frame(options,true);frame.present=false;frame.width=192;frame.height=128;
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;
    gi.contact.collectSamplingDiagnostics=false;gi.spatial.enabled=true;
    placement.baseSpacing=32;placement.candidateCount=8;placement.trace.rayResolution=4;
    placement.trace.samplingSeed=29;gi.areaShadows.tileResolution=64;
    frame.effects.areaLights.samplesPerAxis=4;frame.camera=SealedGiRoomCameraPreset(0,frame.OutputAspect());
    Mode(frame,false,false,15,false,false,false,true,2);Reset(frame);Draw(lab,frame);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
    while(lab.ProbeSdfQualityStatistics().pending){Draw(lab,frame);
        Check(std::chrono::steady_clock::now()<deadline,"Radiance-only spatial scene preparation timed out");}
    std::uint64_t comparedChannels=0,metadataChannels=0,filteredRayRecords=0;
    unsigned pairs=0,resizes=0,historyToggles=0;glm::uvec2 previousSize(0);
    const auto compare=[&](const auto& before,const auto& after,unsigned test,const char* stage){
        Check(!before.empty()&&before.size()==after.size(),std::string("Radiance-only spatial output missing or changed extent: ")+stage);
        for(std::size_t texel=0;texel<before.size();++texel)for(unsigned channel=0;channel<4;++channel){
            const float a=before[texel][channel],b=after[texel][channel];
            Check(std::isfinite(a)&&std::isfinite(b),"Radiance-only spatial output is nonfinite");
            if(std::bit_cast<std::uint32_t>(a)!=std::bit_cast<std::uint32_t>(b)){
                std::cout<<"RADIANCE FILTER CONTRACT mismatch test="<<test<<" stage="<<stage<<" texel="<<texel
                    <<" channel="<<channel<<std::setprecision(12)<<" before="<<a<<" after="<<b<<std::endl;
                Check(false,"Radiance-only spatial filtering changed exact FP32 output bits");}
            ++comparedChannels;
        }
    };
    const auto metadata=[&](const auto& rays,const auto& original){
        Check(!rays.empty()&&rays.size()==original.size()&&rays.size()%4==0,"Radiance-only spatial metadata extent mismatch");
        for(std::size_t ray=0;ray<rays.size()/4;++ray)for(unsigned part=1;part<4;++part)for(unsigned channel=0;channel<4;++channel){
            Check(std::bit_cast<std::uint32_t>(rays[ray*4+part][channel])==std::bit_cast<std::uint32_t>(original[ray*4+part][channel]),
                "Radiance-only spatial filtering changed immutable producer metadata");
            ++metadataChannels;
        }
        filteredRayRecords+=rays.size()/4;
    };
    for(unsigned test=0;test<8;++test){
        frame.width=test<3?192:test<6?191:160;frame.height=test<3?128:test<6?123:101;
        placement.trace.rayResolution=test<4?4:test<7?8:16;
        placement.curvatureAwareWeights=(test&1)!=0;
        gi.safeProbeOrigins=test!=5;gi.directionalReconstruction=(test&1)!=0;
        gi.spatial.passes=1+test%4;gi.spatial.halfKernelSize=test==6?2:1;
        gi.spatial.strength=test==7?.65f:1.f;
        frame.screenProbeStudyState.occluderOffset=test>=4?.17f:0.f;
        frame.camera=SealedGiRoomCameraPreset(test==6?1:0,frame.OutputAspect());
        Mode(frame,false,false,15,false,false,false,true,2);
        Reset(frame);Draw(lab,frame);Effective(lab,false,false,15,false,false,false,true,2);
        Check(!lab.PipelineStatistics().surfaceCacheRadianceOnlyFiltering,"Packed spatial control unexpectedly used compact filtering");
        const auto packedWork=lab.PipelineStatistics();
        const auto packedBefore=lab.ReadbackSurfaceCacheSpatialRays();
        const auto neighborsBefore=lab.ReadbackSurfaceCacheSpatialNeighbors();
        const auto irradianceBefore=lab.ReadbackSurfaceCacheProbeIrradiance();
        const auto rawBefore=lab.ReadbackSurfaceCacheRawGi(),spatialBefore=lab.ReadbackSurfaceCacheSpatialGi();
        const auto contactBefore=lab.ReadbackSurfaceCacheContactGi();
        const auto directionalBefore=(gi.directionalReconstruction||placement.curvatureAwareWeights)?lab.ReadbackSurfaceCacheProbeDirectional():std::vector<glm::vec4>{};
        const auto originsBefore=gi.safeProbeOrigins?lab.ReadbackSurfaceCacheProbeOrigins():std::vector<glm::vec4>{};
        Mode(frame,true,false,15,false,false,false,true,2);Reset(frame);Draw(lab,frame);
        Effective(lab,true,false,15,false,false,false,true,2);
        Check(lab.PipelineStatistics().surfaceCacheRadianceOnlyFiltering,"Requested radiance-only spatial shader was not selected");
        const auto compactWork=lab.PipelineStatistics();
        Check(compactWork.surfaceCacheSpatialFilterTexels>0&&compactWork.surfaceCacheSpatialFilterTexels*4==packedWork.surfaceCacheSpatialFilterTexels&&
            compactWork.surfaceCacheSpatialRayTargetBytes*4==packedWork.surfaceCacheSpatialRayTargetBytes,
            "Radiance-only spatial filtering did not reduce only the ping-pong atlas extent by four");
        const auto packedAfter=lab.ReadbackSurfaceCacheSpatialRays();
        compare(packedBefore,packedAfter,test,"restored_packed_rays");
        metadata(packedAfter,lab.ReadbackScreenProbeTrace(false,false,true).packed);
        compare(neighborsBefore,lab.ReadbackSurfaceCacheSpatialNeighbors(),test,"neighbors");
        compare(irradianceBefore,lab.ReadbackSurfaceCacheProbeIrradiance(),test,"raw_probe_irradiance");
        compare(rawBefore,lab.ReadbackSurfaceCacheRawGi(),test,"raw_gi");
        compare(spatialBefore,lab.ReadbackSurfaceCacheSpatialGi(),test,"filtered_gi");
        compare(contactBefore,lab.ReadbackSurfaceCacheContactGi(),test,"contact_gi");
        if(!directionalBefore.empty())compare(directionalBefore,lab.ReadbackSurfaceCacheProbeDirectional(),test,"filtered_directional");
        if(!originsBefore.empty())compare(originsBefore,lab.ReadbackSurfaceCacheProbeOrigins(),test,"safe_origins");
        if(previousSize.x&&previousSize!=glm::uvec2(frame.width,frame.height))++resizes;
        previousSize={frame.width,frame.height};++pairs;
        if(test==0)for(unsigned toggle=0;toggle<2;++toggle){
            Mode(frame,toggle!=0,false,15,false,false,false,true,2);Draw(lab,frame);
            Check(lab.PipelineStatistics().surfaceCacheRadianceOnlyFiltering==(toggle!=0),"Radiance-only spatial history toggle selected wrong shader");
            Check(lab.PipelineStatistics().surfaceCacheHistoryUsed,"Execution-only spatial atlas toggle discarded valid history");
            ++historyToggles;
        }
        std::cout<<"RADIANCE FILTER CONTRACT pair="<<test<<" size="<<frame.width<<'x'<<frame.height
            <<" passes="<<gi.spatial.passes<<" ray_resolution="<<placement.trace.rayResolution
            <<" directional="<<gi.directionalReconstruction<<" curved_support="<<placement.curvatureAwareWeights
            <<" safe_origins="<<gi.safeProbeOrigins<<" exact_fp32_bits=1 immutable_metadata=1 timed=0"<<std::endl;
    }
    Check(pairs==8&&resizes>=2&&historyToggles==2&&comparedChannels>0&&metadataChannels>0,
        "Radiance-only spatial contracts missed resizing, exact parity, immutable metadata or history toggles");
    Mode(frame,false,false,15,false,false,false,true,2);Reset(frame);Draw(lab,frame);
    std::cout<<"RADIANCE FILTER CONTRACT PASS pairs="<<pairs<<" compared_fp32_channels="<<comparedChannels
        <<" immutable_metadata_channels="<<metadataChannels<<" filtered_ray_records="<<filteredRayRecords
        <<" render_extent_changes="<<resizes<<" no_reset_history_toggles="<<historyToggles
        <<" filter_fragment_capacity_ratio=.25 metadata_copy_filter_invocations=0 timed=0"<<std::endl;
}
