#pragma once



#include <glad/glad.h>
#include <string>
#include <vector>
#include "code/ECS/Core/Resource/resource_interface.h"
#include "code/Resource/Shader/shader_description.h"
#include "code/ECS/Core/Resource/resource_handle.h"
#include "code/Resource/Shader/shader_factory.h"
#include "code/Resource/Shader/shader_program.h"

#include "code/Resource/Shader/shader_type.h"

namespace Resource{

    class ShaderProgramFactory : public ILoadFromConfig{
        public:
            bool LoadFromConfigFile(const std::string& configFile, Log::StackLogErrorHandle errHandle = nullptr) override;
            ResourceHandle<ShaderProgram> GetShaderProgramInstance(const std::unordered_map<ShaderStage, ShaderDescription>& shaderDescriptions = {}, Log::StackLogErrorHandle errHandle = nullptr);
            virtual void Release() override;
            inline ShaderProgramType GetShaderType() const { return shaderType_; }
        private:
            std::unordered_map<ShaderStage, ShaderDescription> localShaderDescriptions_;
            std::unordered_map<ShaderStage, ResourceHandle<ShaderFactory>> shaderFactories_;
            ShaderProgramType shaderType_;
            std::string configFilePath_;
    };
}