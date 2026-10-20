#pragma once

// Editor-only skin experiment. Source evidence is retained in the JSON profile;
// renderer approximations are explicit and never written into the source asset.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <json/json.h>
#include "Model/Public/static_model.h"
#include "Model/Public/model_module.h"
#include "Render/Public/Material/material_runtime.h"

// Model's other decoder has private state too. Do not share vertical-flip state
// with renderer loaders or change the project's global stb implementation.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#undef STB_IMAGE_IMPLEMENTATION
#undef STB_IMAGE_STATIC

namespace Model::EditorPreview {
namespace fs = std::filesystem;
inline std::string SkinPathText(const fs::path& path) { const auto p=path.u8string();return {p.begin(),p.end()}; }
inline fs::path SkinPath(const std::string& text) {return fs::path(std::u8string(text.begin(),text.end()));}
inline void SkinCheck(bool condition,const std::string& message) {if(!condition)throw std::runtime_error(message);}

struct SkinPreviewSettings {
    bool enabled=true,detailEnabled=true,controlEnabled=true;
    glm::vec4 scatterColor{1,.645f,.515f,1};
    float roughness=.42f,scatterStrength=.35f,wrap=.35f,specularMix=.25f,roughness2=.65f;
    float transmission=0,detailStrength=.5f,diffuseNormalBlend=.5f;
    float roughnessBias=0,roughness2Bias=.1f,specularBase1=0,specularBase2=0;
    float specularScale1=.25f,specularScale2=.2f,specularPower2=1,detailRange=5;
    glm::vec4 specularColor1{.867f,.883f,1,1},specularColor2{1};
    // Defaults recovered from Geralt's shipped pbr_skin inheritance. These two
    // colors retain source byte/255 encoding; the native kernel squares them.
    float coarseWrapScale=.2f,coarseWrapBase=.15f,diffuseStraight=1,diffuseSteep=.3f;
    float diffuseAngleExponent=.3f,diffuseDistribution=1.3f,diffuseScale=.85f,coarseColorInvRange=3;
    glm::vec4 coarseColor{248.f/255,165.f/255,81.f/255,1};
    glm::vec4 detailColorBleed{206.f/255,109.f/255,173.f/255,1};
    glm::vec4 detailTiling{10,12,0,0};
};
struct SkinMaterialEntry {std::string name;fs::path detailNormal,controlMap;};
struct SkinPreparedModel {
    std::shared_ptr<const StaticModel> geometry;
    std::vector<std::uint32_t> materialIndices;
    std::set<std::uint32_t> detailMaterials;
    std::set<std::uint32_t> controlMaterials;
    std::vector<std::string> unmatchedNames;
};
struct SkinPreviewProfile {
    bool loaded=false;
    fs::path path;
    Json::Value source;
    SkinPreviewSettings settings;
    std::vector<SkinMaterialEntry> materials;

