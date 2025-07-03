#pragma once

#include <string>
#include <unordered_map>
#include <json/json.h>

#include "code/ECS/Core/Resource/resource_interface.h"
#include "code/ECS/Core/Resource/resource_handle.h"
#include "code/Resource/Material/material.h"
#include "code/Resource/Material/material_interface.h"
#include "code/DebugTool/ConsoleHelp/color_log.h"


namespace Resource{

class MaterialInterfaceFactory {
    public: 
        static MaterialInterfaceFactory& Instance();
        bool GetMaterialFromFile(const Json::Value& mataData, IMaterial& out, const Log::StackLogErrorHandle errHandle = nullptr);
};

}