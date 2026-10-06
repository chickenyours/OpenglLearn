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
#include "Render/Public/Pipeline/surface_pass.h"
#include "Render/Public/Pipeline/area_lights.h"
#include "Render/Public/Pipeline/sky_light.h"
#include "Render/Public/Pipeline/diffuse_probe_volume.h"

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
    // xyz=linear point intensity; w=finite source radius in world units.
    // Radius zero retains the legacy ideal point light exactly.
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

enum class PbrOutput : std::uint8_t { Surface, DiffuseRadiance, WorldNormal };

inline std::shared_ptr<const Render::Material::MaterialTemplate> MakePbrTemplate(Domain domain = Domain::Surface) {
    using namespace Render::Material;
    if (domain != Domain::Surface && domain != Domain::Translucent) return {};
    MaterialTemplateDesc desc;
    desc.name = domain == Domain::Translucent ? "TranslucentPbr" : "StandardPbr";
    desc.domain = domain;
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
        {"alphaCutoff", ParameterType::Float, 0.0f, "Alpha cutoff", "Surface", 0.0f, 1.0f, true},
        // Appended fields preserve every original parameter's offset.
        {"uvFlow", ParameterType::Vec4, glm::vec4(0), "UV velocity / second layer velocity", "Flow",
         std::nullopt, std::nullopt, true},
        {"useFlowMap", ParameterType::Bool, false, "Use flow map", "Flow"},
        {"flowStrength", ParameterType::Float, 1.0f, "Flow map UV velocity", "Flow", 0.0f, 8.0f, true},
        {"emissiveIntensity", ParameterType::Float, 1.0f, "Emission multiplier", "Surface", 0.0f, std::nullopt, true},
        {"useEmissiveMap", ParameterType::Bool, false, "Use linear emission map", "Textures"},
        // Translucent opacity fades optical influence, not raster coverage.
        // Coverage remains albedo.a*baseColor.a, so a full glass pixel does not
        // composite its already-refracted background over a second original one.
        {"opacity", ParameterType::Float, 1.0f,
         domain == Domain::Translucent ? "Optical effect strength" : "Alpha multiplier",
         "Transmission", 0.0f, 1.0f, true},
        {"transmission", ParameterType::Float, 1.0f, "Dielectric transmission", "Transmission", 0.0f, 1.0f, true},
        {"ior", ParameterType::Float, 1.45f, "Index of refraction", "Transmission", 1.0f, 2.5f, true},
        {"thickness", ParameterType::Float, 0.1f, "Optical thickness (world units)", "Transmission", 0.0f, 100.0f, true},
        {"distortionStrength", ParameterType::Float, 4.0f, "Refraction limit / custom distortion (pixels)", "Transmission", 0.0f, 256.0f, true},
        {"useDistortionMap", ParameterType::Bool, false, "Use signed RG distortion", "Textures"},
        {"absorptionColor", ParameterType::Vec4, glm::vec4(1), "Transmission after one world unit", "Transmission",
         0.0f, 1.0f, true},
        {"twoSided", ParameterType::Bool, false, "Two-sided thin sheet", "Transmission"}
    };
    // All slots are required even while their switches are off. The host binds
    // white to missing color/scalar maps and a flat normal to a missing normal map.
    desc.textures = {
        {"albedoMap", 0, true}, {"normalMap", 1, true}, {"metallicMap", 2, true},
        {"roughnessMap", 3, true}, {"aoMap", 4, true},
        {"distortionMap", 5, false}, {"flowMap", 6, false}, {"emissiveMap", 7, false}
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
    Render::RenderResourceHandle<Render::ShaderProgramSpec> program, Domain domain = Domain::Surface) {
    Render::PipelineSpec spec;
    spec.shaderProgram = program;
    spec.expectVertexLayout = PbrVertexLayout();
    spec.depthTest = true;
    spec.depthWrite = domain != Domain::Translucent;
    spec.blendEnable = domain == Domain::Translucent;
    spec.blendMode = domain == Domain::Translucent ? Render::BlendMode::Alpha : Render::BlendMode::Opaque;
    // Supports mirrored/nonuniformly scaled objects and reflected camera views.
    // Translucent default front-surface selection uses geometric normal versus
    // view direction in the fragment shader, independent of triangle winding.
    spec.cullMode = Render::CullMode::None;
    return spec;
}

