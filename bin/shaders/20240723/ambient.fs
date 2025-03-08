#version 330 core

out vec4 FragColor;
uniform vec3 lightColor;
uniform vec3 objectColor;

void main(){
    float strength = 0.1;
    vec3 result = lightColor * objectColor * strength;
    FragColor = vec4(result,1.0);
    //FragColor = vec4(lightColor * objectColor * strength,1.0);
}