// Included after the screen-trace regression helpers. Exact CPU triangles are
// a geometry oracle; this does not reproduce the SDF or GPU marching algorithm.
void ProbeSdfTracingQuality(MaterialLabModule& lab,const Options& options) {
    auto frame=MakeProbeSdfTraceFrame(options);
    frame.present=false;frame.screenProbeStudyHud=false;
    frame.width=options.studySizeSet?options.benchmarkWidth:320;
    frame.height=options.studySizeSet?options.benchmarkHeight:240;
    frame.effects.outputSize={0,0};frame.camera=ProbePlacementCameraPreset(0,frame.OutputAspect());
    auto& placement=frame.effects.screenProbePlacement;
    auto& tracing=placement.trace;auto& sdf=tracing.sdf;
    placement.frameIndex=0;placement.freeze=false;placement.reset=true;placement.baseSpacing=32;placement.candidateCount=8;
    tracing.rayResolution=4;tracing.maxSteps=512;tracing.maxMip=10;tracing.displayMode=0;
    sdf.enabled=true;sdf.useMesh=true;sdf.useGlobal=true;sdf.meshTraceDistance=2;sdf.maxSteps=192;sdf.sliceView=0;
    sdf.visualMode=0;
    const auto render=[&] {
        Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;placement.reset=false;
    };
    const auto save=[&](const char* label) {
        if(options.screenshot.empty())return;
        auto path=std::filesystem::path(options.screenshot);
        path.replace_filename(path.stem().string()+"_"+label+path.extension().string());Save(lab.Readback(),path.string());
    };
    const auto globalEqual=[&](const std::vector<glm::vec4>& a,const std::vector<glm::vec4>& b,const char* label) {
        Check(!a.empty()&&a.size()==b.size(),std::string(label)+": incomplete Global SDF atlas");
        for(std::size_t pixel=0;pixel<a.size();++pixel)
            for(unsigned channel=0;channel<4;++channel)
                Check(std::isfinite(a[pixel][channel])&&std::abs(a[pixel][channel]-b[pixel][channel])<.00002f,
                    std::string(label)+": Global SDF differs from complete CPU union at voxel "+std::to_string(pixel));
    };
    const auto checkUnion=[&](const char* label,bool completeReference) {
        const auto scene=lab.PreparedProbeSdfScene();Check(bool(scene),"Probe SDF scene was not prepared");
        globalEqual(lab.ReadbackProbeGlobalSdf(),scene->GlobalPixels(),label);
        if(completeReference) {
            const auto rebuilt=Render::BuildProbeSdfScene(scene->Instances(),scene->Settings());
            globalEqual(scene->GlobalPixels(),rebuilt->GlobalPixels(),label);
        }
    };
    const auto inspect=[&](const ScreenProbeTraceReadback& data,const ScreenProbePlacementReadback& probes,
        const char* label,bool exactGeometry) {
        const std::size_t raysPerProbe=std::size_t(tracing.rayResolution)*tracing.rayResolution;
        Check(data.probeSlots==probes.uniform.size()+probes.spawned.size()&&
            data.radianceStatus.size()==std::size_t(data.probeSlots)*raysPerProbe&&
            data.radianceStatus.size()==data.hitWorldDistance.size()&&
            data.radianceStatus.size()==data.hitPixelStepsMip.size()&&
            data.radianceStatus.size()==data.rayDirectionDepthFetches.size(),
            std::string(label)+": incomplete combined ray atlas");
        std::array<unsigned,17> counts{};
        const auto triangles=exactGeometry?ProbeSdfTriangles(frame.screenProbeStudyState):std::vector<Render::ProbeTriangle>{};
        const auto vp=frame.camera->projection*frame.camera->view,inverseVP=glm::inverse(vp);
        for(std::size_t index=0;index<data.radianceStatus.size();++index) {
            const auto color=data.radianceStatus[index],hit=data.hitWorldDistance[index];
            const auto metadata=data.hitPixelStepsMip[index],ray=data.rayDirectionDepthFetches[index];
            for(unsigned channel=0;channel<4;++channel)
                Check(std::isfinite(color[channel])&&std::isfinite(hit[channel])&&
                    std::isfinite(metadata[channel])&&std::isfinite(ray[channel]),std::string(label)+": nonfinite ray result");
            const auto status=unsigned(std::round(color.w));
            Check(status<counts.size()&&std::abs(color.w-float(status))<.001f,std::string(label)+": unclassified ray status");
            ++counts[status];
            const auto probe=index/raysPerProbe;
            const auto position=probe<probes.uniform.size()?probes.uniform[probe]:probes.spawned[probe-probes.uniform.size()];
            const auto normalDepth=probe<probes.uniform.size()?probes.uniformNormals[probe]:probes.spawnedNormals[probe-probes.uniform.size()];
            if(position.w<.5f){Check(status==0,std::string(label)+": inactive probe emitted a ray");continue;}
            if(status<9||status>12)continue;
            Check(hit.w>0&&hit.w<=tracing.maxDistance+.002f&&metadata.z>0&&metadata.z<=sdf.maxSteps&&metadata.w>=0&&ray.w>0,
                std::string(label)+": invalid SDF geometry hit/traversal accounting");
            const auto normal=glm::normalize(glm::vec3(normalDepth)),direction=glm::normalize(glm::vec3(ray));
            Check(glm::dot(normal,direction)>.02f,std::string(label)+": SDF retried a rejected launch hemisphere");
            float bias=tracing.normalBias;const auto clip=vp*glm::vec4(glm::vec3(position),1);
            if(clip.w>0) {
                const auto corner=inverseVP*glm::vec4(glm::vec3(clip)/clip.w+
                    glm::vec3(1.f/frame.width,1.f/frame.height,0),1);
                bias=std::max(bias,2.f*std::abs(glm::dot(glm::vec3(corner)/corner.w-glm::vec3(position),normal)));
            }
            const auto origin=glm::vec3(position)+normal*bias;
            Check(glm::length(glm::vec3(hit)-(origin+direction*hit.w))<.004f,
                std::string(label)+": SDF hit does not lie on the actual outgoing world ray");
            if(exactGeometry) {
                float nearest=tracing.maxDistance+.002f;
                for(const auto& triangle:triangles)nearest=PlacementTriangleDistance(origin,direction,triangle,nearest);
                Check(nearest<tracing.maxDistance+.001f&&std::abs(nearest-hit.w)<.006f,
                    std::string(label)+": SDF hit is not the independent first triangle intersection");
            }
            const bool readable=status==9||status==10;
            if(!readable) {
                Check(glm::length(glm::vec3(color))<1e-7f&&metadata.x==-1&&metadata.y==-1,
                    std::string(label)+": unavailable radiance was replaced by fabricated surface color");
                continue;
            }
            const auto pixel=glm::ivec2(glm::round(glm::vec2(metadata)));
            Check(pixel.x>=0&&pixel.y>=0&&pixel.x<int(frame.width)&&pixel.y<int(frame.height)&&
                glm::length(glm::vec2(pixel)-glm::vec2(metadata))<.001f,
                std::string(label)+": readable SDF hit has an invalid source pixel");
            const auto at=std::size_t(pixel.y)*frame.width+pixel.x;
            Check(at<data.sourceHdr.size()&&at<data.sourceViewPositions.size()&&data.sourceViewPositions[at].w>.5f&&
                glm::length(glm::vec3(color)-glm::vec3(data.sourceHdr[at]))<.0002f,
                std::string(label)+": SDF radiance is not the pre-marker HDR source");
            const auto sourceWorld=glm::vec3(glm::inverse(frame.camera->view)*glm::vec4(glm::vec3(data.sourceViewPositions[at]),1));
            const auto hitClip=vp*glm::vec4(glm::vec3(hit),1);
            const auto corner=inverseVP*glm::vec4(glm::vec3(hitClip)/hitClip.w+
                glm::vec3(2.f/frame.width,2.f/frame.height,0),1);
            const auto footprint=glm::length(glm::vec3(corner)/corner.w-glm::vec3(hit));
            Check(glm::length(sourceWorld-glm::vec3(hit))<footprint*1.6f+sdf.hitEpsilon*2,
                std::string(label)+": SDF hit sampled a different visible surface");
        }
        std::cout<<"Probe SDF "<<label<<": screen="<<counts[1]<<", Mesh readable/unreadable="<<counts[9]<<'/'<<counts[11]
            <<", Global readable/unreadable="<<counts[10]<<'/'<<counts[12]<<", nohit="<<counts[13]
            <<", uncovered="<<counts[14]<<", budget="<<counts[15]<<'\n';
        return counts;
    };

    render();const auto baseline=lab.ReadbackScreenProbeTrace();
    const auto raw=lab.ReadbackScreenProbeTrace(false,true);
    const auto probes=lab.ReadbackScreenProbePlacement();
    const auto baseCounts=inspect(baseline,probes,"near Mesh + far Global",true);
    Check(baseCounts[9]+baseCounts[11]>0&&baseCounts[10]+baseCounts[12]>0&&baseCounts[11]+baseCounts[12]>0,
        "SDF fixture did not exercise near Mesh, far Global and unavailable geometric hits");
    Check(baseCounts[9]+baseCounts[10]>0,"SDF fallback did not recover readable HDR on any screen miss");
    unsigned screenHits=0,recovered=0;
    for(std::size_t ray=0;ray<raw.radianceStatus.size();++ray) {
        if(raw.radianceStatus[ray].w==1) {
            Check(baseline.radianceStatus[ray]==raw.radianceStatus[ray]&&baseline.hitWorldDistance[ray]==raw.hitWorldDistance[ray]&&
                baseline.hitPixelStepsMip[ray]==raw.hitPixelStepsMip[ray]&&baseline.rayDirectionDepthFetches[ray]==raw.rayDirectionDepthFetches[ray],
                "SDF fallback modified a completed screen hit");++screenHits;
        } else if(baseline.radianceStatus[ray].w>=9&&baseline.radianceStatus[ray].w<=12)++recovered;
    }
    Check(screenHits>0&&recovered>0,"SDF fallback lacked both preserved screen hits and newly recovered geometry");
    checkUnion("initial GPU union",true);save("radiance");
    tracing.displayMode=1;render();save("status");tracing.displayMode=0;
    sdf.sliceView=2;render();save("mesh_slice");sdf.sliceView=1;render();save("global_slice");sdf.sliceView=0;
    render();Check(lab.PipelineStatistics().probeSdfUploadedTexels==0,"Unchanged SDF scene uploaded cached mesh/global data again");

    // Turning illumination off changes radiance only. A zero-valued successful
    // source lookup remains a readable hit, distinct from unavailable color.
    const auto lighting=frame.effects.lighting;
    const auto shadows=frame.effects.shadows;
    frame.effects.lighting.ambientAndExposure={0,0,0,1};
    for(auto& color:frame.effects.lighting.lightColors)color=glm::vec4(0);
    frame.effects.shadows.sunIntensity=0;frame.effects.sky.enabled=false;frame.effects.spotLight.enabled=false;frame.effects.areaLights.count=0;
    render();const auto black=lab.ReadbackScreenProbeTrace();inspect(black,lab.ReadbackScreenProbePlacement(),"black legal radiance",false);
    unsigned blackReadable=0;
    for(std::size_t ray=0;ray<black.radianceStatus.size();++ray) {
        Check(black.radianceStatus[ray].w==baseline.radianceStatus[ray].w,"Lighting changes altered SDF geometry/readability classification");
        const auto status=unsigned(std::round(black.radianceStatus[ray].w));
        if(status==1||status==9||status==10) {
            Check(glm::length(glm::vec3(black.radianceStatus[ray]))<1e-6f,"Black readable surface was mistaken for missing radiance");++blackReadable;
        }
    }
    Check(blackReadable>0,"Zero-light fixture lost all legal readable hits");frame.effects.lighting=lighting;frame.effects.shadows=shadows;

    sdf.useGlobal=false;sdf.meshTraceDistance=tracing.maxDistance;render();
    const auto meshCounts=inspect(lab.ReadbackScreenProbeTrace(),lab.ReadbackScreenProbePlacement(),"Mesh only",true);
    Check(meshCounts[9]+meshCounts[11]>0&&meshCounts[10]+meshCounts[12]==0,"Mesh-only tracing did not use local distance assets");
    sdf.useGlobal=true;sdf.useMesh=false;render();
    const auto globalCounts=inspect(lab.ReadbackScreenProbeTrace(),lab.ReadbackScreenProbePlacement(),"Global only",true);
    Check(globalCounts[10]+globalCounts[12]>0&&globalCounts[9]+globalCounts[11]==0,"Global-only tracing did not use merged distance samples");
    sdf.useMesh=true;sdf.meshTraceDistance=2;

    const auto scene=lab.PreparedProbeSdfScene();Check(bool(scene),"SDF scene missing before movement regression");
    const auto meshGeneration=scene->MeshGeneration(),bakedVoxels=scene->Statistics().bakedVoxels;
    const auto originalGlobal=scene->GlobalPixels();const auto totalGlobalTexels=originalGlobal.size();
    for(const float offset:{.8f,-.6f,0.f}) {
        frame.screenProbeStudyState.occluderOffset=offset;render();
        const auto moved=lab.PreparedProbeSdfScene();const auto statistics=lab.PipelineStatistics();
        Check(moved->MeshGeneration()==meshGeneration&&moved->Statistics().bakedVoxels==bakedVoxels,
            "Transform-only movement rebaked/repacked immutable Mesh SDF assets");
        Check(moved->Statistics().staticVoxelUpdates==0&&moved->Statistics().dynamicVoxelUpdates>0&&
            moved->Statistics().updatedVoxels>0&&moved->Statistics().updatedVoxels<moved->Statistics().totalVoxels,
            "Movable object update rebuilt static or complete Global SDF voxels");
        Check(statistics.probeSdfUploadedTexels>0&&statistics.probeSdfUploadedTexels<totalGlobalTexels&&statistics.probeSdfUploadRegions>0,
            "Movable Global SDF did not upload a bounded partial region");
        const auto& update=moved->Statistics();
        std::cout<<"Probe SDF update offset="<<offset<<": bricks="<<update.updatedBricks<<'/'<<update.totalBricks
            <<", voxels="<<update.updatedVoxels<<'/'<<update.totalVoxels
            <<", GPU uploaded texels="<<statistics.probeSdfUploadedTexels<<'/'<<totalGlobalTexels
            <<", upload regions="<<statistics.probeSdfUploadRegions<<", Global update ms="<<update.updateMilliseconds<<'\n';
        checkUnion("moved dirty bricks versus full rebuild",true);
        inspect(lab.ReadbackScreenProbeTrace(),lab.ReadbackScreenProbePlacement(),"moved object",offset==.8f);
        if(offset==.8f){sdf.sliceView=1;render();save("moved_global_slice");sdf.sliceView=0;}
        render();Check(lab.PipelineStatistics().probeSdfUploadedTexels==0,"Stationary frame repeated the previous dirty upload");
    }
    globalEqual(lab.PreparedProbeSdfScene()->GlobalPixels(),originalGlobal,"return-to-origin stale voxel check");

    // A disabled tracer may miss the one dirty update, then see a stationary
    // frame whose dirty list is empty. Re-enabling must restore the full union.
    sdf.enabled=false;render();frame.screenProbeStudyState.occluderOffset=.35f;render();render();
    sdf.enabled=true;render();checkUnion("re-enabled after missed dirty list",true);
    Check(lab.PreparedProbeSdfScene()->MeshGeneration()==meshGeneration&&
        lab.PreparedProbeSdfScene()->Statistics().bakedVoxels==bakedVoxels,
        "Re-enabling a moved SDF scene rebaked immutable distance assets");
    Check(lab.PipelineStatistics().probeSdfUploadedTexels==totalGlobalTexels,
        "Re-enabling after an unavailable dirty list did not restore the complete Global SDF");
    frame.screenProbeStudyState.occluderOffset=0;render();checkUnion("re-enable return to origin",true);
    globalEqual(lab.PreparedProbeSdfScene()->GlobalPixels(),originalGlobal,"re-enable restoration has stale voxels");

    render();placement.freeze=true;render();const auto frozen=lab.ReadbackScreenProbePlacement();
    frame.camera=ProbePlacementCameraPreset(1,frame.OutputAspect());frame.effects.cameraCut=true;render();
    const auto frozenProbes=lab.ReadbackScreenProbePlacement();
    Check(PlacementSamePositions(frozen.uniform,frozenProbes.uniform)&&PlacementSamePositions(frozen.spawned,frozenProbes.spawned),
        "Camera movement relocated frozen SDF trace origins");
    const auto frozenRaw=lab.ReadbackScreenProbeTrace(false,true),frozenCombined=lab.ReadbackScreenProbeTrace();
    inspect(frozenCombined,frozenProbes,"frozen origins/new source camera",true);
    unsigned unavailable=0,retried=0;
    for(std::size_t ray=0;ray<frozenRaw.radianceStatus.size();++ray)if(frozenRaw.radianceStatus[ray].w==6) {
        ++unavailable;if(frozenCombined.radianceStatus[ray].w>=9&&frozenCombined.radianceStatus[ray].w<=15)++retried;
    }
    Check(unavailable>0&&retried>0,"SDF fallback did not retry frozen origins unavailable to the current screen capture");
    tracing.displayMode=1;render();save("frozen_origin_status");
    std::cout<<"Probe SDF tracing passed: independent first triangle hits, untouched screen hits, true HDR/black readability, "
        "Mesh/Global isolation, signed union uploads, repeated dirty/full agreement, immutable asset reuse and frozen world-origin retries.\n";
}
