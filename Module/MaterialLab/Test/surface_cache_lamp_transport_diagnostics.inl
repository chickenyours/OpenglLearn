// Read-only, probe-major transport diagnostics for the recessed lamp fixture.
// The region masks use actual world positions/normals, never irradiance or a
// screenshot's brightness. Known-black rays remain in all sample denominators.
void SurfaceCacheLampTransportDiagnostics(MaterialLabModule& lab,const std::string& tag) {
    const auto placed=lab.ReadbackScreenProbePlacement();
    const auto light=lab.ReadbackSurfaceCacheProbeIrradiance();
    const auto origins=lab.PipelineStatistics().surfaceCacheProbeOriginsCollected?
        lab.ReadbackSurfaceCacheProbeOrigins():std::vector<glm::vec4>{};
    const auto rays=lab.ReadbackScreenProbeTrace(false);
    const auto scene=lab.PreparedProbeSdfScene();
    const auto cache=lab.PreparedSurfaceCache();
    const auto slots=placed.uniform.size()+placed.spawned.size();
    const auto directions=std::size_t(rays.rayResolution)*rays.rayResolution;
    Check(placed.uniformNormals.size()==placed.uniform.size()&&placed.spawnedNormals.size()==placed.spawned.size()&&
        light.size()>=slots&&directions>0&&rays.probeSlots==slots&&rays.radianceStatus.size()==slots*directions&&
        rays.hitWorldDistance.size()==slots*directions&&rays.hitPixelStepsMip.size()==slots*directions&&
        rays.rayDirectionDepthFetches.size()==slots*directions,"Lamp transport diagnostic atlas extent mismatch");
    const bool originsPresent=origins.size()>=slots;
    const auto geometry=scene?scene->PackedGeometry():nullptr;
    const bool ownersPresent=geometry&&cache&&geometry->TriangleCount()==cache->TriangleCount();
    const auto readable=[](int status){return status==1||status==5||status==9||status==10||status==13;};
    const auto luminance=[](glm::vec3 rgb){return .2126*double(rgb.x)+.7152*double(rgb.y)+.0722*double(rgb.z);};
    struct Moments {
        std::uint64_t count=0;double sum=0,squared=0,minimum=1e30,maximum=-1e30;
        void Add(double value){++count;sum+=value;squared+=value*value;minimum=std::min(minimum,value);maximum=std::max(maximum,value);}
        double Mean() const{return count?sum/double(count):0.;}
        double Std() const{return count?std::sqrt(std::max(0.,squared/double(count)-Mean()*Mean())):0.;}
    };
    struct Probe {
        std::size_t index=0;glm::vec3 position{};double energy=0,alpha=0;int origin=0;
        bool originAvailable=false,adaptive=false;
        std::uint64_t read=0,black=0,short10=0,short30=0,trim=0,trim10=0,trim30=0,backface=0,unknown=0,unmappedScreen=0;
        std::array<std::uint64_t,18> statuses{};
    };
    struct Region {
        const char* name="";std::uint64_t probes=0,adaptive=0,invalid=0,unknownOrigin=0,read=0,black=0,short10=0,short30=0;
        std::uint64_t trim=0,trim10=0,trim30=0,backface=0,unknown=0,unmappedScreen=0;
        Moments energy,validEnergy,alpha;
        std::array<std::uint64_t,18> statuses{};std::array<std::uint64_t,9> originStatuses{};
        void Add(const Probe& p) {
            ++probes;adaptive+=p.adaptive;invalid+=p.originAvailable&&p.origin<=0;unknownOrigin+=!p.originAvailable;
            energy.Add(p.energy);alpha.Add(p.alpha);if(p.originAvailable&&p.origin>0)validEnergy.Add(p.energy);
            if(p.originAvailable){const int bin=p.origin>0?p.origin-1:(p.origin<0?3+(-p.origin-1):8);if(bin>=0&&bin<8)++originStatuses[std::size_t(bin)];else ++originStatuses[8];}
            read+=p.read;black+=p.black;short10+=p.short10;short30+=p.short30;trim+=p.trim;trim10+=p.trim10;trim30+=p.trim30;
            backface+=p.backface;unknown+=p.unknown;unmappedScreen+=p.unmappedScreen;
            for(std::size_t i=0;i<statuses.size();++i)statuses[i]+=p.statuses[i];
        }
    };
    std::array<Region,3> regions{};
    regions[0].name="ceiling_band_0_to_35cm";
    regions[1].name="trim_underside";
    regions[2].name="ceiling_interior_gt70cm";
    std::vector<Probe> ceilingBand;
    for(std::size_t index=0;index<slots;++index) {
        const bool coarse=index<placed.uniform.size();const auto local=coarse?index:index-placed.uniform.size();
        const auto pp=coarse?placed.uniform[local]:placed.spawned[local];
        const auto nd=coarse?placed.uniformNormals[local]:placed.spawnedNormals[local];
        if(pp.w<.5f||nd.w<=0||glm::dot(glm::vec3(nd),glm::vec3(nd))<.5f)continue;
        const auto n=glm::normalize(glm::vec3(nd));if(n.y>=-.99f)continue;
        const float outside=std::max(std::abs(pp.x),std::abs(pp.z))-1.10f;
        int region=-1;
        if(std::abs(pp.y-4.5f)<.002f&&outside>=0&&outside<=.35f)region=0;
        else if(std::abs(pp.y-4.47f)<.002f&&std::abs(pp.x)<=1.101f&&std::abs(pp.z)<=1.101f)region=1;
        else if(std::abs(pp.y-4.5f)<.002f&&outside>.7f)region=2;
        if(region<0)continue;
        Probe p;p.index=index;p.position=glm::vec3(pp);p.adaptive=!coarse;
        p.originAvailable=originsPresent;p.origin=originsPresent?int(std::lround(origins[index].w)):0;
        p.energy=luminance(glm::vec3(light[index]));p.alpha=light[index].w;
        Check(std::isfinite(p.energy)&&std::isfinite(p.alpha),"Nonfinite lamp probe integral");
        for(std::size_t ray=0;ray<directions;++ray) {
            const auto at=index*directions+ray;const auto value=rays.radianceStatus[at];
            Check(std::isfinite(value.w),"Nonfinite lamp ray status");
            const int status=int(std::lround(value.w));if(status>=0&&status<18)++p.statuses[std::size_t(status)];
            const bool isReadable=readable(status);p.read+=isReadable;
            p.black+=isReadable&&glm::dot(glm::vec3(value),glm::vec3(value))<=1e-18f;
            p.unknown+=status==11||status==12||status==17;
            const auto distance=rays.hitWorldDistance[at].w;
            // Empty/exhausted/launch-rejected rays do not carry a certified hit.
            const bool hit=status==1||status==9||status==10||status==11||status==12||status==17;
            if(hit&&distance>0){p.short10+=distance<.1f;p.short30+=distance<.3f;}
            // Status 1/17 retains screen pixel/step/mip metadata. Its W must
            // never be interpreted as an owner-map triangle index. Only SDF
            // statuses 9..12 carry metadata=(-1,-1,steps,packedTriangle).
            if(status==1||status==17){++p.unmappedScreen;continue;}
            if(!ownersPresent||status<9||status>12)continue;
            const auto packedTriangle=rays.hitPixelStepsMip[at].w;
            if(!std::isfinite(packedTriangle))continue;
            const int triangle=int(std::lround(packedTriangle));
            if(triangle<0||std::uint32_t(triangle)>=geometry->TriangleCount())continue;
            const auto ownerOffset=std::size_t(cache->TriangleOwnerOffset())+std::size_t(triangle)/4;
            if(ownerOffset>=cache->Metadata().size())continue;
            const int owner=int(std::lround(cache->Metadata()[ownerOffset][triangle%4]));
            if(owner>=14&&owner<=17){++p.trim;if(distance>0){p.trim10+=distance<.1f;p.trim30+=distance<.3f;}}
            const auto triangleOffset=std::size_t(geometry->NodeCount())*2+std::size_t(triangle)*5;
            if(triangleOffset+2>=geometry->Pixels().size())continue;
            const auto a=glm::vec3(geometry->Pixels()[triangleOffset]);
            const auto ab=glm::vec3(geometry->Pixels()[triangleOffset+1])-a,ac=glm::vec3(geometry->Pixels()[triangleOffset+2])-a;
            const auto hitNormal=glm::cross(ab,ac),direction=glm::vec3(rays.rayDirectionDepthFetches[at]);
            if(glm::dot(hitNormal,hitNormal)>1e-16f&&glm::dot(direction,direction)>.5f)
                p.backface+=glm::dot(hitNormal,-direction)<=0;
        }
        regions[std::size_t(region)].Add(p);if(region==0)ceilingBand.push_back(p);
    }
    for(const auto& region:regions) {
        const auto samples=region.probes*directions;
        const auto fraction=[&](std::uint64_t count){return samples?double(count)/double(samples):0.;};
        std::cout<<"LAMP TRANSPORT tag="<<tag<<" region="<<region.name<<" probes="<<region.probes<<" adaptive="<<region.adaptive
            <<" rays_per_probe="<<directions<<" origin_invalid="<<region.invalid<<" origin_unavailable="<<region.unknownOrigin
            <<" E_mean="<<region.energy.Mean()<<" E_std="<<region.energy.Std()<<" E_min="<<(region.energy.count?region.energy.minimum:0)
            <<" E_max="<<(region.energy.count?region.energy.maximum:0)<<" valid_E_mean="<<region.validEnergy.Mean()<<" valid_E_std="<<region.validEnergy.Std()
            <<" alpha_mean="<<region.alpha.Mean()<<" readable="<<fraction(region.read)<<" black="<<fraction(region.black)
            <<" hit_lt10cm="<<fraction(region.short10)<<" hit_lt30cm="<<fraction(region.short30)
            <<" trim_hit="<<fraction(region.trim)<<" trim_lt10cm="<<fraction(region.trim10)<<" trim_lt30cm="<<fraction(region.trim30)
            <<" backface="<<fraction(region.backface)<<" unknown_11_12_17="<<fraction(region.unknown)
            <<" owner_unmapped_screen="<<region.unmappedScreen<<" owner_map_available="<<ownersPresent<<" status_hist=";
        for(std::size_t i=0;i<region.statuses.size();++i)if(region.statuses[i])std::cout<<i<<":"<<region.statuses[i]<<",";
        std::cout<<" origin_hist_1_2_3_minus1_to_minus5_other=";
        for(const auto count:region.originStatuses)std::cout<<count<<",";
        std::cout<<std::endl;
    }
    std::sort(ceilingBand.begin(),ceilingBand.end(),[](const Probe& a,const Probe& b){return a.energy<b.energy||(a.energy==b.energy&&a.index<b.index);});
    for(std::size_t rank=0;rank<std::min<std::size_t>(12,ceilingBand.size());++rank) {
        const auto& p=ceilingBand[rank];
        std::cout<<"LAMP TRANSPORT LOW tag="<<tag<<" rank="<<rank<<" probe="<<p.index<<" adaptive="<<p.adaptive
            <<" p="<<p.position.x<<","<<p.position.y<<","<<p.position.z<<" E="<<p.energy<<" alpha="<<p.alpha
            <<" origin="<<p.origin<<" origin_available="<<p.originAvailable<<" readable="<<p.read<<" black="<<p.black
            <<" short10="<<p.short10<<" short30="<<p.short30<<" trim="<<p.trim<<" trimshort10="<<p.trim10
            <<" trimshort30="<<p.trim30<<" backface="<<p.backface<<" unknown="<<p.unknown<<std::endl;
    }
}
