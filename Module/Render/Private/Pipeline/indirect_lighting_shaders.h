#pragma once

#include <string>
#include "Render/Public/Pipeline/indirect_lighting.h"

namespace Render {
namespace IndirectLightingDetail {
inline std::string Preamble(bool cachedGeometry = false) {
    return std::string("#version 450 core\n#define GI_CACHED_GEOMETRY ") + (cachedGeometry ? "1\n" : "0\n") +
        "#define GI_MAX_DIMENSION " + std::to_string(MaxIndirectLightingDimension) + "\n" + R"GLSL(
in vec2 vUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D giInput;
layout(binding = 1) uniform sampler2D giNormal;
layout(binding = 2) uniform sampler2D giDepth;
#if GI_CACHED_GEOMETRY
layout(binding = 3) uniform sampler2D giGeometry;
#endif
layout(std140, binding = 9) uniform IndirectLightingData {
    mat4 giProjection;
    mat4 giInverseProjection;
    mat4 giView;
    vec4 giTrace;
    ivec4 giOptions;
};
const float GI_PI = 3.14159265358979323846;
bool GIFinite(vec3 value) { return !any(isnan(value)) && !any(isinf(value)); }
ivec2 GIFullSize() { return textureSize(giDepth, 0); }
ivec2 GIHalfSize() {
    ivec2 size=(GIFullSize()+ivec2(1))/2;
    int longest=max(size.x,size.y);
    return longest>GI_MAX_DIMENSION ? max(size*GI_MAX_DIMENSION/longest,ivec2(1)) : size;
}
ivec2 GIFullPixelForHalf(ivec2 pixel) {
    // Integer mapping is deliberate: half-size centers can lie on a full-size
    // texel boundary, where float UV roundoff selects different neighbours.
    return clamp(((pixel * 2 + ivec2(1)) * GIFullSize()) / (GIHalfSize() * 2),
                 ivec2(0), GIFullSize() - ivec2(1));
}
vec3 GIPosition(ivec2 pixel, float depth) {
#if GI_CACHED_GEOMETRY
    return texelFetch(giGeometry, pixel, 0).xyz;
#else
    vec2 uv = (vec2(pixel) + 0.5) / vec2(GIFullSize());
    vec4 value = giInverseProjection * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    return abs(value.w) > 1e-8 ? value.xyz / value.w : vec3(0.0);
#endif
}
vec3 GIViewNormal(ivec2 pixel) {
    vec4 value = texelFetch(giNormal, pixel, 0);
    if (value.a < 0.5 || !GIFinite(value.xyz)) return vec3(0.0);
    vec3 normal = mat3(giView) * value.xyz;
    float magnitude = dot(normal, normal);
    return magnitude > 0.01 ? normal * inversesqrt(magnitude) : vec3(0.0);
}
bool GISurface(ivec2 pixel, out vec3 position, out vec3 normal) {
#if GI_CACHED_GEOMETRY
    vec4 value=texelFetch(giGeometry,pixel,0);
    position=value.xyz;
    normal=value.a>0.5 ? GIViewNormal(pixel) : vec3(0);
    return value.a>0.5;
#else
    float depth = texelFetch(giDepth, pixel, 0).r;
    position = GIPosition(pixel, depth);
    normal = GIViewNormal(pixel);
    return depth >= 0.0 && depth < 0.999999 && GIFinite(position);
#endif
}
bool GIProjectClip(vec4 projected, out ivec2 pixel) {
    if (projected.w <= 1e-7) return false;
    vec3 ndc = projected.xyz / projected.w;
    if (!GIFinite(ndc) || any(greaterThanEqual(abs(ndc), vec3(1.0)))) return false;
    pixel = clamp(ivec2(floor((ndc.xy * 0.5 + 0.5) * vec2(GIFullSize()))),
                  ivec2(0), GIFullSize() - ivec2(1));
    return true;
}
bool GIProject(vec3 position, out ivec2 pixel) {
    return GIProjectClip(giProjection*vec4(position,1),pixel);
}
float GISurfaceWeight(vec3 position, vec3 normal, vec3 otherPosition, vec3 otherNormal) {
    float agreement = dot(normal, otherNormal);
    if (agreement < 0.85) return 0.0;
    float sigma = max(giTrace.z * 2.0, 0.005);
    float planeDistance = max(abs(dot(otherPosition-position, normal)),
                              abs(dot(otherPosition-position, otherNormal)));
    return exp(-0.5 * planeDistance * planeDistance / (sigma * sigma)) * pow(agreement, 24.0);
}
)GLSL";
}
} // namespace IndirectLightingDetail

