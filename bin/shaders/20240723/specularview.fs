#version 330 core

in vec3 Normal;
in vec3 Pos;
in vec3 lightViewPos;

uniform vec3 viewPos;
uniform vec3 lightColor;
//uniform vec3 objectColor;

out vec4 FragColor;

void main(){
    float strength = 0.5;
    vec3 lightVector = normalize(lightViewPos - Pos);
    vec3 lightReflect = normalize(reflect(-lightVector,Normal));
    //vec3 viewVector = normalize(viewPos - Pos);
    FragColor = vec4(strength * pow(max(dot(lightReflect,normalize(-Pos)),0.0),32)*lightColor,1.0);

    
}