#version 330 core

in vec2 TexCoords;

uniform sampler2D ssaoMap;

out vec3 FragColor;

vec3 guassBlur(vec2 TexCoords, vec2 offset);

void main(){
    vec2 offset = 1.0 / textureSize(ssaoMap, 0); // gets size of single texel
    vec3 result = guassBlur(TexCoords, offset);
    FragColor = result;
}


vec3 guassBlur(vec2 TexCoords, vec2 offset) {
    vec3 result = vec3(0.0);
    float kernel[9] = float[](
        1.0 / 16, 2.0 / 16, 1.0 / 16,
        2.0 / 16, 4.0 / 16, 2.0 / 16,
        1.0 / 16, 2.0 / 16, 1.0 / 16
    );

    vec2 offsets[9] = vec2[](
        vec2(-offset.x,  offset.y), // top-left
        vec2( 0.0f,    offset.y), // top-center
        vec2( offset.x,  offset.y), // top-right
        vec2(-offset.x,  0.0f),   // center-left
        vec2( 0.0f,    0.0f),   // center-center
        vec2( offset.x,  0.0f),   // center-right
        vec2(-offset.x, -offset.y), // bottom-left
        vec2( 0.0f,   -offset.y), // bottom-center
        vec2( offset.x, -offset.y)  // bottom-right    
    );

    for(int i = 0; i < 9; i++) {
        result += texture(ssaoMap, TexCoords + offsets[i]).rgb * kernel[i];
    }

    return result;
}

