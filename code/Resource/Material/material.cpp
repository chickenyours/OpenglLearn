#include "material.h"

#include "code/DebugTool/ConsoleHelp/color_log.h"

#include "code/Graphic/Opengl/ubo_binding_config.h"

#include "code/Resource/Material/material_manager.h"

#include "code/Resource/Texture/texture.h"
// #include "code/Resource/Shader/shader_program.h"



using namespace Resource;

bool Material::LoadFromConfigFile(const std::string& configFile){
    if(!MaterialManager::GetInstance()->LoadMaterialFromConfigFile(configFile, *this)){
        LOG_ERROR("Resource Material", "Fail to load material from configFile");
        return false;
    }
    isLoaded_ = true;
    return true;
}

void Material::Release(){
    LOG_INFO("Resource Material", "Release material: " + name_);
    isLoaded_ = false;
}

void Material::BindTexture(GLuint textureID, size_t index){

}

void Material::BindShader(GLuint shaderID, size_t index){

}

void Material::UploadPropertyData(size_t start, size_t size, const void* data){

}

Material::~Material() {
    if (propertybuffer_) {
        delete[] propertybuffer_;
    }
}

