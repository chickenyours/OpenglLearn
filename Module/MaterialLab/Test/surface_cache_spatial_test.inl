// Read the actual GPU signal before and after filtering. The high-sample
// reference is formed only in this test; it never enters the renderer.
void SurfaceCacheSpatialQuality(MaterialLabModule& lab,const Options& options) {
    auto fixture=options;fixture.sdfBanquet=false;fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);
    frame.width=256;frame.height=192;frame.effects.outputSize={0,0};frame.present=false;frame.screenProbeStudyHud=false;
    frame.camera=ProbePlacementCameraPreset(0,frame.OutputAspect());
    auto& gi=frame.effects.surfaceCacheGi;auto& spatial=gi.spatial;auto& temporal=gi.temporal;
    auto& placement=frame.effects.screenProbePlacement;auto& trace=placement.trace;
    gi.enabled=true;gi.useScreenProbes=true;gi.view=2;gi.probeDiagnostics=false;temporal.enabled=false;
    spatial.enabled=true;spatial.passes=3;spatial.halfKernelSize=1;spatial.strength=1;spatial.visibilityCheck=true;
    placement.baseSpacing=24;placement.frameIndex=0;placement.adaptive=true;placement.candidateCount=8;
    placement.freeze=false;placement.reset=true;trace.rayResolution=4;trace.samplingSeed=0;
    trace.maxSteps=512;trace.sdf.maxSteps=256;
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;placement.reset=false;temporal.reset=false;};
    const auto finite=[&](const auto& data,const char* label){
        Check(data.size()==std::size_t(frame.width)*frame.height,std::string("Missing or wrong-sized ")+label+": "+lab.LastError());
        for(const auto& p:data)for(unsigned c=0;c<4;++c)Check(std::isfinite(p[c]),std::string("Nonfinite ")+label);
    };
    const auto difference=[](const auto& a,const auto& b){Check(a.size()==b.size(),"Spatial comparison changed extent");double maximum=0;
        for(std::size_t i=0;i<a.size();++i)maximum=std::max(maximum,glm::length(glm::dvec3(a[i])-glm::dvec3(b[i])));return maximum;};
    const auto energy=[](const auto& data){double total=0;for(const auto& p:data)total+=p.x+p.y+p.z;return total;};
    const auto save=[&](const char* label){if(options.screenshot.empty())return;auto path=std::filesystem::path(options.screenshot);
        path.replace_filename(path.stem().string()+"_"+label+path.extension().string());Save(lab.Readback(),path.string());};
    render();const auto raw=lab.ReadbackSurfaceCacheRawGi(),filtered=lab.ReadbackSurfaceCacheSpatialGi();
    finite(raw,"unfiltered GI");finite(filtered,"spatial GI");
    Check(energy(filtered)>1e-3&&difference(raw,filtered)>1e-5,"Enabled spatial stage never changed the sampled GI");
    Check(lab.PipelineStatistics().surfaceCacheSpatialPasses>0&&lab.PipelineStatistics().surfaceCacheTemporalPasses==0,
        "Single-frame spatial fixture did not isolate spatial filtering from history");
    const auto rays=lab.ReadbackScreenProbeTrace();const auto atlas=lab.ReadbackSurfaceCacheSpatialRays();
    Check(atlas.size()>=rays.radianceStatus.size()*4,"Missing filtered ray atlas");
    unsigned unknown=0,changed=0;double metadataError=0;
    for(std::size_t i=0;i<rays.radianceStatus.size();++i) {
        const std::array<glm::vec4,3> parts{rays.hitWorldDistance[i],rays.hitPixelStepsMip[i],rays.rayDirectionDepthFetches[i]};
        for(unsigned part=0;part<3;++part)for(unsigned c=0;c<4;++c)
            metadataError=std::max(metadataError,std::abs(double(atlas[i*4+part+1][c])-parts[part][c]));
        Check(atlas[i*4].w==rays.radianceStatus[i].w,"Spatial filtering changed a ray status");
        const auto status=unsigned(std::round(rays.radianceStatus[i].w));
        const bool readable=status==1||status==5||status==9||status==10||status==13;
        if(!readable){if(status>0)++unknown;for(unsigned c=0;c<4;++c)Check(atlas[i*4][c]==rays.radianceStatus[i][c],"Spatial filtering filled an unknown ray with neighboring light");}
        else if(glm::length(glm::vec3(atlas[i*4])-glm::vec3(rays.radianceStatus[i]))>1e-6f)++changed;
    }
    Check(metadataError==0&&unknown>0&&changed>0,"Spatial stage lost exact ray metadata or failed to exercise readable / unknown rays");
    save("spatial");
    spatial.strength=0;render();
    Check(difference(lab.ReadbackSurfaceCacheRawGi(),lab.ReadbackSurfaceCacheSpatialGi())<2e-6,"Zero filter strength differs from raw GI");
    const auto copyAtlas=lab.ReadbackSurfaceCacheSpatialRays();const auto copyRays=lab.ReadbackScreenProbeTrace(false);
    for(std::size_t i=0;i<copyRays.radianceStatus.size();++i)for(unsigned c=0;c<4;++c)
        Check(copyAtlas[i*4][c]==copyRays.radianceStatus[i][c],"Zero strength failed to copy ray radiance exactly");
    spatial.strength=1;spatial.enabled=false;render();
    Check(lab.PipelineStatistics().surfaceCacheSpatialPasses==0,"Disabled spatial filter still scheduled passes");
    Check(difference(raw,lab.ReadbackSurfaceCacheRawGi())<1e-6,"Spatial toggle contaminated the unfiltered reference");
    spatial.enabled=true;render();
    Check(difference(filtered,lab.ReadbackSurfaceCacheSpatialGi())<2e-6,"Spatial on/off round trip depends on stale state");
    frame.effects.shadows.sunIntensity=0;render();
    Check(std::abs(energy(lab.ReadbackSurfaceCacheSpatialGi()))<1e-5,"Spatial filtering fabricated light in an unlit scene");
    frame.effects.shadows.sunIntensity=6;render();const auto twice=lab.ReadbackSurfaceCacheSpatialGi();
    double lightScaleError=0;for(std::size_t i=0;i<filtered.size();++i)for(unsigned c=0;c<3;++c)lightScaleError+=std::abs(double(twice[i][c])-2.*filtered[i][c]);
    Check(lightScaleError<energy(filtered)*.003+1e-4,"Spatial weights changed with radiance or failed light-energy linearity");
    frame.effects.shadows.sunIntensity=3;render();
    spatial.passes=1;spatial.halfKernelSize=2;render();finite(lab.ReadbackSurfaceCacheSpatialGi(),"larger spatial kernel");
    Check(lab.PipelineStatistics().surfaceCacheSpatialPasses>0,"Alternate spatial kernel failed to schedule filtering");
    spatial.passes=3;spatial.halfKernelSize=1;render();

    // Mask out geometry borders for the flat-surface noise metric. A separate
    // mask below explicitly checks thin/curved geometry and unsupported pixels.
    const auto inverseView=glm::inverse(frame.camera->view);const auto pixelCount=raw.size();
    std::vector<int> surface(pixelCount,-1);std::vector<bool> interior(pixelCount,false),edge(pixelCount,false);
    for(std::size_t i=0;i<pixelCount;++i){const auto p=rays.sourceViewPositions[i],n=rays.sourceWorldNormals[i];if(p.w<.5f||n.w<.5f)continue;
        const auto w=glm::vec3(inverseView*glm::vec4(glm::vec3(p),1));
        if(std::abs(w.y)<.002f&&n.y>.999f)surface[i]=0;
        else if(std::abs(w.y-5)<.002f&&n.y<-.999f)surface[i]=1;
        else if(std::abs(w.z+3.5f)<.002f&&n.z>.999f)surface[i]=2;
        else if(std::abs(w.x+5)<.002f&&n.x>.999f)surface[i]=3;
        else if(std::abs(w.x-5)<.002f&&n.x<-.999f)surface[i]=4;
    }
    unsigned interiorCount=0;
    for(unsigned y=3;y+3<frame.height;++y)for(unsigned x=3;x+3<frame.width;++x){const auto i=std::size_t(y)*frame.width+x;if(surface[i]<0)continue;
        bool same=true;for(int dy=-3;dy<=3&&same;++dy)for(int dx=-3;dx<=3;++dx)if(surface[std::size_t(int(y)+dy)*frame.width+unsigned(int(x)+dx)]!=surface[i]){same=false;break;}
        if(same){interior[i]=true;++interiorCount;}}
    Check(interiorCount>1000,"Spatial noise fixture lacks flat interior receivers");
    unsigned edgeCount=0;
    for(unsigned y=2;y+2<frame.height;++y)for(unsigned x=2;x+2<frame.width;++x){const auto i=std::size_t(y)*frame.width+x;
        const auto p=rays.sourceViewPositions[i],n=rays.sourceWorldNormals[i];if(p.w<.5f||n.w<.5f)continue;
        const auto unit=glm::normalize(glm::vec3(n));bool discontinuity=false;
        for(int dy=-2;dy<=2&&!discontinuity;++dy)for(int dx=-2;dx<=2;++dx){const auto at=std::size_t(int(y)+dy)*frame.width+unsigned(int(x)+dx);
            const auto q=rays.sourceViewPositions[at],nn=rays.sourceWorldNormals[at];
            const auto worldDelta=glm::vec3(inverseView*glm::vec4(glm::vec3(q)-glm::vec3(p),0));
            if(q.w<.5f||nn.w<.5f||glm::dot(unit,glm::normalize(glm::vec3(nn)))<.8f||
                std::abs(glm::dot(worldDelta,unit))>std::max(.002f,std::abs(p.z)*.01f)){discontinuity=true;break;}}
        if(discontinuity){edge[i]=true;++edgeCount;}}
    Check(edgeCount>100,"Spatial edge fixture lacks thin / curved geometry boundaries");
    std::vector<glm::dvec3> reference(pixelCount,glm::dvec3(0));trace.rayResolution=16;spatial.enabled=false;
    constexpr unsigned referenceSamples=8,samples=6;
    for(unsigned seed=100;seed<100+referenceSamples;++seed){trace.samplingSeed=seed;render();const auto image=lab.ReadbackSurfaceCacheRawGi();finite(image,"spatial reference");
        for(std::size_t i=0;i<pixelCount;++i)reference[i]+=glm::dvec3(image[i])/double(referenceSamples);}
    trace.rayResolution=4;spatial.enabled=true;const glm::dvec3 luminance(.2126,.7152,.0722);
    double rawMse=0,spatialMse=0,rawEdgeMse=0,spatialEdgeMse=0;const unsigned patchWidth=(frame.width+15)/16,patchHeight=(frame.height+15)/16;
    const auto patchSlots=std::size_t(patchWidth)*patchHeight*5;
    double rawPatchMse=0,spatialPatchMse=0;
    for(unsigned seed=0;seed<samples;++seed){trace.samplingSeed=seed;render();const auto a=lab.ReadbackSurfaceCacheRawGi(),b=lab.ReadbackSurfaceCacheSpatialGi();
        finite(a,"noisy raw GI");finite(b,"denoised GI");std::vector<double> pa(patchSlots,0),pb(patchSlots,0);std::vector<unsigned> counts(patchSlots,0);
        for(std::size_t i=0;i<pixelCount;++i)if(edge[i]){const double ea=glm::dot(glm::dvec3(a[i])-reference[i],luminance),eb=glm::dot(glm::dvec3(b[i])-reference[i],luminance);
            rawEdgeMse+=ea*ea/(double(samples)*edgeCount);spatialEdgeMse+=eb*eb/(double(samples)*edgeCount);}
        for(std::size_t i=0;i<pixelCount;++i)if(interior[i]){const double ea=glm::dot(glm::dvec3(a[i])-reference[i],luminance),eb=glm::dot(glm::dvec3(b[i])-reference[i],luminance);
            rawMse+=ea*ea/(double(samples)*interiorCount);spatialMse+=eb*eb/(double(samples)*interiorCount);
            const auto patch=(std::size_t(surface[i])*patchHeight+unsigned(i/frame.width)/16)*patchWidth+unsigned(i%frame.width)/16;
            pa[patch]+=ea;pb[patch]+=eb;++counts[patch];}
        unsigned patches=0;double ma=0,mb=0;for(std::size_t p=0;p<patchSlots;++p)if(counts[p]>=16){const double a=pa[p]/counts[p],b=pb[p]/counts[p];ma+=a*a;mb+=b*b;++patches;}
        Check(patches>5,"Spatial patch metric has insufficient samples");rawPatchMse+=ma/(double(patches)*samples);spatialPatchMse+=mb/(double(patches)*samples);
    }
    std::cout<<"Surface Cache spatial convergence raw_mse="<<rawMse<<" spatial_mse="<<spatialMse
        <<" raw_patch_mse="<<rawPatchMse<<" spatial_patch_mse="<<spatialPatchMse<<" pixels="<<interiorCount
        <<" raw_edge_mse="<<rawEdgeMse<<" spatial_edge_mse="<<spatialEdgeMse<<" edge_pixels="<<edgeCount<<std::endl;
    Check(spatialMse<rawMse&&spatialPatchMse<rawPatchMse,"Spatial filtering did not reduce residual flat-surface / broad-patch error against an independent high-ray reference");
    Check(spatialEdgeMse<=rawEdgeMse*1.15+1e-8,"Spatial smoothing appreciably increased GI error at geometry boundaries");

    // A 24-pixel grid at this small test extent is intentionally coarse.
    // Also measure denser probes: 256 pixels / 8 ~= 1280 pixels / 40, much
    // closer to the world-space probe separation in the interactive viewer.
    placement.baseSpacing=8;placement.reset=true;trace.rayResolution=16;spatial.enabled=false;
    std::vector<glm::dvec3> denseReference(pixelCount,glm::dvec3(0));
    constexpr unsigned denseReferenceSamples=8,denseSamples=12;
    for(unsigned seed=200;seed<200+denseReferenceSamples;++seed){trace.samplingSeed=seed;render();const auto image=lab.ReadbackSurfaceCacheRawGi();finite(image,"dense spatial reference");
        for(std::size_t i=0;i<pixelCount;++i)denseReference[i]+=glm::dvec3(image[i])/double(denseReferenceSamples);}
    trace.rayResolution=4;spatial.enabled=true;double denseRawMse=0,denseSpatialMse=0,denseRawPatchMse=0,denseSpatialPatchMse=0;
    unsigned denseChangedRays=0,denseReadableRays=0;
    for(unsigned seed=20;seed<20+denseSamples;++seed){trace.samplingSeed=seed;render();const auto a=lab.ReadbackSurfaceCacheRawGi(),b=lab.ReadbackSurfaceCacheSpatialGi();
        finite(a,"dense raw GI");finite(b,"dense spatial GI");
        std::vector<double> pa(patchSlots,0),pb(patchSlots,0);std::vector<unsigned> counts(patchSlots,0);
        for(std::size_t i=0;i<pixelCount;++i)if(interior[i]){const double ea=glm::dot(glm::dvec3(a[i])-denseReference[i],luminance),eb=glm::dot(glm::dvec3(b[i])-denseReference[i],luminance);
            denseRawMse+=ea*ea/(double(denseSamples)*interiorCount);denseSpatialMse+=eb*eb/(double(denseSamples)*interiorCount);
            const auto patch=(std::size_t(surface[i])*patchHeight+unsigned(i/frame.width)/16)*patchWidth+unsigned(i%frame.width)/16;
            pa[patch]+=ea;pb[patch]+=eb;++counts[patch];}
        unsigned patches=0;double ma=0,mb=0;for(std::size_t p=0;p<patchSlots;++p)if(counts[p]>=16){const double a=pa[p]/counts[p],b=pb[p]/counts[p];ma+=a*a;mb+=b*b;++patches;}
        denseRawPatchMse+=ma/(double(patches)*denseSamples);denseSpatialPatchMse+=mb/(double(patches)*denseSamples);
        if(seed==20){const auto before=lab.ReadbackScreenProbeTrace(false);const auto after=lab.ReadbackSurfaceCacheSpatialRays();
            Check(after.size()>=before.radianceStatus.size()*4,"Dense filtered rays missing");
            for(std::size_t ray=0;ray<before.radianceStatus.size();++ray){const auto status=unsigned(std::round(before.radianceStatus[ray].w));
                if(status!=1&&status!=5&&status!=9&&status!=10&&status!=13)continue;++denseReadableRays;
                if(glm::length(glm::vec3(after[ray*4])-glm::vec3(before.radianceStatus[ray]))>1e-6f)++denseChangedRays;}}
    }
    std::cout<<"Surface Cache spatial density spacing=8 raw_mse="<<denseRawMse<<" spatial_mse="<<denseSpatialMse
        <<" raw_patch_mse="<<denseRawPatchMse<<" spatial_patch_mse="<<denseSpatialPatchMse
        <<" changed_rays="<<denseChangedRays<<" readable_rays="<<denseReadableRays<<std::endl;
    Check(denseSpatialMse<denseRawMse&&denseSpatialPatchMse<denseRawPatchMse,
        "Spatial filtering failed to reduce error at a probe density representative of the interactive viewer");

    // A probe overlay shows ray statuses. This separate geometric diagnostic
    // identifies actual reconstruction holes without treating legal black as
    // missing. Detect support on thin posts and curved spheres independently.
    trace.samplingSeed=0;placement.baseSpacing=64;placement.candidateCount=16;
    unsigned baseHoles=0,adaptiveHoles=0,thinSupported=0,sphereSupported=0,adaptivePixels=0;
    unsigned acceptedVisiblePairs=0,blockedWithoutVisibility=0,blockedWithVisibility=0;
    spatial.halfKernelSize=2;
    for(unsigned camera=0;camera<3;++camera){frame.camera=ProbePlacementCameraPreset(camera,frame.OutputAspect());
        placement.adaptive=false;placement.reset=true;render();const auto before=lab.ReadbackSurfaceCacheProbeCoverage();finite(before,"uniform geometric coverage");
        placement.adaptive=true;placement.reset=true;render();const auto after=lab.ReadbackSurfaceCacheProbeCoverage();finite(after,"adaptive geometric coverage");
        const auto gbuffer=lab.ReadbackScreenProbeTrace();const auto image=lab.ReadbackSurfaceCacheSpatialGi();finite(image,"edge-preserving GI");
        const auto inv=glm::inverse(frame.camera->view);
        for(std::size_t i=0;i<after.size();++i){const auto p=gbuffer.sourceViewPositions[i],n=gbuffer.sourceWorldNormals[i];
            const bool valid=p.w>=.5f&&n.w>=.5f&&glm::dot(glm::vec3(n),glm::vec3(n))>=.5f;
            if(!valid){Check(glm::length(glm::vec3(image[i]))<1e-6f,"Spatial filtering spilled illumination into background");continue;}
            if(before[i].w<.5f)++baseHoles;if(after[i].w<.5f){++adaptiveHoles;Check(glm::length(glm::vec3(image[i]))<1e-6f,"Spatial filtering invented GI for a surface without a compatible probe");}
            if(after[i].x>.9f&&after[i].y>.1f&&after[i].y<.6f&&after[i].w>.5f)++adaptivePixels;
            const auto w=glm::vec3(inv*glm::vec4(glm::vec3(p),1));
            if(after[i].w>.5f&&std::abs(w.x+.1f)<.04f&&w.y>.05f&&w.y<2.3f&&std::abs(w.z-.5f)<.11f)++thinSupported;
            if(after[i].w>.5f&&std::abs(glm::length(w-glm::vec3(1.85f,.9f,-.6f))-.9f)<.01f)++sphereSupported;
        }
        gi.view=6;render();save((std::string("coverage_")+std::to_string(camera)).c_str());gi.view=2;
        const auto probes=lab.ReadbackScreenProbePlacement();
        // Use the prepared source mesh triangles and instance transforms, but
        // deliberately bypass both GPU and CPU BVH traversal for this oracle.
        std::vector<Render::ProbeTriangle> triangles;const auto scene=lab.PreparedProbeSdfScene();Check(bool(scene),"Missing geometry for visibility reference");
        for(const auto& instance:scene->Instances()){const auto mesh=instance.geometryMesh?instance.geometryMesh:instance.mesh;
            for(const auto& triangle:mesh->Triangles())triangles.push_back({
                glm::vec3(instance.localToWorld*glm::vec4(triangle.a,1)),
                glm::vec3(instance.localToWorld*glm::vec4(triangle.b,1)),
                glm::vec3(instance.localToWorld*glm::vec4(triangle.c,1)),triangle.diffuseReflectance});}
        const auto probePoint=[&](std::size_t index){const bool uniform=index<probes.uniform.size();const auto slot=uniform?index:index-probes.uniform.size();
            const auto p=uniform?probes.uniform[slot]:probes.spawned[slot],n=uniform?probes.uniformNormals[slot]:probes.spawnedNormals[slot];
            return glm::dvec3(p)+glm::normalize(glm::dvec3(n))*double(gi.rayBias);};
        const auto inspectPairs=[&](const auto& table,bool checkedVisibility){
            const auto slots=probes.uniform.size()+probes.spawned.size();Check(table.size()>=slots*8,"Missing spatial neighbor table");
            unsigned blocked=0;
            for(std::size_t probe=0;probe<slots;++probe)for(unsigned pair=0;pair<8;++pair){const auto value=table[probe*8+pair];
                if(value.x<.5f||value.y<.0001f)continue;const auto neighbor=std::size_t(std::round(value.x)-1);
                Check(neighbor<slots,"Spatial filter selected an out-of-range probe");
                const auto a=probePoint(probe),b=probePoint(neighbor),delta=b-a;const double distance=glm::length(delta);
                if(distance<=double(gi.rayBias)*.25)continue;
                const bool occluded=SurfaceCacheReferenceOccluded(a,delta/distance,std::max(distance-.0001,0.),triangles);
                if(occluded)++blocked;else if(checkedVisibility)++acceptedVisiblePairs;
            }return blocked;
        };
        spatial.visibilityCheck=false;render();blockedWithoutVisibility+=inspectPairs(lab.ReadbackSurfaceCacheSpatialNeighbors(),false);
        spatial.visibilityCheck=true;render();blockedWithVisibility+=inspectPairs(lab.ReadbackSurfaceCacheSpatialNeighbors(),true);
    }
    std::cout<<"Surface Cache adaptive coverage uniform_holes="<<baseHoles<<" adaptive_holes="<<adaptiveHoles
        <<" adaptive_pixels="<<adaptivePixels<<" thin_supported="<<thinSupported<<" sphere_supported="<<sphereSupported<<std::endl;
    Check(baseHoles>0&&adaptiveHoles<baseHoles&&adaptivePixels>0&&thinSupported>0&&sphereSupported>0,
        "Adaptive placement failed to improve true geometric coverage on the thin / curved fixture");
    std::cout<<"Surface Cache spatial visibility accepted_visible="<<acceptedVisiblePairs<<" blocked_without_check="<<blockedWithoutVisibility
        <<" blocked_with_check="<<blockedWithVisibility<<std::endl;
    Check(acceptedVisiblePairs>10&&blockedWithVisibility==0,"Spatial neighbor sharing crossed geometry according to independent double-precision triangle visibility");
    Check(blockedWithoutVisibility>0,"Spatial visibility fixture did not exercise a blocked probe pair");

    placement.baseSpacing=32;placement.candidateCount=8;spatial.halfKernelSize=1;
    unsigned defaultUniformHoles=0,defaultAdaptiveHoles=0,defaultActiveBase=0,defaultActiveAdaptive=0;
    unsigned defaultThinSupported=0,defaultSphereSupported=0,defaultReceivers=0;
    for(unsigned camera=0;camera<3;++camera){frame.camera=ProbePlacementCameraPreset(camera,frame.OutputAspect());
        placement.adaptive=false;placement.reset=true;render();const auto before=lab.ReadbackSurfaceCacheProbeCoverage();finite(before,"default uniform coverage");
        placement.adaptive=true;placement.reset=true;render();const auto after=lab.ReadbackSurfaceCacheProbeCoverage();finite(after,"default adaptive coverage");
        const auto gbuffer=lab.ReadbackScreenProbeTrace();const auto inv=glm::inverse(frame.camera->view);
        for(std::size_t i=0;i<after.size();++i){if(before[i].x>.9f&&before[i].z>.9f&&before[i].w<.5f)++defaultUniformHoles;
            if(after[i].x>.9f&&after[i].z>.9f&&after[i].w<.5f)++defaultAdaptiveHoles;
            const auto p=gbuffer.sourceViewPositions[i],n=gbuffer.sourceWorldNormals[i];if(p.w<.5f||n.w<.5f)continue;++defaultReceivers;
            if(after[i].w<.5f)continue;const auto w=glm::vec3(inv*glm::vec4(glm::vec3(p),1));
            if(std::abs(w.x+.1f)<.04f&&w.y>.05f&&w.y<2.3f&&std::abs(w.z-.5f)<.11f)++defaultThinSupported;
            if(std::abs(glm::length(w-glm::vec3(1.85f,.9f,-.6f))-.9f)<.01f)++defaultSphereSupported;}
        const auto probes=lab.ReadbackScreenProbePlacement();
        defaultActiveBase+=unsigned(std::count_if(probes.uniform.begin(),probes.uniform.end(),[](const auto& p){return p.w>.5f;}));
        defaultActiveAdaptive+=unsigned(std::count_if(probes.spawned.begin(),probes.spawned.end(),[](const auto& p){return p.w>.5f;}));
        if(camera==0){gi.view=6;render();save("coverage_default");gi.view=2;}
    }
    std::cout<<"Surface Cache adaptive default spacing=32 candidates=8 uniform_holes="<<defaultUniformHoles<<" adaptive_holes="<<defaultAdaptiveHoles
        <<" receivers="<<defaultReceivers<<" active_base="<<defaultActiveBase<<" active_adaptive="<<defaultActiveAdaptive
        <<" thin_supported="<<defaultThinSupported<<" sphere_supported="<<defaultSphereSupported<<std::endl;
    Check(defaultUniformHoles>0&&defaultAdaptiveHoles<defaultUniformHoles*.25&&defaultActiveAdaptive>0&&defaultThinSupported>0&&defaultSphereSupported>0,
        "Default adaptive settings failed to improve actual receiver coverage");

    frame.camera=ProbePlacementCameraPreset(0,frame.OutputAspect());placement.baseSpacing=24;placement.candidateCount=8;placement.reset=true;
    spatial.halfKernelSize=1;
    temporal.enabled=true;temporal.maxFrames=8;temporal.directionPhases=8;temporal.reset=true;render();render();
    Check(lab.PipelineStatistics().surfaceCacheHistoryUsed,"Spatial output did not feed GI temporal accumulation");
    spatial.strength=.5f;render();Check(!lab.PipelineStatistics().surfaceCacheHistoryUsed,"Spatial parameter change reused incompatible GI history");
    const auto history=lab.ReadbackSurfaceCacheTemporalHistory();finite(history,"spatial-reset history");
    for(const auto& p:history)Check(p.w<=1.0001f,"Spatial parameter edit retained old GI frame counts");
    spatial.strength=1;temporal.reset=true;for(unsigned i=0;i<12;++i)render();save("spatial_temporal");
    std::cout<<"Surface Cache spatial passed: changed_rays="<<changed<<" unknown_preserved="<<unknown
        <<" metadata_error="<<metadataError<<" light_scale_error="<<lightScaleError<<std::endl;
}
