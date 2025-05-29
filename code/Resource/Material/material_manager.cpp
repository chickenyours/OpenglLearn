#include "material_manager.h"

#include "code/DebugTool/ConsoleHelp/color_log.h"
#include "code/ToolAndAlgorithm/Json/json_helper.h"


using namespace Resource;

bool MaterialManager::LoadMaterialFromConfigFile(const std::string& configFile, Material* material){
    Json::Value config;
    std::string configType;
    const Json::Value* resource;
    std::string resourceType;
    const Json::Value* materialConfig;
    std::string materialType;
    const Json::Value* args;
    const Json::Value* textures;
    const Json::Value* properties;
    const Json::Value* shaders;

    bool flag = true;

    if(!Tool::JsonHelper::LoadJsonValueFromFile(configFile, config)){
        LOG_ERROR("RESOURCE MATERIAL","material config file can't be loaded: " + configFile);
        return false;
    }

    if(!Tool::JsonHelper::TryGetString(config,"configType",configType) || configType != "resource"){
        LOG_ERROR("RESOURCE MATERIAL","configType Error: " + configFile);
        return false;
    }

    if(!Tool::JsonHelper::TryGetObject(config,"resource",resource)){
        LOG_ERROR("RESOURCE MATERIAL","'resource' object is not exit, in " + configFile);
        return false;
    }

    if(!Tool::JsonHelper::TryGetString(*resource, "resourceType", resourceType) || resourceType != "material"){
        LOG_ERROR("RESOURCE MATERIAL","'resourceType' is Error, in " + configFile);
        return false;
    }

   if(!Tool::JsonHelper::TryGetObject(*resource, "material", materialConfig)){
        LOG_ERROR("RESOURCE MATERIAL","'materialConfig' object is not exit, in " + configFile);
        return false;
    }

    if(!Tool::JsonHelper::TryGetString(*materialConfig, "materialType",materialType)){
        LOG_ERROR("RESOURCE MATERIAL","'materialType' Error, in " + configFile);
        flag = false;
    }

    MaterialBluePrintFunction function = GetMaterialBluePrintTypeFunction(materialType);
    if(!function){
        LOG_ERROR("RESOURCE MATERIAL","No Registered MaterialBluePrint have this materialType: " + materialType + ", in " + configFile);
        flag = false;
    }
    

    if(!Tool::JsonHelper::TryGetObject(*materialConfig, "args", args)){
        LOG_ERROR("RESOURCE MATERIAL","'resource' object is not exit: " + configFile);
        return false;
    }

    if(!Tool::JsonHelper::TryGetObject(*args, "textures", textures)){
        LOG_ERROR("RESOURCE MATERIAL","'textures' object is not exit: " + configFile);
        flag = false;
    }

    if(!Tool::JsonHelper::TryGetObject(*args, "properties", properties)){
        LOG_ERROR("RESOURCE MATERIAL","'properties' object is not exit: " + configFile);
        flag = false;
    }

    if(!Tool::JsonHelper::TryGetObject(*args, "shaders", shaders)){
        LOG_ERROR("RESOURCE MATERIAL","'shaders' object is not exit: " + configFile);
        flag = false;
    }

    if(flag){
        if(!function(material,textures,properties,shaders)){
            LOG_ERROR("RESOURCE MATERIAL", "MaterialBluePrint Function fail to load this material" + configFile);
            return false;
        }
    }

    return true;

}