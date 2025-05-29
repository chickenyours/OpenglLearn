#include "shader.h"

#include "code/Resource/Shader/shader_manager.h"
#include "code/DebugTool/ConsoleHelp/color_log.h"

using namespace Resource;


void Shader::Release(){
    if(shaderID_){
        glDeleteShader(shaderID_);
        LOG_INFO("Resource Shader", "successfuly delete shaderID " + std::to_string(shaderID_));
        shaderID_ = 0;
    }
}

Shader::~Shader(){
    Release();
}