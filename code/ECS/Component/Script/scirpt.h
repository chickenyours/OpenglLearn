#pragma once

#include <functional>

#include "code/ECS/Component/component.h"

#include "code/ECS/data_type.h"

#include "code/Script/script_interface.h"

#include "code/Script/script_manager.h"

namespace ECS::Component{
    struct Script : public Component<Script>{
        IScript* scriptInsterface = nullptr;
        int state = 0;

        bool LoadFromMetaDataImpl(const Json::Value& data, Log::StackLogErrorHandle errHandle = nullptr) {
            std::string scriptName;
            if(!Tool::JsonHelper::TryGetString(data,"name",scriptName)){
                REPORT_STACK_ERROR(errHandle,"Component:Script->LoadFromMetaDataImpl","Cannot load Name");
                return false;
            }
            
            scriptInsterface = ScriptManager::Instance().GetScriptObject(scriptName);
            
            if(!scriptInsterface){
                REPORT_STACK_ERROR(errHandle,"Component:Script->LoadFromMetaDataImpl","Cannot load script interface object: " + scriptName);
                return false;
            }

            return true;
        }



    };
    
}