#include "material.h"

#include "code/ToolAndAlgorithm/Json/json_config_identification.h"
#include "code/Resource/Material/material_interface_loader_registry.h"

using namespace Resource;


bool Material::LoadFromConfigFile(const std::string& configFile, Log::StackLogErrorHandle errHandle){
    Json::Value config;
    if(!Tool::JsonHelper::LoadJsonValueFromFile(configFile,config,errHandle)){
        REPORT_STACK_ERROR(errHandle,"Material->LoadFromConfigFile", "fail to load configFile, " + configFile);
        return false;
    }

    const Json::Value* resource;
    if(!Tool::TryToExtractResourceObject(config,resource,errHandle)){
        REPORT_STACK_ERROR(errHandle, "Material->LoadFromConfigFile", "fail to extract resource object from configFile, " + configFile);
        return false;
    }

    std::string resourceType;
    if(!Tool::JsonHelper::TryGetString(*resource,"resourceType",resourceType)){
        REPORT_STACK_ERROR(errHandle, "Material->LoadFromConfigFile", "missing or invalid 'resourceType' in configFile, " + configFile);
        return false;
    }

    if(resourceType != "material"){
        REPORT_STACK_ERROR(errHandle, "Material->LoadFromConfigFile", "'resourceType' is not 'material' in configFile, " + configFile);
        return false;
    }

    const Json::Value* material;
    if(!Tool::JsonHelper::TryGetObject(*resource,"material",material)){
        REPORT_STACK_ERROR(errHandle, "Material->LoadFromConfigFile", "missing or invalid 'material' object in configFile, " + configFile);
        return false;
    }

    const Json::Value* interfaces;
    if(!Tool::JsonHelper::TryGetArray(*material,"interfaces",interfaces)){
        REPORT_STACK_ERROR(errHandle, "Material->LoadFromConfigFile", "missing or invalid 'interfaces' array in configFile, " + configFile);
        return false;
    }


    std::vector<const Json::Value*> interfacesArray;
    if(!Tool::JsonHelper::TryTraverseArray(*interfaces,interfacesArray)){
        REPORT_STACK_ERROR(errHandle, "Material->LoadFromConfigFile", "failed to traverse 'interfaces' array in configFile, " + configFile);
        return false;
    }

    for(const Json::Value* it : interfacesArray){
        std::string type;
        if(!Tool::JsonHelper::TryGetString(*it,"type",type)){
            LOG_ERROR("material", "fail to load interface because type is not exist" + std::string(", in ") + configFile);
            continue;
        }
        auto loadermap = GetIMaterialLoaderMap();
        auto loader = loadermap.find(type);
        if(loader == loadermap.end()){
            LOG_ERROR("material", "no loader found for interface type: " + type + ", in " + configFile);
            continue;
        }

        const Json::Value* args;
        if(!Tool::JsonHelper::TryGetObject(*it,"args",args)){
            LOG_ERROR("material", "missing or invalid 'args' object for interface type: " + type + ", in " + configFile);
            continue;
        }

        // renderMode

        const Json::Value* shaderPrograms;
        const Json::Value* textures;
        const Json::Value* properties;
        const Json::Value* state;

        Tool::JsonHelper::TryGetObject(*args, "shaderPrograms", shaderPrograms, Json::Value());
        Tool::JsonHelper::TryGetObject(*args, "textures", textures, Json::Value());
        Tool::JsonHelper::TryGetObject(*args, "properties", properties, Json::Value());
        Tool::JsonHelper::TryGetObject(*args, "state", state, Json::Value());

        std::unique_ptr<IMaterial> materialinterface = loader->second();

        if(!materialinterface->LoadFromMataData(
            *textures, *shaderPrograms, *properties, *state, errHandle
        )){
            LOG_ERROR("material", "failed to load material interface for type: " + type);
            continue;
        }

        auto typemap = GetIMaterialTypeMap();
        auto interfaceType = typemap.find(type);
        if(interfaceType == typemap.end()){
            LOG_ERROR("material", "no interface type found for: " + type);
            continue;
        }

        features[interfaceType->second] = std::move(materialinterface);
        
    }

    return true;
    
}


void Material::Release(){

}

