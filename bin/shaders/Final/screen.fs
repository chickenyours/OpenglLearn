#version 460 core

in vec2 TexCoords;

out vec4 FragColor;
 
layout (binding = 0) uniform sampler2D colorBuffer;

void main(){
    FragColor = texture(colorBuffer,TexCoords);
}