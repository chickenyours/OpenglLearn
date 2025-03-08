#version 330 core

// in vec3 Pos;
// in vec3 Normal;
in vec2 TexCoords;  // 确保这里声明了 TexCoord
// in vec3 Eye;

uniform sampler2D diffusion;
uniform sampler2D specular;

out vec4 FragColor;

void main() {
    FragColor = texture(diffusion, TexCoords);  // 使用 TexCoord 进行纹理采样
    //FragColor = vec4(1.0);
}
