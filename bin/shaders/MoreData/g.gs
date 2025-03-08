#version 330 core
//输入控制点类型(point)
layout (points) in;
//points：绘制GL_POINTS图元时（1）。
//lines：绘制GL_LINES或GL_LINE_STRIP时（2） : GL_LINES偏移量为2 GL_LINE_STRIP为1
//lines_adjacency：GL_LINES_ADJACENCY或GL_LINE_STRIP_ADJACENCY（4）
//triangles：GL_TRIANGLES、GL_TRIANGLE_STRIP或GL_TRIANGLE_FAN（3）
//triangles_adjacency：GL_TRIANGLES_ADJACENCY或GL_TRIANGLE_STRIP_ADJACENCY（6）
//输出图元类型(line_strip) 最大输出图元顶点为 2
layout (triangle_strip , max_vertices = 5) out;
//points
//lines
//line_loop 
//line_strip
//triangles
//triangle_fan
//triangle_strip
//输出点图元
//layout (points,max_vertices = 1) out;

in VS_OUT{
    vec3 color;
} gs_in[];


/*
GLSL提供给我们一个内建(Built-in)变量，在内部看起来（可能）是这样的：
in gl_Vertex
{
    vec4  gl_Position;
    float gl_PointSize;
    float gl_ClipDistance[];
} gl_in[];
*/

out vec3 fColor;

void build_house(vec4 position)
{   
    fColor = gs_in[0].color;
    gl_Position = position + vec4(-0.2, -0.2, 0.0, 0.0);    // 1:左下
    EmitVertex();   
    gl_Position = position + vec4( 0.2, -0.2, 0.0, 0.0);    // 2:右下
    EmitVertex();
    gl_Position = position + vec4(-0.2,  0.2, 0.0, 0.0);    // 3:左上
    EmitVertex();
    gl_Position = position + vec4( 0.2,  0.2, 0.0, 0.0);    // 4:右上
    fColor = vec3(1.0, 1.0, 1.0);
    EmitVertex();
    gl_Position = position + vec4( 0.0,  0.4, 0.0, 0.0);    // 5:顶部
    fColor = vec3(1.0, 1.0, 1.0);
    EmitVertex();
    EndPrimitive();
}

void main(){
    build_house(gl_in[0].gl_Position);
}
