#version 330 core

in vec2 TexCrood;

uniform sampler2D diffusion;

out vec4 FragColor;

void main(){
    vec4 texColor = texture(diffusion,TexCrood);
    FragColor = texColor;
}