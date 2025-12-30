#version 460 core
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

flat in int vID[];
flat out int gID;

layout (std140, binding = 1) uniform CameraData {
    mat4 view;
    mat4 projection;   // 不用
    vec3 viewPos;      // 不用
    vec3 viewDir;      // 不用
};

uniform float lineWidth = 3.0;  // 轴的像素宽度

void main()
{
    gID = vID[0];

    vec4 p = gl_in[0].gl_Position;

    // ❗ 只取旋转，不取位移
    mat4 viewNoTranslation = view;
    viewNoTranslation[3] = vec4(0,0,0,1);

    // 转成“摄像机方向空间”
    vec4 vpos = viewNoTranslation * p;

    // ---- 关键：扩宽为屏幕空间粗线 ----
    // 像素 -> NDC 转换，假设宽=1920
    float w = lineWidth / 1920.0 * 2.0;

    // 屏幕空间水平偏移（简单可用）
    vec2 offset = vec2(w, 0);

    // 4 个点（一个小矩形）
    vec4 a = vec4(vpos.x + offset.x, vpos.y + offset.y, vpos.z, vpos.w);
    vec4 b = vec4(vpos.x - offset.x, vpos.y - offset.y, vpos.z, vpos.w);

    // 线段终点 p 在 shader 中已经是实际坐标
    // 直接画从 p 到 vpos
    gl_Position = a; EmitVertex();
    gl_Position = b; EmitVertex();
    gl_Position = vec4(0,0,0,1); EmitVertex();
    gl_Position = vec4(0,0,0,1); EmitVertex();

    EndPrimitive();
}
