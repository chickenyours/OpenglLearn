#pragma once

#include <string>
#include "Render/Public/Pipeline/temporal_effects.h"

namespace Render {
namespace TemporalEffectsDetail {
inline std::string FragmentPreamble() {
    return R"GLSL(#version 450 core
in vec2 vUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D currentColor;
layout(binding = 1) uniform sampler2D historyColor;
layout(binding = 2) uniform sampler2D currentDepth;
layout(binding = 3) uniform sampler2D historyDepth;
layout(binding = 4) uniform sampler2D opaqueColor;
layout(std140, binding = 6) uniform TemporalData {
    mat4 currentInverseViewProjection;
    mat4 previousViewProjection;
    mat4 previousInverseViewProjection;
    mat4 currentUnjitteredViewProjection;
    mat4 previousUnjitteredViewProjection;
    vec4 temporal;
    vec4 screen;
    vec4 motion;
    vec4 cameraPosition;
    vec4 reactive;
};
vec2 SafeUV(sampler2D image, vec2 uv) {
    vec2 halfTexel = 0.5 / vec2(textureSize(image, 0));
    return clamp(uv, halfTexel, vec2(1.0) - halfTexel);
}
vec4 ReadColor(sampler2D image, vec2 uv) { return texture(image, SafeUV(image, uv)); }
ivec2 DepthPixel(sampler2D image, vec2 uv) {
    ivec2 size = textureSize(image, 0);
    return clamp(ivec2(floor(uv * vec2(size))), ivec2(0), size - 1);
}
float ReadDepth(sampler2D image, vec2 uv) { return texelFetch(image, DepthPixel(image, uv), 0).r; }
bool Inside(vec2 uv) { return all(greaterThanEqual(uv, vec2(0))) && all(lessThanEqual(uv, vec2(1))); }
bool Reconstruct(mat4 inverseVP, vec2 uv, float depth, out vec3 world) {
    vec4 h = inverseVP * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1);
    if (abs(h.w) < 1e-8 || any(isnan(h)) || any(isinf(h))) return false;
    world = h.xyz / h.w;
    return !any(isnan(world)) && !any(isinf(world));
}
vec2 CurrentJitterUV() {
    // The matrices differ only by projection jitter. Applying the unjittered
    // VP to a clip-space point unprojected with jittered inverse VP removes that
    // displacement. Negating it gives the raw color/depth sampling offset.
    vec4 unjittered = currentUnjitteredViewProjection *
        (currentInverseViewProjection * vec4(0, 0, 0, 1));
    if (abs(unjittered.w) < 1e-8 || any(isnan(unjittered)) || any(isinf(unjittered))) return vec2(0);
    return -0.5 * unjittered.xy / unjittered.w;
}
vec3 FiniteHDR(vec3 color) {
    color = mix(color, vec3(0), isnan(color));
    return clamp(color, vec3(0), vec3(60000));
}
float TransparencyReactive(vec2 rawUV, vec3 color) {
    if (reactive.x < 0.5) return 0.0;
    vec3 opaque = FiniteHDR(ReadColor(opaqueColor, rawUV).rgb);
    vec3 delta = abs(FiniteHDR(color) - opaque);
    float difference = max(delta.r, max(delta.g, delta.b));
    float magnitude = max(max(color.r, max(color.g, color.b)), max(opaque.r, max(opaque.g, opaque.b)));
    return clamp(difference * reactive.y / max(magnitude, 0.1), 0.0, 1.0);
}
)GLSL";
}
} // namespace TemporalEffectsDetail

