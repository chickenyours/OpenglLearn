#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

#include <glm/glm.hpp>

#include "Render/Public/Material/pbr_material.h"

namespace Render::Material {

inline constexpr std::uint32_t ToonPassBinding = 3;
inline constexpr std::uint32_t ToonShadowTextureBinding = 8;

// Toon keeps the imported MaterialData and texture slots intact. It changes
// the lighting model, rather than replacing the material asset or its atlas.
// Colors below are linear multipliers, except outlineColorAndWidth.rgb, which
// is an authored sRGB ink color. This pass writes display-encoded RGBA8.
struct alignas(16) ToonPassConstants {
    glm::mat4 lightViewProjection{1.0f};
    // xyz points FROM the surface TO the directional light; w enables shadows.
    glm::vec4 lightDirectionAndShadow{-0.4f, 0.8f, 0.6f, 1.0f};
    // rgb = colored shade multiplier; w = strength of the normal-based band.
    glm::vec4 shadowTintAndStrength{0.62f, 0.60f, 0.74f, 0.65f};
    // Band threshold, transition width, normal wrap, small stylized highlight.
    glm::vec4 shading{0.08f, 0.025f, 0.22f, 0.025f};
    // Ink is display sRGB. Width is measured in physical viewport pixels.
    glm::vec4 outlineColorAndWidth{0.075f, 0.065f, 0.105f, 0.9f};
    // Viewport width/height, shadow normalized-depth bias, normal world bias.
    glm::vec4 viewportAndBias{1280.0f, 960.0f, 0.00015f, 0.002f};
    // Character cast-shadow strength, floor shadow strength, highlight edge,
    // reserved. Floor is white and receives shadows without casting them.
    glm::vec4 options{0.35f, 0.42f, 0.90f, 0.0f};
    glm::vec4 lightColorAndExposure{1.0f, 1.0f, 1.0f, 1.0f};
};

static_assert(std::is_standard_layout_v<ToonPassConstants>);
static_assert(std::is_trivially_copyable_v<ToonPassConstants>);
static_assert(offsetof(ToonPassConstants, lightViewProjection) == 0);
static_assert(offsetof(ToonPassConstants, lightDirectionAndShadow) == 64);
static_assert(offsetof(ToonPassConstants, shadowTintAndStrength) == 80);
static_assert(offsetof(ToonPassConstants, shading) == 96);
static_assert(offsetof(ToonPassConstants, outlineColorAndWidth) == 112);
static_assert(offsetof(ToonPassConstants, viewportAndBias) == 128);
static_assert(offsetof(ToonPassConstants, options) == 144);
static_assert(offsetof(ToonPassConstants, lightColorAndExposure) == 160);
static_assert(sizeof(ToonPassConstants) == 176);

inline std::string ToonPassGLSL() {
    return R"GLSL(
layout(std140, binding = 3) uniform ToonPassData {
    mat4 lightViewProjection;
    vec4 lightDirectionAndShadow;
    vec4 shadowTintAndStrength;
    vec4 shading;
    vec4 outlineColorAndWidth;
    vec4 viewportAndBias;
    vec4 options;
    vec4 lightColorAndExposure;
};
)GLSL";
}

inline std::string ToonViewObjectGLSL() {
    return R"GLSL(
layout(std140, binding = 0) uniform ViewData {
    mat4 uViewProjection;
    vec4 uCameraPosition;
};
layout(std140, binding = 1) uniform ObjectData { mat4 uModel; };
)GLSL";
}

inline std::string ToonCommonGLSL() {
    return R"GLSL(
vec3 ToonSafeNormalize(vec3 value, vec3 fallbackValue) {
    float squaredLength = dot(value, value);
    return squaredLength > 1e-12 ? value * inversesqrt(squaredLength) : fallbackValue;
}
vec3 ToonSRGBToLinear(vec3 value) {
    value = max(value, vec3(0));
    return mix(value / 12.92, pow((value + 0.055) / 1.055, vec3(2.4)),
               greaterThan(value, vec3(0.04045)));
}
vec3 ToonLinearToSRGB(vec3 value) {
    value = clamp(value, vec3(0), vec3(1));
    return mix(value * 12.92, 1.055 * pow(value, vec3(1.0 / 2.4)) - 0.055,
               greaterThan(value, vec3(0.0031308)));
}
)GLSL";
}

