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

#include <vector>
#include <string>
#include <glad/glad.h>
#include <unordered_map>
#include "code/ECS/Core/Resource/resource_interface.h"
#include "code/ECS/Core/Resource/resource_handle.h"
#include "code/ECS/Core/Resource/resource_load_option.h"

#include "code/Resource/Shader/shader_description.h"

#include "code/DebugTool/ConsoleHelp/color_log.h"

namespace Resource {

    class Shader;

    

    class ShaderFactory : public ILoadable {
        public:
            static std::string GetShaderResourceKey(std::string filePath, const ShaderDescription& despription);
            // 如果返回值为true,说明加载成功,则着色器对象会被注册到资源管理器,out会被资源管理器返回的句柄覆盖
            // 返回false,errorMsg会被错误报告日志覆盖,其他什么也没有发生
            ResourceHandle<Shader> TryGetShaderInstance(const ShaderDescription& description, Log::StackLogErrorHandle errHandle = nullptr);
            // void GenerateFinalShaderCode(std::string& out);
            void Release() override;
            inline unsigned int GetShaderType(){return shaderType_;}
            void ReleaseCodeCache();
            inline void SetCodeFilePath(const std::string& shaderFilePath){
                if(shaderFilePath.empty()) return;
                if(isCodeLoaded_){
                    ReleaseCodeCache();
                }
                codeFilePath_ = shaderFilePath;
            }
            std::string GetCodeFilePath(){ return codeFilePath_; }
            void Print();
        private:
            bool LoadShaderCode(Log::StackLogErrorHandle errHandle = nullptr);
            bool isCodeLoaded_ = false;
            unsigned int shaderType_;
            std::string codeFilePath_;
            std::string codeCache_;


    };
}