// Reconstruct view positions once for all ray steps and bilateral filter taps.
// RGBA32F preserves depth accuracy for thin surfaces and plane rejection.
inline std::string IndirectLightingGeometryShader() {
    return IndirectLightingDetail::Preamble() + R"GLSL(
void main() {
    ivec2 pixel = clamp(ivec2(gl_FragCoord.xy), ivec2(0), GIFullSize()-ivec2(1));
    float depth = texelFetch(giDepth, pixel, 0).r;
    vec3 position = GIPosition(pixel, depth);
    outColor = depth >= 0.0 && depth < 0.999999 && GIFinite(position) ? vec4(position,1) : vec4(0);
}
)GLSL";
}

// Slots 0/1/2: full-size direct diffuse outgoing radiance (plus emission),
// world-space normal, depth. All three use the same jittered camera. Output is
// half-size irradiance E; PBR applies diffuseReflectance / PI exactly once.
inline std::string IndirectLightingGatherShader(bool cachedGeometry = false) {
    return IndirectLightingDetail::Preamble(cachedGeometry) + R"GLSL(
uint GIHash(uvec2 pixel, uint seed) {
    uint value = pixel.x * 0x9e3779b9u ^ pixel.y * 0x85ebca6bu ^ seed;
    value = (value ^ (value >> 16)) * 0x7feb352du;
    value = (value ^ (value >> 15)) * 0x846ca68bu;
    return value ^ (value >> 16);
}
vec3 GIFilterSource(vec3 hitPosition, vec3 hitNormal, float rayLength) {
    vec4 projected = giProjection * vec4(hitPosition, 1.0);
    vec2 center = (projected.xy / projected.w * 0.5 + 0.5) * vec2(GIFullSize()) - 0.5;
    float pixelsPerUnit = 0.5 * float(GIFullSize().y) * abs(giProjection[1][1]);
    if (abs(giProjection[3][3]) < 0.5) pixelsPerUnit /= max(-hitPosition.z, 0.1);
    // A ray represents a finite angular cell. Filtering that cell's footprint
    // avoids point-sampling bright checkerboard texels into isolated GI spikes.
    float radius = clamp(rayLength * pixelsPerUnit * 0.35 / sqrt(float(max(giOptions.x, 1))), 0.75, 4.0);
    vec3 sum = vec3(0.0);
    float total = 0.0;
    for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
        float weight = float((x == 0 ? 2 : 1) * (y == 0 ? 2 : 1));
        total += weight;
        ivec2 pixel = ivec2(floor(center + vec2(x,y) * radius + 0.5));
        if (any(lessThan(pixel, ivec2(0))) || any(greaterThanEqual(pixel, GIFullSize()))) continue;
        vec3 position, normal;
        if (!GISurface(pixel, position, normal) || dot(normal, hitNormal) < 0.9) continue;
        float planeDistance = max(abs(dot(position-hitPosition, hitNormal)), abs(dot(position-hitPosition, normal)));
        if (planeDistance > max(giTrace.z * 2.0, 0.02)) continue;
        vec4 source = texelFetch(giInput, pixel, 0);
        if (source.a >= 0.5 && GIFinite(source.rgb)) sum += clamp(source.rgb, vec3(0), vec3(60000)) * weight;
    }
    // Invalid/background footprint taps are zero, not removed from the divisor.
    // A tiny emitter must not acquire a full filter footprint of energy.
    return sum / total;
}
bool GIBehindSurfaceAtPixel(vec3 rayPosition, ivec2 pixel, out vec3 surface, out vec3 normal) {
    // Reject empty/in-front samples before fetching and transforming normals.
#if GI_CACHED_GEOMETRY
    vec4 geometry=texelFetch(giGeometry,pixel,0);
    if(geometry.a<0.5) return false;
    surface=geometry.xyz;
#else
    float depth = texelFetch(giDepth,pixel,0).r;
    if (depth < 0.0 || depth >= 0.999999) return false;
    surface = GIPosition(pixel,depth);
#endif
    float viewGap = surface.z - rayPosition.z;
    if (viewGap < -giTrace.y) return false;
    normal = GIViewNormal(pixel);
    if (dot(normal, normal) < 0.5) return viewGap >= 0.0;
    // A local surface plane avoids self-hits from reconstructing a sloped
    // depth texel at its center while the ray crosses elsewhere in that texel.
    return viewGap >= -giTrace.y && dot(rayPosition - surface, normal) <= 0.0;
}
bool GIBehindSurface(vec3 rayPosition, out ivec2 pixel, out vec3 surface, out vec3 normal) {
    return GIProject(rayPosition,pixel) && GIBehindSurfaceAtPixel(rayPosition,pixel,surface,normal);
}
vec3 GITraceRay(vec3 origin, vec3 direction) {
    float previousDistance = 0.0;
    vec4 originClip=giProjection*vec4(origin,1);
    vec4 directionClip=giProjection*vec4(direction,0);
    int steps = clamp(giOptions.y, 4, 64);
    for (int step = 0; step < 64; ++step) {
        if (step >= steps) break;
        float distance = giTrace.x * float(step + 1) / float(steps);
        vec3 rayPosition = origin + direction * distance;
        ivec2 pixel;
        if (!GIProjectClip(originClip+directionClip*distance, pixel)) break;
        vec3 surface, normal;
        if (GIBehindSurfaceAtPixel(rayPosition, pixel, surface, normal)) {
            float low = previousDistance, high = distance;
            for (int refine = 0; refine < 5; ++refine) {
                float middle = (low + high) * 0.5;
                ivec2 testPixel; vec3 testSurface, testNormal;
                if (GIBehindSurface(origin + direction * middle, testPixel, testSurface, testNormal)) high = middle;
                else low = middle;
            }
            rayPosition = origin + direction * high;
            if (!GIProject(rayPosition, pixel) || !GISurface(pixel, surface, normal)) return vec3(0.0);
            // A nearest-depth layer has finite thickness. Refuse large depth
            // jumps, self intersections, missing normals and hit backfaces.
            if (abs(surface.z - rayPosition.z) > giTrace.y || high <= max(giTrace.z, 0.001) ||
                dot(normal, -direction) <= 0.05 || dot(normal, origin - surface) <= 0.0) return vec3(0.0);
            return GIFilterSource(rayPosition, normal, high);
        }
        previousDistance = distance;
    }
    return vec3(0.0);
}
void main() {
    outColor = vec4(0.0);
    if (giOptions.z == 0 || giTrace.w <= 0.0) return;
    ivec2 pixel = GIFullPixelForHalf(ivec2(gl_FragCoord.xy));
    vec3 position, normal;
    if (!GISurface(pixel, position, normal) || dot(normal, normal) < 0.5) return;
    vec3 axis = abs(normal.z) < 0.999 ? vec3(0,0,1) : vec3(0,1,0);
    vec3 tangent = normalize(cross(axis, normal));
    vec3 bitangent = cross(normal, tangent);
    vec3 origin = position + normal * max(giTrace.z, 0.0001);
    vec3 sum = vec3(0.0);
    int samples = clamp(giOptions.x, 1, 32);
    // Independently shift BOTH dimensions of the stratified hemisphere. Only
    // rotating azimuth leaves identical elevation rings on every pixel, which
    // creates wide correlated hit-count bands on flat walls. Integer avalanche
    // hashes avoid the diagonal screen-space lattice of a linear IGN seed.
    vec2 shift = vec2(GIHash(uvec2(pixel), 0x68bc21ebu) & 0x00ffffffu,
                      GIHash(uvec2(pixel), 0x02e5be93u) & 0x00ffffffu) / 16777216.0;
    // Misses remain zero in the fixed denominator; normalizing only hits would
    // turn a tiny visible emitter into a full hemisphere of illumination.
    for (int i = 0; i < 32; ++i) {
        if (i >= samples) break;
        float u = fract((float(i) + 0.5) / float(samples) + shift.x);
        float phi = float(i) * 2.39996322973 + 2.0 * GI_PI * shift.y;
        vec3 local = vec3(sqrt(u) * cos(phi), sqrt(u) * sin(phi), sqrt(1.0 - u));
        vec3 direction = tangent * local.x + bitangent * local.y + normal * local.z;
        sum += GITraceRay(origin, direction);
    }
    // Cosine-weighted sampling already accounts for receiver cosine/PDF.
    // Radiance along a ray has no extra inverse-square attenuation here.
    vec3 irradiance = sum * (GI_PI * giTrace.w / float(samples));
    outColor = vec4(clamp(irradiance, vec3(0.0), vec3(60000.0)), 1.0);
}
)GLSL";
}

