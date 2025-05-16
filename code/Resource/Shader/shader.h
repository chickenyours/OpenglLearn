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
#include "code/ECS/Core/Resource/resource.h"

namespace Resource {

    class Shader : public AbstractResource {
        public:
            bool LoadFromConfigFile(const std::string& configFile) override;
            bool LoadShaderCodeFromFile(unsigned int shaderType, const std::string& filePath);
            void AddMacro(const std::string& value);
            void ClearMacro();
            void ReleaseCodeCache();
            // std::string& GenerateFinalShaderCode();
            bool GenerateShader(std::string& errorMsg);
            void ReleaseShader();
            void Release() override;
            inline GLuint GetID(){return shaderID_;}
            inline unsigned int GetShaderType(){return shaderType_;}
            
            void Print();
        private:

            GLuint shaderID_ = 0;
            unsigned int shaderType_;
            
            bool isClass = false;

            std::vector<std::string> macroCache_;
            std::string codeCache_;
    };
}