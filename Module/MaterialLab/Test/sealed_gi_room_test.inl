// This fixture verifies the room itself and observable light transport. It
// deliberately does not reproduce the renderer's SDF, card or BVH algorithms.
void SealedGiRoomQuality(MaterialLabModule& lab,const Options& options) {
    auto fixture=options;fixture.sealedGiRoom=true;fixture.sdfBanquet=false;
    fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);
    frame.width=options.renderSizeSet?options.benchmarkWidth:640;
    frame.height=options.renderSizeSet?options.benchmarkHeight:400;
    frame.effects.outputSize={0,0};frame.present=false;frame.screenProbeStudyHud=false;
    frame.camera=SealedGiRoomCameraPreset(0,frame.OutputAspect());
    auto& gi=frame.effects.surfaceCacheGi;auto& temporal=gi.temporal;
    auto& placement=frame.effects.screenProbePlacement;
    gi.enabled=true;gi.useScreenProbes=true;gi.view=0;gi.probeDiagnostics=false;
    gi.spatial.enabled=true;temporal.enabled=true;
    placement.enabled=true;placement.adaptive=true;placement.freeze=false;
    placement.frameIndex=0;placement.reset=true;
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());
        frame.effects.cameraCut=false;placement.reset=false;temporal.reset=false;};
    const auto save=[&](const char* label){if(options.screenshot.empty())return;
        auto path=std::filesystem::path(options.screenshot);
        path.replace_filename(path.stem().string()+"_"+label+path.extension().string());
        Save(lab.Readback(),path.string());};
    const auto pixels=std::size_t(frame.width)*frame.height;
    const auto finite=[&](const auto& image,const char* label){
        Check(image.size()==pixels,std::string("Missing or wrong-sized sealed-room ")+label+": "+lab.LastError());
        for(const auto& p:image)for(unsigned c=0;c<4;++c)
            if(!std::isfinite(p[c]))throw std::runtime_error(std::string("Nonfinite sealed-room ")+label);
    };
    const auto energy=[](const auto& image){double sum=0;
        for(const auto& p:image)sum+=std::abs(double(p.x))+std::abs(double(p.y))+std::abs(double(p.z));return sum;};

    // Reconstruct the exact visible meshes, then use an independent double
    // precision brute-force intersection oracle. No distance volume is read.
    const auto plane=MakePlane(),box=MakeBox(),sphere=MakeSphere(48,64);
    const auto& objects=SealedGiRoomObjects();
    std::vector<Render::ProbeTriangle> triangles;
    for(const auto& object:objects){
        const auto& mesh=object.mesh==ShowcaseMesh::Sphere?sphere:object.mesh==ShowcaseMesh::Plane?plane:box;
        const auto transform=ProbePlacementTransform(object,frame.screenProbeStudyState);
        for(std::size_t i=0;i+2<mesh.indices.size();i+=3){
            const auto a=glm::vec3(transform*glm::vec4(mesh.vertices[mesh.indices[i]].position,1));
            const auto b=glm::vec3(transform*glm::vec4(mesh.vertices[mesh.indices[i+1]].position,1));
            const auto c=glm::vec3(transform*glm::vec4(mesh.vertices[mesh.indices[i+2]].position,1));
            if(glm::length(glm::cross(b-a,c-a))>1e-8f)triangles.push_back({a,b,c});
        }
    }
    Check(!triangles.empty(),"Sealed room has no visible triangle geometry");
    unsigned enclosureRays=0;
    const std::array<glm::dvec3,5> interiorPoints{{
        {0,3.2,0},{-3.6,2.7,-3.1},{3.6,2.7,-3.1},
        {-3.6,2.7,3.1},{3.6,2.7,3.1}}};
    for(const auto& origin:interiorPoints)for(int z=-1;z<=1;++z)
        for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
            if(x==0&&y==0&&z==0)continue;
            const auto direction=glm::normalize(glm::dvec3(x,y,z));
            Check(SurfaceCacheReferenceOccluded(origin,direction,20,triangles),
                "Sealed-room enclosure has an opening along an interior reference ray");
            ++enclosureRays;
        }
    // Explicitly cover the recessed opening and its seam with vertical rays.
    // A ray is allowed to hit the diffuser, trim, lining or closed backing.
    for(int z=-6;z<=6;++z)for(int x=-6;x<=6;++x){
        Check(SurfaceCacheReferenceOccluded({double(x)*.18,3.9,double(z)*.18},{0,1,0},2,triangles),
            "Ceiling recess is not closed by visible geometry");++enclosureRays;
    }
    const auto& light=frame.effects.areaLights.lights[0];
    Check(frame.effects.areaLights.count==1&&light.enabled&&!light.twoSided,
        "Sealed room must have one downward single-sided area emitter");
    Check(glm::dot(glm::normalize(glm::cross(light.halfAxisU,light.halfAxisV)),glm::vec3(0,-1,0))>.999f,
        "Ceiling emitter faces away from the enclosed room");
    unsigned emitterPaths=0;
    const glm::dvec3 openReceiver(0,3.7,0);
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
        const auto endpoint=glm::dvec3(light.center)+glm::dvec3(light.halfAxisU)*(double(x)*.7)+
            glm::dvec3(light.halfAxisV)*(double(y)*.7);
        const auto delta=endpoint-openReceiver;const double distance=glm::length(delta);
        Check(!SurfaceCacheReferenceOccluded(openReceiver,delta/distance,distance-.002,triangles),
            "Ceiling emitter is hidden behind the ceiling or its own fixture");++emitterPaths;
    }
    Check(frame.effects.shadows.sunIntensity==0&&!frame.effects.sky.enabled&&
        !frame.effects.spotLight.enabled&&glm::length(glm::vec3(frame.effects.lighting.ambientAndExposure))==0,
        "Sealed room has an unintended sun, sky, spot or ambient source");
    for(const auto& color:frame.effects.lighting.lightColors)
        Check(glm::length(glm::vec3(color))==0,"Sealed room has an unintended point light");
    Check(SealedGiRoomLampIndex<objects.size(),"Sealed-room emitter index is out of range");
    unsigned porcelain=0;
    for(unsigned i=SealedGiRoomLampIndex+1;i<objects.size();++i){
        const auto material=SealedGiRoomObjectMaterial(i);
        Check(glm::min(material.albedo.r,glm::min(material.albedo.g,material.albedo.b))>=.75f&&
            material.roughness>=.1f&&material.roughness<=.3f&&glm::length(material.emission)==0,
            "Ceramic inspection geometry is not a bright non-emissive glazed material");++porcelain;
    }
    Check(porcelain>=4,"Sealed GI room needs several ceramic inspection objects");

    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
    do {render();Check(std::chrono::steady_clock::now()<deadline,"Sealed room SDF preparation timed out");}
    while(lab.ProbeSdfQualityStatistics().pending);
    for(unsigned warm=0;warm<32;++warm)render();
    const auto timing=lab.MeasureFrames(frame,12);
    Check(std::isfinite(timing.gpuMilliseconds)&&timing.gpuMilliseconds>0&&
        std::isfinite(timing.elapsedMilliseconds)&&timing.elapsedMilliseconds>0,"Invalid sealed-room timing");
    std::cout<<"SEALED GI PERF size="<<frame.width<<"x"<<frame.height
        <<" gpu_ms="<<timing.gpuMilliseconds<<" elapsed_ms="<<timing.elapsedMilliseconds
        <<" rays_per_probe="<<placement.trace.rayResolution*placement.trace.rayResolution
        <<" area_samples="<<frame.effects.areaLights.samplesPerAxis*frame.effects.areaLights.samplesPerAxis
        <<" frames=12 readback=0 present=0"<<std::endl;
    const auto requireCachedShadows=[&](const char* label){const auto s=lab.PipelineStatistics();
        Check(s.areaLightShadowCacheUsed&&!s.areaLightShadowsUpdated&&s.areaLightShadowPasses==0&&s.areaLightShadowBytes>0,
            std::string(label)+" rebuilt or lost the area-light shadow cache");};
    requireCachedShadows("Static camera");
    const auto stationaryCamera=frame.camera;
    auto movingCamera=*stationaryCamera;const glm::vec3 displacement(.02f,0,0);
    movingCamera.position+=displacement;
    movingCamera.view=movingCamera.view*glm::translate(glm::mat4(1),-displacement);
    frame.camera=movingCamera;render();requireCachedShadows("Camera translation");
    frame.camera=stationaryCamera;render();requireCachedShadows("Restored camera");
    for(unsigned warm=0;warm<10;++warm)render();
    Check(lab.PipelineStatistics().surfaceCacheHistoryUsed,
        "Static sealed-room camera did not reuse the accumulated GI history");
    const auto raw=lab.ReadbackSurfaceCacheRawGi(),accumulated=lab.ReadbackSurfaceCacheTemporalGi();
    finite(raw,"raw indirect light");finite(accumulated,"accumulated indirect light");
    const auto rawEnergy=energy(raw),accumulatedEnergy=energy(accumulated);
    Check(rawEnergy>1e-3&&accumulatedEnergy>1e-3,
        "Ceiling rectangle did not generate measurable one-bounce GI");
    const auto total=lab.Readback();Check(total.width==frame.width&&total.height==frame.height,
        "Sealed-room screenshot dimensions differ from the configured scene");save("total");

    // Classify receivers from world geometry rather than screen coordinates.
    // These are diagnostic color means, not a brittle artistic color ordering.
    const auto gbuffer=lab.ReadbackScreenProbeTrace();
    Check(gbuffer.sourceViewPositions.size()==pixels&&gbuffer.sourceWorldNormals.size()==pixels,
        "Sealed-room probe tracing did not publish its receiver GBuffer");
    std::array<glm::dvec3,7> colorSum; colorSum.fill(glm::dvec3(0));
    std::array<std::size_t,7> colorCount{};
    const auto inverseView=glm::inverse(frame.camera->view);
    for(std::size_t i=0;i<pixels;++i){
        const auto p=gbuffer.sourceViewPositions[i],n=gbuffer.sourceWorldNormals[i];
        if(p.w<.5f||n.w<.5f||accumulated[i].w<=0)continue;
        const auto world=glm::vec3(inverseView*glm::vec4(glm::vec3(p),1));int region=-1;
        if(std::abs(world.x-SealedGiRoomMinimum.x)<.015f&&n.x>.99f)region=0;
        else if(std::abs(world.x-SealedGiRoomMaximum.x)<.015f&&n.x<-.99f)region=1;
        else if(std::abs(world.z-SealedGiRoomMinimum.z)<.015f&&n.z>.99f)region=2;
        else if(std::abs(world.z-SealedGiRoomMaximum.z)<.015f&&n.z<-.99f)region=3;
        else if(std::abs(world.y-SealedGiRoomMinimum.y)<.015f&&n.y>.99f)region=4;
        else if(std::abs(world.y-SealedGiRoomMaximum.y)<.015f&&n.y<-.99f)region=5;
        else if(world.y>.03f&&world.y<2.3f&&world.x>-3.2f&&world.x<3.2f&&world.z>-2.8f&&world.z<2.8f)region=6;
        if(region>=0){colorSum[region]+=glm::dvec3(accumulated[i]);++colorCount[region];}
    }
    Check(colorCount[6]>10&&glm::length(colorSum[6])>1e-5,
        "Visible ceramic geometry did not receive indirect illumination");
    const std::array<const char*,7> labels{{"coral","jade","cobalt","ochre","floor","ceiling","ceramics"}};
    for(unsigned i=0;i<labels.size();++i)if(colorCount[i]){
        const auto mean=colorSum[i]/double(colorCount[i]);
        std::cout<<"SEALED GI receiver="<<labels[i]<<" pixels="<<colorCount[i]<<" indirect_rgb="
            <<mean.x<<","<<mean.y<<","<<mean.z<<std::endl;
    }
    gi.view=2;render();save("indirect");gi.view=0;
    gi.enabled=false;placement.enabled=false;render();
    requireCachedShadows("GI disabled");
    const auto direct=lab.Readback();save("direct");
    Check(direct.rgba.size()==total.rgba.size(),"GI toggle changed the output image extent");
    double difference=0,directEnergy=0;std::size_t changedPixels=0;
    for(std::size_t i=0;i<total.rgba.size();i+=4){double perPixel=0;
        for(unsigned c=0;c<3;++c){perPixel+=std::abs(double(total.rgba[i+c])-double(direct.rgba[i+c]));
            directEnergy+=direct.rgba[i+c];}
        difference+=perPixel;if(perPixel>1)++changedPixels;
    }
    Check(changedPixels>10&&difference>10,"Enabling GI did not change the lit room image");
    Check(directEnergy>100,"Disabling indirect GI removed the directly illuminated scene");

    // Restore a genuinely populated lit history before switching off the sole
    // source; a reset check without old light would miss stale energy leaks.
    gi.enabled=true;placement.enabled=true;render();
    for(unsigned warm=0;warm<12;++warm)render();
    Check(lab.PipelineStatistics().surfaceCacheHistoryUsed,"Restored GI did not repopulate its lit history");
    frame.effects.areaLights.lights[0].enabled=false;render();
    Check(!lab.PipelineStatistics().surfaceCacheHistoryUsed,
        "Disabling the ceiling lamp reused stale illuminated GI history");
    const auto darkRaw=lab.ReadbackSurfaceCacheRawGi(),darkTemporal=lab.ReadbackSurfaceCacheTemporalGi();
    finite(darkRaw,"lamp-off raw light");finite(darkTemporal,"lamp-off accumulated light");
    const auto darkAtlas=lab.ReadbackSurfaceCacheAtlas();
    Check(!darkAtlas.empty(),"Lamp-off Surface Cache readback is missing");
    for(const auto& p:darkAtlas)for(unsigned c=0;c<4;++c)
        Check(std::isfinite(p[c]),"Lamp-off Surface Cache contains nonfinite values");
    const double darkRawEnergy=energy(darkRaw),darkTemporalEnergy=energy(darkTemporal),darkAtlasEnergy=energy(darkAtlas);
    Check(darkRawEnergy<1e-5&&darkTemporalEnergy<1e-5&&darkAtlasEnergy<1e-5,
        "Unlit sealed room contains fabricated ambient, duplicate emissive transport or stale history");
    const auto dark=lab.Readback();Check(dark.rgba.size()==total.rgba.size(),"Lamp-off image is missing or resized");
    unsigned darkMaximum=0;
    for(std::size_t i=0;i<dark.rgba.size();i+=4)for(unsigned c=0;c<3;++c)
        darkMaximum=std::max(darkMaximum,unsigned(dark.rgba[i+c]));
    Check(darkMaximum<=1,"Switching off the ceiling lamp left visible emission or ambient lighting");save("lamp_off");
    frame.effects.areaLights.lights[0].enabled=true;render();requireCachedShadows("Lamp restored");
    frame.effects.areaLights.lights[0].radiance*=1.1f;render();requireCachedShadows("Lamp brightness changed");
    frame.screenProbeStudyState.occluderOffset+=.1f;render();
    const auto editedShadows=lab.PipelineStatistics();
    Check(editedShadows.areaLightShadowsUpdated&&editedShadows.areaLightShadowPasses>0,
        "Moving the ceramic cube reused stale area-light shadows");
    render();requireCachedShadows("Static edited geometry");
    std::cout<<"SEALED GI SHADOW CACHE camera_rebuilt=0 radiance_rebuilt=0 geometry_updated_faces="
        <<editedShadows.areaLightShadowPasses<<" atlas_bytes="<<editedShadows.areaLightShadowBytes<<std::endl;
    std::cout<<"SEALED GI PASS size="<<frame.width<<"x"<<frame.height
        <<" enclosure_rays="<<enclosureRays<<" open_emitter_paths="<<emitterPaths<<" ceramic_objects="<<porcelain
        <<" raw_energy="<<rawEnergy<<" accumulated_energy="<<accumulatedEnergy
        <<" gi_changed_pixels="<<changedPixels<<" gi_rgb8_difference="<<difference<<" direct_rgb8_energy="<<directEnergy
        <<" lamp_off_raw="<<darkRawEnergy<<" lamp_off_temporal="<<darkTemporalEnergy
        <<" lamp_off_atlas="<<darkAtlasEnergy<<" lamp_off_max_rgb8="<<darkMaximum<<std::endl;
}
