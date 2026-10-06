#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <glm/glm.hpp>

namespace Render {
// A finite point source with a smooth angular beam. Intensity is linear RGB
// radiant intensity on the beam axis; angles are half angles in radians.
// This light currently feeds world-space GI transport, including visibility.
struct SpotLightSettings {
    bool enabled=false;
    glm::vec3 position{0},direction{0,0,-1},intensity{60};
    float innerAngle=.174532925f,outerAngle=.34906585f,sourceRadius=.04f;
};
struct alignas(16) SpotLightConstants {
    glm::vec4 positionRadius{0};
    glm::vec4 directionOuter{0}; // xyz=unit forward direction; w=cos(outerAngle)
    glm::vec4 intensityInner{0}; // xyz=on-axis intensity; w=cos(innerAngle)
};
static_assert(std::is_trivially_copyable_v<SpotLightConstants>);
static_assert(offsetof(SpotLightConstants,positionRadius)==0);
static_assert(offsetof(SpotLightConstants,directionOuter)==16);
static_assert(offsetof(SpotLightConstants,intensityInner)==32);
static_assert(sizeof(SpotLightConstants)==48);
inline bool ValidateSpotLightSettings(const SpotLightSettings& light,std::string* error=nullptr) {
    if(error)error->clear();
    if(!light.enabled)return true;
    const auto fail=[&](const char* message){if(error)*error=message;return false;};
    const auto finite=[](glm::vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);};
    if(!finite(light.position)||glm::any(glm::greaterThan(glm::abs(light.position),glm::vec3(1000000))))
        return fail("Spot light position must be finite and within world bounds");
    if(!finite(light.direction)||glm::any(glm::greaterThan(glm::abs(light.direction),glm::vec3(1000000)))||
       glm::dot(light.direction,light.direction)<1e-12f)
        return fail("Spot light direction must be finite and nonzero");
    if(!finite(light.intensity)||glm::any(glm::lessThan(light.intensity,glm::vec3(0)))||
       glm::any(glm::greaterThan(light.intensity,glm::vec3(1000000))))
        return fail("Spot light intensity must be finite and between zero and 1000000");
    if(!std::isfinite(light.innerAngle)||!std::isfinite(light.outerAngle)||light.innerAngle<0||
       light.outerAngle<.001f||light.innerAngle>=light.outerAngle||light.outerAngle>1.553343034f)
        return fail("Spot light half angles must satisfy 0 <= inner < outer <= 89 degrees");
    if(!std::isfinite(light.sourceRadius)||light.sourceRadius<0||light.sourceRadius>10000)
        return fail("Spot light source radius must be finite and between zero and 10000");
    return true;
}
inline SpotLightConstants MakeSpotLightConstants(const SpotLightSettings& light) {
    std::string error;
    if(!ValidateSpotLightSettings(light,&error))throw std::invalid_argument(error);
    SpotLightConstants result;
    if(!light.enabled||glm::all(glm::equal(light.intensity,glm::vec3(0))))return result;
    result.positionRadius=glm::vec4(light.position,light.sourceRadius);
    result.directionOuter=glm::vec4(glm::normalize(light.direction),std::cos(light.outerAngle));
    result.intensityInner=glm::vec4(light.intensity,std::cos(light.innerAngle));
    return result;
}
// Direction points from the source to the receiver. Smoothstep has a zero
// derivative at both beam boundaries, avoiding a hard lighting ring.
inline float SpotLightConeWeight(const SpotLightConstants& light,glm::vec3 sourceToReceiver) {
    const float phase=std::clamp((glm::dot(glm::vec3(light.directionOuter),sourceToReceiver)-light.directionOuter.w)/
        std::max(light.intensityInner.w-light.directionOuter.w,1e-6f),0.f,1.f);
    return phase*phase*(3-2*phase);
}
} // namespace Render
