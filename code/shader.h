#pragma once
#include <string>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Render{
    class Shader{
        private:
            unsigned int _shaderType;
            unsigned int _shaderID;
            std::string _name;
        public:
            Shader(int shaderType,std::string filePath);
            unsigned int getShaderID();
            unsigned int getShaderType();
            inline std::string GetName(){return _name;}
            ~Shader();
    };
    
    class ShaderProgram{
        private:
            unsigned int _shaderProgramID;
            void Load(Shader* vertexShader,Shader* fragShader,Shader* geometryShader = nullptr);
            std::string _name;
        public:
            ShaderProgram(Shader* vertexShader,Shader* fragShader,Shader* geometryShader = nullptr);
            ShaderProgram(std::string vertexShaderPath,std::string fragShaderPath,std::string geometryShaderPath = "");
            ~ShaderProgram();
            inline std::string GetName(){return _name;}
            void Use();
            int getShaderProgramID();
            void Print(int tabs = 0);
    };
    
    inline void ShaderU1i(ShaderProgram& program,const std::string& uniform,int v){
        glUniform1i(glGetUniformLocation(program.getShaderProgramID(),uniform.c_str()),v);
    }
    
    inline void ShaderU1f(ShaderProgram& program,const std::string& uniform,float v){
        glUniform1f(glGetUniformLocation(program.getShaderProgramID(),uniform.c_str()),v);
    }
    
    inline void ShaderUmatf4(ShaderProgram& program,const std::string& uniform,const glm::mat4 v){
        glUniformMatrix4fv(glGetUniformLocation(program.getShaderProgramID(),uniform.c_str()),1,GL_FALSE,glm::value_ptr(v));
    }
    
    inline void ShaderUvec3(ShaderProgram& program,const std::string& uniform,glm::vec3 v){
        glUniform3f(glGetUniformLocation(program.getShaderProgramID(),uniform.c_str()),v[0],v[1],v[2]);
    }
    
    inline void ShaderUvec4(ShaderProgram& program,const std::string& uniform,glm::vec4 v){
        glUniform4f(glGetUniformLocation(program.getShaderProgramID(),uniform.c_str()),v[0],v[1],v[2],v[3]);
    }
    
    inline void ShaderUvec2(ShaderProgram& program,const std::string& uniform,glm::vec2 v){
        glUniform2f(glGetUniformLocation(program.getShaderProgramID(),uniform.c_str()),v[0],v[1]);
    }
    
    inline void ShaderUb(ShaderProgram& program,const std::string& uniform, bool v){
        glUniform1i(glGetUniformLocation(program.getShaderProgramID(),uniform.c_str()),v ? GL_TRUE:GL_FALSE);
    }
    
    inline void ShaderUB(ShaderProgram& program,const std::string& blockName,int point){
        glUniformBlockBinding(program.getShaderProgramID(),glGetUniformBlockIndex(program.getShaderProgramID(),blockName.c_str()),point);
    }
    
}

