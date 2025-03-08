#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 viewPos;
uniform vec3 lightPos;
uniform vec3 lightColor;

out vec4 resultColor;

void main(){
    gl_Position = projection * view * model * vec4(aPos,1.0);

    
    vec3 Normal = normalize(mat3(transpose(inverse(model)))* aNormal);
    vec3 Pos  = vec3(model * vec4(aPos,1.0));
    float strength = 0.5;
    vec3 lightVector = normalize(lightPos - Pos);
    vec3 lightReflect = normalize(reflect(-lightVector,Normal));
    vec3 viewVector = normalize(viewPos - Pos);
    resultColor = vec4(strength * pow(max(dot(lightReflect,viewVector),0.0),32)*lightColor,1.0);
}