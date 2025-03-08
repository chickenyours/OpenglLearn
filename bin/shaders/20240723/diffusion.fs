#version 330 core


in vec3 Normal;
in vec3 Pos;

uniform vec3 lightPos;
uniform vec3 lightColor;
uniform vec3 objectColor;

out vec4 FragColor;

void main(){
    float strength = 0.1;
    float mult = clamp(dot(Normal,normalize(lightPos-Pos)),0.0,1.0);
    FragColor = vec4(objectColor*lightColor*mult*100.0/(length(lightPos-Pos)*length(lightPos-Pos)+1) + objectColor*strength,1.0);
}