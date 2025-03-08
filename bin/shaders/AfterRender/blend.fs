#version 330 core 

in vec2 TexCoords;

uniform sampler2D sceneColorBuffer;
uniform sampler2D outputColorBuffer;

out vec4 FragColor;

void main(){
    float Bloom = .4;
    FragColor = vec4(texture(sceneColorBuffer,TexCoords).rgb + texture(outputColorBuffer,TexCoords).rgb * Bloom,1.0);
}
