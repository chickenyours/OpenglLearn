// Curved silhouettes and both sides of depth/normal discontinuities are kept
// in this fixture. No probe-coverage mask may remove a difficult receiver.
// The reference is independent of probe reconstruction, but intentionally
// shares the completed single-bounce Surface Cache; it is not path-traced truth.
void SurfaceCacheEdgeQuality(MaterialLabModule& lab,const Options& options) {
    Check(options.vulkan||options.surfaceCacheEdgeDiagnosticsOnly,
        "Full edge reference quality requires Vulkan; use --surface-cache-edge-diagnostics for OpenGL structural checks");
    Check(!options.surfaceCacheFeatureAblation||!options.surfaceCacheEdgeDiagnosticsOnly,"Feature ablation needs the paired Vulkan reference, not diagnostics-only mode");
    auto fixture=options;fixture.sealedGiRoom=true;fixture.sdfBanquet=false;
    fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);
    frame.width=options.renderSizeSet?options.benchmarkWidth:640;
    frame.height=options.renderSizeSet?options.benchmarkHeight:400;
    frame.effects.outputSize={0,0};frame.present=false;frame.screenProbeStudyHud=false;
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;
    auto& trace=placement.trace;gi.enabled=true;gi.useScreenProbes=true;gi.view=2;
    // Isolate the legacy history curve; temporal/probe-imprint fixtures cover static convergence.
    gi.temporal.staticConvergence=false;
    gi.probeDiagnostics=false;gi.cacheBilinear=true;gi.collectProbeCoverage=true;
    placement.enabled=true;placement.adaptive=true;placement.cornerRefinement=true;
    placement.baseSpacing=32;placement.candidateCount=8;placement.frameIndex=0;
    placement.freeze=false;placement.reset=true;trace.rayResolution=8;
    trace.cosineHemisphere=true;trace.samplingSeed=0;trace.maxSteps=512;trace.sdf.maxSteps=256;
    const auto spatialSettings=gi.spatial;const auto temporalSettings=gi.temporal;
    const bool defaultSafeOrigins=gi.safeProbeOrigins,defaultCurvature=placement.curvatureRefinement,defaultVisibility=placement.reconstructionVisibility;
    gi.spatial.enabled=false;gi.temporal.enabled=false;
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());
        frame.effects.cameraCut=false;placement.reset=false;gi.temporal.reset=false;};
    const std::size_t pixels=std::size_t(frame.width)*frame.height;
    const glm::dvec3 luma(.2126,.7152,.0722);
    const auto finite=[&](const auto& image,const char* label){
        Check(image.size()==pixels,std::string("Missing edge fixture ")+label+": "+lab.LastError());
        for(const auto& p:image)for(unsigned c=0;c<4;++c)
            Check(std::isfinite(p[c]),"Nonfinite edge fixture image");
    };
    const auto save=[&](const auto& hdr,const std::string& label,bool diagnostic=false){
        if(options.screenshot.empty())return;
        FramePixels image;image.width=frame.width;image.height=frame.height;image.rgba.resize(pixels*4);
        for(std::size_t i=0;i<pixels;++i){for(unsigned c=0;c<3;++c){
            double value=std::max(0.,double(hdr[i][c]));
            if(!diagnostic){value*=4.;value=value/(1.+value);
                value=value<=.0031308?12.92*value:1.055*std::pow(value,1./2.4)-.055;}
            image.rgba[i*4+c]=std::uint8_t(std::lround(std::clamp(value,0.,1.)*255.));
        }image.rgba[i*4+3]=255;}
        auto path=std::filesystem::path(options.screenshot);
        path.replace_filename(path.stem().string()+"_"+label+path.extension().string());Save(image,path.string());
    };
    const auto classify=[](glm::vec3 p,glm::vec3 n)->unsigned char {
        // Region zero includes all four spherical/ellipsoidal ceramics.
        const auto& objects=SealedGiRoomObjects();
        for(unsigned object:{19u,20u,23u,24u}) {
            const auto& shape=objects[object];
            const glm::vec3 q=(p-shape.position)/shape.scale;
            if(std::abs(glm::length(q)-1.f)<.04f)return 0;
        }
        if((std::abs(p.x-SealedGiRoomMinimum.x)<.02f&&n.x>.98f)||
           (std::abs(p.x-SealedGiRoomMaximum.x)<.02f&&n.x<-.98f)||
           (std::abs(p.z-SealedGiRoomMinimum.z)<.02f&&n.z>.98f)||
           (std::abs(p.z-SealedGiRoomMaximum.z)<.02f&&n.z<-.98f)||
           (std::abs(p.y-SealedGiRoomMaximum.y)<.02f&&n.y<-.98f))return 1;
        return 2;
    };
    struct Pose {
        Render::PipelineCamera camera;
        std::vector<glm::vec3> reference;
        std::vector<unsigned char> valid,edge,region;
        std::size_t edgePixels=0,curvedPixels=0,wallPixels=0;
    };
    const auto makeMask=[&](Pose& pose){
        const auto sources=lab.ReadbackScreenProbeSources();
        finite(sources.sourceViewPositions,"GBuffer positions");finite(sources.sourceWorldNormals,"GBuffer normals");
        pose.valid.assign(pixels,0);pose.edge.assign(pixels,0);pose.region.assign(pixels,2);
        std::vector<glm::vec3> world(pixels),normals(pixels);
        const auto inverseView=glm::inverse(pose.camera.view);
        for(std::size_t i=0;i<pixels;++i){const auto p=sources.sourceViewPositions[i],n=sources.sourceWorldNormals[i];
            if(p.w<.5f||n.w<.5f||glm::dot(glm::vec3(n),glm::vec3(n))<.5f)continue;
            pose.valid[i]=1;world[i]=glm::vec3(inverseView*glm::vec4(glm::vec3(p),1));
            normals[i]=glm::normalize(glm::vec3(n));pose.region[i]=classify(world[i],normals[i]);
        }
        // Eight-neighbor discontinuities seed a Chebyshev distance transform.
        // Seed pixels have distance one: the exact seam is measured as well.
        std::vector<unsigned char> distance(pixels,255);
        for(unsigned y=0;y<frame.height;++y)for(unsigned x=0;x<frame.width;++x){
            const auto i=std::size_t(y)*frame.width+x;if(!pose.valid[i])continue;bool boundary=false;
            for(int dy=-1;dy<=1&&!boundary;++dy)for(int dx=-1;dx<=1;++dx){
                const int xx=int(x)+dx,yy=int(y)+dy;
                if(xx<0||yy<0||xx>=int(frame.width)||yy>=int(frame.height))continue;
                const auto j=std::size_t(yy)*frame.width+unsigned(xx);
                if(!pose.valid[j]||glm::dot(normals[i],normals[j])<.8f||
                   std::max(std::abs(glm::dot(world[j]-world[i],normals[i])),
                       std::abs(glm::dot(world[j]-world[i],normals[j])))>
                       std::max(.003f,std::abs(sources.sourceViewPositions[i].z)*.01f)) {boundary=true;break;}
            }
            if(boundary)distance[i]=1;
        }
        const auto relax=[&](int x,int y,int xx,int yy){
            if(xx<0||yy<0||xx>=int(frame.width)||yy>=int(frame.height))return;
            const auto i=std::size_t(y)*frame.width+unsigned(x),j=std::size_t(yy)*frame.width+unsigned(xx);
            if(pose.valid[i])distance[i]=std::min(distance[i],static_cast<unsigned char>(std::min(255,int(distance[j])+1)));
        };
        for(int y=0;y<int(frame.height);++y)for(int x=0;x<int(frame.width);++x){
            relax(x,y,x-1,y);relax(x,y,x-1,y-1);relax(x,y,x,y-1);relax(x,y,x+1,y-1);}
        for(int y=int(frame.height)-1;y>=0;--y)for(int x=int(frame.width)-1;x>=0;--x){
            relax(x,y,x+1,y);relax(x,y,x+1,y+1);relax(x,y,x,y+1);relax(x,y,x-1,y+1);}
        for(std::size_t i=0;i<pixels;++i)if(pose.valid[i]&&distance[i]<=16){
            pose.edge[i]=1;++pose.edgePixels;pose.curvedPixels+=pose.region[i]==0;pose.wallPixels+=pose.region[i]==1;
        }
        Check(pose.edgePixels>100,"Edge fixture has no measurable discontinuity band");
    };
    struct Error {double squared=0,dark=0,bright=0,bias=0;std::uint64_t count=0;};
    struct Metrics {
        std::array<std::array<Error,3>,4> errors{}; // all/curved/wall/other, raw/spatial/temporal
        std::uint64_t frames=0,qualityFrames=0,holes=0,lowSupport=0,edgeSamples=0;
        double readability=0,history=0;std::array<double,2> period8{},period16{};
        std::uint64_t originFrames=0,originActive=0,originInvalid=0,originDetails=0;
        std::array<std::uint64_t,6> originReasons{};
        std::uint64_t rayFrames=0,raySamples=0,rayActive=0,rayReadable=0,rayUnavailable=0;
        std::array<std::uint64_t,18> rayStatuses{};
    };
    const std::array<const char*,4> regionNames{{"all","curved","walls","other"}};
    const std::array<const char*,3> stageNames{{"raw","spatial","temporal"}};
    const auto report=[&](const char* view,const char* interval,const Metrics& metric){
        Check(metric.edgeSamples>0,"Edge fixture has no measured receivers");
        std::cout<<"SURFACE CACHE EDGE coverage view="<<view<<" interval="<<interval
            <<" frames="<<metric.frames<<" reference_frames="<<metric.qualityFrames
            <<" edge_samples="<<metric.edgeSamples<<" true_holes="<<metric.holes
            <<" low_support="<<metric.lowSupport
            <<" hole_fraction="<<double(metric.holes)/double(metric.edgeSamples)
            <<" low_support_fraction="<<double(metric.lowSupport)/double(metric.edgeSamples)
            <<" mean_readability="<<metric.readability/double(metric.edgeSamples)
            <<" mean_history="<<metric.history/double(metric.edgeSamples)
            <<" grid_phase8_x_residual_mse="<<metric.period8[0]/std::max(1.,double(metric.qualityFrames))
            <<" grid_phase8_y_residual_mse="<<metric.period8[1]/std::max(1.,double(metric.qualityFrames))
            <<" grid_phase16_x_residual_mse="<<metric.period16[0]/std::max(1.,double(metric.qualityFrames))
            <<" grid_phase16_y_residual_mse="<<metric.period16[1]/std::max(1.,double(metric.qualityFrames));
        if(options.surfaceCacheFeatureAblation){std::cout<<" origin_collected_frames="<<metric.originFrames
                <<" origin_active="<<metric.originActive<<" origin_invalid="<<metric.originInvalid
                <<" origin_invalid_fraction="<<double(metric.originInvalid)/std::max(1.,double(metric.originActive));
            for(unsigned reason=1;reason<=5;++reason)std::cout<<" origin_invalid_reason"<<reason<<"="<<metric.originReasons[reason];}
        if(options.surfaceCacheFeatureAblation){std::cout<<" ray_collected_frames="<<metric.rayFrames<<" ray_samples="<<metric.raySamples
                <<" ray_active_samples="<<metric.rayActive<<" ray_readable="<<metric.rayReadable<<" ray_unavailable="<<metric.rayUnavailable
                <<" ray_readable_fraction="<<double(metric.rayReadable)/std::max(1.,double(metric.rayActive));
            for(unsigned status=0;status<metric.rayStatuses.size();++status)std::cout<<" ray_status"<<status<<"="<<metric.rayStatuses[status];}
        std::cout<<std::endl;
        for(unsigned region=0;region<4;++region)for(unsigned stage=0;stage<3;++stage){const auto& e=metric.errors[region][stage];
            if(!e.count)continue;
            std::cout<<"SURFACE CACHE EDGE error view="<<view<<" interval="<<interval
                <<" region="<<regionNames[region]<<" stage="<<stageNames[stage]<<" samples="<<e.count
                <<" luma_mse="<<e.squared/double(e.count)<<" dark_mse="<<e.dark/double(e.count)
                <<" bright_mse="<<e.bright/double(e.count)<<" signed_bias="<<e.bias/double(e.count)<<std::endl;
        }
    };
    // Anchor, first moving frame, and four later checkpoints. Continuous
    // motion still renders all 32 frames; only these poses have costly truth.
    constexpr std::array<unsigned,6> referenceSteps{{0,1,4,8,12,16}};
    constexpr unsigned referenceSeeds=8;
    std::uint64_t historyReusedFrames=0,totalCurvedPixels=0,totalWallPixels=0;
    std::uint64_t referencePoseCount=0,ablationCases=0,ablationFrames=0,ablationWarmupFrames=0,ablationHistoryReused=0;
    std::uint64_t ablationStationaryFrames=0,ablationMovingFrames=0,ablationMovingHistoryReused=0;
    for(unsigned view=0;view<2;++view){
        gi.safeProbeOrigins=defaultSafeOrigins;placement.curvatureRefinement=defaultCurvature;placement.reconstructionVisibility=defaultVisibility;
        const char* viewName=view==0?"wall_ceiling":"oval_wall";
        auto base=SealedGiRoomCameraPreset(1,frame.OutputAspect());
        if(view==1){base.position={2.60f,1.75f,.90f};
            base.view=glm::lookAt(base.position,glm::vec3(1.18f,1.20f,-1.15f),glm::vec3(0,1,0));}
        const auto inverse=glm::inverse(base.view);const auto right=glm::normalize(glm::vec3(inverse[0]));
        const auto forward=glm::normalize(-glm::vec3(inverse[2]));
        const auto cameraAt=[&](unsigned step){auto camera=base;
            camera.position=base.position+right*(.0125f*float(step));
            const auto direction=glm::vec3(glm::rotate(glm::mat4(1),.004f*float(step),glm::vec3(0,1,0))*glm::vec4(forward,0));
            camera.view=glm::lookAt(camera.position,camera.position+direction,glm::vec3(0,1,0));return camera;};
        std::array<Pose,referenceSteps.size()> poses;
        const unsigned poseCount=options.surfaceCacheEdgeDiagnosticsOnly?1u:options.surfaceCacheFeatureAblation?2u:unsigned(poses.size());
        for(unsigned index=0;index<poseCount;++index){auto& pose=poses[index];pose.camera=cameraAt(referenceSteps[index]);
            frame.camera=pose.camera;gi.useScreenProbes=true;placement.enabled=true;placement.reset=true;
            gi.temporal.enabled=false;gi.spatial.enabled=false;trace.samplingSeed=0;frame.effects.cameraCut=true;render();
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
            while(lab.ProbeSdfQualityStatistics().pending){render();Check(std::chrono::steady_clock::now()<deadline,"Edge fixture SDF preparation timed out");}
            makeMask(pose);totalCurvedPixels+=pose.curvedPixels;totalWallPixels+=pose.wallPixels;
            if(options.surfaceCacheEdgeDiagnosticsOnly)continue;
            if(view==1&&index==0)Check(pose.curvedPixels>100&&pose.wallPixels>100,
                "Close oval camera omitted one side of its curved / wall silhouette");
            pose.reference.assign(pixels,glm::vec3(0));
            std::vector<glm::vec3> firstHalf(pixels,glm::vec3(0)),secondHalf(pixels,glm::vec3(0));
            gi.useScreenProbes=false;placement.enabled=false;gi.raysPerPixel=32;
            for(unsigned sample=0;sample<referenceSeeds;++sample){trace.samplingSeed=1009+sample*73;render();
                const auto image=lab.ReadbackSurfaceCacheRawGi();finite(image,"per-pixel reference");
                auto& half=sample<referenceSeeds/2?firstHalf:secondHalf;
                for(std::size_t i=0;i<pixels;++i){pose.reference[i]+=glm::vec3(image[i])/float(referenceSeeds);
                    half[i]+=glm::vec3(image[i])/float(referenceSeeds/2);}
            }
            double uncertainty=0,mean=0;for(std::size_t i=0;i<pixels;++i)if(pose.edge[i]){
                const double delta=glm::dot(glm::dvec3(firstHalf[i])-glm::dvec3(secondHalf[i]),luma);
                uncertainty+=delta*delta;mean+=glm::dot(glm::dvec3(pose.reference[i]),luma);}
            std::cout<<"SURFACE CACHE EDGE reference view="<<viewName<<" step="<<referenceSteps[index]
                <<" spp="<<referenceSeeds*32<<" edge_pixels="<<pose.edgePixels<<" curved_pixels="<<pose.curvedPixels
                <<" wall_pixels="<<pose.wallPixels<<" half_difference_mse="<<uncertainty/double(pose.edgePixels)
                <<" mean="<<mean/double(pose.edgePixels)<<" shared_cache=1 single_bounce=1"<<std::endl;
            Check(uncertainty>1e-12,"Edge reference seeds did not produce independent nonzero lighting samples");
            ++referencePoseCount;
            if(index==0)save(pose.reference,std::string(viewName)+"_reference");
        }
        gi.useScreenProbes=true;placement.enabled=true;placement.reset=true;placement.freeze=false;
        trace.samplingSeed=0;placement.frameIndex=0;trace.rayResolution=8;
        gi.spatial=spatialSettings;gi.spatial.enabled=true;gi.temporal=temporalSettings;gi.temporal.enabled=true;
        frame.camera=base;frame.effects.cameraCut=true;
        // Debug signals bypass real ray radiance and receiver albedo. Constant
        // one exposes genuine reconstruction holes; base/adaptive tags expose
        // the selection boundaries without Monte Carlo or cache differences.
        gi.temporal.enabled=false;gi.spatial.enabled=false;
        for(unsigned mode=1;mode<=2;++mode){gi.reconstructionDebug=mode;render();
            const auto tagged=lab.ReadbackSurfaceCacheRawGi(),coverage=lab.ReadbackSurfaceCacheProbeCoverage();
            finite(tagged,"reconstruction diagnostic");finite(coverage,"diagnostic coverage");
            const auto& pose=poses[0];double squared=0,jumpSum=0,maxJump=0;
            std::uint64_t measured=0,missing=0,holes=0,largeJumps=0,pairs=0;
            double phaseSum[2][2][16]{};std::uint64_t phaseCount[2][2][16]{};
            double mean=0;
            for(unsigned y=0;y<frame.height;++y)for(unsigned x=0;x<frame.width;++x){
                const auto i=std::size_t(y)*frame.width+x;if(!pose.edge[i])continue;
                const double value=glm::dot(glm::dvec3(tagged[i]),luma);
                squared+=(value-1.)*(value-1.);++measured;missing+=value<.999;
                holes+=coverage[i].w<.5f;mean+=value;
                if(mode==2&&coverage[i].w>=.5f)
                    Check(value>=.24999&&value<=.75001,"Reconstruction weights left the convex base/adaptive range");
                for(unsigned period=0;period<2;++period)for(unsigned axis=0;axis<2;++axis){
                    const unsigned phase=(axis?y:x)%(period?16u:8u);
                    phaseSum[period][axis][phase]+=value;++phaseCount[period][axis][phase];}
                for(unsigned axis=0;axis<2;++axis){if((axis==0&&x+1>=frame.width)||(axis==1&&y+1>=frame.height))continue;
                    const auto j=i+(axis?frame.width:1);if(!pose.edge[j])continue;
                    const double jump=std::abs(value-glm::dot(glm::dvec3(tagged[j]),luma));
                    jumpSum+=jump;maxJump=std::max(maxJump,jump);largeJumps+=jump>.1;++pairs;}
            }
            Check(measured>100&&pairs>100,"Missing edge reconstruction diagnostic samples");
            if(mode==1){
                Check(missing==holes,"Constant illumination changed on a geometrically supported receiver");
                Check(squared<=double(holes)+double(measured)*1e-10,"Constant reconstruction failed normalization");
            }
            // These two fixed full-resolution views keep real silhouette
            // discontinuities in the count. The previous group-switching
            // estimator exceeded 2.5%/2.9%; leave conservative room for genuine
            // geometry edges while rejecting its periodic coarse/fine jumps.
            if(mode==2&&frame.width==1280&&frame.height==800)
                Check(largeJumps*50<pairs,"Periodic base/adaptive switching exceeded the edge regression limit");
            double phaseEnergy[2][2]{};mean/=double(measured);
            for(unsigned period=0;period<2;++period)for(unsigned axis=0;axis<2;++axis)
                for(unsigned phase=0;phase<(period?16u:8u);++phase)if(phaseCount[period][axis][phase]){
                    const double delta=phaseSum[period][axis][phase]/double(phaseCount[period][axis][phase])-mean;
                    phaseEnergy[period][axis]+=delta*delta*double(phaseCount[period][axis][phase])/double(measured);}
            std::cout<<"SURFACE CACHE EDGE reconstruction view="<<viewName<<" mode="<<mode
                <<" edge_samples="<<measured<<" constant_one_mse="<<squared/double(measured)
                <<" nonconstant_pixels="<<missing<<" true_holes="<<holes
                <<" adjacent_pairs="<<pairs<<" mean_adjacent_jump="<<jumpSum/double(pairs)
                <<" max_adjacent_jump="<<maxJump<<" jumps_over_01="<<largeJumps
                <<" grid_phase8_x_signal_mse="<<phaseEnergy[0][0]<<" grid_phase8_y_signal_mse="<<phaseEnergy[0][1]
                <<" grid_phase16_x_signal_mse="<<phaseEnergy[1][0]<<" grid_phase16_y_signal_mse="<<phaseEnergy[1][1]<<std::endl;
            save(tagged,std::string(viewName)+"_debug_"+std::to_string(mode));
        }
        gi.reconstructionDebug=0;gi.spatial=spatialSettings;gi.spatial.enabled=true;
        gi.temporal=temporalSettings;gi.temporal.enabled=true;gi.temporal.reset=true;
        if(!options.surfaceCacheFeatureAblation)for(unsigned warm=0;warm<16;++warm)render();
        if(options.surfaceCacheEdgeDiagnosticsOnly){
            save(lab.ReadbackSurfaceCacheRawGi(),std::string(viewName)+"_raw");
            save(lab.ReadbackSurfaceCacheTemporalGi(),std::string(viewName)+"_temporal");
            continue;
        }
        const auto measure=[&](Metrics& metric,const Pose& pose,bool hasReference,const char* diagnosticInterval=nullptr){
            const auto raw=lab.ReadbackSurfaceCacheRawGi(),spatial=lab.ReadbackSurfaceCacheSpatialGi();
            const auto accumulated=lab.ReadbackSurfaceCacheTemporalGi(),coverage=lab.ReadbackSurfaceCacheProbeCoverage();
            const auto history=lab.ReadbackSurfaceCacheTemporalHistory();
            finite(raw,"raw");finite(spatial,"spatial");finite(accumulated,"temporal");finite(coverage,"probe coverage");finite(history,"history");
            ++metric.frames;if(hasReference)++metric.qualityFrames;
            if(options.surfaceCacheFeatureAblation){
                const auto rays=lab.ReadbackScreenProbeTrace(false);
                Check(rays.probeSlots>0&&rays.radianceStatus.size()==std::size_t(rays.probeSlots)*rays.rayResolution*rays.rayResolution,
                    "Feature ablation lost the current trace atlas status samples");++metric.rayFrames;
                for(const auto& value:rays.radianceStatus){Check(std::isfinite(value.w),"Feature ablation observed a nonfinite ray status");
                    const int status=int(std::lround(value.w));Check(status>=0&&status<int(metric.rayStatuses.size()),"Feature ablation observed an unknown ray status code");
                    ++metric.raySamples;++metric.rayStatuses[unsigned(status)];if(status==0)continue;++metric.rayActive;
                    const bool readable=status==1||status==5||status==9||status==10||status==13;
                    metric.rayReadable+=readable;metric.rayUnavailable+=!readable;
                }
            }
            if(options.surfaceCacheFeatureAblation&&lab.PipelineStatistics().surfaceCacheProbeOriginsCollected){
                const auto origins=lab.ReadbackSurfaceCacheProbeOrigins();const auto probes=lab.ReadbackScreenProbePlacement();
                const auto slots=probes.uniform.size()+probes.spawned.size();
                Check(origins.size()==slots&&slots>0,"Feature ablation lost the actual probe origin atlas");++metric.originFrames;
                for(std::size_t probe=0;probe<slots;++probe){const bool uniform=probe<probes.uniform.size();const auto index=uniform?probe:probe-probes.uniform.size();
                    const auto p=uniform?probes.uniform[index]:probes.spawned[index],nd=uniform?probes.uniformNormals[index]:probes.spawnedNormals[index];
                    if(p.w<.5f)continue;const auto origin=origins[probe];for(unsigned channel=0;channel<4;++channel)
                        Check(std::isfinite(origin[channel]),"Feature ablation observed a nonfinite safe origin");
                    ++metric.originActive;const int status=int(std::lround(origin.w));
                    Check(status>=-5&&status<=3&&status!=0,"Feature ablation observed an unclassified active origin");
                    if(status>=0)continue;++metric.originInvalid;++metric.originReasons[unsigned(-status)];
                    if(metric.originDetails<5){++metric.originDetails;
                        const auto normal=glm::normalize(glm::vec3(nd)),towardCamera=glm::normalize(pose.camera.position-glm::vec3(p));
                        std::cout<<"SURFACE CACHE EDGE ORIGIN INVALID view="<<viewName<<" interval="<<(diagnosticInterval?diagnosticInterval:"ablation")
                            <<" frame="<<metric.frames<<" probe="<<probe<<" adaptive="<<!uniform<<" reason="<<status
                            <<" p="<<p.x<<","<<p.y<<","<<p.z<<" n="<<normal.x<<","<<normal.y<<","<<normal.z
                            <<" source_view_facing="<<glm::dot(normal,towardCamera)<<" view_depth="<<nd.w<<std::endl;}
                }
            }
            double bins[2][2][3][16]{};
            std::uint64_t counts[2][2][3][16]{};
            std::array<double,3> regionSum{};std::array<std::uint64_t,3> regionCount{};
            for(std::size_t i=0;i<pixels;++i){
                if(!pose.valid[i]){Check(glm::length(glm::vec3(raw[i]))<1e-6f&&glm::length(glm::vec3(accumulated[i]))<1e-6f,"Edge GI leaked into invalid geometry");continue;}
                Check(history[i].w>=0&&history[i].w<=float(gi.temporal.maxFrames)+.001f,"Edge history exceeded frame cap");
                if(!pose.edge[i])continue;
                ++metric.edgeSamples;metric.holes+=coverage[i].w<.5f;
                metric.lowSupport+=coverage[i].w>=.5f&&coverage[i].x>.9f&&coverage[i].y<.1f&&coverage[i].z>.9f;
                metric.readability+=raw[i].w;metric.history+=history[i].w;
                if(coverage[i].w<.5f)Check(glm::length(glm::vec3(raw[i]))<1e-6f,"Missing probe support fabricated raw edge GI");
                if(!hasReference)continue;
                const std::array<glm::vec4,3> values{{raw[i],spatial[i],accumulated[i]}};
                for(unsigned stage=0;stage<3;++stage){const double e=glm::dot(glm::dvec3(values[stage])-glm::dvec3(pose.reference[i]),luma);
                    for(unsigned region:{0u,unsigned(pose.region[i])+1u}){auto& error=metric.errors[region][stage];
                        error.squared+=e*e;error.dark+=std::min(e,0.)*std::min(e,0.);
                        error.bright+=std::max(e,0.)*std::max(e,0.);error.bias+=e;++error.count;}
                    if(stage==2){const auto region=pose.region[i];regionSum[region]+=e;++regionCount[region];
                        for(unsigned period=0;period<2;++period)for(unsigned axis=0;axis<2;++axis){
                            // Report x/y grid-phase residuals after removing
                            // regional bias; this is not a spectral proof.
                            const unsigned size=period?16:8,bin=unsigned(axis?i/frame.width:i%frame.width)%size;
                            bins[period][axis][region][bin]+=e;++counts[period][axis][region][bin];}
                    }
                }
            }
            if(hasReference)for(unsigned period=0;period<2;++period)for(unsigned axis=0;axis<2;++axis){double energy=0;std::uint64_t samples=0;
                for(unsigned region=0;region<3;++region)if(regionCount[region]){
                    const double mean=regionSum[region]/double(regionCount[region]);
                    for(unsigned bin=0;bin<(period?16u:8u);++bin)if(counts[period][axis][region][bin]){
                        const double delta=bins[period][axis][region][bin]/double(counts[period][axis][region][bin])-mean;
                        energy+=delta*delta*double(counts[period][axis][region][bin]);samples+=counts[period][axis][region][bin];}}
                if(period)metric.period16[axis]+=energy/std::max(1.,double(samples));else metric.period8[axis]+=energy/std::max(1.,double(samples));}
        };
        if(options.surfaceCacheFeatureAblation){
            struct FeatureCase {const char* name;bool safe,curvature,visibility;};
            constexpr std::array<FeatureCase,5> cases{{
                {"legacy",false,false,true},{"safe_only",true,false,true},
                {"curvature_only",false,true,true},{"current",true,true,true},
                {"no_screen_visibility",true,true,false}}};
            for(const auto& feature:cases){
                gi.safeProbeOrigins=feature.safe;placement.curvatureRefinement=feature.curvature;placement.reconstructionVisibility=feature.visibility;
                gi.reconstructionDebug=0;gi.spatial=spatialSettings;gi.spatial.enabled=true;
                gi.temporal=temporalSettings;gi.temporal.enabled=true;gi.temporal.reset=true;
                frame.camera=poses[0].camera;frame.effects.cameraCut=true;placement.reset=true;trace.samplingSeed=0;
                for(unsigned warm=0;warm<16;++warm){render();++ablationWarmupFrames;}
                const std::string interval=std::string("ablation_")+feature.name;
                Metrics metric;std::uint64_t reused=0;
                for(unsigned sample=0;sample<4;++sample){render();const bool historyUsed=lab.PipelineStatistics().surfaceCacheHistoryUsed;
                    reused+=historyUsed;ablationHistoryReused+=historyUsed;measure(metric,poses[0],true,interval.c_str());++ablationFrames;++ablationStationaryFrames;}
                Check(metric.frames==4&&metric.qualityFrames==4&&reused==4,"Feature ablation omitted paired stationary frames or valid temporal history");
                report(viewName,interval.c_str(),metric);++ablationCases;
                std::cout<<"SURFACE CACHE EDGE ABLATION view="<<viewName<<" case="<<feature.name
                    <<" safe_origins="<<feature.safe<<" curvature="<<feature.curvature<<" screen_visibility="<<feature.visibility
                    <<" warmup_frames=16 stationary_measured_frames=4 first_move_measured_frames=1 history_reused_static="<<reused
                    <<" diagnostic_visibility_disable="<<!feature.visibility<<std::endl;
                save(lab.ReadbackSurfaceCacheRawGi(),std::string(viewName)+"_"+interval+"_raw");
                save(lab.ReadbackSurfaceCacheSpatialGi(),std::string(viewName)+"_"+interval+"_spatial");
                save(lab.ReadbackSurfaceCacheTemporalGi(),std::string(viewName)+"_"+interval+"_temporal");
                frame.camera=poses[1].camera;render();const bool movingHistory=lab.PipelineStatistics().surfaceCacheHistoryUsed;
                Check(movingHistory,"Feature ablation first moving frame failed to reuse the established camera history");
                Metrics firstMoving;const std::string firstInterval=interval+"_first_move";
                measure(firstMoving,poses[1],true,firstInterval.c_str());++ablationFrames;++ablationMovingFrames;
                ablationHistoryReused+=movingHistory;ablationMovingHistoryReused+=movingHistory;
                Check(firstMoving.frames==1&&firstMoving.qualityFrames==1,"Feature ablation omitted its paired first-moving frame");
                report(viewName,firstInterval.c_str(),firstMoving);
                save(lab.ReadbackSurfaceCacheRawGi(),std::string(viewName)+"_"+firstInterval+"_raw");
                save(lab.ReadbackSurfaceCacheSpatialGi(),std::string(viewName)+"_"+firstInterval+"_spatial");
                save(lab.ReadbackSurfaceCacheTemporalGi(),std::string(viewName)+"_"+firstInterval+"_temporal");
            }
            // Restore the tested current feature set before the next camera and
            // the final lamp-off check; the visibility-off case is diagnostic.
            gi.safeProbeOrigins=true;placement.curvatureRefinement=true;placement.reconstructionVisibility=true;
            continue;
        }
        Metrics stationary;for(unsigned sample=0;sample<4;++sample){render();measure(stationary,poses[0],true);}
        report(viewName,"stationary",stationary);
        save(lab.ReadbackSurfaceCacheRawGi(),std::string(viewName)+"_raw");
        save(lab.ReadbackSurfaceCacheSpatialGi(),std::string(viewName)+"_spatial");
        save(lab.ReadbackSurfaceCacheTemporalGi(),std::string(viewName)+"_temporal");
        save(lab.ReadbackSurfaceCacheProbeCoverage(),std::string(viewName)+"_coverage",true);
        Metrics moving,firstMoving;std::uint64_t viewHistoryReused=0;
        for(unsigned tick=0;tick<32;++tick){const unsigned step=tick<16?tick+1:31-tick;
            frame.camera=cameraAt(step);render();const bool historyUsed=lab.PipelineStatistics().surfaceCacheHistoryUsed;
            historyReusedFrames+=historyUsed;viewHistoryReused+=historyUsed;
            if(tick==0)Check(historyUsed,"Edge first moving frame did not reuse valid camera history");
            unsigned referenceIndex=unsigned(poses.size());
            for(unsigned index=0;index<referenceSteps.size();++index)if(referenceSteps[index]==step){referenceIndex=index;break;}
            if(referenceIndex<poses.size()){
                measure(moving,poses[referenceIndex],true);
                if(tick==0){measure(firstMoving,poses[referenceIndex],true);
                    save(lab.ReadbackSurfaceCacheTemporalGi(),std::string(viewName)+"_first_move");}
            } else {
                Pose current;current.camera=*frame.camera;makeMask(current);measure(moving,current,false);
            }
            if(tick==15)save(lab.ReadbackSurfaceCacheTemporalGi(),std::string(viewName)+"_peak_move");
        }
        report(viewName,"first_move",firstMoving);report(viewName,"continuous_motion",moving);
        Check(moving.frames==32&&moving.qualityFrames>=8,"Edge motion did not exercise continuous movement and reference checkpoints");
        Check(viewHistoryReused>=16,"Edge camera path did not exercise sustained history reuse");
    }
    Check(totalCurvedPixels>100&&totalWallPixels>100,"Edge fixture omitted curved or wall-side silhouette receivers");
    if(!options.surfaceCacheEdgeDiagnosticsOnly&&!options.surfaceCacheFeatureAblation)
        Check(historyReusedFrames>=32,"Edge fixture failed to reuse history while continuously moving");
    if(options.surfaceCacheFeatureAblation)
        Check(referencePoseCount==4&&ablationCases==10&&ablationFrames==50&&ablationStationaryFrames==40&&ablationMovingFrames==10&&
            ablationWarmupFrames==160&&ablationHistoryReused==50&&ablationMovingHistoryReused==10,
            "Feature ablation did not complete both reference poses and all static/first-moving feature cases per view");
    frame.effects.areaLights.lights[0].enabled=false;render();
    const auto black=lab.ReadbackSurfaceCacheRawGi(),accumulated=lab.ReadbackSurfaceCacheTemporalGi();
    finite(black,"lamp-off raw");finite(accumulated,"lamp-off temporal");
    double blackEnergy=0,historyEnergy=0;std::uint64_t readableBlack=0;
    for(std::size_t i=0;i<pixels;++i){blackEnergy+=glm::length(glm::dvec3(black[i]));
        historyEnergy+=glm::length(glm::dvec3(accumulated[i]));readableBlack+=black[i].w>.5f;}
    Check(blackEnergy<1e-5&&historyEnergy<1e-5,"Edge pipeline invented or retained light after lamp-off");
    Check(readableBlack>100,"Edge pipeline classified known black samples as unavailable");
    std::cout<<"SURFACE CACHE EDGE COMPLETE size="<<frame.width<<"x"<<frame.height
        <<" probe_spacing=32 probe_rays=64 reference_spp="<<referenceSeeds*32
        <<" diagnostics_only="<<options.surfaceCacheEdgeDiagnosticsOnly
        <<" feature_ablation="<<options.surfaceCacheFeatureAblation<<" reference_unique_poses="<<referencePoseCount
        <<" continuous_motion_frames="<<(options.surfaceCacheEdgeDiagnosticsOnly||options.surfaceCacheFeatureAblation?0:64)<<" history_reused_frames="<<historyReusedFrames
        <<" ablation_cases="<<ablationCases<<" ablation_measured_frames="<<ablationFrames<<" ablation_warmup_frames="<<ablationWarmupFrames
        <<" ablation_stationary_measured_frames="<<ablationStationaryFrames<<" ablation_moving_measured_frames="<<ablationMovingFrames
        <<" ablation_history_reused_frames="<<ablationHistoryReused
        <<" ablation_moving_history_reused_frames="<<ablationMovingHistoryReused
        <<" curved_edge_pixels="<<totalCurvedPixels<<" wall_edge_pixels="<<totalWallPixels
        <<" readable_black="<<readableBlack<<" lamp_off_raw="<<blackEnergy<<" lamp_off_temporal="<<historyEnergy
        <<" synthetic_guards=passed quality_metrics=paired_reference_review"<<std::endl;
}
