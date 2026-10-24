// Untimed comparisons replay both consumers from one immutable producer frame.
// This file is included after surface_cache_budget_test.inl; the CLI dispatch
// stays separate from performance samples and the independent Native oracle.
namespace NativeReconstructionComparisonFixture {
using namespace SurfaceCacheBudgetFixture;
using ReceiverCaptureComparisonFixture::Compare;
struct Counts {std::uint64_t visible=0,readable=0,unknown=0,risky=0;};
void PrintImages(const NativeReconstructionComparisonReadback& pair,const SurfaceReceiverCaptureReadback& current,
                 std::span<const glm::vec4> production,const char* label,unsigned index) {
    Check(production.size()==pair.images[0].size()&&current.normals.size()==production.size(),
        "Reconstruction diagnostic producer extent differs");
    for(unsigned texture=0;texture<4;++texture){
        glm::vec4 lower(std::numeric_limits<float>::infinity()),upper(-std::numeric_limits<float>::infinity());
        std::uint64_t zero=0,one=0;std::size_t first=pair.images[texture].size();
        for(std::size_t pixel=0;pixel<pair.images[texture].size();++pixel){const auto value=pair.images[texture][pixel];
            lower=glm::min(lower,value);upper=glm::max(upper,value);zero+=value==glm::vec4(0);one+=value==glm::vec4(1);
            if(first==pair.images[texture].size()&&value!=glm::vec4(0))first=pixel;
        }
        std::cout<<"GI90 RECONSTRUCTION_IMAGE case="<<label<<" frame="<<index<<" texture="<<texture
            <<" zero_pixels="<<zero<<" one_pixels="<<one<<" first_nonzero="<<first<<" min=";
        for(unsigned c=0;c<4;++c)std::cout<<(c?",":"")<<std::setprecision(10)<<lower[c];std::cout<<" max=";
        for(unsigned c=0;c<4;++c)std::cout<<(c?",":"")<<upper[c];std::cout<<std::endl;
    }
    std::array<std::size_t,5> samples{{0,std::size_t(pair.width)/2,
        std::size_t(pair.height/2)*pair.width+pair.width/2,pair.images[0].size()-1,0}};
    for(std::size_t pixel=0;pixel<pair.images[0].size();++pixel)if(pair.images[0][pixel]!=pair.images[2][pixel]){samples.back()=pixel;break;}
    for(auto pixel:samples){std::cout<<"GI90 RECONSTRUCTION_PIXEL case="<<label<<" frame="<<index<<" index="<<pixel
        <<" x="<<pixel%pair.width<<" y="<<pixel/pair.width;
        const std::array<glm::vec4,7> values{{pair.images[0][pixel],pair.images[1][pixel],pair.images[2][pixel],pair.images[3][pixel],
            production[pixel],current.positions[pixel],current.normals[pixel]}};
        constexpr std::array<const char*,7> names{{"old_base","old_raw","mrt_base","mrt_raw","production_base","view_owner","normal"}};
        for(unsigned value=0;value<values.size();++value){std::cout<<" "<<names[value]<<"=";
            for(unsigned c=0;c<4;++c)std::cout<<(c?",":"")<<std::setprecision(10)<<values[value][c];
            std::cout<<" "<<names[value]<<"_bits=";
            for(unsigned c=0;c<4;++c)std::cout<<(c?",":"")<<std::bit_cast<std::uint32_t>(values[value][c]);}
        std::cout<<std::endl;
    }
}
Counts Sample(MaterialLabModule& lab,FrameSettings& frame,const char* label,unsigned index,bool shared,
              bool requireBlack=false,bool requireUnknown=false) {
    Draw(lab,frame);const auto stats=lab.PipelineStatistics();
    const auto pair=lab.ReadbackSurfaceCacheNativeReconstructionComparison();
    Check(!pair.images[0].empty(),lab.LastError());
    Check(stats.surfaceCacheNativeReconstructionCompared&&stats.surfaceCacheNativeReconstructionReplayDraws==3&&
        pair.width==frame.width&&pair.height==frame.height&&pair.sharedStencil==shared,
        "Native reconstruction replay lost its expected producer, dimensions or hook selection");
    const bool expectedFused=frame.effects.surfaceCacheGi.fuseNativeReconstruction&&
        frame.effects.surfaceCacheGi.contact.screenContactDetection&&!frame.effects.surfaceCacheGi.contact.referenceReceiver;
    Check(pair.productionFused==expectedFused&&stats.surfaceCacheNativeReconstructionDraws==(expectedFused?1u:2u),
        "Native reconstruction comparison did not use the requested production path");
    const auto current=lab.ReadbackSurfaceReceiverCapture();
    Check(!current.positions.empty()&&current.positions.size()==pair.images[0].size(),lab.LastError());
    const auto production=lab.ReadbackSurfaceCacheNativeBaseGi();Check(!production.empty(),lab.LastError());
    PrintImages(pair,current,production,label,index);
    Counts count;double energy=0;
    for(unsigned texture=0;texture<2;++texture){
        const auto& old=pair.images[texture];const auto& next=pair.images[texture+2];
        const auto delta=Compare(old,next);std::uint64_t excessive=0,classification=0;
        for(std::size_t pixel=0;pixel<old.size();++pixel){
            const auto a=old[pixel],b=next[pixel];
            for(unsigned channel=0;channel<4;++channel){
                // Predeclared float32 equivalence limits. In particular mass
                // is compared directly; a changed denominator cannot hide
                // behind the normalized risk or a thresholded final image.
                const double limit=(texture==1&&channel==2?2.e-5:2.e-6)+2.e-6*std::max(std::abs(double(a[channel])),std::abs(double(b[channel])));
                excessive+=std::abs(double(a[channel])-b[channel])>limit;
            }
            if(texture==0)classification+=(a.w>0)!=(b.w>0);
            else {
                classification+=std::bit_cast<std::uint32_t>(a.w)!=std::bit_cast<std::uint32_t>(b.w);
                // Raw y is the legacy float32 weighted sum divided by mass,
                // not a clamped mask. Divide/multiply and bucket accumulation
                // can overshoot the convex upper bound by a few float ULPs
                // (build39 observed 1+1ULP). This is solely a diagnostic
                // validity bound; old/new equivalence and quality gates below
                // keep their original limits. The final mask x remains [0,1].
                constexpr float RawRiskUpper=1.f+8.f*std::numeric_limits<float>::epsilon();
                Check(a.x>=0&&a.x<=1&&a.y>=0&&a.y<=RawRiskUpper&&a.z>=0&&(a.w==0||a.w==1),
                    "Original raw contact mask has invalid risk, mass or receiver validity");
                if(pair.images[0][pixel].w<=0&&a.w>0)
                    Check(a==glm::vec4(1,1,0,1)&&b==glm::vec4(1,1,0,1),
                        "Missing probe measurement did not retain the exact receiver-ray fallback");
            }
        }
        std::cout<<"GI90 RECONSTRUCTION_COMPARE case="<<label<<" frame="<<index<<" texture="<<(texture?"raw_mask":"base")
            <<" untimed=1 same_producer=1 replay_draws=3 producer_advance=0 shared_stencil="<<shared
            <<" production_fused="<<pair.productionFused<<" pixels="<<old.size()<<" bitwise_equal="<<(delta.different==0)
            <<" different_components="<<delta.different<<" max_abs="<<std::setprecision(10)<<delta.maximum<<" max_ulp="<<delta.ulp
            <<" abs_limit="<<(texture?"xyw:2e-6,z:2e-5":"2e-6")<<" relative_limit=2e-6"
            <<" classification_differences="<<classification<<" excessive_components="<<excessive<<" accepted="<<(!excessive&&!classification)<<std::endl;
        Check(!excessive&&!classification,"Native shared reconstruction exceeds the predeclared float32 or exact validity contract");
    }
    for(std::size_t pixel=0;pixel<pair.images[0].size();++pixel){
        const auto base=pair.images[0][pixel],raw=pair.images[1][pixel];
        const bool visible=current.positions[pixel].w>=.5f;
        count.visible+=visible;count.readable+=visible&&base.w>0;count.unknown+=visible&&base.w<=0;
        count.risky+=visible&&raw.x>.001f;energy+=glm::dot(glm::dvec3(base),glm::dvec3(base));
    }
    Check(count.visible>100,"Native reconstruction comparison contains too few actual receivers");
    if(requireBlack)Check(count.readable>100&&energy<1.e-8,"Valid-black reconstruction lost readable coverage or fabricated indirect light");
    if(requireUnknown)Check(count.unknown>100&&energy<1.e-8,"Unknown-cache reconstruction fabricated indirect light or became readable");
    if(frame.effects.surfaceCacheGi.nativeReconstructionComparison==1){
        // Verify the diagnostic shader is the actual production shader, rather
        // than merely comparing two isolated offscreen implementations.
        const auto delta=Compare(pair.images[pair.productionFused?2:0],production);
        Check(delta.different==0,"Same-producer replay differs from the actual production Native Base image");
    }
    std::cout<<"GI90 RECONSTRUCTION_COMPARE COVERAGE case="<<label<<" frame="<<index<<" visible="<<count.visible
        <<" readable="<<count.readable<<" unknown="<<count.unknown<<" risky="<<count.risky<<" base_energy="<<energy
        <<" valid_black="<<requireBlack<<" unknown_black="<<requireUnknown<<std::endl;
    return count;
}
void Run(MaterialLabModule& lab,const Options& options) {
    auto base=Make(options,13,true);base.present=options.benchmarkPresent;
    auto& gi=base.effects.surfaceCacheGi;gi.fuseNativeReconstruction=true;gi.nativeReconstructionComparison=1;
    gi.cacheSettledGather=false;gi.contact.collectSamplingDiagnostics=false;gi.contact.collectTraceTimings=false;
    base.effects.screenProbePlacement.trace.sdf.collectRayDiagnostics=false;
    Check(lab.ResetRenderHistory(),lab.LastError());Reset(base);
    // The actual sealed-room fixture contains curved spheres/ovals and the
    // ceiling/black-trim parallel layers, including the known 3cm separation.
    Sample(lab,base,"sealed_curved_parallel",0,true);
    const auto lit=Sample(lab,base,"sealed_curved_parallel",1,true);
    Check(lit.readable>100&&lit.risky>100,"Shared reconstruction fixture lacks readable and nontrivial risk measurements");
    const auto camera=*base.camera;
    for(unsigned f=0;f<3;++f){base.screenProbeStudyState.occluderOffset=.15f*float(f+1);
        if(f==1){auto moved=camera;moved.position.x+=.12f;moved.view=camera.view*glm::translate(glm::mat4(1),glm::vec3(-.12f,0,0));base.camera=moved;}
        Sample(lab,base,"moving_cube_camera",f,true);}
    auto frame=base;frame.camera=camera;frame.effects.surfaceCacheGi.intensity=0;
    Sample(lab,frame,"zero_intensity_valid_black",0,true,true);
    frame=base;frame.camera=camera;frame.effects.surfaceCacheGi.directionalReconstruction=false;
    frame.effects.screenProbePlacement.curvatureAwareWeights=false;
    Sample(lab,frame,"strict_scalar_geometry",0,true);
    frame=base;frame.effects.surfaceCacheGi.view=2;frame.effects.surfaceCacheGi.reconstructionDebug=1;
    Sample(lab,frame,"debug_hook_fallback",0,false);
    frame=base;frame.effects.screenProbePlacement.cornerRefinement=false;
    Sample(lab,frame,"coarse_top4_fallback",0,false);
    frame=base;frame.effects.surfaceCacheGi.nativeReconstructionComparison=2;
    Sample(lab,frame,"curved_metadata_hook_mismatch",0,false);
    frame=base;frame.effects.surfaceCacheGi.nativeReconstructionComparison=3;
    Sample(lab,frame,"failed_certified_origins",0,true,false,true);
    frame=base;frame.effects.surfaceCacheGi.fuseNativeReconstruction=false;
    Sample(lab,frame,"original_production_replayed",0,true);
    frame=base;frame.effects.surfaceCacheGi.contact.referenceReceiver=true;
    Sample(lab,frame,"reference_receiver_original_production",0,true);
    // This is the independent unknown-cache fixture used by the retained
    // native quality contracts: actual query geometry stays in the room while
    // every cache card moves 50m away. No synthetic brightness predicate is
    // inserted in either consumer under test.
    frame=base;auto instances=lab.PreparedProbeSdfScene()->Instances();
    for(auto& instance:instances)instance.localToWorld[3].x+=50;
    Render::ProbeGlobalSdfSettings tiny;tiny.counts=glm::uvec3(2);
    const auto remote=Render::BuildProbeSdfScene(instances,tiny);frame.effects.surfaceCacheGi.cache=Render::BuildSurfaceCache(*remote);
    Reset(frame);Draw(lab,frame);Sample(lab,frame,"actual_unknown_cache",0,true,false,true);
    frame.effects.surfaceCacheGi.nativeReconstructionComparison=0;Draw(lab,frame);
    Check(!lab.PipelineStatistics().surfaceCacheNativeReconstructionCompared&&
        lab.PipelineStatistics().surfaceCacheNativeReconstructionReplayDraws==0,
        "Disabled native reconstruction comparison retained replay work");
    std::cout<<"GI90 RECONSTRUCTION_COMPARE COMPLETE accepted=1 untimed=1 same_producer=1 cases=11"
        <<" producer_advance=0 risk_mass_compared=1 valid_black=1 unknown_black=1 hook_fallback=1 production_base_exact=1"<<std::endl;
}
}
void SurfaceCacheNativeReconstructionComparison(MaterialLabModule& lab,const Options& options) {
    NativeReconstructionComparisonFixture::Run(lab,options);
}
