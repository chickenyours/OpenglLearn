#version 460 core
layout(location = 0) in vec3 aPos;

flat out int vID;
out vec3 vWorldPos;

void main() {
    vWorldPos = aPos;
    vID = gl_VertexID;
    gl_Position = vec4(aPos, 1.0); // 暂存 world pos，GS 会转换
}
