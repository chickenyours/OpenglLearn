#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "code/ECS/Component/component.h"
#include "code/Resource/Material/material.h"

#include "code/Resource/RenderPipe/UniformBindings.h"


namespace ECS::Component{
    
    struct MeshRenderer : Component<MeshRenderer>
    {
        Render::StaticModelComponentDataUBOLayout uboData;

        // GLuint VAO;
        std::vector<Resource::ResourceHandle<Material>> materialList;

        bool LoadFromMetaDataImpl(const Json::Value& data, Log::StackLogErrorHandle errHandle = nullptr) {
            
            const Json::Value* materialFilePathArray;
            if(!Tool::JsonHelper::TryGetArray(data,"material",materialFilePathArray)){
                REPORT_STACK_ERROR(errHandle, "MeshRenderer->LoadFromMetaData", "Failed to load material file path.");
                return false;
            }

            std::vector<std::string> materialFilePath;
            if(!Tool::JsonHelper::TryTraverseArray(*materialFilePathArray, materialFilePath)){
                REPORT_STACK_ERROR(errHandle, "MeshRenderer->LoadFromMetaData", "Failed to traverse material file path array.");
                return false;
            }

            for(const std::string& filePath : materialFilePath){

                auto material = ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<Material>(
                    FromConfig<Material>(filePath)
                );
    
                if(!material){
                    REPORT_STACK_ERROR(errHandle, "MeshRenderer->LoadFromMetaData", "Failed to load material resource.");
                    return false;
                }

                materialList.push_back(std::move(material));

            }

            

            

            return true;
        }
    };
    

}