// Half-size à-trous reconstruction. Ping-pong three passes with options.w set
// to 1, 2 and 4. Radiance is averaged linearly; color-distance weights would
// preserve noisy bright/dark sample-count bands instead of removing them.
inline std::string IndirectLightingDenoiseShader(bool cachedGeometry = false) {
    return IndirectLightingDetail::Preamble(cachedGeometry) + R"GLSL(
void main() {
    outColor = vec4(0.0);
    if (giOptions.z == 0) return;
    ivec2 center = clamp(ivec2(gl_FragCoord.xy), ivec2(0), GIHalfSize()-ivec2(1));
    vec3 position, normal;
    if (!GISurface(GIFullPixelForHalf(center), position, normal) || dot(normal, normal) < 0.5) return;
    int stride = clamp(giOptions.w, 1, 8);
    const float kernel[5] = float[5](1.0, 4.0, 6.0, 4.0, 1.0);
    vec3 sum = vec3(0.0);
    float weights = 0.0;
    for (int y = -2; y <= 2; ++y) for (int x = -2; x <= 2; ++x) {
        ivec2 pixel = clamp(center + ivec2(x,y) * stride, ivec2(0), GIHalfSize()-ivec2(1));
        vec4 value = texelFetch(giInput, pixel, 0);
        if (value.a < 0.5 || !GIFinite(value.rgb)) continue;
        vec3 samplePosition, sampleNormal;
        if (!GISurface(GIFullPixelForHalf(pixel), samplePosition, sampleNormal)) continue;
        float weight = kernel[x+2] * kernel[y+2] * GISurfaceWeight(position, normal, samplePosition, sampleNormal);
        sum += value.rgb * weight;
        weights += weight;
    }
    outColor = vec4(weights > 1e-6 ? sum / weights : vec3(0.0), 1.0);
}
)GLSL";
}

