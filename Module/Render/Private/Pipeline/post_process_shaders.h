#pragma once

#include <string>

#include "Render/Public/Pipeline/post_process.h"

namespace Render {

namespace PostProcessDetail {
inline std::string FragmentPreamble() {
    return R"GLSL(#version 450 core
in vec2 vUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D inputTexture;
layout(binding = 1) uniform sampler2D secondTexture;
layout(binding = 2) uniform sampler2D sceneDepth;
layout(std140, binding = 5) uniform PostProcessData {
    mat4 postProjection;
    mat4 postInverseProjection;
    vec4 postBloom;
    vec4 postSsao;
    vec4 postOutput;
    vec4 postScreen;
    vec4 postGrading;
    vec4 postMisc;
};
// Clamp at texel centers as well as using ClampToEdge samplers. This prevents
// screen-space kernels from depending on any inherited texture address state.
vec2 SafeUV(sampler2D image, vec2 uv) {
    vec2 halfTexel = 0.5 / vec2(textureSize(image, 0));
    return clamp(uv, halfTexel, vec2(1.0) - halfTexel);
}
vec4 ReadImage(sampler2D image, vec2 uv) {
    vec4 value = texture(image, SafeUV(image, uv));
    // External HDR materials can overflow RGBA16F. Keep NaN/Inf from spreading
    // through Bloom, tone mapping and display-space neighbourhood filters.
    value = mix(value, vec4(0.0), isnan(value));
    return clamp(value, vec4(-60000.0), vec4(60000.0));
}
vec3 SafeNormalize(vec3 value, vec3 fallbackValue) {
    float lengthSquared = dot(value, value);
    return lengthSquared > 1e-12 ? value * inversesqrt(lengthSquared) : fallbackValue;
}
ivec2 DepthTexel(vec2 uv) {
    ivec2 size = textureSize(sceneDepth, 0);
    return clamp(ivec2(floor(uv * vec2(size))), ivec2(0), size - ivec2(1));
}
float ReadDepth(vec2 uv) { return texelFetch(sceneDepth, DepthTexel(uv), 0).r; }
vec3 ViewPosition(vec2 uv, float depth) {
    // Half-resolution AO pixels do not generally coincide with depth texel
    // centers. Reconstruct the position of the exact texel read above.
    uv = (vec2(DepthTexel(uv)) + 0.5) / vec2(textureSize(sceneDepth, 0));
    vec4 homogeneous = postInverseProjection * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    return abs(homogeneous.w) > 1e-8 ? homogeneous.xyz / homogeneous.w : vec3(0.0, 0.0, -postMisc.y);
}
)GLSL";
}
}

inline std::string FullscreenVertexShader() {
    return R"GLSL(#version 450 core
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texcoord;
out vec2 vUV;
void main() {
    vUV = texcoord;
    gl_Position = vec4(position, 0.0, 1.0);
}
)GLSL";
}

inline std::string BloomExtractShader() {
    return PostProcessDetail::FragmentPreamble() + R"GLSL(
void main() {
    vec3 color = max(ReadImage(inputTexture, vUV).rgb, vec3(0.0));
    float brightness = dot(color, vec3(0.299, 0.587, 0.114));
    float threshold = max(postBloom.x, 0.0);
    // A soft knee around the legacy luminance threshold avoids abrupt rings as
    // highlights cross the cutoff. Bloom strength is applied once at composite.
    float knee = max(threshold * 0.5, 1e-4);
    float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee);
    float contribution = max(soft, brightness - threshold) / max(brightness, 1e-5);
    outColor = vec4(clamp(color * contribution, vec3(0.0), vec3(60000.0)), 1.0);
}
)GLSL";
}

inline std::string GaussianBlurShader() {
    return PostProcessDetail::FragmentPreamble() + R"GLSL(
void main() {
    vec2 texel = 1.0 / vec2(textureSize(inputTexture, 0));
    vec2 stepUV = postBloom.zw * texel;
    vec3 color = ReadImage(inputTexture, vUV).rgb * 0.4026;
    color += (ReadImage(inputTexture, vUV - stepUV).rgb + ReadImage(inputTexture, vUV + stepUV).rgb) * 0.2447;
    color += (ReadImage(inputTexture, vUV - 2.0 * stepUV).rgb + ReadImage(inputTexture, vUV + 2.0 * stepUV).rgb) * 0.0545;
    // The rounded legacy coefficients sum to 1.001, not 1. Normalize to avoid
    // accumulating brightness through a many-level Bloom pyramid.
    outColor = vec4(clamp(color / 1.001, vec3(0.0), vec3(60000.0)), 1.0);
}
)GLSL";
}

