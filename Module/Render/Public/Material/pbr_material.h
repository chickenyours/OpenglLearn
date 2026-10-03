#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>

#include <glm/glm.hpp>

#include "Render/Public/Material/material.h"
#include "Render/Public/RHIResourceType/Pipeline/pipeline.h"
#include "Render/Public/Pipeline/scene_effects.h"

namespace Render::Material {

inline constexpr std::uint32_t PbrPassBinding = 3;

// View/model/material bindings remain 0/1/2. This block owns point lights and
// ambient color. Exposure/outputOptions remain for source compatibility; the
// surface shader outputs linear HDR and the output pass now applies display.
struct alignas(16) PbrPassConstants {
    std::array<glm::vec4, 4> lightPositions{
        glm::vec4(5.0f, 5.0f, 0.0f, 1.0f),
        glm::vec4(-5.0f, 5.0f, 0.0f, 1.0f),
        glm::vec4(0.0f, -5.0f, -5.0f, 1.0f),
        glm::vec4(0.0f, -5.0f, 5.0f, 1.0f)
    };
    std::array<glm::vec4, 4> lightColors{
        glm::vec4(100.0f, 100.0f, 100.0f, 0.0f),
        glm::vec4(100.0f, 100.0f, 100.0f, 0.0f),
        glm::vec4(100.0f, 100.0f, 100.0f, 0.0f),
        glm::vec4(100.0f, 100.0f, 100.0f, 0.0f)
    };
    glm::vec4 ambientAndExposure{0.03f, 0.03f, 0.03f, 1.0f};
    // Legacy output defaults retained for source compatibility, all unused by
    // this surface shader: denominator, gamma, encoding switch, reserved.
    glm::vec4 outputOptions{0.2f, 2.2f, 1.0f, 0.0f};
    // Screen-space normal variance scale, maximum added alpha-squared, enabled,
    // reserved. A bounded isotropic NDF filter, independent of material storage.
    glm::vec4 specularAA{0.15f, 0.20f, 1.0f, 0.0f};
};

static_assert(std::is_standard_layout_v<PbrPassConstants>);
static_assert(std::is_trivially_copyable_v<PbrPassConstants>);
static_assert(offsetof(PbrPassConstants, lightPositions) == 0);
static_assert(offsetof(PbrPassConstants, lightColors) == 64);
static_assert(offsetof(PbrPassConstants, ambientAndExposure) == 128);
static_assert(offsetof(PbrPassConstants, outputOptions) == 144);
static_assert(offsetof(PbrPassConstants, specularAA) == 160);
static_assert(sizeof(PbrPassConstants) == 176);

inline std::shared_ptr<const Render::Material::MaterialTemplate> MakePbrTemplate() {
    using namespace Render::Material;
    MaterialTemplateDesc desc;
    desc.name = "StandardPbr";
    desc.domain = Domain::Surface;
    desc.parameters = {
        {"baseColor", ParameterType::Vec4, glm::vec4(1.0f), "Base color (linear)", "Surface",
         0.0f, 1.0f, true},
        {"metallic", ParameterType::Float, 0.0f, "Metallic", "Surface", 0.0f, 1.0f, true},
        {"roughness", ParameterType::Float, 0.5f, "Roughness", "Surface", 0.0f, 1.0f, true},
        {"ao", ParameterType::Float, 1.0f, "Ambient occlusion", "Surface", 0.0f, 1.0f, true},
        {"normalScale", ParameterType::Float, 1.0f, "Normal strength", "Surface", 0.0f, 4.0f, true},
        {"useNormalMap", ParameterType::Bool, false, "Use normal map", "Textures"},
        {"useMetallicMap", ParameterType::Bool, false, "Use metallic map", "Textures"},
        {"useRoughnessMap", ParameterType::Bool, false, "Use roughness map", "Textures"},
        {"useAoMap", ParameterType::Bool, false, "Use AO map", "Textures"},
        {"baseColorIsSRGB", ParameterType::Bool, true, "Decode albedo texture from sRGB", "Textures"},
        {"uvTransform", ParameterType::Vec4, glm::vec4(1.0f, 1.0f, 0.0f, 0.0f),
         "UV scale and offset", "Surface", std::nullopt, std::nullopt, true},
        {"emissiveColor", ParameterType::Vec4, glm::vec4(0.0f), "Emissive radiance", "Surface",
         0.0f, std::nullopt, true},
        {"reflectionStrength", ParameterType::Float, 0.0f, "Planar reflection", "Surface", 0.0f, 1.0f, true},
        {"alphaCutoff", ParameterType::Float, 0.0f, "Alpha cutoff", "Surface", 0.0f, 1.0f, true}
    };
    // All slots are required even while their switches are off. The host binds
    // white to missing color/scalar maps and a flat normal to a missing normal map.
    desc.textures = {
        {"albedoMap", 0, true}, {"normalMap", 1, true}, {"metallicMap", 2, true},
        {"roughnessMap", 3, true}, {"aoMap", 4, true}
    };
    return MaterialTemplate::Create(std::move(desc));
}

inline Render::VertexLayout PbrVertexLayout() {
    // Packed floats: position3, normal3, uv2, tangent4 = 48 bytes. Tangent.w
    // stores the mesh's bitangent handedness; the model determinant also applies.
    return {{Render::VertexFieldType::Vec3, Render::VertexFieldType::Vec3,
             Render::VertexFieldType::Vec2, Render::VertexFieldType::Vec4}};
}

inline Render::PipelineSpec PbrPipelineSpec(
    Render::RenderResourceHandle<Render::ShaderProgramSpec> program) {
    Render::PipelineSpec spec;
    spec.shaderProgram = program;
    spec.expectVertexLayout = PbrVertexLayout();
    spec.depthTest = true;
    spec.depthWrite = true;
    spec.blendEnable = false;
    spec.blendMode = Render::BlendMode::Opaque;
    // Supports mirrored/nonuniformly scaled objects and reflected camera views:
    // a negative determinant must not hide a mesh's reversed winding.
    spec.cullMode = Render::CullMode::None;
    return spec;
}

inline std::string PbrVertexShader() {
    std::string source = R"GLSL(#version 450 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aTangent;

layout(std140, binding = 0) uniform ViewData {
    mat4 uViewProjection;
    vec4 uCameraPosition;
};
layout(std140, binding = 1) uniform ObjectData {
    mat4 uModel;
};
)GLSL";
    source += MakePbrTemplate()->GenerateGLSLUniformBlock();
    source += R"GLSL(
out vec3 vWorldPosition;
out vec3 vWorldNormal;
out vec2 vTexCoord;
out vec4 vWorldTangent;

vec3 SafeNormalize(vec3 value, vec3 fallbackValue) {
    float squaredLength = dot(value, value);
    return squaredLength > 1e-12 ? value * inversesqrt(squaredLength) : fallbackValue;
}

void main() {
    vec4 worldPosition = uModel * vec4(aPosition, 1.0);
    mat3 modelLinear = mat3(uModel);
    float modelDeterminant = determinant(modelLinear);
    mat3 normalMatrix = mat3(1.0);
    // A singular model has no mathematical normal transform; keep the fallback
    // finite. Every nonsingular scale, including a reflection, uses inverse-T.
    if (abs(modelDeterminant) > 1e-12)
        normalMatrix = transpose(inverse(modelLinear));
    vec3 N = SafeNormalize(normalMatrix * aNormal, vec3(0.0, 0.0, 1.0));
    vec3 tangentAxis = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 fallbackTangent = SafeNormalize(cross(tangentAxis, N), vec3(1.0, 0.0, 0.0));
    vec3 T = modelLinear * aTangent.xyz;
    T = SafeNormalize(T - N * dot(N, T), fallbackTangent);
    float handedness = (aTangent.w < 0.0 ? -1.0 : 1.0) *
                       (modelDeterminant < 0.0 ? -1.0 : 1.0);

    vWorldPosition = worldPosition.xyz;
    vWorldNormal = N;
    vWorldTangent = vec4(T, handedness);
    vTexCoord = aTexCoord * uvTransform.xy + uvTransform.zw;
    gl_Position = uViewProjection * worldPosition;
}
)GLSL";
    return source;
}

