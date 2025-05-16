#include "shader.h"

#include <sstream>

#include "code/ToolAndAlgorithm/Json/json_helper.h"
#include "code/ToolAndAlgorithm/File/file_loader.h"
#include "code/DebugTool/ConsoleHelp/color_log.h"

using namespace Resource;

bool Shader::LoadFromConfigFile(const std::string& configFile){
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
        LOG_ERROR("Resource Shader","Error isClass");
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
                shaderType = NULL;
            }

        // 直接文件加载
        if(!LoadShaderCodeFromFile(shaderType, filePath)){
            LOG_ERROR("Resource Shader","File cannot be opened");
            return false;
        }
    }

    // 载入宏
    if(!Tool::JsonHelper::TryTraverseArray(macros, macroCache_)){
        LOG_ERROR("Resource Shader","Appearing unexpect type in macro array");
            return false;
    }

    return true;

}

bool Shader::LoadShaderCodeFromFile(unsigned int shaderType, const std::string& filePath){
    if(!Tool::FileLoader::LoadFileToString(filePath, codeCache_)){
        return false;
    }
    shaderType_ = shaderType;
}

void Shader::AddMacro(const std::string& value){
    macroCache_.push_back(value);
}
void Shader::ClearMacro(){
    macroCache_.clear();
}
void Shader::ReleaseCodeCache(){
    codeCache_.clear();
}

bool Shader::GenerateShader(std::string& errorMsg){
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

    // 构建 Shader 源码的多段数组
    std::vector<const GLchar*> shaderSources;

    // 添加宏定义
    for (const auto& macro : macroCache_) {
        std::string defineStr = "#define " + macro + "\n";
        shaderSources.push_back(strdup(defineStr.c_str()));
    }

    // 添加实际的 Shader 代码
    shaderSources.push_back(codeCache_.c_str());

    glShaderSource(shaderID, shaderSources.size(), shaderSources.data(), nullptr);
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

    // 清理之前的 ShaderID 防止内存泄漏
    if (shaderID_ != 0) {
        glDeleteShader(shaderID_);
    }

    shaderID_ = shaderID;
    return true;

}

void Shader::ReleaseShader(){
    if (shaderID_ != 0) {
        glDeleteShader(shaderID_);
        shaderID_ = 0;
    }
}

void Shader::Release(){
    ReleaseShader();
    ClearMacro();
    ReleaseCodeCache();
}

void Shader::Print(){
    std::stringstream ss;
    ss  << '\n'
        << "ShaderID: " << shaderID_ << '\n'
        << "shaderType: " << 
            [this]() -> std::string {
                if(shaderType_ == GL_VERTEX_SHADER) return "vertex shader";
                else if(shaderType_ == GL_FRAGMENT_SHADER) return "fragment shader";
                else if(shaderType_ == GL_GEOMETRY_SHADER) return "geometry shader";
                else if(shaderType_ == GL_COMPUTE_SHADER) return "compute shader";
                else return "unknown";
            }() << '\n'
        << "isClass: " << isClass << '\n'
        << "macros: " << '\n'
            <<  [this]() -> std::string {
                    std::stringstream ss;
                    for(auto& it : macroCache_){
                        ss << '\t' << it << '\n';
                    }
                    return ss.str();
                }()
        << "code: " << '\n'
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
            }();
}   