inline std::string PbrVertexShader(Domain domain = Domain::Surface) {
    if (domain != Domain::Surface && domain != Domain::Translucent) return {};
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
    source += MakePbrTemplate(domain)->GenerateGLSLUniformBlock();
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

// Shared by surface, capture and alpha-tested depth shaders. Time is pass data;
// Texture addressing defines wrapping. Flow advection uses two bounded phases
// that crossfade at resets; velocity derivatives never grow with elapsed time.
inline std::string PbrFlowGLSL() {
    return R"GLSL(
vec4 PbrFlowUVs(vec2 uv) {
    vec2 scroll=uv+uvFlow.xy*surfaceScreenTime.z;
    if (useFlowMap != 0u && all(greaterThan(textureSize(flowMap,0),ivec2(0)))) {
        vec2 flow=(texture(flowMap,uv).rg*2.0-1.0)*flowStrength;
        float phase=fract(surfaceScreenTime.z*.2);
        return vec4(scroll+flow*((phase-.5)*5),scroll+flow*((fract(phase+.5)-.5)*5));
    }
    return vec4(scroll,uv+uvFlow.zw*surfaceScreenTime.z);
}
vec4 PbrSampleMap(sampler2D image,vec4 uv) {
    vec4 value;
    if(useFlowMap!=0u && all(greaterThan(textureSize(flowMap,0),ivec2(0)))) {
        float weight=1-abs(fract(surfaceScreenTime.z*.2)*2-1);
        // Explicit gradients ignore the hidden phase reset. The texture itself
        // remains smooth while its zero-weight phase wraps to the next cycle.
        vec2 dx=dFdx(uv.xy),dy=dFdy(uv.xy),dx2=dFdx(uv.zw),dy2=dFdy(uv.zw);
        value=mix(textureGrad(image,uv.zw,dx2,dy2),textureGrad(image,uv.xy,dx,dy),weight);
        if(any(notEqual(uvFlow.zw,vec2(0)))) {
            vec2 delta=(uvFlow.zw-uvFlow.xy)*surfaceScreenTime.z;
            value=.5*(value+mix(textureGrad(image,uv.zw+delta,dx2,dy2),textureGrad(image,uv.xy+delta,dx,dy),weight));
        }
    } else {
        value=texture(image,uv.xy);
        if (any(notEqual(uvFlow.zw,vec2(0)))) value=0.5*(value+texture(image,uv.zw));
    }
    return value;
}
)GLSL";
}