inline std::string PbrSceneEffectsGLSL() {
    return R"GLSL(
layout(std140, binding = 4) uniform SceneEffectsData {
    mat4 lightViewProjection[4];
    mat4 mainView;
    mat4 reflectionViewProjection;
    vec4 cascadeSplits;
    vec4 sunDirectionIntensity;
    vec4 sunColor;
    vec4 clipPlane;
    vec4 effects;
    vec4 shadowParams;
    vec4 screenAndAo;
    vec4 cascadeWorldTexelSize;
    vec4 cascadeInverseDepthRange;
    vec4 shadowFilter;
};
)GLSL";
}

// Reusable directional CSM sampling. Requires SceneEffectsData and a sampler2D
// named shadowAtlas; it does not depend on material varyings or normal mapping.
// Derivatives must be evaluated by the caller BEFORE discard/divergent control
// flow, then passed as worldDx/worldDy. The light projections are orthographic.
inline std::string PbrDirectionalShadowGLSL() {
    return R"GLSL(
vec3 ShadowSafeNormalize(vec3 value, vec3 fallbackValue) {
    float squaredLength = dot(value, value);
    return squaredLength > 1e-12 ? value * inversesqrt(squaredLength) : fallbackValue;
}

int DirectionalCascadeIndex(float viewDepth) {
    for (int i = 0; i < 3; ++i) if (viewDepth < cascadeSplits[i]) return i;
    return 3;
}

float CascadeVisibility(int cascade, vec3 worldPosition, vec3 geometricNormal,
                        vec3 worldDx, vec3 worldDy) {
    ivec2 atlasSize = textureSize(shadowAtlas, 0);
    if (atlasSize.x < 8 || atlasSize.x != atlasSize.y) return 1.0;
    int tileSize = atlasSize.x / 2;
    float worldTexel = max(cascadeWorldTexelSize[cascade], 1e-8);
    float depthPerWorld = max(cascadeInverseDepthRange[cascade], 1e-8);
    float depthPerTexel = worldTexel * depthPerWorld;
    vec3 L = -ShadowSafeNormalize(sunDirectionIntensity.xyz, vec3(0, -1, 0));
    float NgDotL = clamp(dot(geometricNormal, L), 0.0, 1.0);
    float grazing = sqrt(max(1.0 - NgDotL * NgDotL, 0.0));
    // The legacy extras keep their original normalized-depth/world units.
    // New small offsets scale with this cascade's actual world-space texel.
    float normalOffset = max(shadowParams.y, 0.0) + max(shadowFilter.y, 0.0) * worldTexel * grazing;
    vec4 lightPosition = lightViewProjection[cascade] *
        vec4(worldPosition + geometricNormal * normalOffset, 1.0);
    if (abs(lightPosition.w) < 1e-8 || any(isnan(lightPosition)) || any(isinf(lightPosition))) return 1.0;
    vec3 local = lightPosition.xyz / lightPosition.w * 0.5 + 0.5;
    if (any(lessThan(local, vec3(0))) || any(greaterThan(local, vec3(1)))) return 1.0;

    vec4 lightDx = lightViewProjection[cascade] * vec4(worldDx, 0);
    vec4 lightDy = lightViewProjection[cascade] * vec4(worldDy, 0);
    float reciprocalW2 = 0.5 / (lightPosition.w * lightPosition.w);
    vec3 dx = (lightDx.xyz * lightPosition.w - lightPosition.xyz * lightDx.w) * reciprocalW2;
    vec3 dy = (lightDy.xyz * lightPosition.w - lightPosition.xyz * lightDy.w) * reciprocalW2;
    float determinant = dx.x * dy.y - dx.y * dy.x;
    float derivativeScale = length(dx.xy) * length(dy.xy);
    bool reliablePlane = abs(determinant) > max(derivativeScale * 1e-5, 1e-15);
    vec3 faceNormal = ShadowSafeNormalize(cross(worldDx, worldDy), geometricNormal);
    float faceCosine = abs(dot(faceNormal, L));
    // Extremely grazing/degenerate triangles cannot provide a stable light-
    // space plane. Ordinary steep receivers still get their exact correction.
    reliablePlane = reliablePlane && faceCosine > 0.001;
    vec2 depthGradient = vec2(0);
    if (reliablePlane) {
        depthGradient = vec2(dy.y * dx.z - dx.y * dy.z, dx.x * dy.z - dy.x * dx.z) / determinant;
        reliablePlane = !any(isnan(depthGradient)) && !any(isinf(depthGradient));
        if (!reliablePlane) depthGradient = vec2(0);
    }
    float depthBias = max(shadowParams.x, 0.0) + max(shadowFilter.x, 0.0) * depthPerTexel;
    // A near-singular projected plane cannot supply a useful gradient. Keep
    // its fallback bounded to a quarter texel instead of a large slope bias.
    if (!reliablePlane) depthBias += 0.25 * depthPerTexel;
    float faceSlope = sqrt(max(1.0 - faceCosine * faceCosine, 0.0)) / max(faceCosine, 0.001);
    // A fixed depth limit clips valid NEGATIVE corrections on sloped surfaces
    // and reintroduces acne. Scale by the true triangle slope: default 4 covers
    // the complete tent footprint (<2*sqrt(2) texels), without increasing bias.
    float correctionLimit = max(shadowFilter.w, 0.0) * depthPerTexel * max(1.0, faceSlope);

    ivec2 tileOrigin = ivec2(cascade % 2, cascade / 2) * tileSize;
    vec2 texelPosition = vec2(tileOrigin) + local.xy * float(tileSize) - 0.5;
    ivec2 firstTap = ivec2(floor(texelPosition)) - ivec2(1);
    float visibility = 0.0, weightSum = 0.0;
    // A tent centered on the fractional receiver position. Its four weights
    // per axis vary continuously with phase; compare individual exact texel
    // depths BEFORE weighting. Integer phases reduce to the familiar 1:2:1.
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
        ivec2 candidate = firstTap + ivec2(x, y);
        vec2 axisWeight = max(vec2(2.0) - abs(vec2(candidate) - texelPosition), vec2(0));
        float weight = axisWeight.x * axisWeight.y;
        ivec2 tap = clamp(candidate, tileOrigin, tileOrigin + ivec2(tileSize - 1));
        vec2 tapLocal = (vec2(tap - tileOrigin) + 0.5) / float(tileSize);
        // Fit the receiver plane at the sampled texel CENTER, including the
        // fractional phase and tile-edge clamping. No adjacent cascade leaks.
        float correction = clamp(dot(depthGradient, tapLocal - local.xy), -correctionLimit, correctionLimit);
        float receiverDepth = local.z + correction - depthBias;
        float storedDepth = texelFetch(shadowAtlas, tap, 0).r;
        visibility += (receiverDepth <= storedDepth ? 1.0 : 0.0) * weight;
        weightSum += weight;
    }
    return visibility / max(weightSum, 1e-6);
}