// Slot 0 is the smaller accumulated image; slot 1 is the larger current level.
// The destination must be a separate image, never either sampled attachment.
inline std::string BloomUpsampleShader() {
    return PostProcessDetail::FragmentPreamble() + R"GLSL(
void main() {
    vec2 texel = 1.0 / vec2(textureSize(inputTexture, 0));
    vec3 upsampled = vec3(0.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float weight = (x == 0 ? 2.0 : 1.0) * (y == 0 ? 2.0 : 1.0);
            upsampled += ReadImage(inputTexture, vUV + vec2(x, y) * texel).rgb * weight;
        }
    }
    // The normalized tent contributes half as much at each coarser level;
    // the user's Bloom strength is applied once by the HDR composite.
    vec3 color = ReadImage(secondTexture, vUV).rgb + upsampled / 32.0;
    outColor = vec4(clamp(color, vec3(0.0), vec3(60000.0)), 1.0);
}
)GLSL";
}

// Conventional OpenGL depth [0,1], with clear depth 1 and a non-reversed
// projection. Computes view-space normals from the nearest depth neighbours;
// no G-buffer or 1024-byte sample uniform is required. Output may be half-size.
inline std::string SsaoShader() {
    return PostProcessDetail::FragmentPreamble() + R"GLSL(
vec3 DepthNormal(vec2 uv, vec3 center) {
    vec2 texel = 1.0 / vec2(textureSize(sceneDepth, 0));
    vec2 leftUV = SafeUV(sceneDepth, uv - vec2(texel.x, 0.0));
    vec2 rightUV = SafeUV(sceneDepth, uv + vec2(texel.x, 0.0));
    vec2 downUV = SafeUV(sceneDepth, uv - vec2(0.0, texel.y));
    vec2 upUV = SafeUV(sceneDepth, uv + vec2(0.0, texel.y));
    vec3 left = ViewPosition(leftUV, ReadDepth(leftUV));
    vec3 right = ViewPosition(rightUV, ReadDepth(rightUV));
    vec3 down = ViewPosition(downUV, ReadDepth(downUV));
    vec3 up = ViewPosition(upUV, ReadDepth(upUV));
    vec3 dx = abs(center.z - left.z) < abs(right.z - center.z) ? center - left : right - center;
    vec3 dy = abs(center.z - down.z) < abs(up.z - center.z) ? center - down : up - center;
    vec3 towardsCamera = SafeNormalize(-center, vec3(0.0, 0.0, 1.0));
    vec3 normal = SafeNormalize(cross(dx, dy), towardsCamera);
    return dot(normal, towardsCamera) < 0.0 ? -normal : normal;
}
void main() {
    float depth = ReadDepth(vUV);
    if (postSsao.w < 0.5 || depth >= 0.999999) {
        outColor = vec4(1.0);
        return;
    }
    vec3 position = ViewPosition(vUV, depth);
    vec3 normal = DepthNormal(vUV, position);
    vec2 cell = mod(floor(gl_FragCoord.xy), 4.0);
    float angle = fract(sin(dot(cell, vec2(12.9898, 78.233))) * 43758.5453) * 6.28318530718;
    vec3 rotation = vec3(cos(angle), sin(angle), 0.0);
    vec3 axis = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 tangent = SafeNormalize(rotation - normal * dot(rotation, normal), SafeNormalize(cross(axis, normal), vec3(1.0, 0.0, 0.0)));
    vec3 bitangent = cross(normal, tangent);
    float radius = max(postSsao.x, 0.001);
    float bias = max(postSsao.y, 0.0);
    float occlusion = 0.0;
    float sampleCount = 0.0;
    for (int i = 0; i < 32; ++i) {
        // Fixed deterministic hemisphere, with more samples near its origin.
        float fraction = (float(i) + 0.5) / 32.0;
        float z = 0.1 + 0.9 * fract((float(i) + 0.5) * 0.61803398875);
        float radial = sqrt(max(1.0 - z * z, 0.0));
        float phi = float(i) * 2.39996322973;
        vec3 hemisphere = vec3(cos(phi) * radial, sin(phi) * radial, z);
        float scale = mix(0.1, 1.0, fraction * fraction);
        vec3 samplePosition = position + mat3(tangent, bitangent, normal) * hemisphere * (radius * scale);
        vec4 projected = postProjection * vec4(samplePosition, 1.0);
        if (projected.w <= 1e-6) continue;
        vec3 ndc = projected.xyz / projected.w;
        vec2 sampleUV = ndc.xy * 0.5 + 0.5;
        if (abs(ndc.z) > 1.0 || any(lessThan(sampleUV, vec2(0.0))) || any(greaterThan(sampleUV, vec2(1.0)))) continue;
        // A sample over the clear background is visible, not missing. Keep it
        // in the denominator so silhouettes do not amplify nearby occlusion.
        sampleCount += 1.0;
        float sampledDepth = ReadDepth(sampleUV);
        if (sampledDepth >= 0.999999) continue;
        vec3 surfacePosition = ViewPosition(sampleUV, sampledDepth);
        float rangeWeight = smoothstep(0.0, 1.0, radius / max(abs(position.z - surfacePosition.z), 1e-5));
        occlusion += (surfacePosition.z >= samplePosition.z + bias ? 1.0 : 0.0) * rangeWeight;
    }
    float ao = 1.0 - occlusion / max(sampleCount, 1.0);
    ao = pow(clamp(ao, 0.0, 1.0), max(postSsao.z, 0.01));
    outColor = vec4(vec3(ao), 1.0);
}
)GLSL";
}

