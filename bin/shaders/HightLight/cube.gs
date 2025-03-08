#version 330 core
layout (triangles) in;
layout (triangle_strip, max_vertices=18) out;

//每个面的光空间变换矩阵
uniform mat4 shadowMatrices[6];
uniform vec3 CubeLightPos;

out vec4 FragPos; // FragPos from GS (output per emitvertex)

void main()
{
    for(int face = 0; face < 6; ++face)
    {
        //指定立方体哪个目标面
        gl_Layer = face; // built-in variable that specifies to which face we render.
        for(int i = 0; i < 3; ++i) // for each triangle's vertices
        {
            FragPos = gl_in[i].gl_Position;
            gl_Position = shadowMatrices[face] * vec4(FragPos.xyz - CubeLightPos,1.0);
            EmitVertex();
        }    
        EndPrimitive();
    }
}