float DirectionalVisibility(vec3 worldPosition, vec3 geometricNormal, vec3 worldDx, vec3 worldDy) {
    if (effects.x < 0.5) return 1.0;
    float viewDepth = -(mainView * vec4(worldPosition, 1)).z;
    if (viewDepth <= 0.0 || viewDepth >= cascadeSplits.w) return 1.0;
    int cascade = DirectionalCascadeIndex(viewDepth);
    vec3 normal = ShadowSafeNormalize(geometricNormal, vec3(0, 1, 0));
    float visibility = CascadeVisibility(cascade, worldPosition, normal, worldDx, worldDy);
    if (cascade < 3) {
        float previousSplit = cascade == 0 ? 0.0 : cascadeSplits[cascade - 1];
        float width = (cascadeSplits[cascade] - previousSplit) * clamp(shadowParams.z, 0.0, 0.45);
        if (width > 1e-6 && viewDepth > cascadeSplits[cascade] - width) {
            float next = CascadeVisibility(cascade + 1, worldPosition, normal, worldDx, worldDy);
            visibility = mix(visibility, next, smoothstep(cascadeSplits[cascade] - width,
                                                        cascadeSplits[cascade], viewDepth));
        }
    }
    float fadeWidth = cascadeSplits.w * clamp(shadowFilter.z, 0.0, 0.5);
    if (fadeWidth > 1e-6)
        visibility = mix(visibility, 1.0, smoothstep(cascadeSplits.w - fadeWidth, cascadeSplits.w, viewDepth));
    return visibility;
}

