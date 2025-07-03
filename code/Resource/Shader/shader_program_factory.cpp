#include "shader_program_factory.h"

#include "code/Resource/Shader/shader_manager.h"

#include "code/ToolAndAlgorithm/Json/json_helper.h"
#include "code/ToolAndAlgorithm/Json/json_config_identification.h"

#include "code/DebugTool/ConsoleHelp/color_log.h"

#include "code/Resource/Shader/shader.h"

#include "code/ToolAndAlgorithm/Hash/md5.h"

using namespace Resource;

bool ShaderProgramFactory::LoadFromConfigFile(const std::string& configFile, Log::StackLogErrorHandle errHandle) {
    Json::Value config;
    if (!Tool::JsonHelper::LoadJsonValueFromFile(configFile, config, errHandle)) {
        REPORT_STACK_ERROR(errHandle, "ShaderProgram", "fail to load file: " + configFile);
        return false;
    }

    configFilePath_ = configFile;

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

    auto loadShaderFactory = [](const Json::Value* shaderJson, const std::string& name, Log::StackLogErrorHandle errHandle)->Resource::ResourceHandle<ShaderFactory> {
        if(!shaderJson){
            REPORT_STACK_ERROR(errHandle, "ShaderProgramFactory->loadShaderFactory", name + " shader config not found");
            return nullptr;
        }

        std::string shaderFilePath;
        if(!Tool::JsonHelper::TryGetString(*shaderJson, "filePath", shaderFilePath)){
            REPORT_STACK_ERROR(errHandle, "ShaderProgramFactory->loadShaderFactory", "missing 'filePath' in shader config, type" + name);
            return nullptr;
        }

        auto shaderFactory = Resource::ShaderManager::GetInstance().GetShaderFactoryFromShaderFile(shaderFilePath,errHandle);

        if(!shaderFactory){
            REPORT_STACK_ERROR(errHandle, "ShaderProgramFactory->loadShaderFactory", "failed to load shader factory from file, type: " + name + ",in " + shaderFilePath);
            return nullptr;
        }

        return shaderFactory;
    };
    auto loadShaderLocalDescription = [](const Json::Value* shaderJson, Log::StackLogErrorHandle errHandle) -> ShaderDescription {
        ShaderDescription description = {};
        if(shaderJson){
            const Json::Value* macrosArray = nullptr;
            if (!Tool::JsonHelper::TryGetArray(*shaderJson, "macros", macrosArray)) {
                REPORT_STACK_ERROR(errHandle, "ShaderProgramFactory->LocalDescription", "missing or invalid 'macros' array in shader config");
            }
            else{
                if (!Tool::JsonHelper::TryTraverseArray(*macrosArray, description.macros)) {
                    REPORT_STACK_ERROR(errHandle, "ShaderProgramFactory->LocalDescription", "failed to parse 'macros' array");
                }
            }
        }
        return description;
    };






    // 解析 shaderType_
    shaderType_ = ShaderProgramType::UNKNOWN;
    if (vs && fs) shaderType_ = gs ? ShaderProgramType::VERTEX_GEOMETRY_FRAGMENT : ShaderProgramType::VERTEX_FRAGMENT;
    else if (cs) shaderType_ = ShaderProgramType::COMPUTE;

    // 分支处理
    if (shaderType_ == ShaderProgramType::VERTEX_FRAGMENT) {
        auto v = loadShaderFactory(vs, "vertex", errHandle);
        if (!v) {
            REPORT_STACK_ERROR(errHandle, "ShaderProgramFactory", "Failed to load vertex shader");
            return false;
        }
        auto f = loadShaderFactory(fs, "fragment", errHandle);
        if (!f) {
            REPORT_STACK_ERROR(errHandle, "ShaderProgramFactory", "Failed to load fragment shader");
            return false;
        }

        shaderFactories_.emplace(ShaderStage::Vertex,std::move(v));
        shaderFactories_.emplace(ShaderStage::Fragment,std::move(f));

        localShaderDescriptions_.emplace(ShaderStage::Vertex,loadShaderLocalDescription(vs,errHandle)); 
        localShaderDescriptions_.emplace(ShaderStage::Fragment,loadShaderLocalDescription(fs,errHandle)); 

        // if(!linkProgram({v->GetShaderID(), f->GetShaderID()},errHandle)){
        //     REPORT_STACK_ERROR(errHandle, "ShaderProgram", "Failed to link vertex and fragment shaders");
        //     return false;
        // }
    } 
    else if (shaderType_ == ShaderProgramType::VERTEX_GEOMETRY_FRAGMENT) {
        auto v = loadShaderFactory(vs, "vertex", errHandle);
        if (!v) {
            REPORT_STACK_ERROR(errHandle, "ShaderProgramFactory", "Failed to load vertex shader");
            return false;
        }

        auto g = loadShaderFactory(gs, "geometry", errHandle);
        if (!g) {
            REPORT_STACK_ERROR(errHandle, "ShaderProgramFactory", "Failed to load geometry shader");
            return false;
        }

        auto f = loadShaderFactory(fs, "fragment", errHandle);
        if (!f) {
            REPORT_STACK_ERROR(errHandle, "ShaderProgramFactory", "Failed to load fragment shader");
            return false;
        }
        shaderFactories_.emplace(ShaderStage::Vertex,std::move(v));
        shaderFactories_.emplace(ShaderStage::Fragment,std::move(f));
        shaderFactories_.emplace(ShaderStage::Geometry,std::move(g));

        localShaderDescriptions_.emplace(ShaderStage::Vertex,loadShaderLocalDescription(vs,errHandle)); 
        localShaderDescriptions_.emplace(ShaderStage::Fragment,loadShaderLocalDescription(fs,errHandle)); 
        localShaderDescriptions_.emplace(ShaderStage::Geometry,loadShaderLocalDescription(gs,errHandle)); 
        // if(!linkProgram({v->GetShaderID(), g->GetShaderID(), f->GetShaderID()}, errHandle)){
        //     REPORT_STACK_ERROR(errHandle, "ShaderProgram", "Failed to link vertex, geometry, and fragment shaders");
        //     return false;
        // }
    } 
    else if (shaderType_ == ShaderProgramType::COMPUTE) {
        // TODO: compute shader 支持
        REPORT_STACK_ERROR(errHandle, "ShaderProgramFactory", "Compute shader not implemented");
        return false;
    }
    else{
        REPORT_STACK_ERROR(errHandle, "ShaderProgramFactory", "Unknown shader program type: " + configFile);
        return false;
    }

    isLoad_ = true;
    return true;
}

