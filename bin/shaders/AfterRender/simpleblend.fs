#version 330 core 

in vec2 TexCoords;

uniform sampler2D colorbuffer;


out vec4 FragColor;

void main(){
    float quanzhong = 0.2;
    FragColor = vec4(texture(colorbuffer,TexCoords).rgb,quanzhong);
}
