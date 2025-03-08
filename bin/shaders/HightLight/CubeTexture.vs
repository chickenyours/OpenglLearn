#version 330 core

layout (location = 0) in vec3 aPos; // 顶点位置
layout (location = 1) in vec3 aTexCoords; // 纹理坐标

out vec3 TexCoords; // 传递给片段着色器的纹理坐标

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    TexCoords = aTexCoords;
    gl_Position = projection * view * model * vec4(aPos, 1.0f);
}
