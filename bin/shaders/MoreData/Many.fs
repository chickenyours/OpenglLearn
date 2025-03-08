#version 330 core

in vec3 Eye;
in vec2 TexCoord;
in vec3 Pos;
in vec3 Normal;

uniform sampler2D diffusion;

out vec4 FragColor;

void main(){
    FragColor = texture(diffusion,TexCoord);
}