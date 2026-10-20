// Structural reconstruction checks use analytic radiance, not Monte Carlo
// estimates. This fixture deliberately keeps the existing edge-quality camera
// paths and its 1280x800 before/after measurements completely independent.
void SurfaceCacheAdaptiveSurfaceQuality(MaterialLabModule& lab,const Options& options) {
    auto fixture=options;fixture.sealedGiRoom=true;fixture.sdfBanquet=false;
    fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);
    frame.width=options.renderSizeSet?options.benchmarkWidth:640;
    frame.height=options.renderSizeSet?options.benchmarkHeight:400;
    frame.effects.outputSize={0,0};frame.present=false;frame.screenProbeStudyHud=false;
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;
    gi.contact.enabled=false; // Analytic probe reconstruction must remain independent of real receiver rays.
    auto& trace=placement.trace;
    gi.enabled=true;gi.useScreenProbes=true;gi.view=2;gi.collectProbeCoverage=true;
    gi.probeDiagnostics=false;gi.spatial.enabled=false;gi.temporal.enabled=false;
    gi.reconstructionDebug=1;
    placement.enabled=true;placement.adaptive=true;placement.cornerRefinement=true;
    // Isolate the analytic kernel contract from the separate, approximate
    // screen-segment visibility estimator exercised below as an A/B check.
    placement.reconstructionVisibility=false;
    placement.curvatureRefinement=false; // this fixture isolates the existing dual-surface/kernel contract
    placement.baseSpacing=32;placement.frameIndex=0;placement.freeze=false;placement.reset=true;
    trace.rayResolution=8;trace.cosineHemisphere=true;trace.samplingSeed=0;
    trace.maxSteps=512;trace.sdf.maxSteps=256;
    const auto pixels=std::size_t(frame.width)*frame.height;
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());
        frame.effects.cameraCut=false;placement.reset=false;gi.temporal.reset=false;};
    const auto finite=[&](const auto& image,const char* label){
        Check(image.size()==pixels,std::string("Missing adaptive structural ")+label+": "+lab.LastError());
        for(const auto& p:image)for(unsigned channel=0;channel<4;++channel)
            Check(std::isfinite(p[channel]),"Nonfinite adaptive structural image");
    };
    const auto field=[](glm::vec3 p){return std::clamp(.45+.04*(double(p.x)+2.*double(p.y)+.5*double(p.z)),.05,.95);};
    const auto planeId=[](glm::vec3 p,glm::vec3 n){
        if(std::abs(p.y-SealedGiRoomMinimum.y)<.02f&&n.y>.98f)return 0;
        if(std::abs(p.x-SealedGiRoomMinimum.x)<.02f&&n.x>.98f)return 1;
        if(std::abs(p.x-SealedGiRoomMaximum.x)<.02f&&n.x<-.98f)return 2;
        if(std::abs(p.z-SealedGiRoomMinimum.z)<.02f&&n.z>.98f)return 3;
        if(std::abs(p.z-SealedGiRoomMaximum.z)<.02f&&n.z<-.98f)return 4;
        if(std::abs(p.y-SealedGiRoomMaximum.y)<.02f&&n.y<-.98f)return 5;
        return -1;
    };
    const auto save=[&](const auto& values,const std::string& label){
        if(options.screenshot.empty())return;
        FramePixels image;image.width=frame.width;image.height=frame.height;image.rgba.resize(pixels*4);
        // Diagnostics have a fixed [0,1] scale, independent of scene exposure.
        for(std::size_t i=0;i<pixels;++i){for(unsigned channel=0;channel<3;++channel)
            image.rgba[i*4+channel]=std::uint8_t(std::lround(std::clamp(double(values[i][channel]),0.,1.)*255.));
            image.rgba[i*4+3]=255;}
        auto path=std::filesystem::path(options.screenshot);
        path.replace_filename(path.stem().string()+"_"+label+path.extension().string());Save(image,path.string());
    };
    auto base=SealedGiRoomCameraPreset(1,frame.OutputAspect());
    const float halfHeight=2.5f,halfWidth=halfHeight*frame.OutputAspect();
    base.projection=glm::ortho(-halfWidth,halfWidth,-halfHeight,halfHeight,base.nearPlane,base.farPlane);
    const auto inverseBase=glm::inverse(base.view);
    const auto right=glm::normalize(glm::vec3(inverseBase[0]));
    const auto forward=glm::normalize(-glm::vec3(inverseBase[2]));
    const float worldPixel=2.f*halfWidth/float(frame.width);
    struct Source {
        std::vector<glm::vec3> world,normal;
        std::vector<float> depth;
        std::vector<int> plane;
        std::vector<unsigned char> valid;
    };
    struct Sample {
        glm::vec3 p,n;glm::vec2 screen;float depth=0,state=0;
        unsigned global=0,slot=0;bool adaptive=false;
    };
    const auto readSource=[&](const Render::PipelineCamera& camera){
        const auto data=lab.ReadbackScreenProbeSources();
        finite(data.sourceViewPositions,"source positions");finite(data.sourceWorldNormals,"source normals");
        Source source;source.world.resize(pixels);source.normal.resize(pixels);source.depth.resize(pixels);
        source.plane.assign(pixels,-1);source.valid.assign(pixels,0);
        const auto inverseView=glm::inverse(camera.view);
        for(std::size_t i=0;i<pixels;++i){const auto p=data.sourceViewPositions[i],n=data.sourceWorldNormals[i];
            if(p.w<.5f||n.w<.5f||glm::dot(glm::vec3(n),glm::vec3(n))<.5f)continue;
            source.valid[i]=1;source.world[i]=glm::vec3(inverseView*glm::vec4(glm::vec3(p),1));
            source.normal[i]=glm::normalize(glm::vec3(n));source.depth[i]=std::max(std::abs(p.z),.0001f);
            source.plane[i]=planeId(source.world[i],source.normal[i]);
        }
        return source;
    };
    const auto geometryWeight=[&](const Source& source,std::size_t pixel,const Sample& sample){
        if(!source.valid[pixel]||sample.state<.5f||sample.depth<=0)return 0.;
        const auto n=source.normal[pixel];const double agreement=glm::dot(n,sample.n);
        if(agreement<=.6)return 0.;
        const auto delta=sample.p-source.world[pixel];
        const double plane=std::max(std::abs(glm::dot(delta,n)),std::abs(glm::dot(delta,sample.n)));
        const double relative=plane/std::max(double(std::min(source.depth[pixel],sample.depth)),.0001);
        if(relative>placement.relativeDepthThreshold)return 0.;
        return std::exp2(-6.643856189774725*relative/double(placement.relativeDepthThreshold))*std::pow(agreement,8.);
    };
    const auto basis=[](double value){const double a=std::abs(value);
        if(a<.5)return .75-a*a;const double edge=std::max(0.,1.5-a);return .5*edge*edge;};
    std::uint64_t totalDualCells=0,totalSourceSamples=0,totalOutside=0,totalCrossSurface=0,totalNominalOnly=0;
    for(const bool continuous:{true,false}) {
      placement.continuousInterpolation=continuous;
      for(unsigned candidates:{4u,8u,16u}) {
        // Exercise the old public A/B branch without repeating the full layout
        // matrix: two camera phases and the eight-slot dual-surface layout.
        if(!continuous&&candidates!=8)continue;
        const unsigned cameraPhases=continuous?5u:2u;
        placement.candidateCount=candidates;
        // Public contract: a subcell owns two distinct surface slots.
        const unsigned cellCount=candidates/2,columns=cellCount>=8?4u:2u,rows=cellCount/columns;
        const glm::ivec2 layout{int(columns),int(rows)};
        const glm::dvec2 step(double(placement.baseSpacing)/columns,double(placement.baseSpacing)/rows);
        const auto radius=step*1.5;
        std::vector<float> previousError;std::vector<int> previousPlane;
        double phaseDelta=0;std::uint64_t phasePairs=0;
        for(unsigned phase=0;phase<cameraPhases;++phase){
            auto camera=base;camera.position+=right*(worldPixel*.25f*float(phase));
            camera.view=glm::lookAt(camera.position,camera.position+forward,glm::vec3(0,1,0));
            frame.camera=camera;frame.effects.cameraCut=true;placement.reset=true;
            trace.samplingSeed=0;trace.selectedProbe=std::numeric_limits<std::uint32_t>::max();
            gi.reconstructionDebug=1;render();
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
            while(lab.ProbeSdfQualityStatistics().pending){render();Check(std::chrono::steady_clock::now()<deadline,"Adaptive structural SDF preparation timed out");}
            const auto source=readSource(camera);const auto probes=lab.ReadbackScreenProbePlacement();
            const auto constant=lab.ReadbackSurfaceCacheRawGi(),coverage=lab.ReadbackSurfaceCacheProbeCoverage();
            finite(constant,"constant field");finite(coverage,"geometric coverage");
            Check(probes.candidateCount==candidates&&probes.uniform.size()==std::size_t(probes.uniformGridSize.x)*probes.uniformGridSize.y,
                "Adaptive structural probe layout does not match the requested slot budget");
            Check(probes.frameIndex%8==0,"Analytic structural fixture unexpectedly advanced its fixed placement phase");
            Check(probes.spawned.size()==probes.uniform.size()*candidates&&probes.spawnedNormals.size()==probes.spawned.size()&&
                probes.uniformNormals.size()==probes.uniform.size(),"Adaptive structural probe metadata is incomplete");
            const auto vp=camera.projection*camera.view;
            std::vector<Sample> uniform(probes.uniform.size()),adaptive;
            std::vector<int> slotToSample(probes.spawned.size(),-1);
            double maximumReprojection=0,maximumSourceDistance=0;std::uint64_t activeUniform=0;
            const auto sampleAt=[&](glm::vec4 p,glm::vec4 nd,unsigned global,unsigned slot,bool isAdaptive){
                Sample sample;sample.p=glm::vec3(p);sample.n=glm::normalize(glm::vec3(nd));
                sample.depth=nd.w;sample.state=p.w;sample.global=global;sample.slot=slot;sample.adaptive=isAdaptive;
                const auto clip=vp*glm::vec4(sample.p,1);
                Check(clip.w>0,"Active adaptive probe projected behind its source camera");
                sample.screen=(glm::vec2(clip)/clip.w*.5f+.5f)*glm::vec2(frame.width,frame.height)-.5f;
                const auto pixel=glm::ivec2(glm::round(sample.screen));
                Check(pixel.x>=0&&pixel.y>=0&&pixel.x<int(frame.width)&&pixel.y<int(frame.height),"Adaptive representative left the captured image");
                const auto at=std::size_t(pixel.y)*frame.width+unsigned(pixel.x);
                Check(source.valid[at],"Adaptive representative does not originate from valid geometry");
                const double reprojection=glm::length(sample.screen-glm::vec2(pixel));
                const double sourceDistance=glm::length(sample.p-source.world[at]);
                maximumReprojection=std::max(maximumReprojection,reprojection);
                maximumSourceDistance=std::max(maximumSourceDistance,sourceDistance);
                Check(reprojection<.03&&sourceDistance<std::max(.0003f,worldPixel*.01f)&&glm::dot(sample.n,source.normal[at])>.9998f,
                    "Adaptive representative is an averaged/invented point instead of an actual GBuffer sample");
                ++totalSourceSamples;return sample;
            };
            for(unsigned i=0;i<uniform.size();++i)if(probes.uniform[i].w>.5f){
                uniform[i]=sampleAt(probes.uniform[i],probes.uniformNormals[i],i,i,false);++activeUniform;}
            // Infer the placement phase from real, independently projected
            // source points. Do not reproduce the shader's jitter generator.
            glm::ivec2 jitter(-1);
            for(unsigned i=0;i<uniform.size();++i)if(uniform[i].state>.5f){
                const glm::uvec2 tile(i%probes.uniformGridSize.x,i/probes.uniformGridSize.x);
                for(unsigned axis=0;axis<2;++axis)if(tile[axis]+1<probes.uniformGridSize[axis]){
                    const int value=int(std::lround(uniform[i].screen[axis]))-int(tile[axis]*placement.baseSpacing);
                    Check(value>=0&&value<int(placement.baseSpacing),"Uniform probe left its physical placement interval");
                    if(jitter[axis]<0)jitter[axis]=value;
                    Check(jitter[axis]==value,"Uniform lattice has inconsistent projected sample centers");}}
            Check(jitter.x>=0&&jitter.y>=0,"Structural fixture could not identify both uniform lattice axes");
            std::array<std::vector<double>,2> uniformCenters;
            for(unsigned axis=0;axis<2;++axis){const unsigned extent=axis?frame.height:frame.width;
                for(unsigned tile=0;tile<probes.uniformGridSize[axis];++tile)
                    uniformCenters[axis].push_back(double(std::min(tile*placement.baseSpacing+unsigned(jitter[axis]),extent-1)));}
            const auto coarseCoordinate=[&](unsigned query,unsigned axis){
                const auto& centers=uniformCenters[axis];
                const auto upper=std::upper_bound(centers.begin(),centers.end(),double(query));
                const unsigned lower=std::min(upper==centers.begin()?0u:unsigned(upper-centers.begin()-1),unsigned(centers.size()>1?centers.size()-2:0));
                double fraction;
                if(placement.continuousInterpolation){const auto right=std::min(std::size_t(lower)+1,centers.size()-1);
                    fraction=(double(query)-centers[lower])/std::max(1.,centers[right]-centers[lower]);}
                else {const int extent=int(axis?frame.height:frame.width);
                    const int full=std::clamp(int(query)-jitter[axis],0,extent-1);
                    fraction=double(full-int(lower*placement.baseSpacing)+1)/double(placement.baseSpacing+2);}
                return std::make_pair(lower,std::clamp(fraction,0.,1.));
            };
            for(unsigned i=0;i<probes.spawned.size();++i)if(probes.spawned[i].w>.5f){
                slotToSample[i]=int(adaptive.size());
                auto sample=sampleAt(probes.spawned[i],probes.spawnedNormals[i],unsigned(uniform.size())+i,i,true);
                const unsigned tileIndex=i/candidates,cellIndex=(i%candidates)/2;
                const glm::ivec2 tile(int(tileIndex%probes.uniformGridSize.x),int(tileIndex/probes.uniformGridSize.x));
                const glm::ivec2 cell(int(cellIndex%columns),int(cellIndex/columns));
                const auto begin=tile*int(placement.baseSpacing)+cell*int(placement.baseSpacing)/layout;
                const auto end=glm::min(tile*int(placement.baseSpacing)+(cell+1)*int(placement.baseSpacing)/layout,glm::ivec2(frame.width,frame.height));
                const auto pixel=glm::ivec2(glm::round(sample.screen));
                Check(pixel.x>=begin.x&&pixel.y>=begin.y&&pixel.x<end.x&&pixel.y<end.y,
                    "Actual adaptive representative escaped the subcell that owns its slot");
                adaptive.push_back(sample);
            }
            double constantMaximum=0;std::uint64_t valid=0,holes=0;
            for(std::size_t i=0;i<pixels;++i){if(!source.valid[i]){
                    Check(glm::length(glm::vec3(constant[i]))<1e-6f,"Constant reconstruction filled invalid background");continue;}
                ++valid;if(coverage[i].w<.5f){++holes;continue;}
                constantMaximum=std::max(constantMaximum,std::abs(double(constant[i].x)-1.));
                Check(std::abs(constant[i].x-1.f)<2e-5f,"Constant analytic field was not normalized on supported geometry");
            }
            struct DualCell {glm::ivec2 begin,end;std::array<unsigned,2> slots;};
            std::vector<DualCell> dualCells;
            for(unsigned tileIndex=0;tileIndex<uniform.size();++tileIndex)for(unsigned cellIndex=0;cellIndex<cellCount;++cellIndex){
                const glm::ivec2 tile(int(tileIndex%probes.uniformGridSize.x),int(tileIndex/probes.uniformGridSize.x));
                const glm::ivec2 cell(int(cellIndex%columns),int(cellIndex/columns));
                const auto begin=tile*int(placement.baseSpacing)+cell*int(placement.baseSpacing)/layout;
                const auto end=glm::min(tile*int(placement.baseSpacing)+(cell+1)*int(placement.baseSpacing)/layout,glm::ivec2(frame.width,frame.height));
                std::array<unsigned,6> planeCounts{};unsigned other=0;
                for(int y=begin.y;y<end.y;++y)for(int x=begin.x;x<end.x;++x){const auto at=std::size_t(y)*frame.width+unsigned(x);
                    if(!source.valid[at])continue;if(source.plane[at]<0)++other;else ++planeCounts[unsigned(source.plane[at])];}
                std::array<int,2> expected{{-1,-1}};unsigned distinct=0;
                for(unsigned id=0;id<6;++id)if(planeCounts[id]){if(distinct<2)expected[distinct]=int(id);++distinct;}
                if(other||distinct!=2||planeCounts[unsigned(expected[0])]<8||planeCounts[unsigned(expected[1])]<8)continue;
                const unsigned first=tileIndex*candidates+cellIndex*2,second=first+1;
                Check(slotToSample[first]>=0&&slotToSample[second]>=0,"A two-surface subcell lost one of its independent representatives");
                const auto& a=adaptive[unsigned(slotToSample[first])];const auto& b=adaptive[unsigned(slotToSample[second])];
                const int ia=planeId(a.p,a.n),ib=planeId(b.p,b.n);
                Check(ia!=ib&&((ia==expected[0]&&ib==expected[1])||(ia==expected[1]&&ib==expected[0])),
                    "Two active subcell slots represent the same face instead of both visible surfaces");
                for(int y=begin.y;y<end.y;++y)for(int x=begin.x;x<end.x;++x){const auto at=std::size_t(y)*frame.width+unsigned(x);
                    if(source.valid[at])Check(coverage[at].w>=.5f,"One face of a two-surface cell remains geometrically unsupported");}
                dualCells.push_back({begin,end,{{first,second}}});
            }
            Check(!dualCells.empty(),"Orthographic fixture did not exercise two separate room faces in a single adaptive subcell");
            totalDualCells+=dualCells.size();
            gi.reconstructionDebug=3;render();const auto affine=lab.ReadbackSurfaceCacheRawGi();finite(affine,"analytic affine field");
            std::vector<float> errors(pixels);double squared=0,bias=0,maximum=0;std::uint64_t measured=0;
            for(std::size_t i=0;i<pixels;++i)if(source.valid[i]&&coverage[i].w>=.5f){
                const double error=double(affine[i].x)-field(source.world[i]);errors[i]=float(error);
                Check(affine[i].x>=.04998f&&affine[i].x<=.95002f&&std::abs(affine[i].x-affine[i].y)<1e-5f&&
                    std::abs(affine[i].x-affine[i].z)<1e-5f,"Analytic field left the convex source range");
                squared+=error*error;bias+=error;maximum=std::max(maximum,std::abs(error));++measured;
                if(phase&&source.plane[i]>=0&&source.plane[i]==previousPlane[i]){
                    const double delta=error-double(previousError[i]);phaseDelta+=delta*delta;++phasePairs;}
            }
            Check(measured>1000,"Adaptive affine fixture has insufficient supported receivers");
            if(phase==0){
                // A different ray realization must not alter an analytic probe
                // field. This directly separates layout bias from MC variance.
                trace.samplingSeed=37;render();const auto otherSeed=lab.ReadbackSurfaceCacheRawGi();finite(otherSeed,"analytic seed invariance");
                double seedDifference=0;for(std::size_t i=0;i<pixels;++i)
                    seedDifference=std::max(seedDifference,glm::length(glm::dvec3(affine[i])-glm::dvec3(otherSeed[i])));
                Check(seedDifference<2e-6,"Changing Monte Carlo seed changed the analytic reconstruction signal");
                trace.samplingSeed=0;
                // Choose a moved representative away from image borders. Its
                // impulse reveals support measured from its actual position.
                int selected=-1;glm::vec2 nominal(0);double best=-1;
                for(const auto& cell:dualCells)for(unsigned slot:cell.slots){const auto index=slotToSample[slot];const auto& sample=adaptive[unsigned(index)];
                    if(sample.screen.x<float(radius.x+3)||sample.screen.y<float(radius.y+3)||
                        sample.screen.x>float(frame.width)-float(radius.x+4)||sample.screen.y>float(frame.height)-float(radius.y+4))continue;
                    const auto center=(glm::vec2(cell.begin+cell.end)-1.f)*.5f;
                    const double displacement=glm::length(sample.screen-center);
                    if(displacement>best){best=displacement;selected=index;nominal=center;}
                }
                Check(selected>=0&&best>.75,"Fixture did not exercise an adaptive point relocated away from its nominal cell center");
                const auto& chosen=adaptive[unsigned(selected)];trace.selectedProbe=chosen.global;gi.reconstructionDebug=4;render();
                const auto impulse=lab.ReadbackSurfaceCacheRawGi();finite(impulse,"selected-probe impulse");
                double maximumOutside=0,maximumCrossSurface=0,maximumImpulseError=0,maximumAffineContractError=0,response=0;
                std::uint64_t outside=0,crossSurface=0,nominalOnly=0,compatibleNominalOnly=0,contractPixels=0;
                for(unsigned y=0;y<frame.height;++y)for(unsigned x=0;x<frame.width;++x){const auto at=std::size_t(y)*frame.width+x;
                    if(!source.valid[at])continue;const double value=impulse[at].x;response=std::max(response,value);
                    const glm::dvec2 delta=glm::abs(glm::dvec2(chosen.screen)-glm::dvec2(x,y));
                    const bool inSupport=delta.x<radius.x-.001&&delta.y<radius.y-.001;
                    const bool outsideSupport=delta.x>radius.x+.001||delta.y>radius.y+.001;
                    if(outsideSupport){++outside;maximumOutside=std::max(maximumOutside,std::abs(value));
                        const auto nominalDelta=glm::abs(glm::dvec2(nominal)-glm::dvec2(x,y));
                        if(nominalDelta.x<radius.x-.01&&nominalDelta.y<radius.y-.01){
                            ++nominalOnly;compatibleNominalOnly+=geometryWeight(source,at,chosen)>0;}}
                    if(inSupport&&geometryWeight(source,at,chosen)==0){++crossSurface;maximumCrossSurface=std::max(maximumCrossSurface,std::abs(value));}
                    if(delta.x>radius.x+2||delta.y>radius.y+2)continue;
                    // Independent brute-force mathematical reference: sum all
                    // actual representatives, with no tile window, cell AABB,
                    // grouping bucket or nearest-candidate selection.
                    double mass=0,affineSum=0,selectedMass=0;
                    const auto coarseX=coarseCoordinate(x,0),coarseY=coarseCoordinate(y,1);
                    const auto baseX=coarseX.first,baseY=coarseY.first;
                    const double fx=coarseX.second,fy=coarseY.second;
                    for(unsigned corner=0;corner<4;++corner){const unsigned xx=std::min(baseX+corner%2,probes.uniformGridSize.x-1);
                        const unsigned yy=std::min(baseY+corner/2,probes.uniformGridSize.y-1);const auto& sample=uniform[xx+yy*probes.uniformGridSize.x];
                        const double weight=(corner%2?fx:1.-fx)*(corner/2?fy:1.-fy)*geometryWeight(source,at,sample);
                        if(weight>0){mass+=weight;affineSum+=weight*field(sample.p);}}
                    for(const auto& sample:adaptive){const auto d=(glm::dvec2(sample.screen)-glm::dvec2(x,y))/step;
                        const double kernel=basis(d.x)*basis(d.y);if(kernel<=0)continue;
                        const double weight=kernel*geometryWeight(source,at,sample);if(weight<=0)continue;
                        mass+=weight;affineSum+=weight*field(sample.p);if(sample.global==chosen.global)selectedMass+=weight;}
                    if(mass<.0002)continue; // Avoid the shared usability threshold's numerical boundary.
                    maximumImpulseError=std::max(maximumImpulseError,std::abs(value-selectedMass/mass));
                    maximumAffineContractError=std::max(maximumAffineContractError,std::abs(double(affine[at].x)-affineSum/mass));++contractPixels;
                }
                Check(outside>100&&crossSurface>5&&contractPixels>100,"Impulse fixture did not exercise support exterior / incompatible geometry / analytic contract");
                Check(response>1e-3,"Selected adaptive probe never contributed to its analytic impulse image");
                Check(maximumOutside<2e-6&&maximumCrossSurface<2e-6,"Adaptive kernel leaked outside its actual radius or through incompatible geometry");
                Check(maximumImpulseError<3e-4&&maximumAffineContractError<3e-4,
                    "GPU reconstruction disagreed with the actual-position analytic kernel reference");
                totalOutside+=outside;totalCrossSurface+=crossSurface;totalNominalOnly+=nominalOnly;
                std::cout<<"SURFACE CACHE ADAPTIVE kernel candidates="<<candidates<<" selected="<<chosen.global
                    <<" continuous_interpolation="<<continuous
                    <<" displacement_pixels="<<best<<" radius_x="<<radius.x<<" radius_y="<<radius.y
                    <<" outside_samples="<<outside<<" nominal_only_samples="<<nominalOnly
                    <<" compatible_nominal_only_samples="<<compatibleNominalOnly<<" cross_surface_samples="<<crossSurface
                    <<" maximum_outside="<<maximumOutside<<" maximum_cross_surface="<<maximumCrossSurface
                    <<" maximum_response="<<response<<" contract_pixels="<<contractPixels
                    <<" impulse_contract_max_error="<<maximumImpulseError<<" affine_contract_max_error="<<maximumAffineContractError
                    <<" analytic_seed_max_difference="<<seedDifference<<std::endl;
                const auto interpolation=continuous?"":"legacy_";
                save(affine,interpolation+std::string("slots_")+std::to_string(candidates)+"_affine");
                save(impulse,interpolation+std::string("slots_")+std::to_string(candidates)+"_impulse");
                gi.reconstructionDebug=3;trace.selectedProbe=std::numeric_limits<std::uint32_t>::max();
            }
            std::cout<<"SURFACE CACHE ADAPTIVE affine candidates="<<candidates<<" phase="<<phase<<" camera_translation_pixels="<<.25*phase
                <<" continuous_interpolation="<<continuous
                <<" active_uniform="<<activeUniform<<" active_adaptive="<<adaptive.size()<<" dual_surface_cells="<<dualCells.size()
                <<" valid_pixels="<<valid<<" unsupported_pixels="<<holes<<" measured_pixels="<<measured
                <<" constant_max_error="<<constantMaximum<<" source_projection_max_pixels="<<maximumReprojection
                <<" source_position_max_distance="<<maximumSourceDistance<<" luma_mse="<<squared/double(measured)
                <<" signed_bias="<<bias/double(measured)<<" maximum_receiver_error="<<maximum<<std::endl;
            previousError=std::move(errors);previousPlane=source.plane;
        }
        Check(phasePairs>1000,"Subpixel motion did not retain stable same-plane receivers");
        std::cout<<"SURFACE CACHE ADAPTIVE subpixel candidates="<<candidates<<" phases="<<cameraPhases<<" phase_step_pixels=.25"
            <<" continuous_interpolation="<<continuous
            <<" residual_delta_mse="<<phaseDelta/double(phasePairs)<<" compared_receivers="<<phasePairs
            <<" stochastic_samples=0 affine_exactness=report_only"<<std::endl;
      }
    }
    Check(totalDualCells>0&&totalSourceSamples>100&&totalOutside>100&&totalCrossSurface>10&&totalNominalOnly>0,
        "Adaptive structural fixture did not exercise its layout and support obligations");
    // A separate, bounded visibility experiment. Uniform source points keep
    // their identities when visibility changes adaptive allocation. The oracle
    // below intersects the original transformed triangles in double precision;
    // it does not reuse the GPU BVH, SDF, or its three visibility taps.
    placement.continuousInterpolation=true;
    auto visibilityCamera=base;visibilityCamera.position={0,4,0};
    visibilityCamera.view=glm::lookAt(visibilityCamera.position,glm::vec3(0,0,0),glm::vec3(0,0,-1));
    frame.camera=visibilityCamera;placement.baseSpacing=std::clamp((frame.height+5u)/6u,4u,128u);
    placement.candidateCount=8;placement.reconstructionVisibility=false;
    placement.reset=true;placement.freeze=false;trace.samplingSeed=0;gi.reconstructionDebug=1;render();
    const auto visibilitySource=readSource(visibilityCamera);const auto visibilityProbes=lab.ReadbackScreenProbePlacement();
    std::vector<Render::ProbeTriangle> visibilityTriangles;
    const auto visibilityScene=lab.PreparedProbeSdfScene();Check(bool(visibilityScene),"Missing source mesh for visibility reference");
    for(const auto& instance:visibilityScene->Instances()){const auto mesh=instance.geometryMesh?instance.geometryMesh:instance.mesh;
        for(const auto& triangle:mesh->Triangles())visibilityTriangles.push_back({
            glm::vec3(instance.localToWorld*glm::vec4(triangle.a,1)),
            glm::vec3(instance.localToWorld*glm::vec4(triangle.b,1)),
            glm::vec3(instance.localToWorld*glm::vec4(triangle.c,1)),triangle.diffuseReflectance});}
    struct BlockedPair {unsigned probe;std::size_t receiver;};
    std::vector<BlockedPair> blockedPairs;std::vector<unsigned> selectedSources;
    const auto visibilityVp=visibilityCamera.projection*visibilityCamera.view;
    for(unsigned probe=0;probe<visibilityProbes.uniform.size()&&selectedSources.size()<2;++probe){
        const auto p=visibilityProbes.uniform[probe],nd=visibilityProbes.uniformNormals[probe];
        if(p.w<.5f||nd.w<=0||planeId(glm::vec3(p),glm::vec3(nd))!=0)continue;
        const auto clip=visibilityVp*glm::vec4(glm::vec3(p),1);
        const auto screen=(glm::vec2(clip)/clip.w*.5f+.5f)*glm::vec2(frame.width,frame.height)-.5f;
        const auto center=glm::ivec2(glm::round(screen));const int reach=int(placement.baseSpacing)-1;
        const auto to=glm::dvec3(p)+glm::normalize(glm::dvec3(nd))*double(gi.rayBias);
        const auto start=blockedPairs.size();
        for(int y=std::max(0,center.y-reach);y<=std::min(int(frame.height)-1,center.y+reach)&&blockedPairs.size()<start+4;y+=3)
            for(int x=std::max(0,center.x-reach);x<=std::min(int(frame.width)-1,center.x+reach)&&blockedPairs.size()<start+4;x+=3){
                const auto at=std::size_t(y)*frame.width+unsigned(x);
                if(!visibilitySource.valid[at]||visibilitySource.plane[at]!=0)continue;
                const auto from=glm::dvec3(visibilitySource.world[at])+glm::dvec3(visibilitySource.normal[at])*double(gi.rayBias);
                const auto delta=to-from;const double distance=glm::length(delta);if(distance<=double(gi.rayBias)*2)continue;
                // Prioritize definite on-screen foreground evidence, while the
                // independent triangle test establishes a real obstruction.
                const auto middle=glm::vec3((from+to)*.5);
                const auto projected=visibilityVp*glm::vec4(middle,1);
                const auto uv=glm::vec2(projected)/projected.w*.5f+.5f;
                const auto middlePixel=glm::ivec2(glm::floor(uv*glm::vec2(frame.width,frame.height)));
                if(middlePixel.x<0||middlePixel.y<0||middlePixel.x>=int(frame.width)||middlePixel.y>=int(frame.height))continue;
                const auto middleAt=std::size_t(middlePixel.y)*frame.width+unsigned(middlePixel.x);
                const double middleDepth=-(visibilityCamera.view*glm::vec4(middle,1)).z;
                if(!visibilitySource.valid[middleAt]||middleDepth-double(visibilitySource.depth[middleAt])<.15)continue;
                if(!SurfaceCacheReferenceOccluded(from,delta/distance,distance-.0001,visibilityTriangles))continue;
                blockedPairs.push_back({probe,at});
            }
        if(blockedPairs.size()>start)selectedSources.push_back(probe);
    }
    unsigned readablePairs=0,observedBlocked=0,decreased=0,increased=0,inconclusive=0;
    double largestIncrease=0,maximumBlockedValue=0;
    for(const auto probe:selectedSources){
        trace.selectedProbe=probe;gi.reconstructionDebug=4;placement.reconstructionVisibility=false;render();
        const auto without=lab.ReadbackSurfaceCacheRawGi();finite(without,"visibility off impulse");
        placement.reconstructionVisibility=true;render();
        const auto with=lab.ReadbackSurfaceCacheRawGi();finite(with,"visibility on impulse");
        const auto after=lab.ReadbackScreenProbePlacement();
        Check(glm::length(glm::vec3(after.uniform[probe])-glm::vec3(visibilityProbes.uniform[probe]))<1e-5f,
            "Visibility A/B changed the selected uniform probe's physical identity");
        for(const auto& pair:blockedPairs)if(pair.probe==probe){const auto a=without[pair.receiver],b=with[pair.receiver];
            if(a.w<.5f||b.w<.5f||a.x<=1e-5f){++inconclusive;continue;}
            ++readablePairs;const double change=double(b.x)-double(a.x);largestIncrease=std::max(largestIncrease,change);
            increased+=change>2e-5;decreased+=change< -2e-5;
            if(b.x<2e-6f){++observedBlocked;maximumBlockedValue=std::max(maximumBlockedValue,double(b.x));
                Check(b.x<=a.x+2e-5f,"Observed blocked visibility pair increased its selected-probe contribution");}
        }
    }
    Check(!blockedPairs.empty()&&observedBlocked>0,"Top-down visibility fixture did not exercise a readable occluded probe-pixel pair");
    Check(maximumBlockedValue<2e-6,"Known blocked visibility subset retained selected-probe light");
    std::cout<<"SURFACE CACHE ADAPTIVE visibility triangle_blocked_candidates="<<blockedPairs.size()
        <<" selected_uniform_sources="<<selectedSources.size()<<" readable_off_positive_pairs="<<readablePairs
        <<" observed_blocked="<<observedBlocked<<" decreased="<<decreased<<" increased="<<increased
        <<" inconclusive="<<inconclusive<<" largest_increase="<<largestIncrease
        <<" maximum_blocked_value="<<maximumBlockedValue
        <<" validation="<<(observedBlocked?"observed_subset":"not_exercised")<<std::endl;
    placement.reconstructionVisibility=false;trace.selectedProbe=std::numeric_limits<std::uint32_t>::max();
    std::cout<<"SURFACE CACHE ADAPTIVE COMPLETE size="<<frame.width<<"x"<<frame.height
        <<" layouts=3 subpixel_phases=5 legacy_layouts=1 legacy_subpixel_phases=2 actual_source_samples="<<totalSourceSamples<<" dual_surface_cells="<<totalDualCells
        <<" outside_kernel_samples="<<totalOutside<<" incompatible_surface_samples="<<totalCrossSurface
        <<" nominal_only_samples="<<totalNominalOnly<<" monte_carlo_variance=excluded"<<std::endl;
}