    static SkinPreviewProfile Load(const fs::path& path) {
        std::ifstream input(path,std::ios::binary);SkinCheck(bool(input),"Cannot open skin profile: "+SkinPathText(path));
        Json::CharReaderBuilder reader;Json::Value root;std::string error;
        const bool parsed=Json::parseFromStream(reader,input,&root,&error);
        SkinCheck(parsed,"Invalid skin profile JSON: "+error);
        SkinCheck(root.isObject()&&root["schema"].isString()&&root["schema"].asString()=="model.skin-profile.v1","Unsupported skin profile schema");
        SkinCheck(root["materials"].isArray()&&!root["materials"].empty(),"Skin profile needs exact material names");
        SkinPreviewProfile profile;profile.loaded=true;profile.path=fs::absolute(path);profile.source=root["source"];
        std::set<std::string> names;
        for(const auto& item:root["materials"]) {
            SkinCheck(item.isObject()&&item["name"].isString()&&!item["name"].asString().empty(),"Skin material needs an exact name");
            SkinMaterialEntry entry;entry.name=item["name"].asString();
            SkinCheck(names.insert(entry.name).second,"Duplicate skin material: "+entry.name);
            if(item.isMember("detailNormal")) {
                SkinCheck(item["detailNormal"].isString(),"detailNormal must be a texture path");
                if(!item["detailNormal"].asString().empty()) {
                    entry.detailNormal=SkinPath(item["detailNormal"].asString());
                    if(entry.detailNormal.is_relative())entry.detailNormal=profile.path.parent_path()/entry.detailNormal;
                }
            }
            if(item.isMember("controlMap")) {
                SkinCheck(item["controlMap"].isString(),"controlMap must be a texture path");
                if(!item["controlMap"].asString().empty()) {
                    entry.controlMap=SkinPath(item["controlMap"].asString());
                    if(entry.controlMap.is_relative())entry.controlMap=profile.path.parent_path()/entry.controlMap;
                }
            }
            profile.materials.push_back(std::move(entry));
        }
        const auto& parameters=root["renderer"];
        SkinCheck(parameters.isNull()||parameters.isObject(),"Skin renderer parameters must be an object");
        const auto number=[&](const char* name,float& target,float low,float high){
            if(!parameters.isMember(name))return;
            SkinCheck(parameters[name].isNumeric(),std::string("Invalid skin parameter: ")+name);
            const double value=parameters[name].asDouble();
            SkinCheck(std::isfinite(value)&&value>=low&&value<=high,std::string("Skin parameter out of range: ")+name);
            target=float(value);
        };
        const auto vector=[&](const char* name,glm::vec4& target,bool color){
            if(!parameters.isMember(name))return;
            const auto& value=parameters[name];SkinCheck(value.isArray()&&value.size()==4,std::string(name)+" needs four numbers");
            for(unsigned i=0;i<4;++i){SkinCheck(value[i].isNumeric(),std::string("Invalid vector: ")+name);const double v=value[i].asDouble();SkinCheck(std::isfinite(v)&&(!color||(v>=0&&v<=1)),std::string("Invalid vector: ")+name);target[int(i)]=float(v);}
        };
        auto& s=profile.settings;
        number("roughness",s.roughness,.045f,1);number("skinScatterStrength",s.scatterStrength,0,1);
        number("skinWrap",s.wrap,0,1);number("skinSpecularMix",s.specularMix,0,1);number("skinRoughness2",s.roughness2,.045f,1);
        number("skinTransmission",s.transmission,0,1);number("skinDetailStrength",s.detailStrength,0,2);
        number("skinDiffuseNormalBlend",s.diffuseNormalBlend,0,1);
        number("skinRoughnessBias",s.roughnessBias,-1,1);number("skinRoughness2Bias",s.roughness2Bias,-1,1);
        number("skinSpecularBase1",s.specularBase1,0,1);number("skinSpecularBase2",s.specularBase2,0,1);
        number("skinSpecularScale1",s.specularScale1,0,1);number("skinSpecularScale2",s.specularScale2,0,1);
        number("skinSpecularPower2",s.specularPower2,.01f,16);number("skinDetailRange",s.detailRange,0,100);
        number("skinCoarseWrapScale",s.coarseWrapScale,0,4);number("skinCoarseWrapBase",s.coarseWrapBase,-4,4);
        number("skinDiffuseStraight",s.diffuseStraight,0,1);number("skinDiffuseSteep",s.diffuseSteep,0,1);
        number("skinDiffuseAngleExponent",s.diffuseAngleExponent,.001f,16);number("skinDiffuseDistribution",s.diffuseDistribution,.001f,16);
        number("skinDiffuseScale",s.diffuseScale,0,4);number("skinCoarseColorInvRange",s.coarseColorInvRange,0,100);
        vector("skinScatterColor",s.scatterColor,true);vector("skinDetailTiling",s.detailTiling,false);
        vector("skinSpecularColor1",s.specularColor1,true);vector("skinSpecularColor2",s.specularColor2,true);
        vector("skinCoarseColor",s.coarseColor,true);vector("skinDetailColorBleed",s.detailColorBleed,true);
        SkinCheck(s.detailTiling.x>0&&s.detailTiling.y>0,"Skin detail tiling XY must be positive");
        if(parameters.isMember("useSkinDetailMap")){SkinCheck(parameters["useSkinDetailMap"].isBool(),"useSkinDetailMap must be bool");s.detailEnabled=parameters["useSkinDetailMap"].asBool();}
        if(parameters.isMember("useSkinControlMap")){SkinCheck(parameters["useSkinControlMap"].isBool(),"useSkinControlMap must be bool");s.controlEnabled=parameters["useSkinControlMap"].asBool();}
        return profile;
    }

