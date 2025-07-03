#pragma once

#include <json/json.h>
#include "code/DebugTool/ConsoleHelp/color_log.h"
#include "code/ToolAndAlgorithm/Json/json_helper.h"


#include "code/ECS/Core/Resource/resource_handle.h"

namespace ECS::Component{

template <typename Derived>
struct Component
{
    bool LoadFromMataData(const Json::Value& data, Log::StackLogErrorHandle errHandle = nullptr){
        if(!static_cast<Derived*>(this)->LoadFromMetaDataImpl(data, errHandle)){
            REPORT_STACK_ERROR(errHandle, "Component->LoadFromMataData", "Failed to load metadata");
            return false;
        }
        return true;
    }
};
    
}