ResourceHandle<ShaderProgram> ShaderProgramFactory::GetShaderProgramInstance(const std::unordered_map<ShaderStage, ShaderDescription>& shaderDescriptions, Log::StackLogErrorHandle errHandle){

    if(!isLoad_){
        REPORT_STACK_ERROR(errHandle, "ShaderProgramFactory->GetShaderProgramInstance", "Shader program is not loaded");
        return nullptr;
    }


    if(shaderType_ == ShaderProgramType::VERTEX_FRAGMENT || shaderType_ == ShaderProgramType::VERTEX_GEOMETRY_FRAGMENT){



        auto vertex = shaderFactories_.find(ShaderStage::Vertex);
        if(vertex == shaderFactories_.end()){
            REPORT_STACK_ERROR(errHandle, "ShaderProgramFactory->GetShaderProgramInstance", "Vertex shader factory not found");
            return nullptr;
        }

        auto fragment = shaderFactories_.find(ShaderStage::Fragment);
        if(fragment == shaderFactories_.end()){
            REPORT_STACK_ERROR(errHandle, "ShaderProgramFactory->GetShaderProgramInstance", "Fragment shader factory not found");
            return nullptr;
        }

        auto vertexDescription = localShaderDescriptions_.find(ShaderStage::Vertex);
        if(vertexDescription == localShaderDescriptions_.end()){
            REPORT_STACK_ERROR(errHandle, "ShaderProgramFactory->GetShaderProgramInstance", "Vertex shader description not found");
            return nullptr;
        }

        auto fragmentDescription = localShaderDescriptions_.find(ShaderStage::Fragment);
        if(fragmentDescription == localShaderDescriptions_.end()){
            REPORT_STACK_ERROR(errHandle, "ShaderProgramFactory->GetShaderProgramInstance", "Fragment shader description not found");
            return nullptr;
        }
        
        auto vertexDescriptionFromOutside = shaderDescriptions.find(ShaderStage::Vertex);
        auto vertexShader = vertex->second->TryGetShaderInstance(
            vertexDescriptionFromOutside != shaderDescriptions.end() ? 
            vertexDescription->second + vertexDescriptionFromOutside->second : vertexDescription->second,
            errHandle
        );
        if(!vertexShader){
            REPORT_STACK_ERROR(errHandle, "ShaderProgramFactory->GetShaderProgramInstance", "Failed to create vertex shader instance");
            return nullptr;
        }

        auto fragmentDescriptionFromOutside = shaderDescriptions.find(ShaderStage::Fragment);
        auto fragmentShader = fragment->second->TryGetShaderInstance(
            fragmentDescriptionFromOutside != shaderDescriptions.end() ?
            fragmentDescription->second + fragmentDescriptionFromOutside->second : fragmentDescription->second,
            errHandle);
        if(!fragmentShader){
            REPORT_STACK_ERROR(errHandle, "ShaderProgramFactory->GetShaderProgramInstance", "Failed to create fragment shader instance");
            return nullptr;
        }

        ResourceHandle<Shader> geometryShader;
        if(shaderType_ == ShaderProgramType::VERTEX_GEOMETRY_FRAGMENT){
            auto geometry = shaderFactories_.find(ShaderStage::Geometry);
            if (geometry == shaderFactories_.end()) {
                REPORT_STACK_ERROR(errHandle, "ShaderProgramFactory->GetShaderProgramInstance", "Geometry shader factory not found");
                return nullptr;
            }

            auto geometryDescription = localShaderDescriptions_.find(ShaderStage::Geometry);
            if (geometryDescription == localShaderDescriptions_.end()) {
                REPORT_STACK_ERROR(errHandle, "ShaderProgramFactory->GetShaderProgramInstance", "Geometry shader description not found");
                return nullptr;
            }

            auto geometryDescriptionFromOutside = shaderDescriptions.find(ShaderStage::Geometry);
            geometryShader = geometry->second->TryGetShaderInstance(
                geometryDescriptionFromOutside != shaderDescriptions.end() ? 
                geometryDescription->second + geometryDescriptionFromOutside->second : geometryDescription->second,
                errHandle
            );
            if (!geometryShader) {
                REPORT_STACK_ERROR(errHandle, "ShaderProgramFactory->GetShaderProgramInstance", "Failed to create geometry shader instance");
                return nullptr;
            }
        }

        std::string shaderProgramResourceKey;
        if(shaderType_ == ShaderProgramType::VERTEX_GEOMETRY_FRAGMENT || shaderType_ == ShaderProgramType::VERTEX_FRAGMENT){   
            MD5 md5;
            std::string combinedString = vertexShader.GetName() + fragmentShader.GetName();
            if(shaderType_ == ShaderProgramType::VERTEX_GEOMETRY_FRAGMENT){
                combinedString += geometryShader.GetName();
            }
            shaderProgramResourceKey = "ShaderProgram:"+ configFilePath_ + ":" + md5(combinedString);
        }

    
        auto program = ECS::Core::ResourceModule::ResourceManager::GetInctance().Get(
            FromGenerator<ShaderProgram>(
                shaderProgramResourceKey,
                [&](Log::StackLogErrorHandle errhandle)->std::unique_ptr<ShaderProgram> {
                    auto instance = std::make_unique<ShaderProgram>();
                    GLuint program = glCreateProgram();
                    glAttachShader(program,vertexShader->GetShaderID());
                    glAttachShader(program,fragmentShader->GetShaderID());
                    if(shaderType_ == ShaderProgramType::VERTEX_GEOMETRY_FRAGMENT){
                        glAttachShader(program,geometryShader->GetShaderID());
                    }
                    glLinkProgram(program);

                    GLint success;
                    char infoLog[1024];
                    glGetProgramiv(program, GL_LINK_STATUS, &success);
                    if (!success) {
                        glGetProgramInfoLog(program, 1024, nullptr, infoLog);
                        REPORT_STACK_ERROR(errHandle, "ShaderProgramFactory->GetShaderProgramInstance", std::string("Link error: ") + infoLog);
                        glDeleteProgram(program);
                        return nullptr;
                    }

                    instance->id_ = program;
                    return instance;
                }
            ));
            return program;
        }
    return nullptr;
}

void ShaderProgramFactory::Release(){
    localShaderDescriptions_.clear();
    shaderFactories_.clear();
    isLoad_ = false;
}