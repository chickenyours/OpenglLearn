#version 330 core
#pragma SLOT(common_macros)
in vec3 FragPos;

uniform samplerCube cubeMap;

out vec4 FragColor;

void main(){
    vec3 envColor = textureLod(cubeMap, FragPos,1).rgb;
    //vec3 envColor = texture(cubeMap, FragPos).rgb;
    
    //envColor = envColor / (envColor + vec3(1.0));
  
    FragColor = vec4(envColor, 1.0);
}
