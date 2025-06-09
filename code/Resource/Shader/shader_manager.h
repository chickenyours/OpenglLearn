#pragma once

#include <string>
#include "code/ECS/Core/Resource/resource_interface.h"
#include "code/ECS/Core/Resource/resource_handle.h"

#include "code/Resource/Shader/shader_description.h"

#include "code/DebugTool/ConsoleHelp/color_log.h"

namespace Resource{

    class ShaderFactory;
    class Shader;

    class ShaderDescription;

    class ShaderManager{
        public:
            static ShaderManager& GetInstance();
            // 获取工厂
            ResourceHandle<ShaderFactory> GetShaderFactoryFromShaderFile(const std::string& shaderFile, Log::StackLogErrorHandle errHandle = nullptr);
            // 获取Sahder
            ResourceHandle<Shader> GetShaderFromShaderFile(const std::string& shaderFile,  const ShaderDescription& description = {}, Log::StackLogErrorHandle errHandle = nullptr);
        private:
    };
}