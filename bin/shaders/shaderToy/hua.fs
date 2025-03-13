#version 460 core

in vec2 TexCoords; // 纹理坐标，映射到 fragCoord
out vec4 FragColor;

uniform float iTime;
uniform vec2 iResolution;

#define PI 3.14159
#define shortestWidth min(iResolution.x, iResolution.y)
#define smoothDist (4.0 / shortestWidth)

// === UTILITIES ===

vec2 vec2coord(float angle, float len) {
    return cos(angle + vec2(0, -0.5 * PI)) * len;
}

float outline(float dist, float lineWidth) {
    float halfLineWidth = lineWidth / 2.0;
    float col1 = smoothstep(lineWidth, lineWidth - smoothDist, dist);
    float col2 = smoothstep(-lineWidth, -lineWidth + smoothDist, dist);
    return min(col1, col2);
}

// === SDF ===
float sdfCircle(vec2 coord, vec2 center, float radius) {
    return length(coord - center) - radius;
}

float sdfLineSegment(vec2 p, vec2 a, vec2 b) {
    vec2 ba = b - a;
    vec2 pa = p - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - h * ba);
}

float petal(vec2 origin, float size, vec2 uv) {
    vec2 relativeUv = uv - origin;
    vec2 circle1Center = vec2coord(PI / 3.0, size);
    vec2 circle2Center = vec2(-circle1Center.x, circle1Center.y);
    float circle1 = sdfCircle(relativeUv, circle1Center, size);
    float circle2 = sdfCircle(relativeUv, circle2Center, size);
    float circle3 = sdfCircle(relativeUv, vec2coord(0.0, size), size);

    return max(max(circle1, circle2), -circle3);
}

// === EFFECTS ===

vec2 shockwave(vec2 uv, float startVectorLength) {
    float width = 0.2;
    float strength = 0.2;
    float progress = min(max(length(uv) - startVectorLength, 0.0), width) / width;
    float deformationFactor = 1.0 + strength * sin(PI * progress + PI);
    return uv * deformationFactor;
}

vec2 rotate(vec2 uv, float angle) {
    return cos(atan(uv.y, uv.x) + angle + vec2(0.0, -0.5 * PI)) * length(uv);
}

vec2 sixSegmentSymmetry(vec2 uv) {
    return length(uv) * cos((mod(atan(uv.y / uv.x) - PI / 3.0, PI / 3.0) + PI / 3.0) + vec2(0.0, -0.5 * PI));
}

// === MAIN ===

void main() {
    vec2 fragCoord = TexCoords * iResolution;

    vec2 uv = (2.0 * fragCoord - iResolution.xy) / shortestWidth;

    uv = rotate(uv, iTime);
    uv = sixSegmentSymmetry(uv);
    uv = shockwave(uv, mod(iTime, 5.0) - 0.5);

    float timeLoop = 2.5 - abs(mod(iTime, 5.0) - 2.5);
    float size = (sin(min(timeLoop * 1.5, 0.6 * PI) * 2.0 - 0.5 * PI) + 1.0) / 4.0;
    float lineWidth = max(0.02 * size, 0.004);

    float petalShape = petal(vec2(0, 0), size, uv);
    float petalCol = outline(petalShape, lineWidth);

    float hexagonHeight = 1.5 * size;
    float hexagon = max(uv.y - hexagonHeight, -petalShape);

    vec2 topCircleCenter = vec2(0.0, size * 3.0);
    float rightCornerDistance = hexagonHeight / sin(PI / 3.0);
    vec2 rightCorner = vec2coord(PI / 3.0, rightCornerDistance);
    float topCircleRadius = length(topCircleCenter - rightCorner);
    float topCircle = sdfCircle(uv, topCircleCenter, topCircleRadius);

    float hexagonSegment = max(hexagon, topCircle);
    float hexagonSegmentCol = smoothstep(smoothDist / 2.0, -smoothDist / 2.0, hexagonSegment);

    float line1 = sdfLineSegment(abs(uv), vec2coord(PI / 3.0, size), vec2coord(PI / 3.0, rightCornerDistance * 0.8));
    float line2 = sdfLineSegment(abs(uv) - vec2coord((5.0 / 6.0) * PI, 0.06 * size),
                                 vec2coord(PI / 3.0, size * 1.1), vec2coord(PI / 3.0, rightCornerDistance * 0.75));
    float line3 = sdfLineSegment(abs(uv) - vec2coord((5.0 / 6.0) * PI, 0.12 * size),
                                 vec2coord(PI / 3.0, size * 1.17), vec2coord(PI / 3.0, rightCornerDistance * 0.8));
    float lineCol = outline(min(min(line1, line2), line3), lineWidth);

    float colorIntensity = max(max(petalCol, hexagonSegmentCol), lineCol);
    vec3 col = mix(vec3(1.0), vec3(0.0), colorIntensity);

    FragColor = vec4(col, 1.0);
}
