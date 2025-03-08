#version 330 core

in vec2 Tex;

out vec4 FragColor;

uniform sampler2D diffuse;

void main(){
    FragColor = texture(diffuse,Tex);
}