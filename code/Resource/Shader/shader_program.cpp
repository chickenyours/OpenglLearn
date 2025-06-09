#include "shader_program.h"

#include "code/Resource/Shader/shader_manager.h"

#include "code/ToolAndAlgorithm/Json/json_helper.h"
#include "code/ToolAndAlgorithm/Json/json_config_identification.h"

#include "code/DebugTool/ConsoleHelp/color_log.h"

#include "code/Resource/Shader/shader.h"

using namespace Resource;

bool ShaderProgram::LoadFromConfigFile(const std::string& configFile, Log::StackLogErrorHandle errHandle) {
    Json::Value config;
    if (!Tool::JsonHelper::LoadJsonValueFromFile(configFile, config, errHandle)) {
        REPORT_STACK_ERROR(errHandle, "ShaderProgram", "fail to load file: " + configFile);
        return false;
    }

    const Json::Value* resource = nullptr;
    if (!Tool::TryToExtractResourceObject(config, resource, errHandle)) {
        REPORT_STACK_ERROR(errHandle, "ShaderProgram", "fail to extract resource object: " + configFile);
        return false;
    }

    std::string resourceType;
    if (!Tool::JsonHelper::TryGetString(*resource, "resourceType", resourceType)) {
        REPORT_STACK_ERROR(errHandle, "ShaderProgram", "fail to get 'resourceType': " + configFile);
        return false;
    }
    if (resourceType != "shader program") {
        REPORT_STACK_ERROR(errHandle, "ShaderProgram", "resourceType is not 'shader program': " + configFile);
        return false;
    }

    const Json::Value* args = nullptr;
    if (!Tool::JsonHelper::TryGetObject(*resource, "args", args)) {
        REPORT_STACK_ERROR(errHandle, "ShaderProgram", "fail to get 'args' object: " + configFile);
        return false;
    }

    const Json::Value* vs = nullptr;
    const Json::Value* fs = nullptr;
    const Json::Value* gs = nullptr;
    const Json::Value* cs = nullptr;
    Tool::JsonHelper::TryGetObject(*args, "vertexShader", vs);
    Tool::JsonHelper::TryGetObject(*args, "fragmentShader", fs);
    Tool::JsonHelper::TryGetObject(*args, "geometryShader", gs);
    Tool::JsonHelper::TryGetObject(*args, "computeShader", cs);

    struct ShaderConfig{
        std::string filePath;
        ShaderDescription description;
    };

    auto parseShaderConfig = [](const Json::Value& config, ShaderConfig& out, Log::StackLogErrorHandle errHandle) -> bool {
        if (!Tool::JsonHelper::TryGetString(config, "filePath", out.filePath)) {
            REPORT_STACK_ERROR(errHandle, "ShaderProgram->ParseShaderConfig", "missing 'filePath' in shader config");
            return false;
        }
        const Json::Value* macrosArray = nullptr;
        if (!Tool::JsonHelper::TryGetArray(config, "macros", macrosArray)) {
            REPORT_STACK_ERROR(errHandle, "ShaderProgram->ParseShaderConfig", "missing or invalid 'macros' array in shader config");
            return false;
        }
        if (!Tool::JsonHelper::TryTraverseArray(*macrosArray, out.description.macros)) {
            REPORT_STACK_ERROR(errHandle, "ShaderProgram->ParseShaderConfig", "failed to parse 'macros' array");
            return false;
        }
        return true;
    };

    auto loadShader = [parseShaderConfig](const Json::Value* shaderJson, const std::string& name, Log::StackLogErrorHandle errHandle) -> Resource::ResourceHandle<Shader> {
        if (!shaderJson) {
            REPORT_STACK_ERROR(errHandle, "ShaderProgram->LoadShader", name + " shader config not found");
            return nullptr;
        }
        ShaderConfig cfg;
        if (!parseShaderConfig(*shaderJson, cfg, errHandle)) {
            REPORT_STACK_ERROR(errHandle, "ShaderProgram->LoadShader", "failed to parse " + name + " shader config");
            return nullptr;
        }
        auto shader = ShaderManager::GetInstance().GetShaderFromShaderFile(cfg.filePath, cfg.description, errHandle);
        if (!shader) {
            REPORT_STACK_ERROR(errHandle, "ShaderProgram->LoadShader", "failed to load " + name + " shader from file");
        }
        return shader;
    };

    auto linkProgram = [this](const std::vector<GLuint>& shaders, Log::StackLogErrorHandle errHandle) -> bool {
        GLuint program = glCreateProgram();
        for (GLuint sid : shaders) glAttachShader(program, sid);
        glLinkProgram(program);

        GLint success;
        char infoLog[1024];
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(program, 1024, nullptr, infoLog);
            REPORT_STACK_ERROR(errHandle, "ShaderProgram->LinkingShaderProgram", std::string("link error: ") + infoLog);
            return false;
        }

        this->id_ = program;
        return true;
    };

    // 解析 shaderType_
    shaderType_ = ShaderProgramType::UNKNOWN;
    if (vs && fs) shaderType_ = gs ? ShaderProgramType::VERTEX_GEOMETRY_FRAGMENT : ShaderProgramType::VERTEX_FRAGMENT;
    else if (cs) shaderType_ = ShaderProgramType::COMPUTE;

    // 分支处理
    if (shaderType_ == ShaderProgramType::VERTEX_FRAGMENT) {
        auto v = loadShader(vs, "vertex", errHandle);
        if (!v) {
            REPORT_STACK_ERROR(errHandle, "ShaderProgram", "Failed to load vertex shader");
            return false;
        }
        auto f = loadShader(fs, "fragment", errHandle);
        if (!f) {
            REPORT_STACK_ERROR(errHandle, "ShaderProgram", "Failed to load fragment shader");
            return false;
        }
        if(!linkProgram({v->GetShaderID(), f->GetShaderID()},errHandle)){
            REPORT_STACK_ERROR(errHandle, "ShaderProgram", "Failed to link vertex and fragment shaders");
            return false;
        }
    } 
    else if (shaderType_ == ShaderProgramType::VERTEX_GEOMETRY_FRAGMENT) {
        auto v = loadShader(vs, "vertex", errHandle);
        auto g = loadShader(gs, "geometry", errHandle);
        auto f = loadShader(fs, "fragment", errHandle);
        if (!v || !g || !f) {
            REPORT_STACK_ERROR(errHandle, "ShaderProgram", "Failed to load vertex, geometry, or fragment shader");
            return false;
        }
        if(!linkProgram({v->GetShaderID(), g->GetShaderID(), f->GetShaderID()}, errHandle)){
            REPORT_STACK_ERROR(errHandle, "ShaderProgram", "Failed to link vertex, geometry, and fragment shaders");
            return false;
        }
    } 
    else if (shaderType_ == ShaderProgramType::COMPUTE) {
        // TODO: compute shader 支持
        REPORT_STACK_ERROR(errHandle, "ShaderProgram", "Compute shader not implemented");
        return false;
    }
    else{
        REPORT_STACK_ERROR(errHandle, "ShaderProgram", "Unknown shader program type: " + configFile);
        return false;
    }

    isLoad_ = true;
    return true;
}

void ShaderProgram::Release(){
    if (id_ != 0) {
        glDeleteProgram(id_);
        LOG_INFO("Shader Program", std::string("Shader program with ID ") + LOG_COLOR_BRIGHT_MAGENTA + std::to_string(id_) + LOG_INFO_COLOR +" has been deleted.");
        id_ = 0;
    }
    isLoad_ = false;
}