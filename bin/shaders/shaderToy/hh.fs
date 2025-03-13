// Thank You Inigo Quilez for the articles https://iquilezles.org/
// 光线前进算法
#version 460 core

in vec2 TexCoords; // 输入的纹理坐标作为 fragCoord 使用
out vec4 FragColor;

uniform float iTime;
uniform vec2 iResolution;

#define PI 3.141592653589793

float _FogStr = 0.08;
vec3 _FogColor = vec3(0.502, 0.600, 0.698);
vec3 _LightColor = vec3(1.0, 0.902, 0.800);
float _CellSize = 4.0;

float ease_step(float x, float k) {
    return floor(x) + (mod(x, 1.0) < k ? smoothstep(0.0, 1.0, smoothstep(0.0, 1.0, (x - floor(x)) / k)) : 1.0);
}

float length8(vec2 p) { p = p * p; p = p * p; p = p * p; return pow(p.x + p.y, 1.0 / 8.0); }
float length2(vec2 p) { return sqrt(p.x * p.x + p.y * p.y); }

float sdTorus82(vec3 p, vec2 t) {
    vec2 q = vec2(length8(p.xz) - t.x, p.y);
    return length2(q) - t.y;
}

float sdRoundBox(vec3 p, vec3 b, float r) {
    vec3 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0) - r;
}

float sdTorus(vec3 p, vec2 t) {
    vec2 q = vec2(length(p.xz) - t.x, p.y);
    return length(q) - t.y;
}

float sdBoxFrame(vec3 p, vec3 b, float e) {
    p = abs(p) - b;
    vec3 q = abs(p + e) - e;
    return min(min(
        length(max(vec3(p.x, q.y, q.z), 0.0)) + min(max(p.x, max(q.y, q.z)), 0.0),
        length(max(vec3(q.x, p.y, q.z), 0.0)) + min(max(q.x, max(p.y, q.z)), 0.0)),
        length(max(vec3(q.x, q.y, p.z), 0.0)) + min(max(q.x, max(q.y, p.z)), 0.0));
}

float GetDist(vec3 position) {
    vec3 repeatPos = position;
    float cellSize = _CellSize;

    repeatPos.x = mod(position.x, cellSize) - cellSize * 0.5;
    repeatPos.z = mod(position.z, cellSize) - cellSize * 0.5;
    repeatPos.y = mod(position.y, cellSize) - cellSize * 0.5;

    float cellVariation = sin(floor(position.x / cellSize) * 0.3 +
                              floor(position.z / cellSize) * 0.5 +
                              floor(position.y / cellSize) * 0.7);
    float timeOffset = cellVariation * 0.2;

    vec3 boxPos = repeatPos;
    boxPos.xz = mat2(cos(-iTime * 0.5 + timeOffset), -sin(-iTime * 0.5 + timeOffset),
                     sin(-iTime * 0.5 + timeOffset), cos(-iTime * 0.5 + timeOffset)) * boxPos.xz;
    float boxFrame = sdBoxFrame(boxPos, vec3(0.85), 0.2);

    vec3 torusPos = repeatPos;
    torusPos.xz = mat2(cos(iTime * 0.5 + timeOffset), -sin(iTime * 0.5 + timeOffset),
                       sin(iTime * 0.5 + timeOffset), cos(iTime * 0.5 + timeOffset)) * torusPos.xz;
    float roundTor = sdTorus82(torusPos.xzy, vec2(0.5, 0.2));
    float torus = sdTorus(torusPos.xzy, vec2(0.5, 0.1));

    float blendFactor = sin(iTime + cellVariation) * 0.5 + 0.5;
    float torusShape = mix(roundTor, torus, blendFactor);

    return min(boxFrame, torusShape);
}

float RayMarch(vec3 rayOrigin, vec3 rayDirection, out int val) {
    float distanceOrigin = 0.0;
    int i;
    for (i = 0; i < 200; i++) {
        vec3 point = rayOrigin + distanceOrigin * rayDirection;
        float dist = GetDist(point);
        distanceOrigin += dist * 0.95;
        if (distanceOrigin > 200.0 || dist < 0.00001) break;
    }
    val = i;
    return distanceOrigin;
}

vec3 GetNormal(vec3 position) {
    vec2 e = vec2(0.0001, 0);
    return normalize(vec3(
        GetDist(position + e.xyy) - GetDist(position - e.xyy),
        GetDist(position + e.yxy) - GetDist(position - e.yxy),
        GetDist(position + e.yyx) - GetDist(position - e.yyx)
    ));
}

vec3 GetLight(vec3 position) {
    vec3 lightPos = vec3(2.0 * sin(iTime * 0.5), 4.0, -5.0);
    vec3 lightDir = normalize(lightPos - position);
    vec3 normal = GetNormal(position);
    float diffuse = max(dot(normal, lightDir), 0.0);
    return _LightColor * diffuse + vec3(0.1);
}

float Random(vec2 uv) {
    return fract(sin(dot(uv, vec2(12.9898, 78.233))) * 43758.5453123);
}

float Dither(vec2 uv) {
    return Random(uv) * 0.03 - 0.015;
}

void main() {
    vec2 uv = vec2(TexCoords.x - 0.5, TexCoords.y - 0.5);
    uv.x *= iResolution.x / iResolution.y;
    vec3 rayOrigin = vec3(0.0, 0.0, iTime * 2.0);
    vec3 rayDirection = normalize(vec3(uv.x, uv.y, 1.0));

    float angle = ease_step(iTime * 0.25, 0.25) * (PI / 2.0);
    rayDirection.xz = mat2(cos(angle), -sin(angle), sin(angle), cos(angle)) * rayDirection.xz;

    int steps = 0;
    float dist = RayMarch(rayOrigin, rayDirection, steps);

    float fogAmount = 1.0 - exp2(-dist * _FogStr);
    vec3 color = vec3(0.0);

    if (dist < 200.0) {
        vec3 hitPos = rayOrigin + dist * rayDirection;
        vec3 lighting = GetLight(hitPos);
        vec3 normal = GetNormal(hitPos);
        vec3 objectColor = vec3(0.8, 0.3, 0.2) * (normal.y * 0.5 + 0.5) +
                            vec3(0.2, 0.5, 0.8) * (normal.x * 0.5 + 0.5) +
                            vec3(0.3, 0.6, 0.3) * (normal.z * 0.5 + 0.5);
        color = objectColor * lighting;
        float rim = pow(1.0 - max(dot(normal, -rayDirection), 0.0), 3.0);
        color += vec3(0.5, 0.7, 1.0) * rim * 0.5;
    }

    color = mix(color, _FogColor, fogAmount);
    color += Dither(TexCoords.xy);
    float vignette = length(uv) * 0.5;
    color *= 1.0 - vignette;

    FragColor = vec4(color, 1.0);
    //FragColor = vec4(,0.0,1.0);
}
