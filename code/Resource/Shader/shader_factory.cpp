#include "shader_factory.h"

#include <sstream>

#include "code/ToolAndAlgorithm/Json/json_helper.h"
#include "code/ToolAndAlgorithm/File/file_loader.h"
#include "code/DebugTool/ConsoleHelp/color_log.h"

#include "code/Resource/Shader/shader.h"

using namespace Resource;

bool ShaderFactory::LoadFromConfigFile(const std::string& configFile){

    source = configFile;

    Json::Value config;
    std::string configType;
    const Json::Value* resource;
    std::string resourceType;
    bool isClass;
    const Json::Value* macros;
    

    if(!Tool::JsonHelper::LoadJsonValueFromFile(configFile,config)){
        LOG_ERROR("Resource Shader","Error configFile");
        return false;
    }

    if(!Tool::JsonHelper::TryGetString(config, "configType", configType) && configType != "resource"){
        LOG_ERROR("Resource Shader","Error configType");
        return false;
    }

    if(!Tool::JsonHelper::TryGetObject(config, "resource", resource)){
        LOG_ERROR("Resource Shader","Error resource");
        return false;
    }

    if(!Tool::JsonHelper::TryGetString(*resource, "resourceType", resourceType) && resourceType != "shader"){
        LOG_ERROR("Resource Shader","Error resourceType");
        return false;
    }

    if(!Tool::JsonHelper::TryGetBool(*resource,"isClass", isClass)){
        LOG_ERROR("Resource Shader","Error isClass");
        return false;
    }

    if(!Tool::JsonHelper::TryGetArray(*resource,"macros", macros)){
        LOG_ERROR("Resource Shader","Error macros");
        return false;
    }


    if(isClass){
        // 注册机制
    }
    else{

        std::string filePath;
        std::string shaderTypeString;
        unsigned int shaderType;

        if(!Tool::JsonHelper::TryGetString(*resource, "filePath", filePath)){
            LOG_ERROR("Resource Shader","filePath is not exist");
            return false;
        }

        codeFilePath_ = filePath;

        if(!Tool::JsonHelper::TryGetString(*resource, "shaderType", shaderTypeString)){
            LOG_ERROR("Resource Shader","shaderType is not exist");
            return false;
        }

        // 翻译类型
            if(shaderTypeString == "vertex shader") shaderType = GL_VERTEX_SHADER;
            else if(shaderTypeString == "fragment shader") shaderType = GL_FRAGMENT_SHADER;
            else if(shaderTypeString == "geometry shader") shaderType = GL_GEOMETRY_SHADER;
            else if(shaderTypeString == "compute shader") shaderType = GL_COMPUTE_SHADER;
            else {
                LOG_WARNING("Resource Shader", "Unknown shaderType: " + shaderTypeString);
                shaderType = GL_INVALID_ENUM;
            }

        // 直接文件加载
        if(!LoadShaderCodeFromFile(shaderType, filePath)){
            LOG_ERROR("Resource Shader","File cannot be opened");
            return false;
        }
    }


    // 载入宏
    if(!Tool::JsonHelper::TryTraverseArray(*macros, macroCache_)){
        LOG_ERROR("Resource Shader","Appearing unexpect type in macro array");
            return false;
    }

    // std::string errorMsg;
    // if(!GenerateShader(errorMsg)){
    //     LOG_ERROR("Resource Shader","Fail to generate shader: " + errorMsg);
    //     return false;
    // }

    isLoad_ = true;
    return true;

}

bool ShaderFactory::LoadShaderCodeFromFile(unsigned int shaderType, const std::string& filePath){
    if(source.empty()) source = filePath;
    codeFilePath_ = filePath;
    if(!Tool::FileLoader::LoadFileToString(filePath, codeCache_)){
        LOG_ERROR("Resource ShaderFactory", "Failed to load shader code from file: " + filePath);
        return false;
    }
    shaderType_ = shaderType;
    isLoad_ = true;
    return true;
}

void ShaderFactory::AddMacro(const std::string& value){
    macroCache_.push_back(value);
}
void ShaderFactory::ClearMacro(){
    macroCache_.clear();
}
void ShaderFactory::ReleaseCodeCache(){
    codeCache_.clear();
}

