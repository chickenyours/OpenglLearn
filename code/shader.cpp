#include "shader.h"
#include <iostream>
#include <fstream>
#include <cstring>

using namespace Render;

Shader::Shader(int shaderType,std::string filePath) : _shaderType(shaderType){
    //read shaderSource from file
    std::string shaderPath = filePath;
        _name = shaderPath;
        std::ifstream infile(shaderPath,std::ios::binary);
        if (infile.is_open())
        {
            infile.seekg(0, std::ios::end);
            std::streampos file_size = infile.tellg();
            infile.seekg(0, std::ios::beg);
            char *shaderSource = new char[static_cast<int>(file_size)+1];
            infile.read(shaderSource, file_size);
            infile.close();
            shaderSource[static_cast<int>(file_size)] = '\0';
            _shaderID = glCreateShader(shaderType); 
            glShaderSource(_shaderID, 1, &shaderSource, NULL);
            glCompileShader(_shaderID);
            int success;
            char infolog[1024];
            glGetShaderiv(_shaderID, GL_COMPILE_STATUS, &success);
            if (!success)
            {
                glGetShaderInfoLog(_shaderID, 1024, NULL, infolog);
                std::cerr << "[ERROR] [Shader] Compilation failed for shader file: " << filePath << "\n" << infolog << std::endl;
                _shaderID=-1;
            }
            delete[] shaderSource;
        }
        else
        {
            std::cerr << "[ERROR] [Shader] Failed to open shader file: " << shaderPath << std::endl;
            return;
        }
    }
Shader::~Shader(){
    if(_shaderID)
    {
        glDeleteShader(_shaderID);
    }
}

unsigned int Shader::getShaderID()
{
    return _shaderID;
}
unsigned int Shader::getShaderType(){
    return  _shaderType;
}

void ShaderProgram::Load(Shader* vertexShader,Shader* fragShader,Shader* geometryShader){
    if(vertexShader->getShaderType() == GL_VERTEX_SHADER && fragShader->getShaderType() == GL_FRAGMENT_SHADER
        && (!geometryShader || geometryShader && geometryShader->getShaderType() == GL_GEOMETRY_SHADER)){
        _name = vertexShader->GetName() + "&" + fragShader->GetName();
        _shaderProgramID =glCreateProgram();
        glAttachShader(_shaderProgramID,vertexShader->getShaderID());
        if(geometryShader){
            glAttachShader(_shaderProgramID,geometryShader->getShaderID());
            _name += "&" + geometryShader->GetName();
        }
        glAttachShader(_shaderProgramID,fragShader->getShaderID());
        glLinkProgram(_shaderProgramID);
        int success;
        char infolog[1024];
        glGetProgramiv(_shaderProgramID, GL_LINK_STATUS, &success);
        if (!success){
            glGetProgramInfoLog(_shaderProgramID, 1024, NULL, infolog);
            std::cerr << "[ERROR] [ShaderProgram] link error: "<<_shaderProgramID<<":"<<infolog<<std::endl;
        }
        std::cout << "[INFO] [ShaderProgram] Load a ShaderProgram " << _name << std::endl;
    }
    else{
        std::cerr <<"[ERROR] [ShaderProgram] ShaderProgram can't be created because of type error:";
    }
}

void ShaderProgram::Print(int tabs){
    std::string tab = "";
    for(int i = 0; i< tabs; i++){
        tab += "\t";
    }

    std::cout << tab <<"======ShaderProgramInfo======"<<std::endl;

    std::cout << tab << "Name: " << _name <<std::endl;
    std::cout << tab << "API_ID: " << getShaderProgramID() << std::endl; 
    std::cout << tab <<"======EndShaderProgramInfo======"<<std::endl;
}

ShaderProgram::ShaderProgram(Shader* vertexShader,Shader* fragShader,Shader* geometryShader){
    Load(vertexShader,fragShader,geometryShader);
}

ShaderProgram::ShaderProgram(std::string vertexShaderPath,std::string fragShaderPath,std::string geometryShaderPath){
    Shader vertexShader = Shader(GL_VERTEX_SHADER,vertexShaderPath);
    Shader fragShader = Shader(GL_FRAGMENT_SHADER,fragShaderPath);
    if(geometryShaderPath != ""){
        Shader geometryShader = Shader(GL_GEOMETRY_SHADER,geometryShaderPath);
        Load(&vertexShader,&fragShader,&geometryShader);
    }
    else{
        Load(&vertexShader,&fragShader);
    }
}

ShaderProgram::~ShaderProgram(){
    glDeleteProgram(_shaderProgramID);
    std::cout<< "destroy ShaderPrograme: "<<_name<<std::endl;
}
void ShaderProgram::Use(){
    glUseProgram(_shaderProgramID);
}
int ShaderProgram::getShaderProgramID(){
    return _shaderProgramID;
}
