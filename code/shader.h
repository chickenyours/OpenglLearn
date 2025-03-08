#pragma once
#include <string>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

using std::string;

class Shader{
    private:
        unsigned int _shaderType;
        unsigned int _shaderID;
    public:
        static const string SHADER_BASE_PATH;
        Shader(int shaderType,string filePath);
        unsigned int getShaderID();
        unsigned int getShaderType();
        ~Shader();
};

class ShaderProgram{
    private:
        unsigned int _shaderProgramID;
        void Load(Shader* vertexShader,Shader* fragShader,Shader* geometryShader = nullptr);
    public:
        ShaderProgram(Shader* vertexShader,Shader* fragShader,Shader* geometryShader = nullptr);
        ShaderProgram(string vertexShaderPath,string fragShaderPath,string geometryShaderPath = "");
        ~ShaderProgram();
        void Use();
        int getShaderProgramID();
};

inline void ShaderU1i(ShaderProgram& program,const string& uniform,int v){
    glUniform1i(glGetUniformLocation(program.getShaderProgramID(),uniform.c_str()),v);
}

inline void ShaderU1f(ShaderProgram& program,const string& uniform,float v){
    glUniform1f(glGetUniformLocation(program.getShaderProgramID(),uniform.c_str()),v);
}

inline void ShaderUmatf4(ShaderProgram& program,const string& uniform,const glm::mat4 v){
    glUniformMatrix4fv(glGetUniformLocation(program.getShaderProgramID(),uniform.c_str()),1,GL_FALSE,glm::value_ptr(v));
}

inline void ShaderUvec3(ShaderProgram& program,const string& uniform,glm::vec3 v){
    glUniform3f(glGetUniformLocation(program.getShaderProgramID(),uniform.c_str()),v[0],v[1],v[2]);
}

inline void ShaderUvec4(ShaderProgram& program,const string& uniform,glm::vec4 v){
    glUniform4f(glGetUniformLocation(program.getShaderProgramID(),uniform.c_str()),v[0],v[1],v[2],v[3]);
}

inline void ShaderUvec2(ShaderProgram& program,const string& uniform,glm::vec2 v){
    glUniform2f(glGetUniformLocation(program.getShaderProgramID(),uniform.c_str()),v[0],v[1]);
}

inline void ShaderUB(ShaderProgram& program,const string& blockName,int point){
    glUniformBlockBinding(program.getShaderProgramID(),glGetUniformBlockIndex(program.getShaderProgramID(),blockName.c_str()),point);
}

