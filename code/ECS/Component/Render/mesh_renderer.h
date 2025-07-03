#pragma once
#include "code/ECS/Component/component.h"


#include "code/Resource/Material/material.h"


namespace ECS::Component{
    
    struct MeshRenderer : Component<MeshRenderer>
    {
        // GLuint VAO;
        Resource::ResourceHandle<Material> material;

        bool LoadFromMetaDataImpl(const Json::Value& data, Log::StackLogErrorHandle errHandle = nullptr) {
            
            std::string materialFilePath;
            if(!Tool::JsonHelper::TryGetString(data,"material",materialFilePath)){
                REPORT_STACK_ERROR(errHandle, "MeshRenderer->LoadFromMetaData", "Failed to load material file path.");
                return false;
            }

            material = ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<Material>(
                FromConfig<Material>(materialFilePath)
            );

            if(!material){
                REPORT_STACK_ERROR(errHandle, "MeshRenderer->LoadFromMetaData", "Failed to load material resource.");
                return false;
            }
            

            

            return true;
        }
    };
    

}