bool ShaderFactory::GenerateShader(std::string& errorMsg, Shader& out){
    GLuint shaderID = glCreateShader(shaderType_); 
    if(shaderID == 0){   //创建失败
        GLenum error = glGetError();
        if (error == GL_INVALID_ENUM) {
            errorMsg = "glCreateShader: Invalid Shader Type";
        } else if (error == GL_INVALID_OPERATION) {
            errorMsg = "No valid OpenGL Context found";
        } else if (error == GL_OUT_OF_MEMORY) {
            errorMsg = "Out of memory, unable to allocate shader";
        } else {
            errorMsg = "Unknown error occurred";
        }
        return false;
    }

    if(codeCache_.empty()){
        errorMsg = "Shader code cache is empty";
        glDeleteShader(shaderID); // ✨ 释放分配的 Shader 资源
        return false;
    }

    std::vector<std::string> sourceStrings;
    std::vector<const GLchar*> shaderSources;

    // 假设 codeCache_ 是 std::string，包含完整的 shader 代码

    std::string::size_type versionEnd = codeCache_.find('\n');
    std::string versionLine = codeCache_.substr(0, versionEnd);
    std::string remainingCode = codeCache_.substr(versionEnd + 1);

    // 加入 version 行
    sourceStrings.push_back(versionLine + "\n");

    // 加入宏定义
    for (const auto& macro : macroCache_) {
        sourceStrings.push_back("#define " + macro + "\n");
    }

    // 加入剩余代码
    sourceStrings.push_back(remainingCode);

    // 构建最终指针数组
    std::vector<const GLchar*> rawPointers;
    rawPointers.reserve(sourceStrings.size());
    for (const auto& str : sourceStrings) {
        rawPointers.push_back(str.c_str());
    }

    // 编译 Shader
    glShaderSource(shaderID, rawPointers.size(), rawPointers.data(), nullptr);
    glCompileShader(shaderID);

    int success;
    char infolog[1024];
    glGetShaderiv(shaderID, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(shaderID, 1024, NULL, infolog);
        errorMsg = "Shader compilation failed:\n" + std::string(infolog);
        glDeleteShader(shaderID); // ✨ 编译失败时释放资源
        return false;
    }

    if(!out.shaderID_){
        glDeleteShader(out.shaderID_);
    }

    out.shaderID_ = shaderID;

    return true;

}

void ShaderFactory::Release(){
    LOG_INFO("Resource ShaderFactory", "Release ShaderFactory, source: " + source);
    ClearMacro();
    ReleaseCodeCache();
}

void ShaderFactory::Print(){
    if(!isLoad_){
        LOG_ERROR("Resource ShaderFactory", "Print fail because the object is not loaded");
        return;
    }
    std::stringstream ss;
    ss  << "\n===========Shader Info==========\n"
        << "source file path: " + source + '\n'
        << "shaderType: " << 
            [this]() -> std::string {
                if(shaderType_ == GL_VERTEX_SHADER) return "vertex shader";
                else if(shaderType_ == GL_FRAGMENT_SHADER) return "fragment shader";
                else if(shaderType_ == GL_GEOMETRY_SHADER) return "geometry shader";
                else if(shaderType_ == GL_COMPUTE_SHADER) return "compute shader";
                else return "unknown";
            }() << '\n'
        << "macros: " << '\n'
            <<  [this]() -> std::string {
                    std::stringstream ss;
                    for(auto& it : macroCache_){
                        ss << '\t' << it << '\n';
                    }
                    return ss.str();
                }()
        << "code file path: " + codeFilePath_ << "\n"
        << "code: " << "\n\t"
            << [this]() -> std::string {
                size_t size = std::min(codeCache_.size(), static_cast<size_t>(512));
                std::string out;
                out.reserve(size * 2 + 5);
                for(size_t i = 0; i < size; i++){
                    char c = codeCache_[i];
                    out.push_back(c);
                    if(c == '\n'){
                        out.push_back('\t');
                    }
                }
                if(size != codeCache_.size()){
                    out += "\n\t...";
                }
                return out;
            }()
        << "\n================================\n";
    LOG_INFO("Resource ShaderFactory", ss.str());
}   

void ShaderFactory::GenerateFinalShaderCode(std::string& out) {
    if(!isLoad_) return;

    out.clear();  // 清空旧内容

    // 拆分 codeCache_ 中的 #version 行和剩余代码
    std::string::size_type versionEnd = codeCache_.find('\n');
    std::string versionLine = (versionEnd != std::string::npos)
        ? codeCache_.substr(0, versionEnd)
        : "#version 460 core"; // fallback

    std::string remainingCode = (versionEnd != std::string::npos)
        ? codeCache_.substr(versionEnd + 1)
        : "";

    // 加入 #version 行
    out += versionLine + "\n";

    // 加入宏定义
    for (const auto& macro : macroCache_) {
        out += "#define " + macro + "\n";
    }

    // 加入主代码体
    out += remainingCode;
}
