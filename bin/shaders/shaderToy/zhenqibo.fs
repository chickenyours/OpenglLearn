#version 460 core

in vec2 TexCoords; // 输入的纹理坐标作为 fragCoord 使用
out vec4 FragColor;

uniform float iTime;
uniform vec2 iResolution;

#define speed 10. 
#define wave_thing
#define audio_vibration_amplitude .125

float jTime;

// 纹理镜像函数 (简化版)
vec4 textureMirror(vec2 c){
    vec2 cf = fract(c);
    return vec4(mix(cf, 1.0 - cf, mod(floor(c), 2.0)), 0.0, 0.0);
}

// 计算强度
float amp(vec2 p){
    return smoothstep(1.0, 8.0, abs(p.x));   
}

float pow512(float a){
    a *= a; a *= a; a *= a; a *= a; a *= a;
    a *= a; a *= a; a *= a;
    return a * a;
}

float pow1d5(float a){
    return a * sqrt(a);
}

float hash21(vec2 co){
    return fract(sin(dot(co.xy, vec2(1.9898, 7.233))) * 45758.5433);
}

float hash(vec2 uv){
    float a = amp(uv);
    float w = a > 0.0 ? (1.0 - 0.4 * pow512(0.51 + 0.49 * sin((0.02 * (uv.y + 0.5 * uv.x) - jTime) * 2.0))) : 0.0;
    return (a > 0.0 ? a * pow1d5(hash21(uv)) * w : 0.0);
}

// 三角噪声
vec2 trinoise(vec2 uv){
    const float sq = sqrt(3.0 / 2.0);
    uv.x *= sq;
    uv.y -= 0.5 * uv.x;
    vec2 d = fract(uv);
    uv -= d;

    bool c = dot(d, vec2(1)) > 1.0;

    vec2 dd = 1.0 - d;
    vec2 da = c ? dd : d, db = c ? d : dd;
    
    float nn = hash(uv + float(c));
    float n2 = hash(uv + vec2(1, 0));
    float n3 = hash(uv + vec2(0, 1));
    
    float nmid = mix(n2, n3, d.y);
    float ns = mix(nn, c ? n2 : n3, da.y);
    float dx = da.x / db.y;
    return vec2(mix(ns, nmid, dx), 0.0);
}

// 地图函数
vec2 map(vec3 p){
    vec2 n = trinoise(p.xz);
    return vec2(p.y - 2.0 * n.x, n.y);
}

// 梯度计算
vec3 grad(vec3 p){
    const vec2 e = vec2(0.005, 0.0);
    float a = map(p).x;
    return vec3(map(p + e.xyy).x - a,
                map(p + e.yxy).x - a,
                map(p + e.yyx).x - a) / e.x;
}

// 光线求交
vec2 intersect(vec3 ro, vec3 rd){
    float d = 0.0, h = 0.0;
    for(int i = 0; i < 500; i++){
        vec3 p = ro + d * rd;
        vec2 s = map(p);
        h = s.x;
        d += h * 0.5;
        if(abs(h) < 0.003 * d)
            return vec2(d, s.y);
        if(d > 150.0 || p.y > 2.0) break;
    }
    return vec2(-1.0);
}

// 星噪声
float starnoise(vec3 rd){
    float c = 0.0;
    vec3 p = normalize(rd) * 300.0;
    for (float i = 0.0; i < 4.0; i++){
        vec3 q = fract(p) - 0.5;
        vec3 id = floor(p);
        float c2 = smoothstep(0.5, 0.0, length(q));
        c2 *= step(hash21(id.xz / id.y), 0.06 - i * i * 0.005);
        c += c2;
        p = p * 0.6 + 0.5 * p * mat3(0.6, 0, 0.8, 0, 1, 0, -0.8, 0, 0.6);
    }
    return c * c;
}

// 天空颜色
vec3 gsky(vec3 rd, vec3 ld, bool mask){
    float haze = exp2(-5.0 * (abs(rd.y) - 0.2 * dot(rd, ld)));
    float st = mask ? (starnoise(rd)) * (1.0 - min(haze, 1.0)) : 0.0;
    vec3 back = vec3(0.4, 0.1, 0.7);
    vec3 col = clamp(mix(back, vec3(0.7, 0.1, 0.4), haze) + st, 0.0, 1.0);
    return col;
}

// 主程序
void main() {
    vec2 fragCoord = TexCoords * iResolution; // 转换纹理坐标到像素坐标
    vec2 uv = (2.0 * fragCoord - iResolution.xy) / iResolution.y;
    
    float dt = fract(hash21(fragCoord) + iTime) * 0.25;
    jTime = mod(iTime - dt, 4000.0);
    vec3 ro = vec3(0.0, 1.0, (-20000.0 + jTime * speed));
    vec3 rd = normalize(vec3(uv, 4.0 / 3.0));
    
    vec2 i = intersect(ro, rd);
    float d = i.x;
    vec3 ld = normalize(vec3(0.0, 0.125 + 0.05 * sin(0.1 * jTime), 1.0));
    
    vec3 fog = d > 0.0 ? exp2(-d * vec3(0.14, 0.1, 0.28)) : vec3(0.0);
    vec3 sky = gsky(rd, ld, d < 0.0);
    
    vec3 p = ro + d * rd;
    vec3 n = normalize(grad(p));
    
    float diff = dot(n, ld) + 0.1 * n.y;
    vec3 col = vec3(0.1, 0.11, 0.18) * diff;
    
    vec3 rfd = reflect(rd, n);
    vec3 rfcol = gsky(rfd, ld, true);
    
    col = mix(col, rfcol, 0.05 + 0.95 * pow(max(1.0 + dot(rd, n), 0.0), 5.0));
    col = mix(sky, col, fog);
    
    if (d < 0.0) d = 1e6;
    d = min(d, 10.0);
    FragColor = vec4(clamp(col, 0.0, 1.0), d < 0.0 ? 0.0 : 0.1 + exp2(-d));
}