inline std::string PbrFragmentShader(Domain domain = Domain::Surface, PbrOutput output = PbrOutput::Surface) {
    if (domain != Domain::Surface && domain != Domain::Translucent) return {};
    std::string source = R"GLSL(#version 450 core

layout(binding = 0) uniform sampler2D albedoMap;
layout(binding = 1) uniform sampler2D normalMap;
layout(binding = 2) uniform sampler2D metallicMap;
layout(binding = 3) uniform sampler2D roughnessMap;
layout(binding = 4) uniform sampler2D aoMap;
layout(binding = 5) uniform sampler2D distortionMap;
layout(binding = 6) uniform sampler2D flowMap;
layout(binding = 7) uniform sampler2D emissiveMap;
layout(binding = 8) uniform sampler2D opaqueColor;
layout(binding = 9) uniform sampler2D opaqueDepth;
layout(binding = 12) uniform sampler2D shadowAtlas;
layout(binding = 13) uniform sampler2D reflectionColor;
layout(binding = 14) uniform sampler2D ambientOcclusion;
layout(binding = 15) uniform sampler2D diffuseIrradiance;

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
    source += "\n#define PBR_TRANSLUCENT " + std::to_string(domain == Domain::Translucent ? 1 : 0) + "\n";
    source += "#define PBR_OUTPUT " + std::to_string(static_cast<unsigned>(output)) + "\n";
    source += MakePbrTemplate(domain)->GenerateGLSLUniformBlock();
    source += Render::SurfacePassGLSL();
    source += PbrFlowGLSL();
    source += PbrSceneEffectsGLSL();
    source += PbrDirectionalShadowGLSL();
    source += R"GLSL(
in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec2 vTexCoord;
in vec4 vWorldTangent;
layout(location = 0) out vec4 outColor;

const float PI = 3.14159265358979323846;

float PbrDielectricF0() {
#if PBR_TRANSLUCENT
    float ratio=(ior-1.0)/(ior+1.0);
    return ratio*ratio;
#else
    return 0.05; // Preserve the legacy opaque dielectric response.
#endif
}

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
float FilterNormalMapRoughness(float r,vec4 flowUVs) {
    if(useNormalMap==0u||specularAA.z<.5||specularAA.x<=0)return r;
    // Averaged mip/layer normals shorten before normalization. Retain that
    // lost variance in the NDF instead of recreating a sharp subpixel lobe.
    vec3 raw=PbrSampleMap(normalMap,flowUVs).xyz*2-1;
    float len=clamp(length(raw),.001,1);
    float variance=(1-len)/len*normalScale*normalScale;
    float kernel=min(2*specularAA.x*variance,specularAA.y);
    return sqrt(sqrt(min(pow(r,4)+kernel,1.0)));
}

float FilterPointSourceRoughness(float roughness,vec3 V,vec3 L,vec3 H,
                               float distanceSquared,float sourceRadius) {
    if(sourceRadius<=0)return roughness;
    // Integrate the finite source's angular footprint into the GGX NDF. The
    // differential of normalize(V+L) accounts for oblique half-vector changes;
    // a visible lamp no longer becomes an infinitesimal moving highlight.
    float angularSquared=min(sourceRadius*sourceRadius/max(distanceSquared,1e-8),1.0);
    float halfLengthSquared=max(dot(V+L,V+L),1e-4);
    float halfCosine=dot(H,L);
    float kernel=.5*angularSquared*(1+halfCosine*halfCosine)/halfLengthSquared;
    return sqrt(sqrt(min(pow(roughness,4)+kernel,1.0)));
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

vec3 SurfaceNormal(vec4 flowUVs) {
    vec3 N = SafeNormalize(vWorldNormal, vec3(0.0, 0.0, 1.0));
    if (useNormalMap == 0u) return N;
    vec3 tangentAxis = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 fallbackTangent = SafeNormalize(cross(tangentAxis, N), vec3(1.0, 0.0, 0.0));
    vec3 T = SafeNormalize(vWorldTangent.xyz - N * dot(N, vWorldTangent.xyz), fallbackTangent);
    vec3 B = cross(N, T) * (vWorldTangent.w < 0.0 ? -1.0 : 1.0);
    vec3 tangentNormal = PbrSampleMap(normalMap, flowUVs).xyz * 2.0 - 1.0;
    tangentNormal.xy *= normalScale;
    tangentNormal = SafeNormalize(tangentNormal, vec3(0.0, 0.0, 1.0));
    return SafeNormalize(mat3(T, B, N) * tangentNormal, N);
}
)GLSL";
    source += Render::AreaLightsGLSL();
    source += Render::SkyLightGLSL();
    source += "#define REALTIME_GI_CACHE diffuseIrradiance\n";
    source += Render::DiffuseProbeGLSL();
    source += R"GLSL(

// Produces scene-linear HDR radiance. It deliberately contains no exposure,
// tone mapping or output encoding; those belong to the pipeline output pass.
vec3 EvaluatePbrLighting(vec3 albedo, float surfaceMetallic,
                         float surfaceRoughness, float surfaceAo, vec3 N,
                         vec3 geometricNormal, float sunVisibility, out vec3 diffuseRadiance) {
    vec3 V = SafeNormalize(uCameraPosition.xyz - vWorldPosition, N);
    vec3 F0 = mix(vec3(PbrDielectricF0()), albedo, surfaceMetallic);
    vec3 directDiffuse = vec3(0.0), directSpecular = vec3(0.0);
    for (int lightIndex = 0; lightIndex < 4; ++lightIndex) {
        if (all(lessThanEqual(lightColors[lightIndex].rgb,vec3(0)))) continue;
        vec3 toLight = lightPositions[lightIndex].xyz - vWorldPosition;
        float distanceSquared = max(dot(toLight, toLight), 1e-4);
        vec3 L = SafeNormalize(toLight, N);
        float NdotL = max(dot(N, L), 0.0);
        float geometricHorizon = smoothstep(0.0, 0.05, dot(geometricNormal, L));
        if (NdotL <= 0.0 || geometricHorizon <= 0.0) continue;
        vec3 H = SafeNormalize(V + L, N);
        float sourceRadius = max(lightColors[lightIndex].w, 0.0);
        // Inside a finite source, its represented radius bounds inverse-square
        // energy. Keep the actual distance for its angular GGX footprint.
        vec3 radiance = max(lightColors[lightIndex].rgb, vec3(0.0)) * geometricHorizon /
                        max(distanceSquared, sourceRadius * sourceRadius);
        float pointRoughness=FilterPointSourceRoughness(surfaceRoughness,V,L,H,distanceSquared,
                                                       sourceRadius);
        float distribution = DistributionGGX(N, H, pointRoughness);
        float geometry = GeometrySmith(N, V, L, pointRoughness);
        vec3 fresnel = FresnelSchlick(max(dot(H, V), 0.0), F0);
        vec3 specular = distribution * geometry * fresnel /
            max(4.0 * max(dot(N, V), 0.0) * NdotL, 1e-4);
        vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - surfaceMetallic);
        directDiffuse += diffuseWeight * albedo / PI * radiance * NdotL;
        directSpecular += specular * radiance * NdotL;
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
        vec3 incident = radiance * NdotL * sunVisibility * geometricHorizon;
        directDiffuse += diffuseWeight * albedo / PI * incident;
        directSpecular += specular * incident;
    }
    vec3 areaDiffuse, areaSpecular;
    EvaluateAreaLighting(vWorldPosition,V,N,geometricNormal,albedo,surfaceMetallic,surfaceRoughness,
                         areaDiffuse,areaSpecular,PbrDielectricF0());
    directDiffuse += areaDiffuse; directSpecular += areaSpecular;
    diffuseRadiance = directDiffuse;
#if PBR_TRANSLUCENT
    // Screen AO belongs to the opaque receiver behind this surface.
    float screenAo = 1.0;
    float diffuseFraction = 1.0-clamp(transmission,0.0,1.0);
#else
    float screenAo = screenAndAo.z > 0.5 ?
        clamp(texture(ambientOcclusion, gl_FragCoord.xy * screenAndAo.xy).r, 0.0, 1.0) : 1.0;
    float diffuseFraction = 1.0;
#endif
    // AO affects only ambient lighting. Shadow visibility affects only the sun;
    // unrelated point lights, emission and ambient must not be darkened by CSM.
    float probeCoverage=0,probeSkyVisibility=1; vec3 probeIrradiance=vec3(0);
#if PBR_OUTPUT == 0
    if(probeOptions.x>2.5) {
        vec4 gather=texture(diffuseIrradiance,gl_FragCoord.xy*surfaceScreenTime.xy+surfaceScreenJitter.xy);
        probeCoverage=1;probeIrradiance=max(gather.rgb,vec3(0))/PI;probeSkyVisibility=clamp(gather.a,0,1);
    } else probeCoverage=EvaluateDiffuseProbes(vWorldPosition,N,reflect(-V,N),surfaceRoughness,
                                              probeIrradiance,probeSkyVisibility);
#endif
    vec3 indirect = max(ambientAndExposure.rgb, vec3(0.0)) * albedo * surfaceAo * screenAo;
#if !PBR_TRANSLUCENT
    if (surfaceOptions.x > 0.5 && effects.z < 0.5)
        indirect += max(texture(diffuseIrradiance,gl_FragCoord.xy*surfaceScreenTime.xy).rgb,vec3(0)) *
                    albedo/PI*(1.0-surfaceMetallic)*surfaceAo*(1.0-probeCoverage);
#endif
    vec3 skyDiffuseRadiance,probeDiffuseWeight;
    vec3 sky=EvaluateSkyLighting(N,V,albedo,surfaceMetallic,surfaceRoughness,F0,
                                surfaceAo*screenAo,diffuseFraction,mix(1.0,probeSkyVisibility,probeCoverage),
                                skyDiffuseRadiance,probeDiffuseWeight);
    // Sky-lit diffuse surfaces can feed the single bounce, but reflected sky
    // and the gathered indirect buffer are never captured as source radiance.
    diffuseRadiance += surfaceOptions.z>.5?vec3(0):skyDiffuseRadiance;
#if !PBR_TRANSLUCENT
    if(surfaceOptions.y>.5) {
        vec2 uv=gl_FragCoord.xy*surfaceScreenTime.xy+surfaceScreenJitter.xy;
        // Both Fresnel coefficients are already integrated by the GGX VNDF
        // pass. Applying the environment BRDF LUT again would double-weight it.
        sky=skyDiffuseRadiance+(F0*max(texture(opaqueColor,uv).rgb,vec3(0))+
            max(texture(opaqueDepth,uv).rgb,vec3(0)))*surfaceAo*screenAo;
    }
#endif
    vec3 worldDiffuse=probeIrradiance*albedo*probeDiffuseWeight*surfaceAo*screenAo*diffuseFraction;
    return (indirect+directDiffuse)*diffuseFraction + directSpecular + sky +
           probeCoverage*(worldDiffuse-skyDiffuseRadiance);
}