vec3 DirectionalCascadeColor(vec3 worldPosition) {
    float viewDepth = -(mainView * vec4(worldPosition, 1)).z;
    if (effects.x < 0.5 || viewDepth <= 0.0 || viewDepth >= cascadeSplits.w) return vec3(0.15);
    const vec3 colors[4] = vec3[4](vec3(1, 0.15, 0.10), vec3(0.1, 0.8, 0.2),
                                 vec3(0.1, 0.35, 1), vec3(1, 0.8, 0.1));
    return colors[DirectionalCascadeIndex(viewDepth)];
}
)GLSL";
}

inline std::string PbrFragmentShader() {
    std::string source = R"GLSL(#version 450 core

layout(binding = 0) uniform sampler2D albedoMap;
layout(binding = 1) uniform sampler2D normalMap;
layout(binding = 2) uniform sampler2D metallicMap;
layout(binding = 3) uniform sampler2D roughnessMap;
layout(binding = 4) uniform sampler2D aoMap;
layout(binding = 12) uniform sampler2D shadowAtlas;
layout(binding = 13) uniform sampler2D reflectionColor;
layout(binding = 14) uniform sampler2D ambientOcclusion;

layout(std140, binding = 0) uniform ViewData {
    mat4 uViewProjection;
    vec4 uCameraPosition;
};
layout(std140, binding = 3) uniform PbrPassData {
    vec4 lightPositions[4];
    vec4 lightColors[4];
    vec4 ambientAndExposure;
    vec4 outputOptions;
    vec4 specularAA;
};
)GLSL";
    source += MakePbrTemplate()->GenerateGLSLUniformBlock();
    source += PbrSceneEffectsGLSL();
    source += PbrDirectionalShadowGLSL();
    source += R"GLSL(
