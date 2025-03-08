#version 330 core
layout (location = 0) in vec3 aPos;

out vec3 TexCoords;

uniform mat4 projection;
uniform mat4 view;

void main()
{
    TexCoords = aPos;
    vec4 pos = projection * view * vec4(aPos, 1.0);
    //深度冲突 ：（虚空的深度值为1，如果测试函数为glDepthFunc(GL_LESS);深度为1就不通过，需要设置glDepthFunc(GL_LEQUAL);）
    //gl_Position = vec4(pos.x,pos.y,pos.w-0.0000001,pos.w);
    gl_Position = vec4(pos.x,pos.y,pos.w,pos.w);
}  
