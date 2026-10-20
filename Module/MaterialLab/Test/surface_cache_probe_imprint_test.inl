// Include after surface_cache_geometry_quality_test.inl: its independent
// double-precision primary-ray oracle supplies actual raster object ownership.
// All quality ROIs are geometric. Production masks never select receivers.
void SurfaceCacheProbeImprintQuality(MaterialLabModule& lab,const Options& options) {
    using SurfaceCacheGeometryFixture::Oracle;
    Check(options.vulkan,"Probe-imprint quality requires Vulkan");
    const bool convergence=options.surfaceCacheProbeImprintConvergence;
    auto fixture=options;fixture.sealedGiRoom=true;fixture.sdfBanquet=false;fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);frame.width=options.renderSizeSet?options.benchmarkWidth:640;
    frame.height=options.renderSizeSet?options.benchmarkHeight:400;frame.effects.outputSize={0,0};frame.present=false;frame.screenProbeStudyHud=false;
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::None;
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;auto& trace=placement.trace;
    gi.enabled=true;gi.useScreenProbes=true;gi.view=0;gi.reconstructionDebug=0;gi.probeDiagnostics=false;gi.probeOriginDiagnostics=false;
    gi.collectProbeCoverage=false;gi.spatial.enabled=true;gi.temporal.enabled=true;gi.contact.enabled=true;
    placement.enabled=true;placement.adaptive=true;placement.cornerRefinement=true;placement.freeze=false;placement.frameIndex=0;placement.candidateCount=8;
    trace.cosineHemisphere=true;trace.samplingSeed=0;trace.maxSteps=512;trace.sdf.maxSteps=256;
    const auto baselineGi=gi;const auto baselinePlacement=placement;
    constexpr unsigned stageCount=5,unfilteredStage=2,filteredStage=3,temporalStage=4,regions=9,warm=16,measured=8;
    constexpr std::array<unsigned,4> periods{{4,8,16,32}};
    constexpr std::array<const char*,stageCount> stageNames{{"probe_raw","probe_spatial","contact_unfiltered","contact_filtered","temporal"}};
    constexpr std::array<const char*,regions> regionNames{{"ellipse_core_0to3cm","ellipse_transition_3to35cm","ellipse_shell_over35cm",
        "prism_core_0to3cm","prism_transition_3to35cm","prism_shell_over35cm","wall_core_0to3cm","wall_transition_3to35cm","wall_interior_over35cm"}};
    constexpr std::array<const char*,3> viewNames{{"oval_close","contact_near","wall_corner"}},variantNames{{"previous","improved","contact_off"}};
    const auto pixels=std::size_t(frame.width)*frame.height;const glm::dvec3 luma(.2126,.7152,.0722);
    const auto mean=[](double value,std::uint64_t count){return value/double(std::max<std::uint64_t>(1,count));};
    const auto squared=[](glm::dvec3 value){return glm::dot(value,value)/3.;};
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;placement.reset=false;gi.temporal.reset=false;};
    const auto reset=[&]{frame.effects.cameraCut=true;placement.reset=true;gi.temporal.reset=true;};
    const auto finite=[&](const auto& image,const char* label){Check(image.size()==pixels,std::string("Missing probe-imprint ")+label+": "+lab.LastError());
        for(const auto& p:image)for(unsigned c=0;c<4;++c)Check(std::isfinite(p[c]),"Nonfinite probe-imprint image");};
    const auto path=[&](const std::string& label,const char* extension=".png"){auto p=std::filesystem::path(options.screenshot);
        p.replace_filename(p.stem().string()+"_"+label+extension);return p.string();};
    const auto save=[&](const auto& hdr,const std::string& label,bool diagnostic=false){if(options.screenshot.empty())return;
        FramePixels image;image.width=frame.width;image.height=frame.height;image.rgba.resize(pixels*4);
        for(std::size_t i=0;i<pixels;++i){for(unsigned c=0;c<3;++c){double v=std::max(0.,double(hdr[i][c]));
            if(!diagnostic){v*=4;v=v/(1+v);v=v<=.0031308?12.92*v:1.055*std::pow(v,1/2.4)-.055;}
            image.rgba[4*i+c]=std::uint8_t(std::lround(std::clamp(v,0.,1.)*255));}image.rgba[4*i+3]=255;}Save(image,path(label));};
    const auto& objects=SealedGiRoomObjects();unsigned ovalIndex=unsigned(objects.size()),prismIndex=ovalIndex;
    std::vector<SealedGiRoomMaterial> materials;for(unsigned i=0;i<objects.size();++i){materials.push_back(SealedGiRoomObjectMaterial(i));
        const auto& m=materials.back();for(unsigned c=0;c<3;++c)Check(std::isfinite(m.albedo[c])&&std::isfinite(m.emission[c]),"Nonfinite imprint fixture material");
        Check(std::isfinite(m.roughness),"Nonfinite imprint fixture roughness");
        if(std::string(objects[i].name)=="Porcelain upright oval")ovalIndex=i;if(std::string(objects[i].name)=="Porcelain tall prism")prismIndex=i;}
    Check(ovalIndex<objects.size()&&prismIndex<objects.size(),"Probe-imprint fixture lacks ellipsoid/prism");
    Oracle oracle;oracle.Initialize(objects,materials);
    const auto& oval=objects[ovalIndex];const auto& prism=objects[prismIndex];
    const auto ovalInverse=glm::inverse(ProbePlacementTransform(oval,ProbePlacementSceneSettings{}));
    const auto prismInverse=glm::inverse(ProbePlacementTransform(prism,ProbePlacementSceneSettings{}));
    const auto prismDistance=[&](glm::vec3 p){const auto q=glm::abs(glm::vec3(prismInverse*glm::vec4(p,1)))*prism.scale-prism.scale;
        return double(glm::length(glm::max(q,glm::vec3(0)))+std::min(std::max(q.x,std::max(q.y,q.z)),0.f));};
    const auto ovalDistance=[&](glm::vec3 world){const auto a=glm::dvec3(oval.scale),a2=a*a;
        const auto p=glm::abs(glm::dvec3(ovalInverse*glm::vec4(world,1))*a);
        if(glm::dot(p/a,p/a)<=1.)return (glm::length(p/a)-1.)*std::min({a.x,a.y,a.z});
        const auto equation=[&](double t){const auto q=a*p/(a2+t);return glm::dot(q,q)-1.;};
        double low=0,high=std::max(1.,glm::length(a*p));while(equation(high)>0)high*=2;
        for(unsigned step=0;step<40;++step){const double mid=.5*(low+high);if(equation(mid)>0)low=mid;else high=mid;}
        return glm::length(p-a2*p/(a2+.5*(low+high)));};
    const auto configure=[&](unsigned variant){gi=baselineGi;placement=baselinePlacement;trace.samplingSeed=0;placement.frameIndex=0;
        placement.curvatureAwareWeights=convergence||variant!=0;gi.contact.coherentProbeRisk=convergence||variant!=0;
        gi.temporal.staticConvergence=convergence&&variant!=0;
        gi.contact.referenceReceiver=false;gi.contact.enabled=convergence||variant!=2;};
    struct Pose {std::vector<unsigned char> region;std::vector<int> owner;std::vector<glm::vec3> normal,world;
        std::vector<float> distance;std::vector<glm::vec4> reference,split;std::array<std::uint64_t,regions> counts{};};
    struct Summary {double mse=0,floor=0,hp=0,hpFloor=0,gradient=0,gradientFloor=0,gradientP99=0,value=0,truth=0,variance=0;
        std::uint64_t count=0,hpCount=0,pairs=0;};
    struct Phase {std::array<glm::dvec3,32> sum{},split{};std::array<std::uint64_t,32> count{};};
    struct Accumulator {Summary metric;glm::dvec3 sum{},truth{},split{};std::array<std::array<Phase,2>,4> phase{};std::vector<double> gradients;};
    const unsigned beginView=options.surfaceCacheProbeImprintView<3?options.surfaceCacheProbeImprintView:0;
    const unsigned endView=options.surfaceCacheProbeImprintView<3?beginView+1:convergence?1:3;
    for(unsigned view=beginView;view<endView;++view){const std::string viewName=convergence&&view==0?"oval_frontal_static":viewNames[view];auto camera=SealedGiRoomCameraPreset(0,frame.OutputAspect());
        const std::array<glm::vec3,3> eyes{{{2.0f,1.55f,-2.15f},{2.55f,1.42f,-1.90f},{1.7f,3.10f,1.35f}}};
        const std::array<glm::vec3,3> targets{{{1.22f,1.15f,-1.10f},{1.48f,1.05f,-.72f},{3.65f,4.15f,3.15f}}};
        camera.position=convergence&&view==0?glm::vec3(1.28f,1.30f,-2.20f):eyes[view];
        const auto target=convergence&&view==0?glm::vec3(1.28f,1.10f,-1.15f):targets[view];
        camera.view=glm::lookAt(camera.position,target,glm::vec3(0,1,0));frame.camera=camera;configure(1);reset();render();
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(180);
        while(lab.ProbeSdfQualityStatistics().pending){render();Check(std::chrono::steady_clock::now()<deadline,"Probe-imprint SDF preparation timed out");}
        const auto source=lab.ReadbackScreenProbeSources();finite(source.sourceViewPositions,"positions");finite(source.sourceWorldNormals,"normals");
        Pose pose;pose.region.assign(pixels,0);pose.owner.assign(pixels,-1);pose.normal.resize(pixels);pose.world.resize(pixels);pose.distance.assign(pixels,-1);
        const auto inverse=glm::dmat4(glm::inverse(camera.view));std::uint64_t ownershipRejected=0;
        for(std::size_t i=0;i<pixels;++i){const auto vp=source.sourceViewPositions[i],nd=source.sourceWorldNormals[i];
            if(vp.w<.5f||nd.w<.5f||glm::dot(glm::vec3(nd),glm::vec3(nd))<.5f)continue;
            const auto w=glm::dvec3(inverse*glm::dvec4(glm::dvec3(vp),1)),relative=w-glm::dvec3(camera.position);const double distance=glm::length(relative);
            const auto hit=oracle.Nearest(glm::dvec3(camera.position),relative/distance,distance+.005);
            if(hit.triangle<0||std::abs(hit.t-distance)>.003){++ownershipRejected;continue;}
            const int owner=int(oracle.triangles[unsigned(hit.triangle)].object);const auto normal=glm::normalize(glm::vec3(nd));
            pose.owner[i]=owner;pose.normal[i]=normal;pose.world[i]=glm::vec3(w);double gap=-1;unsigned family=3;
            if(!view||view==1){if(owner==int(ovalIndex)){family=0;gap=prismDistance(glm::vec3(w));}
                else if(owner==int(prismIndex)){family=1;gap=ovalDistance(glm::vec3(w));}
                // Only the large smooth shell excludes the geometric grazing
                // silhouette. Contact core/transition receivers stay complete.
                if(gap>=.35&&glm::dot(glm::dvec3(normal),-relative/distance)<.25)continue;
            }else if(w.x>2.8&&w.y>3.3&&w.z>2.3){family=2;
                if(std::abs(w.x-4)<.015&&normal.x<-.99f)gap=std::min(4.5-w.y,3.5-w.z);
                else if(std::abs(w.z-3.5)<.015&&normal.z<-.99f)gap=std::min(4.5-w.y,4-w.x);
                else if(std::abs(w.y-4.5)<.015&&normal.y<-.99f)gap=std::min(4-w.x,3.5-w.z);}
            if(family>=3||gap<-.015)continue;gap=std::max(0.,gap);const unsigned r=family*3+(gap<.03?0:gap<.35?1:2);
            pose.distance[i]=float(gap);pose.region[i]=static_cast<unsigned char>(r+1);++pose.counts[r];}
        std::cout<<"PROBE IMPRINT setup view="<<viewName<<" size="<<frame.width<<'x'<<frame.height<<" placement_phase=0 seed=0 spacing="<<placement.baseSpacing
            <<" warm="<<warm<<" measured="<<measured<<" owner_oracle_rejected="<<ownershipRejected<<" geometry_roi=1 production_mask_selects_roi=0\n";
        gi.contact.referenceReceiver=true;gi.contact.raysPerPixel=32;gi.contact.filterStrength=0;gi.temporal.enabled=false;gi.spatial.enabled=false;reset();
        const unsigned referenceSeeds=options.surfaceCacheCurvedContactReferenceSeeds;pose.reference.assign(pixels,glm::vec4(0));pose.split.assign(pixels,glm::vec4(0));
        trace.samplingSeed=1009;for(unsigned relightWarm=0;relightWarm<4;++relightWarm)render();
        for(unsigned seed=0;seed<referenceSeeds;++seed){trace.samplingSeed=1009+73*seed;render();const auto reference=lab.ReadbackSurfaceCacheContactUnfilteredGi();finite(reference,"safe-origin reference");
            for(std::size_t i=0;i<pixels;++i){pose.reference[i]+=reference[i]/float(referenceSeeds);pose.split[i]+=reference[i]*((seed<referenceSeeds/2?2.f:-2.f)/float(referenceSeeds));}}
        std::uint64_t excludedUnknown=0,retained=0;for(std::size_t i=0;i<pixels;++i)if(pose.region[i]){
            if(pose.reference[i].w<=0){--pose.counts[pose.region[i]-1];pose.region[i]=0;++excludedUnknown;}else ++retained;}
        Check(retained>100,"Probe-imprint geometry ROI contains too few safe-reference receivers");
        std::cout<<"PROBE IMPRINT reference view="<<viewName<<" spp="<<referenceSeeds*32<<" split_spp="<<referenceSeeds*16
            <<" excluded_unknown="<<excludedUnknown<<" retained="<<retained<<" relight_warm_frames=4 warm_seed=1009 warm_rays_excluded=1 shared_cache=1 safe_receiver_origin=1 path_truth=0\n";
        save(pose.reference,viewName+"_reference_tone4");std::vector<glm::vec4> roiImage(pixels,glm::vec4(0,0,0,1));
        for(std::size_t i=0;i<pixels;++i)if(pose.region[i])roiImage[i][(pose.region[i]-1)/3]=.3f+.35f*float((pose.region[i]-1)%3);
        save(roiImage,viewName+"_geometry_roi_Rellipse_Gprism_Bwall",true);
        std::array<std::array<Summary,regions>,stageCount> previous{};
        const auto compatible=[&](std::size_t i,std::size_t j,float cosine){return pose.region[i]&&pose.region[j]==pose.region[i]&&pose.owner[i]==pose.owner[j]&&
            glm::dot(pose.normal[i],pose.normal[j])>cosine;};
        if(convergence){
            const unsigned endFrame=options.surfaceCacheProbeImprintConvergenceFrames;
            // Only static convergence changes in this A/B. The previous color
            // fixes stay enabled. Checkpoints are actual post-reset renders.
            using Images=std::array<std::vector<glm::vec4>,stageCount>;
            using Moments=std::array<std::vector<double>,stageCount>;
            const auto reportStatic=[&](const Images& image,const Moments& squares,unsigned samples,unsigned checkpoint,unsigned variant,
                const char* estimate,const std::vector<glm::vec4>& history){
                const std::string prefix=viewName+"_"+variantNames[variant]+"_frame"+std::to_string(checkpoint)+"_"+estimate;
                for(unsigned s=0;s<stageCount;++s){finite(image[s],"static estimate");save(image[s],prefix+"_"+stageNames[s]+"_tone4");
                    std::array<Accumulator,regions> values;std::array<double,regions> historySum{};
                    for(unsigned y=0;y<frame.height;++y)for(unsigned x=0;x<frame.width;++x){const auto i=std::size_t(y)*frame.width+x;if(!pose.region[i])continue;
                        auto& a=values[pose.region[i]-1];auto& m=a.metric;const auto rgb=glm::dvec3(image[s][i]),truth=glm::dvec3(pose.reference[i]);
                        const auto residual=rgb-truth,split=glm::dvec3(pose.split[i]);++m.count;m.mse+=squared(residual);m.floor+=.25*squared(split);
                        a.sum+=rgb;a.truth+=truth;a.split+=split;historySum[pose.region[i]-1]+=history[i].w;
                        if(samples>1){const double v=glm::dot(rgb,luma);m.variance+=std::max(0.,squares[s][i]-double(samples)*v*v)/double(samples-1);}
                        for(unsigned p=0;p<periods.size();++p)for(unsigned axis=0;axis<2;++axis){auto& phase=a.phase[p][axis];const unsigned bin=(axis?y:x)%periods[p];
                            phase.sum[bin]+=residual;phase.split[bin]+=split;++phase.count[bin];}
                        if(x>=2&&y>=2&&x+2<frame.width&&y+2<frame.height){const std::array<std::size_t,4> taps{{i-2,i+2,i-2*frame.width,i+2*frame.width}};
                            bool valid=true;for(const auto j:taps)valid=valid&&compatible(i,j,.98f);
                            if(valid){glm::dvec3 neighbor(0),neighborSplit(0);for(const auto j:taps){neighbor+=(glm::dvec3(image[s][j])-glm::dvec3(pose.reference[j]))*.25;neighborSplit+=glm::dvec3(pose.split[j])*.25;}
                                m.hp+=squared(residual-neighbor);m.hpFloor+=.25*squared(split-neighborSplit);++m.hpCount;}}
                        for(unsigned axis=0;axis<2;++axis){if((axis==0&&x+1>=frame.width)||(axis==1&&y+1>=frame.height))continue;const auto j=i+(axis?frame.width:1);
                            if(!compatible(i,j,.985f)||glm::length(pose.world[j]-pose.world[i])>.03f)continue;
                            const double g=squared((glm::dvec3(image[s][j])-glm::dvec3(pose.reference[j]))-residual);
                            m.gradient+=g;m.gradientFloor+=.25*squared(glm::dvec3(pose.split[j])-split);a.gradients.push_back(std::sqrt(g));++m.pairs;}}
                    for(unsigned r=0;r<regions;++r){auto& a=values[r];auto& m=a.metric;if(!m.count)continue;
                        const auto bias=(a.sum-a.truth)/double(m.count),splitMean=a.split/double(m.count);
                        if(!a.gradients.empty()){std::sort(a.gradients.begin(),a.gradients.end());m.gradientP99=a.gradients[std::min(a.gradients.size()-1,std::size_t(.99*double(a.gradients.size())))];}
                        std::cout<<"PROBE IMPRINT convergence view="<<viewName<<" variant="<<variantNames[variant]<<" static_convergence="<<(variant!=0)
                            <<" checkpoint="<<checkpoint<<" estimate="<<estimate<<" samples="<<samples<<" stage="<<stageNames[s]<<" region="<<regionNames[r]
                            <<" pixels="<<m.count<<" rgb_residual_mse="<<mean(m.mse,m.count)<<" reference_rgb_floor="<<mean(m.floor,m.count)
                            <<" rgb_hp_mse="<<mean(m.hp,m.hpCount)<<" reference_hp_floor="<<mean(m.hpFloor,m.hpCount)
                            <<" residual_gradient_rms="<<std::sqrt(mean(m.gradient,m.pairs))<<" reference_gradient_floor="<<mean(m.gradientFloor,m.pairs)
                            <<" residual_gradient_p99="<<m.gradientP99<<" mean_luma="<<glm::dot(a.sum,luma)/double(m.count)
                            <<" reference_mean_luma="<<glm::dot(a.truth,luma)/double(m.count)<<" temporal_variance="<<mean(m.variance,m.count)
                            <<" temporal_variance_sampled="<<(samples>1)<<" final_temporal_history_mean="<<mean(historySum[r],m.count);
                        for(unsigned p=0;p<periods.size();++p)for(unsigned axis=0;axis<2;++axis){const auto& phase=a.phase[p][axis];double energy=0,floor=0;
                            for(unsigned bin=0;bin<periods[p];++bin)if(phase.count[bin]){energy+=squared(phase.sum[bin]/double(phase.count[bin])-bias)*double(phase.count[bin]);
                                floor+=.25*squared(phase.split[bin]/double(phase.count[bin])-splitMean)*double(phase.count[bin]);}
                            std::cout<<" phase"<<periods[p]<<(axis?'y':'x')<<"="<<mean(energy,m.count)<<" phase"<<periods[p]<<(axis?'y':'x')<<"_floor="<<mean(floor,m.count);}
                        std::cout<<" phase_definition=regional_bias_removed_conditional_phase_energy spectral_proof=0\n";}
                }
            };
            const auto reportDifference=[&](const Images& a,const Images& b,unsigned checkpoint,unsigned variant,const char* kind){
                for(unsigned s=0;s<stageCount;++s){std::array<Accumulator,regions> values;
                    for(unsigned y=0;y<frame.height;++y)for(unsigned x=0;x<frame.width;++x){const auto i=std::size_t(y)*frame.width+x;if(!pose.region[i])continue;
                        auto& v=values[pose.region[i]-1];const auto difference=glm::dvec3(a[s][i])-glm::dvec3(b[s][i]);v.metric.mse+=squared(difference);++v.metric.count;v.sum+=difference;
                        for(unsigned axis=0;axis<2;++axis){auto& phase=v.phase[3][axis];const unsigned bin=(axis?y:x)%32;phase.sum[bin]+=difference;++phase.count[bin];}}
                    for(unsigned r=0;r<regions;++r){const auto& v=values[r];if(!v.metric.count)continue;const auto bias=v.sum/double(v.metric.count);double phaseEnergy=0;
                        for(unsigned axis=0;axis<2;++axis){const auto& phase=v.phase[3][axis];for(unsigned bin=0;bin<32;++bin)if(phase.count[bin])
                            phaseEnergy+=squared(phase.sum[bin]/double(phase.count[bin])-bias)*double(phase.count[bin]);}
                        std::cout<<"PROBE IMPRINT convergence_difference view="<<viewName<<" variant="<<variantNames[variant]<<" checkpoint="<<checkpoint
                            <<" stage="<<stageNames[s]<<" region="<<regionNames[r]<<" kind="<<kind<<" pixels="<<v.metric.count
                            <<" rgb_difference_mse="<<mean(v.metric.mse,v.metric.count)<<" phase32_xy_difference_energy="<<mean(phaseEnergy,v.metric.count)
                            <<" reference_cancels=1 confidence_interval=0\n";}
                }
            };
            std::array<std::uint64_t,3> expectedActive{};
            std::array<std::array<Summary,2>,3> previousStaticCore{};
            for(unsigned variant=0;variant<2;++variant){configure(variant);reset();Images images,lag8,previousBlock,cumulative,block;
                std::array<std::vector<glm::dvec4>,stageCount> sum,blockSum;Moments sumSquares,blockSquares;
                for(unsigned s=0;s<stageCount;++s){sum[s].assign(pixels,glm::dvec4(0));blockSum[s].assign(pixels,glm::dvec4(0));
                    sumSquares[s].assign(pixels,0);blockSquares[s].assign(pixels,0);cumulative[s].resize(pixels);block[s].resize(pixels);}
                std::uint64_t unknownSelected=0,alphaChecked=0,maskZeroChecked=0;unsigned lag8Frame=0;
                std::uint64_t geometryPointFrames=0,rawGeometryAlphaNonpositive=0,spatialGeometryAlphaNonpositive=0;
                std::cout<<"PROBE IMPRINT convergence_setup view="<<viewName<<" variant="<<variantNames[variant]<<" static_convergence="<<gi.temporal.staticConvergence
                    <<" curvature_aware=1 coherent_risk=1 post_reset_warm_frames=0 checkpoints="<<(endFrame==64?"32,64":"32,64,128")
                    <<" cumulative_is_cpu_mean=1 placement_phase=0 seed=0\n";
                for(unsigned f=1;f<=endFrame;++f){render();images[0]=lab.ReadbackSurfaceCacheRawGi();images[1]=lab.ReadbackSurfaceCacheSpatialGi();
                    images[unfilteredStage]=lab.ReadbackSurfaceCacheContactUnfilteredGi();images[filteredStage]=lab.ReadbackSurfaceCacheContactGi();images[temporalStage]=lab.ReadbackSurfaceCacheTemporalGi();
                    for(unsigned s=0;s<stageCount;++s)finite(images[s],"static current stage");const auto mask=lab.ReadbackSurfaceCacheContactMask();finite(mask,"static mask");
                    for(std::size_t i=0;i<pixels;++i){Check(images[unfilteredStage][i].w==images[filteredStage][i].w,"Static convergence changed receiver coverage");++alphaChecked;
                        if(source.sourceViewPositions[i].w>.5f&&source.sourceWorldNormals[i].w>.5f){++geometryPointFrames;
                            rawGeometryAlphaNonpositive+=images[0][i].w<=0;spatialGeometryAlphaNonpositive+=images[1][i].w<=0;}
                        if(mask[i].x<=1e-6f){Check(images[unfilteredStage][i]==images[filteredStage][i]&&images[unfilteredStage][i]==images[1][i],"Static convergence changed mask-zero resolve");++maskZeroChecked;}
                        if(images[unfilteredStage][i].w<=0){Check(images[filteredStage][i].w<=0&&glm::length(glm::vec3(images[filteredStage][i]))<1e-7f,"Static convergence filled unknown receiver");
                            unknownSelected+=source.sourceViewPositions[i].w>.5f&&source.sourceWorldNormals[i].w>.5f&&mask[i].w>.5f&&mask[i].x>.001f;}
                        for(unsigned s=0;s<stageCount;++s){sum[s][i]+=glm::dvec4(images[s][i]);blockSum[s][i]+=glm::dvec4(images[s][i]);
                            const double intensity=glm::dot(glm::dvec3(images[s][i]),luma);sumSquares[s][i]+=intensity*intensity;blockSquares[s][i]+=intensity*intensity;}}
                    if(f==24||f==56||f==120){lag8=images;lag8Frame=f;}
                    if(f%32)continue;
                    for(unsigned s=0;s<stageCount;++s){block[s].resize(pixels);for(std::size_t i=0;i<pixels;++i){cumulative[s][i]=glm::vec4(sum[s][i]/double(f));block[s][i]=glm::vec4(blockSum[s][i]/32.);}}
                    const bool checkpoint=f==32||f==64||f==128;
                    if(checkpoint){const auto history=lab.ReadbackSurfaceCacheTemporalHistory();finite(history,"static temporal history");
                        reportStatic(images,sumSquares,1,f,variant,"current",history);reportStatic(cumulative,sumSquares,f,f,variant,"cumulative",history);
                        reportStatic(block,blockSquares,32,f,variant,"last32",history);
                        Check(lag8Frame+8==f,"Missing fixed lag-eight convergence snapshot");reportDifference(images,lag8,f,variant,"lag8_current");
                        if(f>32)reportDifference(block,previousBlock,f,variant,"adjacent_32frame_means");
                        const auto probes=lab.ReadbackScreenProbePlacement();std::uint64_t active=0;for(const auto& p:probes.uniform)active+=p.w>.5f;for(const auto& p:probes.spawned)active+=p.w>.5f;
                        const unsigned checkpointIndex=f==32?0:f==64?1:2;if(!variant)expectedActive[checkpointIndex]=active;
                        else Check(active==expectedActive[checkpointIndex],"Static convergence changed active probe count");
                        std::uint64_t selected=0;for(const auto& p:mask)selected+=p.w>.5f&&p.x>.001f;
                        std::cout<<"PROBE IMPRINT convergence_workload view="<<viewName<<" variant="<<variantNames[variant]<<" checkpoint="<<f<<" active_probes="<<active
                            <<" selected_ratio_all_pixels="<<double(selected)/double(pixels)<<" alpha_checks="<<alphaChecked<<" mask_zero_checks="<<maskZeroChecked
                            <<" selected_geometry_unknown_count="<<unknownSelected<<" geometry_source_valid_pointframes="<<geometryPointFrames
                            <<" probe_raw_geometry_alpha_nonpositive_count="<<rawGeometryAlphaNonpositive<<" probe_spatial_geometry_alpha_nonpositive_count="<<spatialGeometryAlphaNonpositive
                            <<" probe_raw_geometry_alpha_nonpositive_mean_per_frame="<<double(rawGeometryAlphaNonpositive)/double(f)
                            <<" probe_spatial_geometry_alpha_nonpositive_mean_per_frame="<<double(spatialGeometryAlphaNonpositive)/double(f)
                            <<" per_probe_and_selected_receiver_ray_budget_equal=1 total_receiver_counts_may_differ=1\n";
                        if(f==endFrame){std::array<std::array<Summary,2>,3> coreShell{};
                            for(std::size_t i=0;i<pixels;++i)if(pose.region[i]){const unsigned r=pose.region[i]-1;if(r%3==1)continue;
                                auto& value=coreShell[r/3][r%3==0?0:1];++value.count;value.value+=glm::dot(glm::dvec3(images[temporalStage][i]),luma);
                                value.truth+=glm::dot(glm::dvec3(pose.reference[i]),luma);value.floor+=.25*squared(glm::dvec3(pose.split[i]));}
                            if(!variant)previousStaticCore=coreShell;
                            for(unsigned family=0;family<3;++family){const auto& core=coreShell[family][0];const auto& shell=coreShell[family][1];
                                const bool eligible=core.count>=20&&shell.count>=20;
                                std::cout<<"PROBE IMPRINT convergence_core_guard view="<<viewName<<" variant="<<variantNames[variant]<<" checkpoint="<<f<<" family="<<family
                                    <<" eligible="<<eligible<<" core_pixels="<<core.count<<" shell_pixels="<<shell.count;
                                if(!eligible){std::cout<<'\n';continue;}
                                const auto& oldCore=previousStaticCore[family][0];const auto& oldShell=previousStaticCore[family][1];
                                const double oldNear=mean(oldCore.value,oldCore.count),coreMean=mean(core.value,core.count),referenceNear=mean(core.truth,core.count);
                                const double oldContrast=mean(oldShell.value,oldShell.count)-oldNear,contrast=mean(shell.value,shell.count)-coreMean;
                                const double referenceContrast=mean(shell.truth,shell.count)-referenceNear;
                                const double oldError=std::abs(oldNear-referenceNear),error=std::abs(coreMean-referenceNear);
                                const double oldContrastError=std::abs(oldContrast-referenceContrast),contrastError=std::abs(contrast-referenceContrast);
                                const double overshoot=std::max(0.,coreMean-referenceNear)-std::max(0.,oldNear-referenceNear);
                                const double tolerance=.05*std::max(referenceNear,std::abs(referenceContrast))+std::sqrt(mean(core.floor,core.count));
                                std::cout<<" old_mean="<<oldNear<<" new_mean="<<coreMean<<" reference_mean="<<referenceNear
                                    <<" old_absolute_error="<<oldError<<" new_absolute_error="<<error<<" bright_overshoot_delta="<<overshoot
                                    <<" old_contrast_error="<<oldContrastError<<" new_contrast_error="<<contrastError<<" tolerance="<<tolerance<<'\n';
                                if(variant){Check(error<=oldError+tolerance,"Static convergence increased absolute contact-core bias");
                                    Check(overshoot<=tolerance,"Static convergence increased bright contact-core error");
                                    Check(contrastError<=oldContrastError+tolerance,"Static convergence increased core-shell contrast error");}}
                        }
                        const std::string prefix=viewName+"_"+variantNames[variant]+"_frame"+std::to_string(f);
                        save(mask,prefix+"_mask_current_linear",true);if(!options.screenshot.empty())Save(lab.Readback(),path(prefix+"_total_current_renderer_ldr"));}
                    previousBlock.swap(block);for(unsigned s=0;s<stageCount;++s){std::fill(blockSum[s].begin(),blockSum[s].end(),glm::dvec4(0));std::fill(blockSquares[s].begin(),blockSquares[s].end(),0.);}
                }
            }
            continue;
        }
        for(unsigned variant=0;variant<3;++variant){configure(variant);reset();
            std::array<std::vector<glm::vec4>,stageCount> average,images;std::array<std::vector<double>,stageCount> lumaSquared;
            for(unsigned s=0;s<stageCount;++s){average[s].assign(pixels,glm::vec4(0));lumaSquared[s].assign(pixels,0);}
            std::vector<glm::vec4> maskMean(pixels,glm::vec4(0));std::uint64_t alphaChecks=0,maskZero=0,unknown=0,history=0;
            std::uint64_t uniformActive=0,adaptiveActive=0,selectedMask=0,validMask=0;
            std::uint64_t unknownBackground=0,unknownGeometry=0,selectedGeometryUnknown=0;
            std::uint64_t minimumActive=std::numeric_limits<std::uint64_t>::max(),maximumActive=0;
            for(unsigned f=0;f<warm+measured;++f){render();if(f<warm)continue;images[0]=lab.ReadbackSurfaceCacheRawGi();images[1]=lab.ReadbackSurfaceCacheSpatialGi();
                std::vector<glm::vec4> mask(pixels,glm::vec4(0));
                if(variant<2){images[unfilteredStage]=lab.ReadbackSurfaceCacheContactUnfilteredGi();images[filteredStage]=lab.ReadbackSurfaceCacheContactGi();mask=lab.ReadbackSurfaceCacheContactMask();finite(mask,"contact mask");}
                else {images[unfilteredStage]=images[1];images[filteredStage]=images[1];}
                images[temporalStage]=lab.ReadbackSurfaceCacheTemporalGi();for(unsigned s=0;s<stageCount;++s)finite(images[s],stageNames[s]);
                const auto probes=lab.ReadbackScreenProbePlacement();std::uint64_t uniform=0,adaptive=0;
                for(const auto& p:probes.uniform){for(unsigned c=0;c<4;++c)Check(std::isfinite(p[c]),"Nonfinite imprint uniform probe");uniform+=p.w>.5f;}
                for(const auto& p:probes.spawned){for(unsigned c=0;c<4;++c)Check(std::isfinite(p[c]),"Nonfinite imprint adaptive probe");adaptive+=p.w>.5f;}
                uniformActive+=uniform;adaptiveActive+=adaptive;minimumActive=std::min(minimumActive,uniform+adaptive);maximumActive=std::max(maximumActive,uniform+adaptive);
                for(std::size_t i=0;i<pixels;++i){maskMean[i]+=mask[i]/float(measured);
                    validMask+=mask[i].w>.5f;selectedMask+=mask[i].w>.5f&&mask[i].x>.001f;
                    if(variant<2){Check(images[unfilteredStage][i].w==images[filteredStage][i].w,"Probe-imprint contact filter changed readability");++alphaChecks;
                        if(mask[i].x<=1e-6f){Check(images[unfilteredStage][i]==images[filteredStage][i],"Probe-imprint filter changed a mask-zero receiver");
                            Check(images[unfilteredStage][i]==images[1][i],"Probe-imprint mask-zero resolve differs from probe spatial GI");++maskZero;}
                        if(images[unfilteredStage][i].w<=0){Check(images[filteredStage][i].w<=0&&glm::length(glm::vec3(images[filteredStage][i]))<1e-7f,"Probe-imprint filter filled an unknown center");++unknown;
                            const bool geometry=source.sourceViewPositions[i].w>.5f&&source.sourceWorldNormals[i].w>.5f;
                            if(geometry){++unknownGeometry;selectedGeometryUnknown+=mask[i].w>.5f&&mask[i].x>.001f;}else ++unknownBackground;}}
                    for(unsigned s=0;s<stageCount;++s){average[s][i]+=images[s][i]/float(measured);const double value=glm::dot(glm::dvec3(images[s][i]),luma);lumaSquared[s][i]+=value*value;}}
                history+=lab.PipelineStatistics().surfaceCacheHistoryUsed;
                if(f+1==warm+measured&&!options.screenshot.empty())Save(lab.Readback(),path(viewName+"_"+variantNames[variant]+"_total_renderer_ldr"));}
            const std::string prefix=viewName+"_"+variantNames[variant];save(maskMean,prefix+"_contact_mask_mean_linear",true);
            std::array<std::array<Summary,regions>,stageCount> result{};
            for(unsigned s=0;s<stageCount;++s){save(average[s],prefix+"_"+stageNames[s]+"_mean_tone4");std::array<Accumulator,regions> accumulators;
                for(unsigned y=0;y<frame.height;++y)for(unsigned x=0;x<frame.width;++x){const auto i=std::size_t(y)*frame.width+x;if(!pose.region[i])continue;
                    auto& a=accumulators[pose.region[i]-1];auto& m=a.metric;const auto value=glm::dvec3(average[s][i]),truth=glm::dvec3(pose.reference[i]);
                    const auto residual=value-truth,split=glm::dvec3(pose.split[i]);++m.count;m.mse+=squared(residual);m.floor+=.25*squared(split);
                    a.sum+=value;a.truth+=truth;a.split+=split;const double intensity=glm::dot(value,luma);
                    m.variance+=std::max(0.,lumaSquared[s][i]-measured*intensity*intensity)/double(measured-1);
                    for(unsigned period=0;period<periods.size();++period)for(unsigned axis=0;axis<2;++axis){auto& p=a.phase[period][axis];const unsigned bin=(axis?y:x)%periods[period];
                        p.sum[bin]+=residual;p.split[bin]+=split;++p.count[bin];}
                    if(x>=2&&y>=2&&x+2<frame.width&&y+2<frame.height){const std::array<std::size_t,4> taps{{i-2,i+2,i-2*frame.width,i+2*frame.width}};
                        bool valid=true;for(const auto j:taps)valid=valid&&compatible(i,j,.98f);
                        if(valid){glm::dvec3 neighbor(0),neighborSplit(0);for(const auto j:taps){neighbor+=(glm::dvec3(average[s][j])-glm::dvec3(pose.reference[j]))*.25;neighborSplit+=glm::dvec3(pose.split[j])*.25;}
                            m.hp+=squared(residual-neighbor);m.hpFloor+=.25*squared(split-neighborSplit);++m.hpCount;}}
                    for(unsigned axis=0;axis<2;++axis){if((axis==0&&x+1>=frame.width)||(axis==1&&y+1>=frame.height))continue;const auto j=i+(axis?frame.width:1);
                        if(!compatible(i,j,.985f)||glm::length(pose.world[j]-pose.world[i])>.03f)continue;
                        const auto gradient=(glm::dvec3(average[s][j])-glm::dvec3(pose.reference[j]))-residual;
                        const double g=squared(gradient);m.gradient+=g;m.gradientFloor+=.25*squared(glm::dvec3(pose.split[j])-split);a.gradients.push_back(std::sqrt(g));++m.pairs;}}
                for(unsigned r=0;r<regions;++r){auto& a=accumulators[r];auto& m=a.metric;if(!m.count)continue;
                    m.value=glm::dot(a.sum,luma)/double(m.count);m.truth=glm::dot(a.truth,luma)/double(m.count);
                    if(!a.gradients.empty()){std::sort(a.gradients.begin(),a.gradients.end());m.gradientP99=a.gradients[std::min(a.gradients.size()-1,std::size_t(.99*double(a.gradients.size())))];}
                    result[s][r]=m;const auto bias=(a.sum-a.truth)/double(m.count),splitMean=a.split/double(m.count);
                    std::cout<<"PROBE IMPRINT quality view="<<viewName<<" variant="<<variantNames[variant]<<" stage="<<stageNames[s]<<" region="<<regionNames[r]
                        <<" pixels="<<m.count<<" rgb_residual_mse="<<mean(m.mse,m.count)<<" reference_rgb_floor="<<mean(m.floor,m.count)
                        <<" corrected_rgb_mse="<<std::max(0.,mean(m.mse-m.floor,m.count))<<" rgb_hp_mse="<<mean(m.hp,m.hpCount)<<" reference_hp_floor="<<mean(m.hpFloor,m.hpCount)
                        <<" residual_gradient_rms="<<std::sqrt(mean(m.gradient,m.pairs))<<" reference_gradient_floor="<<mean(m.gradientFloor,m.pairs)<<" residual_gradient_p99="<<m.gradientP99
                        <<" mean_luma="<<m.value<<" reference_mean_luma="<<m.truth<<" temporal_variance="<<mean(m.variance,m.count)
                        <<" bias_rgb="<<bias.x<<','<<bias.y<<','<<bias.z;
                    for(unsigned period=0;period<periods.size();++period)for(unsigned axis=0;axis<2;++axis){const auto& p=a.phase[period][axis];double energy=0,floor=0;
                        for(unsigned bin=0;bin<periods[period];++bin)if(p.count[bin]){energy+=squared(p.sum[bin]/double(p.count[bin])-bias)*double(p.count[bin]);
                            floor+=.25*squared(p.split[bin]/double(p.count[bin])-splitMean)*double(p.count[bin]);}
                        std::cout<<" phase"<<periods[period]<<(axis?'y':'x')<<"="<<mean(energy,m.count)<<" phase"<<periods[period]<<(axis?'y':'x')<<"_floor="<<mean(floor,m.count);}
                    std::cout<<" phase_definition=regional_bias_removed_conditional_phase_energy spectral_proof=0\n";}
            }
            if(!options.screenshot.empty()){std::ofstream csv(path(prefix+"_geometry_distance_profiles",".csv"));Check(bool(csv),"Cannot write imprint geometry profile CSV");
                csv<<"stage,family,distance_min_m,distance_max_m,pixels,mean_r,mean_g,mean_b,reference_r,reference_g,reference_b,mean_mask\n";
                struct Profile {glm::dvec3 value{},truth{};double mask=0;std::uint64_t count=0;};
                for(unsigned s=0;s<stageCount;++s){std::array<std::array<Profile,50>,3> profiles{};
                    for(std::size_t i=0;i<pixels;++i)if(pose.region[i]&&pose.distance[i]>=0&&pose.distance[i]<.5f){
                        const unsigned family=(pose.region[i]-1)/3,bin=std::min(49u,unsigned(pose.distance[i]*100));auto& p=profiles[family][bin];
                        p.value+=glm::dvec3(average[s][i]);p.truth+=glm::dvec3(pose.reference[i]);p.mask+=maskMean[i].x;++p.count;}
                    for(unsigned family=0;family<3;++family)for(unsigned bin=0;bin<50;++bin){auto& p=profiles[family][bin];if(!p.count)continue;
                        p.value/=double(p.count);p.truth/=double(p.count);csv<<stageNames[s]<<','<<family<<','<<bin*.01<<','<<(bin+1)*.01<<','<<p.count<<','
                            <<p.value.x<<','<<p.value.y<<','<<p.value.z<<','<<p.truth.x<<','<<p.truth.y<<','<<p.truth.z<<','<<p.mask/double(p.count)<<'\n';}}}
            std::cout<<"PROBE IMPRINT invariants view="<<viewName<<" variant="<<variantNames[variant]<<" alpha_checks="<<alphaChecks<<" mask_zero_preserved="<<maskZero
                <<" unknown_preserved="<<unknown<<" history_used_frames="<<history<<" contact_off_stage_alias_spatial="<<(variant==2)
                <<" unknown_background_count="<<unknownBackground<<" unknown_geometry_count="<<unknownGeometry<<" selected_geometry_unknown_count="<<selectedGeometryUnknown
                <<" per_probe_and_selected_receiver_ray_budget_equal=1 total_probe_and_receiver_counts_may_differ=1\n";
            std::cout<<"PROBE IMPRINT workload view="<<viewName<<" variant="<<variantNames[variant]<<" measured_frames="<<measured
                <<" active_probes_mean="<<mean(double(uniformActive+adaptiveActive),measured)<<" active_probes_min="<<minimumActive<<" active_probes_max="<<maximumActive
                <<" uniform_active_mean="<<mean(double(uniformActive),measured)<<" adaptive_active_mean="<<mean(double(adaptiveActive),measured)
                <<" mask_collected="<<(variant<2)<<" selected_pixel_frames="<<selectedMask<<" valid_mask_pixel_frames="<<validMask
                <<" selected_ratio_all_pixels="<<mean(double(selectedMask),std::uint64_t(pixels)*measured)
                <<" selected_ratio_valid_mask="<<mean(double(selectedMask),validMask)<<'\n';
            if(!variant)previous=result;
            if(variant==1)for(unsigned family=0;family<3;++family){const auto& oldCore=previous[temporalStage][family*3];const auto& core=result[temporalStage][family*3];const auto& shell=result[temporalStage][family*3+2];
                const auto& oldShell=previous[temporalStage][family*3+2];
                if(core.count<20||shell.count<20)continue;const double tolerance=.05*std::max(core.truth,std::abs(shell.truth-core.truth))+std::sqrt(mean(core.floor,core.count));
                const double oldError=std::abs(oldCore.value-core.truth),error=std::abs(core.value-core.truth);
                const double overshoot=std::max(0.,core.value-core.truth)-std::max(0.,oldCore.value-core.truth);
                const double referenceContrast=shell.truth-core.truth,oldContrast=oldShell.value-oldCore.value,contrast=shell.value-core.value;
                const double oldContrastError=std::abs(oldContrast-referenceContrast),contrastError=std::abs(contrast-referenceContrast);
                std::cout<<"PROBE IMPRINT core_guard view="<<viewName<<" family="<<family<<" old_mean="<<oldCore.value<<" new_mean="<<core.value<<" reference_mean="<<core.truth
                    <<" old_absolute_error="<<oldError<<" new_absolute_error="<<error<<" bright_overshoot_delta="<<overshoot<<" tolerance="<<tolerance
                    <<" old_core_shell_contrast="<<oldContrast<<" new_core_shell_contrast="<<contrast<<" reference_core_shell_contrast="<<referenceContrast
                    <<" old_contrast_error="<<oldContrastError<<" new_contrast_error="<<contrastError<<'\n';
                Check(error<=oldError+tolerance,"Probe-imprint changes increased absolute contact-core bias");
                Check(overshoot<=tolerance,"Probe-imprint changes increased bright contact-core error");
                Check(contrastError<=oldContrastError+tolerance,"Probe-imprint changes increased core-shell contrast error");}
        }
    }
    unsigned resetChecks=0;
    for(unsigned parameter=0;parameter<(convergence?3u:2u);++parameter){configure(1);reset();for(unsigned f=0;f<3;++f)render();
        Check(lab.PipelineStatistics().surfaceCacheHistoryUsed,"Probe-imprint reset guard lacks populated history");
        if(parameter==0)placement.curvatureAwareWeights=false;else if(parameter==1)gi.contact.coherentProbeRisk=false;else gi.temporal.staticConvergence=false;
        render();Check(!lab.PipelineStatistics().surfaceCacheHistoryUsed,"Probe-imprint flag change retained incompatible history");
        finite(lab.ReadbackSurfaceCacheRawGi(),"flag-toggle raw");finite(lab.ReadbackSurfaceCacheSpatialGi(),"flag-toggle spatial");
        finite(lab.ReadbackSurfaceCacheContactUnfilteredGi(),"flag-toggle receiver");finite(lab.ReadbackSurfaceCacheContactGi(),"flag-toggle filtered");
        finite(lab.ReadbackSurfaceCacheTemporalGi(),"flag-toggle temporal");finite(lab.ReadbackSurfaceCacheContactMask(),"flag-toggle mask");++resetChecks;}
    std::cout<<"PROBE IMPRINT history_flag_resets="<<resetChecks<<" material_finite=1 flag_toggle_finite=1\n";
    std::cout<<"PROBE IMPRINT PASS: diagnostic comparisons only; lower HP alone is not an improvement assertion; reference split floor reported\n";
}