vec3 PbrEmission(vec4 flowUVs) {
    vec3 emission=clamp(emissiveColor.rgb,vec3(0),vec3(60000))*min(emissiveIntensity,60000.0);
    if (useEmissiveMap!=0u && all(greaterThan(textureSize(emissiveMap,0),ivec2(0))))
        emission*=clamp(PbrSampleMap(emissiveMap,flowUVs).rgb,vec3(0),vec3(60000));
    return clamp(emission,vec3(0),vec3(60000));
}

// Refraction reads a single opaque layer. Never interpolate its foreground
// colors into a background tap. Bilinear depth confidence fades the UV offset
// near a silhouette; the final color filter accepts only background taps.
float PbrBackgroundConfidence(ivec2 pixel,ivec2 size,float surfaceDepth,mat4 inverseProjection) {
    float rawDepth=texelFetch(opaqueDepth,pixel,0).r;
    if (rawDepth>=0.999999) return 1.0;
    vec2 uv=(vec2(pixel)+0.5)/vec2(size);
    vec4 background=inverseProjection*vec4(uv*2.0-1.0,rawDepth*2.0-1.0,1);
    if (abs(background.w)<1e-8) return 0.0;
    float tolerance=max(0.001,surfaceDepth*0.001);
    return smoothstep(-tolerance,tolerance,-background.z/background.w-surfaceDepth);
}
vec4 PbrRefractionBackground(vec2 uv,float surfaceDepth,mat4 inverseProjection) {
    ivec2 size=textureSize(opaqueDepth,0);
    vec2 pixel=uv*vec2(size)-0.5;
    ivec2 lower=ivec2(floor(pixel));
    vec2 phase=fract(pixel);
    vec3 color=vec3(0); float confidence=0.0;
    for (int y=0;y<2;++y) for (int x=0;x<2;++x) {
        ivec2 tap=clamp(lower+ivec2(x,y),ivec2(0),size-1);
        float weight=(x==0 ? 1.0-phase.x : phase.x)*(y==0 ? 1.0-phase.y : phase.y);
        weight*=PbrBackgroundConfidence(tap,size,surfaceDepth,inverseProjection);
        color+=texelFetch(opaqueColor,tap,0).rgb*weight;
        confidence+=weight;
    }
    return vec4(color/max(confidence,1e-8),confidence);
}

