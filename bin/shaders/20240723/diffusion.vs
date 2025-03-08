#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection; 

out vec3 Normal;
out vec3 Pos;

void main(){
    gl_Position =  projection * view * model * vec4(aPos,1.0);
    Normal =  normalize(mat3(transpose(inverse(model))) * aNormal);
    Pos = vec3(model * vec4(aPos,1.0));
}