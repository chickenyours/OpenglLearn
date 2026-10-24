// Budget-changing architecture experiment. Timing and ray-inspection passes
// are separate; no readback or instrumented shader enters the FPS samples.
namespace SurfaceCacheBudgetFixture {
using namespace SurfaceCacheNativeFixture;
FrameSettings Make(const Options& options,unsigned profile,bool quality) {
    auto plain=options;plain.gi90Profile=0;auto frame=Frame(plain,quality);
    Mode(frame,true,false,3,false,false,false,false,0,0,12);
    ApplySurfaceCacheBudget(frame,profile);
    frame.effects.surfaceCacheGi.contact.collectSamplingDiagnostics=false;
    if(quality)frame.present=options.benchmarkPresent;
    return frame;
}
struct LightingQueryCounts {
    std::array<std::uint64_t,2> counts{};
    bool available=false,actual=false,updated=false;
    std::string operator[](std::size_t index) const{return available?std::to_string(counts[index]):"unavailable";}
    const char* Source() const{return !updated?"not_executed":!available?"unavailable":actual?"actual":"replay";}
};
LightingQueryCounts LightingQueryCensus(MaterialLabModule& lab,const Render::SurfaceCacheGiSettings& gi) {
    LightingQueryCounts result;const auto stats=lab.PipelineStatistics();result.updated=stats.surfaceCacheLightingUpdated;
    // A skipped lighting draw certifies zero work without reading an old atlas.
    if(!result.updated){result.available=result.actual=true;return result;}
    if(!stats.surfaceCacheLightingQueriesCollected)return result;
    const auto values=lab.ReadbackSurfaceCacheLightingQueries();const auto cache=gi.cache?gi.cache:lab.PreparedSurfaceCache();
    Check(cache&&!values.empty()&&values.size()==cache->PlaneTexels(),"Missing or truncated cached-light query counters");
    result.available=true;result.actual=gi.ueLumenHigh;
    std::uint64_t active=0,written=0;
    for(auto v:values){
        for(unsigned k=0;k<2;++k){Check(std::isfinite(v[k])&&v[k]>=0&&v[k]<=16777216.f&&v[k]==std::floor(v[k]),"Invalid cached-light query counter");result.counts[k]+=std::uint64_t(v[k]);}
        if(result.actual){
            Check(v.w==0||v.w==1,"Invalid actual cached-light MRT write marker");
            if(v.w==0)Check(v==glm::vec4(0),"Unwritten cached-light MRT texel retained query work");
            else {Check(v.z==0||v.z==1,"Invalid actual cached-light MRT active marker");active+=std::uint64_t(v.z);++written;}
        }
    }
    if(result.actual){
        Check(written==stats.surfaceCacheLightingWrittenTexels,"Actual cached-light MRT coverage differs from scheduled rectangles");
        Check(active<=stats.surfaceCacheTexels,"Actual cached-light active texels exceed card capacity");
    }
    return result;
}
void AuditReceiverCompaction(MaterialLabModule& lab,bool expected,unsigned profile,unsigned path){
    const auto control=lab.ReadbackSurfaceCacheReceiverCompaction();
    Check(control.has_value()==expected,"Compact receiver control availability differs from the completed path");
    std::cout<<"GI90 RECEIVER_COMPACT profile="<<profile<<" path="<<PathNames[path]<<" untimed=1 available="<<bool(control);
    if(control){const auto& c=*control;
        Check(c[0]==c[5]&&c[5]<=c[4],"Compact receiver queued/consumed/capacity mismatch");
        Check(c[15]==0,"Compact receiver queue overflow or invalid pixel ID");
        Check(c[1]==(c[0]+63u)/64u&&c[2]==1&&c[3]==1,"Compact receiver indirect dispatch differs from the exact queue extent");
        std::cout<<" queued="<<c[0]<<" dispatch_x="<<c[1]<<" dispatch_y="<<c[2]<<" dispatch_z="<<c[3]
            <<" capacity="<<c[4]<<" consumed="<<c[5]<<" overflow="<<c[15]<<" queue_exact=1";
    }
    std::cout<<std::endl;
}
struct NativeProbeCounts {
    std::uint64_t capacity=0,active=0,launchable=0,generated=0,directions=0,native=0,retries=0,software=0;
    std::array<std::uint64_t,18> status{};
};
struct NativeReceiverCounts {
    std::uint64_t directions=0,native=0,retries=0,software=0,originNative=0,originSoftware=0,launchable=0,production=0,reconstructed=0;
    std::uint64_t unknown=0,rejected=0,padding=0,excess=0,fallback=0;
    std::array<std::uint64_t,4> budgets{};
};
unsigned NativeReceiverMovingTag(glm::vec4 fresh) {
    if(!std::isfinite(fresh.z)||fresh.z<0||fresh.z>31||fresh.z!=std::floor(fresh.z+.5f))return 0;
    const unsigned tag=unsigned(fresh.z),multiplier=(tag&15u)>>1;
    return multiplier==2||multiplier==4?tag:0;
}
unsigned NativeReceiverExposureBudget(const Render::SurfaceCacheGiSettings& gi,bool freshCollected,float soft,glm::vec4 fresh,bool forcedCore=false) {
    // Independent accounting from the original exposure image, before looking
    // at either production mask.w or the diagnostic ray terminal states.
    const unsigned base=gi.contact.raysPerPixel;
    if(!gi.nativeAdaptiveFreshSampling||!freshCollected)return base;
    const unsigned tag=NativeReceiverMovingTag(fresh),moving=(tag&15u)>>1;
    unsigned count=base;
    if(std::isfinite(fresh.x)&&std::isfinite(fresh.y)&&fresh.x>.000001f)
        count=base*unsigned(std::clamp(std::floor(double(fresh.y)/base+.5),1.,4.));
    if(moving>1&&(soft>=.25f||(tag&16u)))count=std::max(count,base*std::min(moving,4u));
    return count;
}
std::vector<unsigned char> NativeReceiverRasterCoreMap(MaterialLabModule& lab,const FrameSettings& frame,std::size_t count) {
    const auto& gi=frame.effects.surfaceCacheGi;
    const bool haloActive=gi.nativeReceiverFilterSupportHalo&&gi.compactReceiverTracing&&gi.sparseReceiverSampling;
    if(!gi.nativeForceRasterCore&&!haloActive)return {};
    Check(frame.camera.has_value(),"Native forced-core audit lacks the producing camera");
    const auto capture=lab.ReadbackSurfaceReceiverCapture();const auto cache=lab.PreparedSurfaceCache();
    const auto classification=gi.sparseReceiverSampling?lab.ReadbackSurfaceCacheReceiverClassification():std::vector<glm::vec4>{};
    Check(cache&&capture.positions.size()==count&&capture.normals.size()==count&&
        (!gi.sparseReceiverSampling||classification.size()==count),"Native forced-core audit lacks current capture/classification");
    const auto& metadata=cache->Metadata();const auto objects=cache->CardCount()/6;
    Check(metadata.size()>=std::size_t(objects)*6*Render::SurfaceCacheCardTexels,"Native forced-core audit lacks object bounds");
    const auto inverse=glm::inverse(frame.camera->view);std::vector<unsigned char> result(count,0);
    std::uint64_t forced=0,checked=0,boundary=0,unknownOwner=0;
    // Bits in the returned map are independent provenance: strict geometry=1,
    // newly scheduled halo=2. A strict bit only forces risk when the GI flag does.
    std::vector<unsigned char> independentSeeds;
    if(haloActive){independentSeeds.resize(count);
        Check(gi.receiverLatticeMax<4,"Independent halo audit requires the strict-only known-owner selector");
        Check(std::size_t(capture.width)*capture.height==count,"Halo capture extent mismatch");}
    for(std::size_t pixel=0;pixel<count;++pixel){
        const auto v=capture.positions[pixel],normal=capture.normals[pixel];
        bool valid=v.w>=.5f&&normal.w>=.5f&&glm::dot(glm::vec3(normal),glm::vec3(normal))>=.5f;
        for(unsigned c=0;c<3;++c)valid&=std::isfinite(v[c])&&std::isfinite(normal[c]);
        const bool owner=valid&&v.w>=1.f&&v.w==std::floor(v.w)&&v.w<=float(objects);
        if(gi.sparseReceiverSampling){const auto c=classification[pixel];
            Check((c.x==0||c.x==1)&&(c.y==0||c.y==1)&&(haloActive?(c.z==0||c.z==1):c.z==0)&&c.w==0,"Native classification changed its selector/core ABI");
            if(haloActive)Check(c.z<=c.x&&c.y+c.z<=1,"Halo provenance overlaps strict core or an unselected receiver");
            Check(c.y<=c.x,"Forced native core was omitted from the physical queue selector");
            if(!owner)Check(c.y==0,"Uncertified receiver owner became a forced 3cm core");
            if(haloActive&&!owner)Check(c.z==0,"Uncertified receiver owner became a new support halo");
        }
        if(!owner){unknownOwner+=valid;continue;}
        const auto p=glm::vec3(inverse*glm::vec4(glm::vec3(v),1)),n=glm::normalize(glm::vec3(normal));
        const unsigned own=unsigned(v.w)-1;bool core=false,definite=false,possible=false;
        for(unsigned object=0;object<objects;++object){if(object==own)continue;
            const auto at=std::size_t(object)*6*Render::SurfaceCacheCardTexels;
            const auto lo=glm::vec3(metadata[at+9]),hi=glm::vec3(metadata[at+10]);
            const auto delta=glm::max(glm::max(lo-p,p-hi),glm::vec3(0));
            const float front=glm::dot((lo+hi)*.5f-p,n)+glm::dot((hi-lo)*.5f,glm::abs(n));
            core|=glm::dot(delta,delta)<.03f*.03f&&front>1e-5f;
            // Independent double geometric classification. A one-micrometre
            // band is reported explicitly for float transform/FMA ambiguity;
            // all risks/budgets below still use the actual classifier bit.
            const auto pd=glm::dvec3(p),nd=glm::dvec3(n),ld=glm::dvec3(lo),hd=glm::dvec3(hi);
            const auto dd=glm::max(glm::max(ld-pd,pd-hd),glm::dvec3(0));
            const double distance=glm::length(dd),facing=glm::dot((ld+hd)*.5-pd,nd)+glm::dot((hd-ld)*.5,glm::abs(nd));
            definite|=distance<.03-1e-6&&facing>1e-5+1e-6;
            possible|=distance<.03+1e-6&&facing>1e-5-1e-6;
        }
        const bool actual=gi.sparseReceiverSampling?classification[pixel].y>.5f:core;
        if(haloActive){
            // Seed truth comes from independent object-bound geometry, not
            // the classification being audited. Double boundary bands remain
            // explicit and never alter production gates or actual ray budgets.
            independentSeeds[pixel]=static_cast<unsigned char>((core?1:0)|(definite?2:0)|(possible?4:0));
            const bool originalSelector=classification[pixel].x>.5f&&classification[pixel].z<.5f;
            Check(originalSelector==actual,"Known-owner original selector differs from strict 3cm geometry metadata");
        }
        Check(actual?possible:!definite,"Native 3cm classifier differs from independent object-bound geometry");
        if(definite==possible){++checked;Check(actual==definite,"Native nonboundary core classification mismatch");}else ++boundary;
        result[pixel]=actual?1:0;forced+=actual;
    }
    if(haloActive){
        struct Vertex {glm::dvec3 p{0},n{0};float owner=0;bool valid=false;};
        const auto vertex=[&](int x,int y){
            Vertex v;if(x<0||y<0||x>=int(capture.width)||y>=int(capture.height))return v;
            const auto index=std::size_t(y)*capture.width+unsigned(x);
            const auto p=capture.positions[index],n=capture.normals[index];
            bool valid=p.w>=1&&p.w==std::floor(p.w)&&p.w<=float(objects)&&p.z<0&&n.w>.5f&&
                glm::dot(glm::vec3(n),glm::vec3(n))>.5f;
            for(unsigned k=0;k<3;++k)valid&=std::isfinite(p[k])&&std::isfinite(n[k]);
            if(!valid)return v;
            v.p=glm::dvec3(inverse*glm::vec4(glm::vec3(p),1));v.n=glm::dvec3(glm::normalize(glm::vec3(n)));v.owner=p.w;
            for(unsigned k=0;k<3;++k)valid&=std::isfinite(v.p[k])&&std::isfinite(v.n[k]);
            v.valid=valid;return v;
        };
        const auto compatible=[&](const Vertex& center,const Vertex& other,bool possible){
            if(!center.valid||!other.valid||center.owner!=other.owner)return false;
            const auto delta=other.p-center.p;const double alignment=std::clamp(glm::dot(center.n,other.n),-1.,1.);
            const double curve=.75*glm::length(delta)*std::sqrt(std::max(0.,2.*(1.-alignment)));
            const double plane=std::max(std::abs(glm::dot(delta,center.n)),std::abs(glm::dot(delta,other.n)));
            const double boundary=possible?1e-6:-1e-6;
            return alignment>=double(.9659258263f)-boundary&&plane<=std::max(double(.0003f),curve)+boundary;
        };
        const auto reachesIndependentSeed=[&](int x,int y,bool possible){
            const auto horizontal=[&](int qx,int qy){
                const auto center=vertex(qx,qy);if(!center.valid)return false;
                const auto seed=[&](int sx,int sy){
                    if(sx<0||sy<0||sx>=int(capture.width)||sy>=int(capture.height))return false;
                    const auto id=std::size_t(sy)*capture.width+unsigned(sx);
                    return (independentSeeds[id]&(possible?4u:2u))!=0;
                };
                if(seed(qx,qy))return true;
                for(int sign:{-1,1})for(int step=1;step<=7;++step){
                    const int nx=qx+sign*step;const auto other=vertex(nx,qy);
                    if(!compatible(center,other,possible))break;
                    if(seed(nx,qy))return true;
                }
                return false;
            };
            if(horizontal(x,y))return true;
            const auto center=vertex(x,y);if(!center.valid)return false;
            for(int sign:{-1,1})for(int step=1;step<=7;++step){
                const int ny=y+sign*step;const auto other=vertex(x,ny);
                if(!compatible(center,other,possible))break;
                if(horizontal(x,ny))return true;
            }
            return false;
        };
        std::uint64_t halo=0,definitePaths=0,boundaryPaths=0;
        // Only newly added pixels need the bounded H->V proof. This is not
        // a full-screen 15x15 scan, and GPU class bits never supply seed truth.
        for(std::size_t pixel=0;pixel<count;++pixel)if(classification[pixel].z>.5f){
            Check((result[pixel]&1u)==0,"Strict core was mislabeled as added halo");
            const int x=int(pixel%capture.width),y=int(pixel/capture.width);
            Check(reachesIndependentSeed(x,y,true),"Added receiver halo has no independent bounded geometric core path");
            const bool definite=reachesIndependentSeed(x,y,false);
            definitePaths+=definite;boundaryPaths+=!definite;result[pixel]|=2u;++halo;
        }
        std::cout<<"GI90 SUPPORT_HALO_CPU untimed=1 added="<<halo<<" independent_definite_paths="<<definitePaths
            <<" float_boundary_paths="<<boundaryPaths<<" radius_pixels=7 separable_order=horizontal_then_vertical"
            <<" seed_source=independent_double_object_bounds seed_classification_not_used=1"
            <<" endpoint_band_m=0.000001 normal_dot_band=0.000001 classification_z=added_halo"
            <<" strict_geometry_bit=1 halo_bit=2 unknown_owner_seed=0"<<std::endl;
    }
    std::cout<<"GI90 RASTER_CORE_CPU untimed=1 forced="<<forced<<" independent_nonboundary_checked="<<checked
        <<" float_boundary_pixels="<<boundary<<" boundary_band_m=0.000001 unknown_owner="<<unknownOwner
        <<" radius_m=0.03 same_receiver_owner=1 front_hemisphere=1 classifier_y_available="<<!classification.empty()
        <<" brightness_and_mask_not_used=1"<<std::endl;
    return result;
}
void AuditNativeReceiverQueue(MaterialLabModule& lab,const Render::SurfaceCacheGiSettings& gi,unsigned profile,unsigned path,
    const std::vector<glm::vec4>& mask,const std::vector<glm::vec4>& soft,const std::vector<glm::vec4>& base,
    const std::vector<glm::vec4>& fresh,bool freshCollected,const std::vector<unsigned char>* forcedCore=nullptr) {
    const auto control=lab.ReadbackSurfaceCacheReceiverCompaction();
    const auto queue=lab.ReadbackSurfaceCacheReceiverCompactionQueue();
    Check(control.has_value()==gi.compactReceiverTracing&&queue.has_value()==gi.compactReceiverTracing,
        "Native queue availability differs from the completed compact path");
    if(!queue){std::cout<<"GI90 RECEIVER_QUEUE_BUDGET profile="<<profile<<" path="<<PathNames[path]<<" untimed=1 available=0"<<std::endl;return;}
    Check(queue->size()==(*control)[0]&&(*control)[0]==(*control)[5]&&(*control)[15]==0,"Native queue count differs from original control audit");
    std::vector<unsigned char> queued(mask.size(),0);
    std::array<std::uint64_t,4> planned{};
    std::array<std::uint64_t,5> coreActual{},lowActual{};
    std::uint64_t coreRays=0,lowRays=0,totalRays=0,riskZero=0,failedLaunch=0,otherZero=0;
    const auto forced=[&](std::uint32_t id){return gi.nativeForceRasterCore&&forcedCore&&!forcedCore->empty()&&((*forcedCore)[id]&1u)!=0;};
    const auto expected=[&](std::uint32_t id){return NativeReceiverExposureBudget(gi,freshCollected,soft[id].x,fresh.empty()?glm::vec4(0):fresh[id],forced(id));};
    std::uint64_t strictQueued=0,haloQueued=0,strictDirections=0,haloDirections=0;
    for(const auto id:*queue){
        Check(id<mask.size()&&!queued[id]++,"Native compact queue contains an out-of-range or duplicate pixel ID");
        const unsigned count=expected(id),actual=unsigned(mask[id].w);
        Check(count>=8&&count<=32&&count%8==0&&(actual==0||actual==count),"Native queued actual budget differs from independent exposure demand");
        ++planned[count/8-1];++coreActual[actual/8];coreRays+=actual;
        if(forcedCore&&!forcedCore->empty()){
            const auto provenance=(*forcedCore)[id];
            strictQueued+=(provenance&1u)!=0;haloQueued+=(provenance&2u)!=0;
            if(provenance&1u)strictDirections+=actual;if(provenance&2u)haloDirections+=actual;
        }
        if(!actual){
            const auto b=base[id];bool baseValid=b.a>0&&std::isfinite(b.a);
            for(unsigned c=0;c<3;++c)baseValid&=std::isfinite(b[c]);
            float risk=baseValid?(std::isfinite(soft[id].x)?std::clamp(soft[id].x,0.f,1.f):1.f):1.f;
            if(gi.nativeAdaptiveFreshSampling&&freshCollected&&std::isfinite(fresh[id].x)){
                risk=std::max(risk,std::clamp(fresh[id].x,0.f,1.f));if(NativeReceiverMovingTag(fresh[id])&16u)risk=1.f;}
            if(forced(id))risk=1.f;
            if(mask[id].x<0)++failedLaunch;
            else if(baseValid&&risk<=.000001f)++riskZero;
            else ++otherZero;
        }
    }
    for(std::size_t pixel=0;pixel<mask.size();++pixel){const unsigned actual=unsigned(mask[pixel].w);totalRays+=actual;
        if(forcedCore&&!forcedCore->empty()&&((*forcedCore)[pixel]&2u))Check(queued[pixel],"Added halo was omitted from the physical producer queue");
        if(!queued[pixel]&&actual){++lowActual[actual/8];lowRays+=actual;}}
    Check(coreRays+lowRays==totalRays&&coreActual[0]==riskZero+failedLaunch+otherZero,"Native core/low budget partition lost physical work");
    std::cout<<"GI90 RECEIVER_QUEUE_BUDGET profile="<<profile<<" path="<<PathNames[path]<<" untimed=1 available=1 queued="<<queue->size()
        <<" core_planned_8="<<planned[0]<<" core_planned_16="<<planned[1]<<" core_planned_24="<<planned[2]<<" core_planned_32="<<planned[3]
        <<" core_actual_0="<<coreActual[0]<<" core_actual_8="<<coreActual[1]<<" core_actual_16="<<coreActual[2]<<" core_actual_24="<<coreActual[3]<<" core_actual_32="<<coreActual[4]
        <<" core_zero_risk="<<riskZero<<" core_zero_failed_launch="<<failedLaunch<<" core_zero_other="<<otherZero
        <<" low_actual_0=unavailable low_actual_8="<<lowActual[1]<<" low_actual_16="<<lowActual[2]<<" low_actual_24="<<lowActual[3]<<" low_actual_32="<<lowActual[4]
        <<" core_actual_rays="<<coreRays<<" low_actual_rays="<<lowRays<<" total_actual_rays="<<totalRays
        <<" independent_fresh_budget=1 low_zero_schedule_not_inferred=1"<<std::endl;
    if(gi.nativeReceiverFilterSupportHalo&&gi.compactReceiverTracing&&gi.sparseReceiverSampling)
        std::cout<<"GI90 RECEIVER_QUEUE_HALO profile="<<profile<<" path="<<PathNames[path]
            <<" untimed=1 strict_core_queued="<<strictQueued<<" added_halo_queued="<<haloQueued
            <<" strict_core_actual_directions="<<strictDirections<<" added_halo_actual_directions="<<haloDirections
            <<" physical_only_from_mask_w=1 independent_fresh_budget_unchanged=1"<<std::endl;
    for(unsigned lanes:{32u,64u}){
        std::uint64_t raySlots=0,bucketRaySlots=0,waves=0,mixedPlanned=0,mixedActual=0,zeroWaves=0,bucketWaves=0;
        for(std::size_t first=0;first<queue->size();first+=lanes){
            unsigned maximum=0,minimumNonzero=32,maximumPlanned=0,minimumPlanned=32;
            for(std::size_t i=first;i<std::min(queue->size(),first+lanes);++i){const auto id=(*queue)[i];const unsigned count=unsigned(mask[id].w),budget=expected(id);
                maximum=std::max(maximum,count);if(count)minimumNonzero=std::min(minimumNonzero,count);
                maximumPlanned=std::max(maximumPlanned,budget);minimumPlanned=std::min(minimumPlanned,budget);}
            ++waves;raySlots+=std::uint64_t(lanes)*maximum;zeroWaves+=maximum==0;
            mixedPlanned+=minimumPlanned!=maximumPlanned;mixedActual+=maximum>0&&minimumNonzero!=maximum;
        }
        // Stable partition by the existing planned budget. Zero-ray early exits
        // retain their real positions inside each hypothetical budget queue.
        std::array<unsigned,4> pending{},maximum{};
        for(const auto id:*queue){const auto bucket=expected(id)/8-1;maximum[bucket]=std::max(maximum[bucket],unsigned(mask[id].w));
            if(++pending[bucket]==lanes){bucketRaySlots+=std::uint64_t(lanes)*maximum[bucket];++bucketWaves;pending[bucket]=maximum[bucket]=0;}}
        for(unsigned bucket=0;bucket<4;++bucket)if(pending[bucket]){bucketRaySlots+=std::uint64_t(lanes)*maximum[bucket];++bucketWaves;}
        Check(coreRays<=raySlots&&coreRays<=bucketRaySlots,"Native queue wave model lost actual physical directions");
        std::cout<<"GI90 RECEIVER_QUEUE_MODEL profile="<<profile<<" path="<<PathNames[path]<<" untimed=1 model_only=1 gpu_occupancy_measured=0 timing_claim=0"
            <<" assumed_lanes="<<lanes<<" queue_order_actual=1 bucket_order_stable_model=1 waves="<<waves
            <<" mixed_planned_budget_waves="<<mixedPlanned<<" mixed_actual_nonzero_budget_waves="<<mixedActual<<" zero_ray_waves="<<zeroWaves
            <<" actual_core_directions="<<coreRays<<" wave_ray_slots="<<raySlots<<" hypothetical_bucket_waves="<<bucketWaves<<" hypothetical_bucket_ray_slots="<<bucketRaySlots
            <<" modeled_lane_efficiency="<<(raySlots?double(coreRays)/double(raySlots):0.)
            <<" modeled_bucket_lane_efficiency="<<(bucketRaySlots?double(coreRays)/double(bucketRaySlots):0.)
            <<" modeled_ray_slot_reduction="<<(raySlots?1.-double(bucketRaySlots)/double(raySlots):0.)
            <<" source_origin_cache_branch_and_memory_costs_excluded=1"<<std::endl;
    }
}
NativeReceiverCounts NativeReceiverCensus(MaterialLabModule& lab,const Render::SurfaceCacheGiSettings& gi,bool auditQueue=false,unsigned profile=0,unsigned path=0,const FrameSettings* frame=nullptr) {
    const auto& stats=lab.PipelineStatistics();const bool adaptive=gi.nativeAdaptiveFreshSampling;
    if(gi.ueLumenHigh){
        Check(!stats.surfaceCacheCompactReceivers&&!stats.surfaceCacheNativeAdaptiveFreshSampling&&
            stats.surfaceCacheNativeReceiverDiagnosticPages==0&&!stats.surfaceCacheFreshSamplingCollected,
            "UE High executed a retired receiver producer");
        Check(lab.ReadbackSurfaceCacheTransportMask().empty()&&lab.ReadbackSurfaceCacheTransportTrace(true).empty(),
            "UE High published stale receiver work from the legacy path");
        if(auditQueue)std::cout<<"GI90 UE RECEIVER actual_directions=0 native_queries=0 software_queries=0"
            <<" receiver_replay_executed=0 compact_available=0"<<std::endl;
        return {};
    }
    const bool haloActive=gi.nativeReceiverFilterSupportHalo&&gi.compactReceiverTracing&&gi.sparseReceiverSampling;
    Check(stats.surfaceCacheNativeReceiverFilterSupportHalo==haloActive,"Native support halo flag differs from the actual producer");
    Check(stats.surfaceCacheNativeAdaptiveFreshSampling==adaptive&&stats.surfaceCacheNativeReceiverDiagnosticPages==(adaptive?4u:0u),
        "Native receiver diagnostic pages differ from the actually selected producer");
    Check(stats.surfaceCacheNativeForceRasterCore==gi.nativeForceRasterCore,"Native forced-core flag differs from the actual receiver producer");
    Check(gi.contact.raysPerPixel==8,"Native receiver census requires the retained eight-stratum batch ABI");
    const auto mask=lab.ReadbackSurfaceCacheTransportMask(),soft=lab.ReadbackSurfaceCacheContactMask(),base=lab.ReadbackSurfaceCacheNativeBaseGi();
    const bool freshCollected=stats.surfaceCacheFreshSamplingCollected;
    const auto fresh=adaptive&&freshCollected?lab.ReadbackSurfaceCacheFreshSampling():std::vector<glm::vec4>{};
    Check(!mask.empty()&&mask.size()==soft.size()&&mask.size()==base.size()&&(!adaptive||!freshCollected||fresh.size()==mask.size()),
        "Native receiver base/soft-mask/exposure audit extent mismatch");
    Check(!(gi.nativeForceRasterCore||haloActive)||frame,"Native core/halo census requires the producing frame");
    const auto forcedCore=(gi.nativeForceRasterCore||haloActive)?NativeReceiverRasterCoreMap(lab,*frame,mask.size()):std::vector<unsigned char>{};
    const auto forced=[&](std::size_t pixel){return gi.nativeForceRasterCore&&!forcedCore.empty()&&(forcedCore[pixel]&1u)!=0;};
    const auto halo=[&](std::size_t pixel){return !forcedCore.empty()&&(forcedCore[pixel]&2u)!=0;};
    std::uint64_t haloPixels=0,haloPhysical=0,haloSynthetic=0,haloFailed=0,haloRiskZero=0,haloDirections=0,haloExpectedDirections=0,independentPhysicalDirections=0;
    std::uint64_t forcedPhysical=0,forcedSourceFailed=0,forcedNoActual=0,forcedOldRiskZero=0,forcedOldRiskZeroDirections=0,forcedExtraBudgetDirections=0;
    NativeReceiverCounts result;std::vector<unsigned char> seen(mask.size(),0),expected(mask.size(),8);
    for(std::size_t pixel=0;pixel<mask.size();++pixel){const auto m=mask[pixel];
        Check(std::isfinite(m.w)&&m.w>=0&&m.w<=float(adaptive?32:8)&&m.w==std::floor(m.w)&&unsigned(m.w)%8==0,
            "Invalid native production direction count or incomplete angular batch");
        expected[pixel]=static_cast<unsigned char>(NativeReceiverExposureBudget(gi,freshCollected,soft[pixel].x,fresh.empty()?glm::vec4(0):fresh[pixel],forced(pixel)));
        result.production+=std::uint64_t(m.w);
        if(m.w>0)independentPhysicalDirections+=expected[pixel];
        if(m.w>0){Check(unsigned(m.w)==expected[pixel],"Native physical budget differs from independent original exposure demand");
            ++result.launchable;++result.budgets[unsigned(m.w)/8-1];result.fallback+=!freshCollected;}
        if(m.x==.25f){Check(m.w==0&&std::isfinite(m.y)&&m.y>=0&&m.y<=1,"Native reconstruction lost soft risk/provenance");++result.reconstructed;}
        if(m.w>0||m.x==.25f){float risk=base[pixel].a>0?std::clamp(soft[pixel].x,0.f,1.f):1.f;
            if(adaptive&&freshCollected&&std::isfinite(fresh[pixel].x)){
                risk=std::max(risk,std::clamp(fresh[pixel].x,0.f,1.f));if(NativeReceiverMovingTag(fresh[pixel])&16u)risk=1;}
            if(forced(pixel))risk=1.f;
            Check(std::isfinite(soft[pixel].x)&&std::isfinite(m.y)&&std::abs(m.y-risk)<=.000001f,
                "Native receiver differs from original per-pixel effective contact risk");}
        if(halo(pixel)){
            ++haloPixels;haloPhysical+=m.w>0;haloSynthetic+=m.x==.25f&&m.w==0;haloFailed+=m.x<0;
            if(m.w>0){haloDirections+=std::uint64_t(m.w);haloExpectedDirections+=expected[pixel];}
            float risk=base[pixel].a>0?std::clamp(soft[pixel].x,0.f,1.f):1.f;
            if(adaptive&&freshCollected&&std::isfinite(fresh[pixel].x)){
                risk=std::max(risk,std::clamp(fresh[pixel].x,0.f,1.f));if(NativeReceiverMovingTag(fresh[pixel])&16u)risk=1.f;}
            if(risk<=.000001f){++haloRiskZero;Check(m.w==0,"Zero-risk halo performed physical receiver directions");}
        }
        if(forced(pixel)){
            forcedPhysical+=m.w>0;forcedSourceFailed+=m.x<0;forcedNoActual+=m.w==0&&m.x>=0;
            float oldRisk=base[pixel].a>0?std::clamp(soft[pixel].x,0.f,1.f):1.f;
            if(adaptive&&freshCollected&&std::isfinite(fresh[pixel].x)){
                oldRisk=std::max(oldRisk,std::clamp(fresh[pixel].x,0.f,1.f));if(NativeReceiverMovingTag(fresh[pixel])&16u)oldRisk=1.f;}
            forcedOldRiskZero+=oldRisk<=.000001f;
            if(m.w>0){
                if(oldRisk<=.000001f)forcedOldRiskZeroDirections+=std::uint64_t(m.w);
                else forcedExtraBudgetDirections+=std::uint64_t(expected[pixel]-NativeReceiverExposureBudget(gi,freshCollected,soft[pixel].x,fresh.empty()?glm::vec4(0):fresh[pixel]));
            }
        }
    }
    // Stream pages independently: four full-resolution replay atlases must not
    // remain simultaneously resident in CPU memory during untimed inspection.
    std::size_t pageSize=0;
    for(unsigned page=0;page<(adaptive?4u:1u);++page){
        const auto records=adaptive?lab.ReadbackSurfaceCacheNativeReceiverTrace(page):lab.ReadbackSurfaceCacheTransportTrace(true);
        Check(!records.empty()&&records.size()%4==0&&records.size()/4>=mask.size()*8,"Missing native receiver replay page");
        if(page==0)pageSize=records.size();else Check(records.size()==pageSize,"Native receiver page extents differ");
        for(std::size_t i=0;i<records.size()/4;++i){const auto pixel=i/8;const unsigned ray=unsigned(i%8),logical=page*8+ray;
            const auto origin=records[i*4],q=records[i*4+2];bool zero=true;
            for(unsigned part=0;part<4;++part)for(unsigned k=0;k<4;++k)zero&=records[i*4+part][k]==0;
            if(pixel>=mask.size()){Check(zero,"Native replay padding performed work");++result.padding;continue;}
            if(logical>=expected[pixel]){Check(zero,"Native replay performed an excess fresh batch");++result.excess;continue;}
            Check(std::isfinite(origin.w)&&origin.w>=0&&origin.w<=7&&origin.w==std::floor(origin.w),"Invalid native receiver replay state");
            const unsigned state=unsigned(origin.w);if(!state){Check(zero,"Inactive native replay slot is not fully zero");continue;}
            for(unsigned k=0;k<3;++k)Check(std::isfinite(q[k])&&q[k]>=0&&q[k]==std::floor(q[k]),"Invalid native receiver query counter");
            Check(page==0&&ray==0?q.z>=0:q.z==0,"Native origin work was counted outside page zero ray zero");
            const auto packed=std::uint64_t(q.z);result.originNative+=packed%256;result.originSoftware+=packed/256;
            if(state==7){Check(mask[pixel].w==0&&q.x==0&&q.y==0,"Rejected native source supplied physical directions");++result.rejected;continue;}
            Check(mask[pixel].w>0&&logical<unsigned(mask[pixel].w),"Receiver replay executed an unscheduled direction");
            Check((q.x==1||q.x==2)&&q.y==0,"Native receiver did not use exclusively one traversal plus an optional exact retry");
            for(unsigned part=0;part<4;++part)for(unsigned k=0;k<4;++k)Check(std::isfinite(records[i*4+part][k]),"Nonfinite native physical receiver tuple");
            ++seen[pixel];++result.directions;result.native+=std::uint64_t(q.x);result.retries+=std::uint64_t(q.x)-1;result.software+=std::uint64_t(q.y);
            const unsigned terminal=state>3?state-3:state;result.unknown+=terminal==2&&records[i*4+3].w==0;
        }
    }
    for(std::size_t pixel=0;pixel<mask.size();++pixel)Check(seen[pixel]==unsigned(mask[pixel].w),"Native replay omitted/duplicated a per-pixel physical batch");
    Check(result.directions==result.production&&result.native==result.directions+result.retries,"Four-page replay differs from production mask.w");
    if(haloActive){
        Check(result.production==independentPhysicalDirections&&haloDirections==haloExpectedDirections,
            "Added halo changed independent fresh budgets or lost physical directions");
        std::cout<<"GI90 SUPPORT_HALO_CENSUS profile="<<profile<<" path="<<PathNames[path]<<" untimed=1 added="<<haloPixels
            <<" physical="<<haloPhysical<<" synthetic="<<haloSynthetic<<" source_failed="<<haloFailed<<" risk_zero="<<haloRiskZero
            <<" actual_directions="<<haloDirections<<" independent_fresh_directions="<<haloExpectedDirections
            <<" total_mask_w="<<result.production<<" total_independent_physical_budget="<<independentPhysicalDirections
            <<" strict_force_enabled="<<gi.nativeForceRasterCore<<" halo_force_risk=0 halo_force_budget=0"<<std::endl;
    }
    if(auditQueue)AuditNativeReceiverQueue(lab,gi,profile,path,mask,soft,base,fresh,freshCollected,&forcedCore);
    if(gi.nativeForceRasterCore)std::cout<<"GI90 RASTER_CORE_CENSUS profile="<<profile<<" path="<<PathNames[path]<<" untimed=1 physical="<<forcedPhysical
        <<" source_failed="<<forcedSourceFailed<<" no_actual_unavailable_anchor_or_support="<<forcedNoActual
        <<" original_effective_risk_zero="<<forcedOldRiskZero<<" original_risk_zero_actual_directions="<<forcedOldRiskZeroDirections
        <<" moving_budget_extra_directions="<<forcedExtraBudgetDirections<<" mask_y_force_one=1 same_pixel_seed_origin_fullN=1"
        <<" source_guards_unchanged=1"<<std::endl;
    return result;
}
struct NativeNeighborCounts {std::uint64_t accepted=0,checked=0,failures=0,queries=0,queriedPairs=0,retries=0,acceptedQueries=0;};
NativeNeighborCounts NativeNeighborCensus(MaterialLabModule& lab,const FrameSettings& frame,
    const SurfaceCacheGeometryFixture::Oracle* oracle=nullptr) {
    const auto table=lab.ReadbackSurfaceCacheNativeNeighbors(),origins=lab.ReadbackSurfaceCacheProbeOrigins();
    const auto& gi=frame.effects.surfaceCacheGi;const bool diagnostic=lab.PipelineStatistics().surfaceCacheNativeNeighborQueryDiagnostics;
    constexpr unsigned neighbors=Render::SurfaceCacheSpatialNeighborCount;NativeNeighborCounts result;
    Check(!origins.empty()&&table.size()>=origins.size()*neighbors,"Missing native spatial-neighbor table");
    for(std::size_t i=0;i<table.size();++i){const auto v=table[i];
        for(unsigned k=0;k<4;++k)Check(std::isfinite(v[k]),"Nonfinite native neighbor entry");
        Check(v.w>=0&&v.w<=2&&v.w==std::floor(v.w)&&(!diagnostic?v.w==0:true),"Invalid actual native neighbor initialization count");
        if(diagnostic){result.queries+=std::uint64_t(v.w);result.queriedPairs+=v.w>0;result.retries+=v.w==2;}
        if(i>=origins.size()*neighbors){Check(v==glm::vec4(0),"Native neighbor padding performed work");continue;}
        if(v.x<=0){Check(v.x==0&&v.y==0,"Rejected native neighbor retained a radiance weight");continue;}
        Check(v.x==std::floor(v.x)&&v.x<=float(origins.size())&&v.y>=.0001f,"Invalid accepted native neighbor ID or weight");
        const auto a=origins[i/neighbors],b=origins[std::size_t(v.x)-1];++result.accepted;
        if(diagnostic&&gi.spatial.visibilityCheck)Check(v.w==1||v.w==2,"Accepted native neighbor skipped visibility traversal");
        result.acceptedQueries+=diagnostic?std::uint64_t(v.w):0;
        for(unsigned k=0;k<4;++k)Check(std::isfinite(a[k])&&std::isfinite(b[k]),"Nonfinite accepted neighbor source");
        Check(a.w>0&&b.w>0,"Accepted native neighbor lacks certified original sources");
        const auto start=glm::dvec3(a),end=glm::dvec3(b),delta=end-start;const double distance=glm::length(delta);
        double scale=1;for(unsigned k=0;k<3;++k)scale=std::max(scale,std::max(std::abs(start[k]),std::abs(end[k])));
        const double epsilon=std::max(std::max(double(gi.originSafetyEpsilon),.00001),8.0*double(std::numeric_limits<float>::epsilon())*scale);
        Check(distance>2*epsilon,"Native neighbor accepted a segment too short for original safe endpoints");
        if(oracle){
            // Exact original visibility interval, in double precision. No
            // endpoint forgiveness or blocker-distance tolerance is applied.
            const auto hit=oracle->Nearest(start,delta/distance,distance-epsilon,std::nextafter(double(1e-5f),0.));
            ++result.checked;if(hit.triangle>=0){if(result.failures<8)std::cout<<"GI90 NEIGHBOR ORACLE FAILURE probe="<<i/neighbors
                <<" slot="<<i%neighbors<<" other="<<std::size_t(v.x)-1<<" distance="<<distance<<" maximum="<<distance-epsilon
                <<" blocker="<<hit.t<<" triangle="<<hit.triangle<<std::endl;++result.failures;}
        }
    }
    Check(!oracle||result.checked==result.accepted,"Native neighbor oracle omitted an accepted segment");
    Check(!diagnostic||result.queries==result.queriedPairs+result.retries,"Native neighbor query accounting mismatch");
    return result;
}
// Sparse same-pixel baseline records exist only in metrics-only native runs.
// RGB stages are luminance; history.w is the dedicated temporal count, while
// current alpha is readability. Decoding a mix never certifies a ray budget.
struct ReceiverBaselineStage {
    std::uint32_t pixel=0;
    float base=0,unfiltered=0,raw=0,current=0,currentCoverage=0,history=0,historyCount=0,effectiveMask=0;
    unsigned char currentDomain=0; // 0: low mask; 1: finite decoded RGBA; 2: unavailable
};
std::vector<ReceiverBaselineStage> CaptureReceiverBaselineStages(MaterialLabModule& lab,
    const std::vector<unsigned char>& bands,const std::vector<glm::vec4>& final) {
    const auto base=lab.ReadbackSurfaceCacheSpatialGi(),unfiltered=lab.ReadbackSurfaceCacheContactUnfilteredGi();
    const auto raw=lab.ReadbackSurfaceCacheContactGi(),history=lab.ReadbackSurfaceCacheTemporalHistory();
    const auto mask=lab.ReadbackSurfaceCacheContactMask();
    const bool freshCollected=lab.PipelineStatistics().surfaceCacheFreshSamplingCollected;
    const auto fresh=freshCollected?lab.ReadbackSurfaceCacheFreshSampling():std::vector<glm::vec4>{};
    Check(base.size()==final.size()&&unfiltered.size()==final.size()&&raw.size()==final.size()&&
        history.size()==final.size()&&mask.size()==final.size()&&bands.size()==final.size()&&
        (!freshCollected||fresh.size()==final.size()),"Baseline temporal cohort readback extent mismatch");
    Finite(base,"baseline cohort spatial base");Finite(unfiltered,"baseline cohort contact unfiltered");
    Finite(raw,"baseline cohort contact filtered temporal input");Finite(history,"baseline cohort temporal history");
    const glm::vec3 luma(.2126f,.7152f,.0722f);std::vector<ReceiverBaselineStage> result;
    for(std::size_t i=0;i<final.size();++i) {
        const unsigned band=bands[i]&3;if(!bands[i]||final[i].w<=0||(band!=2&&band!=3))continue;
        ReceiverBaselineStage r;r.pixel=std::uint32_t(i);
        r.base=glm::dot(glm::vec3(base[i]),luma);r.unfiltered=glm::dot(glm::vec3(unfiltered[i]),luma);
        r.raw=glm::dot(glm::vec3(raw[i]),luma);r.history=glm::dot(glm::vec3(history[i]),luma);r.historyCount=history[i].w;
        float blend=mask[i].x;
        // Exact original CTEffectiveMask semantics. Other fresh channels do not
        // invalidate finite fresh.x; only the full-motion tag needs validation.
        if(freshCollected&&std::isfinite(fresh[i].x)) {
            blend=std::max(blend,std::clamp(fresh[i].x,0.f,1.f));
            if(NativeReceiverMovingTag(fresh[i])&16u)blend=std::max(blend,1.f);
        }
        r.effectiveMask=blend;
        if(std::isfinite(blend)&&blend>=.05f) {
            const auto value=(unfiltered[i]-(1.f-blend)*base[i])/blend;
            bool valid=value.a>=-.0001f&&value.a<=1.0001f;
            for(unsigned c=0;c<4;++c)valid&=std::isfinite(value[c]);
            r.currentDomain=valid?1:2;
            if(valid){r.current=glm::dot(glm::vec3(value),luma);r.currentCoverage=value.a;}
        } else if(!std::isfinite(blend))r.currentDomain=2;
        result.push_back(r);
    }
    return result;
}
struct TemporalCohortSum {
    double base=0,unfiltered=0,raw=0,current=0,currentCoverage=0,history=0,historyCount=0,inverseHistoryCount=0;
    double nativeHistory=0,nativeHistoryCount=0,nativeInverseHistoryCount=0,effectiveMask=0;
    double pairedBaseline=0,pairedNative=0;
    std::uint64_t decoded=0,lowMask=0,unavailable=0,paired=0,historyPositive=0,nativeHistoryPositive=0;
    std::uint64_t historyLe1=0,nativeHistoryLe1=0,historyLt4=0,nativeHistoryLt4=0,historyGe32=0,nativeHistoryGe32=0;
};
void NativeBaseCohortMetrics(MaterialLabModule& lab,const FrameSettings& frame,unsigned view,unsigned checkpoint,
    const std::vector<unsigned char>& bands,const std::vector<glm::vec4>& reference,const std::vector<glm::vec4>& final,
    const std::vector<glm::vec4>& mask,const std::vector<ReceiverBaselineStage>& baselineStages) {
    // Untimed readbacks only: no diagnostic replay or extra rendering pass.
    const auto base=lab.ReadbackSurfaceCacheNativeBaseGi(),soft=lab.ReadbackSurfaceCacheContactMask();
    const auto beforeTemporal=lab.ReadbackSurfaceCacheRawGi(),history=lab.ReadbackSurfaceCacheTemporalHistory();
    const auto current=frame.effects.surfaceCacheGi.sparseReceiverSampling?lab.ReadbackSurfaceCacheTransportReconstruction():std::vector<glm::vec4>{};
    Check(mask.size()==reference.size()&&mask.size()==bands.size()&&mask.size()==final.size()&&
        mask.size()==base.size()&&mask.size()==soft.size()&&mask.size()==beforeTemporal.size()&&mask.size()==history.size()&&
        (!frame.effects.surfaceCacheGi.sparseReceiverSampling||current.size()==mask.size()),"Native cohort metrics readback extent mismatch");
    Finite(base,"native cohort Base");Finite(soft,"native cohort original soft mask");Finite(beforeTemporal,"native cohort before temporal");
    if(!current.empty())Finite(current,"native cohort current reconstruction");
    Finite(history,"native cohort temporal history");std::size_t baselineCursor=0;
    struct Sum {
        TemporalCohortSum temporal;
        glm::dvec3 base{0};double soft=0,current=0,beforeTemporal=0,final=0,reference=0,baseCoverage=0,currentCoverage=0;
        std::uint64_t pixels=0,baseValid=0,support=0,readable=0,riskPositive=0,noPhysical=0,noCurrent=0,noReadable=0,sourceFailed=0;
    };
    std::array<std::array<std::array<Sum,4>,2>,64> groups{};const glm::dvec3 luma(.2126,.7152,.0722);
    for(std::size_t i=0;i<mask.size();++i) {
        const unsigned band=bands[i]&3;if(!bands[i]||reference[i].w<=0||(band!=2&&band!=3))continue;
        const auto m=mask[i];const bool physical=m.w>0,interpolated=m.x==.25f,support=physical||interpolated;
        const unsigned category=physical?0:interpolated?1:m.x==0?2:3;auto& sum=groups[bands[i]>>2][band-2][category];
        // A completed unknown budget is still current support. Coverage is
        // reported separately and never decides whether rays were executed.
        const auto value=current.empty()?glm::vec4(0):current[i];const bool requested=soft[i].x>.05f;
        ++sum.pixels;sum.base+=glm::dvec3(base[i]);sum.soft+=soft[i].x;
        sum.current+=glm::dot(glm::dvec3(value),luma);sum.beforeTemporal+=glm::dot(glm::dvec3(beforeTemporal[i]),luma);
        sum.final+=glm::dot(glm::dvec3(final[i]),luma);sum.reference+=glm::dot(glm::dvec3(reference[i]),luma);
        sum.baseCoverage+=base[i].a;sum.currentCoverage+=value.a;sum.baseValid+=base[i].a>0;
        sum.support+=support;sum.readable+=support&&value.a>0;sum.riskPositive+=requested;
        sum.noPhysical+=requested&&!physical;sum.noCurrent+=requested&&!support;
        sum.noReadable+=requested&&(!support||value.a<=0);sum.sourceFailed+=m.x<0;
        Check(baselineCursor<baselineStages.size()&&baselineStages[baselineCursor].pixel==i,
            "Temporal cohort sparse baseline pixel differs from candidate");
        const auto& b=baselineStages[baselineCursor++];auto& t=sum.temporal;
        t.base+=b.base;t.unfiltered+=b.unfiltered;t.raw+=b.raw;t.history+=b.history;t.historyCount+=b.historyCount;
        t.nativeHistory+=glm::dot(glm::dvec3(history[i]),luma);t.nativeHistoryCount+=history[i].w;t.effectiveMask+=b.effectiveMask;
        t.decoded+=b.currentDomain==1;t.lowMask+=b.currentDomain==0;t.unavailable+=b.currentDomain==2;
        if(b.currentDomain==1){t.current+=b.current;t.currentCoverage+=b.currentCoverage;
            if(support&&!current.empty()){++t.paired;t.pairedBaseline+=b.current;t.pairedNative+=glm::dot(glm::dvec3(value),luma);}}
        if(b.historyCount>0){++t.historyPositive;t.inverseHistoryCount+=1./b.historyCount;}
        if(history[i].w>0){++t.nativeHistoryPositive;t.nativeInverseHistoryCount+=1./history[i].w;}
        t.historyLe1+=b.historyCount<=1;t.nativeHistoryLe1+=history[i].w<=1;
        t.historyLt4+=b.historyCount<4;t.nativeHistoryLt4+=history[i].w<4;
        t.historyGe32+=b.historyCount>=32;t.nativeHistoryGe32+=history[i].w>=32;
    }
    Check(baselineCursor==baselineStages.size(),"Temporal cohort did not consume its sparse baseline");
    constexpr std::array<const char*,4> names{{"physical","interpolated","cache","other"}};
    for(unsigned group=0;group<groups.size();++group)for(unsigned band=0;band<2;++band)for(unsigned category=0;category<4;++category) {
        const auto& sum=groups[group][band][category];if(!sum.pixels)continue;const double n=double(sum.pixels);
        std::cout<<"GI90 NATIVE COHORT METRICS ONLY view="<<view<<" frame="<<checkpoint<<" group="<<group
            <<" band="<<(band==0?"core":"shell")<<" sampling="<<names[category]<<" pixels="<<sum.pixels
            <<" base_mean="<<glm::dot(sum.base,luma)/n<<" base_r="<<sum.base.r/n<<" base_g="<<sum.base.g/n<<" base_b="<<sum.base.b/n
            <<" base_coverage="<<sum.baseCoverage/n<<" base_valid="<<sum.baseValid<<" softmask_mean="<<sum.soft/n
            <<" softmask_gt_005="<<sum.riskPositive<<" risk_gt_005_no_physical="<<sum.noPhysical
            <<" risk_gt_005_no_current_support="<<sum.noCurrent<<" risk_gt_005_no_readable_current="<<sum.noReadable
            <<" current_support="<<sum.support<<" current_readable="<<sum.readable<<" source_failed="<<sum.sourceFailed
            <<" current_source_mean="<<sum.current/n<<" current_source_coverage="<<sum.currentCoverage/n<<" current_source_available="<<!current.empty()
            <<" before_temporal_mean="<<sum.beforeTemporal/n<<" temporal_mean="<<sum.final/n<<" reference_mean="<<sum.reference/n
            <<" support_contract=physical_w_or_synthetic_x025 threshold=.05"<<std::endl;
        const auto& t=sum.temporal;const double decoded=double(std::max<std::uint64_t>(1,t.decoded));
        const double paired=double(std::max<std::uint64_t>(1,t.paired));
        std::cout<<"GI90 TEMPORAL COHORT METRICS ONLY view="<<view<<" frame="<<checkpoint<<" group="<<group
            <<" band="<<(band==0?"core":"shell")<<" sampling="<<names[category]<<" pixels="<<sum.pixels
            <<" baseline_base_mean="<<t.base/n<<" baseline_contact_unfiltered_mean="<<t.unfiltered/n
            <<" baseline_filtered_raw_mean="<<t.raw/n<<" native_filtered_raw_mean="<<sum.beforeTemporal/n
            <<" baseline_effective_mask_mean="<<t.effectiveMask/n
            <<" baseline_current_decoded_mean="<<t.current/decoded<<" baseline_current_decoded_coverage="<<t.currentCoverage/decoded
            <<" baseline_current_decoded="<<t.decoded<<" baseline_current_below_005="<<t.lowMask<<" baseline_current_unavailable="<<t.unavailable
            <<" current_paired="<<t.paired<<" baseline_current_paired_mean="<<t.pairedBaseline/paired<<" native_current_paired_mean="<<t.pairedNative/paired
            <<" baseline_history_mean="<<t.history/n<<" native_history_mean="<<t.nativeHistory/n
            <<" baseline_history_count_mean="<<t.historyCount/n<<" native_history_count_mean="<<t.nativeHistoryCount/n
            <<" baseline_inverse_history_count_mean="<<t.inverseHistoryCount/double(std::max<std::uint64_t>(1,t.historyPositive))
            <<" native_inverse_history_count_mean="<<t.nativeInverseHistoryCount/double(std::max<std::uint64_t>(1,t.nativeHistoryPositive))
            <<" baseline_history_positive="<<t.historyPositive<<" native_history_positive="<<t.nativeHistoryPositive
            <<" baseline_history_count_le1="<<t.historyLe1<<" native_history_count_le1="<<t.nativeHistoryLe1
            <<" baseline_history_count_lt4="<<t.historyLt4<<" native_history_count_lt4="<<t.nativeHistoryLt4
            <<" baseline_history_count_ge32="<<t.historyGe32<<" native_history_count_ge32="<<t.nativeHistoryGe32
            <<" baseline_history_minus_filtered_raw="<<(t.history-t.raw)/n
            <<" native_history_minus_filtered_raw="<<(t.nativeHistory-sum.beforeTemporal)/n
            <<" baseline_raw_source=contact_filter native_raw_source=receiver_filter history_w=temporal_count current_alpha=readability"
            <<" inverse_count=nominal_current_weight_before_recovery_and_clamp"<<std::endl;
    }
}
// UE High geometry uses the fixed full-sphere equal-area octahedron, not
// the legacy per-receiver cosine domain. This is an independent double
// evaluation of the public 8x8 / 16x16 angular-cell center contract.
glm::dvec3 UeEqualAreaDirection(unsigned ray,unsigned resolution) {
    const double x=2.*(double(ray%resolution)+.5)/resolution-1.,y=2.*(double(ray/resolution)+.5)/resolution-1.;
    const double diamond=1.-std::abs(x)-std::abs(y),radius=1.-std::abs(diamond);
    const double angle=radius==0?0:.7853981633974483096*((std::abs(y)-std::abs(x))/radius+1.);
    const double ring=radius*std::sqrt(std::max(0.,2.-radius*radius));
    const auto sign=[](double value){return value>0?1.:value<0?-1.:0.;};
    return glm::normalize(glm::dvec3(ring*sign(x)*std::abs(std::cos(angle)),
        ring*sign(y)*std::abs(std::sin(angle)),sign(diamond)*(1.-radius*radius)));
}
struct UeWorldAuditCounts {
    std::uint64_t directions=0,native=0,retries=0,checked=0,nearChecks=0,uniform=0,failures=0;
    double maximum=0;
};
UeWorldAuditCounts AuditUeWorldRays(MaterialLabModule& lab,const FrameSettings& frame,
    SurfaceCacheGeometryFixture::Oracle* oracle=nullptr) {
    const auto data=lab.ReadbackUeLumenRadianceCache(true);UeWorldAuditCounts result;
    Check(data.available&&data.diagnostics,"UE world geometry audit lacks actual diagnostic records");
    const auto& c=data.control;
    Check(c[57]==0&&c[56]<=c[60]&&data.audit.size()==std::size_t(c[56])*4,
        "UE world ray audit overflow or truncated records");
    Check(c[2]<=16384&&data.queue.size()==std::size_t(c[2])*4&&std::isfinite(data.maxDistance)&&data.maxDistance>0,
        "UE world trace queue/interval is invalid");
    std::uint64_t expectedDirections=0,expectedUniform=0;
    for(unsigned i=0;i<c[2];++i){
        const auto packed=data.queue[i*4+3];const unsigned level=(packed>>24)&127u,resolution=packed>>31?8:16;
        Check(level<4&&(packed&0xffffffu)<16384,"UE world queue has an invalid clipmap/probe ID");
        const float start=(50.f/48.f)*std::ldexp(1.f,int(level))*1.7320508075688772f;
        if(data.maxDistance>start+1e-5f){expectedDirections+=resolution*resolution;expectedUniform+=resolution*resolution/16;}
    }
    result.directions=c[13];result.native=c[12];
    Check(result.directions==expectedDirections&&result.native>=result.directions&&result.native<=2*result.directions,
        "UE world actual native directions differ from the executed queue intervals");
    result.retries=result.native-result.directions;
    Check(c[59]==expectedUniform,"UE world audit omitted an executed uniform sample");
    std::vector<unsigned char> seen(std::size_t(c[2])*256,0);
    for(std::size_t i=0;i<data.audit.size()/4;++i){
        const auto logical=data.audit[i*4],encodedDistance=data.audit[i*4+1],triangle=data.audit[i*4+2],flags=data.audit[i*4+3];
        const unsigned queue=logical/256,ray=logical%256;
        Check(queue<c[2]&&!seen[logical]++,"UE world audit duplicated an actual ray or has an invalid queue ID");
        const auto packed=data.queue[queue*4+3];const unsigned level=(packed>>24)&127u,resolution=packed>>31?8:16;
        Check(ray<resolution*resolution,"UE world audit reported an inactive angular slot");
        const bool found=(flags&1)!=0,exhausted=(flags&2)!=0,close=(flags&16)!=0,uniform=(flags&32)!=0;
        const unsigned inits=flags>>8;
        const float distance=std::bit_cast<float>(encodedDistance);
        const float start=(50.f/48.f)*std::ldexp(1.f,int(level))*1.7320508075688772f;
        const double limit=double(float(data.maxDistance-start));constexpr double minimum=1e-5;
        Check(std::isfinite(distance)&&distance>=minimum&&distance<=limit&&limit>minimum&&(inits==1||inits==2),
            "UE world physical ray lies outside its actual interval or lacks native query work");
        Check(found==(triangle!=0xffffffffu)&&close==(found&&!exhausted&&distance<=.2f)&&uniform==(logical%16==0)&&
            (close||uniform),"UE world audit flags do not describe the actual sampled ray");
        const glm::dvec3 center(std::bit_cast<float>(data.queue[queue*4]),std::bit_cast<float>(data.queue[queue*4+1]),
            std::bit_cast<float>(data.queue[queue*4+2]));
        Check(std::isfinite(center.x)&&std::isfinite(center.y)&&std::isfinite(center.z),"Nonfinite UE world probe center");
        const auto direction=UeEqualAreaDirection(ray,resolution),origin=center+direction*double(start);
        result.nearChecks+=close;result.uniform+=uniform;
        if(!oracle)continue;
        auto independent=oracle->Nearest(origin+direction*minimum,direction,limit-minimum);
        if(independent.triangle>=0)independent.t+=minimum;
        double error=exhausted?1000.:0.;
        if(!exhausted&&found)error=independent.triangle<0?1000.:std::abs(independent.t-double(distance));
        else if(!exhausted&&independent.triangle>=0&&independent.t<limit-.0003)error=1000.;
        if(error>.0003&&result.failures<8)std::cout<<"GI90 ORACLE FAILURE ue_world=1 logical="<<logical
            <<" reported="<<distance<<" reference="<<independent.t<<" interval="<<limit<<" triangle="<<triangle<<std::endl;
        result.maximum=std::max(result.maximum,error);result.failures+=error>.0003;++result.checked;
    }
    Check(result.nearChecks==c[58]&&result.uniform==c[59],"UE world audit omitted a reported near hit or uniform record");
    std::cout<<"GI90 UE WORLD "<<(oracle?"ORACLE":"CENSUS")<<" actual_directions="<<result.directions
        <<" actual_native_queries="<<result.native<<" retries="<<result.retries<<" software_queries=0 checked="<<result.checked
        <<" near_reported="<<c[58]<<" near_records="<<result.nearChecks<<" uniform_records="<<result.uniform
        <<" overflow="<<c[57]<<" endpoint_max_m="<<result.maximum<<" failures="<<result.failures
        <<" actual_intervals=1 no_retrace=1"<<std::endl;
    return result;
}

NativeProbeCounts NativeProbeCensus(MaterialLabModule& lab,bool ueHigh=false) {
    const auto& stats=lab.PipelineStatistics();
    Check(stats.surfaceCacheNativeScreenProbeTransport&&stats.surfaceCacheNativeProbeQueryDiagnostics,
        "Native probe census requires the actual instrumented native producer");
    const auto probes=lab.ReadbackScreenProbePlacement();const auto rays=lab.ReadbackScreenProbeTrace(false,false,false);
    const auto origins=lab.ReadbackSurfaceCacheProbeOrigins();NativeProbeCounts result;
    Check(rays.rayResolution==8&&rays.probeSlots>0&&origins.size()==rays.probeSlots,
        "Native probe census lost the original 64-direction/source ABI");
    result.capacity=std::uint64_t(rays.probeSlots)*64;
    Check(rays.radianceStatus.size()==result.capacity&&rays.hitPixelStepsMip.size()==result.capacity&&
        rays.rayDirectionDepthFetches.size()==result.capacity&&(!ueHigh||rays.hitWorldDistance.size()==result.capacity)&&
        stats.surfaceCacheProbeRayBudget==result.capacity,
        "Native probe ray capacity differs from the original logical probe budget");
    Check(probes.uniform.size()+probes.spawned.size()==rays.probeSlots,"Native probe placement/census extent mismatch");
    Check(probes.uniformNormals.size()==probes.uniform.size()&&probes.spawnedNormals.size()==probes.spawned.size(),
        "Native probe census lacks original launch normals");
    for(std::size_t probe=0;probe<rays.probeSlots;++probe) {
        const auto source=probe<probes.uniform.size()?probes.uniform[probe]:probes.spawned[probe-probes.uniform.size()];
        const auto normalValue=probe<probes.uniformNormals.size()?probes.uniformNormals[probe]:probes.spawnedNormals[probe-probes.uniformNormals.size()];
        const bool active=source.w>=.5f,launchable=origins[probe].w>0;
        Check(!launchable||active,"Inactive native probe received a certified source");
        const auto normal=launchable?glm::normalize(glm::dvec3(normalValue)):glm::dvec3(0,0,1);
        const auto axis=std::abs(normal.z)<.99?glm::dvec3(0,0,1):glm::dvec3(0,1,0);
        const auto tangent=glm::normalize(glm::cross(axis,normal)),bitangent=glm::cross(normal,tangent);
        result.active+=active;result.launchable+=launchable;std::uint64_t actual=0;
        for(unsigned ray=0;ray<64;++ray) {
            const auto at=probe*64+ray;const auto value=rays.radianceStatus[at],q=rays.hitPixelStepsMip[at],direction=rays.rayDirectionDepthFetches[at];
            for(unsigned k=0;k<4;++k)Check(std::isfinite(value[k])&&std::isfinite(direction[k]),"Nonfinite native probe sample");
            Check(value.w>=0&&value.w==std::floor(value.w),"Invalid native probe status");
            const unsigned state=unsigned(value.w);Check(state<result.status.size(),"Invalid native probe status range");++result.status[state];
            Check(state==0||state==5||state==9||state==11||state==13||state==15||state==16,"Unexpected native probe terminal state");
            Check(std::isfinite(q.x)&&q.x==0&&std::isfinite(q.y)&&q.y>=0&&q.y<=2&&q.y==std::floor(q.y),
                "Native probe query diagnostics are not actual zero-software/one-or-two-native counts");
            const bool queried=state==9||state==11||state==13||state==15;
            Check(queried?(q.y==1||q.y==2):q.y==0,"Native probe terminal state disagrees with its physical query count");
            if(state==0||state==11||state==15||state==16)Check(glm::vec3(value)==glm::vec3(0),"Unknown native probe direction supplied light");
            const bool generated=glm::dot(glm::vec3(direction),glm::vec3(direction))>.5f;result.generated+=generated;
            Check(!queried||generated,"Native query lacks its original generated direction");
            if(ueHigh)Check(state!=5,"UE full-sphere probe retained a hemisphere rejection");
            if(queried&&ueHigh){
                const auto hit=rays.hitWorldDistance[at],origin=origins[probe];
                for(unsigned k=0;k<4;++k)Check(std::isfinite(hit[k])&&std::isfinite(origin[k]),
                    "UE actual-direction census lacks finite same-query endpoint/source");
                const auto delta=glm::dvec3(hit)-glm::dvec3(origin);const double length=glm::length(delta);
                Check(origin.w>0&&std::isfinite(q.z)&&q.z>1e-5f&&hit.w>=1e-5f&&hit.w<=q.z&&
                    std::isfinite(length)&&length>0&&std::abs(length-double(hit.w))<=.0003,
                    "UE actual importance ray endpoint differs from its physical interval/distance");
                // Importance sampling and blue-noise jitter change physical
                // directions; same-query endpoints retain the actual direction,
                // independently of the canonical scatter/gather bin metadata.
                const auto actualDirection=delta/length;
                Check(std::isfinite(actualDirection.x)&&std::isfinite(actualDirection.y)&&std::isfinite(actualDirection.z),
                    "UE actual importance ray has no finite direction");
                if(q.w<0)Check(std::abs(double(hit.w)-double(q.z))<=.0003,
                    "UE native miss did not retain its original near-query endpoint");
                ++actual;++result.directions;result.native+=std::uint64_t(q.y);result.retries+=std::uint64_t(q.y)-1;
            }else if(queried){
                // Check each original 8x8 angular cell geometrically, without
                // duplicating the production hash or jitter implementation.
                const auto d=glm::normalize(glm::dvec3(direction));const double cosine=glm::dot(d,normal);
                const double radial=(1-cosine*cosine)*8;double azimuth=std::atan2(glm::dot(d,bitangent),glm::dot(d,tangent))/6.283185307179586;
                if(azimuth<0)azimuth+=1;azimuth*=8;constexpr double tolerance=.00002;
                Check(cosine>0&&radial>=double(ray%8)-tolerance&&radial<=double(ray%8+1)+tolerance&&
                    azimuth>=double(ray/8)-tolerance&&azimuth<=double(ray/8+1)+tolerance,
                    "Native producer omitted, duplicated or permuted an original two-dimensional cosine stratum");
                ++actual;++result.directions;result.native+=std::uint64_t(q.y);result.retries+=std::uint64_t(q.y)-1;
            }
        }
        Check(actual==(launchable?64u:0u),"Native probe omitted or duplicated directions from its certified 64-ray budget");
    }
    Check(result.generated==result.active*64&&result.directions==result.launchable*64&&
        result.native==result.directions+result.retries,"Native producer direction/query census disagrees with its original strata");
    Check(!stats.surfaceCacheWorldTransportReused,"Native probe base reported reuse of an unexecuted world transport cache");
    return result;
}
void AuditNativeProbeTransport(MaterialLabModule& lab,FrameSettings& frame,unsigned profile,unsigned path) {
    auto& p=frame.effects.screenProbePlacement;auto& gi=frame.effects.surfaceCacheGi;
    const bool oldProbeDiagnostic=p.trace.sdf.collectRayDiagnostics,oldLocalDiagnostic=gi.contact.collectSamplingDiagnostics;
    p.trace.sdf.collectRayDiagnostics=true;gi.contact.collectSamplingDiagnostics=!gi.ueLumenHigh;Draw(lab,frame);
    const auto probes=NativeProbeCensus(lab,gi.ueLumenHigh);AuditReceiverCompaction(lab,gi.compactReceiverTracing,profile,path);
    const auto lighting=LightingQueryCensus(lab,gi);const auto receiver=NativeReceiverCensus(lab,gi,true,profile,path,&frame);
    const auto neighbors=NativeNeighborCensus(lab,frame);
    const auto world=gi.ueLumenHigh?AuditUeWorldRays(lab,frame):UeWorldAuditCounts{};
    std::cout<<"GI90 AUDIT profile="<<profile<<" path="<<PathNames[path]<<" untimed=1 split_transport=1 native_screen_probe=1"
        <<" cache_transport_rays="<<world.directions<<" cache_native_queries="<<world.native<<" cache_software_queries=0"
        <<" world_update_executed="<<gi.ueLumenHigh<<" ue_radiance_cache="<<gi.ueLumenHigh
        <<" capacity="<<probes.capacity<<" active_probes="<<probes.active<<" launchable_probes="<<probes.launchable
        <<" generated_directions="<<probes.generated<<" main_actual_directions="<<probes.directions
        <<" main_software_queries="<<probes.software<<" main_native_queries="<<probes.native<<" main_native_retries="<<probes.retries
        <<" expected_probe_rays="<<probes.launchable*64<<" cache_lighting_native_queries="<<lighting[0]<<" cache_lighting_software_queries="<<lighting[1]
        <<" cache_lighting_count_available="<<(lighting.available&&lighting.actual)<<" cache_lighting_query_source="<<lighting.Source()<<" cache_lighting_updated="<<lighting.updated
        <<" actual_receiver_rays="<<receiver.directions<<" receiver_gather_native="<<receiver.native<<" receiver_gather_software="<<receiver.software<<" receiver_native_retries="<<receiver.retries
        <<" receiver_origin_native="<<receiver.originNative<<" receiver_origin_software="<<receiver.originSoftware<<" reconstructed_receivers="<<receiver.reconstructed
        <<" expected_receiver_rays="<<receiver.production<<" receiver_budget_8="<<receiver.budgets[0]<<" receiver_budget_16="<<receiver.budgets[1]
        <<" receiver_budget_24="<<receiver.budgets[2]<<" receiver_budget_32="<<receiver.budgets[3]
        <<" receiver_pages="<<(gi.ueLumenHigh?0:gi.nativeAdaptiveFreshSampling?4:1)<<" receiver_unknown_hits="<<receiver.unknown<<" receiver_padding_zero="<<receiver.padding
        <<" receiver_excess_zero="<<receiver.excess<<" receiver_fresh_collected="<<lab.PipelineStatistics().surfaceCacheFreshSamplingCollected
        <<" neighbor_accepted="<<neighbors.accepted<<" neighbor_actual_native_queries="<<neighbors.queries<<" neighbor_native_retries="<<neighbors.retries
        <<" neighbor_actual_count_available="<<lab.PipelineStatistics().surfaceCacheNativeNeighborQueryDiagnostics
        <<" independent_fresh_budget=1 sampling_budget_exact=1";
    for(unsigned i=0;i<probes.status.size();++i)std::cout<<" status"<<i<<"="<<probes.status[i];std::cout<<std::endl;
    p.trace.sdf.collectRayDiagnostics=oldProbeDiagnostic;gi.contact.collectSamplingDiagnostics=oldLocalDiagnostic;
}
void Audit(MaterialLabModule& lab,FrameSettings& frame,unsigned profile,unsigned path) {
    auto& p=frame.effects.screenProbePlacement;auto& gi=frame.effects.surfaceCacheGi;
    if(gi.nativeScreenProbeTransport){AuditNativeProbeTransport(lab,frame,profile,path);return;}
    if(gi.splitReceiverGather){
        gi.contact.collectSamplingDiagnostics=true;Draw(lab,frame);
        AuditReceiverCompaction(lab,gi.compactReceiverTracing,profile,path);
        const auto lighting=LightingQueryCensus(lab,gi);
        std::array<std::uint64_t,2> directions{},nativeQueries{},nativeRetries{},softwareQueries{},originNative{},originSoftware{},launchable{};
        for(unsigned local=0;local<2;++local){const auto records=lab.ReadbackSurfaceCacheTransportTrace(local!=0);
            Check(!records.empty()&&records.size()%4==0,"Missing transport audit records");
            const unsigned count=local?gi.contact.raysPerPixel:gi.transportRays;
            for(std::size_t i=0;i<records.size()/4;++i){const auto origin=records[i*4],q=records[i*4+2];
                const unsigned state=unsigned(origin.w+.5f);Check(state<=7,"Invalid transport audit state");
                if(i%count==0)launchable[local]+=state>0&&state<7;
                if(!state)continue;
                for(unsigned k=0;k<3;++k)Check(q[k]>=0&&q[k]==std::floor(q[k]),"Invalid transport query counter");
                const auto packed=std::uint64_t(q.z);originNative[local]+=packed%256;originSoftware[local]+=packed/256;
                if(state==7)continue;++directions[local];
                if(gi.coherentReceiverTracing){Check(q.x==1||q.x==2,"Hybrid native ray must execute exactly one traversal plus an optional exact retry");
                    nativeRetries[local]+=std::uint64_t(q.x)-1;}
                nativeQueries[local]+=std::uint64_t(q.x);softwareQueries[local]+=std::uint64_t(q.y);
            }
            Check(directions[local]==launchable[local]*count,"Transport directions differ from scheduled whole strata");
            Check(nativeQueries[local]==directions[local]+nativeRetries[local],"Native transport query count differs from actual directions and exact retries");
        }
        std::uint64_t productionDirections=0,reconstructedReceivers=0;
        for(auto v:lab.ReadbackSurfaceCacheTransportMask()) {
            Check(v.w>=0&&v.w==std::floor(v.w),"Invalid production receiver count");
            productionDirections+=std::uint64_t(v.w);
            if(v.x==.25f){
                if(gi.deferredReceiverMix)Check(v.w==0&&std::isfinite(v.y)&&v.y>=0&&v.y<=1,"Deferred reconstruction lost geometric risk or physical provenance");
                else Check(v.w==0&&v.y>=1&&v.y<=4&&v.y==std::floor(v.y),"Sparse reconstruction lacks current-ray neighbors");
                ++reconstructedReceivers;}
        }
        Check(productionDirections==directions[1],"Transport replay differs from production receiver directions");
        std::cout<<"GI90 AUDIT profile="<<profile<<" path="<<PathNames[path]<<" untimed=1 split_transport=1"
            <<" cache_transport_rays="<<directions[0]<<" cache_native_queries="<<nativeQueries[0]<<" cache_software_queries="<<softwareQueries[0]
            <<" cache_native_retries="<<nativeRetries[0]
            <<" cache_origin_native="<<originNative[0]<<" cache_origin_software="<<originSoftware[0]
            <<" cache_lighting_native_queries="<<lighting[0]<<" cache_lighting_software_queries="<<lighting[1]
            <<" cache_lighting_count_available="<<(lighting.available&&lighting.actual)<<" cache_lighting_query_source="<<lighting.Source()<<" cache_lighting_updated="<<lighting.updated
            <<" actual_receiver_rays="<<directions[1]<<" receiver_gather_native="<<nativeQueries[1]<<" receiver_gather_software="<<softwareQueries[1]
            <<" receiver_native_retries="<<nativeRetries[1]
            <<" receiver_origin_native="<<originNative[1]<<" receiver_origin_software="<<originSoftware[1]
            <<" reconstructed_receivers="<<reconstructedReceivers
            <<" expected_receiver_rays="<<launchable[1]*gi.contact.raysPerPixel<<" sampling_budget_exact=1"<<std::endl;
        gi.contact.collectSamplingDiagnostics=false;return;
    }
    p.trace.sdf.collectRayDiagnostics=true;gi.contact.collectSamplingDiagnostics=true;gi.contact.collectTraceDiagnostics=true;
    Draw(lab,frame);
    AuditReceiverCompaction(lab,false,profile,path);
    const auto lighting=LightingQueryCensus(lab,gi);
    const auto probes=lab.ReadbackScreenProbePlacement();
    const auto rays=lab.ReadbackScreenProbeTrace(false,false,false);
    const auto screen=lab.ReadbackScreenProbeTrace(false,true,false);
    const auto origins=lab.ReadbackSurfaceCacheProbeOrigins();
    const auto receivers=lab.ReadbackSurfaceCacheReceiverSampling();
    const auto diagnostics=lab.ReadbackSurfaceCacheContactTraceDiagnostics();
    const auto mask=lab.ReadbackSurfaceCacheContactMask();
    const bool hasFresh=lab.PipelineStatistics().surfaceCacheFreshSamplingCollected;
    const auto fresh=hasFresh?lab.ReadbackSurfaceCacheFreshSampling():std::vector<glm::vec4>{};
    Check(mask.size()==receivers.size()&&(!hasFresh||fresh.size()==receivers.size()),"GI90 receiver planning extent mismatch");
    std::uint64_t active=0,launchable=0,directions=0,screenDirections=0,software=0,native=0,receiverRays=0,readable=0;
    std::array<std::uint64_t,18> status{};
    for(const auto& values:{probes.uniform,probes.spawned})for(auto v:values)active+=v.w>=.5f;
    for(auto v:origins)launchable+=v.w>0;
    Check(rays.radianceStatus.size()==screen.radianceStatus.size(),"GI90 diagnostic atlas mismatch");
    for(std::size_t i=0;i<rays.radianceStatus.size();++i) {
        const auto v=rays.radianceStatus[i];const auto state=unsigned(v.w+.5f);
        Check(state<status.size(),"GI90 invalid ray status");++status[state];
        const auto q=rays.hitPixelStepsMip[i];
        Check(q.x>=0&&q.y>=0&&q.x==std::floor(q.x)&&q.y==std::floor(q.y),"GI90 diagnostic counter is not an integer");
        software+=std::uint64_t(q.x);native+=std::uint64_t(q.y);
        directions+=glm::dot(glm::vec3(rays.rayDirectionDepthFetches[i]),glm::vec3(rays.rayDirectionDepthFetches[i]))>.5f;
        // STTrace stores its depth fetches, including its first source-depth read.
        screenDirections+=screen.rayDirectionDepthFetches[i].w>0;
    }
    for(auto v:receivers){Check(v.y>=0&&v.y==std::floor(v.y)&&v.w>=0&&v.w<=v.y,"GI90 invalid actual receiver ray count");
        receiverRays+=std::uint64_t(v.y);readable+=std::uint64_t(v.w);}
    std::array<std::uint64_t,4> receiverQueries{};std::uint64_t expectedReceiverRays=0;
    for(std::size_t i=0;i<receivers.size();++i) {
        const auto pageValue=[&](unsigned page,unsigned component){
            const auto value=diagnostics[(page-1)/2][i][((page-1)%2)*2+component/2];
            Check(std::isfinite(value)&&value>=0&&value<=16777215&&value==std::floor(value),"Invalid packed GI90 diagnostic integer");
            const auto packed=std::uint32_t(value);return component%2?packed/4096u:packed%4096u;
        };
        receiverQueries[0]+=pageValue(2,0);receiverQueries[1]+=pageValue(3,1);
        receiverQueries[2]+=pageValue(4,0);receiverQueries[3]+=pageValue(5,1);
        Check(pageValue(6,0)<=1&&pageValue(6,1)<=pageValue(6,0)&&pageValue(6,2)<=pageValue(6,1)&&pageValue(6,3)==0,
            "Receiver diagnostic states disagree with launch eligibility");
        Check(float(pageValue(1,1)+pageValue(1,2)+pageValue(1,3))==receivers[i].w,
            "Receiver diagnostic terminal counts disagree with readable rays");
        unsigned expected=0;
        if(pageValue(6,2)) {
            unsigned batches=gi.contact.prioritizeContactCore&&mask[i].x>=.75f?2:1;
            if(hasFresh) {
                const auto v=fresh[i];if(std::isfinite(v.x)&&std::isfinite(v.y)&&v.x>.000001f)
                    batches=std::max(batches,unsigned(std::clamp(std::floor(v.y/gi.contact.raysPerPixel+.5f),1.f,4.f)));
                if(std::isfinite(v.z)&&v.z>=0&&v.z<=31&&v.z==std::floor(v.z)) {
                    const unsigned tag=unsigned(v.z),multiplier=(tag&15)>>1;
                    if((multiplier==2||multiplier==4)&&(mask[i].x>=.25f||(tag&16)))batches=std::max(batches,multiplier);
                }
            }
            expected=gi.contact.raysPerPixel*batches;
        }
        Check(receivers[i].y==float(expected),"Actual receiver loop duplicated or omitted its scheduled directions");
        expectedReceiverRays+=expected;
    }
    std::cout<<"GI90 AUDIT profile="<<profile<<" path="<<PathNames[path]<<" untimed=1"
        <<" capacity="<<rays.radianceStatus.size()<<" active_probes="<<active<<" launchable_probes="<<launchable
        <<" generated_directions="<<directions<<" screen_depth_launches="<<screenDirections
        <<" main_software_queries="<<software<<" main_native_queries="<<native
        <<" cache_lighting_native_queries="<<lighting[0]<<" cache_lighting_software_queries="<<lighting[1]
        <<" cache_lighting_count_available="<<(lighting.available&&lighting.actual)<<" cache_lighting_query_source="<<lighting.Source()<<" cache_lighting_updated="<<lighting.updated
        <<" actual_receiver_rays="<<receiverRays<<" receiver_readable="<<readable
        <<" receiver_origin_native="<<receiverQueries[0]<<" receiver_origin_software="<<receiverQueries[1]
        <<" receiver_gather_native="<<receiverQueries[2]<<" receiver_gather_software="<<receiverQueries[3]
        <<" expected_receiver_rays="<<expectedReceiverRays<<" sampling_budget_exact=1";
    for(unsigned i=0;i<status.size();++i)std::cout<<" status"<<i<<"="<<status[i];
    std::cout<<std::endl;
    p.trace.sdf.collectRayDiagnostics=false;gi.contact.collectSamplingDiagnostics=false;gi.contact.collectTraceDiagnostics=false;
}
void Performance(MaterialLabModule& lab,const Options& options) {
#ifndef NDEBUG
    Check(false,"GI90 performance requires a Release NDEBUG executable");
#endif
    Check(options.vulkan&&!options.softwareRays&&!options.validation,"GI90 timing requires native Vulkan Release without validation");
    const unsigned n=options.surfaceCacheDynamicFrames;
    Check(!options.gi90PerformanceUeOnly||(options.gi90Profile==21&&n==32&&options.benchmarkPresent),"UE-only timing requires profile21,32 samples and --benchmark-present");
    std::cout<<"GI90 SETUP build=Release_NDEBUG target_fps=90 internal="<<(options.renderSizeSet?options.benchmarkWidth:1280)<<"x"
        <<(options.renderSizeSet?options.benchmarkHeight:800)<<" present="<<options.benchmarkPresent
        <<" measured="<<n<<" order="<<(options.gi90PerformanceUeOnly?"21,21":"0,1,1,0")<<" readbacks_in_timing=0 frame_generation=0 contact_detail_timing=1"
        <<" contact_detail_nesting=original_inside_contact_trace_native_outside contact_detail_nonadditive=1";
    if(options.gi90PerformanceUeOnly)std::cout<<" performance_ue_only=1 epochs=2 static_warm=64 motion_warm=16";
    std::cout<<std::endl;
    for(unsigned path=0;path<5;++path)for(unsigned trial=0;trial<(options.gi90PerformanceUeOnly?2u:4u);++trial) {
        const unsigned profile=options.gi90PerformanceUeOnly?21u:(trial==0||trial==3)?0:options.gi90Profile;
        auto frame=Make(options,profile,false);const auto base=*frame.camera;
        frame.effects.surfaceCacheGi.contact.collectTraceTimings=true;
        const auto light=frame.effects.areaLights.lights[0].radiance;Reset(frame);
        for(unsigned f=0;f<64;++f)Draw(lab,frame);
        for(unsigned f=0;f<16;++f){Pose(frame,base,path,f,light);Draw(lab,frame);}
        for(unsigned f=0;f<n;++f) {
            Pose(frame,base,path,f,light);
            const auto start=std::chrono::steady_clock::now();glfwPollEvents();const auto t=lab.MeasureFrames(frame,1,0);
            const double loop=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            frame.effects.cameraCut=false;frame.effects.screenProbePlacement.reset=false;frame.effects.surfaceCacheGi.temporal.reset=false;
            Check(t.gpuFrameMilliseconds.size()==1&&t.backendHostFrameStatistics.size()==1&&t.uniformSnapshots.size()==1,
                "GI90 missing completed-frame timings");
            const auto& stats=lab.PipelineStatistics();
            if(frame.effects.surfaceCacheGi.ueLumenHigh) {
                // UE's screen-probe gather deliberately has no per-receiver
                // contact/split pass. Verify its actual completed producers.
                Check(stats.surfaceCacheUeLumenHigh&&stats.surfaceCacheNativeScreenProbeTransport&&
                    stats.probeSdfHardwareRayTracing&&!stats.probeSdfDirectSoftwareRefinement&&stats.probeSdfNativeUploadsPruned&&
                    stats.surfaceCacheHardwareOrigins&&stats.surfaceCacheProbeOriginsCollected&&
                    stats.screenProbeSinglePassOutputs&&stats.screenProbeHzbLevels>0&&stats.surfaceCacheProbeRayBudget>0,
                    "GI90 lost the intended UE High native screen-probe/HZB path");
            } else Check(stats.surfaceCacheSplitTransport||stats.surfaceCacheGatherCacheReused||(stats.surfaceCacheHardwareContact&&
                (profile<2?stats.probeSdfDirectSoftwareRefinement:stats.probeSdfHardwareRayTracing)),"GI90 lost the intended intersection path");
            std::cout<<"GI90 SAMPLE profile="<<profile<<" path="<<PathNames[path]<<" trial="<<trial<<" frame="<<f
                <<" loop="<<loop<<" gpu="<<t.gpuFrameMilliseconds.front()<<" present="<<frame.present
                <<" capacity="<<stats.surfaceCacheProbeRayBudget<<" spatial_texels="<<stats.surfaceCacheSpatialFilterTexels
                <<" shadow_passes="<<stats.areaLightShadowPasses<<" cache_reused="<<stats.surfaceCacheGatherCacheReused
                <<" raw_omitted="<<stats.surfaceCacheRawGatherOmitted<<" sdf_cpu_voxels="<<lab.PreparedProbeSdfScene()->Statistics().updatedVoxels;
            const auto scopes=Scopes(t);for(unsigned at=1;at<ScopeCount;++at)if(!scopes[at]->empty())std::cout<<" "<<ScopeNames[at]<<"="<<scopes[at]->front();
            if(!t.ueRadianceCacheFrameMilliseconds.empty())std::cout<<" ue_radiance_cache="<<t.ueRadianceCacheFrameMilliseconds.front();
            if(!t.contactRiskFrameMilliseconds.empty())std::cout<<" contact_risk="<<t.contactRiskFrameMilliseconds.front();
            if(!t.contactMaskFrameMilliseconds.empty())std::cout<<" contact_mask="<<t.contactMaskFrameMilliseconds.front();
            if(!t.ueProbeImportanceFrameMilliseconds.empty())std::cout<<" ue_probe_importance="<<t.ueProbeImportanceFrameMilliseconds.front();
            if(!t.nativeProbeNeighborsFrameMilliseconds.empty())std::cout<<" native_neighbors="<<t.nativeProbeNeighborsFrameMilliseconds.front();
            if(!t.nativeProbeFilterFrameMilliseconds.empty())std::cout<<" native_coefficient_filter="<<t.nativeProbeFilterFrameMilliseconds.front();
            if(!t.nativeProbeBaseFrameMilliseconds.empty())std::cout<<" native_base_reconstruct="<<t.nativeProbeBaseFrameMilliseconds.front();
            if(!t.placementUniformFrameMilliseconds.empty())std::cout<<" placement_uniform="<<t.placementUniformFrameMilliseconds.front();
            if(!t.placementMarkFrameMilliseconds.empty())std::cout<<" placement_mark="<<t.placementMarkFrameMilliseconds.front();
            if(!t.placementSpawnFrameMilliseconds.empty())std::cout<<" placement_spawn="<<t.placementSpawnFrameMilliseconds.front();
            if(options.gi90PerformanceUeOnly)std::cout<<" card_lighting_updated_texels="<<stats.surfaceCacheUpdatedTexels
                <<" card_lighting_written_texels="<<stats.surfaceCacheLightingWrittenTexels
                <<" card_lighting_updated="<<stats.surfaceCacheLightingUpdated;
            std::cout<<" native_detail_nested_in_probe_spatial="<<stats.surfaceCacheNativeScreenProbeTransport
                <<" native_base_timed_in_contact_mask="<<stats.surfaceCacheNativeBaseTimedWithContactMask
                <<" native_reconstruction_fused="<<stats.surfaceCacheNativeReconstructionFused
                <<" ue_lumen_high="<<stats.surfaceCacheUeLumenHigh
                <<" native_force_raster_core="<<stats.surfaceCacheNativeForceRasterCore
                <<" native_receiver_filter_support_halo="<<stats.surfaceCacheNativeReceiverFilterSupportHalo
                <<" receiver_motion_static_reuse="<<stats.surfaceCacheStaticReceiverMotionCapture
                <<" receiver_motion_draws="<<stats.surfaceCacheReceiverMotionDraws
                <<" receiver_motion_static_owners="<<stats.surfaceCacheReceiverMotionStaticOwners;
            std::cout<<" contact_risk_mask_nested_in_contact_trace="<<(!stats.surfaceCacheNativeScreenProbeTransport&&!stats.surfaceCacheSplitTransport);
            constexpr std::array<const char*,7> extra{{"sun_shadows","reflection","opaque","placement","post_output","receiver_capture","cached_direct"}};
            for(unsigned at=0;at<extra.size();++at)if(std::isfinite(t.architectureFrameMilliseconds.front()[at]))
                std::cout<<" "<<extra[at]<<"="<<t.architectureFrameMilliseconds.front()[at];
            for(unsigned at=0;at<HostCounterNames.size();++at)std::cout<<" "<<HostCounterNames[at]<<"="<<t.backendHostFrameStatistics.front()[at];
            const auto& uniforms=t.uniformSnapshots.front();
            std::cout<<" descriptor_writes="<<uniforms[8]<<" descriptor_cache_hits="<<uniforms[7]
                <<" uniform_snapshot_copied_bytes="<<uniforms[5];
            const auto& packedSnapshot=lab.PreparedProbeSdfScene()->PackedGeometry()->RefitCpuStatistics();
            const bool snapshotActive=lab.PreparedProbeSdfScene()->Statistics().geometrySnapshotMilliseconds>0;
            std::cout<<" prepare="<<t.scenePreparationFrameMilliseconds.front()<<" record="<<t.frameRecordingFrameMilliseconds.front()
                <<" as_prepare="<<t.backendHostFrameMilliseconds.front()[5]<<" as_pack="<<t.backendHostFrameMilliseconds.front()[6]
                <<" command_encode="<<t.backendHostFrameMilliseconds.front()[1]<<" fence="<<t.backendHostFrameMilliseconds.front()[9]
                <<" sdf_prepare="<<t.preparationDetailFrameMilliseconds.front()[0]<<" cache_prepare="<<t.preparationDetailFrameMilliseconds.front()[1]
                <<" pipeline_prepare="<<t.pipelineHostFrameMilliseconds.front()[0]<<" shadow_record="<<t.pipelineHostFrameMilliseconds.front()[1]
                <<" geometry_transform="<<lab.PreparedProbeSdfScene()->Statistics().geometryTransformMilliseconds
                <<" geometry_snapshot="<<lab.PreparedProbeSdfScene()->Statistics().geometrySnapshotMilliseconds
                <<" geometry_snapshot_full_bytes="<<(lab.PreparedProbeSdfScene()->PackedGeometry()->Pixels().size()*sizeof(glm::vec4))
                <<" geometry_snapshot_copy_bytes="<<(snapshotActive?packedSnapshot.snapshotCopiedBytes:0)
                <<" geometry_snapshot_patch_rows="<<(snapshotActive?packedSnapshot.snapshotPatchedRows:0)
                <<" geometry_snapshot_catchup="<<(snapshotActive&&packedSnapshot.snapshotCatchupUsed)
                <<" geometry_snapshot_versions="<<(snapshotActive?packedSnapshot.snapshotCatchupVersions:0)
                <<" geometry_snapshot_fallback="<<(snapshotActive?unsigned(packedSnapshot.snapshotCopyFallback):0)
                <<" geometry_refit="<<lab.PreparedProbeSdfScene()->Statistics().geometryRefitMilliseconds
                <<" geometry_metadata="<<lab.PreparedProbeSdfScene()->Statistics().metadataMilliseconds<<std::endl;
        }
    }
    std::cout<<"GI90 PERFORMANCE COMPLETE"<<std::endl;
}
// Analytic world-space ROIs, independent of the renderer's retrace mask.
std::vector<unsigned char> Bands(const FrameSettings& frame,const ScreenProbeTraceReadback& source) {
    const auto& objects=SealedGiRoomObjects();unsigned cube=0,oval=0,prism=0,low=0;
    for(unsigned i=0;i<objects.size();++i){if(objects[i].movable)cube=i;
        if(std::string(objects[i].name)=="Porcelain upright oval")oval=i;
        if(std::string(objects[i].name)=="Porcelain tall prism")prism=i;
        if(std::string(objects[i].name)=="Porcelain low oval")low=i;}
    ProbePlacementSceneSettings state;state.occluderOffset=frame.screenProbeStudyState.occluderOffset;
    const auto inverse=glm::inverse(frame.camera->view);
    const auto cubeInverse=glm::inverse(ProbePlacementTransform(objects[cube],state));
    const auto prismInverse=glm::inverse(ProbePlacementTransform(objects[prism],state));
    const auto ovalInverse=glm::inverse(ProbePlacementTransform(objects[oval],state));
    const auto lowInverse=glm::inverse(ProbePlacementTransform(objects[low],state));
    const auto box=[](glm::vec3 p,const glm::mat4& transform,glm::vec3 scale){const auto q=glm::abs(glm::vec3(transform*glm::vec4(p,1)))*scale-scale;
        return glm::length(glm::max(q,glm::vec3(0)))+std::min(std::max(q.x,std::max(q.y,q.z)),0.f);};
    const auto ellipsoid=[](glm::vec3 p,const glm::mat4& transform,glm::vec3 scale){return (glm::length(glm::vec3(transform*glm::vec4(p,1)))-1)*std::min({scale.x,scale.y,scale.z});};
    std::vector<unsigned char> result(source.sourceViewPositions.size());
    for(std::size_t i=0;i<result.size();++i){auto v=source.sourceViewPositions[i],n=source.sourceWorldNormals[i];if(v.w<.5f||n.w<.5f)continue;
        const auto p=glm::vec3(inverse*glm::vec4(glm::vec3(v),1));float gap=10;unsigned group=0;
        const auto cubeD=box(p,cubeInverse,objects[cube].scale),prismD=box(p,prismInverse,objects[prism].scale);
        const auto ovalD=ellipsoid(p,ovalInverse,objects[oval].scale),lowD=ellipsoid(p,lowInverse,objects[low].scale);
        if(n.y>.99f&&std::abs(p.y)<.02f){gap=cubeD;group=1;}
        if(std::abs(prismD)<.005f){gap=ovalD;group=2;}
        if(std::abs(ovalD)<.005f){gap=prismD;group=3;}
        if(std::abs(lowD)<.005f){gap=cubeD;group=4;}
        if(std::abs(cubeD)<.005f){gap=lowD;group=5;}
        // Core/shell contrast is compared within the same receiving object and
        // normal octant, never between a bright object and a different floor.
        const auto an=glm::abs(glm::vec3(n));const unsigned axis=an.x>=an.y&&an.x>=an.z?0:an.y>=an.z?1:2;
        const unsigned normalBin=axis*2+(n[axis]<0?1:0);
        const unsigned band=gap>=-.015f&&gap<.03f?2:gap>=.03f&&gap<.35f?3:1;
        result[i]=static_cast<unsigned char>(band+(group<<2)+(normalBin<<5));
    }
    return result;
}
bool ValidateHits(MaterialLabModule& lab,const FrameSettings& frame,unsigned profile,unsigned view,unsigned checkpoint) {
    auto objects=SealedGiRoomObjects();std::vector<SealedGiRoomMaterial> materials;
    for(unsigned i=0;i<objects.size();++i){materials.push_back(SealedGiRoomObjectMaterial(i));
        if(objects[i].movable)objects[i].position.x+=frame.screenProbeStudyState.occluderOffset;}
    SurfaceCacheGeometryFixture::Oracle oracle;oracle.Initialize(objects,materials);
    if(frame.effects.surfaceCacheGi.nativeScreenProbeTransport) {
        const auto rays=lab.ReadbackScreenProbeTrace(false,false,false);const auto origins=lab.ReadbackSurfaceCacheProbeOrigins();
        Check(rays.rayResolution==8&&rays.probeSlots>0&&origins.size()==rays.probeSlots&&
            rays.radianceStatus.size()==std::size_t(rays.probeSlots)*64&&rays.hitWorldDistance.size()==rays.radianceStatus.size()&&
            rays.rayDirectionDepthFetches.size()==rays.radianceStatus.size()&&rays.hitPixelStepsMip.size()==rays.radianceStatus.size(),
            "Missing native producer records for independent geometry audit");
        std::uint64_t mainChecked=0,mainNear=0,mainUnknown=0,mainUnknownNear=0,mainFailures=0,nearPopulation=0,otherCount=0,otherOrdinal=0;
        std::uint64_t mainNativeQueries=0,mainSoftwareQueries=0,mainNativeRetries=0,mainNativeQueriedDirections=0;
        std::uint64_t mainLogicalDistanceDiffRecords=0;
        std::uint64_t localChecked=0,localNear=0,localFailures=0;double mainMaximum=0,localMaximum=0;
        const bool ueHigh=frame.effects.surfaceCacheGi.ueLumenHigh;
        const bool mainQueryCountsAvailable=ueHigh&&lab.PipelineStatistics().surfaceCacheNativeProbeQueryDiagnostics;
        const double mainLimit=frame.effects.screenProbePlacement.trace.maxDistance;constexpr double minimum=1e-5;
        const auto physical=[](unsigned state){return state==9||state==11||state==13||state==15;};
        for(std::size_t i=0;i<rays.radianceStatus.size();++i){const auto value=rays.radianceStatus[i];
            for(unsigned k=0;k<4;++k)Check(std::isfinite(value[k]),"Nonfinite native producer record in geometry audit");
            Check(value.w>=0&&value.w==std::floor(value.w),"Noninteger native producer terminal state in geometry audit");
            const unsigned state=unsigned(value.w);Check(state==0||state==5||state==9||state==11||state==13||state==15||state==16,
                "Invalid native producer terminal state in geometry audit");
            if(state==11)Check(glm::vec3(value)==glm::vec3(0),"Native cache-unknown geometry supplied light");
            if(physical(state)){const auto hit=rays.hitWorldDistance[i];
                for(unsigned k=0;k<4;++k)Check(std::isfinite(hit[k]),"Nonfinite native physical hit in geometry audit");
                const auto metadata=rays.hitPixelStepsMip[i];
                if(mainQueryCountsAvailable){
                    Check(std::isfinite(metadata.x)&&std::isfinite(metadata.y)&&metadata.x>=0&&metadata.x<=2&&metadata.y>=0&&metadata.y<=2&&
                        metadata.x==std::floor(metadata.x)&&metadata.y==std::floor(metadata.y),
                        "UE main actual query counts are not finite bounded nonnegative integers");
                    const auto native=std::uint64_t(metadata.y);
                    mainSoftwareQueries+=std::uint64_t(metadata.x);mainNativeQueries+=native;
                    mainNativeQueriedDirections+=native>0;mainNativeRetries+=native>0?native-1:0;
                }
                const double interval=ueHigh?double(metadata.z):mainLimit;
                if(ueHigh)Check(std::isfinite(metadata.w)&&metadata.w>=-1&&metadata.w==std::floor(metadata.w),
                    "UE native geometry audit lacks an actual hit/miss triangle result");
                Check(std::isfinite(interval)&&interval>minimum&&interval<=mainLimit&&hit.w>=1e-5f&&double(hit.w)<=interval,
                    "Native physical hit lies outside its actual query interval");
                const bool found=ueHigh?metadata.w>=0:(state==9||state==11);
                // Existing production part3.W can differ from the exact native
                // endpoint distance. Equal values cannot identify ScreenHits.
                if(ueHigh&&state==9&&found){const float logical=rays.rayDirectionDepthFetches[i].w;
                    mainLogicalDistanceDiffRecords+=std::isfinite(logical)&&logical!=hit.w;}
                const bool close=found&&state!=15&&hit.w<=.2f;
                nearPopulation+=close;otherCount+=!close;}}
        const auto mainStride=std::max<std::uint64_t>(1,(otherCount+1023)/1024);
        for(std::size_t i=0;i<rays.radianceStatus.size();++i) {
            const unsigned state=unsigned(rays.radianceStatus[i].w+.5f);if(!physical(state))continue;
            const auto hit=rays.hitWorldDistance[i],origin=origins[i/64],direction=rays.rayDirectionDepthFetches[i];
            const auto metadata=rays.hitPixelStepsMip[i];
            const double interval=ueHigh?double(metadata.z):mainLimit;
            const bool found=ueHigh?metadata.w>=0:(state==9||state==11);
            const bool close=found&&state!=15&&hit.w<=.2f;
            if(!close&&otherOrdinal++%mainStride)continue;
            Check(origin.w>0&&std::isfinite(origin.x)&&std::isfinite(origin.y)&&std::isfinite(origin.z)&&
                (ueHigh||(std::isfinite(direction.x)&&std::isfinite(direction.y)&&std::isfinite(direction.z)&&glm::length(glm::vec3(direction))>.5f)),
                "Native producer reported a physical ray without a valid source/direction");
            const auto endpointDelta=glm::dvec3(hit)-glm::dvec3(origin);const double endpointDistance=glm::length(endpointDelta);
            if(ueHigh)Check(std::isfinite(endpointDistance)&&endpointDistance>0,
                "UE actual-query endpoint cannot reconstruct its importance direction");
            const auto d=ueHigh?endpointDelta/endpointDistance:glm::normalize(glm::dvec3(direction));
            auto independent=oracle.Nearest(glm::dvec3(origin)+d*minimum,d,interval-minimum);
            if(independent.triangle>=0)independent.t+=minimum;
            double error=ueHigh?std::abs(endpointDistance-double(hit.w)):0;
            if(state==15)error=1000;
            else if(found) {
                if(independent.triangle<0)error=1000;
                else error=std::max(error,std::max(std::abs(independent.t-double(hit.w)),
                    glm::length(glm::dvec3(hit)-(glm::dvec3(origin)+d*independent.t))));
            }else if(independent.triangle>=0&&independent.t<interval-.0003)error=1000;
            if(error>.0003&&mainFailures<8)std::cout<<"GI90 ORACLE FAILURE main_native=1 ray="<<i<<" state="<<state
                <<" reported="<<hit.w<<" reference="<<independent.t<<" reported_triangle="<<rays.hitPixelStepsMip[i].w
                <<" reference_triangle="<<independent.triangle<<" ox="<<origin.x<<" oy="<<origin.y<<" oz="<<origin.z
                <<" dx="<<direction.x<<" dy="<<direction.y<<" dz="<<direction.z<<std::endl;
            mainMaximum=std::max(mainMaximum,error);mainFailures+=error>.0003;++mainChecked;mainNear+=close;mainUnknown+=state==11;mainUnknownNear+=close&&state==11;
        }
        Check(mainNear==nearPopulation,"Native main geometry audit omitted a reported near hit");
        const bool adaptive=frame.effects.surfaceCacheGi.nativeAdaptiveFreshSampling;
        Check(!adaptive||lab.PipelineStatistics().surfaceCacheNativeReceiverDiagnosticPages==4,
            "Adaptive receiver geometry audit omitted a diagnostic page");
        std::uint64_t localNearPopulation=0;
        for(unsigned page=0;page<(ueHigh?0u:adaptive?4u:1u);++page){
        const auto records=adaptive?lab.ReadbackSurfaceCacheNativeReceiverTrace(page):lab.ReadbackSurfaceCacheTransportTrace(true);
        Check(!records.empty()&&records.size()%4==0,"Missing native-base local receiver records for independent geometry audit");
        const auto localStride=std::max<std::size_t>(1,records.size()/4/1024);const double localLimit=frame.effects.surfaceCacheGi.maxDistance;
        std::uint64_t pageNearPopulation=0,pageNearChecked=0,pageChecked=0,pageFailures=0;
        for(std::size_t i=0;i<records.size()/4;++i) {
            const auto origin=records[i*4],hit=records[i*4+1],direction=records[i*4+3];
            Check(std::isfinite(origin.w)&&origin.w>=0&&origin.w<=7&&origin.w==std::floor(origin.w),
                "Invalid native-base local query encoding before geometry audit");
            const unsigned encoded=unsigned(origin.w+.5f);if(encoded==0||encoded==7)continue;
            Check(encoded<=6,"Invalid native-base local receiver query state");const unsigned state=encoded>3?encoded-3:encoded;
            for(unsigned k=0;k<4;++k)Check(std::isfinite(origin[k])&&std::isfinite(hit[k])&&std::isfinite(direction[k]),
                "Nonfinite native-base local physical query in geometry audit");
            Check(hit.w>=1e-5f&&double(hit.w)<=localLimit&&glm::length(glm::vec3(direction))>.5f,
                "Native-base local physical query lies outside its actual interval or has no direction");
            const bool close=state==2&&hit.w<=.2f;pageNearPopulation+=close;if(!close&&i%localStride)continue;
            const auto d=glm::normalize(glm::dvec3(direction));
            auto independent=oracle.Nearest(glm::dvec3(origin)+d*minimum,d,localLimit-minimum);
            if(independent.triangle>=0)independent.t+=minimum;
            double error=0;if(state==3)error=1000;
            else if(state==2)error=independent.triangle<0?1000:std::max(std::abs(independent.t-double(hit.w)),
                glm::length(glm::dvec3(hit)-(glm::dvec3(origin)+d*independent.t)));
            else if(independent.triangle>=0&&independent.t<localLimit-.0003)error=1000;
            if(error>.0003&&localFailures<8)std::cout<<"GI90 ORACLE FAILURE local=1 native_base=1 page="<<page<<" ray="<<i<<" state="<<state
                <<" reported="<<hit.w<<" reference="<<independent.t<<" reported_triangle="<<records[i*4+2].w
                <<" reference_triangle="<<independent.triangle<<" ox="<<origin.x<<" oy="<<origin.y<<" oz="<<origin.z
                <<" dx="<<direction.x<<" dy="<<direction.y<<" dz="<<direction.z<<std::endl;
            localMaximum=std::max(localMaximum,error);localFailures+=error>.0003;++localChecked;localNear+=close;
            ++pageChecked;pageNearChecked+=close;pageFailures+=error>.0003;
        }
        Check(pageNearChecked==pageNearPopulation,"Native receiver audit omitted a reported near hit from a fresh page");
        localNearPopulation+=pageNearPopulation;
        std::cout<<"GI90 RECEIVER PAGE ORACLE profile="<<profile<<" view="<<view<<" frame="<<checkpoint<<" page="<<page
            <<" checked="<<pageChecked<<" near_population="<<pageNearPopulation<<" near_checked="<<pageNearChecked<<" failures="<<pageFailures<<std::endl;
        }
        const auto neighbors=NativeNeighborCensus(lab,frame,&oracle);
        const auto world=ueHigh?AuditUeWorldRays(lab,frame,&oracle):UeWorldAuditCounts{};
        if(ueHigh)NativeReceiverCensus(lab,frame.effects.surfaceCacheGi,false,profile,0,&frame);
        std::cout<<"GI90 NEIGHBOR ORACLE profile="<<profile<<" view="<<view<<" frame="<<checkpoint
            <<" accepted="<<neighbors.accepted<<" checked="<<neighbors.checked<<" blocked="<<neighbors.failures
            <<" actual_native_queries="<<neighbors.queries<<" queried_pairs="<<neighbors.queriedPairs<<" native_retries="<<neighbors.retries
            <<" accepted_native_queries="<<neighbors.acceptedQueries
            <<" actual_count_available="<<lab.PipelineStatistics().surfaceCacheNativeNeighborQueryDiagnostics
            <<" all_accepted_checked=1 original_safe_endpoints=1 endpoint_forgiveness=0"<<std::endl;
        std::cout<<"GI90 ORACLE profile="<<profile<<" view="<<view<<" frame="<<checkpoint<<" checked="<<mainChecked+localChecked+world.checked
            <<" near_checked="<<mainNear+localNear+world.nearChecks<<" endpoint_max_m="<<std::max({mainMaximum,localMaximum,world.maximum})
            <<" failures="<<mainFailures+localFailures+neighbors.failures+world.failures
            <<" split_transport=1 native_screen_probe=1 world_checked="<<world.checked
            <<" main_checked="<<mainChecked<<" main_near_checked="<<mainNear<<" main_near_population="<<nearPopulation
            <<" main_unknown_checked="<<mainUnknown<<" main_unknown_near_checked="<<mainUnknownNear
            <<" main_other_population="<<otherCount<<" main_other_stride="<<mainStride
            <<" main_actual_directions="<<nearPopulation+otherCount<<" main_query_counts_available="<<mainQueryCountsAvailable
            <<" main_native_queries="<<(mainQueryCountsAvailable?std::to_string(mainNativeQueries):"unavailable")
            <<" main_software_queries="<<(mainQueryCountsAvailable?std::to_string(mainSoftwareQueries):"unavailable")
            <<" main_native_retries="<<(mainQueryCountsAvailable?std::to_string(mainNativeRetries):"unavailable")
            <<" main_native_queried_directions="<<(mainQueryCountsAvailable?std::to_string(mainNativeQueriedDirections):"unavailable")
            <<" main_logical_distance_diff_records="<<mainLogicalDistanceDiffRecords
            <<" main_endpoint_max_m="<<mainMaximum<<" main_failures="<<mainFailures
            <<" local_checked="<<localChecked<<" local_near_checked="<<localNear<<" local_near_population="<<localNearPopulation
            <<" local_pages="<<(ueHigh?0:adaptive?4:1)<<" local_endpoint_max_m="<<localMaximum<<" local_failures="<<localFailures
            <<" neighbor_checked="<<neighbors.checked<<" neighbor_failures="<<neighbors.failures<<std::endl;
        return mainChecked>0&&mainFailures==0&&localFailures==0&&neighbors.failures==0&&world.failures==0;
    }
    if(frame.effects.surfaceCacheGi.splitReceiverGather){
        std::uint64_t checked=0,nearChecked=0,failures=0;double maximum=0;
        for(bool local:{false,true}){const auto records=lab.ReadbackSurfaceCacheTransportTrace(local);
            Check(!records.empty()&&records.size()%4==0,"Missing split transport trace records local="+std::to_string(local)+
                " diagnostic="+std::to_string(lab.PipelineStatistics().surfaceCacheTransportDiagnostics)+" error="+lab.LastError());
            const auto stride=std::max<std::size_t>(1,records.size()/4/1024);
            const double limit=frame.effects.surfaceCacheGi.maxDistance;
            for(std::size_t i=0;i<records.size()/4;++i){const auto origin=records[i*4],hit=records[i*4+1],direction=records[i*4+3];
                const unsigned encoded=unsigned(origin.w+.5f);if(encoded==0||encoded==7)continue;
                Check(encoded<=6,"Invalid split transport query state");const unsigned state=encoded>3?encoded-3:encoded;
                const bool close=state==2&&hit.w<=.2f;if(!close&&i%stride)continue;
                const double queryLimit=local&&encoded>3?frame.effects.surfaceCacheGi.maxDistance:limit;
                // All production intersection helpers use t_min=1e-5. Match
                // that interval in double precision without loosening the
                // .3 mm endpoint tolerance or ignoring in-range blockers.
                const auto d=glm::normalize(glm::dvec3(direction));constexpr double minimum=1e-5;
                auto independent=oracle.Nearest(glm::dvec3(origin)+d*minimum,d,queryLimit-minimum);
                if(independent.triangle>=0)independent.t+=minimum;
                double error=0;if(state==3)error=1000;
                else if(state==2)error=independent.triangle<0?1000:std::abs(independent.t-double(hit.w));
                else if(independent.triangle>=0&&independent.t<queryLimit-.0003)error=1000;
                if(error>.0003&&failures<8)std::cout<<"GI90 ORACLE FAILURE local="<<local<<" ray="<<i<<" state="<<state
                    <<" reported="<<hit.w<<" reference="<<independent.t<<" reported_triangle="<<records[i*4+2].w
                    <<" reference_triangle="<<independent.triangle<<" ox="<<origin.x<<" oy="<<origin.y<<" oz="<<origin.z
                    <<" dx="<<direction.x<<" dy="<<direction.y<<" dz="<<direction.z<<std::endl;
                maximum=std::max(maximum,error);failures+=error>.0003;++checked;nearChecked+=close;
            }
        }
        std::cout<<"GI90 ORACLE profile="<<profile<<" view="<<view<<" frame="<<checkpoint<<" checked="<<checked
            <<" near_checked="<<nearChecked<<" endpoint_max_m="<<maximum<<" failures="<<failures<<" split_transport=1"<<std::endl;
        return checked>0&&failures==0;
    }
    const auto rays=lab.ReadbackScreenProbeTrace(false,false,false);const auto origins=lab.ReadbackSurfaceCacheProbeOrigins();
    const auto stride=std::max<std::size_t>(1,rays.radianceStatus.size()/1024);std::uint64_t checked=0,nearChecked=0,failures=0;double maximum=0;
    for(std::size_t i=0;i<rays.radianceStatus.size();++i) {
        const unsigned state=unsigned(rays.radianceStatus[i].w+.5f);if(state!=9&&state!=10)continue;
        const bool isNearHit=rays.hitWorldDistance[i].w<=.2f;
        if(!isNearHit&&i%stride)continue;
        const auto origin=origins[i/(rays.rayResolution*rays.rayResolution)];if(origin.w<=0)continue;
        const auto direction=glm::normalize(glm::dvec3(rays.rayDirectionDepthFetches[i]));
        const auto hit=oracle.Nearest(glm::dvec3(origin),direction,frame.effects.surfaceCacheGi.maxDistance+.001);
        const double error=hit.triangle<0?1e3:std::abs(hit.t-double(rays.hitWorldDistance[i].w));
        maximum=std::max(maximum,error);failures+=error>.0003;++checked;nearChecked+=isNearHit;
    }
    std::cout<<"GI90 ORACLE profile="<<profile<<" view="<<view<<" frame="<<checkpoint<<" checked="<<checked
        <<" near_checked="<<nearChecked<<" endpoint_max_m="<<maximum<<" failures="<<failures<<std::endl;
    return checked>0&&failures==0;
}
void NativeProbeCacheContracts(MaterialLabModule& lab,FrameSettings& frame) {
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;
    const auto checkConfidence=[&](const std::vector<glm::vec4>& values,bool stationary){
        unsigned limit=gi.ueLumenHigh?10u:stationary?std::max(gi.temporal.maxFrames,64u):
            gi.temporal.motionStability?std::max(gi.temporal.maxFrames,32u):gi.temporal.maxFrames;
        if(!gi.ueLumenHigh&&!stationary&&gi.temporal.localCameraHistory)limit=std::max(limit,std::min(64u,2*gi.temporal.maxFrames));
        for(auto value:values)Check(std::isfinite(value.w)&&value.w>=0&&value.w<=float(limit)+.0001f,
            "Native-base screen-history confidence exceeds its actual image-filter cap");
    };
    const auto checkPath=[&](){const auto& stats=lab.PipelineStatistics();
        Check(stats.surfaceCacheNativeScreenProbeTransport&&(gi.ueLumenHigh||stats.surfaceCacheSplitTransport)&&!stats.surfaceCacheWorldTransportReused,
            "Native-base contract lost its current probe path or reported an unexecuted world cache");};
    gi.contact.collectSamplingDiagnostics=false;placement.trace.sdf.collectRayDiagnostics=true;
    std::shared_ptr<const Render::SurfaceCacheScene> residentCache;
    std::uint64_t residentGeneration=0;
    std::vector<glm::vec4> beforeSettledAtlas;
    for(unsigned f=0;f<66;++f){
        if(gi.ueLumenHigh&&f==65){
            residentCache=gi.cache?gi.cache:lab.PreparedSurfaceCache();Check(bool(residentCache),"UE card contract lacks its resident cache");
            residentGeneration=residentCache->Generation();beforeSettledAtlas=lab.ReadbackSurfaceCacheAtlas();
        }
        Draw(lab,frame);
    }
    checkPath();
    const auto settled=NativeProbeCensus(lab,gi.ueLumenHigh);const auto atlas=lab.ReadbackSurfaceCacheAtlas();Finite(atlas,"Native base Surface Cache");
    const auto checkUeBudgetedRefresh=[&](const std::vector<glm::vec4>& before,const std::vector<glm::vec4>& after,const char* phase){
        const auto cache=gi.cache?gi.cache:lab.PreparedSurfaceCache();const auto stats=lab.PipelineStatistics();
        Check(cache&&cache==residentCache&&cache->Generation()==residentGeneration&&
            stats.surfaceCacheAttributeUploadedTexels==0&&stats.surfaceCacheMetadataUploadedTexels==0&&stats.surfaceCacheGeometryUploadedTexels==0,
            "UE stable-light refresh discarded resident card data");
        const auto resolution=cache->Settings().pageResolution;
        const std::uint64_t page=std::uint64_t(resolution)*resolution;
        Check(resolution>0&&cache->Width()>0&&cache->PlaneTexels()%cache->Width()==0&&
            cache->TexelCount()==std::uint64_t(cache->CardCount())*page&&cache->TexelCount()>=page,
            "UE direct-light contract has no complete physical page");
        // Official quality override LightingUpdateSpeed16, above the default
        // High speed1: Round(64/16), clamped to the original [1,1024] range.
        // Keep per-axis rounding, CardTileSize8 alignment and the page floor.
        constexpr unsigned lightingUpdateSpeed=16;
        const auto effectiveFactor=std::clamp(std::uint32_t(64.f/float(lightingUpdateSpeed)+.5f),1u,1024u);
        const float axisScale=1.f/std::sqrt(float(effectiveFactor));
        const auto axis=[&](std::uint32_t extent){const auto scaled=std::uint32_t(float(extent)*axisScale+.5f);
            return std::max(resolution,((scaled+7u)/8u)*8u);};
        const auto budget=std::min<std::uint64_t>(cache->TexelCount(),
            std::uint64_t(axis(cache->Width()))*axis(cache->PlaneTexels()/cache->Width()));
        Check(budget>=page&&stats.surfaceCacheTexels==cache->TexelCount()&&stats.surfaceCacheLightingUpdated&&
            stats.surfaceCacheUpdatedTexels>=page&&stats.surfaceCacheUpdatedTexels<=budget&&stats.surfaceCacheUpdatedTexels%page==0&&
            stats.surfaceCacheLightingWrittenTexels==stats.surfaceCacheUpdatedTexels,
            "UE periodic card refresh omitted a whole page or exceeded its independent High budget");
        Finite(after,"UE budgeted card refresh");
        Check(!before.empty()&&before==after,"UE stable-light budgeted refresh changed resident card-light values");
        std::cout<<"GI90 UE CARD REFRESH CONTRACT phase="<<phase<<" budgeted_refresh=1 updated="<<stats.surfaceCacheUpdatedTexels
            <<" written="<<stats.surfaceCacheLightingWrittenTexels<<" budget="<<budget<<" page_texels="<<page
            <<" ue_lighting_update_speed_override="<<lightingUpdateSpeed<<" effective_update_factor="<<effectiveFactor
            <<" cache_resident=1 atlas_values_exact=1"<<std::endl;
    };
    Check(lab.PipelineStatistics().surfaceCacheNativeReceiverDiagnosticPages==0,
        "Ordinary native rendering published optional receiver replay pages");
    for(unsigned page=0;page<4;++page)Check(lab.ReadbackSurfaceCacheNativeReceiverTrace(page).empty(),
        "Native receiver replay remained readable with sampling diagnostics disabled");
    if(gi.ueLumenHigh)checkUeBudgetedRefresh(beforeSettledAtlas,atlas,"settled");
    else Check(!atlas.empty()&&!lab.PipelineStatistics().surfaceCacheLightingUpdated&&lab.PipelineStatistics().surfaceCacheUpdatedTexels==0,
        "Stable native probes unnecessarily relit the Surface Cache");
    checkConfidence(lab.ReadbackSurfaceCacheTemporalHistory(),true);
    for(unsigned f=0;f<3;++f){Draw(lab,frame);checkPath();
        if(gi.ueLumenHigh)checkUeBudgetedRefresh(atlas,lab.ReadbackSurfaceCacheAtlas(),"unchanged");
        else Check(!lab.PipelineStatistics().surfaceCacheLightingUpdated&&lab.PipelineStatistics().surfaceCacheUpdatedTexels==0&&
            Hash(atlas)==Hash(lab.ReadbackSurfaceCacheAtlas()),"Unchanged native receiver frame changed cached card lighting");}
    const auto base=*frame.camera;const auto light=frame.effects.areaLights.lights[0].radiance;
    Pose(frame,base,1,0,light);Draw(lab,frame);checkPath();
    if(gi.ueLumenHigh)checkUeBudgetedRefresh(atlas,lab.ReadbackSurfaceCacheAtlas(),"camera");
    else Check(!lab.PipelineStatistics().surfaceCacheLightingUpdated&&lab.PipelineStatistics().surfaceCacheUpdatedTexels==0&&
        Hash(atlas)==Hash(lab.ReadbackSurfaceCacheAtlas()),"Camera-only native probe movement relit the Surface Cache");
    const auto moved=NativeProbeCensus(lab,gi.ueLumenHigh);checkConfidence(lab.ReadbackSurfaceCacheTemporalHistory(),false);
    frame.camera=base;frame.screenProbeStudyState.occluderOffset=.15f;Draw(lab,frame);checkPath();
    const auto beforeLight=lab.ReadbackSurfaceCacheAtlas();
    frame.effects.areaLights.lights[0].radiance*=.8f;Draw(lab,frame);checkPath();
    Check(lab.PipelineStatistics().surfaceCacheLightingUpdated&&lab.PipelineStatistics().surfaceCacheUpdatedTexels>0&&
        Hash(beforeLight)!=Hash(lab.ReadbackSurfaceCacheAtlas()),"Native probe base retained stale card light after an emission edit");
    if(gi.ueLumenHigh){const auto cache=gi.cache?gi.cache:lab.PreparedSurfaceCache();const auto stats=lab.PipelineStatistics();
        Check(cache&&stats.surfaceCacheUpdatedTexels==cache->TexelCount()&&stats.surfaceCacheLightingWrittenTexels==cache->PlaneTexels(),
            "UE emission edit failed to force a complete resident card-light refresh");}
    frame.width=192;frame.height=120;Draw(lab,frame);checkPath();
    Check(lab.ReadbackSurfaceCacheTemporalGi().size()==std::size_t(frame.width)*frame.height&&
        (gi.ueLumenHigh?lab.ReadbackSurfaceCacheTransportMask().empty():
            lab.ReadbackSurfaceCacheTransportMask().size()==std::size_t(frame.width)*frame.height),
        "Native probe resize retained stale receiver extents");
    const auto resized=NativeProbeCensus(lab,gi.ueLumenHigh);
    if(gi.nativeAdaptiveFreshSampling){
        // Exercise both the active fused Exposure budget and its independent
        // missing-flag fallback. Merely reserving four atlases is never a ray.
        gi.contact.collectSamplingDiagnostics=true;Draw(lab,frame);
        const auto fresh=NativeReceiverCensus(lab,gi,false,0,0,&frame);
        const bool disocclusion=gi.contact.disocclusionSampling;gi.contact.disocclusionSampling=false;Draw(lab,frame);
        Check(!lab.PipelineStatistics().surfaceCacheFreshSamplingCollected,"Disabled Exposure retained an active fresh-budget flag");
        const auto fallback=NativeReceiverCensus(lab,gi,false,0,0,&frame);
        Check(fallback.budgets[1]==0&&fallback.budgets[2]==0&&fallback.budgets[3]==0&&
            fallback.directions==fallback.launchable*8,"Missing fresh flag changed the original eight-direction receiver path");
        gi.contact.disocclusionSampling=disocclusion;frame.screenProbeStudyState.occluderOffset+=.12f;Draw(lab,frame);
        const auto moving=NativeReceiverCensus(lab,gi,false,0,0,&frame);gi.contact.collectSamplingDiagnostics=false;
        std::cout<<"GI90 NATIVE FRESH CONTRACT pages=4 fresh_directions="<<fresh.directions<<" moving_directions="<<moving.directions
            <<" moving_budget_8="<<moving.budgets[0]<<" moving_budget_16="<<moving.budgets[1]<<" moving_budget_24="<<moving.budgets[2]
            <<" moving_budget_32="<<moving.budgets[3]<<" fallback_directions="<<fallback.directions
            <<" fallback_budget_8="<<fallback.budgets[0]<<" fallback_excess_zero="<<fallback.excess
            <<" independently_decoded_exposure=1 per_pixel_mask_w_exact=1 excess_batches_zero=1 padding_zero=1 origin_counter_once=1"
            <<" no_sampling_diagnostics_pages_not_published=1"<<std::endl;
    }
    if(gi.sparseReceiverSampling&&!gi.ueLumenHigh) {
        frame.width=640;frame.height=400;for(unsigned i=0;i<3;++i)Draw(lab,frame);checkPath();
        const auto sparseMask=lab.ReadbackSurfaceCacheTransportMask(),history=lab.ReadbackSurfaceCacheTemporalHistory();
        Check(sparseMask.size()==history.size(),"Native sparse receiver history extent mismatch");checkConfidence(history,true);
        std::uint64_t sparseDirections=0,interpolated=0,baseOnly=0;
        for(auto value:sparseMask) {
            Check(std::isfinite(value.w)&&value.w>=0&&value.w==std::floor(value.w),"Native sparse receiver lost physical direction provenance");
            sparseDirections+=std::uint64_t(value.w);
            if(value.x==.25f){++interpolated;Check(value.w==0&&std::isfinite(value.y)&&value.y>=0&&value.y<=1,
                "Native sparse receiver changed original soft risk into a physical ray count");}
            if(value.x==0)++baseOnly;
        }
        const bool compact=gi.compactReceiverTracing;gi.sparseReceiverSampling=false;gi.compactReceiverTracing=false;
        Draw(lab,frame);checkPath();std::uint64_t fullDirections=0;
        for(auto value:lab.ReadbackSurfaceCacheTransportMask()) {
            Check(std::isfinite(value.w)&&value.w>=0&&value.w==std::floor(value.w),"Native full receiver lost actual direction count");
            fullDirections+=std::uint64_t(value.w);
        }
        gi.sparseReceiverSampling=true;gi.compactReceiverTracing=compact;
        Check(interpolated>100&&sparseDirections*4<fullDirections*3,"Native sparse receivers failed the retained physical-query reduction contract");
        std::cout<<"GI90 SPARSE CONTRACT native_screen_probe=1 sparse_directions="<<sparseDirections<<" full_directions="<<fullDirections
            <<" interpolated="<<interpolated<<" base_only="<<baseOnly<<" actual_queries_reduced=1 receiver_mask_y=original_soft_risk actual_direction_count=mask_w"
            <<" screen_history_confidence=1 screen_confidence_is_world_sample_age=0 world_actual_directions=0"<<std::endl;
    }
    // Reuse the same remote-cache fixture as the full quality run. Certified
    // unknown cache samples must remain black through both new probe and
    // receiver stages, with no reuse of former world/light history.
    auto instances=lab.PreparedProbeSdfScene()->Instances();for(auto& instance:instances)instance.localToWorld[3].x+=50;
    Render::ProbeGlobalSdfSettings tiny;tiny.counts=glm::uvec3(2);const auto remote=Render::BuildProbeSdfScene(instances,tiny);
    gi.cache=Render::BuildSurfaceCache(*remote);Draw(lab,frame);Draw(lab,frame);checkPath();
    const auto unknown=lab.ReadbackSurfaceCacheTemporalGi();Finite(unknown,"Native unknown-cache GI");
    std::uint64_t missing=0;double energy=0;for(auto value:unknown){missing+=value.w<=0;energy+=glm::dot(glm::dvec3(value),glm::dvec3(value));}
    Check(missing>100&&energy<1e-8,"Native probe base fabricated light from unknown cache coverage");
    if(gi.nativeAdaptiveFreshSampling){
        gi.contact.collectSamplingDiagnostics=true;Draw(lab,frame);const auto unknownBudget=NativeReceiverCensus(lab,gi,false,0,0,&frame);
        Check(unknownBudget.unknown>0,"Native unknown-cache fixture did not inspect actual unknown receiver hits");
        std::cout<<"GI90 NATIVE UNKNOWN FRESH CONTRACT actual_directions="<<unknownBudget.directions
            <<" unknown_geometry_hits="<<unknownBudget.unknown<<" budget_8="<<unknownBudget.budgets[0]<<" budget_16="<<unknownBudget.budgets[1]
            <<" budget_24="<<unknownBudget.budgets[2]<<" budget_32="<<unknownBudget.budgets[3]
            <<" unknown_is_not_readable=1 independent_fresh_budget=1 excess_batches_zero=1 padding_zero=1"<<std::endl;
        gi.contact.collectSamplingDiagnostics=false;
    }
    const auto world=gi.ueLumenHigh?AuditUeWorldRays(lab,frame):UeWorldAuditCounts{};
    placement.trace.sdf.collectRayDiagnostics=false;
    std::cout<<"GI90 CACHE CONTRACTS PASS native_screen_probe=1 world_cache="<<gi.ueLumenHigh
        <<" world_actual_directions="<<world.directions<<" screen_confidence_is_world_sample_age=0"
        <<" current_probe_budget_64=1 steady_probe_rays="<<settled.directions<<" camera_probe_rays="<<moved.directions<<" resized_probe_rays="<<resized.directions;
    if(gi.ueLumenHigh)std::cout<<" unchanged_budgeted_refresh_preserves_card_light=1 camera_budgeted_refresh_preserves_card_light=1";
    else std::cout<<" unchanged_preserves_card_light=1 camera_preserves_card_light=1";
    std::cout<<" light_relights=1 resize_current_receivers=1 unknown_black=1 unknown="<<missing<<std::endl;
}
void CacheContracts(MaterialLabModule& lab,const Options& options) {
    auto frame=Make(options,options.gi90Profile,true);frame.width=160;frame.height=100;frame.present=false;
    frame.camera=SealedGiRoomCameraPreset(0,1.6f);Reset(frame);
    if(frame.effects.surfaceCacheGi.nativeScreenProbeTransport){NativeProbeCacheContracts(lab,frame);return;}
    if(frame.effects.surfaceCacheGi.splitReceiverGather){
        const auto& gi=frame.effects.surfaceCacheGi;const bool screenConfidence=gi.geometryReceiverRisk;
        const auto checkConfidence=[&](const std::vector<glm::vec4>& values,bool stationary){
            // This is an image-filter confidence, not an independent world
            // transport sample count. A stable camera reprojection may retain
            // the shader's 32-frame support after a stationary 64-frame epoch.
            unsigned limit=stationary?std::max(gi.temporal.maxFrames,64u):
                gi.temporal.motionStability?std::max(gi.temporal.maxFrames,32u):gi.temporal.maxFrames;
            if(!stationary&&gi.temporal.localCameraHistory)limit=std::max(limit,std::min(64u,2*gi.temporal.maxFrames));
            for(const auto value:values)Check(std::isfinite(value.w)&&value.w>=0&&value.w<=float(limit)+.0001f,
                "Transport screen-history confidence is nonfinite or exceeds its actual shader cap");
        };
        frame.effects.surfaceCacheGi.contact.collectSamplingDiagnostics=false;
        for(unsigned f=0;f<66;++f)Draw(lab,frame);
        Check(lab.PipelineStatistics().surfaceCacheWorldTransportReused,"World transport failed to reuse a valid cache");
        const auto original=lab.ReadbackSurfaceCacheTransport();Check(std::any_of(original.begin(),original.end(),[](auto v){return v.w>0;}),"World transport contained no demanded valid samples");
        if(screenConfidence)checkConfidence(lab.ReadbackSurfaceCacheTemporalHistory(),true);
        const auto base=*frame.camera;const auto light=frame.effects.areaLights.lights[0].radiance;
        Pose(frame,base,1,0,light);Draw(lab,frame);
        Check(lab.PipelineStatistics().surfaceCacheWorldTransportReused,"Camera motion discarded the world-space cache");
        const auto moved=lab.ReadbackSurfaceCacheTransport();std::uint64_t retained=0;
        Check(moved.size()==original.size(),"Camera motion changed the world cache extent");
        for(std::size_t i=0;i<original.size();++i)retained+=original[i].w>0&&original[i]==moved[i];
        Check(retained>10,"Camera motion reported reuse without retaining any actual transport samples");
        const auto mask=lab.ReadbackSurfaceCacheTransportMask(),history=lab.ReadbackSurfaceCacheTemporalHistory();
        Check(mask.size()==history.size(),"Transport history contract extent mismatch");
        if(screenConfidence)checkConfidence(history,false);
        std::uint64_t cacheOnly=0;for(std::size_t i=0;i<mask.size();++i)if(mask[i].x<=0){++cacheOnly;
            if(!screenConfidence)Check(history[i].w<=1,"Repeated cached transport fabricated screen-history samples");}
        Check(cacheOnly>100,"Split transport did not leave a cache-only receiver region");
        frame.camera=base;frame.screenProbeStudyState.occluderOffset=.15f;Draw(lab,frame);
        Check(lab.PipelineStatistics().surfaceCacheWorldTransportReused,"Certified object motion discarded all world transport");
        frame.effects.areaLights.lights[0].radiance*=.8f;Draw(lab,frame);
        Check(!lab.PipelineStatistics().surfaceCacheWorldTransportReused,"Light edit retained stale transport");
        frame.width=192;frame.height=120;Draw(lab,frame);
        Check(lab.PipelineStatistics().surfaceCacheSplitTransport,"Resize lost current receiver evaluation");
        if(frame.effects.surfaceCacheGi.sparseReceiverSampling){
            frame.width=640;frame.height=400;for(unsigned i=0;i<3;++i)Draw(lab,frame);
            std::uint64_t sparseDirections=0,interpolated=0;const auto sparseMask=lab.ReadbackSurfaceCacheTransportMask();
            const auto sparseHistory=lab.ReadbackSurfaceCacheTemporalHistory();
            if(screenConfidence)checkConfidence(sparseHistory,true);
            for(std::size_t i=0;i<sparseMask.size();++i){const auto v=sparseMask[i];
                if(gi.deferredReceiverMix)Check(std::isfinite(v.w)&&v.w>=0&&v.w==std::floor(v.w),"Deferred receiver lost its actual direction count");
                sparseDirections+=std::uint64_t(v.w);
                if(v.x==.25f){++interpolated;
                    if(gi.deferredReceiverMix)Check(v.w==0&&std::isfinite(v.y)&&v.y>=0&&v.y<=1,
                        "Deferred interpolation changed a geometric risk into physical directions");
                    else Check(v.w==0&&v.y>=1&&v.y<=4&&v.y==std::floor(v.y),"Interpolated receiver borrowed a cached frame as a new sample");}
                if(v.x==0&&!screenConfidence)Check(sparseHistory[i].w<=1,"Pure world cache fabricated screen history");}
            const bool compact=frame.effects.surfaceCacheGi.compactReceiverTracing;
            frame.effects.surfaceCacheGi.sparseReceiverSampling=false;frame.effects.surfaceCacheGi.compactReceiverTracing=false;
            Draw(lab,frame);std::uint64_t fullDirections=0;
            for(auto v:lab.ReadbackSurfaceCacheTransportMask())fullDirections+=std::uint64_t(v.w);
            frame.effects.surfaceCacheGi.sparseReceiverSampling=true;frame.effects.surfaceCacheGi.compactReceiverTracing=compact;
            Check(interpolated>100&&sparseDirections*4<fullDirections*3,"Sparse receiver scheduling failed to reduce physical queries");
            std::cout<<"GI90 SPARSE CONTRACT sparse_directions="<<sparseDirections<<" full_directions="<<fullDirections
                <<" interpolated="<<interpolated<<" actual_queries_reduced=1";
            if(screenConfidence)std::cout<<" screen_history_confidence=1 world_retained_equal_age="<<retained;
            else std::cout<<" cached_samples_not_invented=1";
            if(gi.deferredReceiverMix)std::cout<<" receiver_mask_y=geometry_risk actual_direction_count=mask_w";
            std::cout<<std::endl;
        }
        std::cout<<"GI90 CACHE CONTRACTS PASS world_cache=1 camera_preserves_world=1 object_updates_dirty_cards=1 light_invalidates=1 resize_current_receivers=1";
        if(screenConfidence)std::cout<<" screen_history_confidence=1 screen_confidence_is_world_sample_age=0 world_retained_equal_age="<<retained;
        else std::cout<<" cache_only_history_samples=1";
        std::cout<<std::endl;return;
    }
    for(unsigned f=0;f<66;++f)Draw(lab,frame);
    Check(lab.PipelineStatistics().surfaceCacheGatherCacheReused,"Settled gather did not enter cache reuse");
    const auto history=lab.ReadbackSurfaceCacheTemporalHistory();
    for(unsigned f=0;f<3;++f){Draw(lab,frame);Check(lab.PipelineStatistics().surfaceCacheGatherCacheReused,"Stable gather cache was unexpectedly invalidated");}
    Check(Hash(history)==Hash(lab.ReadbackSurfaceCacheTemporalHistory()),"Cached gather fabricated additional independent samples");
    const auto base=*frame.camera;const auto light=frame.effects.areaLights.lights[0].radiance;
    Pose(frame,base,1,0,light);Draw(lab,frame);Check(!lab.PipelineStatistics().surfaceCacheGatherCacheReused,"Camera movement reused stale GI");
    frame.camera=base;frame.screenProbeStudyState.occluderOffset=.15f;Draw(lab,frame);
    Check(!lab.PipelineStatistics().surfaceCacheGatherCacheReused,"Object movement reused stale GI");
    frame.effects.areaLights.lights[0].radiance*=.8f;Draw(lab,frame);
    Check(!lab.PipelineStatistics().surfaceCacheGatherCacheReused,"Light edit reused stale GI");
    frame.width=192;frame.height=120;Draw(lab,frame);
    Check(!lab.PipelineStatistics().surfaceCacheGatherCacheReused,"Resize reused stale GI");
    std::cout<<"GI90 CACHE CONTRACTS PASS history_samples_unchanged=1 camera_invalidates=1 object_invalidates=1 light_invalidates=1 resize_invalidates=1"<<std::endl;
}
void Quality(MaterialLabModule& lab,const Options& options) {
    using Image=std::vector<glm::vec4>;
    struct Snapshot {Image image;std::vector<unsigned char> bands;};
    constexpr std::array<unsigned,10> checkpoints{{64,65,68,72,80,81,88,89,96,112}};
    bool pass=true;const bool metricsOnly=options.gi90QualityMetricsOnly;
    // Detailed stage/cohort readbacks are optional; acceptance still uses every
    // original checkpoint, receiver ROI, baseline image and validity contract.
    const bool stageMetrics=metricsOnly&&options.gi90QualityStageMetrics;
    const bool ueHighStageMetrics=stageMetrics&&Make(options,options.gi90Profile,true).effects.surfaceCacheGi.ueLumenHigh;
    if(metricsOnly)std::cout<<"GI90 QUALITY METRICS ONLY oracle=skipped cache_contracts=skipped png=skipped full_acceptance=not_evaluated"<<std::endl;
    else CacheContracts(lab,options);
    // Record fixed tolerances before looking at candidate images. These compare
    // quality to the retained baseline, not to a physical-lighting ground truth.
    std::cout<<"GI90 QUALITY LIMITS nrmse=.20 contact_contrast_relative=.20 dark_excess_relative=.08 return_extra=.03 views=2 frames=112"<<std::endl;
    for(unsigned view=0;view<2;++view) {
        std::vector<Snapshot> baseline;std::array<Image,2> staticImage;
        const bool temporalCohortMetrics=stageMetrics&&options.gi90Profile>=12&&!ueHighStageMetrics;
        std::vector<std::vector<ReceiverBaselineStage>> baselineStages;
        if(temporalCohortMetrics)baselineStages.resize(checkpoints.size());
        Image decodedReceiver;std::vector<unsigned char> decodedReceiverDomain;
        std::array<double,2> returnResidual{};
        for(unsigned mode=0;mode<2;++mode) {
            const unsigned profile=mode?options.gi90Profile:0;auto frame=Make(options,profile,true);
            frame.camera=SealedGiRoomCameraPreset(view,frame.OutputAspect());const auto base=*frame.camera;
            const auto light=frame.effects.areaLights.lights[0].radiance;Reset(frame);unsigned checkpoint=0;
            for(unsigned f=0;f<112;++f) {
                frame.camera=base;frame.screenProbeStudyState.occluderOffset=0;
                if(f>=64&&f<80)Pose(frame,base,view?2:1,f-64,light);
                if(f>=80&&f<88)frame.screenProbeStudyState.occluderOffset=1.2f;
                const bool inspect=checkpoint<checkpoints.size()&&f+1==checkpoints[checkpoint];
                if(frame.effects.surfaceCacheGi.splitReceiverGather)frame.effects.surfaceCacheGi.contact.collectSamplingDiagnostics=
                    inspect&&!metricsOnly&&!frame.effects.surfaceCacheGi.ueLumenHigh;
                if(frame.effects.surfaceCacheGi.ueLumenHigh)
                    frame.effects.screenProbePlacement.trace.sdf.collectRayDiagnostics=inspect&&!metricsOnly;
                Draw(lab,frame);if(!inspect)continue;
                const auto image=lab.ReadbackSurfaceCacheTemporalGi();Finite(image,"GI90 temporal");
                // Collect all checkpoints even when a geometry gate fails.
                // The final quality result and process exit still reject it.
                if(!metricsOnly)pass&=ValidateHits(lab,frame,profile,view,f+1);
                const auto sources=lab.ReadbackScreenProbeSources();
                Check(image.size()==std::size_t(frame.width)*frame.height&&sources.sourceViewPositions.size()==image.size()&&
                    sources.sourceWorldNormals.size()==image.size(),"GI90 fixed ROI requires full-resolution AO composite and raster sources");
                const auto bands=Bands(frame,sources);
                if(temporalCohortMetrics&&!mode)baselineStages[checkpoint]=CaptureReceiverBaselineStages(lab,bands,image);
                Image f64TransportMask;
                if(stageMetrics&&f==63){
                    const auto& reference=mode?baseline[checkpoint].image:image;
                    const auto stage=[&](const char* name,const Image& values,bool blend=false){
                        Check(values.size()==image.size(),std::string("GI90 missing metrics-only stage ")+name+": "+lab.LastError());
                        struct Mean {double core=0,shell=0;unsigned cores=0,shells=0,coreValid=0,shellValid=0;};
                        std::array<Mean,64> groups{};
                        for(std::size_t i=0;i<values.size();++i)if(bands[i]&&reference[i].w>0){
                            auto& group=groups[bands[i]>>2];const auto value=values[i];
                            const double scalar=blend?value.x:glm::dot(glm::vec3(value),glm::vec3(.2126,.7152,.0722));
                            if((bands[i]&3)==2){group.core+=scalar;++group.cores;group.coreValid+=value.w>0;}
                            if((bands[i]&3)==3){group.shell+=scalar;++group.shells;group.shellValid+=value.w>0;}
                        }
                        for(unsigned index=0;index<groups.size();++index){const auto& group=groups[index];
                            if(group.cores>=8&&group.shells>=32)std::cout<<"GI90 STAGE METRICS ONLY view="<<view<<" profile="<<profile
                                <<" frame="<<f+1<<" stage="<<name<<" field="<<(blend?"blend":"luminance")<<" group="<<index
                                <<" core_mean="<<group.core/group.cores<<" shell_mean="<<group.shell/group.shells
                                <<" core="<<group.cores<<" shell="<<group.shells<<" core_valid="<<group.coreValid<<" shell_valid="<<group.shellValid<<std::endl;
                        }
                    };
                    if(ueHighStageMetrics&&!mode){
                        stage("baseline_final_reflected_gi",image);
                    }else if(frame.effects.surfaceCacheGi.ueLumenHigh){
                        const auto history=lab.ReadbackSurfaceCacheTemporalHistory();
                        const auto capture=lab.ReadbackSurfaceReceiverCapture();
                        Check(history.size()==image.size()&&capture.width==frame.width&&capture.height==frame.height&&
                            capture.reflectance.size()==image.size(),"UE AO decomposition lacks full history/diffuse-color capture");
                        Finite(history,"UE unmodulated irradiance history");Finite(capture.reflectance,"UE diffuse-color capture");
                        Image beforeAO(image.size());
                        for(std::size_t i=0;i<image.size();++i)
                            beforeAO[i]=glm::vec4(glm::vec3(history[i])*glm::max(glm::vec3(capture.reflectance[i]),glm::vec3(0)),image[i].w);
                        stage("ue_unmodulated_irradiance_history",history);
                        stage("ue_reflected_gi_before_ao",beforeAO);
                        stage("ue_final_reflected_gi_with_ao",image);
                        struct AoBand {double reference=0,before=0,after=0;unsigned count=0;};
                        std::array<std::array<AoBand,2>,64> aoGroups{};
                        const glm::dvec3 luma(.2126,.7152,.0722);
                        for(std::size_t i=0;i<image.size();++i)if(bands[i]&&reference[i].w>0){
                            const unsigned band=bands[i]&3u;if(band!=2&&band!=3)continue;
                            auto& value=aoGroups[bands[i]>>2][band==2?0:1];
                            value.reference+=glm::dot(glm::dvec3(reference[i]),luma);
                            value.before+=glm::dot(glm::dvec3(beforeAO[i]),luma);
                            value.after+=glm::dot(glm::dvec3(image[i]),luma);++value.count;
                        }
                        for(unsigned group=0;group<aoGroups.size();++group){
                            const auto& core=aoGroups[group][0];const auto& shell=aoGroups[group][1];
                            if(core.count<8||shell.count<32)continue;
                            const double cr=core.reference/core.count,cb=core.before/core.count,ca=core.after/core.count;
                            const double sr=shell.reference/shell.count,sb=shell.before/shell.count,sa=shell.after/shell.count;
                            const double referenceContrast=sr-cr,beforeContrast=sb-cb,finalContrast=sa-ca;
                            std::cout<<"GI90 UE AO DECOMPOSITION METRICS ONLY view="<<view<<" profile="<<profile
                                <<" frame="<<f+1<<" group="<<group<<" core="<<core.count<<" shell="<<shell.count
                                <<" core_reference="<<cr<<" core_before_ao="<<cb<<" core_final="<<ca
                                <<" core_ao_drop="<<cb-ca<<" core_pre_ao_reference_delta="<<cb-cr
                                <<" shell_reference="<<sr<<" shell_before_ao="<<sb<<" shell_final="<<sa
                                <<" shell_ao_drop="<<sb-sa<<" shell_pre_ao_reference_delta="<<sb-sr
                                <<" contrast_reference="<<referenceContrast<<" contrast_before_ao="<<beforeContrast
                                <<" contrast_final="<<finalContrast<<" contrast_ao_delta="<<finalContrast-beforeContrast
                                <<" contact_error_before_ao="<<std::abs(beforeContrast-referenceContrast)/std::max(.005,std::abs(referenceContrast))
                                <<" contact_error_final="<<std::abs(finalContrast-referenceContrast)/std::max(.005,std::abs(referenceContrast))
                                <<" coverage_source=final_current_alpha history_alpha_is_frame_count=1"<<std::endl;
                        }
                    }else{
                    if(frame.effects.surfaceCacheGi.splitReceiverGather){
                        if(frame.effects.surfaceCacheGi.sparseReceiverSampling){
                            const auto reconstructed=lab.ReadbackSurfaceCacheTransportReconstruction();stage("reconstructed",reconstructed);
                            f64TransportMask=lab.ReadbackSurfaceCacheTransportMask();
                            Check(f64TransportMask.size()==image.size()&&decodedReceiver.size()==image.size()&&
                                decodedReceiverDomain.size()==image.size(),"GI90 decoded-receiver comparison is missing its f64 source");
                            struct Compared {glm::dvec4 original{0},candidate{0};unsigned decoded=0,below=0,invalid=0,coverageOutside=0;};
                            std::array<std::array<std::array<Compared,4>,2>,64> compared{};
                            std::array<std::array<unsigned,2>,64> totals{};
                            for(std::size_t i=0;i<image.size();++i)if(bands[i]&&reference[i].w>0&&((bands[i]&3)==2||(bands[i]&3)==3)){
                                const unsigned group=bands[i]>>2,band=(bands[i]&3)==2?0:1;const auto mask=f64TransportMask[i];
                                const unsigned category=mask.w>0?0:mask.x==.25f?1:mask.x==0?2:3;
                                auto& sum=compared[group][band][category];++totals[group][band];
                                if(decodedReceiverDomain[i]==0){++sum.below;continue;}
                                if(decodedReceiverDomain[i]!=1){++sum.invalid;continue;}
                                sum.original+=glm::dvec4(decodedReceiver[i]);sum.candidate+=glm::dvec4(reconstructed[i]);++sum.decoded;
                                sum.coverageOutside+=decodedReceiver[i].w<-.0001f||decodedReceiver[i].w>1.0001f;
                            }
                            constexpr std::array<const char*,4> names{{"physical","interpolated","cache","other"}};
                            for(unsigned group=0;group<compared.size();++group)if(totals[group][0]>=8&&totals[group][1]>=32)
                                for(unsigned band=0;band<2;++band)for(unsigned category=0;category<names.size();++category){
                                    const auto& sum=compared[group][band][category];const double divisor=std::max(1u,sum.decoded);
                                    const auto a=sum.original/divisor,b=sum.candidate/divisor;const glm::dvec3 luma(.2126,.7152,.0722);
                                    std::cout<<"GI90 RECEIVER DECODE METRICS ONLY view="<<view<<" profile="<<profile<<" frame="<<f+1
                                        <<" group="<<group<<" band="<<(band==0?"core":"shell")<<" sampling="<<names[category]
                                        <<" decoded="<<sum.decoded<<" below_mask_threshold="<<sum.below<<" undecodable="<<sum.invalid
                                        <<" reference_luminance="<<glm::dot(glm::dvec3(a),luma)<<" reconstructed_luminance="<<glm::dot(glm::dvec3(b),luma)
                                        <<" reference_r="<<a.x<<" reference_g="<<a.y<<" reference_b="<<a.z
                                        <<" reconstructed_r="<<b.x<<" reconstructed_g="<<b.y<<" reconstructed_b="<<b.z
                                        <<" reference_coverage="<<a.w<<" reconstructed_coverage="<<b.w
                                        <<" reference_coverage_outside="<<sum.coverageOutside<<" mask_threshold=.05"<<std::endl;
                                }
                        }
                        stage("receiver_filtered",lab.ReadbackSurfaceCacheRawGi());
                    }else{
                        stage("probe_raw",lab.ReadbackSurfaceCacheRawGi());
                        const auto spatial=lab.ReadbackSurfaceCacheSpatialGi();stage("probe_spatial",spatial);
                        const auto mask=lab.ReadbackSurfaceCacheContactMask();stage("contact_blend",mask,true);
                        const auto unfiltered=lab.ReadbackSurfaceCacheContactUnfilteredGi();stage("contact_unfiltered",unfiltered);
                        decodedReceiver.assign(image.size(),glm::vec4(0));decodedReceiverDomain.assign(image.size(),0);
                        struct Decoded {glm::dvec4 sum{0};unsigned decoded=0,below=0,invalid=0,coverageOutside=0;};
                        std::array<std::array<Decoded,2>,64> decoded{};
                        std::array<std::array<unsigned,2>,64> totals{};
                        const bool fresh=lab.PipelineStatistics().surfaceCacheFreshSamplingCollected;
                        for(std::size_t i=0;i<image.size();++i)if(bands[i]&&reference[i].w>0&&((bands[i]&3)==2||(bands[i]&3)==3)){
                            const unsigned group=bands[i]>>2,band=(bands[i]&3)==2?0:1;auto& sum=decoded[group][band];++totals[group][band];
                            const double m=mask[i].x;
                            if(std::isfinite(m)&&m<.05){++sum.below;continue;}
                            decodedReceiverDomain[i]=2;bool finite=std::isfinite(m)&&m<=1.0001&&m>=.05&&!fresh&&unfiltered[i].w>0;
                            const auto value=(glm::dvec4(unfiltered[i])-(1.0-m)*glm::dvec4(spatial[i]))/m;
                            for(unsigned channel=0;channel<4;++channel)finite&=std::isfinite(value[channel]);
                            if(!finite){++sum.invalid;continue;}
                            // U=(1-m)*P+m*R applies equally to linear RGB and
                            // public readability. Decode the whole RGBA tuple;
                            // do not clamp radiance or normalize by readability.
                            decodedReceiver[i]=glm::vec4(value);decodedReceiverDomain[i]=1;sum.sum+=value;++sum.decoded;
                            sum.coverageOutside+=value.w<-.0001||value.w>1.0001;
                        }
                        for(unsigned group=0;group<decoded.size();++group)if(totals[group][0]>=8&&totals[group][1]>=32)
                            for(unsigned band=0;band<2;++band){const auto& sum=decoded[group][band];const auto mean=sum.sum/double(std::max(1u,sum.decoded));
                                std::cout<<"GI90 RECEIVER DECODE METRICS ONLY view="<<view<<" profile="<<profile<<" frame="<<f+1
                                    <<" group="<<group<<" band="<<(band==0?"core":"shell")<<" sampling=baseline"
                                    <<" decoded="<<sum.decoded<<" below_mask_threshold="<<sum.below<<" undecodable="<<sum.invalid
                                    <<" reference_luminance="<<glm::dot(glm::dvec3(mean),glm::dvec3(.2126,.7152,.0722))
                                    <<" reference_r="<<mean.x<<" reference_g="<<mean.y<<" reference_b="<<mean.z
                                    <<" reference_coverage="<<mean.w<<" reference_coverage_outside="<<sum.coverageOutside
                                    <<" mask_threshold=.05 fresh_sampling="<<fresh<<std::endl;
                            }
                        stage("contact_filtered",lab.ReadbackSurfaceCacheContactGi());
                    }
                    stage("temporal_history",lab.ReadbackSurfaceCacheTemporalHistory());
                    }
                    stage("temporal_display",image);
                }
                const auto label="gi90_v"+std::to_string(view)+"_p"+std::to_string(profile)+"_f"+std::to_string(f+1);
                if(!metricsOnly){Tone(options,frame.width,frame.height,image,label+"_indirect");
                    if(!options.screenshot.empty())Save(frame.present?lab.ReadbackDisplay():lab.Readback(),OutputPath(options,label+"_total"));}
                if(f==63)staticImage[mode]=image;
                if(!mode)baseline.push_back({image,bands});
                else {
                    const auto& original=baseline[checkpoint];Check(original.bands==bands,"GI90 changed raster receiver geometry");
                    double square=0,energy=0,coreA=0,coreB=0,shellA=0,shellB=0,darkExcess=0;std::uint64_t count=0,core=0,shell=0,dark=0,unknown=0;
                    const bool split=stageMetrics&&frame.effects.surfaceCacheGi.splitReceiverGather&&!frame.effects.surfaceCacheGi.ueLumenHigh;
                    const auto transportMask=split?(f64TransportMask.empty()?lab.ReadbackSurfaceCacheTransportMask():std::move(f64TransportMask)):Image{};
                    if(split)Check(transportMask.size()==image.size(),"GI90 contact attribution is missing the current transport mask");
                    if(split&&frame.effects.surfaceCacheGi.nativeScreenProbeTransport)
                        NativeBaseCohortMetrics(lab,frame,view,f+1,bands,original.image,image,transportMask,baselineStages[checkpoint]);
                    struct Subset {double before=0,after=0;unsigned pixels=0;};
                    struct Contrast {double coreA=0,coreB=0,shellA=0,shellB=0;unsigned core=0,shell=0;
                        std::array<Subset,4> coreSampling{},shellSampling{};};
                    std::array<Contrast,64> contrast{};
                    std::vector<double> luminances;const glm::vec3 luma(.2126,.7152,.0722);
                    for(std::size_t i=0;i<image.size();++i)if(bands[i]&&original.image[i].w>0)luminances.push_back(glm::dot(glm::vec3(original.image[i]),luma));
                    Check(!luminances.empty(),"GI90 empty visible geometry");std::sort(luminances.begin(),luminances.end());
                    const double darkThreshold=luminances[luminances.size()/10];
                    for(std::size_t i=0;i<image.size();++i)if(bands[i]&&original.image[i].w>0) {
                        const auto a=glm::vec3(original.image[i]),b=glm::vec3(image[i]);const auto delta=b-a;
                        const double la=glm::dot(a,luma),lb=glm::dot(b,luma);square+=glm::dot(delta,delta)/3;energy+=la;++count;unknown+=image[i].w<=0;
                        auto& group=contrast[bands[i]>>2];
                        if((bands[i]&3)==2){coreA+=la;coreB+=lb;++core;group.coreA+=la;group.coreB+=lb;++group.core;}
                        if((bands[i]&3)==3){shellA+=la;shellB+=lb;++shell;group.shellA+=la;group.shellB+=lb;++group.shell;}
                        if(split&&((bands[i]&3)==2||(bands[i]&3)==3)){
                            const auto mask=transportMask[i];
                            // Attribute only the physical work present in this
                            // frame. Reconstructed values and cache reuse do
                            // not become actual receiver directions after filtering.
                            const unsigned category=mask.w>0?0:mask.x==.25f?1:mask.x==0?2:3;
                            auto& subset=((bands[i]&3)==2?group.coreSampling:group.shellSampling)[category];
                            subset.before+=la;subset.after+=lb;++subset.pixels;
                        }
                        if(la<=darkThreshold){darkExcess+=std::max(0.,lb-la);++dark;}
                    }
                    const double mean=energy/std::max<std::uint64_t>(1,count),nrmse=std::sqrt(square/std::max<std::uint64_t>(1,count))/std::max(.005,mean);
                    const double contrastA=shellA/std::max<std::uint64_t>(1,shell)-coreA/std::max<std::uint64_t>(1,core);
                    const double contrastB=shellB/std::max<std::uint64_t>(1,shell)-coreB/std::max<std::uint64_t>(1,core);
                    const double excess=darkExcess/std::max<std::uint64_t>(1,dark)/std::max(.005,mean);
                    double maximumContrastError=0;unsigned measuredGroups=0;
                    for(unsigned index=0;index<contrast.size();++index){const auto& group=contrast[index];if(group.core>=8&&group.shell>=32) {
                        const double a=group.shellA/group.shell-group.coreA/group.core,b=group.shellB/group.shell-group.coreB/group.core;
                        maximumContrastError=std::max(maximumContrastError,std::abs(b-a)/std::max(.005,std::abs(a)));++measuredGroups;
                        std::cout<<"GI90 CONTACT view="<<view<<" frame="<<f+1<<" group="<<index<<" before="<<a<<" after="<<b
                            <<" core_before="<<group.coreA/group.core<<" core_after="<<group.coreB/group.core
                            <<" shell_before="<<group.shellA/group.shell<<" shell_after="<<group.shellB/group.shell
                            <<" core="<<group.core<<" shell="<<group.shell;
                        if(split){constexpr std::array<const char*,4> names{{"physical","interpolated","cache","other"}};
                            for(unsigned band=0;band<2;++band)for(unsigned category=0;category<names.size();++category){
                                const auto& subset=(band==0?group.coreSampling:group.shellSampling)[category];
                                const std::string prefix=std::string(band==0?" core_":" shell_")+names[category];
                                std::cout<<prefix<<"="<<subset.pixels<<prefix<<"_before="<<subset.before/std::max(1u,subset.pixels)
                                    <<prefix<<"_after="<<subset.after/std::max(1u,subset.pixels);
                            }}
                        std::cout<<std::endl;
                    }}
                    const bool accepted=nrmse<=.20&&excess<=.08&&unknown<=count/100&&
                        maximumContrastError<=.20;
                    pass&=accepted;
                    std::cout<<"GI90 QUALITY view="<<view<<" frame="<<f+1<<" profile="<<profile<<" pixels="<<count<<" core="<<core<<" shell="<<shell
                        <<" nrmse="<<nrmse<<" contrast_before="<<contrastA<<" contrast_after="<<contrastB
                        <<" contact_groups="<<measuredGroups<<" contact_group_error="<<maximumContrastError
                        <<" dark_excess="<<excess<<" unknown="<<unknown<<" accepted="<<accepted<<std::endl;
                }
                if(f==111){double error=0,energy=0;std::uint64_t count=0;const auto& initial=staticImage[mode];
                    for(std::size_t i=0;i<image.size();++i)if(bands[i]&&image[i].w>0&&initial[i].w>0){error+=glm::length(glm::vec3(image[i]-initial[i]))/std::sqrt(3.);energy+=glm::dot(glm::vec3(initial[i]),glm::vec3(.2126,.7152,.0722));++count;}
                    returnResidual[mode]=error/std::max(.005*double(count),energy);
                }
                ++checkpoint;
            }
            // Turning all emission off must invalidate both the transport
            // cache and lighting history immediately, including during motion.
            frame.camera=base;frame.screenProbeStudyState.occluderOffset=0;
            frame.effects.areaLights.lights[0].enabled=false;Draw(lab,frame);
            const auto energy=[](const Image& image){double sum=0;for(auto value:image)sum+=glm::dot(glm::dvec3(value),glm::dvec3(value));return sum;};
            Check(!lab.PipelineStatistics().surfaceCacheGatherCacheReused&&energy(lab.ReadbackSurfaceCacheTemporalGi())<1e-8,
                "GI90 retained light after a light-off edit");
            frame.screenProbeStudyState.occluderOffset=.15f;Draw(lab,frame);
            Check(energy(lab.ReadbackSurfaceCacheTemporalGi())<1e-8,"GI90 moving dark receiver fabricated lighting");
            frame.effects.areaLights.lights[0].enabled=true;frame.screenProbeStudyState.occluderOffset=0;Draw(lab,frame);
            auto instances=lab.PreparedProbeSdfScene()->Instances();for(auto& instance:instances)instance.localToWorld[3].x+=50;
            Render::ProbeGlobalSdfSettings tiny;tiny.counts=glm::uvec3(2);const auto remote=Render::BuildProbeSdfScene(instances,tiny);
            frame.effects.surfaceCacheGi.cache=Render::BuildSurfaceCache(*remote);Draw(lab,frame);Draw(lab,frame);
            const auto unknown=lab.ReadbackSurfaceCacheTemporalGi();std::uint64_t count=0;for(auto value:unknown)count+=value.w<=0;
            Check(count>100&&energy(unknown)<1e-8,"GI90 unknown cache coverage borrowed stale light");
            std::cout<<"GI90 VALIDITY profile="<<profile<<" view="<<view<<" light_off=1 moving_black=1 unknown_black=1 unknown="<<count<<std::endl;
        }
        const bool settled=returnResidual[1]<=1.25*returnResidual[0]+.03;pass&=settled;
        std::cout<<"GI90 RETURN view="<<view<<" before="<<returnResidual[0]<<" after="<<returnResidual[1]<<" accepted="<<settled<<std::endl;
    }
    if(metricsOnly)std::cout<<"GI90 QUALITY METRICS ONLY COMPLETE numeric_accepted="<<pass<<" full_acceptance=not_evaluated oracle=skipped cache_contracts=skipped png=skipped"<<std::endl;
    else std::cout<<"GI90 QUALITY COMPLETE accepted="<<pass<<std::endl;
    Check(pass,"GI90 candidate exceeds predeclared image/geometry-quality limits; keep baseline recommended");
}
}
namespace ReceiverCaptureComparisonFixture {
using namespace SurfaceCacheBudgetFixture;
struct Delta {std::uint64_t different=0;double maximum=0;std::uint32_t ulp=0;};
Delta Compare(std::span<const glm::vec4> before,std::span<const glm::vec4> after) {
    Check(before.size()==after.size(),"Receiver capture readback shape differs");Delta delta;
    const auto ordered=[](float x){const auto bits=std::bit_cast<std::uint32_t>(x);return bits&0x80000000u?~bits:bits|0x80000000u;};
    for(std::size_t i=0;i<before.size();++i)for(unsigned c=0;c<4;++c) {
        const float a=before[i][c],b=after[i][c];Check(std::isfinite(a)&&std::isfinite(b),"Nonfinite receiver capture component");
        if(std::bit_cast<std::uint32_t>(a)!=std::bit_cast<std::uint32_t>(b)) {
            ++delta.different;delta.maximum=std::max(delta.maximum,std::abs(double(a)-b));
            const auto u=ordered(a),v=ordered(b);delta.ulp=std::max(delta.ulp,u>v?u-v:v-u);
        }
    }
    return delta;
}
SurfaceReceiverCaptureReadback Pair(MaterialLabModule& lab,FrameSettings frame,const char* label,bool fallback=false) {
    frame.present=false;frame.effects.surfaceCacheGi.cacheSettledGather=false;
    frame.effects.surfaceCacheGi.mrtReceiverCapture=false;Reset(frame);Draw(lab,frame);
    const auto before=lab.ReadbackSurfaceReceiverCapture();Check(!before.reflectance.empty(),lab.LastError());
    Check(!before.mrt,"Legacy receiver capture unexpectedly used MRT");
    frame.effects.surfaceCacheGi.mrtReceiverCapture=true;Reset(frame);Draw(lab,frame);
    auto after=lab.ReadbackSurfaceReceiverCapture();Check(!after.reflectance.empty(),lab.LastError());
    Check(after.mrt==!fallback,"Receiver capture MRT / whole-frame fallback selection differs");
    Check(before.width==after.width&&before.height==after.height,"Receiver capture dimensions changed");
    const auto report=[&](const char* texture,const auto& a,const auto& b,double limit) {
        const auto d=Compare(a,b);const bool accepted=d.maximum<=limit;
        std::cout<<"GI90 CAPTURE_COMPARE case="<<label<<" texture="<<texture<<" untimed=1 pixels="<<a.size()
            <<" mrt="<<after.mrt<<" bitwise_equal="<<(d.different==0)<<" different_components="<<d.different
            <<" max_abs="<<std::setprecision(10)<<d.maximum<<" max_ulp="<<d.ulp<<" abs_limit="<<limit<<" accepted="<<accepted<<std::endl;
        Check(accepted,"MRT receiver capture exceeds predeclared floating-point equivalence limit");
    };
    // Check bitwise first; print the residual even when shader optimization
    // produces an equivalent finite value with a different final float bit.
    report("rho",before.reflectance,after.reflectance,2.e-6);
    report("normal_roughness",before.normals,after.normals,2.e-6);
    report("view_position_owner",before.positions,after.positions,2.e-5);
    Check(before.depth.size()==after.depth.size()&&after.depth.size()==after.positions.size(),"Receiver depth extent differs");
    std::uint64_t owners=0,coverage=0,depth=0,covered=0;double depthMaximum=0;
    for(std::size_t i=0;i<after.positions.size();++i) {
        owners+=std::bit_cast<std::uint32_t>(before.positions[i].w)!=std::bit_cast<std::uint32_t>(after.positions[i].w);
        coverage+=(before.reflectance[i].w>0)!=(after.reflectance[i].w>0);covered+=after.reflectance[i].w>0;
        depth+=std::bit_cast<std::uint32_t>(before.depth[i])!=std::bit_cast<std::uint32_t>(after.depth[i]);
        depthMaximum=std::max(depthMaximum,std::abs(double(before.depth[i])-after.depth[i]));
    }
    std::cout<<"GI90 CAPTURE_IDENTITY case="<<label<<" untimed=1 owner_equal="<<(owners==0)<<" owner_differences="<<owners
        <<" coverage_equal="<<(coverage==0)<<" covered="<<covered<<" depth_bitwise_equal="<<(depth==0)
        <<" depth_differences="<<depth<<" depth_max_abs="<<depthMaximum<<" fallback="<<fallback<<std::endl;
    Check(covered>100&&owners==0&&coverage==0&&depth==0,"MRT receiver owner, coverage or prepass depth differs");return after;
}
void Run(MaterialLabModule& lab,const Options& options) {
    auto frame=Make(options,10,true);frame.present=false;
    Pair(lab,frame,"sealed_room");
    // Existing primitive-scene test entry point plus the actual gallery's five
    // populated maps. The transforms deliberately include unequal scale,
    // rotation, curved geometry, overlapping receivers and a ground plane.
    auto objects=std::make_shared<std::vector<ProbePlacementObject>>(std::initializer_list<ProbePlacementObject>{
        {"capture floor",ShowcaseMesh::Plane,ProbePlacementMaterial::Floor,{0,0,0},{4,1,4}},
        {"capture prism",ShowcaseMesh::Box,ProbePlacementMaterial::Block,{-1.1f,.85f,0},{.65f,.85f,.42f},.37f,.15f,{0,0,1}},
        {"capture oval",ShowcaseMesh::Sphere,ProbePlacementMaterial::Sphere,{1.05f,.8f,-.15f},{.82f,.8f,.47f},-.23f},
        {"capture cutout",ShowcaseMesh::Box,ProbePlacementMaterial::Block,{0,.5f,1.6f},{.48f,.5f,.18f},-.19f}});
    frame.sealedGiRoomStudy=false;frame.probeSceneOverride=objects;
    frame.probeSceneMaterialsOverride=std::make_shared<std::vector<SealedGiRoomMaterial>>(objects->size());
    auto camera=*frame.camera;camera.position={0,2.4f,6.8f};camera.view=glm::lookAt(camera.position,glm::vec3(0,.7f,0),glm::vec3(0,1,0));
    frame.camera=camera;
    auto replacements=std::make_shared<std::vector<ReceiverCaptureMaterialOverride>>();
    for(std::uint32_t i=1;i<objects->size();++i)replacements->push_back({i,16,{}});
    frame.receiverCaptureMaterialOverrides=replacements;
    const auto mapMaterial=lab.InspectMaterial(16);
    Check(mapMaterial.normalMap&&mapMaterial.metallicMap&&mapMaterial.roughnessMap&&mapMaterial.aoMap,
        "Capture fixture requires the actual five-map gallery material");
    const auto mapped=Pair(lab,frame,"five_maps_nonuniform");
    const auto ownerCount=[](const SurfaceReceiverCaptureReadback& image,float owner) {
        return std::count_if(image.positions.begin(),image.positions.end(),[&](auto value){return value.w==owner;});
    };
    Check(ownerCount(mapped,2)>100&&ownerCount(mapped,3)>100&&ownerCount(mapped,4)>100,"Capture fixture lacks visible transformed receivers");
    // Map switches are instance parameters, not legal per-object overrides.
    // Reuse an existing scalar gallery asset, then restore the mapped asset
    // for the alpha/owner tests. Only override parameters declared perObject.
    const auto scalarMaterial=lab.InspectMaterial(0);
    Check(!scalarMaterial.normalMap&&!scalarMaterial.metallicMap&&!scalarMaterial.roughnessMap&&!scalarMaterial.aoMap,
        "Capture scalar fixture unexpectedly enables texture maps");
    for(auto& material:*replacements){material.galleryMaterialIndex=0;material.parameters={{"metallic",0.f},{"roughness",.15f}};}
    const auto scalar=Pair(lab,frame,"scalar_metallic_roughness");
    const auto rhoResponse=Compare(mapped.reflectance,scalar.reflectance),roughnessResponse=Compare(mapped.normals,scalar.normals);
    Check(rhoResponse.maximum>.01&&roughnessResponse.maximum>.01,"Capture fixture did not distinguish mapped and scalar material inputs");
    for(auto& material:*replacements){material.galleryMaterialIndex=16;material.parameters={{"opacity",.6f},{"alphaCutoff",.5f}};}
    const auto visible=Pair(lab,frame,"opacity_above_cutoff");
    replacements->back().cutout=true;replacements->back().parameters={{"opacity",.4f},{"alphaCutoff",.5f}};
    const auto clipped=Pair(lab,frame,"cutout_opacity_discard");
    Check(ownerCount(visible,4)>100&&ownerCount(clipped,4)==0,"Capture cutout fixture did not remove the front receiver");
    replacements->back().parameters={{"baseColor",glm::vec4(1,1,1,0)},{"alphaCutoff",.5f}};
    const auto zeroAlpha=Pair(lab,frame,"cutout_zero_base_alpha");
    Check(ownerCount(zeroAlpha,4)==0,"Capture alpha fixture retained an alpha-zero receiver");
    replacements->back().parameters.clear();replacements->back().unknownOwner=true;
    const auto unknown=Pair(lab,frame,"unknown_owner");Check(ownerCount(unknown,.75f)>100,"Capture unknown-owner fixture was not visible");
    frame.effects.surfaceCacheGi.diagnosticReceiverClipPlane={1,0,0,0};
    const auto plane=Pair(lab,frame,"world_clip_plane");
    Check(Compare(unknown.positions,plane.positions).maximum>.1,"Capture clipping fixture did not change receiver coverage");
    frame.effects.surfaceCacheGi.diagnosticReceiverClipPlane={0,0,0,0};
    replacements->front().omitMrtVariant=true;Pair(lab,frame,"one_material_missing_variant",true);
    std::cout<<"GI90 CAPTURE_COMPARE COMPLETE accepted=1 untimed=1 profile_before=10 profile_after=11 owner_exact=1 depth_exact=1"
        <<" rho_material_response="<<rhoResponse.maximum<<" roughness_material_response="<<roughnessResponse.maximum<<std::endl;
}
}
namespace ReceiverMotionComparisonFixture {
using namespace SurfaceCacheBudgetFixture;
using ReceiverCaptureComparisonFixture::Compare;
struct Sample {
    std::vector<glm::vec4> current,temporal,history;
    std::vector<float> depth;
    SurfaceReceiverMotionReadback motion;
};
void Sequence(MaterialLabModule& lab,FrameSettings frame,const char* label,bool expectedReuse,bool noMotionImage=false,bool coplanar=false,bool requireVisibleMotion=true) {
    constexpr unsigned Frames=7;std::array<Sample,Frames> baseline;
    const auto camera=*frame.camera;std::uint64_t reusedFrames=0,movingPixels=0;
    for(unsigned mode=0;mode<2;++mode) {
        Check(lab.ResetRenderHistory(),lab.LastError());Reset(frame);
        frame.effects.surfaceCacheGi.reuseStaticReceiverMotionCapture=mode!=0;
        frame.effects.surfaceCacheGi.cacheSettledGather=false;
        for(unsigned f=0;f<Frames;++f) {
            const float offset=coplanar?(f<2?-.25f:f==2||f>=5?0.f:.1f):(f<2||f>=5?0.f:f==2?.2f:.4f);
            frame.screenProbeStudyState.occluderOffset=offset;frame.camera=camera;
            if(f==4){auto moved=camera;moved.position.x+=.1f;moved.view=camera.view*glm::translate(glm::mat4(1),glm::vec3(-.1f,0,0));frame.camera=moved;}
            Draw(lab,frame);Sample sample;
            auto current=lab.ReadbackSurfaceReceiverCapture();Check(!current.positions.empty(),lab.LastError());
            sample.current=std::move(current.positions);sample.depth=std::move(current.depth);
            sample.motion=lab.ReadbackSurfaceReceiverMotion();Check(lab.LastError().empty(),lab.LastError());
            sample.temporal=lab.ReadbackSurfaceCacheTemporalGi();sample.history=lab.ReadbackSurfaceCacheTemporalHistory();
            Check(!sample.temporal.empty()&&!sample.history.empty(),lab.LastError());
            const auto& motion=sample.motion;
            Check(motion.previousWorldOwner.empty()==noMotionImage,"Receiver-motion fixture lost its expected image availability");
            if(!mode){Check(!motion.staticCapture,"Old receiver motion unexpectedly used static capture");baseline[f]=std::move(sample);continue;}
            const auto& before=baseline[f];const bool reuse=expectedReuse&&!noMotionImage;
            Check(motion.staticCapture==reuse,"Static receiver-motion optimization or whole-frame fallback was not selected");
            Check(motion.states==before.motion.states,"Receiver-motion classification palette changed");
            const auto currentDelta=Compare(before.current,sample.current),temporalDelta=Compare(before.temporal,sample.temporal),historyDelta=Compare(before.history,sample.history);
            Check(before.depth.size()==sample.depth.size(),"Receiver-motion prepass depth extent differs");
            std::uint64_t depthDifferences=0,ownerDifferences=0,coverageDifferences=0,movingDifferences=0,staticDifferences=0;
            double movingMaximum=0,staticMaximum=0;std::uint64_t visible=0,visibleMoving=0;
            for(std::size_t i=0;i<sample.depth.size();++i)depthDifferences+=std::bit_cast<std::uint32_t>(before.depth[i])!=std::bit_cast<std::uint32_t>(sample.depth[i]);
            Check(before.motion.previousWorldOwner.size()==motion.previousWorldOwner.size(),"Receiver-motion capture image extent differs");
            Check(motion.previousWorldOwner.empty()||motion.previousWorldOwner.size()==sample.current.size(),"Receiver-motion image differs from current receiver extent");
            for(std::size_t i=0;i<motion.previousWorldOwner.size();++i) {
                const auto a=before.motion.previousWorldOwner[i],b=motion.previousWorldOwner[i];
                for(unsigned c=0;c<4;++c)Check(std::isfinite(a[c])&&std::isfinite(b[c]),"Nonfinite receiver-motion capture");
                ownerDifferences+=std::bit_cast<std::uint32_t>(a.w)!=std::bit_cast<std::uint32_t>(b.w);
                coverageDifferences+=(a.w>0)!=(b.w>0);visible+=a.w>0;
                Check(a.w==0||(a.w>=1&&a.w<=64&&a.w==std::floor(a.w)),"Receiver-motion comparison has an invalid owner");
                const bool moving=a.w>=1&&a.w<=64&&before.motion.states[unsigned(a.w)-1]>1.5f;visibleMoving+=moving;
                for(unsigned c=0;c<3;++c)if(std::bit_cast<std::uint32_t>(a[c])!=std::bit_cast<std::uint32_t>(b[c])) {
                    if(moving){++movingDifferences;movingMaximum=std::max(movingMaximum,std::abs(double(a[c])-b[c]));}
                    else {++staticDifferences;staticMaximum=std::max(staticMaximum,std::abs(double(a[c])-b[c]));}
                }
            }
            const unsigned movingOwners=unsigned(std::count_if(motion.states.begin(),motion.states.end(),[](float state){return state>1.5f;}));
            if(reuse) {
                Check(motion.rasterDraws==movingOwners&&before.motion.rasterDraws==motion.rasterDraws+motion.staticOwners,
                    "Static receiver-motion draw subset is not exactly the certified moving owners");
                ++reusedFrames;movingPixels+=visibleMoving;
            } else Check(motion.rasterDraws==before.motion.rasterDraws,"Whole-frame receiver-motion fallback changed its raster draw count");
            std::cout<<"GI90 MOTION_COMPARE case="<<label<<" frame="<<f<<" untimed=1 reuse="<<reuse
                <<" old_raster_draws="<<before.motion.rasterDraws<<" new_raster_draws="<<motion.rasterDraws
                <<" initialization_draws="<<motion.staticCapture<<" static_owners="<<motion.staticOwners<<" moving_owners="<<movingOwners
                <<" current_bitwise="<<(currentDelta.different==0)<<" current_max_abs="<<currentDelta.maximum
                <<" depth_bitwise="<<(depthDifferences==0)<<" owner_equal="<<(ownerDifferences==0)<<" owner_differences="<<ownerDifferences
                <<" coverage_equal="<<(coverageDifferences==0)<<" visible="<<visible<<" moving_pixels="<<visibleMoving
                <<" moving_xyz_bitwise="<<(movingDifferences==0)<<" moving_xyz_max_abs="<<movingMaximum
                <<" static_xyz_differences="<<staticDifferences<<" static_xyz_max_abs="<<staticMaximum<<" static_xyz_consumed=0"
                <<" temporal_bitwise="<<(temporalDelta.different==0)<<" temporal_max_abs="<<temporalDelta.maximum
                <<" history_bitwise="<<(historyDelta.different==0)<<" history_max_abs="<<historyDelta.maximum<<std::endl;
            Check(currentDelta.different==0&&depthDifferences==0&&ownerDifferences==0&&coverageDifferences==0&&
                movingMaximum<=2.e-5&&temporalDelta.different==0&&historyDelta.different==0,
                "Static receiver-motion capture changed visible ownership, current depth or temporal output");
            Check(noMotionImage||visible>100,"Receiver-motion comparison has no visible certified receivers");
        }
    }
    Check(!expectedReuse||reusedFrames==Frames,"Receiver-motion comparison omitted optimized frames");
    if(expectedReuse&&!coplanar&&requireVisibleMotion)Check(movingPixels>100,"Receiver-motion fixture did not exercise a visible moving receiver");
}
void Run(MaterialLabModule& lab,const Options& options) {
    // Both modes start from the same world-transport phase and screen history.
    // Only the motion-capture optimization changes; the accepted profile11
    // estimator and its actual current rays remain enabled for temporal parity.
    auto base=Make(options,11,true);base.present=options.benchmarkPresent;
    Sequence(lab,base,"sealed_motion_camera_stop",true);
    auto frame=base;frame.effects.surfaceCacheGi.diagnosticReceiverClipPlane={0,1,0,-.2f};
    Sequence(lab,frame,"current_world_clip",true);
    auto overrides=std::make_shared<std::vector<ReceiverCaptureMaterialOverride>>();overrides->push_back({19,0,{}});
    frame=base;frame.receiverCaptureMaterialOverrides=overrides;
    overrides->front().parameters={{"opacity",0.f},{"alphaCutoff",.5f}};
    Sequence(lab,frame,"opaque_alpha_discard",true,false,false,false);
    overrides->front().parameters.clear();overrides->front().unknownOwner=true;
    Sequence(lab,frame,"unknown_owner_fallback",false);
    overrides->front().unknownOwner=false;overrides->front().ownerOverride=18;
    Sequence(lab,frame,"mismatched_owner_fallback",false);
    overrides->front().ownerOverride=-2;overrides->front().omitMotionVariant=true;
    Sequence(lab,frame,"missing_mode15_variant_fallback",false);
    overrides->front().omitMotionVariant=false;overrides->front().disableStaticWorldCapture=true;
    Sequence(lab,frame,"unproven_vertex_contract_fallback",false);
    overrides->front().disableStaticWorldCapture=false;overrides->front().cutout=true;
    Sequence(lab,frame,"cutout_old_certification",false,true);
    frame=base;frame.sealedGiRoomStudy=false;
    auto objects=std::make_shared<std::vector<ProbePlacementObject>>(std::initializer_list<ProbePlacementObject>{
        {"motion floor",ShowcaseMesh::Plane,ProbePlacementMaterial::Floor,{0,0,0},{4,1,4}},
        {"moving equal-depth box",ShowcaseMesh::Box,ProbePlacementMaterial::Block,{0,.8f,0},{.6f,.8f,.45f},0,0,{1,0,0},true},
        {"later static equal-depth box",ShowcaseMesh::Box,ProbePlacementMaterial::Block,{0,.8f,0},{.6f,.8f,.45f}}});
    frame.probeSceneOverride=objects;frame.probeSceneMaterialsOverride=std::make_shared<std::vector<SealedGiRoomMaterial>>(objects->size());
    auto camera=*base.camera;camera.position={0,2.4f,6.8f};camera.view=glm::lookAt(camera.position,glm::vec3(0,.7f,0),glm::vec3(0,1,0));frame.camera=camera;
    Sequence(lab,frame,"equal_depth_moving_before_static",true,false,true);
    std::cout<<"GI90 MOTION_COMPARE COMPLETE accepted=1 untimed=1 cases=9 owner_exact=1 depth_exact=1 temporal_bitwise=1"<<std::endl;
}
}
// The CLI/profile switch is owned by the root task; this standalone entry
// keeps the fixture independent of performance and native-probe audits.
void SurfaceCacheReceiverMotionComparison(MaterialLabModule& lab,const Options& options) {ReceiverMotionComparisonFixture::Run(lab,options);}
void SurfaceCacheBudgetStudy(MaterialLabModule& lab,const Options& options) {
    using namespace SurfaceCacheBudgetFixture;
    if(options.gi90UeSmoke){
        auto frame=Make(options,21,false);frame.present=options.benchmarkPresent;Reset(frame);
        const auto base=*frame.camera;const auto light=frame.effects.areaLights.lights[0].radiance;
        for(unsigned f=0;f<4;++f){
            Pose(frame,base,1,f,light);
            frame.effects.screenProbePlacement.trace.sdf.collectRayDiagnostics=f==3;
            Draw(lab,frame);
            Check(lab.PipelineStatistics().surfaceCacheUeLumenHigh,"UE smoke did not execute the UE branch");
        }
        const auto rc=lab.ReadbackUeLumenRadianceCache();
        Check(rc.available&&rc.diagnostics,"UE smoke lacks the actual world-cache diagnostic buffer");
        std::cout<<"GI90 UE_SMOKE COMPLETE frames=4 shader_paths=normal_and_diagnostic world_queries="<<rc.control[13]
            <<" trace_init="<<rc.control[12]<<" adaptive_accepted="<<rc.control[53]<<" capacity="<<rc.control[55]
            <<" full_quality_acceptance=not_evaluated"<<std::endl;
        return;
    }
    if(options.gi90CaptureCompare){ReceiverCaptureComparisonFixture::Run(lab,options);return;}
    if(options.gi90Performance){Performance(lab,options);return;}
    if(options.gi90Quality){SurfaceCacheBudgetFixture::Quality(lab,options);return;}
    bool auditAccepted=true;
    for(unsigned profile:{0u,options.gi90Profile})for(unsigned path=0;path<5;++path) {
        if(options.surfaceCacheReceiverDiagnosticPath<5&&path!=options.surfaceCacheReceiverDiagnosticPath)continue;
        auto frame=Make(options,profile,false);frame.present=false;const auto base=*frame.camera;
        const auto light=frame.effects.areaLights.lights[0].radiance;Reset(frame);
        for(unsigned f=0;f<64;++f)Draw(lab,frame);
        for(unsigned f=0;f<16;++f){Pose(frame,base,path,f,light);Draw(lab,frame);}
        Pose(frame,base,path,16,light);
        Audit(lab,frame,profile,path);
        if(profile>=4)auditAccepted&=ValidateHits(lab,frame,profile,0,0);
    }
    std::cout<<"GI90 AUDIT COMPLETE accepted="<<auditAccepted<<std::endl;
    Check(auditAccepted,"GI90 independent geometry audit failed; candidate remains experimental");
}
