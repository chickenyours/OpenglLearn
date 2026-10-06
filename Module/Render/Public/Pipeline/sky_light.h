#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <vector>
#include <glm/glm.hpp>

namespace Render {
inline constexpr std::uint32_t SkyLightBinding = 11;
inline constexpr std::uint32_t SkyReflectionTextureSlot = 10, SkyBrdfTextureSlot = 11;
inline constexpr std::uint32_t SkyReflectionWidth = 128, SkyReflectionHeight = 64, SkyReflectionLevels = 6;
inline constexpr std::uint32_t SkyAtlasWidth = SkyReflectionWidth + 2;
inline constexpr std::uint32_t SkyAtlasHeight = (SkyReflectionHeight + 2) * SkyReflectionLevels;

// Immutable after creation. Source pixels are scene-linear RGB, row zero is
// north (+Y); longitude zero points +X and increases toward +Z. No sRGB decode.
// SH already contains the cosine convolution divided by pi (Lambert response).
class SkyEnvironment {
public:
    const std::array<glm::vec4,9>& DiffuseSH() const noexcept { return diffuseSH_; }
    const std::vector<glm::vec4>& ReflectionAtlas() const noexcept { return reflectionAtlas_; }
private:
    std::array<glm::vec4,9> diffuseSH_{};
    std::vector<glm::vec4> reflectionAtlas_;
    friend std::shared_ptr<const SkyEnvironment> BakeSkyEnvironment(std::uint32_t, std::uint32_t, std::span<const glm::vec3>);
};
// CPU asset preparation; call when loading/changing an environment, not per frame.
std::shared_ptr<const SkyEnvironment> BakeSkyEnvironment(std::uint32_t width, std::uint32_t height,
                                                       std::span<const glm::vec3> linearRadiance);
std::shared_ptr<const SkyEnvironment> MakeProceduralSkyEnvironment(
    glm::vec3 zenith = {0.18f,0.38f,0.80f}, glm::vec3 horizon = {0.75f,0.82f,0.90f},
    glm::vec3 ground = {0.12f,0.10f,0.08f});
std::shared_ptr<const SkyEnvironment> DefaultSkyEnvironment();
const std::vector<glm::vec4>& SkyBrdfIntegrationLut();
inline constexpr std::uint32_t SkyBrdfLutSize = 64;

struct SkyLightSettings {
    bool enabled = false; // Opt-in preserves existing pipeline callers.
    bool background = true;
    float intensity = 1, diffuseStrength = 1, specularStrength = 1;
    float rotation = 0; // radians around world +Y
    float occlusionStrength = 1;
    // Null selects the shared procedural daylight environment.
    std::shared_ptr<const SkyEnvironment> environment;
};
bool ValidateSkyLightSettings(const SkyLightSettings&, std::string* error = nullptr);
struct alignas(16) SkyLightConstants {
    std::array<glm::vec4,9> diffuseSH{};
    glm::vec4 options{0}; // enabled, intensity, diffuse strength, specular strength
    glm::vec4 transform{1,0,0,1}; // cos/sin rotation, background, AO strength
};
static_assert(std::is_trivially_copyable_v<SkyLightConstants>);
static_assert(offsetof(SkyLightConstants, options) == 144);
static_assert(offsetof(SkyLightConstants, transform) == 160);
static_assert(sizeof(SkyLightConstants) == 176);
SkyLightConstants MakeSkyLightConstants(const SkyLightSettings&, const SkyEnvironment&);

inline std::string SkyLightGLSL() {
    return "\n#define SKY_WIDTH " + std::to_string(SkyReflectionWidth) +
        "\n#define SKY_HEIGHT " + std::to_string(SkyReflectionHeight) +
        "\n#define SKY_LEVELS " + std::to_string(SkyReflectionLevels) + "\n" + R"GLSL(
layout(std140,binding=11) uniform SkyLightData {
    vec4 skySH[9];
    vec4 skyOptions;
    vec4 skyTransform;
};
layout(binding=10) uniform sampler2D skyReflection;
layout(binding=11) uniform sampler2D skyBrdf;
vec3 SkyDirection(vec3 d) {
    return vec3(skyTransform.x*d.x+skyTransform.y*d.z,d.y,
               -skyTransform.y*d.x+skyTransform.x*d.z);
}
vec3 SkyDiffuse(vec3 n) {
    n=SkyDirection(n);
    return max(vec3(0),skySH[0].rgb*.282095 +
        .488603*(skySH[1].rgb*n.y+skySH[2].rgb*n.z+skySH[3].rgb*n.x)+
        1.092548*(skySH[4].rgb*n.x*n.y+skySH[5].rgb*n.y*n.z+skySH[7].rgb*n.x*n.z)+
        skySH[6].rgb*(.315392*(3.0*n.z*n.z-1.0))+
        skySH[8].rgb*(.546274*(n.x*n.x-n.y*n.y)));
}
vec3 SkyReflectionLevel(vec2 uv,float level) {
    // Each layer has one wrapping longitude gutter and clamped pole gutters.
    vec2 pixel=vec2(1.0+uv.x*float(SKY_WIDTH),1.0+uv.y*float(SKY_HEIGHT)+level*float(SKY_HEIGHT+2));
    return max(textureLod(skyReflection,pixel/vec2(SKY_WIDTH+2,(SKY_HEIGHT+2)*SKY_LEVELS),0).rgb,vec3(0));
}
vec3 SkyRadiance(vec3 direction,float roughness) {
    vec3 d=SkyDirection(normalize(direction));
    vec2 uv=vec2(fract(atan(d.z,d.x)/6.28318530718),acos(clamp(d.y,-1.0,1.0))/3.14159265359);
    float level=clamp(roughness,0.0,1.0)*float(SKY_LEVELS-1);
    return mix(SkyReflectionLevel(uv,floor(level)),SkyReflectionLevel(uv,min(floor(level)+1.0,float(SKY_LEVELS-1))),fract(level));
}
vec3 EvaluateSkyLighting(vec3 N,vec3 V,vec3 albedo,float metallic,float roughness,
                        vec3 F0,float ao,float diffuseFraction,float localVisibility,
                        out vec3 diffuseRadiance,out vec3 diffuseWeight) {
    diffuseRadiance=vec3(0);
    diffuseWeight=(vec3(1)-F0)*(1.0-metallic);
    if(skyOptions.x<.5 || skyOptions.y<=0.0) return vec3(0);
    float NoV=max(dot(N,V),.001);
    vec2 dfg=texture(skyBrdf,vec2(NoV,roughness)).rg;
    // Approximate GGX multiple-scattering energy compensation keeps rough
    // conductors from losing most of their reflected environment energy.
    vec3 compensation=vec3(1)+F0*(1.0/max(dfg.x+dfg.y,.001)-1.0);
    vec3 specularWeight=clamp((F0*dfg.x+dfg.y)*compensation,vec3(0),vec3(1));
    diffuseWeight=(vec3(1)-specularWeight)*(1.0-metallic);
    float visibility=mix(1.0,clamp(ao,0.0,1.0),skyTransform.w);
    float specularAO=clamp(pow(NoV+visibility,exp2(-16.0*roughness-1.0))-1.0+visibility,0.0,1.0);
    vec3 diffuse=SkyDiffuse(N)*albedo*diffuseWeight*visibility*skyOptions.z*diffuseFraction;
    diffuseRadiance=diffuse*skyOptions.y;
    vec3 specular=SkyRadiance(reflect(-V,N),roughness)*specularWeight*specularAO*skyOptions.w*localVisibility;
    return (diffuse+specular)*skyOptions.y;
}
)GLSL";
}
} // namespace Render