vec3 PbrTransmission(vec3 N,vec4 flowUVs,float surfaceMetallic) {
#if PBR_TRANSLUCENT
    if (surfaceScreenTime.w<0.5) return vec3(0);
    float influence=clamp(opacity,0.0,1.0);
    vec2 pixelSize=surfaceScreenTime.xy;
    vec2 centerUV=gl_FragCoord.xy*pixelSize;
    vec3 viewPosition=(surfaceView*vec4(vWorldPosition,1)).xyz;
    vec3 incident=SafeNormalize(viewPosition,vec3(0,0,-1));
    vec3 viewNormal=SafeNormalize(mat3(surfaceView)*N,vec3(0,0,1));
    bool leaving=dot(incident,viewNormal)>0.0;
    if (leaving) viewNormal=-viewNormal;
    vec3 transmitted=refract(incident,viewNormal,leaving ? ior : 1.0/ior);
    bool totalInternalReflection=dot(transmitted,transmitted)<1e-8;
    float travel=thickness/max(abs(transmitted.z),0.1);
    vec4 clip=surfaceProjection*vec4(viewPosition+transmitted*travel,1);
    vec2 offset=vec2(0);
    if (clip.w>1e-6) offset=clip.xy/clip.w*0.5+0.5-centerUV;
    if (useDistortionMap!=0u && all(greaterThan(textureSize(distortionMap,0),ivec2(0))))
        offset+=(PbrSampleMap(distortionMap,flowUVs).rg*2.0-1.0)*distortionStrength*pixelSize;
    // A finite pixel cap bounds screen-space approximation errors near grazing
    // surfaces. Escaping the screen falls back to the undistorted background.
    offset=clamp(offset,-distortionStrength*pixelSize,distortionStrength*pixelSize)*influence;
    vec2 sampleUV=centerUV+offset;
    // Fade displacement over the final two pixels, instead of an abrupt jump
    // from the full displacement to zero at the viewport boundary.
    vec2 edgeDistance=min(sampleUV/pixelSize-0.5,(vec2(1)-sampleUV)/pixelSize-0.5);
    offset*=smoothstep(0.0,2.0,min(edgeDistance.x,edgeDistance.y));
    sampleUV=centerUV+offset;
    ivec2 depthSize=textureSize(opaqueDepth,0);
    if (any(lessThanEqual(depthSize,ivec2(0)))) return vec3(0);
    mat4 inverseProjection=inverse(surfaceProjection);
    float surfaceDepth=-viewPosition.z;
    float confidence=PbrRefractionBackground(sampleUV,surfaceDepth,inverseProjection).a;
    sampleUV=centerUV+offset*confidence;
    vec4 background=PbrRefractionBackground(sampleUV,surfaceDepth,inverseProjection);
    // A single opaque snapshot has no hidden layer to reconstruct. If even the
    // reduced footprint is occluded, use the undistorted visible destination.
    vec3 backgroundColor=background.a>1e-6 ? background.rgb : texture(opaqueColor,centerUV).rgb;
    vec3 V=SafeNormalize(uCameraPosition.xyz-vWorldPosition,N);
    float opticalDistance=thickness/max(abs(dot(N,V)),0.1);
    vec3 beer=pow(clamp(absorptionColor.rgb,vec3(0.0001),vec3(1)),vec3(opticalDistance));
    // Equal indices have no optical interface; Schlick's grazing asymptote
    // alone would otherwise attenuate even an IOR=1, zero-offset identity.
    vec3 fresnel=ior<=1.00001 ? vec3(0) : FresnelSchlick(abs(dot(N,V)),vec3(PbrDielectricF0()));
    vec3 transmittance=totalInternalReflection ? vec3(0) : beer*(vec3(1)-fresnel)*
        clamp(transmission,0.0,1.0)*(1.0-surfaceMetallic);
    // Fade the optical coefficients at ONE background coordinate. Mixing the
    // original and refracted images here (or via opacity as blend alpha) would
    // produce two silhouettes even in a completely static frame.
    return max(backgroundColor,vec3(0))*mix(vec3(1),transmittance,influence);
#else
    return vec3(0);
#endif
}

