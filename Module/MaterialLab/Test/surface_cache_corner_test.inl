// The reference bypasses probe placement, integration and reconstruction:
// independent per-pixel triangle-BVH rays read the same completed light cache.
// All quality metrics use linear HDR and subtract the per-pose reference, so a
// physically dark wall/ceiling junction is not mistaken for an artifact.
void SurfaceCacheCornerQuality(MaterialLabModule& lab,const Options& options) {
    const bool filtered=options.surfaceCacheCornerFiltered;
    auto fixture=options;fixture.sealedGiRoom=true;fixture.sdfBanquet=false;
    fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);
    frame.width=options.renderSizeSet?options.benchmarkWidth:320;
    frame.height=options.renderSizeSet?options.benchmarkHeight:200;
    frame.effects.outputSize={0,0};frame.present=false;frame.screenProbeStudyHud=false;
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;
    auto& trace=placement.trace;gi.enabled=true;gi.useScreenProbes=true;gi.view=2;gi.probeDiagnostics=false;
    gi.temporal.enabled=false;gi.spatial.enabled=false;
    placement.enabled=true;placement.adaptive=true;placement.baseSpacing=32;placement.candidateCount=8;
    placement.frameIndex=0;placement.freeze=false;placement.reset=true;
    trace.rayResolution=8;trace.cosineHemisphere=true;trace.samplingSeed=0;
    trace.maxSteps=512;trace.sdf.maxSteps=256;
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());
        frame.effects.cameraCut=false;placement.reset=false;gi.temporal.reset=false;};
    const auto pixels=std::size_t(frame.width)*frame.height;
    const glm::dvec3 luminance(.2126,.7152,.0722);
    const std::array<const char*,2> regionNames{{"ceiling","walls"}};
    const auto meanMetric=[](double sum,std::uint64_t count){
        return count?sum/double(count):std::numeric_limits<double>::quiet_NaN();};
    const auto finite=[&](const auto& image,const char* label){
        Check(image.size()==pixels,std::string("Missing corner fixture ")+label+": "+lab.LastError());
        for(const auto& p:image)for(unsigned c=0;c<4;++c)
            if(!std::isfinite(p[c]))throw std::runtime_error(std::string("Nonfinite corner fixture ")+label);
    };
    // PNGs are a consistent visualization of the offline HDR means. This
    // exposure/tone curve affects presentation only, never the metrics.
    const auto saveMean=[&](const auto& hdr,const std::string& label){
        if(options.screenshot.empty())return;
        FramePixels image;image.width=frame.width;image.height=frame.height;image.rgba.resize(pixels*4);
        for(std::size_t i=0;i<pixels;++i){for(unsigned c=0;c<3;++c){
            const double linear=std::max(0.,double(hdr[i][c]))*4.;
            const double mapped=linear/(1.+linear);
            const double srgb=mapped<=.0031308?12.92*mapped:1.055*std::pow(mapped,1./2.4)-.055;
            image.rgba[i*4+c]=std::uint8_t(std::lround(std::clamp(srgb,0.,1.)*255.));
        }image.rgba[i*4+3]=255;}
        auto path=std::filesystem::path(options.screenshot);
        path.replace_filename(path.stem().string()+"_"+label+path.extension().string());Save(image,path.string());
    };
    struct Pose {
        Render::PipelineCamera camera;
        std::vector<glm::dvec3> reference;
        std::vector<glm::vec3> world,normal;
        std::vector<int> surface;
        std::vector<std::uint8_t> corner,valid;
        std::vector<std::size_t> patchOrigins;
        std::size_t corners=0;
    };
    std::array<Pose,2> poses;
    auto camera=SealedGiRoomCameraPreset(0,frame.OutputAspect());
    camera.position={2.8f,2.65f,2.9f};
    camera.view=glm::lookAt(camera.position,glm::vec3(-1.2f,4.2f,-1.4f),glm::vec3(0,1,0));
    poses[0].camera=camera;poses[1].camera=camera;
    const glm::vec3 displacement(.04f,0,0);
    poses[1].camera.position+=displacement;
    poses[1].camera.view=poses[1].camera.view*glm::translate(glm::mat4(1),-displacement);
    frame.camera=camera;placement.cornerRefinement=true;gi.cacheBilinear=true;
    const auto sourceDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
    do{render();Check(std::chrono::steady_clock::now()<sourceDeadline,"Corner fixture SDF preparation timed out");}
    while(lab.ProbeSdfQualityStatistics().pending);

    const auto surfaceId=[](glm::vec3 p,glm::vec3 n){
        if(std::abs(p.x-SealedGiRoomMinimum.x)<.01f&&n.x>.999f)return 0;
        if(std::abs(p.x-SealedGiRoomMaximum.x)<.01f&&n.x<-.999f)return 1;
        if(std::abs(p.z-SealedGiRoomMinimum.z)<.01f&&n.z>.999f)return 2;
        if(std::abs(p.z-SealedGiRoomMaximum.z)<.01f&&n.z<-.999f)return 3;
        if(std::abs(p.y-SealedGiRoomMaximum.y)<.01f&&n.y<-.999f)return 4;
        return -1;
    };
    const unsigned referenceSamples=options.surfaceCacheCornerFiltered?32u:(pixels>512000?4u:8u);
    std::uint64_t totalCornerPixels=0;
    for(unsigned poseIndex=0;poseIndex<poses.size();++poseIndex){auto& pose=poses[poseIndex];
        frame.camera=pose.camera;frame.effects.cameraCut=true;gi.useScreenProbes=true;placement.enabled=true;
        placement.reset=true;placement.frameIndex=0;trace.samplingSeed=0;render();
        const auto sources=lab.ReadbackScreenProbeTrace();
        Check(sources.sourceViewPositions.size()==pixels&&sources.sourceWorldNormals.size()==pixels,
            "Corner fixture lost its receiver GBuffer");
        pose.world.resize(pixels);pose.normal.resize(pixels);pose.surface.assign(pixels,-1);
        pose.valid.resize(pixels);pose.corner.resize(pixels);
        const auto inverseView=glm::inverse(pose.camera.view);
        std::vector<std::uint8_t> band(pixels);
        for(std::size_t i=0;i<pixels;++i){const auto p=sources.sourceViewPositions[i],n=sources.sourceWorldNormals[i];
            if(p.w<.5f||n.w<.5f||glm::dot(glm::vec3(n),glm::vec3(n))<.5f)continue;
            pose.valid[i]=1;pose.world[i]=glm::vec3(inverseView*glm::vec4(glm::vec3(p),1));
            pose.normal[i]=glm::normalize(glm::vec3(n));const auto w=pose.world[i];
            const int surface=surfaceId(w,pose.normal[i]);pose.surface[i]=surface;
            if(surface<0)continue;
            // Exclude the exact raster seam and select both planar sides of
            // the same physical junction. Ceiling distance is to its walls.
            const float distance=surface==4?std::min(std::min(w.x-SealedGiRoomMinimum.x,SealedGiRoomMaximum.x-w.x),
                std::min(w.z-SealedGiRoomMinimum.z,SealedGiRoomMaximum.z-w.z)):SealedGiRoomMaximum.y-w.y;
            band[i]=distance>.025f&&distance<.65f;
        }
        for(unsigned y=1;y+1<frame.height;++y)for(unsigned x=1;x+1<frame.width;++x){
            const auto i=std::size_t(y)*frame.width+x;if(!band[i])continue;bool same=true;
            for(int dy=-1;dy<=1&&same;++dy)for(int dx=-1;dx<=1;++dx)
                if(pose.surface[std::size_t(int(y)+dy)*frame.width+unsigned(int(x)+dx)]!=pose.surface[i]){same=false;break;}
            if(same){pose.corner[i]=1;++pose.corners;}
        }
        Check(pose.corners>100,"Corner fixture has insufficient same-plane wall/ceiling junction receivers");
        // Accept a block only when all 64 samples are inside the original
        // corner ROI and on one planar surface. No partial blocks, lowered
        // coverage threshold or cross-edge averages enter the patch metric.
        for(unsigned y=0;y+8<=frame.height;y+=8)for(unsigned x=0;x+8<=frame.width;x+=8){
            const auto origin=std::size_t(y)*frame.width+x;const int surface=pose.surface[origin];
            if(surface<0||!pose.corner[origin])continue;bool complete=true;
            for(unsigned dy=0;dy<8&&complete;++dy)for(unsigned dx=0;dx<8;++dx){
                const auto at=origin+std::size_t(dy)*frame.width+dx;
                if(!pose.corner[at]||pose.surface[at]!=surface){complete=false;break;}
            }
            if(complete)pose.patchOrigins.push_back(origin);
        }
        Check(!pose.patchOrigins.empty(),"Corner fixture has no complete same-plane 8x8 ROI blocks");
        totalCornerPixels+=pose.corners;
        gi.useScreenProbes=false;placement.enabled=false;gi.raysPerPixel=32;gi.cacheBilinear=true;
        pose.reference.assign(pixels,glm::dvec3(0));
        std::vector<glm::dvec3> firstHalf(pixels,glm::dvec3(0)),secondHalf(pixels,glm::dvec3(0));
        std::vector<glm::vec4> firstReference;double seedDifference=0;
        for(unsigned sample=0;sample<referenceSamples;++sample){
            trace.samplingSeed=1009+73*sample;render();const auto image=lab.ReadbackSurfaceCacheRawGi();finite(image,"per-pixel reference");
            if(sample==0)firstReference=image;
            if(sample==1)for(std::size_t i=0;i<pixels;++i)
                seedDifference+=glm::length(glm::dvec3(image[i])-glm::dvec3(firstReference[i]));
            auto& half=sample<referenceSamples/2?firstHalf:secondHalf;
            for(std::size_t i=0;i<pixels;++i){pose.reference[i]+=glm::dvec3(image[i])/double(referenceSamples);
                half[i]+=glm::dvec3(image[i])/double(referenceSamples/2);}
        }
        Check(seedDifference>1e-5,"Per-pixel reference sampling seed did not change traced lighting");
        double referenceUncertainty=0,referenceMean=0;
        std::array<double,2> regionReferenceUncertainty{},regionReferenceMean{};
        std::array<std::uint64_t,2> regionReferenceCount{};
        for(std::size_t i=0;i<pixels;++i)if(pose.corner[i]){
            const double delta=glm::dot(firstHalf[i]-secondHalf[i],luminance);
            referenceUncertainty+=delta*delta;referenceMean+=glm::dot(pose.reference[i],luminance);
            const unsigned region=pose.surface[i]==4?0u:1u;
            regionReferenceUncertainty[region]+=delta*delta;
            regionReferenceMean[region]+=glm::dot(pose.reference[i],luminance);++regionReferenceCount[region];
        }
        Check(referenceMean/double(pose.corners)>1e-5,"Corner fixture has no measurable one-bounce indirect illumination");
        std::cout<<"SURFACE CACHE CORNER reference pose="<<poseIndex<<" spp="<<referenceSamples*32
            <<" seed_difference="<<seedDifference<<" half_difference_mse="<<referenceUncertainty/double(pose.corners)
            <<" mean="<<referenceMean/double(pose.corners)<<" pixels="<<pose.corners
            <<" complete_8x8_patches="<<pose.patchOrigins.size()<<std::endl;
        for(unsigned region=0;region<regionNames.size();++region)
            std::cout<<"SURFACE CACHE CORNER reference_region pose="<<poseIndex<<" region="<<regionNames[region]
                <<" spp="<<referenceSamples*32
                <<" half_difference_mse="<<meanMetric(regionReferenceUncertainty[region],regionReferenceCount[region])
                <<" mean="<<meanMetric(regionReferenceMean[region],regionReferenceCount[region])
                <<" pixels="<<regionReferenceCount[region]<<std::endl;
        saveMean(pose.reference,std::string("reference_")+std::to_string(poseIndex));
    }
    struct Candidate {const char* label;bool corner,bilinear;float support=.5f;};
    constexpr std::array<Candidate,7> candidates{{
        {"before",false,false},{"geometry_only",true,false},{"cache_only",false,true},{"after",true,true},
        {"after_support_075",true,true,.75f},{"after_support_100",true,true,1.f},
        {"after_support_125",true,true,1.25f}}};
    struct RegionMetrics {
        double lumaSquared=0,darkSquared=0,brightSquared=0,bias=0;
        double patchSquared=0,darkPatchSquared=0,brightPatchSquared=0;
        std::uint64_t count=0,patchCount=0;
    };
    struct Metrics {
        double rgbSquared=0,lumaSquared=0,darkSquared=0,brightSquared=0,bias=0;
        double phaseDelta=0,motionDelta=0,readability=0;
        double sourceLumaSquared=0,spatialLumaSquared=0;
        double firstMoveSquared=0,firstMoveDarkSquared=0,firstMoveSourceSquared=0;
        double patchSquared=0,darkPatchSquared=0,brightPatchSquared=0;
        std::uint64_t count=0,unsupported=0,phasePairs=0,motionPairs=0,activeAdaptive=0;
        std::uint64_t patchCount=0;
        std::uint64_t firstMoveCount=0;
        bool firstMoveHistoryUsed=false;
        std::array<RegionMetrics,2> regions;
    };
    std::array<Metrics,candidates.size()> metrics;
    constexpr unsigned phases=4;
    gi.useScreenProbes=true;placement.enabled=true;
    gi.temporal.enabled=filtered;gi.spatial.enabled=filtered;
    const unsigned candidateCount=options.surfaceCacheCornerSweep?unsigned(candidates.size()):4u;
    for(unsigned candidateIndex=0;candidateIndex<candidateCount;++candidateIndex){
        const auto& candidate=candidates[candidateIndex];auto& metric=metrics[candidateIndex];
        placement.cornerRefinement=candidate.corner;gi.cacheBilinear=candidate.bilinear;
        placement.cornerProbeSupport=candidate.support;
        std::array<std::vector<float>,phases> stationaryError;
        for(unsigned poseIndex=0;poseIndex<poses.size();++poseIndex){const auto& pose=poses[poseIndex];
            frame.camera=pose.camera;frame.effects.cameraCut=!filtered||poseIndex==0;
            placement.reset=!filtered||poseIndex==0;
            if(filtered){
                // The viewer keeps placement phase/seed fixed. Its completed
                // temporal sequence advances the emitted ray directions.
                placement.frameIndex=0;trace.samplingSeed=0;
                gi.temporal.reset=poseIndex==0;
                for(unsigned warm=0;warm<16;++warm){render();
                    if(poseIndex==1&&warm==0){
                        // Record the actual camera-motion transient before
                        // settling this pose, not just its converged image.
                        const auto moved=lab.ReadbackSurfaceCacheTemporalGi(),source=lab.ReadbackSurfaceCacheRawGi();
                        finite(moved,"first moved accumulated GI");finite(source,"first moved source GI");
                        metric.firstMoveHistoryUsed=lab.PipelineStatistics().surfaceCacheHistoryUsed;
                        for(std::size_t i=0;i<pixels;++i)if(pose.corner[i]){
                            const double e=glm::dot(glm::dvec3(moved[i])-pose.reference[i],luminance);
                            const double raw=glm::dot(glm::dvec3(source[i])-pose.reference[i],luminance);
                            metric.firstMoveSquared+=e*e;metric.firstMoveDarkSquared+=std::min(e,0.)*std::min(e,0.);
                            metric.firstMoveSourceSquared+=raw*raw;++metric.firstMoveCount;
                        }
                    }
                }
                Check(lab.PipelineStatistics().surfaceCacheHistoryUsed,"Filtered corner candidate failed to accumulate its settled GI history");
            }
            std::vector<glm::dvec3> mean(pixels,glm::dvec3(0));std::vector<float> previousError;
            for(unsigned phase=0;phase<phases;++phase){
                if(!filtered){placement.frameIndex=phase*2;trace.samplingSeed=7+12*phase;}
                render();const auto source=lab.ReadbackSurfaceCacheRawGi();
                const auto image=filtered?lab.ReadbackSurfaceCacheTemporalGi():source;
                const auto spatial=filtered?lab.ReadbackSurfaceCacheSpatialGi():source;
                const auto coverage=lab.ReadbackSurfaceCacheProbeCoverage();
                finite(source,"candidate source indirect light");finite(spatial,"candidate spatial indirect light");
                finite(image,"candidate indirect light");finite(coverage,"candidate geometric coverage");
                const auto probes=lab.ReadbackScreenProbePlacement();
                metric.activeAdaptive+=std::count_if(probes.spawned.begin(),probes.spawned.end(),[](const auto& p){return p.w>.5f;});
                std::vector<float> error(pixels);
                for(std::size_t i=0;i<pixels;++i){
                    mean[i]+=glm::dvec3(image[i])/double(phases);
                    if(!pose.valid[i])Check(glm::length(glm::vec3(image[i]))<1e-6f,"Corner reconstruction leaked GI into invalid geometry");
                    const auto world=pose.world[i];
                    if(pose.valid[i]&&std::abs(world.y-SealedGiRoomEmitterCenter.y)<.005f&&
                        std::abs(world.x-SealedGiRoomEmitterCenter.x)<.85f&&std::abs(world.z-SealedGiRoomEmitterCenter.z)<.85f&&
                        pose.normal[i].y<-.99f)
                        Check(glm::length(glm::vec3(image[i]))<1e-6f,"Corner reconstruction assigned indirect light to the zero-albedo emitter");
                    if(!pose.corner[i])continue;
                    const auto delta=glm::dvec3(image[i])-pose.reference[i];const double e=glm::dot(delta,luminance);
                    const double rawError=glm::dot(glm::dvec3(source[i])-pose.reference[i],luminance);
                    const double spatialError=glm::dot(glm::dvec3(spatial[i])-pose.reference[i],luminance);
                    metric.sourceLumaSquared+=rawError*rawError;metric.spatialLumaSquared+=spatialError*spatialError;
                    error[i]=float(e);++metric.count;metric.rgbSquared+=glm::dot(delta,delta)/3.;
                    metric.lumaSquared+=e*e;metric.darkSquared+=std::min(e,0.)*std::min(e,0.);
                    metric.brightSquared+=std::max(e,0.)*std::max(e,0.);metric.bias+=e;metric.readability+=source[i].w;
                    auto& region=metric.regions[pose.surface[i]==4?0:1];
                    region.lumaSquared+=e*e;region.darkSquared+=std::min(e,0.)*std::min(e,0.);
                    region.brightSquared+=std::max(e,0.)*std::max(e,0.);region.bias+=e;++region.count;
                    if(coverage[i].w<.5f)++metric.unsupported;
                    if(!previousError.empty()){const double d=e-previousError[i];metric.phaseDelta+=d*d;++metric.phasePairs;}
                }
                // Subtract the physically varying reference per pixel first.
                // Squaring the mean residual detects broad dark/bright spots,
                // while preserving the genuine corner illumination gradient.
                for(const auto origin:pose.patchOrigins){double sum=0;
                    for(unsigned dy=0;dy<8;++dy)for(unsigned dx=0;dx<8;++dx){
                        const auto at=origin+std::size_t(dy)*frame.width+dx;
                        sum+=glm::dot(glm::dvec3(image[at])-pose.reference[at],luminance);
                    }
                    const double e=sum/64.;metric.patchSquared+=e*e;
                    metric.darkPatchSquared+=std::min(e,0.)*std::min(e,0.);
                    metric.brightPatchSquared+=std::max(e,0.)*std::max(e,0.);++metric.patchCount;
                    auto& region=metric.regions[pose.surface[origin]==4?0:1];
                    region.patchSquared+=e*e;region.darkPatchSquared+=std::min(e,0.)*std::min(e,0.);
                    region.brightPatchSquared+=std::max(e,0.)*std::max(e,0.);++region.patchCount;
                }
                if(poseIndex==0)stationaryError[phase]=error;
                else {
                    // Match a world point to the stationary image. All four
                    // taps must belong to the same planar corner ROI, so
                    // reprojection never averages across a geometric edge.
                    const auto previousVp=poses[0].camera.projection*poses[0].camera.view;
                    for(std::size_t i=0;i<pixels;++i)if(pose.corner[i]){
                        const auto clip=previousVp*glm::vec4(pose.world[i],1);if(clip.w<=0)continue;
                        const auto xy=(glm::vec2(clip)/clip.w*.5f+.5f)*glm::vec2(frame.width,frame.height)-.5f;
                        const auto origin=glm::ivec2(glm::floor(xy));
                        if(origin.x<0||origin.y<0||origin.x+1>=int(frame.width)||origin.y+1>=int(frame.height))continue;
                        const auto fraction=glm::fract(xy);double oldError=0;bool compatible=true;
                        for(unsigned corner=0;corner<4;++corner){const unsigned dx=corner%2,dy=corner/2;
                            const auto at=std::size_t(origin.y+int(dy))*frame.width+unsigned(origin.x+int(dx));
                            if(!poses[0].corner[at]||poses[0].surface[at]!=pose.surface[i]||
                                glm::dot(poses[0].normal[at],pose.normal[i])<.995f||
                                std::abs(glm::dot(poses[0].world[at]-pose.world[i],pose.normal[i]))>.003f){compatible=false;break;}
                            const double weight=(dx?fraction.x:1-fraction.x)*(dy?fraction.y:1-fraction.y);
                            oldError+=double(stationaryError[phase][at])*weight;
                        }
                        if(compatible){const double delta=double(error[i])-oldError;metric.motionDelta+=delta*delta;++metric.motionPairs;}
                    }
                }
                previousError=std::move(error);
            }
            saveMean(mean,std::string(candidate.label)+"_"+std::to_string(poseIndex));
        }
        Check(metric.count>1000&&metric.phasePairs>100&&metric.motionPairs>100&&metric.patchCount>0,
            "Corner comparison has insufficient phase/motion overlap or complete 8x8 ROI blocks");
        auto performanceFrame=frame;performanceFrame.effects.surfaceCacheGi.collectProbeCoverage=false;
        const auto timing=lab.MeasureFrames(performanceFrame,12);
        Check(std::isfinite(timing.gpuMilliseconds)&&timing.gpuMilliseconds>0&&
            std::isfinite(timing.elapsedMilliseconds)&&timing.elapsedMilliseconds>0,"Invalid corner fixture timing");
        Check(!lab.PipelineStatistics().surfaceCacheProbeCoverageCollected,
            "Performance frames unexpectedly collected probe coverage diagnostics");
        Check(lab.ReadbackSurfaceCacheProbeCoverage().empty(),"Uncollected probe coverage returned stale pixels");
        if(filtered)Check(lab.PipelineStatistics().surfaceCacheHistoryUsed,
            "Disabling coverage diagnostics unexpectedly reset GI history");
        std::cout<<"SURFACE CACHE CORNER PERF candidate="<<candidate.label<<" filtered="<<filtered
            <<" support="<<candidate.support
            <<" gpu_ms="<<timing.gpuMilliseconds<<" elapsed_ms="<<timing.elapsedMilliseconds
            <<" frames=12 readback=0 present=0 coverage_diagnostic=0"<<std::endl;
        std::cout<<"SURFACE CACHE CORNER candidate="<<candidate.label<<" filtered="<<filtered<<" corner_refinement="<<candidate.corner
            <<" cache_bilinear="<<candidate.bilinear<<" support="<<candidate.support<<" rgb_mse="<<metric.rgbSquared/double(metric.count)
            <<" luma_mse="<<metric.lumaSquared/double(metric.count)<<" dark_mse="<<metric.darkSquared/double(metric.count)
            <<" bright_mse="<<metric.brightSquared/double(metric.count)<<" signed_bias="<<metric.bias/double(metric.count)
            <<" unsupported="<<metric.unsupported<<" samples="<<metric.count<<" mean_readability="<<metric.readability/double(metric.count)
            <<" phase_error_delta_mse="<<metric.phaseDelta/double(metric.phasePairs)
            <<" motion_error_delta_mse="<<metric.motionDelta/double(metric.motionPairs)
            <<" motion_pairs="<<metric.motionPairs<<" active_adaptive_per_frame="<<double(metric.activeAdaptive)/double(poses.size()*phases)
            <<" source_luma_mse="<<metric.sourceLumaSquared/double(metric.count)
            <<" spatial_luma_mse="<<metric.spatialLumaSquared/double(metric.count)
            <<" patch_mse="<<metric.patchSquared/double(metric.patchCount)
            <<" dark_patch_mse="<<metric.darkPatchSquared/double(metric.patchCount)
            <<" bright_patch_mse="<<metric.brightPatchSquared/double(metric.patchCount)
            <<" patch_samples="<<metric.patchCount<<" patch_size=8 patch_pixels=64"
            <<std::endl;
        for(unsigned region=0;region<regionNames.size();++region){const auto& r=metric.regions[region];
            std::cout<<"SURFACE CACHE CORNER candidate="<<candidate.label<<" filtered="<<filtered
                <<" support="<<candidate.support<<" region="<<regionNames[region]<<" luma_mse="<<meanMetric(r.lumaSquared,r.count)
                <<" dark_mse="<<meanMetric(r.darkSquared,r.count)<<" bright_mse="<<meanMetric(r.brightSquared,r.count)
                <<" signed_bias="<<meanMetric(r.bias,r.count)<<" samples="<<r.count
                <<" patch_mse="<<meanMetric(r.patchSquared,r.patchCount)
                <<" dark_patch_mse="<<meanMetric(r.darkPatchSquared,r.patchCount)
                <<" bright_patch_mse="<<meanMetric(r.brightPatchSquared,r.patchCount)
                <<" patch_samples="<<r.patchCount<<" patch_size=8 patch_pixels=64"<<std::endl;
        }
        if(filtered){Check(metric.firstMoveCount>100&&metric.firstMoveHistoryUsed,
                "Filtered corner motion failed to exercise real history reprojection");
            std::cout<<"SURFACE CACHE CORNER FIRST_MOVE candidate="<<candidate.label
                <<" filtered=1 support="<<candidate.support<<" camera_cut=0 history_used="<<metric.firstMoveHistoryUsed
                <<" source_luma_mse="<<metric.firstMoveSourceSquared/double(metric.firstMoveCount)
                <<" luma_mse="<<metric.firstMoveSquared/double(metric.firstMoveCount)
                <<" dark_mse="<<metric.firstMoveDarkSquared/double(metric.firstMoveCount)
                <<" pixels="<<metric.firstMoveCount<<std::endl;
        }
    }
    // A dark-room guard is independent of subjective smoothness. Neither new
    // cache interpolation nor corner refinement may manufacture ambient light.
    frame.effects.areaLights.lights[0].enabled=false;
    for(unsigned candidateIndex:{0u,3u}){
        placement.cornerRefinement=candidates[candidateIndex].corner;
        placement.cornerProbeSupport=candidates[candidateIndex].support;
        gi.cacheBilinear=candidates[candidateIndex].bilinear;render();const auto dark=lab.ReadbackSurfaceCacheRawGi();finite(dark,"lamp-off GI");
        double darkEnergy=0;std::uint64_t readableBlack=0;
        for(const auto& p:dark){darkEnergy+=glm::length(glm::dvec3(p));readableBlack+=p.w>.5f;}
        Check(darkEnergy<1e-5,"Corner reconstruction fabricated light after the sole emitter was switched off");
        Check(readableBlack>100,"Lamp-off GI lost valid black samples by classifying them as unreadable");
        if(filtered){const auto accumulated=lab.ReadbackSurfaceCacheTemporalGi();finite(accumulated,"lamp-off accumulated GI");
            double historyEnergy=0;for(const auto& p:accumulated)historyEnergy+=glm::length(glm::dvec3(p));
            Check(historyEnergy<1e-5,"Filtered corner reconstruction retained illumination after switching off the sole emitter");}
    }
    // Improvement percentages are reported rather than hardcoded here: the
    // full-resolution run and a visual review determine acceptance. In
    // particular, brightness increases alone never establish correctness.
    std::cout<<"SURFACE CACHE CORNER COMPLETE size="<<frame.width<<"x"<<frame.height
        <<" corner_pixels="<<totalCornerPixels<<" reference_spp="<<referenceSamples*32
        <<" support="<<candidates[3].support
        <<" probe_spacing=32 probe_rays=64 temporal="<<filtered<<" spatial="<<filtered<<" dark_guard=passed"
        <<" mse_ratio="<<metrics[3].lumaSquared/std::max(metrics[0].lumaSquared,1e-30)
        <<" dark_mse_ratio="<<metrics[3].darkSquared/std::max(metrics[0].darkSquared,1e-30)
        <<" bright_mse_ratio="<<metrics[3].brightSquared/std::max(metrics[0].brightSquared,1e-30)
        <<" patch_mse_ratio="<<metrics[3].patchSquared/std::max(metrics[0].patchSquared,1e-30)
        <<" dark_patch_mse_ratio="<<metrics[3].darkPatchSquared/std::max(metrics[0].darkPatchSquared,1e-30)
        <<" bright_patch_mse_ratio="<<metrics[3].brightPatchSquared/std::max(metrics[0].brightPatchSquared,1e-30)
        <<" patch_samples="<<metrics[3].patchCount
        <<" phase_delta_ratio="<<metrics[3].phaseDelta/std::max(metrics[0].phaseDelta,1e-30)
        <<" motion_delta_ratio="<<metrics[3].motionDelta/std::max(metrics[0].motionDelta,1e-30)
        <<std::endl;
    for(unsigned region=0;region<regionNames.size();++region){
        const auto& before=metrics[0].regions[region];const auto& after=metrics[3].regions[region];
        const auto ratio=[&](double a,std::uint64_t aCount,double b,std::uint64_t bCount){
            return aCount&&bCount?meanMetric(a,aCount)/std::max(meanMetric(b,bCount),1e-30):
                std::numeric_limits<double>::quiet_NaN();};
        std::cout<<"SURFACE CACHE CORNER REGION COMPLETE region="<<regionNames[region]<<" filtered="<<filtered
            <<" support="<<candidates[3].support
            <<" mse_ratio="<<ratio(after.lumaSquared,after.count,before.lumaSquared,before.count)
            <<" dark_mse_ratio="<<ratio(after.darkSquared,after.count,before.darkSquared,before.count)
            <<" bright_mse_ratio="<<ratio(after.brightSquared,after.count,before.brightSquared,before.count)
            <<" patch_mse_ratio="<<ratio(after.patchSquared,after.patchCount,before.patchSquared,before.patchCount)
            <<" dark_patch_mse_ratio="<<ratio(after.darkPatchSquared,after.patchCount,before.darkPatchSquared,before.patchCount)
            <<" bright_patch_mse_ratio="<<ratio(after.brightPatchSquared,after.patchCount,before.brightPatchSquared,before.patchCount)
            <<" samples="<<after.count<<" patch_samples="<<after.patchCount<<std::endl;
    }
}
