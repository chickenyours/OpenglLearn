#include "BPRMaterial.h"

#include "code/DebugTool/ConsoleHelp/color_log.h"

#include "code/ECS/Core/Resource/resource_manager.h"
#include "code/Resource/Texture/texture.h"
#include "code/ToolAndAlgorithm/Json/json_helper.h"

using namespace Resource;

using ECS::Core::ResourceSystem::ResourceManager;

bool BPRMaterial::LoadArgs(
    const std::string& materialType,
    const Json::Value& textures,
    const Json::Value& properties,
    const Json::Value& shaders
){

    std::string diffuseMapConfigPath;
    std::string normalMapConfigPath;
    std::string roughnessMapConfigPath;
    std::string aoMapConfigPath;

    if(materialType != "BPR"){
        LOG_ERROR("MATERIAL BPRMaterial", "ERROR materialType: " + materialType);
        return false;
    }

    // load property
    if(!Tool::JsonHelper::TryGetVec3(properties, "color", property.color, glm::vec3(0.0,0.0,0.0)))
        LOG_WARNING("MATERIAL BPRMaterial", "No color");

    if (!Tool::JsonHelper::TryGetFloat(properties, "roughness", property.roughness, 0.5f))
        LOG_WARNING("MATERIAL BPRMaterial", "No roughness");

    if (!Tool::JsonHelper::TryGetFloat(properties, "metallic", property.metallic, 0.5f))
        LOG_WARNING("MATERIAL BPRMaterial", "No metallic");

    if (!Tool::JsonHelper::TryGetBool(properties, "needNormalMap", property.needNormalMap, false))
        LOG_WARNING("MATERIAL BPRMaterial", "No needNormalMap");

    if (!Tool::JsonHelper::TryGetBool(properties, "needRoughnessMap", property.needRoughnessMap, false))
        LOG_WARNING("MATERIAL BPRMaterial", "No needRoughnessMap");

    if (!Tool::JsonHelper::TryGetBool(properties, "needMetallicMap", property.needMetallicMap, false))
        LOG_WARNING("MATERIAL BPRMaterial", "No needMetallicMap");

    if (!Tool::JsonHelper::TryGetBool(properties, "needAOMap", property.needAOMap, false))
        LOG_WARNING("MATERIAL BPRMaterial", "No needAOMap");

    // load texture config
    if(!Tool::JsonHelper::TryGetString(textures,"diffuseMap",diffuseMapConfigPath, DefualtTexture2DPath))
        LOG_WARNING("MATERIAL BPRMaterial", "No diffuseMapConfigPath");

    if(!Tool::JsonHelper::TryGetString(textures,"normalMap",normalMapConfigPath, DefualtTexture2DPath) && property.needNormalMap)
        LOG_WARNING("MATERIAL BPRMaterial", "No normalMapConfigPath");


    if(!Tool::JsonHelper::TryGetString(textures,"roughnessMap",roughnessMapConfigPath, DefualtTexture2DPath) && property.needRoughnessMap)
        LOG_WARNING("MATERIAL BPRMaterial", "No roughnessMapConfigPath");

    if(!Tool::JsonHelper::TryGetString(textures,"aoMap",aoMapConfigPath, DefualtTexture2DPath) && property.needAOMap);
        LOG_WARNING("MATERIAL BPRMaterial", "No aoMapConfigPath");
    
    diffuseMap = ResourceManager::GetInctance().Get<Texture>(diffuseMapConfigPath);
    normalMap = ResourceManager::GetInctance().Get<Texture>(normalMapConfigPath);
    roughnessMap = ResourceManager::GetInctance().Get<Texture>(roughnessMapConfigPath);
    aoMap = ResourceManager::GetInctance().Get<Texture>(aoMapConfigPath);

    return true;
}