// Slots 0/1/2: half-size irradiance, full-size world normal and depth. The
// normal/depth guides belong to the original capture, not a resolved MSAA map.
inline std::string IndirectLightingUpsampleShader(bool cachedGeometry = false) {
    return IndirectLightingDetail::Preamble(cachedGeometry) + R"GLSL(
void main() {
    outColor = vec4(0.0);
    if (giOptions.z == 0) return;
    ivec2 pixel = clamp(ivec2(gl_FragCoord.xy), ivec2(0), GIFullSize() - ivec2(1));
    vec3 position, normal;
    if (!GISurface(pixel, position, normal) || dot(normal, normal) < 0.5) return;
    vec2 halfPosition = (vec2(pixel) + 0.5) * vec2(GIHalfSize()) / vec2(GIFullSize()) - 0.5;
    ivec2 base = ivec2(floor(halfPosition));
    vec3 sum = vec3(0.0);
    float weights = 0.0;
    // Denoising is complete. Reconstruct only the four surrounding half-size
    // samples, preserving geometric edges instead of blurring the full image.
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
        ivec2 halfPixel = clamp(base + ivec2(x,y), ivec2(0), GIHalfSize() - ivec2(1));
        ivec2 guidePixel = GIFullPixelForHalf(halfPixel);
        vec3 samplePosition, sampleNormal;
        if (!GISurface(guidePixel, samplePosition, sampleNormal) || dot(sampleNormal, sampleNormal) < 0.5) continue;
        vec4 sampleValue = texelFetch(giInput, halfPixel, 0);
        if (sampleValue.a < 0.5 || !GIFinite(sampleValue.rgb)) continue;
        vec2 distance = abs(halfPosition - vec2(base + ivec2(x,y)));
        float spatial = max(1.0-distance.x, 0.0) * max(1.0-distance.y, 0.0);
        float weight = spatial * GISurfaceWeight(position, normal, samplePosition, sampleNormal);
        sum += sampleValue.rgb * weight;
        weights += weight;
    }
    outColor = vec4(weights > 1e-6 ? sum / weights : vec3(0.0), 1.0);
}
)GLSL";
}
} // namespace Render