void main() {
    // Derivatives and shadow receiver-plane evaluation precede all discard and
    // divergent cascade/BRDF decisions, so neighbouring helper lanes exist.
    vec3 worldDx = dFdx(vWorldPosition);
    vec3 worldDy = dFdy(vWorldPosition);
    vec3 geometricNormal = SafeNormalize(vWorldNormal, vec3(0, 1, 0));
    // The map switch is draw-uniform. Obtain normalized shading normals and
    // their derivatives before shadow/BRDF divergence or alpha/plane discard.
    vec4 flowUVs=PbrFlowUVs(vTexCoord);
    vec3 shadingNormal = SurfaceNormal(flowUVs);
    vec3 normalDx = dFdx(shadingNormal);
    vec3 normalDy = dFdy(shadingNormal);
    float filteredRoughness=clamp(useRoughnessMap!=0u?PbrSampleMap(roughnessMap,flowUVs).r:roughness,.045,1.0);
    filteredRoughness=FilterSpecularRoughness(FilterNormalMapRoughness(filteredRoughness,flowUVs),normalDx,normalDy);
#if PBR_TRANSLUCENT
    vec3 surfaceToEye=SafeNormalize(uCameraPosition.xyz-vWorldPosition,geometricNormal);
    bool backSurface=dot(geometricNormal,surfaceToEye)<=0.0;
    // twoSided describes a thin sheet, not two independently composited volume
    // interfaces. Face the sheet toward the eye so its back does not invoke a
    // fictitious exit-medium TIR. Derivative magnitudes are unchanged by sign.
    if (twoSided!=0u && backSurface) { geometricNormal=-geometricNormal; shadingNormal=-shadingNormal; }
#endif
    float sunVisibility = 1.0;
    if (sunDirectionIntensity.w > 0.0 || screenAndAo.w > 0.5)
        sunVisibility = DirectionalVisibility(vWorldPosition, geometricNormal, worldDx, worldDy);
    vec4 albedoSample = PbrSampleMap(albedoMap,flowUVs);
    float alpha = albedoSample.a * baseColor.a;
#if PBR_TRANSLUCENT
    // With a snapshot, alpha is coverage only. Without a snapshot, ordinary
    // alpha fading remains a safe fallback for a standalone translucent draw.
    if (surfaceScreenTime.w<0.5) alpha*=opacity;
#else
    alpha*=opacity; // Preserve Surface alpha-cutoff behavior.
#endif
    if (alpha < alphaCutoff) discard;
#if PBR_TRANSLUCENT
    if (twoSided==0u && backSurface) discard;
    if (alpha<=0.0 || opacity<=0.0) discard;
#endif
    if (effects.z > 0.5 && dot(clipPlane, vec4(vWorldPosition, 1.0)) < 0.0) discard;
#if PBR_OUTPUT == 2
    float captureRoughness=filteredRoughness;
    outColor=vec4(surfaceOptions.w>.5?shadingNormal:geometricNormal,surfaceOptions.z>.5?1+captureRoughness:1);
    return;
#endif
#if PBR_OUTPUT == 0
#if !PBR_TRANSLUCENT
    if(surfaceScreenJitter.z>.5) {
        // Resolved diagnostic RGB is already reflected diffuse radiance.
        // Do not apply albedo/AO/PI, analytic direct lighting, sky/reflections
        // or emission again. Alpha/clip discard above retains mesh coverage.
        vec3 radiance=texture(diffuseIrradiance,gl_FragCoord.xy*surfaceScreenTime.xy+surfaceScreenJitter.xy).rgb;
        radiance=mix(radiance,vec3(0),isnan(radiance));
        outColor=vec4(clamp(radiance,vec3(0),vec3(60000)),alpha);return;
    }
#endif
    if (screenAndAo.w > 0.5) {
        vec3 diagnostic = screenAndAo.w < 1.5 ? vec3(sunVisibility) : DirectionalCascadeColor(vWorldPosition);
        outColor = vec4(diagnostic, 1);
        return;
    }
#endif
    // Only the color texture is optionally decoded. The baseColor factor and
    // normal/metallic/roughness/AO samples are always interpreted as linear data.
    vec3 albedo = (baseColorIsSRGB != 0u ? SRGBToLinear(albedoSample.rgb) : albedoSample.rgb) * baseColor.rgb;
    float surfaceMetallic = clamp(useMetallicMap != 0u ? PbrSampleMap(metallicMap, flowUVs).r : metallic, 0.0, 1.0);
    float surfaceRoughness = filteredRoughness;
    float surfaceAo = clamp(useAoMap != 0u ? PbrSampleMap(aoMap, flowUVs).r : ao, 0.0, 1.0);
    vec3 diffuseRadiance;
    vec3 linearHDR = EvaluatePbrLighting(albedo, surfaceMetallic, surfaceRoughness, surfaceAo, shadingNormal,
                                         geometricNormal, sunVisibility,diffuseRadiance);
#if PBR_OUTPUT == 1
    vec3 diffuseOutput=clamp(diffuseRadiance+PbrEmission(flowUVs),vec3(0),vec3(60000));
    outColor=vec4(mix(diffuseOutput,vec3(0),isnan(diffuseOutput)),1);
    return;
#endif
    if (effects.y > 0.5 && reflectionStrength > 0.0) {
        vec4 reflectedClip = reflectionViewProjection * vec4(vWorldPosition, 1.0);
        if (reflectedClip.w > 1e-8) {
            vec2 reflectedUV = reflectedClip.xy / reflectedClip.w * 0.5 + 0.5;
            if (all(greaterThanEqual(reflectedUV, vec2(0.0))) && all(lessThanEqual(reflectedUV, vec2(1.0)))) {
                float weight=clamp(reflectionStrength*shadowParams.w,0.0,1.0);
#if PBR_TRANSLUCENT
                vec3 V=normalize(uCameraPosition.xyz-vWorldPosition);
                float f0=pow((ior-1.0)/(ior+1.0),2.0);
                weight*=f0+(1-f0)*pow(1-clamp(dot(shadingNormal,V),0,1),5);
#endif
                vec3 reflected=texture(reflectionColor,reflectedUV).rgb;
                linearHDR=mix(linearHDR,reflected,weight);
            }
        }
    }
    // Surface parameters keep their unrestricted HDR range, but the current
    // pipeline stores radiance in RGBA16F. Bound each write below half-float
    // infinity; otherwise high emission contaminates Bloom and tone mapping.
    vec3 radiance = linearHDR + PbrEmission(flowUVs);
#if PBR_TRANSLUCENT
    if (surfaceScreenTime.w>0.5) radiance*=opacity;
    radiance+=PbrTransmission(shadingNormal,flowUVs,surfaceMetallic);
#endif
    radiance = mix(radiance, vec3(0.0), isnan(radiance));
    outColor = vec4(clamp(radiance, vec3(0.0), vec3(60000.0)), alpha);
}
)GLSL";
    return source;
}