// Receiver-plane depth correction keeps the narrow PCF filter from producing
// acne on steep faces. The normal offset and comparison bias are intentionally
// small: a floating shadow is especially conspicuous on the white floor.
// worldDx/worldDy must be evaluated before alpha discard.
inline std::string ToonShadowSamplingGLSL() {
    return R"GLSL(
layout(binding = 8) uniform sampler2D shadowMap;
float ToonShadowVisibility(vec3 worldPosition, vec3 normal, vec3 worldDx, vec3 worldDy) {
    if (lightDirectionAndShadow.w < 0.5) return 1.0;
    ivec2 size = textureSize(shadowMap, 0);
    if (any(lessThan(size, ivec2(4)))) return 1.0;
    vec3 L = ToonSafeNormalize(lightDirectionAndShadow.xyz, vec3(0, 1, 0));
    float cosine = clamp(dot(normal, L), 0.0, 1.0);
    vec3 receiver = worldPosition + normal * max(viewportAndBias.w, 0.0) * (1.0 - cosine);
    vec4 projected = lightViewProjection * vec4(receiver, 1);
    if (projected.w <= 1e-8) return 1.0;
    vec3 local = projected.xyz / projected.w * 0.5 + 0.5;
    if (any(lessThan(local, vec3(0))) || any(greaterThan(local, vec3(1)))) return 1.0;
    vec4 clipDx = lightViewProjection * vec4(worldDx, 0);
    vec4 clipDy = lightViewProjection * vec4(worldDy, 0);
    vec3 dx = (clipDx.xyz * projected.w - projected.xyz * clipDx.w) *
              (0.5 / (projected.w * projected.w));
    vec3 dy = (clipDy.xyz * projected.w - projected.xyz * clipDy.w) *
              (0.5 / (projected.w * projected.w));
    float determinant = dx.x * dy.y - dx.y * dy.x;
    float derivativeScale = length(dx.xy) * length(dy.xy);
    vec2 gradient = vec2(0);
    if (abs(determinant) > max(derivativeScale * 1e-5, 1e-14)) {
        gradient = vec2(dy.y * dx.z - dx.y * dy.z, dx.x * dy.z - dy.x * dx.z) / determinant;
        if (any(isnan(gradient)) || any(isinf(gradient))) gradient = vec2(0);
    }
    vec2 pixel = local.xy * vec2(size) - 0.5;
    ivec2 first = ivec2(floor(pixel)) - ivec2(1);
    float visibility = 0.0;
    float weightSum = 0.0;
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
        ivec2 tap = clamp(first + ivec2(x, y), ivec2(0), size - 1);
        vec2 weight2 = max(vec2(2) - abs(vec2(tap) - pixel), vec2(0));
        float weight = weight2.x * weight2.y;
        vec2 sampleUV = (vec2(tap) + 0.5) / vec2(size);
        float correction = dot(gradient, sampleUV - local.xy);
        // Bound a pathological degenerate receiver without clipping ordinary
        // slopes to a fixed depth bias that would reintroduce striping.
        float maxCorrection = 2.0 * length(gradient / vec2(size)) + 1e-5;
        correction = clamp(correction, -maxCorrection, maxCorrection);
        float depth = local.z + correction - max(viewportAndBias.z, 0.0);
        visibility += (depth <= texelFetch(shadowMap, tap, 0).r ? 1.0 : 0.0) * weight;
        weightSum += weight;
    }
    return visibility / max(weightSum, 1e-6);
}
)GLSL";
}

inline std::string ToonVertexShader() {
    std::string source = R"GLSL(#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aTangent;
)GLSL";
    source += ToonViewObjectGLSL();
    source += MakePbrTemplate()->GenerateGLSLUniformBlock();
    source += ToonCommonGLSL();
    source += R"GLSL(
out vec3 vWorldPosition;
out vec3 vWorldNormal;
out vec2 vTexCoord;
out vec4 vWorldTangent;
void main() {
    vec4 world = uModel * vec4(aPosition, 1);
    mat3 linearModel = mat3(uModel);
    float modelDeterminant = determinant(linearModel);
    mat3 normalMatrix = mat3(1);
    if (abs(modelDeterminant) > 1e-12) normalMatrix = transpose(inverse(linearModel));
    vec3 N = ToonSafeNormalize(normalMatrix * aNormal, vec3(0, 1, 0));
    vec3 axis = abs(N.z) < 0.999 ? vec3(0, 0, 1) : vec3(0, 1, 0);
    vec3 fallbackTangent = ToonSafeNormalize(cross(axis, N), vec3(1, 0, 0));
    vec3 T = linearModel * aTangent.xyz;
    T = ToonSafeNormalize(T - N * dot(N, T), fallbackTangent);
    vWorldPosition = world.xyz;
    vWorldNormal = N;
    vWorldTangent = vec4(T, (aTangent.w < 0 ? -1.0 : 1.0) * (modelDeterminant < 0 ? -1.0 : 1.0));
    vTexCoord = aTexCoord * uvTransform.xy + uvTransform.zw;
    gl_Position = uViewProjection * world;
}
)GLSL";
    return source;
}

