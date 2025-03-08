#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoords;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent;

out VS_OUT {
    vec3 FragPos;
    vec2 TexCoords;
    vec4 LightFragPos;
    //vec3 Normal;
    mat3 TBN;
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
    vs_out.LightFragPos = lightSpaceMatrix * Pos;

    //计算TBN
    vec3 T = normalize(vec3(model * vec4(aTangent,   0.0)));
    vec3 B = normalize(vec3(model * vec4(aBitangent, 0.0)));
    vec3 N = normalize(vec3(model * vec4(aNormal,    0.0)));
    vs_out.TBN = mat3(T,B,N); 
    
    // mat3 normalMatrix = mat3(transpose(inverse(model)));
    // vs_out.Normal = normalize(vec3(vec4(normalMatrix * aNormal, 0.0)));
}