in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec2 vTexCoord;
in vec4 vWorldTangent;
layout(location = 0) out vec4 outColor;

const float PI = 3.14159265358979323846;

vec3 SafeNormalize(vec3 value, vec3 fallbackValue) {
    float squaredLength = dot(value, value);
    return squaredLength > 1e-12 ? value * inversesqrt(squaredLength) : fallbackValue;
}

vec3 SRGBToLinear(vec3 encoded) {
    encoded = max(encoded, vec3(0.0));
    vec3 lower = encoded / 12.92;
    vec3 upper = pow((encoded + vec3(0.055)) / 1.055, vec3(2.4));
    return mix(upper, lower, lessThanEqual(encoded, vec3(0.04045)));
}

float DistributionGGX(vec3 N, vec3 H, float surfaceRoughness) {
    float alpha = surfaceRoughness * surfaceRoughness;
    float alphaSquared = alpha * alpha;
    float NdotH = max(dot(N, H), 0.0);
    float denominator = NdotH * NdotH * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / (PI * max(denominator * denominator, 1e-12));
}

// Tokuyoshi/Kaplanyan 2019, conservative isotropic NDF filtering (section 4.5).
// https://yusuketokuyoshi.com/papers/2019/ImprovedGeometricSpecularAA.pdf
// Perceptual roughness r maps to GGX alpha=r*r, so variance is added to r^4.
// This addresses resolved shading-normal variation, not unresolved normal-map
// mip variance, silhouette coverage or shadow-terminator geometry mismatch.
float FilterSpecularRoughness(float perceptualRoughness, vec3 normalDx, vec3 normalDy) {
    if (specularAA.z < 0.5 || specularAA.x <= 0.0 || specularAA.y <= 0.0)
        return perceptualRoughness;
    float variance = clamp(specularAA.x, 0.0, 1.0) *
        (dot(normalDx, normalDx) + dot(normalDy, normalDy));
    if (isnan(variance) || isinf(variance) || variance <= 0.0)
        return perceptualRoughness;
    float kernelAlphaSquared = min(2.0 * variance, clamp(specularAA.y, 0.0, 1.0));
    float alpha = perceptualRoughness * perceptualRoughness;
    return sqrt(sqrt(min(alpha * alpha + kernelAlphaSquared, 1.0)));
}