// Compact directional FXAA: luminance contrast detects edges, then bounded
// narrow/wide samples follow their direction. Operates AFTER display mapping.
// Algorithm background: NVIDIA, Timothy Lottes, FXAA White Paper (2011).
inline std::string FxaaShader() {
    return TemporalEffectsDetail::FragmentPreamble() + R"GLSL(
float Luma(vec3 color) { return dot(color, vec3(0.299, 0.587, 0.114)); }
void main() {
    vec4 center = ReadColor(currentColor, vUV);
    vec2 texel = 1.0 / vec2(textureSize(currentColor, 0));
    float nw = Luma(ReadColor(currentColor, vUV + texel * vec2(-1, 1)).rgb);
    float ne = Luma(ReadColor(currentColor, vUV + texel * vec2(1, 1)).rgb);
    float sw = Luma(ReadColor(currentColor, vUV + texel * vec2(-1, -1)).rgb);
    float se = Luma(ReadColor(currentColor, vUV + texel * vec2(1, -1)).rgb);
    float middle = Luma(center.rgb);
    float lo = min(middle, min(min(nw, ne), min(sw, se)));
    float hi = max(middle, max(max(nw, ne), max(sw, se)));
    if (hi - lo < max(screen.w, hi * screen.z)) { outColor = center; return; }
    vec2 direction = vec2(-((nw + ne) - (sw + se)), (nw + sw) - (ne + se));
    float reduction = max((nw + ne + sw + se) / 32.0, 1.0 / 128.0);
    direction *= 1.0 / (min(abs(direction.x), abs(direction.y)) + reduction);
    direction = clamp(direction, vec2(-8), vec2(8)) * texel;
    vec3 narrow = 0.5 * (ReadColor(currentColor, vUV - direction / 6.0).rgb +
                         ReadColor(currentColor, vUV + direction / 6.0).rgb);
    vec3 wide = narrow * 0.5 + 0.25 * (ReadColor(currentColor, vUV - direction * 0.5).rgb +
                                      ReadColor(currentColor, vUV + direction * 0.5).rgb);
    float wideLuma = Luma(wide);
    outColor = vec4(clamp(wideLuma < lo || wideLuma > hi ? narrow : wide, 0.0, 1.0), center.a);
}
)GLSL";
}

