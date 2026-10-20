// The transport oracle below sums the mathematical reconstruction over every
// live representative, rather than reusing the shader's tile traversal. It
// keeps geometry availability separate from radiance/readable sample counts.
// Include after surface_cache_geometry_quality_test.inl for its independent
// double-precision triangle intersection oracle.
void SurfaceCacheTransportQuality(MaterialLabModule& lab,const Options& options) {
    using SurfaceCacheGeometryFixture::Oracle;
    auto fixture=options;fixture.sealedGiRoom=true;fixture.sdfBanquet=false;
    fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);frame.effects.outputSize={0,0};
    frame.present=false;frame.screenProbeStudyHud=false;
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;auto& trace=placement.trace;
    gi.contact.enabled=false; // Preserve the raw transport and reconstruction contract under test.
    gi.enabled=true;gi.useScreenProbes=true;gi.view=2;gi.safeProbeOrigins=true;gi.rasterSourcePositions=true;
    gi.probeDiagnostics=false;gi.probeOriginDiagnostics=false;gi.collectProbeCoverage=false;gi.reconstructionDebug=0;
    gi.directionalReconstruction=false;gi.spatial.enabled=false;gi.temporal.enabled=false;
    placement.enabled=true;placement.adaptive=true;placement.cornerRefinement=true;placement.curvatureRefinement=true;
    // This oracle isolates the original strict-plane reconstruction algebra.
    // Curved, hit-constrained reuse has a separate safe-receiver quality fixture.
    placement.curvatureAwareWeights=false;
    placement.reconstructionVisibility=false;placement.baseSpacing=32;placement.candidateCount=8;placement.frameIndex=0;placement.freeze=false;
    trace.enabled=true;trace.showPanel=false;trace.cosineHemisphere=true;trace.samplingSeed=17;trace.rayResolution=4;
    trace.maxSteps=256;trace.sdf.enabled=true;trace.sdf.maxSteps=128;trace.sdf.automaticQuality=false;
    std::vector<SealedGiRoomMaterial> materials;const auto& objects=SealedGiRoomObjects();
    for(unsigned object=0;object<objects.size();++object)materials.push_back(SealedGiRoomObjectMaterial(object));
    Oracle oracle;oracle.Initialize(objects,materials);
    const auto readable=[](int status){return status==1||status==5||status==9||status==10||status==13;};
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;placement.reset=false;};
    const auto finite=[](const auto& values,const char* label){Check(!values.empty(),std::string("Missing transport fixture ")+label);
        for(const auto& value:values)for(unsigned channel=0;channel<4;++channel)Check(std::isfinite(value[channel]),"Nonfinite transport fixture data");};
    const auto basis=[](double value){const double a=std::abs(value);return a<.5?.75-a*a:.5*std::pow(std::max(0.,1.5-a),2);};
    struct Probe {glm::dvec3 p{},n{};glm::dvec2 screen{};glm::dvec4 light{};double depth=0,activation=1;
        bool active=false,available=false,partial=false,black=false;};
    struct Receiver {glm::dvec3 p{},n{},rho{};glm::dvec4 sum{},oldSum{};double depth=0,mass=0,oldMass=0,blackMass=0,partialMass=0;bool valid=false;};
    std::uint64_t allChecked=0,allAffected=0,allRgbAffected=0,allPartial=0,allPartialReceivers=0,allBlack=0,allInvalid=0;
    double allMaximum=0,allIntegrationMaximum=0,allCounterfactual=0,allRgbCounterfactual=0;
    const auto start=std::chrono::steady_clock::now();
    for(unsigned resolution=0;resolution<2;++resolution){
        frame.width=resolution?2560u:1280u;frame.height=resolution?1440u:800u;
        auto camera=SealedGiRoomCameraPreset(1,frame.OutputAspect());const auto baseInverse=glm::inverse(camera.view);
        const auto right=glm::normalize(glm::vec3(baseInverse[0])),forward=glm::normalize(-glm::vec3(baseInverse[2]));
        camera.position+=right*.0125f;
        const auto direction=glm::vec3(glm::rotate(glm::mat4(1),.004f,glm::vec3(0,1,0))*glm::vec4(forward,0));
        camera.view=glm::lookAt(camera.position,camera.position+direction,glm::vec3(0,1,0));frame.camera=camera;
        placement.reset=true;frame.effects.cameraCut=true;
        do{render();Check(std::chrono::steady_clock::now()-start<std::chrono::seconds(180),"Transport fixture SDF preparation timed out");}
        while(lab.ProbeSdfQualityStatistics().pending);
        const auto sources=lab.ReadbackScreenProbeSources();const auto placed=lab.ReadbackScreenProbePlacement();
        const auto origins=lab.ReadbackSurfaceCacheProbeOrigins(),irradiance=lab.ReadbackSurfaceCacheProbeIrradiance(),raw=lab.ReadbackSurfaceCacheRawGi();
        const auto rays=lab.ReadbackScreenProbeTrace(false);const std::size_t pixels=std::size_t(frame.width)*frame.height;
        const std::size_t slots=placed.uniform.size()+placed.spawned.size();
        finite(origins,"origins");finite(irradiance,"irradiance");finite(raw,"raw GI");
        Check(sources.sourceViewPositions.size()==pixels&&sources.sourceWorldNormals.size()==pixels&&raw.size()==pixels,"Transport fixture receiver extent mismatch");
        Check(origins.size()==slots&&irradiance.size()>=slots&&rays.probeSlots==slots&&rays.rayResolution==4&&rays.radianceStatus.size()==slots*16,
            "Transport fixture probe-major atlas extent mismatch");
        Check(placed.frameIndex==0&&placed.candidateCount==8,"Transport fixture requires the fixed zero placement phase and eight slots");
        Check(lab.PipelineStatistics().surfaceCacheSpatialPasses==0&&!lab.PipelineStatistics().surfaceCacheHistoryUsed,
            "Transport fixture unexpectedly filtered or accumulated the raw signal");
        const auto inverseView=glm::dmat4(glm::inverse(camera.view)),viewProjection=glm::dmat4(camera.projection*camera.view);
        std::vector<Probe> probes(slots);std::uint64_t active=0,invalid=0,partial=0,black=0;
        double integrationMaximum=0;
        for(std::size_t index=0;index<slots;++index){const bool coarse=index<placed.uniform.size();const auto local=coarse?index:index-placed.uniform.size();
            const auto pp=coarse?placed.uniform[local]:placed.spawned[local],nd=coarse?placed.uniformNormals[local]:placed.spawnedNormals[local];
            auto& probe=probes[index];if(pp.w<.5f)continue;++active;probe.active=true;probe.available=origins[index].w>0;invalid+=!probe.available;
            probe.p=glm::dvec3(pp);probe.n=glm::normalize(glm::dvec3(nd));probe.depth=nd.w;probe.light=glm::dvec4(irradiance[index]);
            probe.activation=pp.w>=4?std::clamp(double(pp.w)-4.,0.,1.):1.;
            const auto clip=viewProjection*glm::dvec4(probe.p,1);probe.screen=(glm::dvec2(clip)/clip.w*.5+.5)*glm::dvec2(frame.width,frame.height)-.5;
            glm::dvec3 sum(0);unsigned count=0;
            for(unsigned ray=0;ray<16;++ray){const auto value=rays.radianceStatus[index*16+ray];const int status=int(std::lround(value.w));
                if(readable(status)){sum+=glm::max(glm::dvec3(value),glm::dvec3(0));++count;}
                if(!probe.available)Check(status==16&&glm::length(glm::dvec3(value))<1e-8,"Rejected origin leaked a ray measurement");}
            const glm::dvec4 expected(sum/16.,double(count)/16.);
            integrationMaximum=std::max(integrationMaximum,glm::length(expected-probe.light));
            probe.partial=probe.available&&count>0&&count<16;partial+=probe.partial;
            probe.black=probe.available&&count==16&&glm::length(sum)<1e-8;black+=probe.black;
        }
        Check(integrationMaximum<3e-5,"Probe integration changed the full-ray denominator or lost known-black readability");
        // Recover the physical lattice phase from captured uniform points,
        // independently of the production Hammersley/jitter implementation.
        glm::ivec2 jitter(-1);
        for(std::size_t index=0;index<placed.uniform.size();++index)if(probes[index].active){
            const glm::uvec2 tile(unsigned(index%placed.uniformGridSize.x),unsigned(index/placed.uniformGridSize.x));
            for(unsigned axis=0;axis<2;++axis)if(tile[axis]+1<placed.uniformGridSize[axis]){
                const int value=int(std::lround(probes[index].screen[axis]))-int(tile[axis]*placement.baseSpacing);
                Check(value>=0&&value<int(placement.baseSpacing),"Transport uniform probe left its physical tile");
                if(jitter[axis]<0)jitter[axis]=value;
                Check(jitter[axis]==value,"Transport uniform lattice has inconsistent projected centers");}}
        Check(jitter.x>=0&&jitter.y>=0,"Transport fixture could not recover both uniform lattice axes");
        std::array<std::vector<double>,2> uniformCenters;
        for(unsigned axis=0;axis<2;++axis){const unsigned extent=axis?frame.height:frame.width;
            for(unsigned tile=0;tile<placed.uniformGridSize[axis];++tile)
                uniformCenters[axis].push_back(double(std::min(tile*placement.baseSpacing+unsigned(jitter[axis]),extent-1)));}
        const auto coarseCoordinate=[&](unsigned query,unsigned axis){
            const auto& centers=uniformCenters[axis];const auto upper=std::upper_bound(centers.begin(),centers.end(),double(query));
            const unsigned lower=std::min(upper==centers.begin()?0u:unsigned(upper-centers.begin()-1),unsigned(centers.size()>1?centers.size()-2:0));
            double fraction;
            if(placement.continuousInterpolation){const auto right=std::min(std::size_t(lower)+1,centers.size()-1);
                fraction=(double(query)-centers[lower])/std::max(1.,centers[right]-centers[lower]);}
            else {const int extent=int(axis?frame.height:frame.width);const int full=std::clamp(int(query)-jitter[axis],0,extent-1);
                fraction=double(full-int(lower*placement.baseSpacing)+1)/double(placement.baseSpacing+2);}
            return std::make_pair(lower,std::clamp(fraction,0.,1.));
        };
        // Include a uniform stride-four sample and dense support around each
        // rejected probe. The dense subset prevents a one-pixel trim receiver
        // from falling between the regular samples; it never edits GPU input.
        std::vector<unsigned char> selected(pixels,0);for(unsigned y=0;y<frame.height;y+=4)for(unsigned x=0;x<frame.width;x+=4)selected[std::size_t(y)*frame.width+x]=1;
        const glm::dvec2 step(double(placement.baseSpacing)/2.); // eight slots: 2 x 2 cells, two representatives each
        for(const auto& probe:probes)if(probe.active&&!probe.available){
            const int xmin=std::max(0,int(std::floor(probe.screen.x-1.5*step.x))-1),xmax=std::min(int(frame.width)-1,int(std::ceil(probe.screen.x+1.5*step.x))+1);
            const int ymin=std::max(0,int(std::floor(probe.screen.y-1.5*step.y))-1),ymax=std::min(int(frame.height)-1,int(std::ceil(probe.screen.y+1.5*step.y))+1);
            for(int y=ymin;y<=ymax;++y)for(int x=xmin;x<=xmax;++x)selected[std::size_t(y)*frame.width+unsigned(x)]=1;}
        std::vector<int> receiverIndex(pixels,-1);std::vector<Receiver> receivers;receivers.reserve(pixels/16);
        std::uint64_t classified=0;
        for(unsigned y=0;y<frame.height;++y)for(unsigned x=0;x<frame.width;++x){const auto index=std::size_t(y)*frame.width+x;if(!selected[index])continue;
            const auto vp=sources.sourceViewPositions[index],wn=sources.sourceWorldNormals[index];if(vp.w<.5f||wn.w<.5f)continue;
            Receiver receiver;receiver.p=glm::dvec3(inverseView*glm::dvec4(glm::dvec3(vp),1));receiver.n=glm::normalize(glm::dvec3(wn));receiver.depth=std::max(.0001,std::abs(double(vp.z)));
            const auto delta=receiver.p-glm::dvec3(camera.position);const double distance=glm::length(delta);
            const auto primary=oracle.Nearest(glm::dvec3(camera.position),delta/distance,distance+.005);
            if(primary.triangle<0||std::abs(primary.t-distance)>.002)continue;const auto& triangle=oracle.triangles[unsigned(primary.triangle)];
            // Only independently classified receivers with matching triangle
            // ownership/normals contribute to this arithmetic oracle. This is
            // not a visual quality mask or a claim about omitted silhouettes.
            if(glm::dot(triangle.n,receiver.n)<.95)continue;
            receiver.rho=glm::dvec3(materials[triangle.object].albedo);receiver.valid=true;++classified;
            receiverIndex[index]=int(receivers.size());receivers.push_back(receiver);
        }
        const auto geometry=[&](const Receiver& receiver,const Probe& probe){
            if(!probe.active||probe.depth<=0)return 0.;const double agreement=glm::dot(receiver.n,probe.n);if(agreement<=.6)return 0.;
            const auto delta=probe.p-receiver.p;const double relative=std::max(std::abs(glm::dot(delta,receiver.n)),std::abs(glm::dot(delta,probe.n)))/std::max(.0001,std::min(receiver.depth,probe.depth));
            return relative>placement.relativeDepthThreshold?0.:std::exp2(-6.643856189774725*relative/double(placement.relativeDepthThreshold))*std::pow(agreement,8.);
        };
        const auto add=[](Receiver& receiver,const Probe& probe,double weight){if(weight<=0)return;
            receiver.oldMass+=weight;receiver.oldSum+=probe.light*weight;
            if(probe.available){receiver.mass+=weight;receiver.sum+=probe.light*weight;receiver.blackMass+=probe.black?weight:0.;receiver.partialMass+=probe.partial?weight:0.;}};
        // Four coarse samples are uniquely selected by the fixed placement
        // phase. Fine support is subsequently scattered from every live probe.
        for(unsigned y=0;y<frame.height;++y)for(unsigned x=0;x<frame.width;++x){const int at=receiverIndex[std::size_t(y)*frame.width+x];if(at<0)continue;auto& receiver=receivers[unsigned(at)];
            const auto coarseX=coarseCoordinate(x,0),coarseY=coarseCoordinate(y,1);
            const unsigned bx=coarseX.first,by=coarseY.first;const double fx=coarseX.second,fy=coarseY.second;
            for(unsigned corner=0;corner<4;++corner){const auto& probe=probes[bx+corner%2+(by+corner/2)*placed.uniformGridSize.x];
                add(receiver,probe,(corner%2?fx:1-fx)*(corner/2?fy:1-fy)*geometry(receiver,probe));}}
        // Iterate the entire actual representative set once. Its projected
        // compact support bounds the receiver scatter, independently of the
        // production tile search, active-mask culling, and bucket ordering.
        for(std::size_t index=placed.uniform.size();index<slots;++index){const auto& probe=probes[index];if(!probe.active||probe.activation<=0)continue;
            const int xmin=std::max(0,int(std::floor(probe.screen.x-1.5*step.x))),xmax=std::min(int(frame.width)-1,int(std::ceil(probe.screen.x+1.5*step.x)));
            const int ymin=std::max(0,int(std::floor(probe.screen.y-1.5*step.y))),ymax=std::min(int(frame.height)-1,int(std::ceil(probe.screen.y+1.5*step.y)));
            for(int y=ymin;y<=ymax;++y)for(int x=xmin;x<=xmax;++x){const int at=receiverIndex[std::size_t(y)*frame.width+unsigned(x)];if(at<0)continue;auto& receiver=receivers[unsigned(at)];
                const auto displacement=(probe.screen-glm::dvec2(x,y))/step;
                const double kernel=basis(displacement.x)*basis(displacement.y)*probe.activation;
                if(kernel>0)add(receiver,probe,kernel*geometry(receiver,probe));}}
        double maximum=0,counterfactualMaximum=0,rgbCounterfactualMaximum=0;std::uint64_t checked=0,affected=0,rgbAffected=0,partialReceivers=0,blackReceivers=0;
        for(std::size_t index=0;index<pixels;++index){const int at=receiverIndex[index];if(at<0)continue;const auto& receiver=receivers[unsigned(at)];
            glm::dvec4 expected(0),previous(0);if(receiver.mass>=.0001){expected=receiver.sum/receiver.mass;expected=glm::dvec4(glm::dvec3(expected)*receiver.rho*double(gi.intensity),std::clamp(expected.w,0.,1.));}
            if(receiver.oldMass>=.0001){previous=receiver.oldSum/receiver.oldMass;previous=glm::dvec4(glm::dvec3(previous)*receiver.rho*double(gi.intensity),std::clamp(previous.w,0.,1.));}
            const double error=glm::length(glm::dvec4(raw[index])-expected);maximum=std::max(maximum,error);++checked;
            const double changed=glm::length(expected-previous);counterfactualMaximum=std::max(counterfactualMaximum,changed);affected+=changed>1e-7;
            const double rgbChanged=glm::length(glm::dvec3(expected)-glm::dvec3(previous));rgbCounterfactualMaximum=std::max(rgbCounterfactualMaximum,rgbChanged);rgbAffected+=rgbChanged>1e-8;
            partialReceivers+=receiver.partialMass>1e-5;blackReceivers+=receiver.blackMass>1e-5;
            Check(error<4e-4*(1.+glm::length(expected)),"Raw GI differs from the independent origin-available reconstruction integral");
        }
        Check(classified>1000&&checked==classified,"Transport fixture did not classify enough independently owned receivers");
        std::cout<<"SURFACE CACHE TRANSPORT size="<<frame.width<<"x"<<frame.height<<" seed=17 directions=16 placement_phase=0 spatial=0 temporal=0 directional=0 screen_visibility=0"
            <<" continuous_interpolation="<<placement.continuousInterpolation
            <<" active="<<active<<" invalid="<<invalid<<" valid_partial="<<partial<<" valid_full_black="<<black
            <<" classified_receivers="<<classified<<" checked_receivers="<<checked<<" affected_receivers="<<affected<<" rgb_affected_receivers="<<rgbAffected
            <<" partial_weight_receivers="<<partialReceivers<<" black_weight_receivers="<<blackReceivers
            <<" rgba_max_error="<<maximum<<" full_count_integration_max_error="<<integrationMaximum
            <<" old_denominator_counterfactual_max_difference="<<counterfactualMaximum<<" old_denominator_rgb_max_difference="<<rgbCounterfactualMaximum<<std::endl;
        allChecked+=checked;allAffected+=affected;allRgbAffected+=rgbAffected;allPartial+=partial;allPartialReceivers+=partialReceivers;allBlack+=black;allInvalid+=invalid;
        allMaximum=std::max(allMaximum,maximum);allIntegrationMaximum=std::max(allIntegrationMaximum,integrationMaximum);allCounterfactual=std::max(allCounterfactual,counterfactualMaximum);
        allRgbCounterfactual=std::max(allRgbCounterfactual,rgbCounterfactualMaximum);if(affected>0&&rgbAffected>0)break;
    }
    Check(allInvalid>0&&allAffected>0&&allRgbAffected>0&&allCounterfactual>1e-7&&allRgbCounterfactual>1e-8,"Transport fixture did not exercise an invalid origin with observable RGB reconstruction support");
    Check(allPartial>0&&allPartialReceivers>0,"Transport fixture did not exercise partially unknown probe measurements in reconstruction");
    // Known black is an actual zero-light measurement. Turning the real lamp
    // off exercises that case without synthetic atlases or production hooks.
    frame.effects.areaLights.lights[0].enabled=false;render();
    const auto dark=lab.ReadbackSurfaceCacheRawGi(),darkOrigins=lab.ReadbackSurfaceCacheProbeOrigins(),darkIrradiance=lab.ReadbackSurfaceCacheProbeIrradiance();
    std::uint64_t darkReadable=0,darkFullBlack=0;double darkMaximum=0;
    for(const auto& value:dark){darkMaximum=std::max(darkMaximum,glm::length(glm::dvec3(value)));darkReadable+=value.w>0;}
    for(std::size_t index=0;index<darkOrigins.size();++index)if(darkOrigins[index].w>0){const auto value=darkIrradiance[index];
        Check(glm::length(glm::dvec3(value))<1e-8,"Lamp-off valid probe retained fabricated energy");darkFullBlack+=value.w>.999f;}
    Check(darkMaximum<1e-7&&darkReadable>1000&&darkFullBlack>0,"Known-black lamp-off measurements were excluded from valid reconstruction");
    std::cout<<"SURFACE CACHE TRANSPORT PASS checked_receivers="<<allChecked<<" invalid_origins="<<allInvalid<<" affected_receivers="<<allAffected<<" rgb_affected_receivers="<<allRgbAffected
        <<" partial_probes="<<allPartial<<" lit_full_black_probes="<<allBlack<<" lamp_off_full_black_probes="<<darkFullBlack
        <<" lamp_off_readable_receivers="<<darkReadable<<" lamp_off_max="<<darkMaximum<<" rgba_max_error="<<allMaximum
        <<" full_count_integration_max_error="<<allIntegrationMaximum<<" counterfactual_max_difference="<<allCounterfactual<<" rgb_counterfactual_max_difference="<<allRgbCounterfactual<<std::endl;
}
