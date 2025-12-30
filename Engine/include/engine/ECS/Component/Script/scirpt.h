#pragma once

#include <functional>

#include "engine/ECS/Component/component.h"

#include "engine/ECS/data_type.h"

#include "engine/Script/script_interface.h"

#include "engine/Script/script_manager.h"

namespace ECS::Component{
    struct Script : public Component<Script>{
        IScript* scriptInterface = nullptr;
        int state = 0;

        bool LoadFromMetaDataImpl(const Json::Value& data, Log::StackLogErrorHandle errHandle = nullptr) {
            std::string scriptName;
            if(!Tool::JsonHelper::TryGetString(data,"name",scriptName)){
                REPORT_STACK_ERROR(errHandle,"Component:Script->LoadFromMetaDataImpl","Cannot load Name");
                return false;
            }
            
            scriptInterface = ScriptManager::Instance().GetScriptObject(scriptName);
            
            if(!scriptInterface){
                REPORT_STACK_ERROR(errHandle,"Component:Script->LoadFromMetaDataImpl","Cannot load script interface object: " + scriptName);
                return false;
            }

            return true;
        }



    };
    
}