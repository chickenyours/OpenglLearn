#version 460 core

in vec2 TexCoords;

out vec4 FragColor;

// 纹理数组
layout (binding = 0) uniform sampler2DArray colorBuffer;

// 新增一个 uniform 表示采样的层级
uniform int layerIndex;

void main() {
    // 在 vec2 的基础上加上层级索引，组成 vec3 来访问纹理数组
    FragColor = texture(colorBuffer, vec3(TexCoords, float(layerIndex)));
}
