// Included inside MaterialLabModule::Impl; imported render assets and immutable
// distance fields share placements but keep their own precision budgets.
void PrepareSdfModels(const FrameSettings& frame) {
    const auto started=std::chrono::steady_clock::now();
    const auto& request=frame.effects.screenProbePlacement.trace.sdf;
    if(!sdfModelVariants) {
        const auto triangles=[](const MeshData& mesh){std::vector<ProbeTriangle> result;
            for(std::size_t i=0;i+2<mesh.indices.size();i+=3) {
                const auto a=mesh.vertices[mesh.indices[i]].position,b=mesh.vertices[mesh.indices[i+1]].position,c=mesh.vertices[mesh.indices[i+2]].position;
                if(glm::length(glm::cross(b-a,c-a))>1e-7f)result.push_back({a,b,c});
            }return result;};
        std::string error;SdfModelLoadOptions options;options.requireClosed=true;
        sdfModelVariants=LoadSdfModelVariants(DefaultSdfModelVariantPaths(MATERIAL_LAB_SOURCE_DIR),options,&error);
        Require(bool(sdfModelVariants),error);
        sdfModels=std::make_unique<Model::ModelModule>(device);Require(sdfModels->Startup(),sdfModels->LastError());
        constexpr std::array<glm::vec3,3> colors{{{.85f,.28f,.08f},{.08f,.65f,.78f},{.18f,.32f,.88f}}};
        for(unsigned tier=0;tier<3;++tier) {
            sdfModelResources[tier]=sdfModels->Upload(sdfModelVariants->variants[tier]->sourceModel);
            Require(bool(sdfModelResources[tier]),sdfModels->LastError());
            for(const auto handle:sdfModelResources[tier]->MaterialHandles()) {
                Require(sdfModelResources[tier]->Materials().SetParameter(handle,"baseColor",glm::vec4(colors[tier],1),&error),error);
                Require(sdfModelResources[tier]->Materials().SetParameter(handle,"metallic",0.f,&error),error);
                Require(sdfModelResources[tier]->Materials().SetParameter(handle,"roughness",.58f,&error),error);
            }
            probeSdfQualityStatus.modelTriangles[tier]=sdfModelVariants->variants[tier]->sourceModel->TriangleCount();
        }
        const auto& source=*sdfModelVariants->variants[2];
        sdfModelResidentMedium=BakeProbeMeshSdf(source.triangles,48,source.topology.closed,.8660254f);
        Require(sdfModelResidentMedium->Closed(),"Imported teapot could not retain its verified closed signed field");
        probeSdfLod=std::make_unique<ProbeSdfLodCache>(std::array<std::uint32_t,3>{24,48,128},.8660254f);
        probeSdfAssetIds[0]=probeSdfLod->RegisterMesh(source.triangles,true,sdfModelResidentMedium);
        probeSdfLod->Request(probeSdfAssetIds[0],ProbeSdfQuality::Low);probeSdfLod->Request(probeSdfAssetIds[0],ProbeSdfQuality::High);
        sdfModelFloor=BakeProbeMeshSdf(triangles(MakePlane()),24,false);
        probeSdfAssets[1]=BakeProbeMeshSdf(triangles(MakeBox()),24,true);
        probeSdfRequestedTiers.assign(SdfModelBanquetObjects().size(),ProbeSdfQuality::Medium);
        sdfModelActiveTiers=probeSdfRequestedTiers;
    }
    // LOD colors are useful for the SDF study, but must not become a changing
    // light transport material when the camera changes MDF/raster quality.
    if(sdfModelGiMaterials!=frame.surfaceCacheStudy) {
        constexpr std::array<glm::vec3,3> debugColors{{{.85f,.28f,.08f},{.08f,.65f,.78f},{.18f,.32f,.88f}}};
        std::string error;
        for(unsigned tier=0;tier<3;++tier)for(const auto handle:sdfModelResources[tier]->MaterialHandles())
            Require(sdfModelResources[tier]->Materials().SetParameter(handle,"baseColor",
                glm::vec4(frame.surfaceCacheStudy?SurfaceCacheAlbedo(0,true):debugColors[tier],1),&error),error);
        sdfModelGiMaterials=frame.surfaceCacheStudy;
    }
    const auto camera=frame.camera.value_or(SdfModelCameraPreset(0,frame.OutputAspect()));
    std::vector<ProbeSdfInstance> instances;bool pending=false;unsigned index=0;
    const SdfModelSceneSettings state{frame.screenProbeStudyState.occluderOffset};
    for(const auto& object:SdfModelBanquetObjects()) {
        const auto transform=SdfModelObjectTransform(object,state);const auto& bounds=sdfModelVariants->variants[2]->localBounds;
        glm::vec3 low(1e30f),high(-1e30f);
        for(unsigned corner=0;corner<8;++corner) {
            const auto point=glm::vec3(transform*glm::vec4(corner&1?bounds.maximum.x:bounds.minimum.x,
                corner&2?bounds.maximum.y:bounds.minimum.y,corner&4?bounds.maximum.z:bounds.minimum.z,1));
            low=glm::min(low,point);high=glm::max(high,point);
        }
        auto desired=static_cast<ProbeSdfQuality>(request.meshQuality);
        if(request.automaticQuality)desired=SelectProbeSdfDistanceQuality(
            glm::length(glm::max(glm::max(low-camera.position,camera.position-high),glm::vec3(0))),
            probeSdfRequestedTiers[index],{4.5f,10.f,.6f});
        probeSdfRequestedTiers[index]=desired;
        const auto previous=probeSdfScene?probeSdfScene->Instances()[index].mesh:sdfModelResidentMedium;
        const auto selected=probeSdfLod->Resolve(probeSdfAssetIds[0],desired,previous);
        Require(!selected.failed,probeSdfLod->Error(probeSdfAssetIds[0],desired));pending|=selected.pending;
        sdfModelActiveTiers[index]=selected.activeQuality;
        instances.push_back({selected.mesh,transform,object.movable,index,sdfModelResidentMedium,sdfModelResidentMedium});++index;
    }
    // Supporting geometry is also represented in the distance field.
    for(float depth:{4.f,-1.f,-7.f})instances.push_back({probeSdfAssets[1],
        glm::translate(glm::mat4(1),glm::vec3(0,.4f,depth))*glm::scale(glm::mat4(1),glm::vec3(10.8f,.45f,1.8f)),false,index++});
    instances.push_back({sdfModelFloor,glm::scale(glm::mat4(1),glm::vec3(11.7f,1,11.7f)),false,index++});
    if(!probeSdfScene){
        auto global=SdfModelGlobalSettings();
        if(frame.sdfClipmapStudy&&request.clipmapsEnabled){
            global.clipmapLevels=4;global.clipmapSpacingScale=4;global.clipmapCamera=camera.position;
            // A power-of-two side divides the physical atlas width, keeping
            // newly entering X slabs cheap to upload as narrow rectangles.
            global.counts=glm::uvec3(64);global.spacing=glm::vec3(.03125f);global.maxDistance=.25f;
        }
        probeSdfScene=BuildProbeSdfScene(instances,global);++probeSdfQualityStatus.sceneBuilds;
    }
    else probeSdfScene->UpdateInstances(instances);
    if(probeSdfScene->Clipmaps()) {
        auto fine=probeSdfLod->Ready(probeSdfAssetIds[0],ProbeSdfQuality::High);
        auto coarse=probeSdfLod->Ready(probeSdfAssetIds[0],ProbeSdfQuality::Low);
        pending|=!fine||!coarse;
        Require(probeSdfLod->Error(probeSdfAssetIds[0],ProbeSdfQuality::High).empty(),"High MDF clipmap source bake failed");
        std::vector<ProbeSdfClipmapMeshes> fields(instances.size());
        for(std::size_t i=0;i<instances.size();++i)
            fields[i]=i<SdfModelBanquetObjects().size()
                ?ProbeSdfClipmapMeshes{fine?fine:sdfModelResidentMedium,sdfModelResidentMedium,coarse?coarse:sdfModelResidentMedium,coarse?coarse:sdfModelResidentMedium}
                :ProbeSdfClipmapMeshes{instances[i].mesh,instances[i].mesh,instances[i].mesh,instances[i].mesh};
        probeSdfScene->UpdateGlobalClipmaps(camera.position,fields);
    }
    probeSdfQualityStatus.importedModels=true;probeSdfQualityStatus.pending=pending;
    probeSdfQualityStatus.activeInstances={};probeSdfQualityStatus.activeRasterTriangles=0;
    for(const auto tier:sdfModelActiveTiers){++probeSdfQualityStatus.activeInstances[std::size_t(tier)];
        probeSdfQualityStatus.activeRasterTriangles+=probeSdfQualityStatus.modelTriangles[frame.sdfModelRasterLod?std::size_t(tier):2];}
    probeSdfQualityStatus.fullRasterTriangles=probeSdfQualityStatus.modelTriangles[2]*SdfModelBanquetObjects().size();
    probeSdfQualityStatus.cache=probeSdfLod->Statistics();
    probeSdfQualityStatus.tierSwitchMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
}
