// Independent, untimed visual-artifact fixture. Original GI90 gates are unchanged.
namespace SurfaceCacheArtifactFixture {
using namespace SurfaceCacheBudgetFixture;
constexpr unsigned StaticSamples=8,FloatRecordLimit=65536;
struct NoisePixel {std::size_t pixel=0;glm::vec4 position{0},normal{0};double mean=0,m2=0;};
std::string JsonString(const std::string& text) {
    std::string out="\"";for(char c:text){if(c=='\\'||c=='\"')out+='\\';out+=c;}return out+'\"';
}
Render::PipelineCamera Camera(unsigned view,float aspect) {
    if(view==0){auto c=SealedGiRoomCameraPreset(0,aspect);
        c.view=glm::lookAt(c.position,glm::vec3(0,4.5f,0),glm::vec3(0,1,0));return c;}
    return SurfaceCacheFreshSamplingCamera(view==1?1:2,aspect,view==2);
}
std::vector<bool> Owners(unsigned view) {
    const auto& objects=SealedGiRoomObjects();std::vector<bool> result(objects.size());
    for(std::size_t i=0;i<objects.size();++i){
        const std::string name=objects[i].name;
        // Include the entire visible slab, not only selected bright pixels.
        result[i]=view==0?name.rfind("Ceiling ",0)==0:
            (i==0||name.rfind("Porcelain ",0)==0);
    }return result;
}
bool Roi(glm::vec4 position,glm::vec4 normal,const std::vector<bool>& owners) {
    if(position.w<=0||normal.w<=0)return false;
    Check(position.w==std::floor(position.w),"Artifact receiver owner is not an integer");
    const auto owner=unsigned(position.w)-1u;
    Check(owner<owners.size(),"Artifact receiver owner is out of range");return owners[owner];
}
void MatrixJson(std::ostream& out,const glm::mat4& m) {
    out<<'[';for(unsigned col=0;col<4;++col)for(unsigned row=0;row<4;++row){
        if(col||row)out<<',';out<<m[col][row];}out<<']';
}
Render::PipelineCamera LitCamera(unsigned view,float aspect) {
    auto camera=SealedGiRoomCameraPreset(0,aspect);
    const std::array<glm::vec3,2> eyes{{{-1.0f,2.20f,.70f},{0,3.40f,-3.05f}}};
    const std::array<glm::vec3,2> targets{{{1.45f,1.20f,-.70f},{0,.80f,.25f}}};
    Check(view<eyes.size(),"GI94 artifact camera is out of range");
    camera.position=eyes[view];
    camera.view=glm::lookAt(camera.position,targets[view],glm::vec3(0,1,0));return camera;
}
void Run(MaterialLabModule& lab,const Options& options,bool litViews=false) {
    Check(options.vulkan&&options.gi90Profile==21&&!options.softwareRays,
        "GI90 artifacts require the native UE High profile");
    Check(!options.screenshot.empty(),"GI90 artifacts require --screenshot as an output prefix");
    auto directory=std::filesystem::path(options.screenshot).parent_path();
    if(!directory.empty())std::filesystem::create_directories(directory);
    std::ostringstream manifest;manifest<<std::setprecision(9);
    manifest<<"{\n\"format\":"<<JsonString(litViews?"GI94 artifacts v1":"GI90 artifacts v1")<<",\"diagnostic_only\":true,"
        "\"acceptance_thresholds_changed\":false,\"warm_frames\":64,\"static_frames\":8,"
        "\"model_frames\":16,\"settle_frames\":16,\"float_record_limit\":"<<FloatRecordLimit
        <<",\"float_sampling\":\"uniform ROI ordinal; noise uses every ROI pixel\","
        "\"binary_layout\":\"little-endian uint32[8], then float32[records]["<<(litViews?28:24)<<"]\","
        "\"binary_fields\":\"pixel_x,y,owner,history_count; world_xyz,1; normal_xyzw; "
        "final_reflected_gi_rgba; reflected_gi_before_ao_rgba; unmodulated_history_rgba"
        <<(litViews?"; cached_direct_source_rgba":"")<<"\",";
    if(litViews)manifest<<"\"scene\":\"sealed_gi_room\",\"direct_source_semantics\":"
        "\"production cached diffuse plus primary specular/emissive; excludes GI; not pure direct diffuse\",";
    manifest<<"\"motion_noise_claim\":false,\"views\":[\n";
    const std::vector<const char*> names=litViews?std::vector<const char*>{"lit_seam","top_curve"}:
        std::vector<const char*>{"ceiling","upright_seam","low_sphere_bottom"};
    for(unsigned view=0;view<names.size();++view){
        auto frame=Make(options,21,false);frame.present=options.benchmarkPresent;
        frame.camera=litViews?LitCamera(view,frame.OutputAspect()):Camera(view,frame.OutputAspect());const auto base=*frame.camera;
        const auto light=frame.effects.areaLights.lights[0].radiance;
        const auto owners=Owners(litViews?1u:view);Reset(frame);
        for(unsigned f=0;f<64;++f)Draw(lab,frame);
        Check(lab.PipelineStatistics().surfaceCacheUeLumenHigh,"Artifact fixture lost the UE producer");
        std::vector<NoisePixel> noise;std::vector<std::string> snapshots;
        std::uint64_t staticReadable=0;double noiseRms=0,meanLuma=0;
        const auto inspect=[&](const char* phase,unsigned phaseFrame,bool accumulate,bool save){
            const auto gi=lab.ReadbackSurfaceCacheTemporalGi();
            const auto source=lab.ReadbackScreenProbeSources();
            Finite(gi,"artifact final GI");Finite(source.sourceViewPositions,"artifact source positions");
            Finite(source.sourceWorldNormals,"artifact source normals");
            const auto count=std::size_t(frame.width)*frame.height;
            Check(gi.size()==count&&source.sourceViewPositions.size()==count&&source.sourceWorldNormals.size()==count,
                "Artifact images do not have the requested full extent");
            if(accumulate){
                if(noise.empty())for(std::size_t i=0;i<count;++i)
                    if(Roi(source.sourceViewPositions[i],source.sourceWorldNormals[i],owners))
                        noise.push_back({i,source.sourceViewPositions[i],source.sourceWorldNormals[i],0,0});
                Check(!noise.empty(),"Artifact static ROI is empty");
                for(auto& p:noise){
                    Check(source.sourceViewPositions[p.pixel]==p.position&&source.sourceWorldNormals[p.pixel]==p.normal,
                        "Static noise sample changed pose, owner or receiver geometry");
                    const auto v=gi[p.pixel];staticReadable+=v.w>0;
                    const double l=glm::dot(glm::dvec3(v),glm::dvec3(.2126,.7152,.0722));
                    const double delta=l-p.mean;p.mean+=delta/double(phaseFrame+1);p.m2+=delta*(l-p.mean);
                }
            }
            if(!save)return;
            const auto history=lab.ReadbackSurfaceCacheTemporalHistory();
            const auto capture=lab.ReadbackSurfaceReceiverCapture();
            Finite(history,"artifact unmodulated history");Finite(capture.reflectance,"artifact reflectance");
            Check(history.size()==count&&capture.reflectance.size()==count,"Artifact AO stages are incomplete");
            std::vector<glm::vec4> beforeAo(count);
            for(std::size_t i=0;i<count;++i)
                beforeAo[i]=glm::vec4(glm::vec3(history[i])*glm::max(glm::vec3(capture.reflectance[i]),glm::vec3(0)),gi[i].w);
            Finite(beforeAo,"artifact reflected GI before AO");
            std::vector<glm::vec4> directSource;
            if(litViews){
                directSource=lab.ReadbackSurfaceCacheDirectSource();
                Check(directSource.size()==count,"GI94 production direct source is missing");
                Finite(directSource,"GI94 production cached-direct base SceneColor");
            }
            std::vector<std::size_t> pixels;pixels.reserve(count/2);
            for(std::size_t i=0;i<count;++i)
                if(Roi(source.sourceViewPositions[i],source.sourceWorldNormals[i],owners))pixels.push_back(i);
            Check(!pixels.empty(),"Artifact snapshot ROI is empty");
            const auto stride=std::max<std::size_t>(1,(pixels.size()+FloatRecordLimit-1)/FloatRecordLimit);
            using Record=std::array<glm::vec4,6>;static_assert(sizeof(Record)==24*sizeof(float));
            std::vector<Record> records;records.reserve(std::min<std::size_t>(pixels.size(),FloatRecordLimit));
            std::vector<glm::vec4> directRecords;if(litViews)directRecords.reserve(records.capacity());
            const auto inverseView=glm::inverse(frame.camera->view);
            for(std::size_t at=0;at<pixels.size();at+=stride){const auto i=pixels[at];
                const auto position=source.sourceViewPositions[i];
                const auto world=inverseView*glm::vec4(glm::vec3(position),1);
                Check(std::isfinite(world.x)&&std::isfinite(world.y)&&std::isfinite(world.z),"Artifact world position is not finite");
                records.push_back({glm::vec4(float(i%frame.width),float(i/frame.width),position.w,history[i].w),
                    glm::vec4(glm::vec3(world),1),source.sourceWorldNormals[i],gi[i],beforeAo[i],history[i]});
                if(litViews)directRecords.push_back(directSource[i]);
            }
            const auto label=(litViews?"gi94_artifact_"+std::string(names[view]):"artifact_v"+std::to_string(view))+
                "_"+phase+"_f"+std::to_string(phaseFrame);
            const auto totalPath=OutputPath(options,label+"_total");
            Save(frame.present?lab.ReadbackDisplay():lab.Readback(),totalPath);
            Tone(options,frame.width,frame.height,gi,label+"_indirect");
            Tone(options,frame.width,frame.height,beforeAo,label+"_before_ao");
            if(litViews)Tone(options,frame.width,frame.height,directSource,label+"_direct_source");
            auto binary=std::filesystem::path(OutputPath(options,label));binary.replace_extension(".f32");
            const std::array<std::uint32_t,8> header{{0x31414947u,1u,frame.width,frame.height,
                std::uint32_t(records.size()),litViews?28u:24u,std::uint32_t(pixels.size()),0u}};
            std::ofstream stream(binary,std::ios::binary);
            stream.write(reinterpret_cast<const char*>(header.data()),std::streamsize(sizeof(header)));
            if(litViews){for(std::size_t i=0;i<records.size();++i){
                stream.write(reinterpret_cast<const char*>(&records[i]),sizeof(Record));
                stream.write(reinterpret_cast<const char*>(&directRecords[i]),sizeof(glm::vec4));
            }}else stream.write(reinterpret_cast<const char*>(records.data()),std::streamsize(records.size()*sizeof(Record)));
            stream.close();Check(bool(stream),"Cannot write artifact float records");
            std::ostringstream row;row<<std::setprecision(9)<<"{\"phase\":"<<JsonString(phase)
                <<",\"phase_frame\":"<<phaseFrame<<",\"object_offset\":"<<frame.screenProbeStudyState.occluderOffset
                <<",\"roi_pixels\":"<<pixels.size()<<",\"float_records\":"<<records.size()
                <<",\"float_stride\":"<<stride<<",\"float_file\":"<<JsonString(binary.generic_string())
                <<",\"total_png\":"<<JsonString(std::filesystem::path(totalPath).generic_string())
                <<",\"indirect_png\":"<<JsonString(std::filesystem::path(OutputPath(options,label+"_indirect")).generic_string())
                <<",\"before_ao_png\":"<<JsonString(std::filesystem::path(OutputPath(options,label+"_before_ao")).generic_string());
            if(litViews)row<<",\"direct_source_png\":"<<JsonString(std::filesystem::path(OutputPath(options,label+"_direct_source")).generic_string());
            row<<'}';
            snapshots.push_back(row.str());
            std::cout<<(litViews?"GI94":"GI90")<<" ARTIFACT SNAPSHOT view="<<view<<" phase="<<phase<<" phase_frame="<<phaseFrame
                <<" roi_pixels="<<pixels.size()<<" float_records="<<records.size()<<" untimed=1"<<std::endl;
        };
        for(unsigned f=0;f<StaticSamples;++f){Draw(lab,frame);inspect("static",f,true,f==0||f==StaticSamples-1);}
        for(const auto& p:noise){meanLuma+=p.mean;noiseRms+=p.m2/double(StaticSamples-1);}
        meanLuma/=double(noise.size());noiseRms=std::sqrt(noiseRms/double(noise.size()));
        std::cout<<(litViews?"GI94":"GI90")<<" ARTIFACT STATIC view="<<view<<" samples="<<StaticSamples<<" roi_pixels="<<noise.size()
            <<" mean_luminance="<<meanLuma<<" temporal_rms="<<noiseRms<<" readable_samples="<<staticReadable
            <<" same_pose_owner_geometry=1 motion_noise_claim=0"<<std::endl;
        for(unsigned f=0;f<16;++f){Pose(frame,base,2,f,light);Draw(lab,frame);
            if(f==7||f==15)inspect("model",f,false,true);}
        Pose(frame,base,0,0,light);
        for(unsigned f=0;f<16;++f){Draw(lab,frame);if(f==15)inspect("settle",f,false,true);}
        if(view)manifest<<",\n";
        manifest<<"{\"view\":"<<view<<",\"name\":"<<JsonString(names[view])<<",\"view_matrix_column_major\":";
        MatrixJson(manifest,base.view);manifest<<",\"projection_column_major\":";MatrixJson(manifest,base.projection);
        manifest<<",\"static_noise\":{\"samples\":8,\"roi_pixels\":"<<noise.size()<<",\"temporal_rms\":"<<noiseRms
            <<",\"mean_luminance\":"<<meanLuma<<",\"readable_samples\":"<<staticReadable
            <<",\"same_pose_owner_geometry\":true},\"snapshots\":[";
        for(std::size_t i=0;i<snapshots.size();++i){if(i)manifest<<',';manifest<<snapshots[i];}
        manifest<<"]}";
    }
    manifest<<"\n]}\n";
    auto output=std::filesystem::path(options.screenshot);
    output.replace_filename(output.stem().string()+"_artifacts.json");
    std::ofstream stream(output);stream<<manifest.str();stream.close();Check(bool(stream),"Cannot write artifact manifest");
    std::cout<<(litViews?"GI94":"GI90")<<" ARTIFACTS COMPLETE diagnostic_only=1 original_quality_gates_unchanged=1 views="
        <<names.size()<<" static_samples=8"<<std::endl;
}
} // namespace SurfaceCacheArtifactFixture
