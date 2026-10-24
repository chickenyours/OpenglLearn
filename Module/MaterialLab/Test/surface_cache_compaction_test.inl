// Small transport/lifetime contracts, separate from the ordinary 224-frame
// contact/temporal acceptance sequence and all interactive timing samples.
void SurfaceCacheCompactionContracts(MaterialLabModule& lab,const Options& options) {
    using namespace SurfaceCacheNativeFixture;
    Check(options.vulkan&&!options.softwareRays,"SDF compaction contracts require capability-enabled Vulkan");
    auto frame=Frame(options,true);frame.present=false;frame.width=192;frame.height=128;
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;auto& trace=placement.trace;
    gi.contact.collectSamplingDiagnostics=false;gi.temporal.enabled=false;gi.spatial.enabled=false;
    placement.baseSpacing=32;placement.candidateCount=8;trace.rayResolution=4;trace.samplingSeed=29;
    frame.camera=SealedGiRoomCameraPreset(0,frame.OutputAspect());Mode(frame,false,true,3,true);Reset(frame);Draw(lab,frame);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
    while(lab.ProbeSdfQualityStatistics().pending){Draw(lab,frame);Check(std::chrono::steady_clock::now()<deadline,"Compaction contract scene preparation timed out");}
    std::uint64_t packedTexels=0,exactMetadata=0,queueIdentities=0;double maximumContinuousError=0;
    unsigned zeroLists=0,shrunkLists=0,resizes=0;std::uint32_t previousPending=0,previousCapacity=0;
    const auto compare=[&](const auto& a,const auto& b){Check(!a.empty()&&a.size()==b.size()&&a.size()%4==0,"Compaction contract changed complete packed atlas capacity");
        packedTexels+=a.size();for(std::size_t texel=0;texel<a.size();++texel)for(unsigned c=0;c<4;++c){
            const double x=a[texel][c],y=b[texel][c];Check(std::isfinite(x)&&std::isfinite(y),"Compaction contract produced nonfinite packed output");
            const bool exact=(texel%4==0&&c==3)||texel%4==2||(texel%4==3&&c==3);
            const double error=std::abs(x-y),tolerance=exact?0.:16.*std::numeric_limits<float>::epsilon()*std::max({1.,std::abs(x),std::abs(y)});
            if(error>tolerance)std::cout<<"COMPACTION CONTRACT packed_mismatch texel="<<texel<<" channel="<<c<<" before="<<x<<" after="<<y<<" tolerance="<<tolerance<<std::endl;
            Check(error<=tolerance,"Compaction contract changed full-atlas metadata or continuous ray transport");
            exactMetadata+=exact;maximumContinuousError=std::max(maximumContinuousError,error);}};
    for(unsigned test=0;test<9;++test){
        const bool noFields=test==1||test==4;
        frame.width=test<3?192:test<8?191:160;frame.height=test<3?128:test<8?123:101;
        trace.rayResolution=test<3||test==8?4:test<6?8:16;
        trace.samplingSeed=test<6?29:0x13579u;gi.safeProbeOrigins=test!=5;
        trace.sdf.useMesh=!noFields&&test!=7;trace.sdf.useGlobal=!noFields&&test!=6;
        trace.sdf.maxSteps=test==6?8:192;trace.maxSteps=test==7?8:128;
        frame.screenProbeStudyState.occluderOffset=test>=6?.17f:0.f;
        frame.camera=SealedGiRoomCameraPreset(test==8?1:0,frame.OutputAspect());
        Mode(frame,false,true,3,true);Reset(frame);Draw(lab,frame);Effective(lab,false,true,3,true);
        const auto screenBefore=lab.ReadbackScreenProbeTrace(false,true,true),sdfBefore=lab.ReadbackScreenProbeTrace(false,false,true);
        // The candidate retains last test's queue buffers, including a full ->
        // empty -> nonempty transition; a reset must clear the allocator and
        // initialize every physical output record before sparse writes.
        Mode(frame,true,true,3,true);Reset(frame);Draw(lab,frame);Effective(lab,true,true,3,true);
        const auto screenAfter=lab.ReadbackScreenProbeTrace(false,true,true),sdfAfter=lab.ReadbackScreenProbeTrace(false,false,true);
        compare(screenBefore.packed,screenAfter.packed);compare(sdfBefore.packed,sdfAfter.packed);
        const auto control=lab.ReadbackProbeSdfCompaction(false);CompactControl(control.control,control.capacity);
        const auto placementData=lab.ReadbackScreenProbePlacement();const auto origins=lab.ReadbackSurfaceCacheProbeOrigins();
        queueIdentities+=CompactQueue(lab,screenAfter,placementData,origins,gi.safeProbeOrigins,gi.enabled);
        if(noFields){Check(control.control[0]==0&&control.control[1]==0&&control.control[12]==0&&control.control[13]==0&&control.control[14]==0,
                "Disabled mesh/global fields did not produce an empty no-dispatch pending list");++zeroLists;}
        if(previousCapacity==control.capacity&&control.control[0]<previousPending)++shrunkLists;
        if(previousCapacity&&previousCapacity!=control.capacity)++resizes;
        std::cout<<"COMPACTION CONTRACT pair="<<test<<" size="<<frame.width<<'x'<<frame.height<<" ray_resolution="<<trace.rayResolution
            <<" mesh="<<trace.sdf.useMesh<<" global="<<trace.sdf.useGlobal<<" safe_origins="<<gi.safeProbeOrigins<<" physical_capacity="<<control.capacity
            <<" pending="<<control.control[0]<<" consumed="<<control.control[12]<<" mesh_march_calls="<<control.control[13]
            <<" global_march_calls="<<control.control[14]<<" packed_includes_padding=1\n";
        previousPending=control.control[0];previousCapacity=control.capacity;
    }
    Check(zeroLists==2&&shrunkLists>=2&&resizes>=2&&queueIdentities>0,"Compaction contracts did not exercise empty/shrinking/resized live lists");
    std::cout<<"COMPACTION CONTRACT PASS pairs=9 zero_lists="<<zeroLists<<" shrinking_lists="<<shrunkLists<<" capacity_resizes="<<resizes
        <<" packed_texels="<<packedTexels<<" exact_metadata_channels="<<exactMetadata<<" maximum_continuous_error="<<maximumContinuousError
        <<" original_queue_identities="<<queueIdentities<<" gpu_timing_claim=0 independent_triangle_certification_oracle=0"<<std::endl;
}
