// Inspect the GPU's actual ray samples and directional coefficients. The
// reference below integrates those samples independently; it does not use the
// shader's sampling hash, reconstruction weights, or coefficient-fit formula.
void SurfaceCacheDirectionalQuality(MaterialLabModule& lab,const Options& options) {
    auto fixture=options;fixture.sealedGiRoom=true;fixture.sdfBanquet=false;
    fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);
    frame.width=320;frame.height=200;frame.effects.outputSize={0,0};
    frame.present=false;frame.screenProbeStudyHud=false;
    frame.camera=SealedGiRoomCameraPreset(0,frame.OutputAspect());
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;
    gi.contact.enabled=false; // Compare directional probe representations without local receiver retracing.
    auto& trace=placement.trace;
    gi.enabled=true;gi.useScreenProbes=true;gi.view=2;gi.probeDiagnostics=false;
    gi.reconstructionDebug=0;gi.directionalReconstruction=true;gi.collectProbeCoverage=true;
    gi.spatial.enabled=false;gi.temporal.enabled=false;
    placement.enabled=true;placement.adaptive=true;placement.cornerRefinement=true;
    placement.curvatureAwareWeights=true; // Exercise the hit-distance guard as well as directional RGB.
    placement.baseSpacing=32;placement.candidateCount=8;placement.frameIndex=0;
    placement.freeze=false;placement.reset=true;
    trace.enabled=true;trace.showPanel=false;trace.cosineHemisphere=true;
    trace.rayResolution=8;trace.samplingSeed=0;trace.maxSteps=512;
    trace.sdf.enabled=true;trace.sdf.maxSteps=256;trace.maxDistance=gi.maxDistance;
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());
        frame.effects.cameraCut=false;placement.reset=false;gi.temporal.reset=false;};
    const auto finite=[&](const auto& image,const char* label){
        Check(!image.empty(),std::string("Missing directional fixture ")+label+": "+lab.LastError());
        for(const auto& value:image)for(unsigned channel=0;channel<4;++channel)
            Check(std::isfinite(value[channel]),std::string("Nonfinite directional fixture ")+label);
    };
    const auto maxDifference=[](const auto& a,const auto& b,bool alpha){
        Check(a.size()==b.size(),"Directional comparison changed image extent");double difference=0;
        for(std::size_t i=0;i<a.size();++i)for(unsigned channel=alpha?3u:0u;channel<(alpha?4u:3u);++channel)
            difference=std::max(difference,std::abs(double(a[i][channel])-double(b[i][channel])));
        return difference;
    };
    const auto energy=[](const auto& image){double total=0;
        for(const auto& value:image)total+=glm::length(glm::dvec3(value));return total;};
    const auto readable=[](int status){return status==1||status==5||status==9||status==10||status==13;};
    const auto geometryHit=[](int status){return status==1||status==17||(status>=9&&status<=12);};
    struct Summary {
        std::uint64_t active=0,adaptive=0,curved=0,knownBlack=0,unknown=0;
        std::uint64_t geometryHits=0,unknownGeometryHits=0,blackGeometryHits=0;
        double scalarError=0,alphaError=0,coefficientMagnitude=0,lowerBound=1e30,ownNormalError=0;
        double nearestGeometryInverseDistanceError=0,nearestGeometryInverseDistanceScaledError=0;
    };
    const auto inspect=[&](const ScreenProbeTraceReadback& rays,const std::vector<glm::vec4>& filtered,
        const char* phase){
        const bool spatial=!filtered.empty();
        const auto probes=lab.ReadbackScreenProbePlacement();
        const auto scalar=lab.ReadbackSurfaceCacheProbeIrradiance();
        const auto coefficients=lab.ReadbackSurfaceCacheProbeDirectional();
        finite(scalar,"scalar probe irradiance");finite(coefficients,"directional coefficients");
        const auto slots=probes.uniform.size()+probes.spawned.size();
        const auto rayCount=std::size_t(trace.rayResolution)*trace.rayResolution;
        Check(rays.probeSlots==slots&&rays.radianceStatus.size()==slots*rayCount&&
            rays.hitWorldDistance.size()==slots*rayCount&&rays.rayDirectionDepthFetches.size()==slots*rayCount&&
            scalar.size()==slots&&coefficients.size()==slots*4,
            "Directional fixture lost probe-major atlas layout");
        Check(!spatial||filtered.size()>=slots*rayCount*4,"Missing spatial directional source rays");
        Summary summary;
        for(std::size_t probe=0;probe<slots;++probe){
            const bool uniform=probe<probes.uniform.size();
            const auto index=uniform?probe:probe-probes.uniform.size();
            const auto pp=uniform?probes.uniform[index]:probes.spawned[index];
            const auto nd=uniform?probes.uniformNormals[index]:probes.spawnedNormals[index];
            if(pp.w<.5f){for(unsigned part=0;part<4;++part)
                    Check(glm::length(coefficients[probe*4+part])<1e-7f,"Inactive probe retained directional data");
                continue;}
            ++summary.active;summary.adaptive+=!uniform;
            const auto normal=glm::normalize(glm::dvec3(nd));
            summary.curved+=std::max({std::abs(normal.x),std::abs(normal.y),std::abs(normal.z)})<.999;
            const auto m0=coefficients[probe*4];
            const glm::dvec3 bx(coefficients[probe*4+1]),by(coefficients[probe*4+2]),bz(coefficients[probe*4+3]);
            glm::dvec3 expected(0);std::uint64_t known=0;double nearestGeometryInverseDistance=0;
            for(std::size_t ray=0;ray<rayCount;++ray){const auto address=probe*rayCount+ray;
                const auto value=spatial?filtered[address*4]:rays.radianceStatus[address];
                const int status=int(std::lround(value.w));
                // Source ray geometry is independent of radiance readability.
                // The closest real geometric hit governs parallax, including
                // known-black and unavailable-cache hits. Completed misses
                // contribute no geometry; they still retain the RGB budget.
                const double hitDistance=spatial?double(filtered[address*4+1].w):double(rays.hitWorldDistance[address].w);
                if(geometryHit(status)&&std::isfinite(hitDistance)&&hitDistance>0){
                    nearestGeometryInverseDistance=std::max(nearestGeometryInverseDistance,1./hitDistance);++summary.geometryHits;
                    summary.unknownGeometryHits+=!readable(status);
                    summary.blackGeometryHits+=readable(status)&&glm::length(glm::dvec3(value))<1e-8;
                }
                if(readable(status)){
                    expected+=glm::max(glm::dvec3(value),glm::dvec3(0));++known;
                    summary.knownBlack+=glm::length(glm::dvec3(value))<1e-8;
                }else if(status>0){++summary.unknown;
                    Check(glm::length(glm::dvec3(value))<1e-7,"Unavailable source ray acquired radiance");}
            }
            expected/=double(rayCount);
            summary.scalarError=std::max(summary.scalarError,glm::length(glm::dvec3(m0)-expected));
            summary.alphaError=std::max(summary.alphaError,std::abs(double(m0.w)-double(known)/double(rayCount)));
            if(!spatial){for(unsigned channel=0;channel<4;++channel)
                    Check(std::abs(double(m0[channel])-double(scalar[probe][channel]))<=2e-5*(1.+std::abs(double(scalar[probe][channel]))),
                        "Directional m0/readability differs from original scalar integration");}
            // For each RGB channel g(w)=a+b.w has global minimum a-|b|.
            // This analytic bound tests every direction, including unobserved
            // ones, without treating those directions as measured lighting.
            for(unsigned channel=0;channel<3;++channel){
                const glm::dvec3 b(bx[channel],by[channel],bz[channel]);
                const double mean=m0[channel],projection=glm::dot(b,normal);
                const double a=mean-(2./3.)*projection;
                summary.coefficientMagnitude+=glm::length(b);
                summary.lowerBound=std::min(summary.lowerBound,a-glm::length(b));
                Check(a-glm::length(b)>=-3e-5*(1.+std::abs(mean)),"Directional fit has a negative radiance lobe");
                const auto evaluate=[&](const glm::dvec3& receiver){return mean+(2./3.)*glm::dot(b,receiver-normal);};
                summary.ownNormalError=std::max(summary.ownNormalError,std::abs(evaluate(normal)-mean));
                const auto axis=std::abs(normal.z)<.99?glm::dvec3(0,0,1):glm::dvec3(0,1,0);
                const auto tangent=glm::normalize(glm::cross(axis,normal));
                const auto bitangent=glm::cross(normal,tangent);
                for(double angle:{-.6,-.25,.25,.6})for(const auto& lateral:{tangent,bitangent}){
                    const auto receiver=normal*std::cos(angle)+lateral*std::sin(angle);
                    Check(evaluate(receiver)>=-3e-5*(1.+std::abs(mean)),"Receiver-normal evaluation became negative");}
            }
            Check(m0.w>=0&&m0.w<=1,"Directional fit changed readable coverage range");
            // Only part 1 alpha carries the private nearest-geometry guard.
            // It is not readability or a radiance normalization denominator.
            const double metadata=coefficients[probe*4+1].w;
            if(placement.curvatureAwareWeights){
                const double error=std::abs(metadata-nearestGeometryInverseDistance);
                summary.nearestGeometryInverseDistanceError=std::max(summary.nearestGeometryInverseDistanceError,error);
                summary.nearestGeometryInverseDistanceScaledError=std::max(summary.nearestGeometryInverseDistanceScaledError,
                    error/(1.+std::abs(nearestGeometryInverseDistance)));
                Check(error<=5e-6*(1.+std::abs(nearestGeometryInverseDistance)),
                    "Directional hit-distance metadata differs from the nearest real source-ray geometry hit");
            }else Check(metadata==0,"Legacy curvature weights retained geometric hit-distance metadata");
            for(unsigned part=2;part<4;++part)
                Check(coefficients[probe*4+part].w==0,"Direction coefficient alpha fabricated readable coverage");
        }
        Check(summary.active>20&&summary.adaptive>0,"Directional fixture did not exercise adaptive probes");
        Check(summary.scalarError<3e-5&&summary.alphaError<2e-6&&summary.ownNormalError<1e-12,
            "Directional coefficients lost original full-count integration or own-normal identity");
        std::cout<<"SURFACE CACHE DIRECTIONAL phase="<<phase<<" active="<<summary.active
            <<" adaptive="<<summary.adaptive<<" curved="<<summary.curved<<" known_black="<<summary.knownBlack
            <<" unavailable="<<summary.unknown<<" scalar_error="<<summary.scalarError
            <<" alpha_error="<<summary.alphaError<<" model_lower_bound="<<summary.lowerBound
            <<" own_normal_identity_error="<<summary.ownNormalError
            <<" coefficient_magnitude="<<summary.coefficientMagnitude
            <<" geometry_hits="<<summary.geometryHits<<" unknown_geometry_hits="<<summary.unknownGeometryHits
            <<" black_geometry_hits="<<summary.blackGeometryHits
            <<" nearest_geometry_metadata="<<placement.curvatureAwareWeights
            <<" nearest_geometry_inverse_distance_error="<<summary.nearestGeometryInverseDistanceError
            <<" nearest_geometry_inverse_distance_scaled_error="<<summary.nearestGeometryInverseDistanceScaledError<<std::endl;
        return summary;
    };

    render();const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
    while(lab.ProbeSdfQualityStatistics().pending){render();
        Check(std::chrono::steady_clock::now()<deadline,"Directional fixture SDF preparation timed out");}
    const auto originalRays=lab.ReadbackScreenProbeTrace(false);
    const auto rawSummary=inspect(originalRays,{},"raw");
    Check(rawSummary.curved>0&&rawSummary.coefficientMagnitude>1e-5,
        "Directional fixture did not exercise nontrivial coefficients on curved geometry");
    Check(rawSummary.knownBlack>0,"Directional fixture omitted legal readable black samples");
    const auto directionalA=lab.ReadbackSurfaceCacheRawGi();finite(directionalA,"directional image A");
    Check(energy(directionalA)>1e-4,"Directional fixture has no indirect-light energy");
    const auto sources=lab.ReadbackScreenProbeSources();
    Check(sources.sourceViewPositions.size()==directionalA.size(),"Missing directional receiver validity");
    for(std::size_t i=0;i<directionalA.size();++i){
        Check(directionalA[i].w>=0&&directionalA[i].w<=1,"Directional reconstruction changed coverage range");
        if(sources.sourceViewPositions[i].w<.5f)
            Check(glm::length(glm::vec3(directionalA[i]))<1e-7f,"Directional GI filled invalid geometry");}
    gi.directionalReconstruction=false;render();
    const auto scalarB=lab.ReadbackSurfaceCacheRawGi();finite(scalarB,"scalar image B");
    Check(!lab.PipelineStatistics().surfaceCacheDirectionalCollected&&lab.ReadbackSurfaceCacheProbeDirectional().empty(),
        "Disabled directional stage returned a stale coefficient atlas");
    Check(maxDifference(directionalA,scalarB,true)<2e-6,"Direction evaluation altered reconstructed readability");
    const double directionEffect=maxDifference(directionalA,scalarB,false);
    Check(directionEffect>1e-7,"Directional coefficients were collected but never evaluated on curved receivers");
    gi.directionalReconstruction=true;render();
    const auto directionalAgain=lab.ReadbackSurfaceCacheRawGi();finite(directionalAgain,"directional image A again");
    const double roundTrip=maxDifference(directionalA,directionalAgain,false);
    Check(roundTrip<2e-6&&maxDifference(directionalA,directionalAgain,true)<2e-6,
        "Directional on/off/on round trip reused stale lighting");

    // The compatibility path must retain its original zero coefficient alpha.
    // Restore the new guard before the existing spatial/unknown/black checks.
    placement.curvatureAwareWeights=false;render();
    inspect(lab.ReadbackScreenProbeTrace(false),{},"legacy_curvature_weights");
    placement.curvatureAwareWeights=true;render();
    const auto restoredCurvature=lab.ReadbackSurfaceCacheRawGi();finite(restoredCurvature,"restored curvature image");
    Check(maxDifference(directionalA,restoredCurvature,false)<2e-6&&maxDifference(directionalA,restoredCurvature,true)<2e-6,
        "Curvature guard on/off/on round trip reused stale directional lighting");

    // The coefficient texture now represents the filtered source. Verify its
    // m0 against that source directly, rather than against the raw atlas.
    gi.spatial.enabled=true;gi.spatial.passes=1;render();
    Check(lab.PipelineStatistics().surfaceCacheSpatialPasses>0,"Directional fixture did not execute spatial filtering");
    const auto spatialRays=lab.ReadbackSurfaceCacheSpatialRays();finite(spatialRays,"filtered directional source");
    const auto spatialSummary=inspect(lab.ReadbackScreenProbeTrace(false),spatialRays,"spatial");
    finite(lab.ReadbackSurfaceCacheSpatialGi(),"directional spatial image");
    gi.spatial.enabled=false;

    // Deliberately unrelated card captures force real unavailable rays. This
    // distinguishes unknown alpha from the readable zero-energy lamp-off case.
    auto shifted=lab.PreparedProbeSdfScene()->Instances();
    for(auto& instance:shifted)instance.localToWorld[3].x+=50;
    Render::ProbeGlobalSdfSettings tiny;tiny.counts=glm::uvec3(2);
    const auto unrelated=Render::BuildProbeSdfScene(shifted,tiny);
    gi.cache=Render::BuildSurfaceCache(*unrelated);render();
    const auto missingSummary=inspect(lab.ReadbackScreenProbeTrace(false),{},"unavailable");
    const auto missing=lab.ReadbackSurfaceCacheRawGi();finite(missing,"unavailable directional GI");
    Check(missingSummary.unknown>100&&energy(missing)<1e-5,
        "Directional fit hid unavailable cache coverage or injected GI into it");
    gi.cache.reset();render();

    gi.temporal.enabled=true;gi.temporal.reset=true;render();render();
    Check(lab.PipelineStatistics().surfaceCacheHistoryUsed,"Directional fixture did not establish temporal history");
    gi.directionalReconstruction=false;render();
    Check(!lab.PipelineStatistics().surfaceCacheHistoryUsed,"Directional option change retained incompatible temporal history");
    render();Check(lab.PipelineStatistics().surfaceCacheHistoryUsed,"Scalar direction toggle never established new history");
    gi.directionalReconstruction=true;render();
    Check(!lab.PipelineStatistics().surfaceCacheHistoryUsed,"Restoring directional evaluation retained scalar history");
    render();Check(lab.PipelineStatistics().surfaceCacheHistoryUsed,"Restored direction stage failed to reuse new history");

    placement.freeze=true;render();render();
    Check(lab.PipelineStatistics().screenProbePlacementPasses==0,"Frozen compatible placement was unexpectedly regenerated");
    const float originalBias=gi.rayBias,originalThickness=trace.relativeThickness;
    trace.relativeThickness*=1.2f;render();
    Check(lab.PipelineStatistics().screenProbePlacementPasses>0&&!lab.PipelineStatistics().surfaceCacheHistoryUsed,
        "Changed visibility thickness reused frozen coverage or incompatible GI history");
    render();Check(lab.PipelineStatistics().screenProbePlacementPasses==0,"Updated frozen visibility coverage did not settle");
    gi.rayBias*=1.5f;render();
    Check(lab.PipelineStatistics().screenProbePlacementPasses>0&&!lab.PipelineStatistics().surfaceCacheHistoryUsed,
        "Changed normal bias reused frozen visibility coverage or incompatible history");
    placement.freeze=false;gi.rayBias=originalBias;trace.relativeThickness=originalThickness;render();

    frame.effects.areaLights.lights[0].enabled=false;render();
    const auto black=lab.ReadbackSurfaceCacheRawGi(),accumulated=lab.ReadbackSurfaceCacheTemporalGi();
    const auto blackCoefficients=lab.ReadbackSurfaceCacheProbeDirectional();
    finite(black,"lamp-off raw");finite(accumulated,"lamp-off history");finite(blackCoefficients,"lamp-off coefficients");
    std::uint64_t readableBlack=0;for(const auto& value:black)readableBlack+=value.w>.5f;
    Check(energy(black)<1e-5&&energy(accumulated)<1e-5&&energy(blackCoefficients)<1e-5&&readableBlack>100,
        "Directional stage fabricated or retained light, or discarded legal black coverage");
    std::cout<<"SURFACE CACHE DIRECTIONAL COMPLETE size="<<frame.width<<'x'<<frame.height
        <<" probe_rays="<<trace.rayResolution*trace.rayResolution<<" spatial_passes=1"
        <<" raw_unknown="<<rawSummary.unknown<<" forced_unknown="<<missingSummary.unknown
        <<" spatial_scalar_error="<<spatialSummary.scalarError<<" direction_effect="<<directionEffect
        <<" on_off_on_error="<<roundTrip
        <<" readable_black="<<readableBlack<<" lamp_off_raw="<<energy(black)
        <<" lamp_off_temporal="<<energy(accumulated)<<" lamp_off_coefficients="<<energy(blackCoefficients)
        <<" history_option_reset=passed frozen_visibility_refresh=passed"<<std::endl;
}