inline std::string PbrFragmentShader(PbrOutput output) {
    return PbrFragmentShader(Domain::Surface,output);
}

inline Render::PipelineSpec PbrShadowPipelineSpec(
    Render::RenderResourceHandle<Render::ShaderProgramSpec> program, Domain domain = Domain::Surface) {
    auto spec = PbrPipelineSpec(program,domain);
    spec.cullMode = Render::CullMode::None;
    spec.depthCompare = Render::CompareOp::Less;
    spec.depthWrite = true;
    spec.blendEnable = false;
    spec.blendMode = Render::BlendMode::Opaque;
    return spec;
}

inline std::string PbrShadowVertexShader(Domain domain = Domain::Surface) {
    if (domain != Domain::Surface && domain != Domain::Translucent) return {};
    std::string source = R"GLSL(#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aTexCoord;
layout(std140, binding = 0) uniform ViewData {
    mat4 uViewProjection;
    vec4 uCameraPosition;
};
layout(std140, binding = 1) uniform ObjectData { mat4 uModel; };
)GLSL";
    source += MakePbrTemplate(domain)->GenerateGLSLUniformBlock();
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

inline std::string PbrShadowFragmentShader(Domain domain = Domain::Surface) {
    if (domain != Domain::Surface && domain != Domain::Translucent) return {};
    std::string source = R"GLSL(#version 450 core
layout(binding = 0) uniform sampler2D albedoMap;
layout(binding = 6) uniform sampler2D flowMap;
)GLSL";
    source += MakePbrTemplate(domain)->GenerateGLSLUniformBlock();
    source += Render::SurfacePassGLSL();
    source += PbrFlowGLSL();
    source += PbrSceneEffectsGLSL();
    source += R"GLSL(
in vec2 vShadowTexCoord;
in vec3 vShadowWorldPosition;
void main() {
    vec4 flowUVs=PbrFlowUVs(vShadowTexCoord);
    if (PbrSampleMap(albedoMap,flowUVs).a * baseColor.a * opacity < alphaCutoff) discard;
    if (effects.z > 0.5 && dot(clipPlane, vec4(vShadowWorldPosition, 1.0)) < 0.0) discard;
}
)GLSL";
    return source;
}

} // namespace Render::Material
