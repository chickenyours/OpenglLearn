// Included after MakeSdfBanquetFrame and the independent triangle-ray helper.
// The raw field-view alpha is world ray distance, independent of raster color.
void SdfBanquetCheck(MaterialLabModule& lab,const Options& options) {
#ifdef MATERIAL_LAB_HAS_MODEL
    auto frame=MakeSdfBanquetFrame(options);frame.present=false;frame.screenProbeStudyHud=false;
    frame.sdfModelStudy=true;frame.sdfModelRasterLod=true;
    frame.width=320;frame.height=240;frame.effects.outputSize={0,0};
    auto& placement=frame.effects.screenProbePlacement;auto& trace=placement.trace;auto& sdf=trace.sdf;
    placement.baseSpacing=96;placement.frameIndex=0;placement.freeze=false;trace.rayResolution=4;
    sdf.enabled=true;sdf.automaticQuality=true;sdf.visualMode=3;sdf.sliceView=0;
    sdf.showVoxelGrid=false;sdf.visualizeSelected=false;
    frame.camera=SdfModelCameraPreset(1,frame.OutputAspect());
    const auto render=[&] {Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;placement.reset=false;};
    const auto save=[&](const char* label) {
        if(options.screenshot.empty())return;auto path=std::filesystem::path(options.screenshot);
        path.replace_filename(path.stem().string()+"_"+label+path.extension().string());Save(lab.Readback(),path.string());
    };
    const auto volume=[&] {
        auto pixels=lab.ReadbackProbeSdfVolume();
        Check(pixels.size()==std::size_t(frame.width)*frame.height,"Banquet field view has no complete raw distance target");
        unsigned hits=0;for(const auto& pixel:pixels) {
            for(unsigned channel=0;channel<4;++channel)Check(std::isfinite(pixel[channel]),"Banquet field view contains nonfinite data");
            Check(pixel.w>=0&&pixel.w<100,"Banquet raw field alpha is not world hit distance");hits+=pixel.w>0;
        }
        Check(hits>64,"Banquet texture-only view lost all field surfaces");return pixels;
    };
    std::uint64_t worldGlobalGeneration=0,worldGeometryGeneration=0,worldTriangles=0;
    unsigned pendingFrames=0;
    const auto verifyResident=[&](const char* label) {
        const auto status=lab.ProbeSdfQualityStatistics();const auto scene=lab.PreparedProbeSdfScene();
        Check(status.importedModels&&scene&&scene->Instances().size()==16,"Banquet did not publish 12 imported teapots and 4 supporting instances");
        Check(status.activeInstances[0]+status.activeInstances[1]+status.activeInstances[2]==12&&status.cache.failedBakes==0,
              std::string(label)+": imported quality residents are incomplete");
        Check(status.modelTriangles[0]==3926&&status.modelTriangles[1]==7852&&status.modelTriangles[2]==15704,
              "Banquet did not load the real reduced and original Utah Teapot meshes");
        Check(status.fullRasterTriangles==status.modelTriangles[2]*12,"Full-detail banquet triangle accounting is incomplete");
        std::uint64_t active=0;for(unsigned tier=0;tier<3;++tier)active+=std::uint64_t(status.activeInstances[tier])*status.modelTriangles[tier];
        Check(status.activeRasterTriangles==active,"Raster geometry did not follow the completed resident distance tier");
        for(unsigned instance=0;instance<12;++instance) {
            const auto& mesh=scene->Instances()[instance].mesh;
            Check(mesh&&mesh->Closed()&&!mesh->Pixels().empty()&&mesh->Geometry()->TriangleCount()==15704,
                  "An imported MDF was replaced by raster LOD triangles or a blank/open field");
            Check(mesh->Resolution()==24||mesh->Resolution()==48||mesh->Resolution()==128,"Banquet MDF does not use 24/48/128 source sampling");
            const float spacing=std::max({mesh->Spacing().x,mesh->Spacing().y,mesh->Spacing().z});
            Check(mesh->SurfaceBias()>0&&std::abs(mesh->SurfaceBias()-glm::length(mesh->Spacing())*.5f)<std::max(.00001f,spacing*.0001f),
                  "Imported thin-wall MDF has no explicit voxel-relative surface expansion");
            Check(std::abs(scene->ObjectPixels()[std::size_t(instance)*Render::ProbeSdfObjectTexels+10].w-mesh->SurfaceBias())<.000001f,
                  "GPU object metadata does not contain the selected asset's explicit local iso bias");
        }
        for(unsigned support=12;support<16;++support)Check(scene->Instances()[support].mesh->SurfaceBias()==0,
              "Imported thin-wall expansion leaked into unrelated table/floor asset defaults");
        if(!worldGlobalGeneration) {
            worldGlobalGeneration=scene->GlobalGeneration();worldGeometryGeneration=scene->GeometryGeneration();
            worldTriangles=scene->PackedGeometry()->TriangleCount();
        }
        Check(scene->GlobalGeneration()==worldGlobalGeneration&&scene->GeometryGeneration()==worldGeometryGeneration&&
              scene->PackedGeometry()->TriangleCount()==worldTriangles,
              std::string(label)+": camera/quality selection rebuilt resident Global or world triangle geometry");
        Check(std::isfinite(status.tierSwitchMilliseconds)&&status.tierSwitchMilliseconds>=0,"Invalid CPU tier-switch timing");
        return status;
    };
    const auto waitReady=[&](int tier,const char* label) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
        bool checkedPending=false;for(;;) {
            render();const auto status=verifyResident(label);
            if(status.pending) {++pendingFrames;if(!checkedPending&&sdf.visualMode){volume();checkedPending=true;}}
            if(!status.pending&&(tier<0||status.activeInstances[std::size_t(tier)]==12)) {
                const auto stats=lab.PipelineStatistics();
                std::cout<<"SDF banquet "<<label<<": L/M/H="<<status.activeInstances[0]<<'/'<<status.activeInstances[1]<<'/'<<status.activeInstances[2]
                    <<", raster="<<status.activeRasterTriangles<<'/'<<status.fullRasterTriangles
                    <<", resident world triangles="<<worldTriangles<<", Global/geometry generation="<<worldGlobalGeneration<<'/'<<worldGeometryGeneration
                    <<", bakes L/M/H="<<status.cache.completedBakes[0]<<'/'<<status.cache.completedBakes[1]<<'/'<<status.cache.completedBakes[2]
                    <<", mesh GPU upload="<<stats.probeSdfMeshUploadedTexels<<", Global GPU upload="<<stats.probeSdfUploadedTexels
                    <<", mesh GPU cache hit="<<stats.probeSdfMeshCacheHit<<", CPU tier ms="<<status.tierSwitchMilliseconds<<'\n';
                return status;
            }
            Check(std::chrono::steady_clock::now()<deadline,std::string(label)+": 128-resolution resident bake exceeded 240 seconds");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };
    const auto nearbyState=waitReady(-1,"automatic near");volume();save("Near_Split");
    Check(nearbyState.activeInstances[2]>0&&nearbyState.activeInstances[0]+nearbyState.activeInstances[1]>0,"Near banquet has no mixed high-detail/distant MDF tiers");
    const auto completedBakes=nearbyState.cache.completedBakes;
    frame.camera=SdfModelCameraPreset(2,frame.OutputAspect());frame.effects.cameraCut=true;
    const auto distantState=waitReady(-1,"automatic far");volume();save("Far_Split");
    Check(distantState.activeInstances[2]<nearbyState.activeInstances[2]&&distantState.activeRasterTriangles<nearbyState.activeRasterTriangles,
          "Walking away did not reduce actual imported raster triangles and high MDF residents");
    Check(distantState.cache.completedBakes==completedBakes&&lab.PipelineStatistics().probeSdfUploadedTexels==0,
          "Camera-distance selection rebaked source fields or reuploaded unchanged Global pixels");
    frame.camera=SdfModelCameraPreset(1,frame.OutputAspect());frame.effects.cameraCut=true;
    const auto returned=waitReady(-1,"cached near return");volume();save("Near_Return");
    Check(returned.activeInstances[2]>=nearbyState.activeInstances[2]&&returned.activeRasterTriangles>distantState.activeRasterTriangles,
          "Returning nearby did not restore imported high detail");
    Check(returned.cache.completedBakes==completedBakes&&lab.PipelineStatistics().probeSdfUploadedTexels==0,
          "Returning to prepared imported tiers rebuilt fields or uploaded Global pixels");
    const auto stableCamera=*frame.camera;const auto stableTiers=returned.activeInstances;
    const auto stableRaster=returned.activeRasterTriangles;const auto stableBuilds=returned.sceneBuilds;
    for(float jitter:{.02f,-.02f,.01f,0.f}) {
        auto camera=stableCamera;camera.position.x+=jitter;
        camera.view=glm::lookAt(camera.position,glm::vec3(-2.75f,1.75f,4),glm::vec3(0,1,0));frame.camera=camera;
        render();const auto status=verifyResident("small camera jitter");
        Check(!status.pending&&status.activeInstances==stableTiers&&status.activeRasterTriangles==stableRaster&&status.sceneBuilds==stableBuilds,
              "Small camera motion repeatedly changed imported MDF/raster tiers");
    }
    frame.camera=stableCamera;sdf.visualMode=0;render();save("Scene");
    const auto scenePixels=lab.Readback();Check(scenePixels.rgba.size()==std::size_t(frame.width)*frame.height*4,"Imported banquet raster scene is incomplete");

    // Load and flatten the original OBJ independently of the runtime field and
    // world BVH. All MDF tiers must approximate this SAME high-detail geometry.
    std::filesystem::path repository;
    for(auto candidate:{std::filesystem::current_path(),std::filesystem::absolute(std::filesystem::path(__FILE__)).parent_path()}) {
        for(unsigned parent=0;parent<8&&!candidate.empty();++parent) {
            if(std::filesystem::is_regular_file(DefaultSdfModelVariantPaths(candidate)[2])){repository=candidate;break;}
            const auto next=candidate.parent_path();if(next==candidate)break;candidate=next;
        }
        if(!repository.empty())break;
    }
    Check(!repository.empty(),"Cannot locate independent source Utah Teapot assets");
    std::string error;SdfModelLoadOptions importOptions;importOptions.requireClosed=true;
    const auto source=LoadSdfModelVariants(DefaultSdfModelVariantPaths(repository),importOptions,&error);Check(bool(source),error);
    const auto transform=lab.PreparedProbeSdfScene()->Instances()[1].localToWorld;
    const auto inverseTransform=glm::inverse(transform);const auto& original=*source->variants[2];
    std::vector<Render::ProbeTriangle> reference;reference.reserve(original.triangles.size());
    for(const auto& triangle:original.triangles)reference.push_back({glm::vec3(transform*glm::vec4(triangle.a,1)),
        glm::vec3(transform*glm::vec4(triangle.b,1)),glm::vec3(transform*glm::vec4(triangle.c,1))});
    const auto center=glm::vec3(transform*glm::vec4(glm::vec3(original.localBounds.Center()),1));
    auto camera=stableCamera;camera.position=center+glm::vec3(0,.45f,5.8f);
    camera.view=glm::lookAt(camera.position,center,glm::vec3(0,1,0));frame.camera=camera;frame.effects.cameraCut=true;
    sdf.automaticQuality=false;sdf.visualMode=1;sdf.visualizeSelected=true;sdf.selectedObject=1;
    struct ReferenceHit {std::size_t pixel;float distance;glm::vec3 direction;};std::vector<ReferenceHit> samples;
    for(unsigned y=4;y<frame.height;y+=8)for(unsigned x=4;x<frame.width;x+=8) {
        const auto uv=(glm::vec2(x,y)+.5f)/glm::vec2(frame.width,frame.height);const auto clip=glm::vec4(uv*2.f-1.f,1,1);
        const auto view=glm::inverse(camera.projection)*clip;
        const auto direction=glm::normalize(glm::vec3(glm::inverse(camera.view)*glm::vec4(glm::vec3(view)/view.w,0)));
        float nearest=camera.farPlane;glm::vec3 normal(0);
        for(const auto& triangle:reference) {
            const float candidate=PlacementTriangleDistance(camera.position,direction,triangle,nearest);
            if(candidate<nearest){nearest=candidate;normal=glm::normalize(glm::cross(triangle.b-triangle.a,triangle.c-triangle.a));}
        }
        if(nearest>=camera.farPlane||glm::dot(normal,-direction)<.45f)continue;
        const auto local=glm::vec3(inverseTransform*glm::vec4(camera.position+direction*nearest,1));
        const auto bounds=original.localBounds;const float height=float(bounds.Extent().y),width=float(bounds.Extent().x);
        // Use stable outer body hits, not the tiny hollow spout or grazing
        // silhouette. LOW can still miss thin shell samples; record that loss.
        if(local.y<float(bounds.minimum.y)+height*.15f||local.y>float(bounds.minimum.y)+height*.72f||
           std::abs(local.x-float(bounds.Center().x))>width*.25f)continue;
        samples.push_back({std::size_t(y)*frame.width+x,nearest,direction});
    }
    Check(samples.size()>=32,"Imported original-triangle fixture has insufficient nongrazing teapot body samples");
    const auto evaluate=[&](const std::vector<glm::vec4>& pixels,const char* label) {
        unsigned readable=0,aligned=0;double errorSum=0,loss=0;
        const auto field=lab.PreparedProbeSdfScene()->Instances()[1].mesh;
        const auto fieldCounts=field->Counts();const auto fieldSpacing=field->Spacing();const auto fieldOrigin=field->Origin();
        const float sampleTolerance=std::max(.001f,std::max({fieldSpacing.x,fieldSpacing.y,fieldSpacing.z})*.02f);
        const auto rawSample=[&](glm::vec3 local) {
            const auto coordinate=glm::clamp((local-fieldOrigin)/fieldSpacing,glm::vec3(0),glm::vec3(fieldCounts-glm::uvec3(1)));
            const auto base=glm::uvec3(glm::floor(coordinate));const auto fraction=coordinate-glm::vec3(base);float result=0;
            for(unsigned z=0;z<2;++z)for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x) {
                const auto cell=glm::min(base+glm::uvec3(x,y,z),fieldCounts-glm::uvec3(1));
                const auto index=(std::size_t(cell.z)*fieldCounts.y+cell.y)*fieldCounts.x+cell.x;
                const float weight=(x?fraction.x:1-fraction.x)*(y?fraction.y:1-fraction.y)*(z?fraction.z:1-fraction.z);
                result+=field->Pixels()[index].x*weight;
            }
            return result;
        };
        for(const auto& sample:samples) {
            const float distance=pixels[sample.pixel].w;
            if(distance>0){
                ++readable;const float delta=std::abs(distance-sample.distance);errorSum+=delta;aligned+=delta<.12f;loss+=std::min(delta,1.f);
                const auto local=glm::vec3(inverseTransform*glm::vec4(camera.position+sample.direction*distance,1));
                Check(std::abs(rawSample(local)-field->SurfaceBias())<sampleTolerance,
                      std::string(label)+": displayed teapot hit is not the explicit raw distance-texture iso surface");
            }
            else loss+=1;
        }
        std::cout<<"SDF banquet original-teapot "<<label<<": reference samples="<<samples.size()<<", field hits="<<readable
            <<", aligned hits="<<aligned<<", matched mean world-ray error="<<(readable?errorSum/readable:0)
            <<", fixed-set capped loss (miss=1)="<<loss/samples.size()<<", explicit local iso bias="<<field->SurfaceBias()<<'\n';
        return std::pair<double,unsigned>{loss/samples.size(),aligned};
    };
    sdf.meshQuality=0;waitReady(0,"manual low source24");const float lowBias=lab.PreparedProbeSdfScene()->Instances()[1].mesh->SurfaceBias();
    const auto low=volume();const auto lowMetrics=evaluate(low,"LOW 24");save("Mesh_Low");
    sdf.meshQuality=1;waitReady(1,"manual medium source48");const float mediumBias=lab.PreparedProbeSdfScene()->Instances()[1].mesh->SurfaceBias();
    const auto medium=volume();evaluate(medium,"MEDIUM 48");save("Mesh_Medium");
    sdf.meshQuality=2;waitReady(2,"manual high source128");const float highBias=lab.PreparedProbeSdfScene()->Instances()[1].mesh->SurfaceBias();
    const auto high=volume();const auto highMetrics=evaluate(high,"HIGH 128");save("Mesh_High");
    Check(highBias<mediumBias&&mediumBias<lowBias,"Imported thin-surface iso expansion did not shrink with higher MDF resolution");
    Check(highMetrics.second>=samples.size()*3/4,"High-resolution texture-only teapot does not align with independently loaded original triangles");
    Check(highMetrics.first<lowMetrics.first,"High-resolution original Mesh SDF did not improve fixed teapot-body reference accuracy");
    unsigned common=0,changed=0;double lowPaired=0,highPaired=0;
    for(const auto& sample:samples)if(low[sample.pixel].w>0&&high[sample.pixel].w>0) {
        ++common;lowPaired+=std::abs(low[sample.pixel].w-sample.distance);highPaired+=std::abs(high[sample.pixel].w-sample.distance);
    }
    for(std::size_t pixel=0;pixel<high.size();++pixel)if(high[pixel].w>0&&low[pixel].w>0&&std::abs(high[pixel].w-low[pixel].w)>.0005f)++changed;
    Check(common>=16&&highPaired<lowPaired&&changed>32,"Imported precision controls did not improve actual shared field hits");
    std::cout<<"SDF banquet paired original geometry: samples="<<common<<", low/high mean error="<<lowPaired/common<<'/'<<highPaired/common
        <<", changed world-distance pixels="<<changed<<'\n';
    sdf.meshQuality=1;const auto cached=waitReady(1,"cached manual medium");
    Check(cached.cache.completedBakes==completedBakes&&lab.PipelineStatistics().probeSdfMeshCacheHit&&
          lab.PipelineStatistics().probeSdfMeshUploadedTexels==0&&lab.PipelineStatistics().probeSdfUploadedTexels==0,
          "Cached imported medium switch recreated local GPU fields or resident Global pixels");

    // One movable imported instance uses local-field reuse and dirty Global
    // updates. Compare actual uploaded pixels with the CPU field, then restore.
    sdf.visualizeSelected=false;sdf.visualMode=3;frame.camera=SdfModelCameraPreset(0,frame.OutputAspect());
    frame.screenProbeStudyState.occluderOffset=.35f;render();const auto moved=lab.PreparedProbeSdfScene();
    Check(moved->GlobalGeneration()>worldGlobalGeneration&&moved->GeometryGeneration()>worldGeometryGeneration,
          "Moving an imported teapot failed to update Global/world geometry");
    const auto expected=SdfModelObjectTransform(SdfModelBanquetObjects()[1],SdfModelSceneSettings{.35f});
    for(unsigned column=0;column<4;++column)for(unsigned row=0;row<4;++row)
        Check(std::abs(moved->Instances()[1].localToWorld[column][row]-expected[column][row])<.000001f,"Wrong imported teapot transform was moved");
    const auto upload=lab.PipelineStatistics();
    Check(upload.probeSdfUpdatedVoxels>0&&upload.probeSdfUpdatedVoxels<moved->Statistics().totalVoxels&&
          upload.probeSdfUploadedTexels>0&&upload.probeSdfUploadedTexels<moved->GlobalPixels().size(),
          "Imported rigid movement did not use a bounded dirty Global update");
    const auto global=lab.ReadbackProbeGlobalSdf();Check(global.size()==moved->GlobalPixels().size(),"Moved imported Global GPU field is incomplete");
    for(std::size_t pixel=0;pixel<global.size();++pixel)for(unsigned channel=0;channel<4;++channel)
        Check(std::isfinite(global[pixel][channel])&&std::abs(global[pixel][channel]-moved->GlobalPixels()[pixel][channel])<.00002f,
              "Moved imported Global field upload disagrees with CPU distance samples");
    volume();save("Moved_Split");
    std::cout<<"SDF banquet dynamic update: voxels="<<upload.probeSdfUpdatedVoxels<<'/'<<moved->Statistics().totalVoxels
        <<", actual GPU Global texels="<<upload.probeSdfUploadedTexels<<'/'<<moved->GlobalPixels().size()<<'\n';
    frame.screenProbeStudyState.occluderOffset=0;render();volume();save("Overview_Split");
    std::cout<<"SDF banquet passed: imported closed classic OBJ tiers, near/far raster and MDF detail, cached return, "
        "stable Global/world reference generations, independent texture-only original-teapot hits, camera hysteresis and dynamic uploads; pending frames="<<pendingFrames<<".\n";
#else
    (void)lab;(void)options;Check(false,"SDF banquet requires the Model module");
#endif
}
