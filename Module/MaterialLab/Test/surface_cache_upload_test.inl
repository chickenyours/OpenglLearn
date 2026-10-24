// GPU integration check for cache transport only. All production GI stages
// stay enabled; deterministic comparisons reset their histories and ray seed.
void SurfaceCacheUploadQuality(MaterialLabModule& lab,const Options& options) {
    auto fixture=options;fixture.sealedGiRoom=true;fixture.sdfBanquet=false;
    fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);
    frame.width=options.renderSizeSet?options.benchmarkWidth:256;
    frame.height=options.renderSizeSet?options.benchmarkHeight:192;
    frame.effects.outputSize={0,0};frame.present=false;frame.screenProbeStudyHud=false;
    frame.camera=SealedGiRoomCameraPreset(0,frame.OutputAspect());
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::None;
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;auto& trace=placement.trace;
    gi.enabled=true;gi.useScreenProbes=true;gi.view=0;gi.reconstructionDebug=0;
    gi.reuseAnyHitBounds=false; // legacy baseline; each explicit pair toggles the real production uniform
    gi.safeProbeOrigins=true;gi.rasterSourcePositions=true;gi.directionalReconstruction=true;
    gi.probeDiagnostics=false;gi.probeOriginDiagnostics=false;gi.collectProbeCoverage=true;
    gi.temporal={};gi.spatial={};gi.contact={};
    placement.enabled=true;placement.adaptive=true;placement.cornerRefinement=true;
    placement.curvatureRefinement=true;placement.curvatureAwareWeights=true;
    placement.freeze=false;placement.frameIndex=0;placement.candidateCount=8;
    trace.enabled=true;trace.cosineHemisphere=true;trace.rayResolution=8;trace.samplingSeed=0;
    trace.maxSteps=512;trace.sdf.maxSteps=256;
    const auto reset=[&]{frame.effects.cameraCut=true;placement.reset=true;gi.temporal.reset=true;trace.samplingSeed=0;};
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;
        placement.reset=false;gi.temporal.reset=false;};
    const auto finite=[](const auto& image,const char* label){Check(!image.empty(),std::string("Missing upload fixture ")+label);
        for(const auto& value:image)for(unsigned c=0;c<4;++c)Check(std::isfinite(value[c]),std::string("Nonfinite upload fixture ")+label);};
    const auto difference=[](const auto& a,const auto& b){Check(a.size()==b.size(),"Upload comparison changed image extent");
        double maximum=0;for(std::size_t i=0;i<a.size();++i)for(unsigned c=0;c<4;++c)
            maximum=std::max(maximum,std::abs(double(a[i][c])-double(b[i][c])));return maximum;};

    // Bootstrap immutable mesh fields using the ordinary scene preparation.
    // The independently owned copies below let this fixture actually skip CPU
    // generations without MaterialLab's internal Update clearing DirtyCards.
    reset();render();const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);
    while(lab.ProbeSdfQualityStatistics().pending){render();Check(std::chrono::steady_clock::now()<deadline,"Upload fixture SDF preparation timed out");}
    const auto prepared=lab.PreparedProbeSdfScene();Check(bool(prepared),"Upload fixture did not prepare its scene");
    auto instances=prepared->Instances();
    auto scene=Render::BuildProbeSdfScene(instances,prepared->Settings());
    const auto& objects=SealedGiRoomObjects();Check(instances.size()==objects.size(),"Upload fixture raster/SDF instance count mismatch");
    std::vector<Render::SurfaceCacheMaterial> materials;materials.reserve(objects.size());
    unsigned movableCube=unsigned(objects.size());
    for(unsigned object=0;object<objects.size();++object){
        materials.push_back({SealedGiRoomObjectMaterial(object).albedo,glm::vec3(0)});
        if(objects[object].movable&&objects[object].mesh==ShowcaseMesh::Box)movableCube=object;
    }
    Check(movableCube<objects.size(),"Upload fixture has no movable cube");
    auto cache=Render::BuildSurfaceCache(*scene,materials);
    gi.scene=scene;gi.cache=cache;
    struct Snapshot {std::vector<glm::vec4> atlas,raw;};
    unsigned comparisons=0,unknownChecks=0,anyHitComparisons=0;
    std::uint64_t anyHitExactBytes=0;
    const auto byteHash=[](const auto& image){
        static_assert(sizeof(glm::vec4)==4*sizeof(float));
        // FNV-1a over the actual native RGBA32F readback bytes: preserve every
        // float bit, including signed zero. Never use implementation-defined std::hash.
        std::uint64_t hash=14695981039346656037ull;
        const auto* bytes=reinterpret_cast<const unsigned char*>(image.data());
        for(std::size_t i=0;i<image.size()*sizeof(glm::vec4);++i){hash^=bytes[i];hash*=1099511628211ull;}
        std::string hexadecimal(16,'0');constexpr char digits[]="0123456789abcdef";
        for(unsigned i=0;i<16;++i)hexadecimal[15-i]=digits[(hash>>(4*i))&15u];
        return hexadecimal;
    };
    const auto snapshot=[&](const char* label,const char* stage,const Snapshot* previous=nullptr){Snapshot result{lab.ReadbackSurfaceCacheAtlas(),lab.ReadbackSurfaceCacheRawGi()};
        // Emit the complete bitwise identity before validity assertions so a
        // failing backend still identifies the exact atlas and gather output.
        std::cout<<"SURFACE CACHE UPLOAD HASH backend="<<(options.vulkan?"Vulkan":"OpenGL")
            <<" case=\""<<label<<"\" stage="<<stage<<" encoding=fnv1a64_native_rgba32f"
            <<" reuse_anyhit_bounds="<<gi.reuseAnyHitBounds<<" pbr_single_light_shadow_atlas="<<(gi.areaLightShadows&&gi.areaShadows.enabled)
            <<" atlas_texels="<<result.atlas.size()<<" atlas_hash="<<byteHash(result.atlas)
            <<" raw_texels="<<result.raw.size()<<" raw_hash="<<byteHash(result.raw)<<std::endl;
        finite(result.atlas,"lighting atlas");finite(result.raw,"raw GI");
        const auto accumulated=lab.ReadbackSurfaceCacheTemporalGi(),contact=lab.ReadbackSurfaceCacheContactGi();
        finite(accumulated,"temporal GI");finite(contact,"contact GI");
        Check(result.atlas.size()==cache->PlaneTexels()&&result.raw.size()==std::size_t(frame.width)*frame.height,
            "Upload fixture readback extent mismatch");
        unsigned unknown=0,black=0;
        for(std::size_t texel=0;texel<result.atlas.size();++texel){const auto value=result.atlas[texel];
            if(cache->Attributes()[texel].w<.5f){++unknown;
                Check(value==glm::vec4(0),"Uncovered card texel acquired lighting during upload");}
            else {Check(value.w>.5f,"Covered card texel lost validity during upload");
                black+=value.x==0&&value.y==0&&value.z==0;}}
        Check(unknown>0&&black>0,"Upload fixture lacks uncovered or valid-black card texels");++unknownChecks;
        const auto stats=lab.PipelineStatistics();
        for(std::size_t pixel=0;pixel<result.raw.size();++pixel){const auto value=result.raw[pixel];
            const bool invalidCoverage=!(value.w>=0&&value.w<=1);
            const bool fabricated=value.w==0&&!(glm::length(glm::vec3(value))<1e-6f);
            if(invalidCoverage||fabricated){
                std::ostringstream detail;detail<<std::setprecision(std::numeric_limits<float>::max_digits10);
                detail<<"SURFACE CACHE UPLOAD INVALID RAW backend="<<(options.vulkan?"Vulkan":"OpenGL")
                    <<" case=\""<<label<<"\" stage="<<stage<<" reuse_anyhit_bounds="<<gi.reuseAnyHitBounds
                    <<" pbr_single_light_shadow_atlas="<<(gi.areaLightShadows&&gi.areaShadows.enabled)<<" pixel="<<pixel
                    <<" xy="<<pixel%frame.width<<','<<pixel/frame.width<<" size="<<frame.width<<'x'<<frame.height
                    <<" invalid_coverage="<<invalidCoverage<<" fabricated="<<fabricated;
                const auto rgba=[&](const char* name,const glm::vec4& v){detail<<' '<<name<<"_rgba=[";
                    for(unsigned c=0;c<4;++c)detail<<(c?",":"")<<v[c];detail<<"] "<<name<<"_bits=["<<std::hex;
                    for(unsigned c=0;c<4;++c)detail<<(c?",":"")<<glm::floatBitsToUint(v[c]);detail<<std::dec<<']';};
                rgba("raw",value);
                if(pixel<accumulated.size())rgba("temporal",accumulated[pixel]);
                if(pixel<contact.size())rgba("contact",contact[pixel]);
                if(previous){
                    if(pixel<previous->raw.size())rgba("paired_raw",previous->raw[pixel]);
                    std::size_t different=result.atlas.size();
                    if(previous->atlas.size()==result.atlas.size()){
                        const auto* a=reinterpret_cast<const unsigned char*>(previous->atlas.data());
                        const auto* b=reinterpret_cast<const unsigned char*>(result.atlas.data());
                        for(std::size_t byte=0;byte<result.atlas.size()*sizeof(glm::vec4);++byte)if(a[byte]!=b[byte]){
                            different=byte/sizeof(glm::vec4);break;}
                    }
                    detail<<" paired_atlas_extent="<<previous->atlas.size()<<" atlas_bytes_equal="
                        <<(previous->atlas.size()==result.atlas.size()&&different==result.atlas.size());
                    if(different<result.atlas.size()){
                        detail<<" atlas_first_diff_texel="<<different;rgba("atlas",result.atlas[different]);
                        rgba("paired_atlas",previous->atlas[different]);rgba("atlas_position",cache->Attributes()[different]);
                    }
                }
                detail<<" history_used="<<stats.surfaceCacheHistoryUsed<<" probe_slots="<<stats.surfaceCacheProbeSlots
                    <<" probe_ray_budget="<<stats.surfaceCacheProbeRayBudget<<" directional="<<stats.surfaceCacheDirectionalCollected
                    <<" spatial_passes="<<stats.surfaceCacheSpatialPasses<<" temporal_passes="<<stats.surfaceCacheTemporalPasses
                    <<" contact="<<stats.surfaceCacheContactCollected<<" extra_readbacks=0";
                std::cout<<detail.str()<<std::endl;
            }
            Check(!invalidCoverage,"Upload fixture produced invalid raw coverage");
            Check(!fabricated,"Unreadable raw GI acquired fabricated lighting");
        }
        Check(stats.surfaceCacheSpatialPasses>0&&stats.surfaceCacheTemporalPasses>0&&stats.surfaceCacheContactCollected,
            "Upload fixture disabled a production GI stage");
        return result;};
    const auto expectFull=[&](bool geometry,const char* label){const auto stats=lab.PipelineStatistics();
        Check(stats.surfaceCacheAttributeUploadedTexels==cache->Attributes().size()&&
            stats.surfaceCacheMetadataUploadedTexels==cache->Metadata().size(),std::string(label)+" did not upload the complete cache");
        Check(stats.surfaceCacheGeometryUploadedTexels==(geometry?scene->PackedGeometry()->Pixels().size():0),
            std::string(label)+" geometry upload disagrees with its generation");
        Check(stats.surfaceCacheUploadRegions==(geometry?3u:2u),std::string(label)+" full upload region count changed");
        Check(stats.surfaceCacheLightingUpdated&&stats.surfaceCacheUpdatedTexels==cache->TexelCount(),
            std::string(label)+" did not retain complete card relighting");};
    const auto expectPartial=[&](const char* label){const auto stats=lab.PipelineStatistics();
        Check(stats.surfaceCacheAttributeUploadedTexels>0&&stats.surfaceCacheAttributeUploadedTexels<cache->Attributes().size(),
            std::string(label)+" did not reduce attribute upload bytes");
        Check(stats.surfaceCacheMetadataUploadedTexels>0&&stats.surfaceCacheMetadataUploadedTexels<cache->Metadata().size(),
            std::string(label)+" did not reduce card metadata upload bytes");
        Check(stats.surfaceCacheGeometryUploadedTexels==scene->PackedGeometry()->Pixels().size(),
            std::string(label)+" did not upload changed trace geometry");
        Check(stats.surfaceCacheLightingUpdated&&stats.surfaceCacheUpdatedTexels==cache->TexelCount(),
            std::string(label)+" replaced complete relighting with stale lighting");
        std::cout<<"SURFACE CACHE UPLOAD partial case="<<label<<" attributes="<<stats.surfaceCacheAttributeUploadedTexels
            <<'/'<<cache->Attributes().size()<<" metadata="<<stats.surfaceCacheMetadataUploadedTexels
            <<'/'<<cache->Metadata().size()<<" geometry="<<stats.surfaceCacheGeometryUploadedTexels
            <<" regions="<<stats.surfaceCacheUploadRegions<<std::endl;};
    const auto expectRelightOnly=[&](const char* label){const auto stats=lab.PipelineStatistics();
        Check(stats.surfaceCacheAttributeUploadedTexels==0&&stats.surfaceCacheMetadataUploadedTexels==0&&
            stats.surfaceCacheGeometryUploadedTexels==0&&stats.surfaceCacheUploadRegions==0,
            std::string(label)+" uploaded unchanged cache data");
        Check(stats.surfaceCacheLightingUpdated&&stats.surfaceCacheUpdatedTexels==cache->TexelCount(),
            std::string(label)+" did not invalidate cached card lighting");};
    const auto compareAnyHit=[&](const Snapshot& previous,const char* label){
        const bool previousReuse=gi.reuseAnyHitBounds;gi.reuseAnyHitBounds=!previousReuse;
        reset();render();expectRelightOnly(label);
        const auto candidate=snapshot(label,gi.reuseAnyHitBounds?"anyhit_reuse":"anyhit_legacy",&previous);
        const auto exact=[&](const auto& a,const auto& b,const char* image){
            Check(a.size()==b.size(),std::string(label)+" any-hit comparison changed image extent");
            const auto size=a.size()*sizeof(glm::vec4);
            const auto* first=reinterpret_cast<const unsigned char*>(a.data());
            const auto* second=reinterpret_cast<const unsigned char*>(b.data());
            for(std::size_t byte=0;byte<size;++byte)if(first[byte]!=second[byte])
                Check(false,std::string(label)+" any-hit changed "+image+" byte "+std::to_string(byte));
            anyHitExactBytes+=size;
        };
        // Compare bytes directly, not just hashes or RGB epsilon: validity,
        // unknowns, coverage alpha and signed zero are part of the contract.
        exact(previous.atlas,candidate.atlas,"lighting atlas");exact(previous.raw,candidate.raw,"raw GI");++anyHitComparisons;
        std::cout<<"SURFACE CACHE ANYHIT BOUNDS compare case=\""<<label<<"\" previous_reuse="<<previousReuse
            <<" candidate_reuse="<<gi.reuseAnyHitBounds<<" pbr_single_light_shadow_atlas="<<(gi.areaLightShadows&&gi.areaShadows.enabled)
            <<" exact_rgba32f_bytes=1 relight=1"<<std::endl;
    };
    const auto compareFull=[&](const Snapshot& incremental,const char* label){
        cache=Render::BuildSurfaceCache(*scene,materials,cache->Settings());gi.cache=cache;reset();render();expectFull(false,label);
        const auto full=snapshot(label,"fresh_full");const double atlasError=difference(incremental.atlas,full.atlas),rawError=difference(incremental.raw,full.raw);
        Check(atlasError<=2e-6&&rawError<=2e-6,std::string(label)+" incremental upload differs from a fresh full upload");++comparisons;
        std::cout<<"SURFACE CACHE UPLOAD compare case="<<label<<" atlas_max="<<atlasError<<" raw_max="<<rawError<<std::endl;};
    const auto move=[&](float offset){frame.screenProbeStudyState.occluderOffset=offset;
        instances[movableCube].localToWorld=ProbePlacementTransform(objects[movableCube],frame.screenProbeStudyState);
        Check(scene->UpdateInstances(instances),"Upload fixture motion did not change its scene");
        Check(cache->Update(*scene,materials),"Upload fixture motion did not change its cache");};

    reset();render();expectFull(true,"initial user scene");const auto initial=snapshot("initial user scene","initial");
    render();const auto steady=lab.PipelineStatistics();
    Check(steady.surfaceCacheAttributeUploadedTexels==0&&steady.surfaceCacheMetadataUploadedTexels==0&&
        steady.surfaceCacheGeometryUploadedTexels==0&&steady.surfaceCacheUploadRegions==0&&!steady.surfaceCacheLightingUpdated,
        "Stable cache uploaded data or repeated card relighting");
    compareAnyHit(initial,"initial user scene");

    move(.30f);reset();render();expectPartial("one-generation move");const auto moved=snapshot("one-generation move","incremental");
    compareAnyHit(moved,"one-generation move");compareFull(moved,"one-generation move");
    move(0);reset();render();expectPartial("restore");const auto restored=snapshot("restore","incremental");
    Check(difference(initial.atlas,restored.atlas)<=2e-6&&difference(initial.raw,restored.raw)<=2e-6,
        "Restoring the cube retained stale uploaded attributes or lighting");compareFull(restored,"restore");

    const auto uploadedGeneration=cache->Generation();move(.15f);move(.45f);
    Check(cache->Generation()==uploadedGeneration+2,"Upload fixture did not actually skip a generation");
    reset();render();expectFull(true,"missed generation");compareFull(snapshot("missed generation","catchup"),"missed generation");

    move(.20f);const auto pendingGeneration=cache->Generation();
    Check(!cache->Update(*scene,materials)&&cache->Generation()==pendingGeneration&&cache->DirtyCards().empty(),
        "Upload fixture did not clear its transient dirty list with a no-op update");
    reset();render();expectFull(true,"empty dirty catchup");compareFull(snapshot("empty dirty catchup","catchup"),"empty dirty catchup");

    const auto originalAreas=frame.effects.areaLights;const auto originalPointLighting=frame.effects.lighting;
    Check(originalAreas.count>0,"Any-hit fixture requires an enabled rectangle emitter");
    frame.effects.areaLights.lights[0].radiance*=.5f;
    reset();render();expectRelightOnly("light intensity");compareAnyHit(snapshot("light intensity","light_edit"),"light intensity");
    // Exercise two supported emitter types without expanding the single-area
    // cached-shadow contract. Both cache visibility queries still use the BVH.
    frame.effects.areaLights=originalAreas;
    frame.effects.areaLights.lights[0].radiance*=glm::vec3(.25f,.8f,.45f);
    frame.effects.lighting.lightPositions[0]=glm::vec4(originalAreas.lights[0].center+glm::vec3(.35f,0,-.15f),1);
    frame.effects.lighting.lightColors[0]=glm::vec4(1.4f,.3f,1.1f,.02f);
    reset();render();expectRelightOnly("colored multiple lights");
    compareAnyHit(snapshot("colored multiple lights","light_edit"),"colored multiple lights");
    frame.effects.areaLights=originalAreas;frame.effects.lighting=originalPointLighting;

    // Changing page resolution grows every attribute plane and must recreate
    // all GPU data, including the otherwise unchanged geometry texture.
    cache=Render::BuildSurfaceCache(*scene,materials,{64});gi.cache=cache;
    reset();render();expectFull(true,"atlas growth");snapshot("atlas growth","recreated");
    std::cout<<"SURFACE CACHE UPLOAD PASS size="<<frame.width<<'x'<<frame.height<<" full_comparisons="<<comparisons
        <<" unknown_black_checks="<<unknownChecks<<" anyhit_pairs="<<anyHitComparisons<<" anyhit_exact_bytes="<<anyHitExactBytes
        <<" complete_relight=1 production_gi=1"<<std::endl;
}