// Slot 0: raw AO; slot 2: full-resolution scene depth. Bilateral weights keep
// ambient occlusion from spreading across depth discontinuities/background.
inline std::string SsaoBlurShader() {
    return PostProcessDetail::FragmentPreamble() + R"GLSL(
void main() {
    float centerDepth = ReadDepth(vUV);
    if (postSsao.w < 0.5 || centerDepth >= 0.999999) {
        outColor = vec4(1.0);
        return;
    }
    float centerZ = ViewPosition(vUV, centerDepth).z;
    vec2 texel = 1.0 / vec2(textureSize(inputTexture, 0));
    float sigmaDepth = max(postSsao.x * 0.1, 0.001);
    float total = 0.0, weights = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            vec2 sampleUV = SafeUV(inputTexture, vUV + vec2(x, y) * texel);
            float depth = ReadDepth(sampleUV);
            if (depth >= 0.999999) continue;
            float sampleZ = ViewPosition(sampleUV, depth).z;
            float deltaZ = (sampleZ - centerZ) / sigmaDepth;
            float spatial = exp(-float(x * x + y * y) / 4.0);
            float weight = spatial * exp(-0.5 * deltaZ * deltaZ);
            total += ReadImage(inputTexture, sampleUV).r * weight;
            weights += weight;
        }
    }
    float ao = weights > 1e-6 ? total / weights : ReadImage(inputTexture, vUV).r;
    outColor = vec4(vec3(clamp(ao, 0.0, 1.0)), 1.0);
}
)GLSL";
}

