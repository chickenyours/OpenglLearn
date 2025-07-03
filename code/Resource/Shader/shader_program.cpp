#include "shader_program.h"

using namespace Resource;

void ShaderProgram::Release(){
    if(id_ != 0){
        glDeleteProgram(id_);
        Log::Info("ShaderProgram", "Shader program deleted successfully. id: " + id_);
        id_ = 0;
    }
}