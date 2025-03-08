#version 330 core


layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 lightPos;

out vec3 Normal;
out vec3 Pos;
out vec3 lightViewPos;

void main(){
    gl_Position = projection * view * model * vec4(aPos,1.0);
    Normal = normalize(mat3(transpose(inverse(view*model)))*aNormal);
    Pos  = vec3(view * model * vec4(aPos,1.0));
    lightViewPos = vec3(view*vec4(lightPos,1.0));
}