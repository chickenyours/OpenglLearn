// Included after the placement and SDF reference helpers. Field-view alpha is
// texture-marched world ray distance, so geometric checks do not rely on hue.
void ProbeSdfQualityCheck(MaterialLabModule& lab,const Options& options) {
    auto frame=MakeProbeSdfTraceFrame(options);frame.present=false;frame.screenProbeStudyHud=false;
    frame.width=320;frame.height=240;frame.effects.outputSize={0,0};
    frame.camera=ProbePlacementCameraPreset(0,frame.OutputAspect());
    auto& placement=frame.effects.screenProbePlacement;auto& tracing=placement.trace;auto& sdf=tracing.sdf;
    placement.baseSpacing=48;placement.frameIndex=0;placement.freeze=false;
    tracing.rayResolution=4;sdf.enabled=true;sdf.automaticQuality=false;sdf.meshQuality=1;
    sdf.visualMode=1;sdf.visualizeSelected=false;sdf.sliceView=0;sdf.showVoxelGrid=true;
    const auto render=[&] {Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;placement.reset=false;};
    const auto save=[&](const char* label) {
        if(options.screenshot.empty())return;
        auto path=std::filesystem::path(options.screenshot);
        path.replace_filename(path.stem().string()+"_"+label+path.extension().string());Save(lab.Readback(),path.string());
    };
    std::uint32_t triangleCount=0;unsigned pendingFrames=0;
    const auto volume=[&] {
        auto pixels=lab.ReadbackProbeSdfVolume();
        Check(pixels.size()==std::size_t(frame.width)*frame.height,"SDF 3D view has an incomplete raw distance target");
        unsigned hits=0;
        for(const auto& pixel:pixels) {
            for(unsigned channel=0;channel<4;++channel)Check(std::isfinite(pixel[channel]),"SDF 3D view contains a nonfinite sample");
            Check(pixel.w>=0&&pixel.w<100,"SDF 3D view alpha is not a valid texture-marched world distance");
            if(pixel.w>0)++hits;
        }
        Check(hits>64,"SDF 3D visualization lost all sampled field surfaces");return pixels;
    };
    const auto waitReady=[&](int tier,const char* label) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
        bool checkedPendingVolume=false;
        for(;;) {
            render();const auto status=lab.ProbeSdfQualityStatistics();const auto scene=lab.PreparedProbeSdfScene();
            Check(bool(scene)&&scene->Instances().size()==12&&!scene->MeshPixels().empty()&&!scene->GlobalPixels().empty(),
                std::string(label)+": a pending quality request published blank distance fields");
            Check(status.activeInstances[0]+status.activeInstances[1]+status.activeInstances[2]==12&&status.cache.failedBakes==0,
                std::string(label)+": active quality instances or background bakes are incomplete");
            if(!triangleCount)triangleCount=scene->PackedGeometry()->TriangleCount();
            Check(scene->PackedGeometry()->TriangleCount()==triangleCount,std::string(label)+": quality changed the raster triangle geometry");
            for(const auto& instance:scene->Instances())Check(bool(instance.mesh)&&!instance.mesh->Pixels().empty(),
                std::string(label)+": an unready Mesh SDF replaced the previous resident tier");
            if(status.pending) {
                ++pendingFrames;if(!checkedPendingVolume){volume();checkedPendingVolume=true;}
            }
            if(!status.pending&&(tier<0||status.activeInstances[std::size_t(tier)]==12)) {
                std::cout<<"Probe SDF quality "<<label<<": active L/M/H="<<status.activeInstances[0]<<'/'<<status.activeInstances[1]<<'/'<<status.activeInstances[2]
                    <<", scene builds="<<status.sceneBuilds<<", cached scene switches="<<status.cachedSceneSwitches
                    <<", bakes L/M/H="<<status.cache.completedBakes[0]<<'/'<<status.cache.completedBakes[1]<<'/'<<status.cache.completedBakes[2]
                    <<", GPU mesh upload="<<lab.PipelineStatistics().probeSdfMeshUploadedTexels
                    <<", GPU mesh cache hit="<<lab.PipelineStatistics().probeSdfMeshCacheHit<<'\n';
                return status;
            }
            Check(std::chrono::steady_clock::now()<deadline,std::string(label)+": requested quality did not become ready within 60 seconds");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };
    const auto worldRay=[&](unsigned x,unsigned y,bool split) {
        glm::vec2 uv=(glm::vec2(x,y)+.5f)/glm::vec2(frame.width,frame.height);
        if(split)uv.x=glm::fract(uv.x*2.f);
        auto ndc=uv*2.f-1.f;if(split)ndc.x*=.5f;
        const auto view=glm::inverse(frame.camera->projection)*glm::vec4(ndc,1,1);
        return glm::normalize(glm::vec3(glm::inverse(frame.camera->view)*glm::vec4(glm::vec3(view)/view.w,0)));
    };
    const auto checkCurrentPose=[&](const char* label) {
        const auto scene=lab.PreparedProbeSdfScene();Check(bool(scene),std::string(label)+": missing active scene");
        const auto expected=ProbePlacementTransform(ProbeSdfObjects()[2],frame.screenProbeStudyState);
        for(unsigned column=0;column<4;++column)for(unsigned row=0;row<4;++row)
            Check(std::abs(scene->Instances()[2].localToWorld[column][row]-expected[column][row])<.000001f,
                std::string(label)+": quality scene retained an outdated movable-object transform");
        const auto rebuilt=Render::BuildProbeSdfScene(scene->Instances(),scene->Settings());
        const auto gpu=lab.ReadbackProbeGlobalSdf();
        Check(gpu.size()==scene->GlobalPixels().size()&&gpu.size()==rebuilt->GlobalPixels().size(),
            std::string(label)+": active Global SDF is incomplete");
        for(std::size_t pixel=0;pixel<gpu.size();++pixel)for(unsigned channel=0;channel<4;++channel)
            Check(std::abs(gpu[pixel][channel]-scene->GlobalPixels()[pixel][channel])<.00002f&&
                std::abs(scene->GlobalPixels()[pixel][channel]-rebuilt->GlobalPixels()[pixel][channel])<.00002f,
                std::string(label)+": quality-switch Global SDF differs from a full union of the current pose");
    };
    waitReady(1,"initial medium");const auto initial=volume();
    const auto triangles=ProbeSdfTriangles(frame.screenProbeStudyState);
    unsigned floorSamples=0;
    for(unsigned y=8;y<frame.height/2;y+=12)for(unsigned x=8;x<frame.width;x+=12) {
        const auto direction=worldRay(x,y,false);float nearest=frame.camera->farPlane;
        for(const auto& triangle:triangles)nearest=PlacementTriangleDistance(frame.camera->position,direction,triangle,nearest);
        const auto point=frame.camera->position+direction*nearest;
        if(nearest>=frame.camera->farPlane||std::abs(point.y)>.003f||std::abs(direction.y)<.15f)continue;
        const float observed=initial[std::size_t(y)*frame.width+x].w;
        Check(observed>0&&std::abs(observed-nearest)<.11f,
            "Texture-only Mesh SDF floor mismatch at ("+std::to_string(x)+","+std::to_string(y)+
            "): observed="+std::to_string(observed)+", expected="+std::to_string(nearest));++floorSamples;
    }
    Check(floorSamples>=16,"Independent field-view fixture has too few visible floor samples");

    // Isolate the same raster sphere for every quality; its shape, camera and
    // triangle list remain fixed. Differences are measured in depth, not color.
    const auto sphereCenter=ProbeSdfObjects()[4].position;const float sphereRadius=ProbeSdfObjects()[4].scale.x;
    std::vector<Render::ProbeTriangle> sphereTriangles;
    for(const auto& triangle:triangles)if(std::abs(glm::length(triangle.a-sphereCenter)-sphereRadius)<.001f&&
        std::abs(glm::length(triangle.b-sphereCenter)-sphereRadius)<.001f&&std::abs(glm::length(triangle.c-sphereCenter)-sphereRadius)<.001f)
        sphereTriangles.push_back(triangle);
    Check(sphereTriangles.size()>1000,"Independent sphere reference did not retain the original raster mesh");
    auto camera=*frame.camera;camera.position={1.85f,1.1f,3};
    camera.view=glm::lookAt(camera.position,sphereCenter,glm::vec3(0,1,0));frame.camera=camera;
    sdf.visualizeSelected=true;sdf.selectedObject=4;
    const auto sphereError=[&](const std::vector<glm::vec4>& pixels,const char* label) {
        const auto mesh=lab.PreparedProbeSdfScene()->Instances()[4].mesh;
        const float voxel=std::max({mesh->Spacing().x,mesh->Spacing().y,mesh->Spacing().z})*sphereRadius;
        double error=0;unsigned checked=0;
        for(unsigned y=4;y<frame.height;y+=8)for(unsigned x=4;x<frame.width;x+=8) {
            const auto direction=worldRay(x,y,false);float nearest=frame.camera->farPlane;
            for(const auto& triangle:sphereTriangles)nearest=PlacementTriangleDistance(camera.position,direction,triangle,nearest);
            if(nearest>=frame.camera->farPlane)continue;
            const auto point=camera.position+direction*nearest;
            // Exclude grazing silhouette pixels: an iso band's small normal
            // displacement can have arbitrarily large ray-distance error there.
            if(glm::dot(glm::normalize(point-sphereCenter),-direction)<.35f)continue;
            const float observed=pixels[std::size_t(y)*frame.width+x].w;
            Check(observed>0&&std::abs(observed-nearest)<voxel*1.2f+.035f,
                std::string(label)+": sampled SDF sphere mismatch at ("+std::to_string(x)+","+std::to_string(y)+
                "): observed="+std::to_string(observed)+", expected="+std::to_string(nearest)+", voxel="+std::to_string(voxel));
            error+=std::abs(observed-nearest);++checked;
        }
        Check(checked>=64,std::string(label)+": field-view sphere did not provide enough independently checked hits");
        const double mean=error/checked;
        std::cout<<"Probe SDF sphere "<<label<<": resolution="<<mesh->Resolution()<<", geometry samples="<<checked
            <<", mean ray-distance error="<<mean<<", world voxel="<<voxel<<'\n';return mean;
    };
    render();const auto mediumPixels=volume();sphereError(mediumPixels,"medium");save("FieldMid");
    sdf.meshQuality=0;waitReady(0,"first low");const auto lowPixels=volume();const double lowError=sphereError(lowPixels,"low");save("FieldLow");
    sdf.meshQuality=2;render(); // Begin the new union using the current pose.
    frame.screenProbeStudyState.occluderOffset=.35f;waitReady(2,"first high/moved while pending");
    checkCurrentPose("pending high adopts latest pose");
    frame.screenProbeStudyState.occluderOffset=0;render();
    const auto highPixels=volume();const double highError=sphereError(highPixels,"high");save("FieldHigh");
    Check(highError<lowError,"Higher-resolution distance samples did not improve the independently checked sphere surface");
    unsigned changedDepth=0,commonHits=0;
    for(std::size_t pixel=0;pixel<lowPixels.size();++pixel)if(lowPixels[pixel].w>0&&highPixels[pixel].w>0) {
        ++commonHits;if(std::abs(lowPixels[pixel].w-highPixels[pixel].w)>.0005f)++changedDepth;
    }
    Check(commonHits>100&&changedDepth>32,"Quality controls changed only a color label, not the sampled field surface");

    frame.screenProbeStudyState.occluderOffset=-.25f;
    sdf.meshQuality=1;const auto returned=waitReady(1,"cached medium/current pose");
    Check(lab.PipelineStatistics().probeSdfMeshCacheHit&&lab.PipelineStatistics().probeSdfMeshUploadedTexels==0,
        "Returning to a resident medium field recreated its GPU distance texture");
    checkCurrentPose("cached medium adopts latest pose");
    frame.screenProbeStudyState.occluderOffset=0;render();
    const auto cachedBuilds=returned.sceneBuilds;const auto cachedBakes=returned.cache.completedBakes;
    for(const unsigned tier:{0u,2u,1u}) {
        sdf.meshQuality=tier;const auto cached=waitReady(int(tier),"cached tier repeat");
        Check(cached.sceneBuilds==cachedBuilds&&cached.cache.completedBakes==cachedBakes,
            "Repeated resident quality selection rebaked local assets or rebuilt the global union");
        Check(lab.PipelineStatistics().probeSdfMeshCacheHit&&lab.PipelineStatistics().probeSdfMeshUploadedTexels==0,
            "Repeated resident quality selection uploaded Mesh SDF pixels again");
    }

    // The narrow, nonuniform post is deliberately close to the Global grid's
    // scale. A positive coarse-field minimum must not be turned into an exact
    // mesh hit merely because its raw min-axis distance is below a display band.
    const auto globalScene=lab.PreparedProbeSdfScene();const auto globalGrid=globalScene->Settings();
    const auto sampleGlobal=[&](glm::vec3 world) {
        const auto grid=glm::clamp((world-globalGrid.origin)/globalGrid.spacing,glm::vec3(0),glm::vec3(globalGrid.counts-glm::uvec3(1)));
        const auto cell=glm::uvec3(glm::floor(grid));const auto fraction=glm::fract(grid);float value=0;
        for(unsigned z=0;z<2;++z)for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x) {
            const auto weight=glm::mix(glm::vec3(1)-fraction,fraction,glm::vec3(x,y,z));
            value+=globalScene->SampleGlobal(glm::min(cell+glm::uvec3(x,y,z),globalGrid.counts-glm::uvec3(1))).x*weight.x*weight.y*weight.z;
        }
        return value;
    };
    const auto globalSlope=[&](glm::vec3 world) {
        const float center=sampleGlobal(world);glm::vec3 gradient;
        for(unsigned axis=0;axis<3;++axis) {
            glm::vec3 delta(0);delta[axis]=globalGrid.spacing[axis]*.5f;
            const float forward=(sampleGlobal(world+delta)-center)/delta[axis],backward=(center-sampleGlobal(world-delta))/delta[axis];
            gradient[axis]=std::abs(forward)>std::abs(backward)?forward:backward;
        }
        return glm::length(gradient);
    };
    auto postCamera=*frame.camera;postCamera.position={-.1f,1.15f,2.1f};
    postCamera.view=glm::lookAt(postCamera.position,ProbeSdfObjects()[3].position,glm::vec3(0,1,0));
    postCamera.projection=glm::perspective(glm::radians(25.f),frame.OutputAspect(),postCamera.nearPlane,postCamera.farPlane);
    frame.camera=postCamera;sdf.visualMode=2;render();const auto globalPost=volume();save("Global_Thin_Post");
    const auto postInverse=glm::inverse(ProbePlacementTransform(ProbeSdfObjects()[3],frame.screenProbeStudyState));
    unsigned validGlobalZeros=0,representedPostRays=0,missingPostRays=0;
    for(unsigned y=48;y<frame.height-48;y+=12)for(unsigned x=frame.width/2-48;x<frame.width/2+48;x+=4) {
        const auto direction=worldRay(x,y,false);const float observed=globalPost[std::size_t(y)*frame.width+x].w;
        if(observed>0) {
            const auto point=postCamera.position+direction*observed;const float residual=std::abs(sampleGlobal(point));
            const float normalizedResidual=residual/std::max(globalSlope(point),.000001f);
            Check(normalizedResidual<.00035f,
                "Global field zero mismatch at ("+std::to_string(x)+","+std::to_string(y)+
                "): distance="+std::to_string(observed)+", normalized residual="+std::to_string(normalizedResidual));++validGlobalZeros;
        }
        float nearest=postCamera.farPlane;
        for(const auto& triangle:triangles)nearest=PlacementTriangleDistance(postCamera.position,direction,triangle,nearest);
        const auto local=glm::vec3(postInverse*glm::vec4(postCamera.position+direction*nearest,1));
        if(glm::any(glm::greaterThan(glm::abs(local),glm::vec3(1.002f))))continue;
        float minimum=1e20f;
        for(float at=std::max(0.f,nearest-.4f);at<=nearest+.6f;at+=.004f)
            minimum=std::min(minimum,sampleGlobal(postCamera.position+direction*at));
        if(minimum>.00025f) {
            Check(observed==0||observed>nearest+.4f,
                "Positive Global thin-post minimum was replaced by mesh at ("+std::to_string(x)+","+std::to_string(y)+
                "): observed="+std::to_string(observed)+", mesh="+std::to_string(nearest)+", minimum="+std::to_string(minimum));++missingPostRays;
        } else if(observed>0&&observed>nearest-.4f&&observed<nearest+.6f)++representedPostRays;
    }
    Check(validGlobalZeros>32&&representedPostRays>0&&missingPostRays>0,
        "Global thin-feature fixture did not exercise sampled surfaces and genuinely unresolved mesh detail");
    std::cout<<"Probe SDF Global thin/nonuniform view: verified field zeros="<<validGlobalZeros
        <<", represented post rays="<<representedPostRays<<", unresolved positive-field post rays="<<missingPostRays<<'\n';

    frame.width=640;frame.height=400;frame.camera=ProbePlacementCameraPreset(0,frame.OutputAspect());
    sdf.visualizeSelected=false;sdf.visualMode=3;render();const auto split=volume();save("Split_Global");
    unsigned leftHits=0,rightHits=0,paired=0;double separation=0;
    for(unsigned y=0;y<frame.height;y+=3)for(unsigned x=0;x<frame.width/2;x+=3) {
        const float mesh=split[std::size_t(y)*frame.width+x].w,global=split[std::size_t(y)*frame.width+x+frame.width/2].w;
        if(mesh>0)++leftHits;if(global>0)++rightHits;
        if(mesh>0&&global>0){separation+=std::abs(mesh-global);++paired;}
    }
    Check(leftHits>64&&rightHits>64&&paired>64&&separation/paired<.8,
        "Mesh/Global split views do not show corresponding surfaces from the same camera rays");
    std::cout<<"Probe SDF split: Mesh hits="<<leftHits<<", Global hits="<<rightHits<<", paired="<<paired
        <<", mean field distance difference="<<separation/paired<<", low/high changed depth pixels="<<changedDepth<<'\n';

    sdf.automaticQuality=true;const auto overview=waitReady(-1,"automatic overview");
    frame.camera=ProbePlacementCameraPreset(1,frame.OutputAspect());frame.effects.cameraCut=true;
    const auto nearby=waitReady(-1,"automatic nearby");volume();save("Automatic");
    Check(nearby.activeInstances[2]>overview.activeInstances[2]&&nearby.activeInstances[2]>0&&
        nearby.activeInstances[0]+nearby.activeInstances[1]>0,"Distance-based quality did not increase nearby detail while retaining mixed tiers");
    const auto stableBuilds=nearby.sceneBuilds;const auto stableTiers=nearby.activeInstances;
    const auto stableCamera=*frame.camera;
    for(const float jitter:{.04f,-.04f,.02f,0.f}) {
        auto jittered=stableCamera;jittered.position.x+=jitter;
        jittered.view=glm::lookAt(jittered.position,glm::vec3(0,1,-.4f),glm::vec3(0,1,0));frame.camera=jittered;render();
        const auto stable=lab.ProbeSdfQualityStatistics();
        Check(!stable.pending&&stable.activeInstances==stableTiers&&stable.sceneBuilds==stableBuilds,
            "Small camera motion repeatedly changed a hysteretic distance-field quality selection");
    }
    sdf.automaticQuality=false;sdf.meshQuality=1;frame.camera=ProbePlacementCameraPreset(0,frame.OutputAspect());waitReady(1,"restore medium");
    std::cout<<"Probe SDF quality passed: texture-only 3D floor/sphere geometry, actual low/high surface differences, "
        "pending resident fields, cached CPU/GPU switches, aligned Mesh/Global views and stable distance-driven tiers; pending frames="<<pendingFrames<<".\n";
}
