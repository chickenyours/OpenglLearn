#version 330 core

uniform sampler2D gAlbedoSpec;
uniform sampler2D ssaoMap;

in vec2 TexCoords;

out vec4 FragColor;

void main(){
    vec3 fragColor = texture(gAlbedoSpec, TexCoords).rgb;
    float ssao = texture(ssaoMap, TexCoords).r;
    FragColor = vec4(fragColor * ssao, 1.0);
}