inline std::string ToonFragmentShader() {
    std::string source = R"GLSL(#version 450 core
layout(binding = 0) uniform sampler2D albedoMap;
layout(binding = 1) uniform sampler2D normalMap;
layout(binding = 7) uniform sampler2D emissiveMap;
layout(location = 0) out vec4 outColor;
)GLSL";
    source += ToonViewObjectGLSL();
    source += MakePbrTemplate()->GenerateGLSLUniformBlock();
    source += ToonPassGLSL();
    source += ToonCommonGLSL();
    source += ToonShadowSamplingGLSL();
    source += R"GLSL(
in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec2 vTexCoord;
in vec4 vWorldTangent;
void main() {
    vec3 worldDx = dFdx(vWorldPosition);
    vec3 worldDy = dFdy(vWorldPosition);
    vec4 atlas = texture(albedoMap, vTexCoord);
    float alpha = atlas.a * baseColor.a * opacity;
    vec3 N = ToonSafeNormalize(vWorldNormal, vec3(0, 1, 0));
    vec3 V = ToonSafeNormalize(uCameraPosition.xyz - vWorldPosition, N);
    if (twoSided != 0u && dot(N, V) < 0.0) N = -N;
    vec3 geometricNormal = N;
    if (useNormalMap != 0u) {
        vec3 T = ToonSafeNormalize(vWorldTangent.xyz - N * dot(vWorldTangent.xyz, N), vec3(1, 0, 0));
        vec3 B = cross(N, T) * vWorldTangent.w;
        vec3 mapped = texture(normalMap, vTexCoord).xyz * 2.0 - 1.0;
        mapped.xy *= normalScale;
        N = ToonSafeNormalize(mat3(T, B, N) * mapped, N);
    }
    float visibility = ToonShadowVisibility(vWorldPosition, geometricNormal, worldDx, worldDy);
    if (alpha < max(alphaCutoff, 0.001)) discard;
    // Decode exactly once. The atlas supplies the eyes, blush, hair glints and
    // clothing details; diffuse lighting never washes those painted colors out.
    vec3 albedo = (baseColorIsSRGB != 0u ? ToonSRGBToLinear(atlas.rgb) : atlas.rgb) * max(baseColor.rgb, vec3(0));
    vec3 L = ToonSafeNormalize(lightDirectionAndShadow.xyz, vec3(0, 1, 0));
    float ndotl = dot(N, L) + shading.z;
    float edge = max(shading.y, 0.0001);
    float band = smoothstep(shading.x - edge, shading.x + edge, ndotl);
    float normalShade = (1.0 - band) * clamp(shadowTintAndStrength.w, 0.0, 1.0);
    float castShade = (1.0 - visibility) * clamp(options.x, 0.0, 1.0);
    // Use one colored shadow band even where a cast shadow overlaps the normal
    // terminator. Multiplying both terms would make hair and face shadows muddy.
    float shade = max(normalShade, castShade);
    vec3 linearColor = albedo * mix(vec3(1), clamp(shadowTintAndStrength.rgb, vec3(0), vec3(1)), shade);
    vec3 H = ToonSafeNormalize(L + V, N);
    float shine = pow(max(dot(N, H), 0.0), 48.0);
    float highlight = smoothstep(clamp(options.z, 0.0, 0.999), 1.0, shine);
    linearColor += vec3(highlight * max(shading.w, 0.0) * band * visibility);
    linearColor *= max(lightColorAndExposure.rgb, vec3(0)) * max(lightColorAndExposure.w, 0.0);
    vec3 emission = max(emissiveColor.rgb, vec3(0)) * max(emissiveIntensity, 0.0);
    if (useEmissiveMap != 0u) emission *= texture(emissiveMap, vTexCoord).rgb;
    linearColor += emission;
    outColor = vec4(ToonLinearToSRGB(linearColor), alpha);
}
)GLSL";
    return source;
}

// Inverted-hull outline. Cull front faces (back faces for a mirrored model),
// test against the completed surface depth and disable outline depth writes.
// Extrusion is in pixel space and therefore remains thin under camera zoom.
inline std::string ToonOutlineVertexShader() {
    std::string source = R"GLSL(#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
)GLSL";
    source += ToonViewObjectGLSL();
    source += MakePbrTemplate()->GenerateGLSLUniformBlock();
    source += ToonPassGLSL();
    source += ToonCommonGLSL();
    source += R"GLSL(