float GeometrySchlickGGX(float NdotDirection, float surfaceRoughness) {
    float r = surfaceRoughness + 1.0;
    float k = r * r / 8.0;
    return NdotDirection / max(NdotDirection * (1.0 - k) + k, 1e-6);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float surfaceRoughness) {
    return GeometrySchlickGGX(max(dot(N, V), 0.0), surfaceRoughness) *
           GeometrySchlickGGX(max(dot(N, L), 0.0), surfaceRoughness);
}

vec3 FresnelSchlick(float cosine, vec3 F0) {
    return F0 + (vec3(1.0) - F0) * pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}

vec3 SurfaceNormal() {
    vec3 N = SafeNormalize(vWorldNormal, vec3(0.0, 0.0, 1.0));
    if (useNormalMap == 0u) return N;
    vec3 tangentAxis = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 fallbackTangent = SafeNormalize(cross(tangentAxis, N), vec3(1.0, 0.0, 0.0));
    vec3 T = SafeNormalize(vWorldTangent.xyz - N * dot(N, vWorldTangent.xyz), fallbackTangent);
    vec3 B = cross(N, T) * (vWorldTangent.w < 0.0 ? -1.0 : 1.0);
    vec3 tangentNormal = texture(normalMap, vTexCoord).xyz * 2.0 - 1.0;
    tangentNormal.xy *= normalScale;
    tangentNormal = SafeNormalize(tangentNormal, vec3(0.0, 0.0, 1.0));
    return SafeNormalize(mat3(T, B, N) * tangentNormal, N);
}

