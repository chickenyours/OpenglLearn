#pragma once
#include "Model/Public/model_module.h"
#include "Render/Public/Material/material.h"
#include "Render/Public/Pipeline/strand_rendering.h"
#include <map>
#include <optional>

namespace Model {
std::shared_ptr<const Render::StrandTextureTable> LoadStrandTextureTable(
    const std::filesystem::path&,std::string* error=nullptr);
bool LoadStrandColorCoordinates(const std::filesystem::path&,std::size_t expectedVertices,
    std::size_t expectedStrands,std::vector<Render::StrandColorCoordinate>&,std::string* error=nullptr);
// Offline extraction produces this portable profile; render parameters retain
// exact material-name matching and use MaterialService's existing contracts.
struct CharacterMaterialEntry {
    std::string name,category;
    std::optional<glm::vec4> baseColor;
    std::optional<glm::vec3> emissive;
    std::optional<float> metallic,roughness,ao,normalScale,alphaCutoff;
    std::optional<bool> twoSided;
    std::optional<AlphaMode> alphaMode;
    std::map<std::string,std::filesystem::path> textures;
    std::map<std::string,Render::Material::ParameterValue> parameters;
};
struct CharacterMaterialReport {
    std::size_t matchedMaterials=0,textureBindings=0,decodedTextures=0;
    std::map<std::string,std::size_t> categories;
    std::vector<std::string> unmatchedEntries;
};
class CharacterMaterialProfile {
public:
    static std::optional<CharacterMaterialProfile> Load(const std::filesystem::path&,std::string* error=nullptr);
    std::shared_ptr<const StaticModel> Prepare(std::shared_ptr<const StaticModel>,CharacterMaterialReport* report=nullptr,std::string* error=nullptr)const;
    bool Apply(ModelResource&,std::string* error=nullptr)const;
    const auto& Entries()const{return entries_;}
    const auto& StrandParameters()const{return strands_;}
    const auto& StrandAssets()const{return strandAssets_;}
    const auto& Path()const{return path_;}
private:
    std::filesystem::path path_;
    std::vector<CharacterMaterialEntry> entries_;
    std::map<std::string,Render::Material::ParameterValue> strands_;
    std::map<std::string,std::filesystem::path> strandAssets_;
};
} // namespace Model
