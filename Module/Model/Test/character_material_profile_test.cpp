#include "Model/Public/character_material_profile.h"
#include <cmath>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>

int main(){
    try{
        const auto require=[](bool value,const std::string& message){if(!value)throw std::runtime_error(message);};
        const auto root=std::filesystem::path(MODEL_SOURCE_DIR)/"Asset/Diana";std::string error;
        const auto original=Model::StaticModelLoader::Load(root/"diana.glb",{},&error);require(bool(original),error);
        require(original->meshes.size()==44&&original->TriangleCount()==216538,"Actual Diana geometry/part selection changed");
        auto profile=Model::CharacterMaterialProfile::Load(root/"materials.json",&error);require(profile.has_value(),error);
        Model::CharacterMaterialReport report;auto prepared=profile->Prepare(original,&report,&error);require(bool(prepared),error);
        require(prepared.get()!=original.get(),"Material preparation mutated original model");
        require(report.matchedMaterials==17&&report.textureBindings==81&&report.decodedTextures==61,"Diana material names or texture coverage incomplete");
        require(report.categories.contains("skin")&&report.categories.contains("cloth"),"Skin/cloth material classification missing");
        std::size_t diffusionMaterials=0,eyeMaterials=0,eyeShellMaterials=0,distinctEyeUvs=0;
        for(const auto& entry:profile->Entries()){
            const auto enabled=[&](const char* key){auto it=entry.parameters.find(key);return it!=entry.parameters.end()&&std::holds_alternative<bool>(it->second)&&std::get<bool>(it->second);};
            if(enabled("skinDiffusionEnabled"))++diffusionMaterials;
            if(enabled("eyeEnabled")){++eyeMaterials;require(entry.textures.contains("distortion")&&entry.textures.contains("flow"),"Refractive eye has no depth/sclera textures");}
            if(enabled("eyeShellEnabled")){++eyeShellMaterials;require(entry.name=="EyeShell_Mat","Eye-shell processing leaked into another material");}
            if(entry.name=="Face_mat")require(enabled("useReSkinControlMap")&&!enabled("useSkinControlMap")&&entry.textures.contains("distortion"),"RE face control was confused with native RGB skin control");
            if(entry.name=="ch0100_00_Body")require(!enabled("skinDiffusionEnabled"),"Source channel-zero body unexpectedly participates in SSS");
        }
        for(const auto& mesh:original->meshes)if(original->materials[mesh.materialIndex].name.starts_with("EyeBall_")){
            require(mesh.hasSecondUV,"Actual eye mesh lost source spherical UV1");
            for(const auto& vertex:mesh.vertices)if(glm::length(vertex.uv1-vertex.uv)>.001f)++distinctEyeUvs;
        }
        require(diffusionMaterials==3&&eyeMaterials==2&&eyeShellMaterials==1&&distinctEyeUvs>100,"Source SSS profile, eye-shell tag or eye UV domains are incomplete");
        const auto fiber=profile->StrandParameters().find("physicalScattering");
        require(fiber!=profile->StrandParameters().end()&&std::holds_alternative<bool>(fiber->second)&&std::get<bool>(fiber->second),"Physical hair scattering profile flag was lost");
        const auto volume=profile->StrandParameters().find("spatialVolume");
        require(volume!=profile->StrandParameters().end()&&std::holds_alternative<bool>(volume->second)&&std::get<bool>(volume->second),"Spatial hair volume profile flag was lost");
        const auto skyVolume=profile->StrandParameters().find("volumeSkyTransmission");
        require(skyVolume!=profile->StrandParameters().end()&&std::holds_alternative<bool>(skyVolume->second)&&std::get<bool>(skyVolume->second),"Diana environment optical paths were disabled");
        const auto dualIbl=profile->StrandParameters().find("dualScatteringIbl");
        require(dualIbl!=profile->StrandParameters().end()&&std::holds_alternative<bool>(dualIbl->second)&&std::get<bool>(dualIbl->second),"Diana must use source dual scattering rather than the dark forward-only closure");
        const auto backGain=profile->StrandParameters().find("backscatterScale");
        require(backGain!=profile->StrandParameters().end()&&std::holds_alternative<float>(backGain->second)&&std::abs(std::get<float>(backGain->second)-1.f)<1e-7f,"Source TT/backscatter gain changed");
        const auto sourceWidths=profile->StrandParameters().find("normalizedVolumeWidth");
        require(sourceWidths!=profile->StrandParameters().end()&&std::holds_alternative<bool>(sourceWidths->second)&&std::get<bool>(sourceWidths->second),"Diana source normalized DSVolume widths were lost");
        const auto sourceAverage=profile->StrandParameters().find("volumeAverageWidth");
        require(sourceAverage!=profile->StrandParameters().end()&&std::holds_alternative<float>(sourceAverage->second)&&std::abs(std::get<float>(sourceAverage->second)-.00013852179108653218f)<1e-10f,"Source STRD average width was changed");
        const auto& hairAssets=profile->StrandAssets();
        require(hairAssets.contains("gradientTable")&&hairAssets.contains("colorCoordinates")&&hairAssets.contains("nttTable")&&hairAssets.contains("nativeMomentTable"),"Native hair lookup or authored appearance asset is missing");
        auto gradients=Model::LoadStrandTextureTable(hairAssets.at("gradientTable"),&error);require(bool(gradients),error);
        require(gradients->width==128&&gradients->height==3&&gradients->texels.size()==384,"Original A/B/C gradients were reduced or reshaped");
        require(std::abs(gradients->texels.front().r-.07421356838f)<1e-7f&&std::abs(gradients->texels.front().g-.05951123816f)<1e-7f,"Gradient sRGB texels were not decoded exactly once before filtering");
        auto ntt=Model::LoadStrandTextureTable(hairAssets.at("nttTable"),&error);require(bool(ntt),error);
        require(ntt->width==128&&ntt->height==128&&ntt->texels.size()==16384,"Original Ntt texture dimensions were lost");
        require(ntt->texels[8192].r==.327392578125f&&ntt->texels[8192].g==1.025390625f,"Native truncated-half Ntt upload values changed");
        const auto moment=Model::LoadStrandTextureTable(hairAssets.at("nativeMomentTable"),&error);require(bool(moment),error);
        require(moment->width==64&&moment->height==1024&&moment->texels.size()==65536,"Native DSLUT must retain all64x64x16 texels");
        require(moment->texels.front().r==.9462890625f&&moment->texels.front().a==.1400146484375f,"Native half-expanded moment asset changed");
        for(const auto& texel:moment->texels)for(int channel=0;channel<4;++channel)require(std::isfinite(texel[channel])&&texel[channel]>=0&&texel[channel]<=1,"Native DSLUT contains invalid hemisphere coefficients");
        const auto nativeKey=profile->StrandParameters().find("nativeMomentParameters");
        require(nativeKey!=profile->StrandParameters().end()&&std::holds_alternative<glm::vec4>(nativeKey->second),"Native DSLUT material key is missing");
        const auto key=std::get<glm::vec4>(nativeKey->second);
        require(key==glm::vec4(1.5499999523162842f,.04652056470513344f,.800000011920929f,.019999999552965164f),"Native DSLUT generation key differs from the audited source material");
        const float ratio=(1.55f-1.f)/(1.55f+1.f);require(key.y==ratio*ratio,"Native CPU Fresnel writer formula disagrees with asset key");
        for(const char* flag:{"useNativeMomentLut","volumeHardwareFiltering","strandMotionVectors","volumePreserveNearField"}){
            const auto value=profile->StrandParameters().find(flag);
            require(value!=profile->StrandParameters().end()&&std::holds_alternative<bool>(value->second)&&std::get<bool>(value->second),std::string("Diana strand feature disabled: ")+flag);
        }
        std::vector<Render::StrandColorCoordinate> colorCoordinates;
        require(Model::LoadStrandColorCoordinates(hairAssets.at("colorCoordinates"),436975,17500,colorCoordinates,&error),error);
        require(colorCoordinates.size()==436975,"Appearance coordinates no longer match the extracted strand topology");
        auto priorCoordinates=colorCoordinates;
        require(!Model::LoadStrandColorCoordinates(hairAssets.at("colorCoordinates"),436974,17500,colorCoordinates,&error)&&!error.empty()&&colorCoordinates.size()==priorCoordinates.size(),"Mismatched appearance topology was accepted or changed the prior data");
        const auto voxel=profile->StrandParameters().find("volumeVoxelSize");
        require(voxel!=profile->StrandParameters().end()&&std::holds_alternative<float>(voxel->second)&&std::abs(std::get<float>(voxel->second)-.005f)<1e-7f,"Source DSVolume voxel size was not converted to metres");
        std::set<int> slots;std::size_t detailed=0;
        for(const auto& m:prepared->materials){for(int i=0;i<8;++i)if(m.textures[i]>=0){slots.insert(i);const auto& image=prepared->textures.at(m.textures[i]);require(image.width>0&&image.height>0,"Empty material texture");require(image.linearColor==(i!=0),"Material color/data color-space mismatch");if(!image.rgba.empty()){std::uint8_t lo=255,hi=0;for(std::size_t n=0;n<image.rgba.size();n+=4){lo=std::min(lo,image.rgba[n]);hi=std::max(hi,image.rgba[n]);}if(hi>lo+8)++detailed;}}}
        require(slots.contains(0)&&slots.contains(1)&&slots.contains(3)&&detailed>=10,"Missing detailed albedo/normal/roughness images");
        const auto temporary=std::filesystem::temp_directory_path()/("invalid_diana_material_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".json");
        const auto reject=[&](const char* text){std::ofstream(temporary)<<text;auto bad=Model::CharacterMaterialProfile::Load(temporary,&error);require(!bad&& !error.empty(),"Malformed material profile was accepted");};
        reject(R"({"schema":"model.character-materials.v1","materials":[{"name":"skin","textures":{"albedo":"../outside.png"}}]})");
        reject(R"({"schema":"model.character-materials.v1","materials":[{"name":"skin","parameters":{"baseColor":[1,2,3,4,5]}}]})");
        reject(R"({"schema":"model.character-materials.v1","materials":[{"name":"skin","roughness":-1}]})");
        reject(R"({"schema":"model.character-materials.v1","materials":[{"name":"skin"}],"strandAssets":{"nttTable":"../outside.lutbin"}})");
        reject(R"({"schema":"model.character-materials.v1","materials":[{"name":"skin"}],"strandAssets":{"nativeMomentTable":"../outside.lutbin"}})");
        reject(R"({"schema":"model.character-materials.v1","materials":[{"name":"skin"}],"strands":{"nativeMomentParameters":[1.55,.04,.8]}})");
        reject(R"({"schema":"model.character-materials.v1","materials":[{"name":"skin"}],"strands":{"nativeMomentParameters":[1.55,2,.8,.02]}})");
        reject(R"({"schema":"model.character-materials.v1","materials":[{"name":"skin"}],"strands":{"useNativeMomentLut":1}})");
        reject(R"({"schema":"model.character-materials.v1","materials":[{"name":"skin"}],"strands":{"volumeExactIntegration":1}})");
        reject(R"({"schema":"model.character-materials.v1","materials":[{"name":"skin"}],"strands":{"volumeProxyBlockers":[1,0,0]}})");
        std::filesystem::remove(temporary);
        std::cout<<"Diana: "<<prepared->VertexCount()<<" vertices, "<<prepared->TriangleCount()<<" triangles, "<<report.matchedMaterials<<" materials, "<<report.textureBindings<<" texture bindings; geometry preservation, color space, texture detail and malformed profiles passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
