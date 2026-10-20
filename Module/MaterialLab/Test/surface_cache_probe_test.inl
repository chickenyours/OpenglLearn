// Tests the emitted GPU directions and physical linearity, rather than
// recreating the shader's random number generator or interpolation formula.
void SurfaceCacheProbeQuality(MaterialLabModule& lab,const Options& options) {
    auto fixtureOptions=options;fixtureOptions.sdfBanquet=false;fixtureOptions.sdfClipmaps=false;
    fixtureOptions.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixtureOptions);
    frame.width=320;frame.height=240;frame.effects.outputSize={0,0};
    frame.present=false;frame.screenProbeStudyHud=false;
    frame.camera=ProbePlacementCameraPreset(0,frame.OutputAspect());
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;
    gi.contact.enabled=false; // This fixture validates the independent probe integration stage.
    gi.temporal.enabled=false;
    gi.spatial.enabled=false;
    auto& trace=placement.trace;
    gi.useScreenProbes=true;gi.probeDiagnostics=false;gi.view=2;
    placement.enabled=true;placement.adaptive=true;placement.baseSpacing=32;
    placement.candidateCount=8;placement.frameIndex=0;placement.freeze=false;placement.reset=true;
    trace.enabled=true;trace.showPanel=false;trace.cosineHemisphere=true;
    trace.rayResolution=8;trace.samplingSeed=0;trace.maxSteps=512;
    trace.sdf.maxSteps=256;trace.maxDistance=gi.maxDistance;
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;placement.reset=false;};
    const auto finite=[&](const auto& data,const char* label){
        Check(!data.empty(),std::string("Missing ")+label+": "+lab.LastError());
        for(const auto& p:data)for(unsigned c=0;c<4;++c)Check(std::isfinite(p[c]),std::string("Nonfinite ")+label);
    };
    const auto energy=[](const auto& data){double sum=0;for(const auto& p:data)sum+=p.x+p.y+p.z;return sum;};
    const auto difference=[](const auto& a,const auto& b){
        Check(a.size()==b.size(),"Probe GI extent changed unexpectedly");double sum=0;
        for(std::size_t i=0;i<a.size();++i)sum+=glm::length(glm::dvec3(a[i])-glm::dvec3(b[i]));return sum;
    };
    const auto save=[&](const char* label){if(options.screenshot.empty())return;
        auto path=std::filesystem::path(options.screenshot);
        path.replace_filename(path.stem().string()+"_"+label+path.extension().string());Save(lab.Readback(),path.string());};
    render();const auto cache=lab.PreparedSurfaceCache();Check(bool(cache),"Probe GI did not prepare its Surface Cache");
    const auto generation=cache->Generation();
    const auto probes=lab.ReadbackScreenProbePlacement();
    const auto rays=lab.ReadbackScreenProbeTrace();
    const auto irradiance=lab.ReadbackSurfaceCacheProbeIrradiance();finite(irradiance,"probe irradiance");
    const auto lit=lab.ReadbackSurfaceCacheRawGi();finite(lit,"reconstructed probe GI");
    const auto slots=probes.uniform.size()+probes.spawned.size();
    const auto raysPerProbe=std::size_t(trace.rayResolution)*trace.rayResolution;
    Check(rays.probeSlots==slots&&rays.radianceStatus.size()==slots*raysPerProbe&&irradiance.size()>=slots,
        "Probe gather lost placement/ray/integration atlas slots");
    const auto statistics=lab.PipelineStatistics();
    Check(statistics.surfaceCacheProbePasses>0&&statistics.surfaceCacheProbeSlots==slots&&
        statistics.surfaceCacheRays==slots*raysPerProbe&&statistics.surfaceCacheProbeRayBudget==slots*raysPerProbe,
        "GI rays were not scheduled in probe space");
    Check(statistics.surfaceCacheRays<std::uint64_t(frame.width)*frame.height,
        "Probe fixture did not reduce ray capacity below one reference ray per pixel");
    Check(statistics.temporalPasses==0&&!statistics.historyUsed&&!statistics.lumenHistoryUsed,
        "Surface probe gather unexpectedly used temporal history");
    unsigned active=0,adaptive=0,knownBlack=0,unreadable=0;std::uint64_t directionCount=0;
    std::array<unsigned,18> rayStatuses{};
    double cosineMean=0,cosineSquared=0,cone=.0,fourthMoment=0;glm::dvec2 tangentMean(0);
    double integrationError=0;float minimumCosine=1,maximumCosine=0;
    for(std::size_t probe=0;probe<slots;++probe) {
        const bool uniform=probe<probes.uniform.size();const auto slot=uniform?probe:probe-probes.uniform.size();
        const auto pp=uniform?probes.uniform[slot]:probes.spawned[slot];
        const auto nd=uniform?probes.uniformNormals[slot]:probes.spawnedNormals[slot];
        if(pp.w<.5f){Check(glm::length(irradiance[probe])<1e-7f,"Inactive probe contributed lighting");continue;}
        ++active;if(!uniform)++adaptive;
        const auto n=glm::normalize(glm::dvec3(nd));const auto axis=std::abs(n.z)<.99?glm::dvec3(0,0,1):glm::dvec3(0,1,0);
        const auto tangent=glm::normalize(glm::cross(axis,n)),bitangent=glm::cross(n,tangent);
        glm::dvec3 expected(0);unsigned readable=0;
        for(std::size_t ray=0;ray<raysPerProbe;++ray) {
            const auto index=probe*raysPerProbe+ray;const auto direction=glm::dvec3(rays.rayDirectionDepthFetches[index]);
            Check(std::abs(glm::length(direction)-1)<2e-4,"Probe emitted a nonunit direction");
            const double cosine=glm::dot(direction,n);
            Check(cosine>0&&cosine<=1.0001,"Probe emitted a ray outside the receiver hemisphere");
            minimumCosine=std::min(minimumCosine,float(cosine));maximumCosine=std::max(maximumCosine,float(cosine));
            cosineMean+=cosine;cosineSquared+=cosine*cosine;fourthMoment+=cosine*cosine*cosine*cosine;
            cone+=cosine>.8?1:0;tangentMean+=glm::dvec2(glm::dot(direction,tangent),glm::dot(direction,bitangent));++directionCount;
            const auto sample=rays.radianceStatus[index];const auto status=unsigned(std::round(sample.w));
            Check(status<rayStatuses.size(),"Probe GI emitted an unclassified ray status");++rayStatuses[status];
            if(status==1||status==9||status==10){expected+=glm::max(glm::dvec3(sample),glm::dvec3(0));++readable;
                if(glm::length(glm::vec3(sample))<1e-7f)++knownBlack;}
            else if(status==5||status==13){++readable;++knownBlack;}
            else ++unreadable;
        }
        expected/=double(raysPerProbe);
        integrationError=std::max(integrationError,glm::length(glm::dvec3(irradiance[probe])-expected));
        Check(std::abs(irradiance[probe].w-float(readable)/float(raysPerProbe))<2e-5f,
            "Probe visibility weight excludes known black or hides missing coverage");
    }
    Check(active>=30&&adaptive>0&&directionCount>2000,"GI fixture did not exercise active adaptive probes");
    cosineMean/=directionCount;cosineSquared/=directionCount;fourthMoment/=directionCount;cone/=directionCount;tangentMean/=double(directionCount);
    // Independent cosine-PDF integrals: E[cos]=2/3, E[cos^2]=1/2,
    // E[cos^4]=1/3 and P(cos>.8)=1-.8^2. A fixed 45-degree cone fails these.
    Check(minimumCosine<.25f&&maximumCosine>.97f&&std::abs(cosineMean-2./3)<.015&&
        std::abs(cosineSquared-.5)<.015&&std::abs(fourthMoment-1./3)<.02&&std::abs(cone-.36)<.025&&glm::length(tangentMean)<.02,
        "Probe ray distribution is biased or retains a fixed-angle cone/band");
    std::cout<<"Surface Cache probe integration active="<<active<<" adaptive="<<adaptive<<" rays="<<directionCount
        <<" integration_error="<<integrationError<<" known_black="<<knownBlack<<" unavailable="<<unreadable
        <<" probe_energy="<<energy(irradiance)<<" raw_energy="<<energy(lit)<<" statuses=";
    for(unsigned status=0;status<rayStatuses.size();++status)std::cout<<status<<':'<<rayStatuses[status]<<' ';
    std::cout<<std::endl;
    save("adaptive");
    Check(integrationError<2e-5,"Probe E/pi differs from the full-count cosine-PDF radiance mean");
    Check(knownBlack>0,"Probe fixture lacks readable black samples needed to test its integration denominator");
    Check(energy(lit)>1e-3,"Probe reconstruction contains no indirect-light energy");

    // Hold cached incident radiance fixed and vary only the visible receiver
    // reflectance: pixels must apply their own rho once, while probe E/pi
    // stays unchanged. Object identity is explicit; gallery material indices
    // do not enumerate this dedicated scene's private material handles.
    Check(lab.SetSurfaceCacheReceiverAlbedoForTest(0,glm::vec3(.325f)),lab.LastError());
    render();const auto halfFloor=lab.ReadbackSurfaceCacheRawGi();finite(halfFloor,"receiver reflectance GI");
    Check(difference(irradiance,lab.ReadbackSurfaceCacheProbeIrradiance())<1e-5,
        "Receiver albedo contaminated the stored probe incident-light integral");
    unsigned floorPixels=0;double receiverError=0,floorEnergy=0,halfFloorEnergy=0,minimumRatio=1,maximumRatio=0;
    const auto inverseView=glm::inverse(frame.camera->view);
    Check(rays.sourceViewPositions.size()==lit.size()&&rays.sourceWorldNormals.size()==lit.size(),"Missing probe receiver GBuffer");
    for(std::size_t pixel=0;pixel<lit.size();++pixel) {
        const auto p=rays.sourceViewPositions[pixel],normal=rays.sourceWorldNormals[pixel];
        if(p.w<.5f||normal.y<.999f)continue;
        const auto world=glm::vec3(inverseView*glm::vec4(glm::vec3(p),1));if(std::abs(world.y)>.002f)continue;
        ++floorPixels;floorEnergy+=lit[pixel].x+lit[pixel].y+lit[pixel].z;
        halfFloorEnergy+=halfFloor[pixel].x+halfFloor[pixel].y+halfFloor[pixel].z;
        if(lit[pixel].x>1e-6f){const double ratio=double(halfFloor[pixel].x)/lit[pixel].x;minimumRatio=std::min(minimumRatio,ratio);maximumRatio=std::max(maximumRatio,ratio);}
        receiverError+=glm::length(glm::dvec3(halfFloor[pixel])-.5*glm::dvec3(lit[pixel]));
    }
    std::cout<<"Surface Cache receiver albedo floor_pixels="<<floorPixels<<" floor_energy="<<floorEnergy
        <<" half_floor_energy="<<halfFloorEnergy<<" expected_half_error="<<receiverError
        <<" min_ratio="<<minimumRatio<<" max_ratio="<<maximumRatio<<std::endl;
    Check(floorPixels>100&&floorEnergy>1e-4&&receiverError<floorEnergy*.002+1e-5,
        "Final pixels did not apply their own receiver albedo exactly once");
    Check(lab.SetSurfaceCacheReceiverAlbedoForTest(0,glm::vec3(.65f)),lab.LastError());render();

    // Adaptive candidates must participate in the final image, rather than
    // remaining a diagnostic overlay while pixels use the old reference path.
    placement.adaptive=false;render();const auto uniformLit=lab.ReadbackSurfaceCacheRawGi();finite(uniformLit,"uniform probe GI");
    Check(difference(lit,uniformLit)>1e-4,"Adaptive surface probes never influenced final GI");
    placement.adaptive=true;render();const auto restored=lab.ReadbackSurfaceCacheRawGi();
    Check(difference(lit,restored)<1e-5,"Adaptive toggle reuses stale filtered output");

    frame.effects.shadows.sunIntensity=0;render();const auto dark=lab.ReadbackSurfaceCacheRawGi();finite(dark,"unlit probe GI");
    Check(std::abs(energy(dark))<1e-5,"Unlit probes contain fabricated ambient light");
    frame.effects.shadows.sunIntensity=6;render();const auto twice=lab.ReadbackSurfaceCacheRawGi();finite(twice,"scaled probe GI");
    double scaleError=0;for(std::size_t i=0;i<lit.size();++i)for(unsigned c=0;c<3;++c)scaleError+=std::abs(double(twice[i][c])-2.*lit[i][c]);
    Check(scaleError<energy(lit)*.003+1e-4,"Probe reconstruction is not linear in source light intensity");
    frame.effects.shadows.sunIntensity=3;frame.camera=ProbePlacementCameraPreset(1,frame.OutputAspect());render();
    Check(lab.PreparedSurfaceCache()->Generation()==generation,"Camera movement invalidated the card captures");
    frame.camera=ProbePlacementCameraPreset(0,frame.OutputAspect());render();
    Check(difference(lit,lab.ReadbackSurfaceCacheRawGi())<1e-5,"Fixed-phase probe GI depends on camera/light frame history");

    // Frozen tile coordinates belong to their placement camera. GI must
    // refresh those probes after camera movement instead of reprojecting an
    // unrelated frozen screen tile; returning to A remains deterministic.
    placement.freeze=true;frame.camera=ProbePlacementCameraPreset(1,frame.OutputAspect());render();
    Check(!lab.PipelineStatistics().screenProbePlacementFrozen,"GI reused frozen probe tile indices after camera movement");
    frame.camera=ProbePlacementCameraPreset(0,frame.OutputAspect());render();
    Check(difference(lit,lab.ReadbackSurfaceCacheRawGi())<1e-5,"Frozen-camera A-B-A probe GI does not restore the original field");
    placement.freeze=false;

    trace.samplingSeed=1;render();const auto secondRays=lab.ReadbackScreenProbeTrace(false);
    unsigned changedDirections=0;for(std::size_t i=0;i<rays.radianceStatus.size();++i)
        if(glm::length(glm::vec3(secondRays.rayDirectionDepthFetches[i])-glm::vec3(rays.rayDirectionDepthFetches[i]))>1e-4f)++changedDirections;
    Check(changedDirections>directionCount/2,"New probe seed failed to vary both angular sample dimensions");
    trace.samplingSeed=0;render();Check(difference(lit,lab.ReadbackSurfaceCacheRawGi())<1e-5,"Explicit sampling seed reuses temporal GI");
    placement.baseSpacing=64;placement.reset=true;render();const auto coarse=lab.PipelineStatistics();
    Check(coarse.surfaceCacheProbeSlots<statistics.surfaceCacheProbeSlots&&coarse.surfaceCacheRays<statistics.surfaceCacheRays,
        "Probe ray allocation does not follow the configured surface probe spacing");
    placement.baseSpacing=32;placement.reset=true;
    for(unsigned resolution:{4u,16u}) {trace.rayResolution=resolution;render();finite(lab.ReadbackSurfaceCacheRawGi(),"changed ray count");
        Check(lab.PipelineStatistics().surfaceCacheRays==slots*resolution*resolution,"Probe angular resolution changed the wrong ray budget");}
    trace.rayResolution=8;gi.view=1;render();
    Check(lab.PipelineStatistics().surfaceCacheRays==0,"Cache-only view unnecessarily traced GI probe rays");
    gi.view=2;render();save("indirect");

    // Quantify the broad patches after reconstruction, not just the emitted
    // direction distribution. The independent seed set is averaged here on
    // the CPU solely to form an offline reference; the renderer never retains
    // or blends these frames. Subtracting that reference preserves real
    // geometric brightness gradients instead of calling them blotches.
    const auto planarSurface=[](glm::vec3 p,glm::vec3 n) {
        if(std::abs(p.y)<.002f&&n.y>.999f&&std::abs(p.x)<4.7f&&p.z> -3.2f&&p.z<4.5f)return 0;
        if(std::abs(p.y-5)<.002f&&n.y<-.999f&&std::abs(p.x)<4.7f&&p.z> -3.2f&&p.z<4.5f)return 1;
        if(std::abs(p.z+3.5f)<.002f&&n.z>.999f&&std::abs(p.x)<4.7f&&p.y>.3f&&p.y<4.7f)return 2;
        if(std::abs(p.x+5)<.002f&&n.x>.999f&&p.y>.3f&&p.y<4.7f&&p.z> -3.2f&&p.z<4.5f)return 3;
        if(std::abs(p.x-5)<.002f&&n.x<-.999f&&p.y>.3f&&p.y<4.7f&&p.z> -3.2f&&p.z<4.5f)return 4;
        return -1;
    };
    const unsigned patchWidth=(frame.width+15)/16,patchHeight=(frame.height+15)/16;
    const auto patchSlots=std::size_t(patchWidth)*patchHeight*5;
    std::vector<int> pixelPatch(lit.size(),-1);std::vector<unsigned> patchCounts(patchSlots,0);
    std::vector<bool> planarProbes(slots,false);unsigned planarProbeCount=0,planarPixelCount=0,patchCount=0;
    for(std::size_t pixel=0;pixel<lit.size();++pixel) {
        const auto p=rays.sourceViewPositions[pixel],n=rays.sourceWorldNormals[pixel];if(p.w<.5f||n.w<.5f)continue;
        const auto world=glm::vec3(inverseView*glm::vec4(glm::vec3(p),1));const int surface=planarSurface(world,glm::vec3(n));
        if(surface<0)continue;
        const auto x=unsigned(pixel%frame.width),y=unsigned(pixel/frame.width);
        const auto patch=(std::size_t(surface)*patchHeight+y/16)*patchWidth+x/16;
        pixelPatch[pixel]=int(patch);++patchCounts[patch];++planarPixelCount;
    }
    for(const auto count:patchCounts)if(count>=16)++patchCount;
    for(std::size_t probe=0;probe<slots;++probe) {
        const bool uniform=probe<probes.uniform.size();const auto slot=uniform?probe:probe-probes.uniform.size();
        const auto p=uniform?probes.uniform[slot]:probes.spawned[slot];
        const auto n=uniform?probes.uniformNormals[slot]:probes.spawnedNormals[slot];
        if(p.w>.5f&&planarSurface(glm::vec3(p),glm::vec3(n))>=0){planarProbes[probe]=true;++planarProbeCount;}
    }
    Check(planarProbeCount>=8&&planarPixelCount>=1000&&patchCount>=5,"Convergence fixture lacks planar probe / pixel coverage");
    std::vector<glm::dvec3> referenceProbe(irradiance.size(),glm::dvec3(0)),referencePixel(lit.size(),glm::dvec3(0));
    trace.rayResolution=16;
    for(unsigned seed=100;seed<108;++seed) {
        trace.samplingSeed=seed;render();const auto pi=lab.ReadbackSurfaceCacheProbeIrradiance(),image=lab.ReadbackSurfaceCacheRawGi();
        finite(pi,"convergence reference irradiance");finite(image,"convergence reference image");
        Check(pi.size()==referenceProbe.size()&&image.size()==referencePixel.size(),"Convergence reference changed atlas extents");
        for(std::size_t i=0;i<pi.size();++i)referenceProbe[i]+=glm::dvec3(pi[i])/8.;
        for(std::size_t i=0;i<image.size();++i)referencePixel[i]+=glm::dvec3(image[i])/8.;
    }
    std::array<double,3> probeMse{},pixelMse{},patchMse{};
    const glm::dvec3 luminance(.2126,.7152,.0722);
    for(unsigned quality=0;quality<3;++quality) {
        trace.rayResolution=4u<<quality;
        for(unsigned seed=0;seed<4;++seed) {
            trace.samplingSeed=seed;render();const auto pi=lab.ReadbackSurfaceCacheProbeIrradiance(),image=lab.ReadbackSurfaceCacheRawGi();
            finite(pi,"convergence sample irradiance");finite(image,"convergence sample image");
            std::vector<double> patchError(patchSlots,0);
            for(std::size_t probe=0;probe<slots;++probe)if(planarProbes[probe]) {
                const auto delta=glm::dvec3(pi[probe])-referenceProbe[probe];probeMse[quality]+=glm::dot(delta,delta)/(3.*planarProbeCount*4.);
            }
            for(std::size_t pixel=0;pixel<image.size();++pixel)if(pixelPatch[pixel]>=0) {
                const double error=glm::dot(glm::dvec3(image[pixel])-referencePixel[pixel],luminance);
                pixelMse[quality]+=error*error/(double(planarPixelCount)*4.);patchError[pixelPatch[pixel]]+=error;
            }
            for(std::size_t patch=0;patch<patchSlots;++patch)if(patchCounts[patch]>=16) {
                const double error=patchError[patch]/patchCounts[patch];patchMse[quality]+=error*error/(double(patchCount)*4.);
            }
        }
        std::cout<<"Surface Cache probe convergence rays="<<trace.rayResolution*trace.rayResolution
            <<" probe_mse="<<probeMse[quality]<<" planar_pixel_mse="<<pixelMse[quality]<<" patch_mean_mse="<<patchMse[quality]
            <<" probes="<<planarProbeCount<<" pixels="<<planarPixelCount<<" patches="<<patchCount<<std::endl;
    }
    Check(probeMse[1]<probeMse[0]*.95&&probeMse[2]<probeMse[1]*.95,
        "More probe rays did not converge toward the independent-seed incident-light reference");
    Check(pixelMse[1]<pixelMse[0]*.95&&pixelMse[2]<pixelMse[1]*.95&&
        patchMse[1]<patchMse[0]*.95&&patchMse[2]<patchMse[1]*.95,
        "More probe rays did not reduce reconstructed planar / broad-patch lighting error");
    trace.rayResolution=8;trace.samplingSeed=0;render();
    std::cout<<"Surface Cache adaptive probe GI passed: active="<<active<<" adaptive="<<adaptive<<" rays="<<directionCount
        <<" cos_mean="<<cosineMean<<" cos2="<<cosineSquared<<" cos4="<<fourthMoment<<" cap_probability="<<cone
        <<" tangent_mean="<<glm::length(tangentMean)<<" integration_error="<<integrationError
        <<" known_black="<<knownBlack<<" unavailable="<<unreadable<<" light_scale_error="<<scaleError
        <<" receiver_albedo_error="<<receiverError<<" floor_pixels="<<floorPixels
        <<" adaptive_image_difference="<<difference(lit,uniformLit)<<'\n';
}