// Produces scene-linear HDR radiance. It deliberately contains no exposure,
// tone mapping or output encoding; those belong to the pipeline output pass.
vec3 EvaluatePbrLighting(vec3 albedo, float surfaceMetallic,
                         float surfaceRoughness, float surfaceAo, vec3 N,
                         vec3 geometricNormal, float sunVisibility) {
    vec3 V = SafeNormalize(uCameraPosition.xyz - vWorldPosition, N);
    vec3 F0 = mix(vec3(0.05), albedo, surfaceMetallic); // preserve the legacy dielectric F0
    vec3 directLighting = vec3(0.0);
    for (int lightIndex = 0; lightIndex < 4; ++lightIndex) {
        vec3 toLight = lightPositions[lightIndex].xyz - vWorldPosition;
        float distanceSquared = max(dot(toLight, toLight), 1e-4);
        vec3 L = SafeNormalize(toLight, N);
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0) continue;
        vec3 H = SafeNormalize(V + L, N);
        vec3 radiance = max(lightColors[lightIndex].rgb, vec3(0.0)) / distanceSquared;
        float distribution = DistributionGGX(N, H, surfaceRoughness);
        float geometry = GeometrySmith(N, V, L, surfaceRoughness);
        vec3 fresnel = FresnelSchlick(max(dot(H, V), 0.0), F0);
        vec3 specular = distribution * geometry * fresnel /
            max(4.0 * max(dot(N, V), 0.0) * NdotL, 1e-4);
        vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - surfaceMetallic);
        directLighting += (diffuseWeight * albedo / PI + specular) * radiance * NdotL;
    }
    vec3 L = -SafeNormalize(sunDirectionIntensity.xyz, vec3(0.0, -1.0, 0.0));
    float NdotL = max(dot(N, L), 0.0);
    // A normal map cannot admit sun from below the mesh's geometric horizon.
    // A short smooth transition avoids a hard normal-map terminator boundary.
    float geometricHorizon = smoothstep(0.0, 0.05, dot(geometricNormal, L));
    if (NdotL > 0.0 && geometricHorizon > 0.0 && sunDirectionIntensity.w > 0.0) {
        vec3 H = SafeNormalize(V + L, N);
        vec3 fresnel = FresnelSchlick(max(dot(H, V), 0.0), F0);
        vec3 specular = DistributionGGX(N, H, surfaceRoughness) *
            GeometrySmith(N, V, L, surfaceRoughness) * fresnel /
            max(4.0 * max(dot(N, V), 0.0) * NdotL, 1e-4);
        vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - surfaceMetallic);
        vec3 radiance = max(sunColor.rgb, vec3(0.0)) * sunDirectionIntensity.w;
        directLighting += (diffuseWeight * albedo / PI + specular) * radiance * NdotL *
                          sunVisibility * geometricHorizon;
    }
    float screenAo = screenAndAo.z > 0.5 ?
        clamp(texture(ambientOcclusion, gl_FragCoord.xy * screenAndAo.xy).r, 0.0, 1.0) : 1.0;
    // AO affects only ambient lighting. Shadow visibility affects only the sun;
    // unrelated point lights, emission and ambient must not be darkened by CSM.
    return max(ambientAndExposure.rgb, vec3(0.0)) * albedo * surfaceAo * screenAo + directLighting;
}