out vec2 vTexCoord;
void main() {
    vec4 world = uModel * vec4(aPosition, 1);
    mat3 linearModel = mat3(uModel);
    mat3 normalMatrix = mat3(1);
    if (abs(determinant(linearModel)) > 1e-12) normalMatrix = transpose(inverse(linearModel));
    vec3 N = ToonSafeNormalize(normalMatrix * aNormal, vec3(0, 1, 0));
    vec4 clip = uViewProjection * world;
    vec4 normalClip = uViewProjection * vec4(N, 0);
    vec2 viewport = max(viewportAndBias.xy, vec2(1));
    vec2 screenNormal = (normalClip.xy * clip.w - clip.xy * normalClip.w) * viewport;
    float squaredLength = dot(screenNormal, screenNormal);
    if (clip.w > 1e-8 && squaredLength > 1e-12) {
        vec2 pixelDirection = screenNormal * inversesqrt(squaredLength);
        clip.xy += pixelDirection * (2.0 * max(outlineColorAndWidth.w, 0.0) / viewport) * clip.w;
        // Avoid ink z-fighting on coplanar shell seams. This is not a second
        // world-space expansion, so a distant model does not gain a thick line.
        clip.z += 0.000015 * clip.w;
    }
    gl_Position = clip;
    vTexCoord = aTexCoord * uvTransform.xy + uvTransform.zw;
}
)GLSL";
    return source;
}

inline std::string ToonOutlineFragmentShader() {
    std::string source = R"GLSL(#version 450 core
layout(binding = 0) uniform sampler2D albedoMap;
layout(location = 0) out vec4 outColor;
)GLSL";
    source += MakePbrTemplate()->GenerateGLSLUniformBlock();
    source += ToonPassGLSL();
    source += R"GLSL(
in vec2 vTexCoord;
void main() {
    float alpha = texture(albedoMap, vTexCoord).a * baseColor.a * opacity;
    if (alpha < max(alphaCutoff, 0.001)) discard;
    outColor = vec4(clamp(outlineColorAndWidth.rgb, vec3(0), vec3(1)), alpha);
}
)GLSL";
    return source;
}

inline std::string ToonShadowVertexShader() {
    std::string source = R"GLSL(#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aTexCoord;
layout(std140, binding = 1) uniform ObjectData { mat4 uModel; };
)GLSL";
    source += MakePbrTemplate()->GenerateGLSLUniformBlock();
    source += ToonPassGLSL();
    source += R"GLSL(
out vec2 vTexCoord;
void main() {
    gl_Position = lightViewProjection * uModel * vec4(aPosition, 1);
    vTexCoord = aTexCoord * uvTransform.xy + uvTransform.zw;
}
)GLSL";
    return source;
}

inline std::string ToonShadowFragmentShader() {
    std::string source = R"GLSL(#version 450 core
layout(binding = 0) uniform sampler2D albedoMap;
)GLSL";
    source += MakePbrTemplate()->GenerateGLSLUniformBlock();
    source += R"GLSL(
in vec2 vTexCoord;
void main() {
    float alpha = texture(albedoMap, vTexCoord).a * baseColor.a * opacity;
    if (alpha < max(alphaCutoff, 0.001)) discard;
}
)GLSL";
    return source;
}

inline std::string ToonGroundVertexShader() {
    std::string source = R"GLSL(#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
)GLSL";
    source += ToonViewObjectGLSL();
    source += ToonCommonGLSL();
    source += R"GLSL(
out vec3 vWorldPosition;
out vec3 vWorldNormal;
void main() {
    vec4 world = uModel * vec4(aPosition, 1);
    mat3 normalMatrix = mat3(1);
    if (abs(determinant(mat3(uModel))) > 1e-12) normalMatrix = transpose(inverse(mat3(uModel)));
    vWorldPosition = world.xyz;
    vWorldNormal = ToonSafeNormalize(normalMatrix * aNormal, vec3(0, 1, 0));
    gl_Position = uViewProjection * world;
}
)GLSL";
    return source;
}

inline std::string ToonGroundFragmentShader() {
    std::string source = R"GLSL(#version 450 core
layout(location = 0) out vec4 outColor;
)GLSL";
    source += ToonPassGLSL();
    source += ToonCommonGLSL();
    source += ToonShadowSamplingGLSL();
    source += R"GLSL(
in vec3 vWorldPosition;
in vec3 vWorldNormal;
void main() {
    vec3 worldDx = dFdx(vWorldPosition);
    vec3 worldDy = dFdy(vWorldPosition);
    float visibility = ToonShadowVisibility(vWorldPosition,
        ToonSafeNormalize(vWorldNormal, vec3(0, 1, 0)), worldDx, worldDy);
    vec3 linearColor = vec3(1.0 - (1.0 - visibility) * clamp(options.y, 0.0, 1.0));
    outColor = vec4(ToonLinearToSRGB(linearColor), 1);
}
)GLSL";
    return source;
}

} // namespace Render::Material