// HDR accumulation for static surfaces under camera motion. This is not a
// substitute for per-object velocity/reactive masks. The host invalidates
// history after material, geometry or other scene-content changes.
inline std::string TaaShader() {
    return TemporalEffectsDetail::FragmentPreamble() + R"GLSL(
vec3 ToYCoCg(vec3 color) {
    return vec3(0.25 * color.r + 0.5 * color.g + 0.25 * color.b,
                0.5 * color.r - 0.5 * color.b,
               -0.25 * color.r + 0.5 * color.g - 0.25 * color.b);
}
vec3 FromYCoCg(vec3 color) { return vec3(color.x + color.y - color.z, color.x + color.z, color.x - color.y - color.z); }
void main() {
    // Resolve to a stable display grid, including the first/history-invalid
    // frame. The raw scene was rasterized on a shifted grid and must be sampled
    // at vUV+jitter; otherwise even accepted history follows the jitter motion.
    vec2 currentUV = SafeUV(currentColor, vUV + CurrentJitterUV());
    vec4 current = ReadColor(currentColor, currentUV);
    if (reactive.x > 0.5) current.a = TransparencyReactive(currentUV, current.rgb);
    if (temporal.w < 0.5 || temporal.x <= 0.0) { outColor = current; return; }
    float depth = ReadDepth(currentDepth, currentUV);
    vec3 world;
    if (!Reconstruct(currentInverseViewProjection, currentUV, depth, world)) { outColor = current; return; }
    bool background = depth >= 0.999999;
    // An infinitely distant background responds to rotation, not camera
    // translation. Finite far-plane geometry is handled by depth rejection.
    vec4 position = background ? vec4(world - cameraPosition.xyz, 0) : vec4(world, 1);
    vec4 previousClip = previousViewProjection * position; // raw history depth grid
    vec4 historyClip = previousUnjitteredViewProjection * position; // resolved color grid
    if (previousClip.w <= 1e-8 || historyClip.w <= 1e-8 ||
        any(isnan(previousClip)) || any(isinf(previousClip)) ||
        any(isnan(historyClip)) || any(isinf(historyClip))) { outColor = current; return; }
    vec3 previousNdc = previousClip.xyz / previousClip.w;
    vec2 previousUV = previousNdc.xy * 0.5 + 0.5;
    vec3 historyNdc = historyClip.xyz / historyClip.w;
    vec2 historyUV = historyNdc.xy * 0.5 + 0.5;
    if (!Inside(previousUV) || !Inside(historyUV) ||
        (!background && (abs(previousNdc.z) > 1.0 || abs(historyNdc.z) > 1.0))) { outColor = current; return; }
    ivec2 previousPixel = DepthPixel(historyDepth, previousUV);
    vec2 previousDepthUV = (vec2(previousPixel) + 0.5) / vec2(textureSize(historyDepth, 0));
    float oldDepth = texelFetch(historyDepth, previousPixel, 0).r;
    if (background != (oldDepth >= 0.999999)) { outColor = current; return; }
    if (!background) {
        vec3 expectedWorld, oldWorld;
        // Compare along the same nearest depth texel's ray; do not compare a
        // texel-center reconstruction with an unrelated fractional UV point.
        if (!Reconstruct(previousInverseViewProjection, previousDepthUV, previousNdc.z * 0.5 + 0.5, expectedWorld) ||
            !Reconstruct(previousInverseViewProjection, previousDepthUV, oldDepth, oldWorld)) { outColor = current; return; }
        float tolerance = max(temporal.y, 0.0) + max(temporal.z, 0.0) * length(world - cameraPosition.xyz);
        if (length(oldWorld - expectedWorld) > tolerance) { outColor = current; return; }
    }
    vec3 lower = vec3(1e30), upper = vec3(-1e30);
    vec2 texel = 1.0 / vec2(textureSize(currentColor, 0));
    for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
        vec3 neighbor = ToYCoCg(FiniteHDR(ReadColor(currentColor, currentUV + vec2(x, y) * texel).rgb));
        lower = min(lower, neighbor); upper = max(upper, neighbor);
    }
    vec3 currentYCoCg = ToYCoCg(FiniteHDR(current.rgb));
    vec3 oldYCoCg = ToYCoCg(FiniteHDR(ReadColor(historyColor, historyUV).rgb));
    oldYCoCg = clamp(oldYCoCg, lower, upper);
    float luminanceChange = abs(oldYCoCg.x - currentYCoCg.x) / max(max(oldYCoCg.x, currentYCoCg.x), 0.01);
    float speedPixels = length((historyUV - vUV) / max(screen.xy, vec2(1e-8)));
    float weight = clamp(temporal.x, 0.0, 0.98) * (1.0 - 0.85 * clamp(luminanceChange, 0.0, 1.0));
    weight *= mix(1.0, 0.65, clamp(speedPixels / 32.0, 0.0, 1.0));
    if (reactive.x > 0.5) {
        // Both masks matter: a moving/flowing transparent surface must not leave
        // its old contribution behind after uncovering an opaque background.
        float response = max(current.a, ReadColor(historyColor, historyUV).a);
        weight *= 1.0 - clamp(response, 0.0, 1.0);
    }
    outColor = vec4(FiniteHDR(FromYCoCg(mix(currentYCoCg, oldYCoCg, weight))), current.a);
}
)GLSL";
}

// Same-size depth-only target, Depth32F recommended. Use depth test/write with
// CompareOp::Always; sample slot 2 must not alias the destination attachment.
inline std::string DepthHistoryCopyShader() {
    return R"GLSL(#version 450 core
layout(binding = 2) uniform sampler2D currentDepth;
void main() {
    ivec2 size = textureSize(currentDepth, 0);
    ivec2 pixel = clamp(ivec2(gl_FragCoord.xy), ivec2(0), size - 1);
    gl_FragDepth = texelFetch(currentDepth, pixel, 0).r;
}
)GLSL";
}

