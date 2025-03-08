#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoords;
layout (location = 2) in vec3 aNormal;

out VS_OUT {
    vec3 FragPos;
    vec2 TexCoords;
    vec3 Normal;
    vec4 LightFragPos;
    vec3 viewPos;
} vs_out;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpaceMatrix;

void main(){
    vec4 Pos = model * vec4(aPos,1.0);
    gl_Position = projection * view * Pos;
    vs_out.FragPos = Pos.xyz;
    vs_out.TexCoords = aTexCoords;
    mat3 normalMatrix = mat3(transpose(inverse(model)));
    vs_out.Normal = normalize(vec3(vec4(normalMatrix * aNormal, 0.0)));
    vs_out.LightFragPos = lightSpaceMatrix * Pos;
    vs_out.viewPos = vec3(view[3][0], view[3][1], view[3][2]);
}