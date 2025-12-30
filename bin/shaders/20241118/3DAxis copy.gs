#version 460 core
layout(points) in;                 // 输入是 point
layout(line_strip, max_vertices = 2) out;
flat in int vID[];
flat out int gID;

layout (std140, binding = 1) uniform CameraData
{
    mat4 view;
    mat4 projection;
    vec3 viewPos;
    vec3 viewDir;
};

// 你想连向的目标点
uniform vec3 targetPoint = vec3(0.0,0.0,0.0);  

void main()
{
    mat4 viewNoTranslation = view;
    viewNoTranslation[3] = vec4(0,0,0,1);   // 去掉平移
    mat4 u_MVP = projection * viewNoTranslation;

    vec3 p = gl_in[0].gl_Position.xyz;

    // 顶点1：当前点
    gl_Position = u_MVP * vec4(p, 1.0);
    EmitVertex();

    // 顶点2：目标点
    gl_Position = u_MVP * vec4(targetPoint, 1.0);
    EmitVertex();

    EndPrimitive();  // 输出一个线段
}
