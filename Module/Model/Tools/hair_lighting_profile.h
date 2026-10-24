#pragma once
#include "Model/Public/character_material_profile.h"
#include "Render/Public/Pipeline/sky_light.h"
#include "Render/Public/Pipeline/strand_lighting.h"
#include <json/json.h>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace Model::EditorPreview {
struct HairLightingProfile {
    std::string label;
    std::shared_ptr<const Render::SkyEnvironment> environment;
    Render::StrandIndirectLightingSettings indirect;
    static HairLightingProfile Load(const std::filesystem::path& path){
        const auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
        std::ifstream file(path,std::ios::binary);require(bool(file),"Cannot open source hair lighting profile");
        Json::CharReaderBuilder reader;Json::Value data;std::string error;
        require(Json::parseFromStream(reader,file,&data,&error),"Invalid source hair lighting JSON");
        require(data["schema"].asString()=="model.hair-lighting.v1","Unsupported source hair lighting profile");
        const auto encoded=data["environmentTable"].asString();
        const auto relative=std::filesystem::path(std::u8string(encoded.begin(),encoded.end()));
        require(!relative.empty()&&relative.is_relative(),"Source environment must be a relative asset path");
        for(const auto& component:relative)require(component!="..","Source environment escapes profile directory");
        auto table=LoadStrandTextureTable(path.parent_path()/relative,&error);if(!table)throw std::runtime_error(error);
        std::vector<glm::vec3> radiance; radiance.reserve(table->texels.size());
        for(const auto& texel:table->texels){
            require(glm::all(glm::greaterThanEqual(glm::vec3(texel),glm::vec3(0)))&&glm::all(glm::lessThanEqual(glm::vec3(texel),glm::vec3(60000))),"Source HDR exceeds the supported scene-linear range");
            radiance.emplace_back(texel);
        }
        HairLightingProfile result;result.label=data["label"].asString();
        result.environment=Render::BakeSkyEnvironment(table->width,table->height,radiance);
        require(bool(result.environment),"Could not bake source HDR environment");
        const auto& cube=data["indirectCube"];
        require(cube.isArray()&&cube.size()==6,"Source indirect cube requires six coefficients");
        for(unsigned i=0;i<6;++i){require(cube[i].isArray()&&cube[i].size()==3,"Invalid source indirect cube RGB");
            for(unsigned c=0;c<3;++c){require(cube[i][c].isNumeric(),"Source indirect RGB must be numeric");const double v=cube[i][c].asDouble();require(std::isfinite(v)&&v>=0&&v<=60000,"Invalid source indirect coefficient");result.indirect.coefficients[i][c]=float(v);}
        }
        result.indirect.enabled=true;
        result.indirect.multiplier=data.get("producerMultiplier",1).asFloat();
        result.indirect.exposureEV=data.get("hostExposureEV",-7).asFloat();
        result.indirect.rotation=data.get("rotationRadians",0).asFloat();
        result.indirect.hostAmbientScale=data.get("hostAmbientScale",1).asFloat();
        require(std::isfinite(result.indirect.multiplier)&&result.indirect.multiplier>=0&&result.indirect.multiplier<=64&&
            std::isfinite(result.indirect.exposureEV)&&std::abs(result.indirect.exposureEV)<=24&&std::isfinite(result.indirect.rotation)&&
            std::isfinite(result.indirect.hostAmbientScale)&&result.indirect.hostAmbientScale>=0&&result.indirect.hostAmbientScale<=1,"Invalid source environment scale, rotation or host ambient scale");
        return result;
    }
};
}
