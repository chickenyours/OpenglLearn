#pragma once

/*
{
    "name" : "hh"
    "configType": "resourceRegister",
    "resource": {
        "resourceType": "shader",
        "shaderType": "BPR",
        "shaderConfig": {
            "shaderFilePath" : "path/to/BPRShader"
        }
    }
  }
*/

/*
{
    "configType": "resource",
    "resource": {
        "resourceType": "shader",
        "isClass" : true,
        "shaderType": "BPR",
        "macros": [
            "NOT_NEED_NORMAL_MAP",
            "NOT_NEED_ROUGHNESS_MAP",
            "NOT_NEED_METALLIC_MAP"
        ]
    }
}
*/

/*
{
    "configType": "resource",
    "resource": {
        "resourceType": "shader",
        "isClass" : false,
        "filePath": "./path/to/shader_file",
        "shaderType": "vertex shader",
        "macros": {
            "NOT_NEED_NORMAL_MAP",
            "NOT_NEED_ROUGHNESS_MAP",
            "NOT_NEED_METALLIC_MAP"
        }
    }
}
*/

#include <string>
#include <glad/glad.h>
#include <unordered_map>
#include "code/ECS/Core/Resource/resource_interface.h"
#include "code/ECS/Core/Resource/resource_handle.h"

namespace Resource {

    class Shader;

    class ShaderFactory : public ILoadFromConfig {
        public:
            bool LoadFromConfigFile(const std::string& configFile) override;
            bool LoadShaderCodeFromFile(unsigned int shaderType, const std::string& filePath);
            void AddMacro(const std::string& value);
            void ClearMacro();
            void ReleaseCodeCache();
            void GenerateFinalShaderCode(std::string& out);

            bool GenerateShader(std::string& errorMsg, Shader& out);
            bool GenerateShaderToResourceManager(std::string& errorMsg, Resource::ResourceHandle<Shader>& out);
            
            void Release() override;
            inline unsigned int GetShaderType(){return shaderType_;}
            
            void Print();
        private:
            
            std::string source;
            std::string codeFilePath_;
            unsigned int shaderType_;
            std::vector<std::string> macroCache_;
            std::string codeCache_;
    };
}