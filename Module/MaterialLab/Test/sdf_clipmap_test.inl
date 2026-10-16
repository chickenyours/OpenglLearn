// Included after MakeSdfClipmapFrame. Readback compares physical ring storage;
// rendered alpha and the unbinned local-field reference check world geometry.
void SdfClipmapCheck(MaterialLabModule& lab,const Options& options) {
#ifdef MATERIAL_LAB_HAS_MODEL
    auto frame=MakeSdfClipmapFrame(options);frame.present=false;frame.screenProbeStudyHud=false;
    frame.sdfModelStudy=true;frame.sdfModelRasterLod=true;frame.sdfClipmapStudy=true;
    frame.width=320;frame.height=240;frame.effects.outputSize={0,0};
    auto& placement=frame.effects.screenProbePlacement;auto& trace=placement.trace;auto& sdf=trace.sdf;
    placement.baseSpacing=96;placement.frameIndex=0;placement.freeze=false;trace.rayResolution=4;
    sdf.enabled=true;sdf.automaticQuality=true;sdf.clipmapsEnabled=true;sdf.visualMode=2;sdf.sliceView=0;
    sdf.showVoxelGrid=false;sdf.visualizeSelected=false;sdf.clipmapLevel=-1;
    sdf.showClipmapLevels=true;sdf.showClipmapBounds=true;
    auto camera=SdfModelCameraPreset(1,frame.OutputAspect());
    camera.position={-2.6875f,3.3125f,5.6875f};
    camera.view=glm::lookAt(camera.position,glm::vec3(-2.75f,1.65f,4),glm::vec3(0,1,0));frame.camera=camera;
    const auto render=[&] {Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;placement.reset=false;};
    const auto save=[&](const std::string& label) {
        if(options.screenshot.empty())return;auto path=std::filesystem::path(options.screenshot);
        path.replace_filename(path.stem().string()+"_"+label+path.extension().string());Save(lab.Readback(),path.string());
    };
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
    unsigned pendingFrames=0;
    for(;;) {
        render();const auto quality=lab.ProbeSdfQualityStatistics();
        Check(quality.cache.failedBakes==0,"Clipmap source Mesh SDF baking failed");
        if(!quality.pending)break;++pendingFrames;
        Check(std::chrono::steady_clock::now()<deadline,"Clipmap HIGH128 source did not become resident within 240 seconds");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    auto scene=lab.PreparedProbeSdfScene();
    Check(scene&&scene->Instances().size()==16&&lab.ProbeSdfQualityStatistics().importedModels,
          "Clipmaps did not retain 12 real imported teapots, 3 tables and the floor");
    auto clipmaps=scene->Clipmaps();Check(bool(clipmaps),"Clipmap study published the legacy fixed Global volume");
    Check(clipmaps->Levels().size()==4,"Global SDF does not contain four camera clipmaps");
    const auto settings=clipmaps->Settings();const auto count=settings.counts;
    Check(count.x>=16&&count==glm::uvec3(count.x)&&count.x%settings.brickSize==0,
          "Unexpected clipmap dimensions or brick alignment");
    Check(std::abs(settings.baseSpacing-.03125f)<.0000001f&&settings.spacingScale==4,
          "Clipmap physical voxel spacing is not .03125 times four per level");
    const auto geometryGeneration=scene->GeometryGeneration();const auto geometryTriangles=scene->PackedGeometry()->TriangleCount();
    const auto completedBakes=lab.ProbeSdfQualityStatistics().cache.completedBakes;
    const auto totalTexels=scene->GlobalPixels().size();
    Check(totalTexels==clipmaps->Pixels().size()&&totalTexels>=std::size_t(count.x)*count.y*count.z*4,
          "Physical Global atlas omits a clipmap level");
    std::array<std::shared_ptr<const Render::ProbeMeshSdf>,3> sourceFields;
    for(unsigned object=0;object<12;++object)for(const auto& field:{scene->Instances()[object].mesh,scene->Instances()[object].globalMesh}) {
        if(!field)continue;
        const int tier=field->Resolution()==24?0:field->Resolution()==48?1:field->Resolution()==128?2:-1;
        if(tier>=0)sourceFields[std::size_t(tier)]=field;
    }
    for(unsigned tier=0;tier<3;++tier)Check(bool(sourceFields[tier]),"Clipmap source 24/48/128 is not resident in the imported fixture");
    const auto gpuEqual=[&](const char* label) {
        const auto current=lab.PreparedProbeSdfScene();const auto gpu=lab.ReadbackProbeGlobalSdf();
        const auto& cpu=current->GlobalPixels();Check(gpu.size()==cpu.size(),std::string(label)+": incomplete physical GPU atlas");
        float maximum=0;bool finite=true;
        for(std::size_t pixel=0;pixel<gpu.size();++pixel)for(unsigned channel=0;channel<4;++channel) {
            finite&=std::isfinite(gpu[pixel][channel]);
            maximum=std::max(maximum,std::abs(gpu[pixel][channel]-cpu[pixel][channel]));
        }
        Check(finite,std::string(label)+": nonfinite Global GPU data");
        Check(maximum<.00002f,std::string(label)+": actual uploaded toroidal texels differ from CPU storage");
        const auto rangeGpu=lab.ReadbackProbeSdfRanges();const auto& rangeCpu=current->Clipmaps()->RangePixels();
        Check(!rangeGpu.empty()&&rangeGpu.size()==rangeCpu.size(),std::string(label)+": incomplete GPU brick range hierarchy");
        float rangeError=0;bool rangesFinite=true;
        for(std::size_t i=0;i<rangeCpu.size();++i)for(unsigned channel=0;channel<4;++channel) {
            rangesFinite&=std::isfinite(rangeGpu[i][channel]);
            rangeError=std::max(rangeError,std::abs(rangeGpu[i][channel]-rangeCpu[i][channel]));
        }
        Check(rangesFinite&&rangeError<.000001f,std::string(label)+": brick min/max GPU upload differs from actual shared-corner ranges");
        std::cout<<"SDF clipmap "<<label<<": physical GPU texels="<<gpu.size()<<", maximum CPU/GPU error="<<maximum<<'\n';
    };
    const auto stableGeometry=[&](const char* label) {
        const auto current=lab.PreparedProbeSdfScene();const auto quality=lab.ProbeSdfQualityStatistics();
        Check(current->GeometryGeneration()==geometryGeneration&&current->PackedGeometry()->TriangleCount()==geometryTriangles,
              std::string(label)+": camera scrolling rebuilt the source world triangle geometry");
        Check(quality.cache.completedBakes==completedBakes&&!quality.pending,
              std::string(label)+": camera scrolling rebaked immutable source assets");
    };
    gpuEqual("resident initial");

    // The reference reads eight local MDF texels explicitly, transforms their
    // distances, then unions ALL objects. It does not call the clipmap builder,
    // brick bins, ObjectDistance or SampleWorld when checking merged nodes.
    const auto sourceSample=[](const Render::ProbeMeshSdf& field,glm::vec3 local) {
        const auto coordinate=glm::clamp((local-field.Origin())/field.Spacing(),glm::vec3(0),glm::vec3(field.Counts()-glm::uvec3(1)));
        const auto first=glm::uvec3(glm::floor(coordinate));const auto fraction=coordinate-glm::vec3(first);float result=0;
        for(unsigned z=0;z<2;++z)for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x) {
            const auto cell=glm::min(first+glm::uvec3(x,y,z),field.Counts()-glm::uvec3(1));
            const auto index=(std::size_t(cell.z)*field.Counts().y+cell.y)*field.Counts().x+cell.x;
            result+=field.Pixels()[index].x*(x?fraction.x:1-fraction.x)*(y?fraction.y:1-fraction.y)*(z?fraction.z:1-fraction.z);
        }return result;
    };
    const auto boxDistance=[](glm::vec3 p,glm::vec3 low,glm::vec3 high){return glm::length(glm::max(glm::max(low-p,p-high),glm::vec3(0)));};
    const auto verifySourceMerge=[&](const char* label) {
        const auto current=lab.PreparedProbeSdfScene();const auto volume=current->Clipmaps();
        unsigned samples=0,owned=0;float maximum=0;
        for(unsigned levelIndex=0;levelIndex<4;++levelIndex) {
            const auto& level=volume->Levels()[levelIndex];
            struct SourceObject {std::shared_ptr<const Render::ProbeMeshSdf> field;glm::mat4 inverse;glm::vec3 low,high;float scale;bool movable;};
            std::vector<SourceObject> objects;
            for(unsigned object=0;object<current->Instances().size();++object) {
                const auto& instance=current->Instances()[object];
                auto field=object<12?sourceFields[levelIndex==0?2:levelIndex==1?1:0]:
                    (instance.globalMesh?instance.globalMesh:instance.mesh);
                const glm::vec3 scales{glm::length(glm::vec3(instance.localToWorld[0])),glm::length(glm::vec3(instance.localToWorld[1])),glm::length(glm::vec3(instance.localToWorld[2]))};
                SourceObject input{field,glm::inverse(instance.localToWorld),glm::vec3(1e30f),glm::vec3(-1e30f),std::min({scales.x,scales.y,scales.z}),instance.movable};
                for(unsigned corner=0;corner<8;++corner) {
                    const auto point=glm::vec3(instance.localToWorld*glm::vec4(corner&1?field->BoundsMax().x:field->BoundsMin().x,
                        corner&2?field->BoundsMax().y:field->BoundsMin().y,corner&4?field->BoundsMax().z:field->BoundsMin().z,1));
                    input.low=glm::min(input.low,point);input.high=glm::max(input.high,point);
                }objects.push_back(std::move(input));
            }
            const std::array<unsigned,9> coordinates{{0,1,count.x/4,count.x/2-1,count.x/2,count.x/2+1,count.x*3/4,count.x-2,count.x-1}};
            for(const auto z:coordinates)for(const auto y:coordinates)for(const auto x:coordinates) {
                const glm::uvec3 logical(x,y,z);const auto world=level.origin+glm::vec3(logical)*level.spacing;
                glm::vec2 fixed(level.narrowBand,0),moving(level.narrowBand,0);
                for(unsigned object=0;object<objects.size();++object) {
                    const auto& input=objects[object];const auto& field=*input.field;
                    const auto local=glm::vec3(input.inverse*glm::vec4(world,1));
                    const auto clamped=glm::clamp(local,field.Origin(),field.Origin()+glm::vec3(field.Counts()-glm::uvec3(1))*field.Spacing());
                    const float raw=sourceSample(field,clamped);float value=(raw-field.SurfaceBias())*input.scale;
                    if(local!=clamped) {
                        const float lower=std::max(boxDistance(local,field.BoundsMin(),field.BoundsMax()),std::max(0.f,raw-field.InterpolationError()-glm::length(local-clamped)));
                        value=std::max(std::max(0.f,lower-field.SurfaceBias())*input.scale,
                            std::max(0.f,boxDistance(world,input.low,input.high)-field.SurfaceBias()*input.scale));
                    }
                    auto& result=input.movable?moving:fixed;if(value<result.x)result={value,float(object+1)};
                }
                const auto closest=fixed.x<=moving.x?fixed:moving;const glm::vec4 expected(closest.x,closest.y,fixed.x,moving.x);
                const auto actual=volume->SampleLogical(logical,levelIndex);
                for(unsigned channel=0;channel<4;++channel)maximum=std::max(maximum,std::abs(actual[channel]-expected[channel]));
                ++samples;owned+=closest.y>0;
            }
        }
        Check(samples>=2000&&owned>16,"Independent source-field union fixture is empty");
        Check(maximum<.00003f,std::string(label)+": clipmap node differs from the independent unbinned 128/48/24 source union");
        std::cout<<"SDF clipmap "<<label<<": independent local-source nodes="<<samples<<", owned="<<owned<<", max merge error="<<maximum<<'\n';
    };
    verifySourceMerge("resident source merge");
    float cameraClearance=1e30f;
    for(unsigned object=0;object<scene->Instances().size();++object)
        cameraClearance=std::min(cameraClearance,clipmaps->ObjectDistance(object,frame.camera->position,0));
    Check(cameraClearance>.1f,"Near clipmap camera is inside or touching an expanded object field");
    const auto initialCamera=*frame.camera;
    render();Check(clipmaps->Statistics().updatedVoxels==0&&lab.PipelineStatistics().probeSdfUploadedTexels==0,
                   "Stationary clipmap frame repeated CPU updates or GPU uploads");
    const auto stationaryGeneration=clipmaps->Generation();
    auto movedCamera=initialCamera;movedCamera.position+=glm::vec3(.01f);
    movedCamera.view=glm::lookAt(movedCamera.position,glm::vec3(-2.75f,1.65f,4),glm::vec3(0,1,0));frame.camera=movedCamera;render();
    Check(clipmaps->Generation()==stationaryGeneration&&clipmaps->Statistics().updatedVoxels==0&&lab.PipelineStatistics().probeSdfUploadedTexels==0,
          "Sub-brick camera jitter recentered or uploaded the Global SDF");
    stableGeometry("sub-brick motion");
    frame.camera=initialCamera;render();

    const auto scroll=[&](glm::vec3 offset,const char* label,bool onlyFinest) {
        const auto oldLevels=clipmaps->Levels();const auto oldPixels=clipmaps->Pixels();
        auto view=initialCamera;view.position+=offset;view.view=glm::lookAt(view.position,glm::vec3(-2.75f,1.65f,4),glm::vec3(0,1,0));
        frame.camera=view;render();stableGeometry(label);
        std::uint64_t expectedEntering=0;unsigned changedLevels=0,overlapSamples=0;
        for(unsigned levelIndex=0;levelIndex<4;++levelIndex) {
            const auto& old=oldLevels[levelIndex];const auto& level=clipmaps->Levels()[levelIndex];
            const auto delta=glm::ivec3(glm::round((level.origin-old.origin)/level.spacing));
            const auto difference=glm::abs(delta);std::uint64_t overlap=1;
            for(unsigned axis=0;axis<3;++axis)overlap*=std::uint64_t(std::max(0,int(count[axis])-difference[axis]));
            const auto entering=level.statistics.totalVoxels-overlap;expectedEntering+=entering;
            changedLevels+=delta!=glm::ivec3(0);
            Check(level.statistics.fullReason==Render::ProbeSdfClipmapFullReason::None&&level.statistics.enteringVoxels==entering&&
                  level.statistics.updatedVoxels==entering,std::string(label)+": scroll did not update precisely its entering brick slabs");
            if(onlyFinest&&levelIndex>0)Check(entering==0,std::string(label)+": a .13-world step unnecessarily updated a coarser clipmap");
            for(unsigned z=0;z<count.z;z+=7)for(unsigned y=0;y<count.y;y+=7)for(unsigned x=0;x<count.x;x+=7) {
                const glm::uvec3 logical(x,y,z);const auto previous=glm::ivec3(logical)+delta;
                if(glm::any(glm::lessThan(previous,glm::ivec3(0)))||glm::any(glm::greaterThanEqual(previous,glm::ivec3(count))))continue;
                const auto physical=(glm::uvec3(previous)+old.scroll)%count;
                const auto index=old.atlasOffset+(std::size_t(physical.z)*count.y+physical.y)*count.x+physical.x;
                Check(clipmaps->SampleLogical(logical,levelIndex)==oldPixels[index],std::string(label)+": toroidal scrolling changed an overlapping world sample");++overlapSamples;
            }
        }
        const auto update=clipmaps->Statistics();const auto gpu=lab.PipelineStatistics();
        Check(expectedEntering>0&&update.updatedVoxels==expectedEntering&&update.enteringVoxels==expectedEntering&&overlapSamples>1000,
              std::string(label)+": clipmap slab update accounting is incomplete");
        Check(gpu.probeSdfUploadedTexels>0&&gpu.probeSdfUploadedTexels<totalTexels&&
              gpu.probeSdfUploadedTexels<=update.updatedVoxels*4+4096&&gpu.probeSdfUploadRegions>0,
              std::string(label)+": sparse entering X/Y/Z slabs became whole-atlas or excessive row uploads");
        if(onlyFinest)Check(changedLevels==1,"Single fine-brick crossing scrolled several levels");
        gpuEqual(label);
        std::cout<<"SDF clipmap "<<label<<": entering/updated="<<expectedEntering<<'/'<<update.updatedVoxels
            <<", levels="<<changedLevels<<", preserved world samples="<<overlapSamples<<", actual GPU uploaded="<<gpu.probeSdfUploadedTexels
            <<'/'<<totalTexels<<", rectangles="<<gpu.probeSdfUploadRegions<<'\n';
    };
    scroll({.13f,0,0},"positive X fine slab",true);
    scroll({.26f,0,0},"repeat positive X",false);
    scroll({-.27f,-.26f,-.39f},"negative XYZ wrap",false);save("Scrolled_Global");
    verifySourceMerge("scrolled source merge");
    auto teleportCamera=initialCamera;teleportCamera.position.x+=3.75f;
    teleportCamera.view=glm::lookAt(teleportCamera.position,glm::vec3(-2.75f,1.65f,4),glm::vec3(0,1,0));frame.camera=teleportCamera;render();
    stableGeometry("camera teleport");
    Check(clipmaps->Levels()[0].statistics.fullReason==Render::ProbeSdfClipmapFullReason::Teleport&&
          clipmaps->Levels()[0].statistics.updatedVoxels==clipmaps->Levels()[0].statistics.totalVoxels,
          "Nonoverlapping finest-camera teleport did not rebuild that clipmap");
    for(unsigned level=1;level<4;++level)Check(clipmaps->Levels()[level].statistics.fullReason==Render::ProbeSdfClipmapFullReason::None,
          "A fine-volume teleport unnecessarily rebuilt a still-overlapping coarser level");
    Check(lab.PipelineStatistics().probeSdfUploadedTexels<totalTexels,"Fine teleport reuploaded every clipmap");gpuEqual("fine teleport");save("Outer_Levels");
    frame.camera=initialCamera;render();gpuEqual("teleport return");

    // A rigid local-field instance moves while the camera and ring origins
    // remain fixed. Both its OLD and NEW influence regions must be refreshed.
    const auto beforeMovement=clipmaps->Pixels();const auto movementLevels=clipmaps->Levels();
    frame.screenProbeStudyState.occluderOffset=.35f;render();
    const auto dynamic=clipmaps->Statistics();const auto dynamicGpu=lab.PipelineStatistics();
    Check(dynamic.updatedVoxels>0&&dynamic.updatedVoxels<dynamic.totalVoxels&&dynamic.enteringVoxels==0&&
          dynamic.staticVoxelUpdates==0&&dynamic.dynamicVoxelUpdates==dynamic.updatedVoxels,
          "Moving one imported teapot did not use cached-static, bounded old/new dynamic dirty regions");
    Check(dynamicGpu.probeSdfUploadedTexels>0&&dynamicGpu.probeSdfUploadedTexels<totalTexels,
          "Moving one teapot uploaded the entire four-level field");
    unsigned changedDynamic=0;bool staticUnchanged=true;
    for(std::size_t pixel=0;pixel<beforeMovement.size();++pixel) {
        const auto actual=clipmaps->Pixels()[pixel];
        staticUnchanged&=actual.z==beforeMovement[pixel].z;
        changedDynamic+=actual!=beforeMovement[pixel];
    }
    Check(staticUnchanged,"A movable-only clipmap edit changed the cached static field");
    Check(changedDynamic>16,"Imported movable teapot changed no real clipmap distance texels");
    for(unsigned level=0;level<4;++level)Check(clipmaps->Levels()[level].origin==movementLevels[level].origin,
          "Object movement incorrectly recentered a camera clipmap");
    gpuEqual("dynamic old/new bounds");verifySourceMerge("dynamic source merge");save("Dynamic_Global");
    std::cout<<"SDF clipmap dynamic: changed texels="<<changedDynamic<<", recomputed="<<dynamic.updatedVoxels
        <<'/'<<dynamic.totalVoxels<<", static recomputed="<<dynamic.staticVoxelUpdates<<", actual uploaded="<<dynamicGpu.probeSdfUploadedTexels<<'\n';
    frame.screenProbeStudyState.occluderOffset=0;render();gpuEqual("dynamic restore");

    // Reconstruct world positions from the visualization target's RGBA32F
    // alpha. Color/bounds overlays cannot satisfy these distance assertions.
    const auto contains=[](const Render::ProbeSdfClipmapLevel& level,glm::vec3 point) {
        const auto low=level.origin+level.spacing,high=level.origin+glm::vec3(level.counts-glm::uvec3(2))*level.spacing;
        return glm::all(glm::greaterThanEqual(point,low))&&glm::all(glm::lessThan(point,high));
    };
    const auto ray=[&](unsigned x,unsigned y) {
        const auto uv=(glm::vec2(x,y)+.5f)/glm::vec2(frame.width,frame.height);
        const auto view=glm::inverse(frame.camera->projection)*glm::vec4(uv*2.f-1.f,1,1);
        return glm::normalize(glm::vec3(glm::inverse(frame.camera->view)*glm::vec4(glm::vec3(view)/view.w,0)));
    };
    const auto renderedField=[&](int forced,const char* label) {
        sdf.visualMode=2;sdf.clipmapLevel=forced;render();const auto pixels=lab.ReadbackProbeSdfVolume();
        Check(pixels.size()==std::size_t(frame.width)*frame.height,std::string(label)+": no raw world-distance target");
        unsigned hits=0,checked=0;std::array<unsigned,4> selected{};float maximum=0;
        for(const auto& pixel:pixels) {
            for(unsigned channel=0;channel<4;++channel)Check(std::isfinite(pixel[channel]),"Nonfinite clipmap visualization output");
            Check(pixel.w>=0&&pixel.w<100,"Clipmap visualization alpha is not world-ray distance");hits+=pixel.w>0;
        }
        for(unsigned y=3;y<frame.height;y+=7)for(unsigned x=3;x<frame.width;x+=7) {
            const auto distance=pixels[std::size_t(y)*frame.width+x].w;if(distance<=0)continue;
            const auto point=frame.camera->position+ray(x,y)*distance;int levelIndex=forced;
            if(levelIndex<0)for(unsigned level=0;level<4;++level)if(contains(clipmaps->Levels()[level],point)){levelIndex=int(level);break;}
            Check(levelIndex>=0&&contains(clipmaps->Levels()[std::size_t(levelIndex)],point),std::string(label)+": rendered hit is outside its guarded clipmap");
            const float raw=clipmaps->SampleWorld(point,unsigned(levelIndex)).x;maximum=std::max(maximum,std::abs(raw));++checked;++selected[std::size_t(levelIndex)];
        }
        Check(hits>32&&checked>=8,std::string(label)+": texture-marched clipmap surface is empty");
        const std::array<unsigned,4> minimumPriorHits{{23000,70000,76000,76000}};
        if(forced>=0)Check(hits>=minimumPriorHits[std::size_t(forced)],"Accelerated brick traversal lost represented reference surfaces");
        const auto stats=lab.PipelineStatistics();
        Check(stats.screenProbePlacementPasses==0&&stats.screenProbeHzbLevels==0&&stats.screenProbeTracePasses==1,
              "Pure SDF volume still computed unused probe placement, HZB or fallback rays");
        if(forced<0)Check(std::count_if(selected.begin(),selected.end(),[](unsigned hits){return hits>0;})>=2,
                         "Automatic near fixture does not exercise actual clipmap handoffs");
        Check(maximum<.003f,std::string(label)+": displayed 3D hit is not a zero of the selected toroidal distance texture");
        std::cout<<"SDF clipmap "<<label<<": world-distance hits="<<hits<<", independently resampled="<<checked
            <<", hit levels="<<selected[0]<<'/'<<selected[1]<<'/'<<selected[2]<<'/'<<selected[3]<<", maximum raw field residual="<<maximum<<'\n';
        return pixels;
    };
    sdf.showClipmapBounds=false;sdf.showClipmapLevels=false;
    const auto automatic=renderedField(-1,"automatic actual fields");save("Auto_NoColors");
    sdf.showClipmapLevels=true;sdf.showClipmapBounds=true;
    const auto colored=renderedField(-1,"automatic colored handoff");save("Auto_Levels");
    unsigned colorChanges=0;
    for(std::size_t pixel=0;pixel<automatic.size();++pixel) {
        Check(std::abs(automatic[pixel].w-colored[pixel].w)<.00002f,"Clipmap palette/bounds changed the actual ray-marched geometry");
        colorChanges+=glm::length(glm::vec3(automatic[pixel]-colored[pixel]))>.001f;
    }
    Check(colorChanges>64,"Clipmap palette/boundary controls changed no visible overlay pixels");
    sdf.useGlobal=false;render();const auto globalDisabled=lab.ReadbackProbeSdfVolume();
    for(std::size_t pixel=0;pixel<colored.size();++pixel)
        Check(globalDisabled[pixel].w==colored[pixel].w,"Disabling Global fallback changed the Global volume visualization");
    sdf.useGlobal=true;
    sdf.showClipmapBounds=false;sdf.showClipmapLevels=false;
    std::array<std::vector<glm::vec4>,4> forcedViews;
    for(unsigned level=0;level<4;++level) {
        const auto label="Forced_Level_"+std::to_string(level);forcedViews[level]=renderedField(int(level),label.c_str());save(label);
    }
    unsigned changedDepth=0,changedCoverage=0,paired=0;double difference=0;
    for(std::size_t pixel=0;pixel<forcedViews[0].size();++pixel) {
        const float finest=forcedViews[0][pixel].w,coarser=forcedViews[1][pixel].w;
        if(finest>0&&coarser>0){++paired;difference+=std::abs(finest-coarser);changedDepth+=std::abs(finest-coarser)>.0005f;}
        changedCoverage+=(finest>0)!=(forcedViews[3][pixel].w>0);
    }
    Check(paired>32&&changedDepth>32&&difference/paired>.0005,
          "Forced clipmap levels recolored identical geometry instead of sampling different real fields");
    Check(changedCoverage>32,"Fine and outer clipmap coverage did not change actual texture-marched hits");
    std::cout<<"SDF clipmap actual level geometry: paired fine/middle="<<paired<<", changed world depths="<<changedDepth
        <<", mean depth difference="<<difference/paired<<", fine/outer changed coverage="<<changedCoverage<<'\n';

    // The existing screen miss path must actually consume the clipmap atlas.
    sdf.clipmapLevel=-1;sdf.visualMode=0;sdf.useMesh=false;sdf.maxSteps=384;
    placement.baseSpacing=32;placement.reset=true;render();
    const auto native=lab.ReadbackScreenProbeTrace(),screen=lab.ReadbackScreenProbeTrace(false,true);
    Check(lab.PipelineStatistics().screenProbePlacementPasses>0&&lab.PipelineStatistics().screenProbeHzbLevels>0,
          "Leaving pure volume mode did not restore actual probe and HZB tracing");
    unsigned globalHits=0,crossingHits=0,exactChecked=0;float maximumHitError=0;
    const auto& geometry=*scene->PackedGeometry();const auto& triangles=geometry.Pixels();
    for(std::size_t rayIndex=0;rayIndex<native.radianceStatus.size();++rayIndex) {
        const auto status=unsigned(std::round(native.radianceStatus[rayIndex].w));
        Check(status!=9&&status!=11,"Global-only clipmap fallback traced a local MDF hit");
        if(status!=10&&status!=12)continue;++globalHits;
        Check(screen.radianceStatus[rayIndex].w!=1,"Global fallback replaced an already successful screen trace");
        const auto hit=native.hitWorldDistance[rayIndex];const auto direction=glm::normalize(glm::vec3(native.rayDirectionDepthFetches[rayIndex]));
        const auto origin=glm::vec3(hit)-direction*hit.w;
        crossingHits+=!contains(clipmaps->Levels()[0],origin)&&contains(clipmaps->Levels()[0],glm::vec3(hit));
        if(exactChecked<8) {
            float nearest=trace.maxDistance+.01f;
            for(unsigned triangle=0;triangle<geometry.TriangleCount();++triangle) {
                const auto at=geometry.NodeCount()*2+triangle*5;
                nearest=PlacementTriangleDistance(origin,direction,{glm::vec3(triangles[at]),glm::vec3(triangles[at+1]),glm::vec3(triangles[at+2])},nearest);
            }
            maximumHitError=std::max(maximumHitError,std::abs(nearest-hit.w));++exactChecked;
        }
    }
    Check(globalHits>0&&exactChecked>0&&maximumHitError<.006f,"Global clipmap fallback missed or returned a non-first triangle hit");
    std::cout<<"SDF clipmap native fallback: Global hits="<<globalHits<<", coarse-origin/fine-domain hits="<<crossingHits
        <<", independent first hits="<<exactChecked<<", max distance error="<<maximumHitError<<'\n';
    sdf.useMesh=true;

    // While tracing is disabled, CPU clipmaps keep following the camera.
    // Re-enabling after a no-op with an empty dirty list must catch the GPU up.
    sdf.enabled=false;frame.screenProbeStudyState.occluderOffset=.35f;render();render();
    sdf.enabled=true;render();gpuEqual("missed dirty generation catchup");
    Check(lab.PipelineStatistics().probeSdfUploadedTexels==totalTexels,"Clipmap missed-generation catchup did not restore the full atlas");
    frame.screenProbeStudyState.occluderOffset=0;render();gpuEqual("catchup restored object");
    sdf.clipmapLevel=-1;sdf.showClipmapLevels=true;sdf.showClipmapBounds=true;sdf.visualMode=3;render();save("Final_Split");
    if(!options.screenshot.empty()) {
        frame.width=1280;frame.height=800;placement.baseSpacing=64;
        frame.camera=SdfClipmapCameraPreset(0,frame.OutputAspect());sdf.visualMode=2;frame.screenProbeStudyHud=true;
        render();save("Overview");
        frame.camera=SdfClipmapCameraPreset(1,frame.OutputAspect());render();save("Close");
    }
    std::cout<<"SDF clipmap passed: four real toroidal Global levels, HIGH/MED/LOW source merge, camera-brick scrolling, "
        "bounded physical GPU uploads, unchanged camera-motion BVH, dynamic old/new bounds, actual per-level texture hits and palette independence; pending frames="<<pendingFrames<<".\n";
#else
    (void)lab;(void)options;Check(false,"SDF clipmap study requires the Model module");
#endif
}
