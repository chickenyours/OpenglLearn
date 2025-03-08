#version 330 core

out vec4 FragColor; // 输出颜色

in vec3 TexCoords; // 从顶点着色器传递过来的纹理坐标

uniform samplerCube cubeTexture; // 立方体纹理

void main()
{
    //FragColor = vec4(1.0);
    FragColor = vec4(vec3(texture(cubeTexture, TexCoords).r),1.0); // 使用纹理坐标从立方体纹理中获取颜色
}
