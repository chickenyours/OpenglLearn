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


namespace Resource {
    class ShaderManager;
    class ShaderFactory;

    // 由ShaderManager系统管理
    class Shader : public ILoadable{
        public:
            Shader() = default;
            inline GLuint GetShaderID(){return shaderID_;}
            virtual void Release() override;
            ~Shader();
        private:
            GLuint shaderID_ = 0;
            friend class ShaderManager; 
            friend class ShaderFactory;
    };
}