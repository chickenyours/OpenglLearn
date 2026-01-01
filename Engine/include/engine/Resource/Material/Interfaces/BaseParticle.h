#pragma once

#include "engine/Resource/Material/material_interface.h"

namespace Resource{

class IBaseParticle : public IMaterial{
    public:
        ResourceHandle<ShaderProgram> mainShader;

        bool LoadFromMataData(
            const Json::Value& textures,
            const Json::Value& shaderPrograms,
            const Json::Value& properties,
            const Json::Value& states,
            Log::StackLogErrorHandle errHandle
        ) override {
            std::string mainShaderProgramPath;
            if(!Tool::JsonHelper::TryGetString(shaderPrograms, "mainShaderProgram", mainShaderProgramPath)){
                REPORT_STACK_ERROR(errHandle, "Material:BPR", "Failed to load mainShaderProgram from metadata.");
                return false;
            }

            mainShader = std::move(ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<ShaderProgramFactory>(
                FromConfig<ShaderProgramFactory>(mainShaderProgramPath))->GetShaderProgramInstance());
            
            if(!mainShader){
                REPORT_STACK_ERROR(errHandle, "Material:BPR", "Failed to load mainShaderProgram resource.");
                return false;
            }
            return true;
        }
};

}