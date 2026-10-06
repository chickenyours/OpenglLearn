#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>

#include <glm/glm.hpp>

namespace Render {

inline constexpr std::uint32_t AreaLightsBinding = 8;
inline constexpr std::uint32_t MaxAreaLights = 4;

struct RectAreaLight {
    glm::vec3 center{0.0f};
    // World-space half axes. cross(U,V) points out of the emitting face.
    glm::vec3 halfAxisU{0.5f, 0.0f, 0.0f};
    glm::vec3 halfAxisV{0.0f, 0.0f, 0.5f};
    // Linear emitted radiance, not point-light intensity or total flux.
    glm::vec3 radiance{1.0f};
    bool enabled = true;
    bool twoSided = false;
};

struct AreaLightsSettings {
    std::array<RectAreaLight, MaxAreaLights> lights{};
    std::uint32_t count = 0;
    std::uint32_t samplesPerAxis = 4; // 4x4 or 8x8 midpoint area quadrature
    // Conservative GGX filtering over each quadrature cell's angular extent.
    // Zero is a diagnostic unfiltered reference; this is not shadow filtering.
    float specularFilter = 1.0f;
};

inline bool ValidateAreaLightsSettings(const AreaLightsSettings& settings, std::string* error = nullptr) {
    if (error) error->clear();
    const auto fail = [&](const char* message) { if (error) *error = message; return false; };
    const auto finite = [](glm::vec3 value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    if (settings.count > MaxAreaLights) return fail("At most four rectangle area lights are supported");
    if (settings.samplesPerAxis != 4 && settings.samplesPerAxis != 8)
        return fail("Area light samplesPerAxis must be 4 or 8");
    if (!std::isfinite(settings.specularFilter) || settings.specularFilter < 0 || settings.specularFilter > 2)
        return fail("Area light specularFilter must be finite and between zero and two");
    for (std::uint32_t i = 0; i < settings.count; ++i) {
        const auto& light = settings.lights[i];
        if (!light.enabled) continue;
        if (!finite(light.center) || !finite(light.halfAxisU) || !finite(light.halfAxisV) || !finite(light.radiance))
            return fail("Area light vectors must be finite");
        const float uSquared = glm::dot(light.halfAxisU, light.halfAxisU);
        const float vSquared = glm::dot(light.halfAxisV, light.halfAxisV);
        if (!std::isfinite(uSquared) || !std::isfinite(vSquared) || uSquared < 1e-8f || vSquared < 1e-8f ||
            uSquared > 1e8f || vSquared > 1e8f)
            return fail("Area light half-axis lengths must be between 0.0001 and 10000 world units");
        if (std::abs(glm::dot(light.halfAxisU, light.halfAxisV)) > 1e-4f * std::sqrt(uSquared * vSquared))
            return fail("Rectangle area light half axes must be perpendicular");
        if (glm::any(glm::lessThan(light.radiance, glm::vec3(0))) ||
            glm::any(glm::greaterThan(light.radiance, glm::vec3(60000))))
            return fail("Area light radiance must be between zero and 60000");
    }
    return true;
}

struct alignas(16) RectAreaLightConstants {
    glm::vec4 centerEnabled{0};
    glm::vec4 halfAxisU{0}; // xyz=world half axis, w=samples per axis
    glm::vec4 halfAxisV{0}; // xyz=world half axis, w=GGX cell-filter strength
    glm::vec4 radianceTwoSided{0};
};
struct alignas(16) AreaLightsConstants {
    std::array<RectAreaLightConstants, MaxAreaLights> lights{};
};
static_assert(std::is_trivially_copyable_v<AreaLightsConstants>);
static_assert(offsetof(RectAreaLightConstants, centerEnabled) == 0);
static_assert(offsetof(RectAreaLightConstants, halfAxisU) == 16);
static_assert(offsetof(RectAreaLightConstants, halfAxisV) == 32);
static_assert(offsetof(RectAreaLightConstants, radianceTwoSided) == 48);
static_assert(sizeof(RectAreaLightConstants) == 64 && sizeof(AreaLightsConstants) == 256);

inline AreaLightsConstants MakeAreaLightsConstants(const AreaLightsSettings& settings) {
    std::string error;
    if (!ValidateAreaLightsSettings(settings, &error)) throw std::invalid_argument(error);
    AreaLightsConstants result;
    for (std::uint32_t i = 0; i < settings.count; ++i) {
        const auto& light = settings.lights[i];
        if (!light.enabled) continue;
        result.lights[i].centerEnabled = glm::vec4(light.center, 1);
        result.lights[i].halfAxisU = glm::vec4(light.halfAxisU, float(settings.samplesPerAxis));
        result.lights[i].halfAxisV = glm::vec4(light.halfAxisV, settings.specularFilter);
        result.lights[i].radianceTwoSided = glm::vec4(light.radiance, light.twoSided ? 1.0f : 0.0f);
    }
    return result;
}

// Insert after the production PBR DistributionGGX(N,H,r),
// GeometrySmith(N,V,L,r) and FresnelSchlick(cosine,F0) functions. The helper
// uses no textures, screen derivatives, random phase or external global state.
// It integrates unoccluded rectangle lights; area shadows need a visibility
// solution and are deliberately not approximated by the directional CSM.
inline std::string AreaLightsGLSL() {
    return R"GLSL(
struct RectAreaLightData {
    vec4 centerEnabled;
    vec4 halfAxisU;
    vec4 halfAxisV;
    vec4 radianceTwoSided;
};
layout(std140, binding = 8) uniform AreaLightsData {
    RectAreaLightData rectangleLights[4];
};

vec3 AreaSafeNormalize(vec3 value, vec3 fallbackValue) {
    float lengthSquared = dot(value, value);
    return lengthSquared > 1e-16 ? value * inversesqrt(lengthSquared) : fallbackValue;
}

float AreaCellRoughness(float roughness, vec3 V, vec3 L, vec3 H, float distanceToLight,
                        vec3 cellU, vec3 cellV, float filterStrength) {
    if (filterStrength <= 0.0) return roughness;
    // Differentiate the incoming direction and then the half vector over a
    // whole quadrature cell. Widen its GGX footprint instead of turning each
    // finite cell into a point emitter with an arbitrarily narrow highlight.
    // This is a conservative isotropic quadrature approximation, not an LTC
    // fit or an exact specular polygon integral. Diffuse energy is unchanged.
    vec3 lightDu = (cellU - L * dot(L, cellU)) / distanceToLight;
    vec3 lightDv = (cellV - L * dot(L, cellV)) / distanceToLight;
    float halfLength = max(length(V + L), 1e-4);
    vec3 halfDu = (lightDu - H * dot(H, lightDu)) / halfLength;
    vec3 halfDv = (lightDv - H * dot(H, lightDv)) / halfLength;
    float kernel = clamp(0.5 * filterStrength * (dot(halfDu, halfDu) + dot(halfDv, halfDv)), 0.0, 1.0);
    float alpha = roughness * roughness;
    return sqrt(sqrt(min(alpha * alpha + kernel, 1.0)));
}

void EvaluateAreaLightingInternal(vec3 worldPosition, vec3 viewDirection, vec3 shadingNormal,
                          vec3 geometricNormal, vec3 albedo, float metallic, float perceptualRoughness,
                          out vec3 directDiffuse, out vec3 directSpecular, float dielectricF0,
                          bool enforceGeometricHorizon) {
    directDiffuse = vec3(0);
    directSpecular = vec3(0);
    vec3 N = AreaSafeNormalize(shadingNormal, vec3(0, 1, 0));
    vec3 Ng = AreaSafeNormalize(geometricNormal, N);
    vec3 V = AreaSafeNormalize(viewDirection, N);
    float NdotV = max(dot(N, V), 0.0);
    if (NdotV <= 0.0) return;
    metallic = clamp(metallic, 0.0, 1.0);
    float roughness = clamp(perceptualRoughness, 0.045, 1.0);
    albedo = max(albedo, vec3(0));
    vec3 F0 = mix(vec3(clamp(dielectricF0, 0.0, 1.0)), albedo, metallic);
    const float areaPi = 3.14159265358979323846;
    for (int index = 0; index < 4; ++index) {
        RectAreaLightData light = rectangleLights[index];
        if (light.centerEnabled.w < 0.5 || all(lessThanEqual(light.radianceTwoSided.rgb,vec3(0)))) continue;
        vec3 U = light.halfAxisU.xyz, W = light.halfAxisV.xyz;
        vec3 areaVector = cross(U, W);
        float quarterArea = length(areaVector);
        if (quarterArea <= 1e-12) continue;
        vec3 emitterNormal = areaVector / quarterArea;
        vec3 toCenter = light.centerEnabled.xyz - worldPosition;
        // Every quadrature cell lies on the same emitting plane. Reject a
        // wholly back-facing one-sided rectangle before its distance/BRDF
        // work; the axis terms retain a conservative floating-point bound.
        float emitterSideMax = dot(emitterNormal,-toCenter) +
            abs(dot(emitterNormal,U)) + abs(dot(emitterNormal,W));
        if (light.radianceTwoSided.w < 0.5 && emitterSideMax <= 0.0) continue;
        // A center-only horizon test would discard a straddling rectangle.
        // The support of both half axes bounds every corner and every sample.
        float receiverHorizonMax = dot(Ng,toCenter) + abs(dot(Ng,U)) + abs(dot(Ng,W));
        if (enforceGeometricHorizon && receiverHorizonMax <= 0.0) continue;
        if (dot(N,toCenter) + abs(dot(N,U)) + abs(dot(N,W)) <= 0.0) continue;
        int grid = light.halfAxisU.w > 6.0 ? 8 : 4;
        vec3 cellU = U * (2.0 / float(grid)), cellV = W * (2.0 / float(grid));
        float cellArea = 4.0 * quarterArea / float(grid * grid);
        for (int y = 0; y < grid; ++y) for (int x = 0; x < grid; ++x) {
            vec2 parameter = (vec2(x, y) + 0.5) * (2.0 / float(grid)) - 1.0;
            vec3 toLight = light.centerEnabled.xyz + U * parameter.x + W * parameter.y - worldPosition;
            float distanceSquared = max(dot(toLight, toLight), max(cellArea * 1e-8, 1e-12));
            float distanceToLight = sqrt(distanceSquared);
            vec3 L = toLight / distanceToLight;
            float NdotL = max(dot(N, L), 0.0);
            float geometricHorizon = enforceGeometricHorizon ? smoothstep(0.0, 0.05, dot(Ng, L)) : 1.0;
            float emitterCosine = dot(emitterNormal, -L);
            emitterCosine = light.radianceTwoSided.w > 0.5 ? abs(emitterCosine) : max(emitterCosine, 0.0);
            if (NdotL <= 0.0 || geometricHorizon <= 0.0 || emitterCosine <= 0.0) continue;
            vec3 H = AreaSafeNormalize(V + L, N);
            vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
            vec3 incoming = max(light.radianceTwoSided.rgb, vec3(0)) *
                            (cellArea * emitterCosine * NdotL * geometricHorizon / distanceSquared);
            directDiffuse += ((vec3(1) - F) * (1.0 - metallic) * albedo / areaPi) * incoming;
            float filteredRoughness = AreaCellRoughness(roughness, V, L, H, distanceToLight,
                                                       cellU, cellV, clamp(light.halfAxisV.w, 0.0, 2.0));
            float distribution = DistributionGGX(N, H, filteredRoughness);
            float geometry = GeometrySmith(N, V, L, filteredRoughness);
            directSpecular += distribution * geometry * F / max(4.0 * NdotV * NdotL, 1e-5) * incoming;
        }
    }
}

void EvaluateAreaLighting(vec3 worldPosition, vec3 viewDirection, vec3 shadingNormal,
                          vec3 geometricNormal, vec3 albedo, float metallic, float perceptualRoughness,
                          out vec3 directDiffuse, out vec3 directSpecular, float dielectricF0) {
    EvaluateAreaLightingInternal(worldPosition, viewDirection, shadingNormal, geometricNormal,
                                 albedo, metallic, perceptualRoughness, directDiffuse, directSpecular,
                                 dielectricF0, true);
}

// Existing callers without a separate geometric normal retain the original
// integral, including its grazing response. Only explicit Ng callers apply
// the normal-map geometric horizon constraint.
void EvaluateAreaLighting(vec3 worldPosition, vec3 viewDirection, vec3 shadingNormal,
                          vec3 albedo, float metallic, float perceptualRoughness,
                          out vec3 directDiffuse, out vec3 directSpecular, float dielectricF0) {
    EvaluateAreaLightingInternal(worldPosition, viewDirection, shadingNormal, shadingNormal,
                                 albedo, metallic, perceptualRoughness, directDiffuse, directSpecular,
                                 dielectricF0, false);
}

void EvaluateAreaLighting(vec3 worldPosition, vec3 viewDirection, vec3 shadingNormal,
                          vec3 albedo, float metallic, float perceptualRoughness,
                          out vec3 directDiffuse, out vec3 directSpecular) {
    EvaluateAreaLighting(worldPosition, viewDirection, shadingNormal, albedo, metallic,
                         perceptualRoughness, directDiffuse, directSpecular, 0.05);
}
)GLSL";
}

} // namespace Render
