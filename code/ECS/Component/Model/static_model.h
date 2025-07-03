#pragma once

#include <vector>
#include "code/ECS/Component/component.h"
#include "code/Resource/Model/model.h"

namespace ECS::Component{

struct StaticModel : public Component<StaticModel>{
    Resource::ResourceHandle<Model> model; 

    bool LoadFromMetaDataImpl(const Json::Value& data, Log::StackLogErrorHandle errHandle = nullptr) {
        std::string modelFilePath;

        if(!Tool::JsonHelper::TryGetString(data, "modelFilePath", modelFilePath)){
            REPORT_STACK_ERROR(errHandle, "StaticModel->LoadFromMetaData", "Failed to load model from path: " + modelFilePath);
            return false;
        }

        model = ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<Model>(
            FromConfig<Model>(
                modelFilePath
            )
        );

        if(!model){
            REPORT_STACK_ERROR(errHandle, "StaticModel->LoadFromMetaData", "Failed to load model resource from path: " + modelFilePath);
            return false;
        }

        return true;
    }
};

} // namespace Resource