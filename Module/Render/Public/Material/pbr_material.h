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
};

static_assert(std::is_standard_layout_v<PbrPassConstants>);
static_assert(std::is_trivially_copyable_v<PbrPassConstants>);
static_assert(offsetof(PbrPassConstants, lightPositions) == 0);
static_assert(offsetof(PbrPassConstants, lightColors) == 64);
static_assert(offsetof(PbrPassConstants, ambientAndExposure) == 128);
static_assert(offsetof(PbrPassConstants, outputOptions) == 144);
static_assert(sizeof(PbrPassConstants) == 160);

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
};
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
};
)GLSL";
    source += MakePbrTemplate()->GenerateGLSLUniformBlock();
    source += PbrSceneEffectsGLSL();
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

float CascadeVisibility(int cascade, vec3 worldPosition, vec3 geometricNormal) {
    vec4 lightPosition = lightViewProjection[cascade] *
        vec4(worldPosition + geometricNormal * max(shadowParams.y, 0.0), 1.0);
    if (abs(lightPosition.w) < 1e-8) return 1.0;
    vec3 local = lightPosition.xyz / lightPosition.w * 0.5 + 0.5;
    if (any(lessThan(local, vec3(0.0))) || any(greaterThan(local, vec3(1.0)))) return 1.0;
    float texel = 1.0 / max(effects.w, 8.0);
    vec2 tile = vec2(float(cascade % 2), float(cascade / 2)) * 0.5;
    vec2 atlasUV = tile + local.xy * 0.5;
    vec2 minimumUV = tile + vec2(0.5 * texel);
    vec2 maximumUV = tile + vec2(0.5 - 0.5 * texel);
    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
        // Never let the PCF footprint read an adjacent cascade in the atlas.
        vec2 uv = clamp(atlasUV + vec2(x, y) * texel, minimumUV, maximumUV);
        float storedDepth = texture(shadowAtlas, uv).r;
        visibility += local.z - max(shadowParams.x, 0.0) <= storedDepth ? 1.0 : 0.0;
    }
    return visibility / 9.0;
}

float DirectionalVisibility(vec3 worldPosition) {
    if (effects.x < 0.5) return 1.0;
    float viewDepth = -(mainView * vec4(worldPosition, 1.0)).z;
    if (viewDepth <= 0.0 || viewDepth > cascadeSplits.w) return 1.0;
    int cascade = 3;
    for (int i = 0; i < 3; ++i) if (viewDepth < cascadeSplits[i]) { cascade = i; break; }
    vec3 normal = SafeNormalize(vWorldNormal, vec3(0.0, 1.0, 0.0));
    float visibility = CascadeVisibility(cascade, worldPosition, normal);
    if (cascade < 3) {
        float previousSplit = cascade == 0 ? 0.0 : cascadeSplits[cascade - 1];
        float width = (cascadeSplits[cascade] - previousSplit) * clamp(shadowParams.z, 0.0, 0.45);
        if (width > 1e-6 && viewDepth > cascadeSplits[cascade] - width) {
            float next = CascadeVisibility(cascade + 1, worldPosition, normal);
            visibility = mix(visibility, next, smoothstep(cascadeSplits[cascade] - width,
                                                        cascadeSplits[cascade], viewDepth));
        }
    }
    return visibility;
}

// Produces scene-linear HDR radiance. It deliberately contains no exposure,
// tone mapping or output encoding; those belong to the pipeline output pass.
vec3 EvaluatePbrLighting(vec3 albedo, float surfaceMetallic,
                         float surfaceRoughness, float surfaceAo, vec3 N) {
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
    if (NdotL > 0.0 && sunDirectionIntensity.w > 0.0) {
        vec3 H = SafeNormalize(V + L, N);
        vec3 fresnel = FresnelSchlick(max(dot(H, V), 0.0), F0);
        vec3 specular = DistributionGGX(N, H, surfaceRoughness) *
            GeometrySmith(N, V, L, surfaceRoughness) * fresnel /
            max(4.0 * max(dot(N, V), 0.0) * NdotL, 1e-4);
        vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - surfaceMetallic);
        vec3 radiance = max(sunColor.rgb, vec3(0.0)) * sunDirectionIntensity.w;
        directLighting += (diffuseWeight * albedo / PI + specular) * radiance * NdotL *
                          DirectionalVisibility(vWorldPosition);
    }
    float screenAo = screenAndAo.z > 0.5 ?
        clamp(texture(ambientOcclusion, gl_FragCoord.xy * screenAndAo.xy).r, 0.0, 1.0) : 1.0;
    // AO affects only ambient lighting. Shadow visibility affects only the sun;
    // unrelated point lights, emission and ambient must not be darkened by CSM.
    return max(ambientAndExposure.rgb, vec3(0.0)) * albedo * surfaceAo * screenAo + directLighting;
}

void main() {
    vec4 albedoSample = texture(albedoMap, vTexCoord);
    float alpha = albedoSample.a * baseColor.a;
    if (alpha < alphaCutoff) discard;
    if (effects.z > 0.5 && dot(clipPlane, vec4(vWorldPosition, 1.0)) < 0.0) discard;
    // Only the color texture is optionally decoded. The baseColor factor and
    // normal/metallic/roughness/AO samples are always interpreted as linear data.
    vec3 albedo = (baseColorIsSRGB != 0u ? SRGBToLinear(albedoSample.rgb) : albedoSample.rgb) * baseColor.rgb;
    float surfaceMetallic = clamp(useMetallicMap != 0u ? texture(metallicMap, vTexCoord).r : metallic, 0.0, 1.0);
    float surfaceRoughness = clamp(useRoughnessMap != 0u ? texture(roughnessMap, vTexCoord).r : roughness, 0.045, 1.0);
    float surfaceAo = clamp(useAoMap != 0u ? texture(aoMap, vTexCoord).r : ao, 0.0, 1.0);
    vec3 linearHDR = EvaluatePbrLighting(albedo, surfaceMetallic, surfaceRoughness, surfaceAo, SurfaceNormal());
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
