#pragma once

#include "code/ToolAndAlgorithm/Json/json_helper.h"

namespace Tool{



inline bool TryToExtractResourceObject(
    const Json::Value& configRootObject, 
    const Json::Value*& out, 
    Log::StackLogErrorHandle errHandle = nullptr){
        std::string configType;
        if(!Tool::JsonHelper::TryGetString(configRootObject,"configType",configType)){
            REPORT_STACK_ERROR(errHandle, "Json Config Indentification", "Failed to extract 'configType' from the JSON object.");
            return false;
        }

        if(configType != "resource"){
            REPORT_STACK_ERROR(errHandle, "Json Config Indentification", "The 'configType' is not 'resource'.");
            return false;
        }

        const Json::Value* resource;
        if(!Tool::JsonHelper::TryGetObject(configRootObject,"resource",resource)){
            REPORT_STACK_ERROR(errHandle, "Json Config Indentification", "Failed to extract 'resource' object from the JSON object.");
            return false;
        }

        out = resource;
        return true;
}

} // namespace Tool