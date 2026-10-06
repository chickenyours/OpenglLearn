#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <glm/glm.hpp>
#include "Render/Public/Pipeline/sky_light.h"
#include "Render/Public/Pipeline/area_lights.h"
#include "Render/Public/Pipeline/spot_light.h"
#include "Render/Public/Pipeline/realtime_gi_query.h"

namespace Render {
inline constexpr std::uint32_t DiffuseProbeBinding=12, DiffuseProbeSettingsBinding=13, MaxDiffuseProbes=64;
inline constexpr std::uint32_t DiffuseProbeVisibilityBinding=14;
// World-space opaque light transport geometry. Texture/material reduction is
// owned by scene extraction; transparent geometry is deliberately excluded.
struct ProbeTriangle {
    glm::vec3 a{0},b{0},c{0};
    glm::vec3 diffuseReflectance{.5f}; // linear albedo * nonmetallic diffuse weight
    glm::vec3 emission{0};
    bool twoSidedEmission=false;
    glm::vec3 specularF0{.04f};
    float roughness=.5f;
    // A paired analytic light owns this emitter's direct illumination. Retain
    // visible radiance, but exclude it from transport to avoid double counting.
    bool analyticEmission=false;
};
struct ProbeBakeSettings {
    glm::vec3 origin{-2.5f,.4f,-2.5f}, spacing{1.666667f,1.066667f,1.666667f};
    glm::uvec3 counts{4,4,4};
    std::uint32_t raysPerProbe=256, diffuseBounces=2;
    float maxDistance=30, rayBias=.005f;
};
struct ProbeBakeLighting {
    SkyLightSettings sky;
    glm::vec3 sunDirection{0,-1,0}, sunColor{1};
    float sunIntensity=0;
    std::array<glm::vec4,4> pointPositions{}, pointColors{};
    AreaLightsSettings areaLights;
    SpotLightSettings spotLight;
};
struct alignas(16) DiffuseProbeData {
    glm::vec4 positionValid{0};
    // xyz=cosine-convolved total indirect radiance/pi, w=unconvolved sky visibility SH.
    std::array<glm::vec4,9> sh{};
    // 4x4 octahedral center-ray first-hit distances, packed four per vec4.
    std::array<glm::vec4,4> distances{};
};
struct alignas(16) DiffuseProbeConstants {
    glm::vec4 origin{0}, spacing{1}, counts{0}, limits{0};
    std::array<DiffuseProbeData,MaxDiffuseProbes> probes{};
};
static_assert(sizeof(DiffuseProbeData)==224);
static_assert(sizeof(DiffuseProbeConstants)==14400); // below OpenGL's guaranteed 16 KiB UBO limit
static_assert(std::is_trivially_copyable_v<DiffuseProbeConstants>);
// Cached first-hit planes keep a receiver above a floor visible even when its
// distance to a probe exceeds the center-ray depth. Separate block fits 16 KiB.
struct alignas(16) DiffuseProbeVisibilityConstants {
    std::array<glm::vec4,MaxDiffuseProbes*16> planes{};
};
static_assert(sizeof(DiffuseProbeVisibilityConstants)==16384);
class DiffuseProbeVolume {
public:
    const DiffuseProbeConstants& Constants() const noexcept { return data_; }
    const DiffuseProbeVisibilityConstants& Visibility() const noexcept { return visibility_; }
    std::uint32_t ValidProbes() const noexcept { return validProbes_; }
    double BakeMilliseconds() const noexcept { return bakeMilliseconds_; }
private:
    DiffuseProbeConstants data_;
    DiffuseProbeVisibilityConstants visibility_;
    std::uint32_t validProbes_=0;
    double bakeMilliseconds_=0;
    friend std::shared_ptr<const DiffuseProbeVolume> BakeDiffuseProbeVolume(
        const ProbeBakeSettings&, std::span<const ProbeTriangle>, const ProbeBakeLighting&);
};
std::shared_ptr<const DiffuseProbeVolume> BakeDiffuseProbeVolume(
    const ProbeBakeSettings&, std::span<const ProbeTriangle>, const ProbeBakeLighting&);
struct DiffuseProbeSettings {
    bool enabled=false;
    float intensity=1, normalBias=.12f, visibilityBias=.15f;
    std::shared_ptr<const DiffuseProbeVolume> volume;
};
struct alignas(16) DiffuseProbeRuntimeConstants { glm::vec4 options{0}; };
bool ValidateDiffuseProbeSettings(const DiffuseProbeSettings&, std::string* error=nullptr);
struct ProbeLightingSample { glm::vec3 irradianceOverPi{0}; float skyVisibility=1, coverage=0; };
// CPU diagnostic query matching the shader, useful for asset bake validation.
ProbeLightingSample SampleDiffuseProbeVolume(const DiffuseProbeVolume&, glm::vec3 position,
    glm::vec3 normal, glm::vec3 reflectionDirection, float roughness=1, float normalBias=.12f, float visibilityBias=.15f);

inline std::string DiffuseProbeGLSL() {
    return RealtimeGiQueryGLSL()+R"GLSL(
struct DiffuseProbe { vec4 positionValid; vec4 sh[9]; vec4 distances[4]; };
layout(std140,binding=12) uniform DiffuseProbeDataBlock {
    vec4 probeOrigin,probeSpacing,probeCounts,probeLimits;
    DiffuseProbe diffuseProbes[64];
};
layout(std140,binding=13) uniform DiffuseProbeSettingsBlock { vec4 probeOptions; };
layout(std140,binding=14) uniform DiffuseProbeVisibilityBlock { vec4 probePlanes[1024]; };
vec2 ProbeOctahedral(vec3 d) {
    d/=max(abs(d.x)+abs(d.y)+abs(d.z),1e-8);
    vec2 p=d.xy;
    if(d.z<0.0) p=(vec2(1)-abs(p.yx))*mix(vec2(-1),vec2(1),greaterThanEqual(p,vec2(0)));
    return p*.5+.5;
}
float ProbeVisibility(int index,vec3 direction,vec3 delta,float distance) {
    vec2 pixel=ProbeOctahedral(direction)*4.0-.5;
    ivec2 lo=ivec2(floor(pixel)); vec2 f=fract(pixel); float value=0,planes=0;
    for(int y=0;y<2;++y) for(int x=0;x<2;++x) {
        ivec2 p=clamp(lo+ivec2(x,y),ivec2(0),ivec2(3)); int i=p.y*4+p.x;
        float weight=(x==0?1-f.x:f.x)*(y==0?1-f.y:f.y);
        value+=diffuseProbes[index].distances[i/4][i%4]*weight;
        vec4 plane=probePlanes[index*16+i];
        float side=dot(plane.xyz,delta)-plane.w;
        planes+=(dot(plane.xyz,plane.xyz)<.5?1.0:smoothstep(-max(probeOptions.w,.01),0.0,side))*weight;
    }
    return max(planes,1.0-smoothstep(value+probeOptions.w,value+probeOptions.w+max(probeOptions.w,.05),distance));
}
void ProbeBasis(vec3 n,out float b[9]) {
    b[0]=.282095; b[1]=.488603*n.y; b[2]=.488603*n.z; b[3]=.488603*n.x;
    b[4]=1.092548*n.x*n.y; b[5]=1.092548*n.y*n.z; b[6]=.315392*(3*n.z*n.z-1);
    b[7]=1.092548*n.x*n.z; b[8]=.546274*(n.x*n.x-n.y*n.y);
}
// Returns coverage even when all probes are rejected: a covered sealed region
// stays dark instead of falling back to the unoccluded global sky.
float EvaluateDiffuseProbes(vec3 position,vec3 N,vec3 R,float roughness,out vec3 irradiance,out float skyVisibility) {
    irradiance=vec3(0); skyVisibility=1;
    if(probeOptions.x<.5) return 0;
    if(probeOptions.x>1.5) {
        float coverage=EvaluateRealtimeGi(position,N,R,roughness,irradiance,skyVisibility);
        irradiance*=probeOptions.y; return coverage;
    }
    vec3 coord=(position-probeOrigin.xyz)/probeSpacing.xyz;
    vec3 outside=max(max(-.5-coord,coord-(probeCounts.xyz-.5)),vec3(0));
    float coverage=1.0-clamp(max(outside.x,max(outside.y,outside.z))*4.0,0.0,1.0);
    if(coverage<=0) return 0;
    vec3 query=position+N*probeOptions.z;
    vec3 grid=clamp((query-probeOrigin.xyz)/probeSpacing.xyz,vec3(0),probeCounts.xyz-1);
    ivec3 base=min(ivec3(floor(grid)),ivec3(probeCounts.xyz)-2); vec3 phase=grid-vec3(base);
    float basis[9],reflectionBasis[9]; ProbeBasis(N,basis); ProbeBasis(R,reflectionBasis);
    float sum=0,visibility=0;
    for(int z=0;z<2;++z) for(int y=0;y<2;++y) for(int x=0;x<2;++x) {
        ivec3 cell=base+ivec3(x,y,z);
        int index=cell.x+int(probeCounts.x)*(cell.y+int(probeCounts.y)*cell.z);
        if(diffuseProbes[index].positionValid.w<.5) continue;
        vec3 delta=query-diffuseProbes[index].positionValid.xyz; float distance=length(delta);
        vec3 direction=distance>1e-6?delta/distance:N;
        float visible=ProbeVisibility(index,direction,delta,distance);
        float normalWeight=pow(max(.05,dot(N,-direction)*.5+.5),2.0);
        vec3 f=mix(vec3(1)-phase,phase,vec3(x,y,z));
        float weight=f.x*f.y*f.z*visible*normalWeight;
        if(weight<=1e-8) continue;
        vec3 color=vec3(0); float sky=0;
        for(int i=0;i<9;++i) {
            color+=diffuseProbes[index].sh[i].rgb*basis[i];
            float convolution=i==0?1.0:(i<4?2.0/3.0:.25);
            sky+=diffuseProbes[index].sh[i].w*reflectionBasis[i]*mix(1.0,convolution,roughness*roughness);
        }
        irradiance+=max(color,vec3(0))*weight; visibility+=clamp(sky,0.0,1.0)*weight; sum+=weight;
    }
    if(sum>1e-8) { irradiance=irradiance/sum*probeOptions.y; skyVisibility=visibility/sum; }
    else skyVisibility=0;
    return coverage;
}
)GLSL";
}
} // namespace Render
