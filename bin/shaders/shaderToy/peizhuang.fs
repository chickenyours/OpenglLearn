#version 460 core

in vec2 TexCoords; // 输入的纹理坐标
out vec4 FragColor;

uniform float iTime;
uniform vec2 iResolution;

// 常量定义
#define POINTCOUNT 8 
#define PI 3.14159

// One-liners
#define inBox(low,high,point) (low.x<=point.x && low.y<=point.y && high.x>point.x && high.y>point.y)
#define toUv(point) 2.0*(point-0.5*iResolution.xy)/iResolution.y

void main() {
    // 计算fragCoord
    vec2 fragCoord = TexCoords * iResolution;

    vec2 uv = toUv(fragCoord);
    vec2 low = floor(uv);
    vec2 high = ceil(uv);
    uv = mod(uv, vec2(1.0));

    vec2[POINTCOUNT] points;
    
    float t = iTime * 0.4;
    for (int i = 0; i < POINTCOUNT; i++) {
        float fi = float(i);
        if (i == POINTCOUNT - 1) {
            points[POINTCOUNT - 1] = toUv(iResolution * 0.5); // 默认中心点
        } else {
            points[i] = vec2(
                (1.75 - 0.125 * fi) * cos(1.333 * t - (1.0 - 0.2 * t) * fi / PI),
                (1.0 - 0.1 * fi) * sin((fi + 1.0) * t + PI * 0.5)
            );
        }
    }

    int iters = 0;
    while (iters < 8) {
        iters++;
        bool sub = false;
        for (int i = 0; i < POINTCOUNT; i++) {
            if (inBox(low, high, points[i])) {
                sub = true;
                break;
            }
        }
        if (sub) {
            vec2 center = (high + low) * 0.5;
            if (uv.x > 0.5) {
                low.x = center.x;
                uv.x = uv.x * 2.0 - 1.0;
            } else {
                high.x = center.x;
                uv.x *= 2.0;
            }
            if (uv.y > 0.5) {
                low.y = center.y;
                uv.y = uv.y * 2.0 - 1.0;
            } else {
                high.y = center.y;
                uv.y *= 2.0;
            }
        } else {
            break;
        }
    }

    vec3 col = vec3(smoothstep(1.0 - 0.01 * pow(2.0, float(iters)), 1.0, max(abs(uv.x - 0.5), abs(uv.y - 0.5)) * 2.0));

    FragColor = vec4(col, 1.0);
}
