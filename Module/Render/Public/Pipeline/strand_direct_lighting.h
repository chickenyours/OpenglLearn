#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>

namespace Render {
struct StrandTextureTable;
inline constexpr std::uint32_t StrandSourceDirectBinding=12;
inline constexpr std::uint32_t StrandSourceIesTextureSlot=7;
inline constexpr std::uint32_t MaxStrandSourceLights=64;
// Native LightParameter stride112. Color already contains the producer's
// photometric conversion. Half-pairs retain the native coefficient precision.
struct alignas(16) StrandSourcePunctualLight {
    glm::vec4 positionRange{0};
    glm::vec4 directionFalloff{0,0,-1,1};
    glm::vec4 attenuation{0,0,1,0}; // z=A, w=B, used by cone
    glm::vec4 colorTolerance{0}; // w=packed half(c1,c2)
    glm::ivec4 shadowIes{-1,-1,0,0};
    glm::vec4 extended{0}; // x=packed half(exponent,c0), y=start distance
    glm::vec4 unusedIntensityScale{0};
};
static_assert(sizeof(StrandSourcePunctualLight)==112);
static_assert(offsetof(StrandSourcePunctualLight,colorTolerance)==48);
static_assert(offsetof(StrandSourcePunctualLight,shadowIes)==64);
static_assert(offsetof(StrandSourcePunctualLight,extended)==80);
inline float StrandPackSourceHalfPair(float low,float high){
    return std::bit_cast<float>(glm::packHalf2x16(glm::vec2(low,high)));
}
inline glm::vec2 StrandUnpackSourceHalfPair(float value){
    return glm::unpackHalf2x16(std::bit_cast<std::uint32_t>(value));
}
struct StrandSourceDirectSettings {
    bool enabled=false;
    std::uint32_t count=0;
    float distanceToSourceMetres=1.f;
    std::array<StrandSourcePunctualLight,MaxStrandSourceLights> lights{};
    // Rows contain source IES response indexed by dot(L,d)*.5+.5. A null
    // table is an explicitly declared unit-IES study, not a native capture.
    std::shared_ptr<const StrandTextureTable> iesTable;
};
struct alignas(16) StrandSourceDirectConstants {
    glm::vec4 options{0}; // enabled,count,IES table available,world-distance to source metres
    std::array<StrandSourcePunctualLight,MaxStrandSourceLights> lights{};
};
static_assert(sizeof(StrandSourceDirectConstants)==16+112*MaxStrandSourceLights);
inline StrandSourceDirectConstants MakeStrandSourceDirectConstants(const StrandSourceDirectSettings& settings){
    if(settings.count>MaxStrandSourceLights)throw std::invalid_argument("Too many source strand lights");
    StrandSourceDirectConstants result;
    if(!settings.enabled)return result;
    if(!std::isfinite(settings.distanceToSourceMetres)||settings.distanceToSourceMetres<=0)
        throw std::invalid_argument("Invalid source strand light distance scale");
    const auto finite=[](glm::vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);};
    for(unsigned i=0;i<settings.count;++i){
        const auto& l=settings.lights[i];
        const auto c12=StrandUnpackSourceHalfPair(l.colorTolerance.w);
        const auto ec0=StrandUnpackSourceHalfPair(l.extended.x);
        if(!finite(glm::vec3(l.positionRange))||!std::isfinite(l.positionRange.w)||l.positionRange.w<=0||
           !finite(glm::vec3(l.directionFalloff))||!std::isfinite(l.directionFalloff.w)||l.directionFalloff.w<=0||
           !finite(glm::vec3(l.colorTolerance))||glm::any(glm::lessThan(glm::vec3(l.colorTolerance),glm::vec3(0)))||
           !std::isfinite(l.attenuation.z)||!std::isfinite(l.attenuation.w)||
           !std::isfinite(l.extended.y)||l.extended.y<0||l.extended.y>=l.positionRange.w||
           !std::isfinite(c12.x)||!std::isfinite(c12.y)||c12.x<0||c12.y<0||
           !std::isfinite(ec0.x)||!std::isfinite(ec0.y)||ec0.x<=0||ec0.y<0||
           (ec0.y==0&&c12.x==0&&c12.y==0)||l.shadowIes.y< -1)
            throw std::invalid_argument("Invalid source strand light coefficients");
        result.lights[i]=l;
    }
    result.options={1.f,float(settings.count),settings.iesTable?1.f:0.f,settings.distanceToSourceMetres};
    return result;
}
}