// Camera-only gather blur, derived from depth reconstruction and previous VP
// reprojection (GPU Gems 3, chapter 27). Current/previous UNJITTERED projection
// differences prevent temporal AA jitter from creating artificial motion.
inline std::string CameraMotionBlurShader() {
    return TemporalEffectsDetail::FragmentPreamble() + R"GLSL(
void main() {
    vec4 current = ReadColor(currentColor, vUV);
    // TAA color is on the display grid while depth remains raw/jittered. For a
    // raw color input the grids already match, so this extra offset is zero.
    vec2 depthOffset = cameraPosition.w > 0.5 ? CurrentJitterUV() : vec2(0);
    vec2 depthUV = SafeUV(currentDepth, vUV + depthOffset);
    float response = cameraPosition.w > 0.5 ? current.a : TransparencyReactive(depthUV, current.rgb);
    if (reactive.x > 0.5 && response > 0.05) { outColor = current; return; }
    float depth = ReadDepth(currentDepth, depthUV);
    // Background has no finite surface depth. Leave it untouched instead of
    // treating the far plane as moving scene geometry or smearing silhouettes.
    if (motion.w < 0.5 || motion.x <= 0.0 || motion.y <= 0.0 || depth >= 0.999999) { outColor = current; return; }
    vec3 world;
    if (!Reconstruct(currentInverseViewProjection, depthUV, depth, world)) { outColor = current; return; }
    vec4 currentClip = currentUnjitteredViewProjection * vec4(world, 1);
    vec4 previousClip = previousUnjitteredViewProjection * vec4(world, 1);
    if (currentClip.w <= 1e-8 || previousClip.w <= 1e-8 ||
        any(isnan(currentClip)) || any(isinf(currentClip)) || any(isnan(previousClip)) || any(isinf(previousClip))) {
        outColor = current; return;
    }
    vec2 velocityUV = (currentClip.xy / currentClip.w - previousClip.xy / previousClip.w) * 0.5 * motion.x;
    vec2 pixelVelocity = velocityUV / max(screen.xy, vec2(1e-8));
    float lengthPixels = length(pixelVelocity);
    if (lengthPixels < 0.25) { outColor = current; return; }
    velocityUV *= min(1.0, motion.y / max(lengthPixels, 1e-6));
    int sampleCount = int(clamp(round(motion.z), 2.0, 32.0));
    float centerDistance = length(world - cameraPosition.xyz);
    float tolerance = max(temporal.y, 0.001) + max(temporal.z, 0.0) * centerDistance;
    vec3 sum = FiniteHDR(current.rgb);
    float weights = 1.0;
    for (int i = 0; i < 32; ++i) {
        if (i >= sampleCount) break;
        float position = (float(i) + 0.5) / float(sampleCount) - 0.5;
        vec2 uv = vUV + velocityUV * position;
        if (!Inside(uv)) continue;
        if (reactive.x > 0.5) {
            vec4 candidate = ReadColor(currentColor, uv);
            float mask = cameraPosition.w > 0.5 ? candidate.a : TransparencyReactive(uv, candidate.rgb);
            if (mask > 0.05) continue;
        }
        vec2 sampleDepthUV = SafeUV(currentDepth, uv + depthOffset);
        float sampleDepth = ReadDepth(currentDepth, sampleDepthUV);
        if (sampleDepth >= 0.999999) continue;
        vec3 sampleWorld;
        if (!Reconstruct(currentInverseViewProjection, sampleDepthUV, sampleDepth, sampleWorld)) continue;
        float difference = abs(length(sampleWorld - cameraPosition.xyz) - centerDistance);
        float weight = 1.0 - smoothstep(tolerance * 0.5, tolerance, difference);
        sum += FiniteHDR(ReadColor(currentColor, uv).rgb) * weight;
        weights += weight;
    }
    outColor = vec4(FiniteHDR(sum / weights), current.a);
}
)GLSL";
}

} // namespace Render
