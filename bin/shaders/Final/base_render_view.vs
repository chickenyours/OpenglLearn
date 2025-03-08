#version 330 core 

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTex;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in vec3 aTangent;

out vec3 FragPos;
out vec2 TexCoords;
out vec3 Normal;
out vec3 Tangent;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;

void main(){
    vec4 Pos =  view * model * vec4(aPos,1.0);
    gl_Position = projection * Pos;
    FragPos = Pos.xyz;
    TexCoords = aTex;
    mat3 normalMatrix = transpose(inverse(mat3(view * model)));
    Normal = normalMatrix * aNormal;
    Tangent = normalMatrix * aTangent;
}