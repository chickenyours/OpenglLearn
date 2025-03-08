#version 330 core 

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTex;
layout(location = 2) in vec3 aNormal;

out vec2 TexCoords;
out vec3 FragPos;
out vec3 Normal;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;

void main(){
    vec4 Pos =  model * vec4(aPos,1.0);
    gl_Position = projection*view * Pos;
    FragPos = Pos.xyz;
    TexCoords = aTex;
    Normal = normalize(mat3(transpose(inverse(model))) * aNormal);
}