    static TextureImage DecodeDetail(const fs::path& path) {
        std::ifstream input(path,std::ios::binary|std::ios::ate);SkinCheck(bool(input),"Cannot open skin control/detail texture: "+SkinPathText(path));
        const auto length=input.tellg();SkinCheck(length>0&&length<=std::numeric_limits<int>::max(),"Skin detail image file is empty or too large");
        std::vector<unsigned char> bytes(static_cast<std::size_t>(length));input.seekg(0);input.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()));
        SkinCheck(bool(input),"Cannot read skin detail normal: "+SkinPathText(path));
        int width=0,height=0,channels=0;stbi_set_flip_vertically_on_load(0);
        TextureImage image;image.name=SkinPathText(path);image.linearColor=true;
        if(stbi_is_16_bit_from_memory(bytes.data(),int(bytes.size()))) {
            std::unique_ptr<stbi_us,decltype(&stbi_image_free)> pixels(stbi_load_16_from_memory(bytes.data(),int(bytes.size()),&width,&height,&channels,4),stbi_image_free);
            SkinCheck(bool(pixels),"Cannot decode skin detail normal: "+SkinPathText(path));
            const std::size_t count=std::size_t(width)*std::size_t(height)*4;image.rgba32f.resize(count);
            for(std::size_t i=0;i<count;++i)image.rgba32f[i]=float(pixels.get()[i])/65535.f;
        } else {
            std::unique_ptr<stbi_uc,decltype(&stbi_image_free)> pixels(stbi_load_from_memory(bytes.data(),int(bytes.size()),&width,&height,&channels,4),stbi_image_free);
            SkinCheck(bool(pixels),"Cannot decode skin detail normal: "+SkinPathText(path));
            image.rgba.assign(pixels.get(),pixels.get()+std::size_t(width)*std::size_t(height)*4);
        }
        SkinCheck(width>0&&height>0,"Skin detail normal has invalid dimensions");
        image.width=std::uint32_t(width);image.height=std::uint32_t(height);image.hasAlpha=channels==2||channels==4;
        return image;
    }

    SkinPreparedModel Prepare(const std::shared_ptr<StaticModel>& original) const {
        SkinPreparedModel prepared;prepared.geometry=original;
        if(!loaded)return prepared;
        auto copy=std::make_shared<StaticModel>(*original);
        std::map<fs::path,std::int32_t> detailTextures;
        std::map<fs::path,std::pair<std::int32_t,std::int32_t>> controlTextures;
        for(const auto& entry:materials) {
            bool matched=false;
            for(std::uint32_t i=0;i<copy->materials.size();++i) {
                auto& material=copy->materials[i];if(material.name!=entry.name)continue;matched=true;
                SkinCheck(material.alphaMode==AlphaMode::Opaque,"Skin profile rejects non-opaque material: "+entry.name);
                prepared.materialIndices.push_back(i);
                if(!entry.detailNormal.empty()) {
                    auto [it,inserted]=detailTextures.emplace(entry.detailNormal,-1);
                    if(inserted){it->second=std::int32_t(copy->textures.size());copy->textures.push_back(DecodeDetail(entry.detailNormal));}
                    material.textures[6]=it->second;prepared.detailMaterials.insert(i);
                } else material.textures[6]=-1;
                if(!entry.controlMap.empty()) {
                    auto [it,inserted]=controlTextures.emplace(entry.controlMap,std::pair<std::int32_t,std::int32_t>{-1,-1});
                    if(inserted) {
                        auto control=DecodeDetail(entry.controlMap);
                        TextureImage roughness=control;roughness.name+=" [roughness G]";roughness.hasAlpha=false;
                        for(std::size_t j=0;j<roughness.rgba.size();j+=4){const auto g=roughness.rgba[j+1];roughness.rgba[j]=roughness.rgba[j+1]=roughness.rgba[j+2]=g;roughness.rgba[j+3]=255;}
                        for(std::size_t j=0;j<roughness.rgba32f.size();j+=4){const float g=roughness.rgba32f[j+1];roughness.rgba32f[j]=roughness.rgba32f[j+1]=roughness.rgba32f[j+2]=g;roughness.rgba32f[j+3]=1;}
                        it->second.first=std::int32_t(copy->textures.size());copy->textures.push_back(std::move(control));
                        it->second.second=std::int32_t(copy->textures.size());copy->textures.push_back(std::move(roughness));
                    }
                    material.textures[5]=it->second.first;material.textures[3]=it->second.second;
                    prepared.controlMaterials.insert(i);
                }
            }
            if(!matched)prepared.unmatchedNames.push_back(entry.name);
        }
        SkinCheck(!prepared.materialIndices.empty(),"Skin profile matched zero materials; check exact imported material names");
        prepared.geometry=std::move(copy);return prepared;
    }

    void Apply(ModelResource& resource,const SkinPreparedModel& prepared) const {
        if(!loaded)return;
        for(const auto i:prepared.materialIndices) {
            const auto& handles=resource.MaterialHandles();SkinCheck(i<handles.size(),"Skin material handle index is invalid");
            const auto set=[&](const char* name,const Render::Material::ParameterValue& value) {
                std::string error;const bool applied=resource.Materials().SetParameter(handles[i],name,value,&error);
                SkinCheck(applied,"Skin parameter "+resource.Source().materials[i].name+" / "+name+": "+error);
            };
            const auto& s=settings;
            set("skinEnabled",s.enabled);set("skinScatterColor",s.scatterColor);set("skinScatterStrength",s.scatterStrength);
            set("skinWrap",s.wrap);set("skinSpecularMix",s.specularMix);set("skinRoughness2",s.roughness2);
            set("skinTransmission",s.transmission);set("skinDetailStrength",s.detailStrength);set("skinDetailTiling",s.detailTiling);
            set("skinDiffuseNormalBlend",s.diffuseNormalBlend);
            set("skinRoughnessBias",s.roughnessBias);set("skinRoughness2Bias",s.roughness2Bias);
            set("skinSpecularColor1",s.specularColor1);set("skinSpecularColor2",s.specularColor2);
            set("skinSpecularBase1",s.specularBase1);set("skinSpecularBase2",s.specularBase2);
            set("skinSpecularScale1",s.specularScale1);set("skinSpecularScale2",s.specularScale2);
            set("skinSpecularPower2",s.specularPower2);set("skinDetailRange",s.detailRange);
            set("skinCoarseWrapScale",s.coarseWrapScale);set("skinCoarseWrapBase",s.coarseWrapBase);
            set("skinDiffuseStraight",s.diffuseStraight);set("skinDiffuseSteep",s.diffuseSteep);
            set("skinDiffuseAngleExponent",s.diffuseAngleExponent);set("skinDiffuseDistribution",s.diffuseDistribution);
            set("skinDiffuseScale",s.diffuseScale);set("skinCoarseColorInvRange",s.coarseColorInvRange);
            set("skinCoarseColor",s.coarseColor);set("skinDetailColorBleed",s.detailColorBleed);
            set("useSkinDetailMap",s.detailEnabled&&prepared.detailMaterials.contains(i));
            set("useSkinControlMap",s.controlEnabled&&prepared.controlMaterials.contains(i));
            // Preserve the control texture's G as a scalar roughness baseline
            // when skinEnabled=false; never interpret normal alpha as roughness.
            set("roughness",s.roughness);set("useRoughnessMap",prepared.controlMaterials.contains(i));
            set("useFlowMap",false);set("uvFlow",glm::vec4(0));
            set("useDistortionMap",false);set("useAoMap",false);set("ao",1.f);
        }
    }
};
} // namespace Model::EditorPreview
