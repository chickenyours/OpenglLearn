#include "shader_factory.h"

#include <sstream>
#include <functional>

#include "code/ToolAndAlgorithm/Json/json_helper.h"
#include "code/ToolAndAlgorithm/File/file_loader.h"
#include "code/DebugTool/ConsoleHelp/color_log.h"

#include "code/Resource/Shader/shader.h"

#include "code/ToolAndAlgorithm/Hash/md5.h"

using namespace Resource;

bool ShaderFactory::LoadShaderCode(Log::StackLogErrorHandle errHandle){
    if(codeFilePath_.empty()){
        return false;
    }
    if(!Tool::FileLoader::LoadFileToString(codeFilePath_, codeCache_)){
        REPORT_STACK_ERROR(errHandle,"Shader Factory", "Failed to load shader code from file: " + codeFilePath_);
        return false;
    }

    // 推导 shaderType_ 根据文件后缀
    std::string::size_type dotPos = codeFilePath_.find_last_of('.');
    if (dotPos != std::string::npos) {
        std::string extension = codeFilePath_.substr(dotPos + 1);
        if (extension == "vs") shaderType_ = GL_VERTEX_SHADER;
        else if (extension == "fs") shaderType_ = GL_FRAGMENT_SHADER;
        else if (extension == "gs") shaderType_ = GL_GEOMETRY_SHADER;
        else if (extension == "cs") shaderType_ = GL_COMPUTE_SHADER;
        else {
            LOG_WARNING("Resource ShaderFactory", "Unknown shader file extension: " + extension);
            shaderType_ = GL_INVALID_ENUM;
        }
    } else {
        LOG_WARNING("Resource ShaderFactory", "File path does not have an extension: " + codeFilePath_);
        shaderType_ = GL_INVALID_ENUM;
    }

    isCodeLoaded_ = true;
    return true;
}


void ShaderFactory::ReleaseCodeCache(){
    codeCache_.clear();
    this->isCodeLoaded_ = false;
}

std::string ShaderFactory::GetShaderResourceKey(std::string filePath, const ShaderDescription& despription){
    // 将宏进行字典排序
    std::vector<std::string> sortedMacros = despription.macros;
    std::sort(sortedMacros.begin(), sortedMacros.end());

    // 拼接 filePath 和排序后的宏
    std::string combinedString = filePath;
    for (const auto& macro : sortedMacros) {
        combinedString += macro;
    }

    // 计算 MD5 值并返回
    MD5 md5;
    // std::cout<< md5(combinedString) << std::endl; 
    return "ShaderInstance:" + filePath + ":" + md5(combinedString);
    
}

ResourceHandle<Shader> ShaderFactory::TryGetShaderInstance(const ShaderDescription& description, Log::StackLogErrorHandle errHandle){

    // 变体解析
    if(codeFilePath_.empty()){
        REPORT_STACK_ERROR(errHandle,"Shader Factory", "Shader file path is empty.");
        return nullptr;
    }

    std::string shaderResourceKey = GetShaderResourceKey(codeFilePath_,description);

    Log::StackLogErrorHandle nouse(nullptr); // 需要改进!!!
    auto shader = ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<Shader>(ECS::Core::ResourceModule::FromKey<Shader>(shaderResourceKey), nouse);
    // 在ResourceManager中找寻是否有存在的shader
    if(shader){
        return std::move(shader);
    }
    
    if(!isCodeLoaded_ && !LoadShaderCode()){
        REPORT_STACK_ERROR(errHandle,"Shader Factory", "Failed to load shader code");
        return nullptr;
    }

    GLuint shaderID = glCreateShader(shaderType_); 
    if(shaderID == 0){   //创建失败
        GLenum error = glGetError();
        if (error == GL_INVALID_ENUM) {
            REPORT_STACK_ERROR(errHandle,"Shader Factory", "glCreateShader: Invalid Shader Type");
        } else if (error == GL_INVALID_OPERATION) {
            REPORT_STACK_ERROR(errHandle, "Shader Factory", "glCreateShader: No valid OpenGL Context found");
        } else if (error == GL_OUT_OF_MEMORY) {
            REPORT_STACK_ERROR(errHandle, "Shader Factory", "glCreateShader: Out of memory, unable to allocate shader");
        } else {
            REPORT_STACK_ERROR(errHandle, "Shader Factory", "glCreateShader: Unknown error occurred");
        }
        return nullptr;
    }

    if(codeCache_.empty()){
        glDeleteShader(shaderID); // ✨ 释放分配的 Shader 资源
        REPORT_STACK_ERROR(errHandle, "Shader Factory", "Shader code cache is empty");
        return nullptr;
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
    for (const auto& macro : description.macros) {
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

    // for(auto c : rawPointers){
    //     std::cout << c;
    // }
    // std::cout << std::endl;

    // 编译 Shader
    glShaderSource(shaderID, rawPointers.size(), rawPointers.data(), nullptr);
    glCompileShader(shaderID);

    int success;
    char infolog[1024];
    glGetShaderiv(shaderID, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(shaderID, 1024, NULL, infolog);
        REPORT_STACK_ERROR(errHandle, "ShaderFactory", "Shader compilation failed: " + std::string(infolog));
        glDeleteShader(shaderID); // ✨ 编译失败时释放资源
        return nullptr;
    }

    // 放入ResourceManager,并返回句柄
    shader = ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<Shader>(
        ECS::Core::ResourceModule::FromGenerator<Shader>(
            shaderResourceKey,
            [shaderID](Log::StackLogErrorHandle errHandle) -> std::unique_ptr<Shader> {
                auto shaderInstance = std::make_unique<Shader>();
                shaderInstance->shaderID_ = shaderID;
                shaderInstance->isLoad_ = true;
                return shaderInstance;
            },
            [](const std::string& key, Shader* resource){
                
            }
        )
        ,errHandle
    );

    // std::cout<<codeFilePath_ << std::endl;
    // std::cout<<shader->shaderID_ << std::endl;

    return std::move(shader);
}

void ShaderFactory::Release(){
    LOG_INFO("Resource ShaderFactory", "Release ShaderFactory, codeFilePath: " + codeFilePath_);
    ReleaseCodeCache();
}

void ShaderFactory::Print(){
    if(!isCodeLoaded_){
        LOG_ERROR("Resource ShaderFactory", "Print fail because the object is not loaded");
        return;
    }
    std::stringstream ss;
    ss  << "\n===========Shader Info==========\n"
        << "code file path: " + codeFilePath_ << "\n"
        << "shaderType: " << 
            [this]() -> std::string {
                if(shaderType_ == GL_VERTEX_SHADER) return "vertex shader";
                else if(shaderType_ == GL_FRAGMENT_SHADER) return "fragment shader";
                else if(shaderType_ == GL_GEOMETRY_SHADER) return "geometry shader";
                else if(shaderType_ == GL_COMPUTE_SHADER) return "compute shader";
                else return "unknown";
            }() << '\n'
        << "code: " << "\n\t"
            << [this]() -> std::string {
                if(!isCodeLoaded_ && !LoadShaderCode()){
                    return "Failed to load shader code.";
                }
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