inline std::string CompositeShader() {
    return PostProcessDetail::FragmentPreamble() + R"GLSL(
vec3 ACESFitted(vec3 color) {
    // Matrix/fitted curve migrated from legacy shaderToy/nsea.fs. Display gamma
    // is intentionally outside this function so it is performed exactly once.
    const mat3 inputMatrix = mat3(
        0.59719, 0.07600, 0.02840,
        0.35458, 0.90834, 0.13383,
        0.04823, 0.01566, 0.83777);
    const mat3 outputMatrix = mat3(
        1.60475, -0.10208, -0.00327,
        -0.53108, 1.10813, -0.07276,
        -0.07367, -0.00605, 1.07602);
    vec3 v = inputMatrix * color;
    vec3 numerator = v * (v + 0.0245786) - 0.000090537;
    vec3 denominator = v * (0.983729 * v + 0.4329510) + 0.238081;
    return clamp(outputMatrix * (numerator / max(denominator, vec3(1e-6))), 0.0, 1.0);
}
void main() {
    vec4 scene = ReadImage(inputTexture, vUV);
    vec3 hdr = max(scene.rgb, vec3(0.0));
    if (postScreen.w > 0.5)
        hdr += max(ReadImage(secondTexture, vUV).rgb, vec3(0.0)) * max(postBloom.y, 0.0);
    hdr *= max(postOutput.x, 0.0);
    int toneMap = int(round(postOutput.z));
    vec3 display = hdr;
    if (toneMap == 0) display = hdr / (hdr + vec3(0.2));
    else if (toneMap == 1) display = ACESFitted(hdr);
    // No tone mapping means clipping to the display range. Bound this before
    // gamma so valid small gamma values cannot overflow pow on HDR highlights.
    else display = clamp(hdr, 0.0, 1.0);
    display = pow(max(display, vec3(0.0)), vec3(1.0 / max(postOutput.y, 0.1)));
    float luminance = dot(display, vec3(0.2126, 0.7152, 0.0722));
    display = mix(vec3(luminance), display, max(postGrading.x, 0.0));
    display = (display - 0.5) * max(postGrading.y, 0.0) + 0.5;
    float edge = clamp(16.0 * vUV.x * vUV.y * (1.0 - vUV.x) * (1.0 - vUV.y), 0.0, 1.0);
    float vignette = pow(edge, 0.2);
    display *= mix(1.0, vignette, clamp(postGrading.z, 0.0, 1.0));
    outColor = vec4(clamp(display, 0.0, 1.0), scene.a);
}
)GLSL";
}

// Runs on the display intermediate after tone mapping. Filters are resolution
// independent and sampling is clamped; screen kernels never read and write the
// same render target. Vignette/color grading are already applied by Composite.
inline std::string FilterShader() {
    return PostProcessDetail::FragmentPreamble() + R"GLSL(
void main() {
    vec4 source = ReadImage(inputTexture, vUV);
    vec3 color = source.rgb;
    int filterMode = int(round(postOutput.w));
    vec2 texel = 1.0 / vec2(textureSize(inputTexture, 0));
    if (filterMode == 1) color = vec3(dot(color, vec3(0.2126, 0.7152, 0.0722)));
    else if (filterMode == 2) color = vec3(1.0) - color;
    else if (filterMode == 3) {
        vec3 neighbours = ReadImage(inputTexture, vUV + vec2(texel.x, 0.0)).rgb
                        + ReadImage(inputTexture, vUV - vec2(texel.x, 0.0)).rgb
                        + ReadImage(inputTexture, vUV + vec2(0.0, texel.y)).rgb
                        + ReadImage(inputTexture, vUV - vec2(0.0, texel.y)).rgb;
        color += max(postGrading.w, 0.0) * (4.0 * color - neighbours);
    } else if (filterMode == 4) {
        vec3 embossed = vec3(0.0);
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
                embossed += ReadImage(inputTexture, vUV + vec2(x, y) * texel).rgb * float(x + y);
        color = vec3(0.5) + embossed;
    } else if (filterMode == 5) {
        vec3 laplacian = -8.0 * color;
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
                if (x != 0 || y != 0) laplacian += ReadImage(inputTexture, vUV + vec2(x, y) * texel).rgb;
        color = abs(laplacian);
    } else if (filterMode == 6) {
        color = vec3(0.0);
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                float weight = (x == 0 ? 2.0 : 1.0) * (y == 0 ? 2.0 : 1.0);
                color += ReadImage(inputTexture, vUV + vec2(x, y) * texel).rgb * (weight / 16.0);
            }
        }
    } else if (filterMode == 7) {
        vec2 radial = vUV - 0.5;
        float radius = length(radial);
        vec2 direction = radial / max(radius, 1e-6);
        float wave = sin((radius * 5.0 + postMisc.z) * 6.28318530718);
        color = ReadImage(inputTexture, vUV + direction * wave * 0.015).rgb;
    }
    outColor = vec4(clamp(color, 0.0, 1.0), source.a);
}
)GLSL";
}

} // namespace Render
