// Focused upward-looking lamp regression. Receiver regions come from the
// observed world-space GBuffer, never from probe availability or brightness.
// The optional per-pixel reference bypasses probe reconstruction, but shares
// the completed single-bounce Surface Cache: it is not path-traced truth.
void SurfaceCacheLampQuality(MaterialLabModule& lab,const Options& options) {
    auto fixture=options;fixture.sealedGiRoom=true;fixture.sdfBanquet=false;
    fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);
    // GL is a structural compatibility run, without a per-pixel reference or
    // GPU timing. Keep it bounded on slower GL adapters.
    frame.width=options.renderSizeSet?options.benchmarkWidth:(options.vulkan?640:320);
    frame.height=options.renderSizeSet?options.benchmarkHeight:(options.vulkan?400:200);
    frame.effects.outputSize={0,0};frame.present=false;frame.screenProbeStudyHud=false;
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::None;
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;
    auto& trace=placement.trace;auto& spatial=gi.spatial;auto& temporal=gi.temporal;
    gi.enabled=true;gi.useScreenProbes=true;gi.view=0;gi.probeDiagnostics=false;
    gi.probeOriginDiagnostics=false;gi.collectProbeCoverage=true;gi.reconstructionDebug=0;
    placement.enabled=true;placement.adaptive=true;placement.cornerRefinement=true;
    placement.frameIndex=0;placement.freeze=false;placement.candidateCount=8;
    trace.cosineHemisphere=true;trace.samplingSeed=0;trace.maxSteps=512;trace.sdf.maxSteps=256;
    const auto baselineGi=gi;const auto baselinePlacement=placement;
    const auto pixels=std::size_t(frame.width)*frame.height;
    const glm::dvec3 luma(.2126,.7152,.0722);
    const auto reset=[&]{frame.effects.cameraCut=true;placement.reset=true;temporal.reset=true;};
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());
        // GL's submission callback does not wait for GPU completion. Do not
        // accumulate twelve expensive 32-ray frames behind a 10s readback.
        if(!options.vulkan&&gi.enabled){const auto completed=lab.ReadbackSurfaceCacheRawGi();
            Check(completed.size()==pixels,std::string("GL lamp frame completion: ")+lab.LastError());}
        frame.effects.cameraCut=false;
        placement.reset=false;temporal.reset=false;};
    const auto finite=[&](const auto& image,const char* label){
        Check(image.size()==pixels,std::string("Missing lamp fixture ")+label+": "+lab.LastError());
        for(const auto& value:image)for(unsigned c=0;c<4;++c)
            Check(std::isfinite(value[c]),"Nonfinite lamp fixture image");};
    const auto outputPath=[&](const std::string& label){auto path=std::filesystem::path(options.screenshot);
        path.replace_filename(path.stem().string()+"_"+label+path.extension().string());return path.string();};
    const auto savePresented=[&](const std::string& label){if(!options.screenshot.empty())Save(lab.Readback(),outputPath(label));};
    const auto save=[&](const auto& hdr,const std::string& label,bool diagnostic=false){if(options.screenshot.empty())return;
        FramePixels image;image.width=frame.width;image.height=frame.height;image.rgba.resize(pixels*4);
        for(std::size_t i=0;i<pixels;++i){for(unsigned c=0;c<3;++c){double value=std::max(0.,double(hdr[i][c]));
            if(!diagnostic){value*=4.;value=value/(1.+value);
                value=value<=.0031308?12.92*value:1.055*std::pow(value,1./2.4)-.055;}
            image.rgba[i*4+c]=std::uint8_t(std::lround(std::clamp(value,0.,1.)*255.));}
            image.rgba[i*4+3]=255;}Save(image,outputPath(label));};
    struct Pose {
        Render::PipelineCamera camera;
        std::vector<glm::vec3> world;
        std::vector<unsigned char> region; // 1 exterior lamp band, 2 interior ceiling
        std::vector<glm::vec4> reference;
        std::array<std::uint64_t,3> counts{};
    };
    const auto cameraAt=[&](unsigned view){auto camera=SealedGiRoomCameraPreset(0,frame.OutputAspect());
        const std::array<glm::vec3,3> eyes{{{-2.75f,2.70f,-2.70f},{-2.05f,3.35f,-2.10f},{2.60f,2.60f,2.65f}}};
        const std::array<glm::vec3,3> targets{{{.28f,4.12f,-.95f},{-.25f,4.40f,.15f},{.55f,4.18f,.10f}}};
        camera.position=eyes[view];camera.view=glm::lookAt(camera.position,targets[view],glm::vec3(0,1,0));return camera;};
    const auto geometry=[&](Pose& pose){const auto source=lab.ReadbackScreenProbeSources();
        finite(source.sourceViewPositions,"source positions");finite(source.sourceWorldNormals,"source normals");
        pose.world.assign(pixels,glm::vec3(0));pose.region.assign(pixels,0);pose.counts={};
        const auto inverse=glm::inverse(pose.camera.view);
        for(std::size_t i=0;i<pixels;++i){const auto p=source.sourceViewPositions[i],n=source.sourceWorldNormals[i];
            if(p.w<.5f||n.w<.5f||n.y>-.99f)continue;
            const auto w=glm::vec3(inverse*glm::vec4(glm::vec3(p),1));pose.world[i]=w;
            if(std::abs(w.y-4.5f)>.005f)continue;
            // Trim reaches +/-1.10. Keep the real ceiling outside it, including
            // its closest visible pixels, rather than measuring the black trim.
            const float exterior=std::max(std::abs(w.x),std::abs(w.z))-1.10f;
            unsigned char region=0;
            if(exterior>=0&&exterior<.35f)region=1;
            else if(exterior>.70f&&std::abs(w.x)<3.45f&&std::abs(w.z)<2.95f)region=2;
            pose.region[i]=region;if(region)++pose.counts[region];}
        Check(pose.counts[1]>20&&pose.counts[2]>20,"Lamp camera omitted the exterior trim band or interior ceiling");
    };
    struct Error {double mse=0,dark=0,bright=0,bias=0,mean=0,hp=0,darkHp=0,residualHp=0;
        std::uint64_t count=0,hpCount=0,unreadable=0;};
    const auto measure=[&](const Pose& pose,const auto& image,const std::string& view,const char* variant,const char* stage){
        finite(image,stage);std::array<Error,3> errors{};std::array<Error,5> distanceBins{};
        constexpr std::array<float,6> binEdges{{0,.01f,.03f,.08f,.15f,.35f}};
        const auto intensity=[&](std::size_t i){return glm::dot(glm::dvec3(image[i]),luma);};
        const auto truth=[&](std::size_t i){return glm::dot(glm::dvec3(pose.reference[i]),luma);};
        for(unsigned y=0;y<frame.height;++y)for(unsigned x=0;x<frame.width;++x){const auto i=std::size_t(y)*frame.width+x;
            const auto region=pose.region[i];if(!region)continue;auto& e=errors[region];
            const double value=intensity(i);e.mean+=value;++e.count;e.unreadable+=image[i].w<=0;
            if(!pose.reference.empty()){const double d=value-truth(i);e.mse+=d*d;e.dark+=std::min(d,0.)*std::min(d,0.);
                e.bright+=std::max(d,0.)*std::max(d,0.);e.bias+=d;}
            if(region==1){const auto p=pose.world[i];const float distance=std::max(std::abs(p.x),std::abs(p.z))-1.10f;
                for(unsigned bin=0;bin<distanceBins.size();++bin)if(distance>=binEdges[bin]&&distance<binEdges[bin+1]){
                    auto& b=distanceBins[bin];++b.count;b.mean+=value;
                    if(!pose.reference.empty()){const double d=value-truth(i);b.mse+=d*d;b.bias+=d;
                        b.dark+=std::min(d,0.)*std::min(d,0.);b.bright+=std::max(d,0.)*std::max(d,0.);}break;}}
            // A symmetric image-space high-pass is only evaluated where every
            // tap belongs to this observed ceiling region. No geometric seam
            // or trim pixel is averaged into the scallop statistic.
            constexpr unsigned radius=3;
            if(x<radius||y<radius||x+radius>=frame.width||y+radius>=frame.height)continue;
            const std::array<std::size_t,4> taps{{i-radius,i+radius,i-radius*frame.width,i+radius*frame.width}};
            bool compatible=true;for(const auto j:taps)compatible=compatible&&pose.region[j]==region;
            if(!compatible)continue;
            double neighbor=0,referenceNeighbor=0;for(const auto j:taps){neighbor+=intensity(j)*.25;
                if(!pose.reference.empty())referenceNeighbor+=truth(j)*.25;}
            const double hp=value-neighbor;e.hp+=hp*hp;e.darkHp+=std::min(hp,0.)*std::min(hp,0.);++e.hpCount;
            if(!pose.reference.empty()){const double residual=hp-(truth(i)-referenceNeighbor);e.residualHp+=residual*residual;}
        }
        for(unsigned region=1;region<3;++region){const auto& e=errors[region];
            std::cout<<"SURFACE CACHE LAMP error view="<<view<<" variant="<<variant<<" stage="<<stage
                <<" region="<<(region==1?"trim_exterior":"interior_ceiling")<<" pixels="<<e.count
                <<" unreadable="<<e.unreadable<<" mean_luma="<<e.mean/double(std::max<std::uint64_t>(1,e.count))
                <<" hp_pairs="<<e.hpCount<<" hp_mse="<<e.hp/double(std::max<std::uint64_t>(1,e.hpCount))
                <<" negative_hp_mse="<<e.darkHp/double(std::max<std::uint64_t>(1,e.hpCount));
            if(!pose.reference.empty())std::cout<<" luma_mse="<<e.mse/double(e.count)<<" dark_mse="<<e.dark/double(e.count)
                <<" bright_mse="<<e.bright/double(e.count)<<" signed_bias="<<e.bias/double(e.count)
                <<" residual_hp_mse="<<e.residualHp/double(std::max<std::uint64_t>(1,e.hpCount));
            std::cout<<std::endl;}
        if(!pose.reference.empty())for(unsigned bin=0;bin<distanceBins.size();++bin){const auto& b=distanceBins[bin];if(!b.count)continue;
            std::cout<<"SURFACE CACHE LAMP distance_bin view="<<view<<" variant="<<variant<<" stage="<<stage
                <<" from_m="<<binEdges[bin]<<" to_m="<<binEdges[bin+1]<<" pixels="<<b.count
                <<" mean="<<b.mean/double(b.count)<<" signed_bias="<<b.bias/double(b.count)
                <<" mse="<<b.mse/double(b.count)<<" dark_mse="<<b.dark/double(b.count)
                <<" bright_mse="<<b.bright/double(b.count)<<std::endl;}
        return errors;
    };
    const auto diagnostics=[&](const Pose& pose,const std::string& view,const char* variant){
        SurfaceCacheLampTransportDiagnostics(lab,view+"_"+variant);
        const auto coverage=lab.ReadbackSurfaceCacheProbeCoverage();finite(coverage,"coverage");save(coverage,view+"_"+variant+"_coverage",true);
        std::array<std::uint64_t,3> holes{},low{};
        for(std::size_t i=0;i<pixels;++i)if(const auto r=pose.region[i]){holes[r]+=coverage[i].w<.5f;
            low[r]+=coverage[i].w>=.5f&&coverage[i].r>.99f&&coverage[i].b>.99f;}
        std::array<std::uint64_t,6> invalid{};std::uint64_t active=0,clipped=0,uncertain=0;
        if(gi.safeProbeOrigins)for(const auto& origin:lab.ReadbackSurfaceCacheProbeOrigins())if(origin.w!=0){++active;
            clipped+=origin.w>1.5f&&origin.w<2.5f;uncertain+=origin.w>2.5f;
            if(origin.w<0){const int reason=int(std::lround(-origin.w));if(reason>=1&&reason<=5)++invalid[unsigned(reason)];}}
        std::array<std::uint64_t,18> status{};
        for(const auto& ray:lab.ReadbackScreenProbeTrace(false).radianceStatus){const int s=int(std::lround(ray.w));
            if(s>=0&&s<int(status.size()))++status[unsigned(s)];}
        std::cout<<"SURFACE CACHE LAMP diagnostic view="<<view<<" variant="<<variant
            <<" band_holes="<<holes[1]<<" ceiling_holes="<<holes[2]<<" band_low_support="<<low[1]
            <<" ceiling_low_support="<<low[2]<<" origin_active="<<active<<" origin_clipped="<<clipped<<" origin_uncertain="<<uncertain;
        for(unsigned reason=1;reason<invalid.size();++reason)std::cout<<" origin_reason"<<reason<<"="<<invalid[reason];
        for(unsigned s=0;s<status.size();++s)std::cout<<" ray_status"<<s<<"="<<status[s];std::cout<<std::endl;
    };
    unsigned measuredViews=0,measuredVariants=0;
    for(unsigned viewIndex=0;viewIndex<3;++viewIndex){const std::string view="lamp"+std::to_string(viewIndex);
        Pose pose;pose.camera=cameraAt(viewIndex);frame.camera=pose.camera;
        gi=baselineGi;placement=baselinePlacement;reset();render();
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
        while(lab.ProbeSdfQualityStatistics().pending){render();Check(std::chrono::steady_clock::now()<deadline,"Lamp SDF preparation timed out");}
        geometry(pose);
        if(options.vulkan){gi.useScreenProbes=false;placement.enabled=false;gi.raysPerPixel=32;
            temporal.enabled=false;spatial.enabled=false;gi.reconstructionDebug=0;reset();
            pose.reference.assign(pixels,glm::vec4(0));std::vector<glm::vec4> half(pixels,glm::vec4(0));
            for(unsigned seed=0;seed<8;++seed){trace.samplingSeed=1009+73*seed;render();
                const auto image=lab.ReadbackSurfaceCacheRawGi();finite(image,"256-spp reference");
                for(std::size_t i=0;i<pixels;++i){pose.reference[i]+=image[i]/8.f;half[i]+=image[i]*(seed<4?.25f:-.25f);}}
            std::array<double,3> uncertainty{};for(std::size_t i=0;i<pixels;++i)if(const auto r=pose.region[i]){
                const double d=glm::dot(glm::dvec3(half[i]),luma);uncertainty[r]+=d*d;}
            std::cout<<"SURFACE CACHE LAMP reference view="<<view<<" spp=256 seeds=8 shared_cache=1 path_truth=0 band_pixels="<<pose.counts[1]
                <<" ceiling_pixels="<<pose.counts[2]<<" band_half_difference_mse="<<uncertainty[1]/double(pose.counts[1])
                <<" ceiling_half_difference_mse="<<uncertainty[2]/double(pose.counts[2])<<std::endl;
            save(pose.reference,view+"_reference");}
        // Structural diagnostics must retain genuinely unsupported receivers.
        // Constant one proves normalization only on geometrically covered
        // pixels; affine residuals expose smooth-field reconstruction errors.
        // These analytic shader modes intentionally bypass launch certification
        // and therefore cannot prove that real transport has no origin holes.
        gi=baselineGi;placement=baselinePlacement;gi.view=2;temporal.enabled=false;spatial.enabled=false;
        struct Analytic {unsigned mode;bool continuous,adaptive;const char* label;};
        const std::array<Analytic,5> analytic{{{1,true,true,"constant"},{3,true,true,"affine"},
            {3,false,true,"affine_legacy"},{3,true,false,"affine_uniform"},{3,false,false,"affine_uniform_legacy"}}};
        for(const auto& diagnostic:analytic){const unsigned mode=diagnostic.mode;
            gi.reconstructionDebug=mode;placement.continuousInterpolation=diagnostic.continuous;
            placement.adaptive=diagnostic.adaptive;reset();render();
            Check(!lab.PipelineStatistics().surfaceCacheContactCollected,"Analytic reconstruction diagnostic scheduled actual contact GI rays");
            const auto image=lab.ReadbackSurfaceCacheRawGi(),coverage=lab.ReadbackSurfaceCacheProbeCoverage();
            finite(image,"analytic diagnostic");finite(coverage,"analytic coverage");
            std::array<double,3> squared{},maximum{};std::array<std::uint64_t,3> holes{},count{};
            for(std::size_t i=0;i<pixels;++i)if(const auto r=pose.region[i]){holes[r]+=coverage[i].w<.5f;
                if(coverage[i].w<.5f)continue;
                const auto p=pose.world[i];const double expected=mode==1?1.:std::clamp(.45+.04*(double(p.x)+2.*p.y+.5*p.z),.05,.95);
                const double d=double(image[i].x)-expected;squared[r]+=d*d;maximum[r]=std::max(maximum[r],std::abs(d));++count[r];}
            for(unsigned r=1;r<3;++r){std::cout<<"SURFACE CACHE LAMP reconstruction view="<<view<<" mode="<<diagnostic.label
                <<" region="<<(r==1?"trim_exterior":"interior_ceiling")<<" geometry_only_validity=1 covered="<<count[r]<<" holes="<<holes[r]
                <<" mse="<<squared[r]/double(std::max<std::uint64_t>(1,count[r]))<<" maximum="<<maximum[r]<<std::endl;
                if(mode==1)Check(maximum[r]<3e-5,"Lamp constant-field reconstruction failed normalization on a supported receiver");}
            if(mode==3){double seamSquared=0,otherSquared=0,seamMaximum=0;std::uint64_t seams=0,other=0;
                const auto expected=[&](std::size_t i){const auto p=pose.world[i];
                    return std::clamp(.45+.04*(double(p.x)+2.*p.y+.5*p.z),.05,.95);};
                for(unsigned y=1;y<frame.height;++y)for(unsigned x=1;x<frame.width;++x){const auto i=std::size_t(y)*frame.width+x;
                    if(pose.region[i]!=2||pose.region[i-1]!=2||pose.region[i-frame.width]!=2)continue;
                    for(unsigned axis=0;axis<2;++axis){const auto j=axis?i-frame.width:i-1;
                        if(coverage[i].w<.5f||coverage[j].w<.5f)continue;
                        const double d=double(image[i].x-image[j].x)-(expected(i)-expected(j));
                        const bool seam=(axis?y:x)%placement.baseSpacing==0;
                        if(seam){seamSquared+=d*d;seamMaximum=std::max(seamMaximum,std::abs(d));++seams;}
                        else{otherSquared+=d*d;++other;}}}
                std::cout<<"SURFACE CACHE LAMP affine_derivative view="<<view<<" mode="<<diagnostic.label
                    <<" seam_pairs="<<seams<<" seam_mse="<<seamSquared/double(std::max<std::uint64_t>(1,seams))
                    <<" seam_max="<<seamMaximum<<" other_mse="<<otherSquared/double(std::max<std::uint64_t>(1,other))<<std::endl;}
            save(image,view+"_"+diagnostic.label,true);}
        constexpr std::array<const char*,7> variants{{"baseline","probe_only","legacy_probe_only","contact_rays32","quality16_30cm","unfiltered_contact","layer_off"}};
        // Every view has a matched on/off contact-tracing comparison; the first
        // additionally isolates coarse continuity and the recovery ray budget.
        const std::vector<unsigned> variantIndices=viewIndex==0?std::vector<unsigned>{0,1,2,3,4,5,6}:std::vector<unsigned>{0,1,4};
        std::array<Error,3> baselineErrors{},probeOnlyErrors{};std::vector<glm::vec4> baselineRaw;
        for(const unsigned variant:variantIndices){gi=baselineGi;placement=baselinePlacement;
            gi.reconstructionDebug=0;gi.view=0;trace.samplingSeed=0;
            if(variant==1||variant==2)gi.contact.enabled=false;
            if(variant==2)placement.continuousInterpolation=false;
            if(variant==3)gi.contact.raysPerPixel=32;
            if(variant==4){gi.contact.raysPerPixel=16;gi.contact.maxDistance=.3f;}
            if(variant==5)gi.contact.filterStrength=0;
            if(variant==6)gi.contact.layerValidation=false;
            reset();render();
            if(variant<=2){const auto coldRaw=lab.ReadbackSurfaceCacheRawGi();
                const auto coldSpatial=spatial.enabled?lab.ReadbackSurfaceCacheSpatialGi():coldRaw;
                const auto coldTemporal=temporal.enabled?lab.ReadbackSurfaceCacheTemporalGi():coldSpatial;
                measure(pose,coldRaw,view,variants[variant],"cold_raw");measure(pose,coldTemporal,view,variants[variant],"cold_temporal");
                savePresented(view+"_"+variants[variant]+"_cold_total");save(coldRaw,view+"_"+variants[variant]+"_cold_raw");}
            for(unsigned warm=1;warm<12;++warm)render();
            const auto raw=lab.ReadbackSurfaceCacheRawGi();finite(raw,"raw");
            if(variant==0)baselineRaw=raw;
            if(variant==1){double difference=0;for(std::size_t i=0;i<pixels;++i)
                for(unsigned c=0;c<4;++c)difference=std::max(difference,std::abs(double(raw[i][c])-baselineRaw[i][c]));
                Check(difference<1e-7,"Contact tracing changed the upstream probe ray phase or raw reconstruction");
                std::cout<<"SURFACE CACHE LAMP matched_raw view="<<view<<" maximum_rgba_difference="<<difference<<std::endl;}
            const auto filtered=spatial.enabled?lab.ReadbackSurfaceCacheSpatialGi():raw;
            const auto accumulated=temporal.enabled?lab.ReadbackSurfaceCacheTemporalGi():filtered;
            measure(pose,raw,view,variants[variant],"raw");measure(pose,filtered,view,variants[variant],"spatial");
            if(gi.contact.enabled){const auto contact=lab.ReadbackSurfaceCacheContactGi(),mask=lab.ReadbackSurfaceCacheContactMask();
                const auto unfiltered=lab.ReadbackSurfaceCacheContactUnfilteredGi();finite(unfiltered,"unfiltered contact GI");
                finite(contact,"contact GI");finite(mask,"contact mask");measure(pose,contact,view,variants[variant],"contact");
                measure(pose,unfiltered,view,variants[variant],"unfiltered_contact");
                save(contact,view+"_"+variants[variant]+"_contact");save(mask,view+"_"+variants[variant]+"_contact_mask",true);
                std::array<std::uint64_t,3> affected{},unknown{};std::uint64_t active=0,valid=0,selectedUnknown=0;double weight=0;
                for(std::size_t i=0;i<pixels;++i){Check(mask[i].x>=0&&mask[i].x<=1,"Invalid contact reconstruction mask");
                    Check(contact[i].w==unfiltered[i].w,"Contact filtering changed readable sample coverage");
                    if(mask[i].w>.5f){++valid;weight+=mask[i].x;if(mask[i].x>0){++active;
                        if(contact[i].w<=0)++selectedUnknown;
                        if(pose.region[i]){++affected[pose.region[i]];if(contact[i].w<=0)++unknown[pose.region[i]];}}}
                    if(mask[i].w>.5f&&mask[i].x<=.000001f)Check(contact[i]==filtered[i],"Contact trace changed an unselected receiver");}
                std::cout<<"SURFACE CACHE LAMP contact view="<<view<<" variant="<<variants[variant]<<" active_pixels="<<active
                    <<" valid_pixels="<<valid<<" mean_weight="<<weight/double(std::max<std::uint64_t>(1,valid))
                    <<" band_pixels="<<affected[1]<<" interior_pixels="<<affected[2]<<" selected_unknown="<<selectedUnknown
                    <<" band_unknown="<<unknown[1]<<" interior_unknown="<<unknown[2]
                    <<" max_extra_rays="<<active*gi.contact.raysPerPixel<<std::endl;
                Check(unknown[1]==0&&unknown[2]==0,"Contact receiver certification created an unknown ceiling sample");}
            const auto accumulatedErrors=measure(pose,accumulated,view,variants[variant],"temporal");
            if(variant==0)baselineErrors=accumulatedErrors;
            if(variant==1)probeOnlyErrors=accumulatedErrors;
            savePresented(view+"_"+variants[variant]+"_total");save(raw,view+"_"+variants[variant]+"_raw");
            save(filtered,view+"_"+variants[variant]+"_spatial");save(accumulated,view+"_"+variants[variant]+"_temporal");
            diagnostics(pose,view,variants[variant]);++measuredVariants;
            if(variant==0){gi.enabled=false;placement.enabled=false;render();savePresented(view+"_direct_only");
                const auto direct=lab.Readback();Check(direct.rgba.size()==pixels*4,"Lamp direct-only readback has the wrong size");
                std::vector<glm::vec4> display(pixels,glm::vec4(1));for(std::size_t i=0;i<pixels;++i)
                    for(unsigned c=0;c<3;++c)display[i][c]=float(direct.rgba[i*4+c])/255.f;
                Pose directPose=pose;directPose.reference.clear();measure(directPose,display,view,"baseline","direct_rgb8_normalized");}
        }
        if(options.vulkan){Check(baselineErrors[1].count>0&&probeOnlyErrors[1].count==baselineErrors[1].count,
                "Lamp contact comparison did not measure the same receiver band");
            Check(baselineErrors[1].mse<probeOnlyErrors[1].mse&&baselineErrors[1].dark<probeOnlyErrors[1].dark,
                "Actual receiver contact tracing did not reduce lamp-band total and dark error");
            std::cout<<"SURFACE CACHE LAMP improvement view="<<view<<" band_mse_ratio="<<baselineErrors[1].mse/probeOnlyErrors[1].mse
                <<" band_dark_mse_ratio="<<baselineErrors[1].dark/probeOnlyErrors[1].dark<<std::endl;}
        ++measuredViews;
    }
    gi=baselineGi;placement=baselinePlacement;frame.camera=cameraAt(0);
    reset();for(unsigned warm=0;warm<12;++warm)render();
    const auto movingBase=*frame.camera;
    const glm::vec3 forward=glm::normalize(-glm::vec3(glm::inverse(movingBase.view)[2]));
    std::uint64_t reusedMotionPixels=0;
    for(unsigned step=1;step<=4;++step){auto movingCamera=movingBase;
        movingCamera.position+=glm::vec3(.015f*step,0,0);
        const auto direction=glm::vec3(glm::rotate(glm::mat4(1),.0075f*step,glm::vec3(0,1,0))*glm::vec4(forward,0));
        movingCamera.view=glm::lookAt(movingCamera.position,movingCamera.position+direction,glm::vec3(0,1,0));frame.camera=movingCamera;render();
        finite(lab.ReadbackSurfaceCacheContactGi(),"moving contact GI");finite(lab.ReadbackSurfaceCacheTemporalGi(),"moving temporal GI");
        const auto history=lab.ReadbackSurfaceCacheTemporalHistory();finite(history,"moving contact history");
        const auto reused=std::count_if(history.begin(),history.end(),[](const auto& p){return p.w>1.5f;});
        Check(lab.PipelineStatistics().surfaceCacheHistoryUsed&&reused>pixels/3,
            "Contact reconstruction cleared compatible history during a small camera motion");
        reusedMotionPixels+=std::uint64_t(reused);}
    savePresented("moving_contact_final");
    std::cout<<"SURFACE CACHE LAMP motion frames=4 reused_pixel_frames="<<reusedMotionPixels<<" reference_error_measured=0"<<std::endl;
    frame.camera=cameraAt(0);
    for(unsigned parameter=0;parameter<6;++parameter){gi=baselineGi;placement=baselinePlacement;reset();
        for(unsigned warm=0;warm<3;++warm)render();Check(lab.PipelineStatistics().surfaceCacheHistoryUsed,"Contact reset guard lacks populated history");
        if(parameter==0)gi.contact.layerValidation=!gi.contact.layerValidation;
        if(parameter==1)gi.contact.filterRadius=gi.contact.filterRadius==1?2:1;
        if(parameter==2)gi.contact.raysPerPixel=gi.contact.raysPerPixel==8?16:8;
        if(parameter==3)gi.contact.filterPasses=gi.contact.filterPasses==1?3:1;
        if(parameter==4)gi.contact.temporalDirections=!gi.contact.temporalDirections;
        if(parameter==5)gi.contact.coverageWeightedFilter=!gi.contact.coverageWeightedFilter;
        render();Check(!lab.PipelineStatistics().surfaceCacheHistoryUsed,"Contact parameter change reused incompatible GI history");}
    std::cout<<"SURFACE CACHE LAMP history_parameter_resets=6"<<std::endl;
    gi=baselineGi;placement=baselinePlacement;
    temporal.enabled=false;reset();render();const auto lit=lab.ReadbackSurfaceCacheContactGi(),litMask=lab.ReadbackSurfaceCacheContactMask();
    frame.effects.areaLights.lights[0].radiance*=2.f;render();
    const auto doubled=lab.ReadbackSurfaceCacheContactGi(),doubledMask=lab.ReadbackSurfaceCacheContactMask();
    finite(lit,"contact linearity baseline");finite(doubled,"contact linearity doubled");double linearityMaximum=0;
    for(std::size_t i=0;i<pixels;++i){Check(litMask[i]==doubledMask[i],"Contact risk mask depends on illumination brightness");
        for(unsigned c=0;c<3;++c)linearityMaximum=std::max(linearityMaximum,std::abs(double(doubled[i][c])-2.*lit[i][c]));}
    Check(linearityMaximum<1e-4,"Contact tracing/filtering violated light-intensity linearity");
    std::cout<<"SURFACE CACHE LAMP linearity maximum_rgb_error="<<linearityMaximum<<" geometry_mask_unchanged=1"<<std::endl;
    frame.effects.areaLights.lights[0].radiance*=.5f;gi=baselineGi;placement=baselinePlacement;
    reset();for(unsigned warm=0;warm<12;++warm)render();
    frame.effects.areaLights.lights[0].enabled=false;render();
    const auto darkContact=lab.ReadbackSurfaceCacheContactGi(),darkTemporal=lab.ReadbackSurfaceCacheTemporalGi();
    finite(darkContact,"lamp-off contact");finite(darkTemporal,"lamp-off temporal");double maximumDark=0;
    for(std::size_t i=0;i<pixels;++i)for(unsigned c=0;c<3;++c)
        maximumDark=std::max({maximumDark,double(std::abs(darkContact[i][c])),double(std::abs(darkTemporal[i][c]))});
    Check(maximumDark<1e-6&&!lab.PipelineStatistics().surfaceCacheHistoryUsed,
        "Contact tracing invented radiance or retained stale light after disabling the sole emitter");
    std::cout<<"SURFACE CACHE LAMP emitter_off maximum_rgb="<<maximumDark<<" history_used=0"<<std::endl;
    frame.effects.areaLights.lights[0].enabled=true;
    if(options.vulkan)for(unsigned batch=0;batch<8;++batch){gi=baselineGi;placement=baselinePlacement;
        const unsigned localBatch=batch%4;gi.contact.enabled=localBatch==1||localBatch==2;
        if(batch>=4){gi.contact.raysPerPixel=16;gi.contact.maxDistance=.3f;}reset();for(unsigned warm=0;warm<4;++warm)render();
        const auto timing=lab.MeasureFrames(frame,4);
        Check(std::isfinite(timing.gpuMilliseconds)&&timing.gpuMilliseconds>0,"Lamp performance batch lacks a valid GPU timestamp");
        std::cout<<"SURFACE CACHE LAMP perf batch="<<batch<<" config="<<(batch<4?"default":"quality")<<" contact="<<gi.contact.enabled
            <<" gpu_ms="<<timing.gpuMilliseconds<<" elapsed_ms="<<timing.elapsedMilliseconds
            <<" size="<<frame.width<<"x"<<frame.height<<" recovery_rays="<<gi.contact.raysPerPixel
            <<" frames=4 readback=0 present=0"<<std::endl;}
    gi=baselineGi;placement=baselinePlacement;gi.reconstructionDebug=3;gi.view=0;reset();render();
    Check(lab.PipelineStatistics().surfaceCacheContactCollected,"An inactive indirect diagnostic disabled real total-view contact GI");
    gi.view=2;render();Check(!lab.PipelineStatistics().surfaceCacheContactCollected,"Analytic indirect view scheduled contact GI");
    gi=baselineGi;placement=baselinePlacement;reset();render();
    std::cout<<"SURFACE CACHE LAMP PASS size="<<frame.width<<"x"<<frame.height<<" views="<<measuredViews
        <<" variants="<<measuredVariants<<" reference_spp="<<(options.vulkan?256:0)
        <<" restored_baseline=1 submitted_reset=1 pending_reset_flags=0 improve_assertions="<<options.vulkan<<std::endl;
}
