#version 460 core

in vec2 TexCoords; // 纹理坐标，映射到 fragCoord
out vec4 FragColor;

uniform float iTime;
uniform vec2 iResolution;
uniform vec2 iMouse;

#define DRAG_MULT 0.38
#define WATER_DEPTH 1.0
#define CAMERA_HEIGHT 1.5
#define ITERATIONS_RAYMARCH 12
#define ITERATIONS_NORMAL 36

#define NormalizedMouse (iMouse.xy / iResolution.xy)

vec2 wavedx(vec2 position, vec2 direction, float frequency, float timeshift) {
    float x = dot(direction, position) * frequency + timeshift;
    float wave = exp(sin(x) - 1.0);
    float dx = wave * cos(x);
    return vec2(wave, -dx);
}

float getwaves(vec2 position, int iterations) {
    float wavePhaseShift = length(position) * 0.1;
    float iter = 0.0;
    float frequency = 1.0;
    float timeMultiplier = 2.0;
    float weight = 1.0;
    float sumOfValues = 0.0;
    float sumOfWeights = 0.0;

    for (int i = 0; i < iterations; i++) {
        vec2 p = vec2(sin(iter), cos(iter));
        vec2 res = wavedx(position, p, frequency, iTime * timeMultiplier + wavePhaseShift);
        position += p * res.y * weight * DRAG_MULT;
        sumOfValues += res.x * weight;
        sumOfWeights += weight;
        weight = mix(weight, 0.0, 0.2);
        frequency *= 1.18;
        timeMultiplier *= 1.07;
        iter += 1232.399963;
    }
    return sumOfValues / sumOfWeights;
}

float raymarchwater(vec3 camera, vec3 start, vec3 end, float depth) {
    vec3 pos = start;
    vec3 dir = normalize(end - start);
    for (int i = 0; i < 64; i++) {
        float height = getwaves(pos.xz, ITERATIONS_RAYMARCH) * depth - depth;
        if (height + 0.01 > pos.y) {
            return distance(pos, camera);
        }
        pos += dir * (pos.y - height);
    }
    return distance(start, camera);
}

vec3 normal(vec2 pos, float e, float depth) {
    vec2 ex = vec2(e, 0);
    float H = getwaves(pos.xy, ITERATIONS_NORMAL) * depth;
    vec3 a = vec3(pos.x, H, pos.y);
    return normalize(
        cross(
            a - vec3(pos.x - e, getwaves(pos.xy - ex.xy, ITERATIONS_NORMAL) * depth, pos.y), 
            a - vec3(pos.x, getwaves(pos.xy + ex.yx, ITERATIONS_NORMAL) * depth, pos.y + e)
        )
    );
}

mat3 createRotationMatrixAxisAngle(vec3 axis, float angle) {
    float s = sin(angle);
    float c = cos(angle);
    float oc = 1.0 - c;
    return mat3(
        oc * axis.x * axis.x + c, oc * axis.x * axis.y - axis.z * s, oc * axis.z * axis.x + axis.y * s, 
        oc * axis.x * axis.y + axis.z * s, oc * axis.y * axis.y + c, oc * axis.y * axis.z - axis.x * s, 
        oc * axis.z * axis.x - axis.y * s, oc * axis.y * axis.z + axis.x * s, oc * axis.z * axis.z + c
    );
}

vec3 getRay(vec2 fragCoord) {
    vec2 uv = ((fragCoord / iResolution) * 2.0 - 1.0) * vec2(iResolution.x / iResolution.y, 1.0);
    vec3 proj = normalize(vec3(uv.x, uv.y, 1.5));
    if (iResolution.x < 600.0) {
        return proj;
    }
    return createRotationMatrixAxisAngle(vec3(0.0, -1.0, 0.0), 3.0 * ((NormalizedMouse.x + 0.5) * 2.0 - 1.0)) 
        * createRotationMatrixAxisAngle(vec3(1.0, 0.0, 0.0), 0.5 + 1.5 * (((NormalizedMouse.y == 0.0 ? 0.27 : NormalizedMouse.y) * 1.0) * 2.0 - 1.0))
        * proj;
}

float intersectPlane(vec3 origin, vec3 direction, vec3 point, vec3 normal) { 
    return clamp(dot(point - origin, normal) / dot(direction, normal), -1.0, 9991999.0); 
}

vec3 extra_cheap_atmosphere(vec3 raydir, vec3 sundir) {
    float special_trick = 1.0 / (raydir.y * 1.0 + 0.1);
    float special_trick2 = 1.0 / (sundir.y * 11.0 + 1.0);
    float raysundt = pow(abs(dot(sundir, raydir)), 2.0);
    float sundt = pow(max(0.0, dot(sundir, raydir)), 8.0);
    float mymie = sundt * special_trick * 0.2;
    vec3 suncolor = mix(vec3(1.0), max(vec3(0.0), vec3(1.0) - vec3(5.5, 13.0, 22.4) / 22.4), special_trick2);
    vec3 bluesky = vec3(5.5, 13.0, 22.4) / 22.4 * suncolor;
    return bluesky * (1.0 + 1.0 * pow(1.0 - raydir.y, 3.0));
}

vec3 getSunDirection() {
    return normalize(vec3(-0.07735, 0.5 + sin(iTime * 0.2 + 2.6) * 0.45, 0.57735));
}

vec3 getAtmosphere(vec3 dir) {
    return extra_cheap_atmosphere(dir, getSunDirection()) * 0.5;
}

float getSun(vec3 dir) { 
    return pow(max(0.0, dot(dir, getSunDirection())), 720.0) * 210.0;
}

vec3 aces_tonemap(vec3 color) {  
    mat3 m1 = mat3(
        0.59719, 0.07600, 0.02840,
        0.35458, 0.90834, 0.13383,
        0.04823, 0.01566, 0.83777
    );
    mat3 m2 = mat3(
        1.60475, -0.10208, -0.00327,
        -0.53108,  1.10813, -0.07276,
        -0.07367, -0.00605,  1.07602
    );
    vec3 v = m1 * color;  
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return pow(clamp(m2 * (a / b), 0.0, 1.0), vec3(1.0 / 2.2));  
}

void main() {
    vec2 fragCoord = TexCoords * iResolution;
    vec3 ray = getRay(fragCoord);

    if (ray.y >= 0.0) {
        vec3 C = getAtmosphere(ray) + getSun(ray);
        FragColor = vec4(aces_tonemap(C * 2.0), 1.0);
        return;
    }

    vec3 origin = vec3(iTime * 0.2, CAMERA_HEIGHT, 1);
    float dist = raymarchwater(origin, origin + ray * 10.0, origin + ray * 20.0, WATER_DEPTH);
    FragColor = vec4(vec3(0.2, 0.3, 0.5) * (1.0 - dist * 0.01), 1.0);
}