void main() {
    // Derivatives and shadow receiver-plane evaluation precede all discard and
    // divergent cascade/BRDF decisions, so neighbouring helper lanes exist.
    vec3 worldDx = dFdx(vWorldPosition);
    vec3 worldDy = dFdy(vWorldPosition);
    vec3 geometricNormal = SafeNormalize(vWorldNormal, vec3(0, 1, 0));
    // The map switch is draw-uniform. Obtain normalized shading normals and
    // their derivatives before shadow/BRDF divergence or alpha/plane discard.
    vec3 shadingNormal = SurfaceNormal();
    vec3 normalDx = dFdx(shadingNormal);
    vec3 normalDy = dFdy(shadingNormal);
    float sunVisibility = DirectionalVisibility(vWorldPosition, geometricNormal, worldDx, worldDy);
    vec4 albedoSample = texture(albedoMap, vTexCoord);
    float alpha = albedoSample.a * baseColor.a;
    if (alpha < alphaCutoff) discard;
    if (effects.z > 0.5 && dot(clipPlane, vec4(vWorldPosition, 1.0)) < 0.0) discard;
    if (screenAndAo.w > 0.5) {
        vec3 diagnostic = screenAndAo.w < 1.5 ? vec3(sunVisibility) : DirectionalCascadeColor(vWorldPosition);
        outColor = vec4(diagnostic, 1);
        return;
    }
    // Only the color texture is optionally decoded. The baseColor factor and
    // normal/metallic/roughness/AO samples are always interpreted as linear data.
    vec3 albedo = (baseColorIsSRGB != 0u ? SRGBToLinear(albedoSample.rgb) : albedoSample.rgb) * baseColor.rgb;
    float surfaceMetallic = clamp(useMetallicMap != 0u ? texture(metallicMap, vTexCoord).r : metallic, 0.0, 1.0);
    float surfaceRoughness = clamp(useRoughnessMap != 0u ? texture(roughnessMap, vTexCoord).r : roughness, 0.045, 1.0);
    surfaceRoughness = FilterSpecularRoughness(surfaceRoughness, normalDx, normalDy);
    float surfaceAo = clamp(useAoMap != 0u ? texture(aoMap, vTexCoord).r : ao, 0.0, 1.0);
    vec3 linearHDR = EvaluatePbrLighting(albedo, surfaceMetallic, surfaceRoughness, surfaceAo, shadingNormal,
                                         geometricNormal, sunVisibility);
    if (effects.y > 0.5 && reflectionStrength > 0.0) {
        vec4 reflectedClip = reflectionViewProjection * vec4(vWorldPosition, 1.0);
        if (reflectedClip.w > 1e-8) {
            vec2 reflectedUV = reflectedClip.xy / reflectedClip.w * 0.5 + 0.5;
            if (all(greaterThanEqual(reflectedUV, vec2(0.0))) && all(lessThanEqual(reflectedUV, vec2(1.0))))
                linearHDR = mix(linearHDR, texture(reflectionColor, reflectedUV).rgb,
                    clamp(reflectionStrength * shadowParams.w, 0.0, 1.0));
        }
    }
    // Surface parameters keep their unrestricted HDR range, but the current
    // pipeline stores radiance in RGBA16F. Bound each write below half-float
    // infinity; otherwise high emission contaminates Bloom and tone mapping.
    vec3 radiance = linearHDR + clamp(emissiveColor.rgb, vec3(0.0), vec3(60000.0));
    radiance = mix(radiance, vec3(0.0), isnan(radiance));
    outColor = vec4(clamp(radiance, vec3(0.0), vec3(60000.0)), alpha);
}
)GLSL";
    return source;
}

inline Render::PipelineSpec PbrShadowPipelineSpec(
    Render::RenderResourceHandle<Render::ShaderProgramSpec> program) {
    auto spec = PbrPipelineSpec(program);
    spec.cullMode = Render::CullMode::None;
    spec.depthCompare = Render::CompareOp::Less;
    return spec;
}

inline std::string PbrShadowVertexShader() {
    std::string source = R"GLSL(#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aTexCoord;
layout(std140, binding = 0) uniform ViewData {
    mat4 uViewProjection;
    vec4 uCameraPosition;
};
layout(std140, binding = 1) uniform ObjectData { mat4 uModel; };
)GLSL";
    source += MakePbrTemplate()->GenerateGLSLUniformBlock();
    source += R"GLSL(
out vec2 vShadowTexCoord;
out vec3 vShadowWorldPosition;
void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    vShadowWorldPosition = world.xyz;
    vShadowTexCoord = aTexCoord * uvTransform.xy + uvTransform.zw;
    gl_Position = uViewProjection * world;
}
)GLSL";
    return source;
}

inline std::string PbrShadowFragmentShader() {
    std::string source = R"GLSL(#version 450 core
layout(binding = 0) uniform sampler2D albedoMap;
)GLSL";
    source += MakePbrTemplate()->GenerateGLSLUniformBlock();
    source += PbrSceneEffectsGLSL();
    source += R"GLSL(
in vec2 vShadowTexCoord;
in vec3 vShadowWorldPosition;
void main() {
    if (texture(albedoMap, vShadowTexCoord).a * baseColor.a < alphaCutoff) discard;
    if (effects.z > 0.5 && dot(clipPlane, vec4(vShadowWorldPosition, 1.0)) < 0.0) discard;
}
)GLSL";
    return source;
}

} // namespace Render::Material
