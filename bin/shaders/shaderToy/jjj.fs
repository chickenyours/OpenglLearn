#version 460 core

in vec2 TexCoords;         // 输入纹理坐标 (0, 1)
out vec4 FragColor;        // 输出片段颜色

uniform float iTime;       // 时间
uniform vec2 iResolution;  // 画布分辨率

const float REPEAT = 5.0;

// 回転行列 (旋转矩阵)
mat2 rot(float a) {
    float c = cos(a), s = sin(a);
    return mat2(c, s, -s, c);
}

// 计算盒子的 SDF (Signed Distance Function)
float sdBox(vec3 p, vec3 b) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

// 计算一个盒子的形状
float box(vec3 pos, float scale) {
    pos *= scale;
    float base = sdBox(pos, vec3(0.4, 0.4, 0.1)) / 1.5;
    pos.xy *= 5.0;
    pos.y -= 3.5;
    pos.xy *= rot(0.75);
    return -base;
}

// 计算多个盒子的组合形状
float box_set(vec3 pos, float gTime) {
    vec3 pos_origin = pos;
    float scaleFactor = 2.0 - abs(sin(gTime * 0.4)) * 1.5;

    pos = pos_origin;
    pos.y += sin(gTime * 0.4) * 2.5;
    pos.xy *= rot(0.8);
    float box1 = box(pos, scaleFactor);

    pos = pos_origin;
    pos.y -= sin(gTime * 0.4) * 2.5;
    pos.xy *= rot(0.8);
    float box2 = box(pos, scaleFactor);

    pos = pos_origin;
    pos.x += sin(gTime * 0.4) * 2.5;
    pos.xy *= rot(0.8);
    float box3 = box(pos, scaleFactor);

    pos = pos_origin;
    pos.x -= sin(gTime * 0.4) * 2.5;
    pos.xy *= rot(0.8);
    float box4 = box(pos, scaleFactor);

    pos = pos_origin;
    pos.xy *= rot(0.8);
    float box5 = box(pos, 0.5) * 6.0;

    pos = pos_origin;
    float box6 = box(pos, 0.5) * 6.0;

    return max(max(max(max(max(box1, box2), box3), box4), box5), box6);
}

// 计算距离场
float map(vec3 pos, float gTime) {
    return box_set(pos, gTime);
}

// 主程序
void main() {
    // 计算像素位置 (fragCoord) -> 从纹理坐标转换
    vec2 fragCoord = TexCoords * iResolution;
    
    // 计算屏幕坐标 (归一化 -1 到 1)
    vec2 p = (fragCoord * 2.0 - iResolution) / min(iResolution.x, iResolution.y);
    vec3 ro = vec3(0.0, -0.2, iTime * 4.0);
    vec3 ray = normalize(vec3(p, 1.5));

    ray.xy = ray.xy * rot(sin(iTime * 0.03) * 5.0);
    ray.yz = ray.yz * rot(sin(iTime * 0.05) * 0.2);

    float t = 0.1;
    vec3 col = vec3(0.0);
    float ac = 0.0;

    for (int i = 0; i < 99; i++) {
        vec3 pos = ro + ray * t;
        pos = mod(pos - 2.0, 4.0) - 2.0;
        
        // 动态时间变量 (避免全局变量)
        float gTime = iTime - float(i) * 0.01;
        
        float d = map(pos, gTime);
        d = max(abs(d), 0.01);
        ac += exp(-d * 23.0);

        t += d * 0.55;
    }

    // 计算颜色
    col = vec3(ac * 0.02);
    col += vec3(0.0, 0.2 * abs(sin(iTime)), 0.5 + sin(iTime) * 0.2);

    // 计算透明度
    float alpha = max(0.0, 1.0 - t * (0.02 + 0.02 * sin(iTime)));
    FragColor = vec4(col, alpha);
}
