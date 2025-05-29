#pragma once

#include "code/ECS/Core/Resource/resource_interface.h"
#include "code/ECS/Core/Resource/resource_handle.h"
#include <string>

namespace Resource{

    class ShaderFactory;
    class Shader;

    class ShaderManager{
        public:
            static ShaderManager& GetInstance();

            // 获取工厂
            ResourceHandle<ShaderFactory> GetShaderFactoryFromConfigFile(const std::string& configFile);

            ResourceHandle<ShaderFactory> GetShaderFactoryFromSahderFile(unsigned int shaderType, const std::string& shaderFile);

            // 获取shader
            

            // 未被外部资源管理的实例,但制造它的工厂会被管理
            bool GetShader(unsigned int shaderType, const std::string& filePath, Shader& out);


    };
}