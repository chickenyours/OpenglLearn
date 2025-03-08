#version 330 core 

in vec2 TexCoords;

uniform sampler2D colorBuffer;


out vec4 FragColor;

void main(){
    FragColor = vec4(1.0);
}