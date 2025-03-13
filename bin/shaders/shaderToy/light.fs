#version 460 core

in vec2 TexCoords; // 输入的纹理坐标作为 fragCoord 使用
out vec4 FragColor;

uniform float iTime;
uniform vec2 iResolution;

#define t iTime
#define r iResolution.xy

void main() {
    vec3 c = vec3(0.0);
    float l, z = t;
    vec2 fragCoord = TexCoords * iResolution; // 转换纹理坐标到像素坐标

    for (int i = 0; i < 3; i++) {
        vec2 uv, p = fragCoord / r;
        uv = p;
        p -= 0.5;
        p.x *= r.x / r.y;
        z += 0.07;
        l = length(p);
        uv += p / l * (sin(z) + 1.0) * abs(sin(l * 9.0 - z - z));
        c[i] = 0.01 / length(mod(uv, 1.0) - 0.5);
    }
    FragColor = vec4(c / l, t);
}
