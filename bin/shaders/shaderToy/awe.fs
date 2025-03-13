#version 460 core

in vec2 TexCoords; // 纹理坐标，映射到 fragCoord
out vec4 FragColor;

uniform float iTime;
uniform vec2 iResolution;

int hexid;
vec3 hpos, point, pt;
float tcol, bcol, hitbol, hexpos, fparam = 0.;

mat2 rot(float a) {
    float s = sin(a), c = cos(a);
    return mat2(c, s, -s, c);
}

vec3 path(float t) {
    return vec3(sin(t * 0.3 + cos(t * 0.2) * 0.5) * 4.0, cos(t * 0.2) * 3.0, t);
}

float hexagon(vec2 p, float r) {
    const vec3 k = vec3(-0.866025404, 0.5, 0.577350269);
    p = abs(p);
    p -= 2.0 * min(dot(k.xy, p), 0.0) * k.xy;
    p -= vec2(clamp(p.x, -k.z * r, k.z * r), r);
    return length(p) * sign(p.y);
}

float hex(vec2 p) {
    p.x *= 0.57735 * 2.0;
    p.y += mod(floor(p.x), 2.0) * 0.5;
    p = abs((mod(p, 1.0) - 0.5));
    return abs(max(p.x * 1.5 + p.y, p.y * 2.0) - 1.0);
}

mat3 lookat(vec3 dir) {
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 rt = normalize(cross(dir, up));
    return mat3(rt, cross(rt, dir), dir);
}

float sphereLineAB(vec3 p, vec3 a, vec3 b, float rad) {
    p = normalize(p);
    return dot(p, cross(a, b)) / length(a - b);
}

vec3 erot(vec3 p, vec3 ax, float ro) {
    return mix(dot(ax, p) * ax, p, cos(ro)) + sin(ro) * cross(ax, p);
}

#define PI 3.14159265359
#define PHI (1.0 + sqrt(5.0)) / 2.0

void icosahedronVerts(in vec3 p, out vec3 face, out vec3 a, out vec3 b, out vec3 c) {
    vec3 V = vec3(PHI, 1, 0);
    vec3 ap = abs(p), v = V;
    if (dot(ap, V.yzx - v) > 0.0) v = V.yzx;
    if (dot(ap, V.zxy - v) > 0.0) v = V.zxy;
    a = normalize(v) * sign(p);
    
    v = V.xxx;
    V = vec3(V.zy, V.x + V.y);
    if (dot(ap, V - v) > 0.0) v = V;
    if (dot(ap, V.yzx - v) > 0.0) v = V.yzx;
    if (dot(ap, V.zxy - v) > 0.0) v = V.zxy;
    face = normalize(v) * sign(p);
   
    float r = PI * 2.0 / 3.0;
    
    b = erot(a, face, -r);
    c = erot(a, face, r);
}

float de(vec3 p) {
    pt = vec3(p.xy - path(p.z).xy, p.z);
    float h = abs(hexagon(pt.xy, 3.0 + fparam));
    hexpos = hex(pt.yz);
    tcol = smoothstep(0.0, 0.15, hexpos);
    h -= tcol * 0.1;
    vec3 pp = p - hpos;
    pp = lookat(point) * pp;
    pp.y -= abs(sin(iTime)) * 3.0 + (fparam - (2.0 - fparam));
    pp.yz *= rot(-iTime);
    
    float bola = length(pp) - 1.0;
    
    if (length(pp) < 1.5) {
        vec3 face;
        vec3 v[3];
        icosahedronVerts(pp, face, v[0], v[1], v[2]);
        float rad = 1.0;

        vec3 mid[6];
        for (int i = 0; i < 3; i++) {
            mid[i * 2] = mix(v[i], v[(i + 1) % 3], 1.0 / 3.0);
            mid[i * 2 + 1] = mix(v[i], v[(i + 1) % 3], 2.0 / 3.0);
        }

        float poly = -1e5;
        for (int i = 0; i < 6; i++) {
            poly = max(poly, sphereLineAB(pp, mid[i], mid[(i + 1) % 6], rad));
        }

        bcol = smoothstep(0.0, 0.05, abs(poly));
        bola += bcol * 0.1;
    }

    float d = min(h, bola);
    hitbol = (d == bola) ? 1.0 : 0.0;
    tcol = (d == bola) ? 1.0 : tcol;
    return d * 0.5;
}

vec3 normal(vec3 p) {
    vec2 e = vec2(0.0, 0.005);
    return normalize(vec3(de(p + e.yxx), de(p + e.xyx), de(p + e.xxy)) - de(p));
}

vec3 march(vec3 from, vec3 dir) {
    vec3 p = from, col = vec3(0.0);
    float d, td = 0.0;
    vec3 g = vec3(0.0);
    
    for (int i = 0; i < 200; i++) {
        d = de(p);
        if (d < 0.001 || td > 200.0) break;
        p += dir * d;
        td += d;
        g += 0.1 / (0.1 + d) * hitbol * abs(normalize(point));
    }

    p -= dir * 0.01;
    vec3 n = normal(p);
    if (d < 0.001) {
        col = pow(max(0.0, dot(-dir, n)), 2.0) * vec3(0.6, 0.7, 0.8) * tcol * bcol;
    }

    col += float(hexid);
    return col;
}

void main() {
    vec2 fragCoord = TexCoords * iResolution;
    vec2 uv = fragCoord / iResolution.xy - 0.5;
    uv.x *= iResolution.x / iResolution.y;
    float t = iTime * 2.0;

    vec3 from = path(t);
    if (mod(iTime - 10.0, 20.0) > 10.0) {
        from = path(floor(t / 20.0) * 20.0 + 10.0);
        from.x += 2.0;
    }

    hpos = path(t + 3.0);
    vec3 adv = path(t + 2.0);
    vec3 dir = normalize(vec3(uv, 0.7));
    vec3 dd = normalize(adv - from);
    point = normalize(adv - hpos);
    point.xz *= rot(sin(iTime) * 0.2);
    dir = lookat(dd) * dir;
    vec3 col = march(from, dir);
    col *= vec3(1.0, 0.9, 0.8);
    FragColor = vec4(col, 1.0);
}
