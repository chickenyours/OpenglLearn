#pragma once

#include <string>
#include <glad/glad.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>


inline void ShaderU1i(GLuint program,const std::string& uniform, int v){
        glUniform1i(glGetUniformLocation(program ,uniform.c_str()),v);
    }
    
inline void ShaderU1f(GLuint program,const std::string& uniform, float v){
    glUniform1f(glGetUniformLocation(program,uniform.c_str()),v);
}

inline void ShaderUmatf4(GLuint program, const std::string& uniform, const glm::mat4& v){
    glUniformMatrix4fv(glGetUniformLocation(program ,uniform.c_str()), 1, GL_FALSE,glm::value_ptr(v));
}

inline void ShaderUvec3(GLuint program ,const std::string& uniform, const glm::vec3& v){
    glUniform3f(glGetUniformLocation(program, uniform.c_str()),v[0],v[1], v[2]);
}

inline void ShaderUvec4(GLuint program ,const std::string& uniform, const glm::vec4& v){
    glUniform4f(glGetUniformLocation(program, uniform.c_str()),v[0],v[1],v[2],v[3]);
}

inline void ShaderUvec2(GLuint program ,const std::string& uniform, const glm::vec2& v){
    glUniform2f(glGetUniformLocation(program, uniform.c_str()),v[0],v[1]);
}
    