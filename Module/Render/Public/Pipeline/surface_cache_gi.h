#pragma once
#include <cmath>
#include <cstdint>
#include <memory>
#include <glm/glm.hpp>
#include "Render/Public/Pipeline/spot_light.h"
namespace Render {
class SurfaceCacheScene;
class ProbeSdfScene;
// Final-gather history, independent of TAA. Moving objects currently invalidate
// the whole history; camera motion reprojects and validates individual taps.
struct SurfaceCacheTemporalSettings {
    bool enabled=true,reset=false,neighborhoodClamp=true;
    std::uint32_t maxFrames=10,directionPhases=8;
    float distanceThreshold=.01f,normalThresholdDegrees=45;
    float movingMaxFrames=2,motionThresholdPixels=1,clampSigma=3;
};
inline bool ValidateSurfaceCacheTemporalSettings(const SurfaceCacheTemporalSettings& s) {
    return s.maxFrames>=1&&s.maxFrames<=64&&s.directionPhases>=1&&s.directionPhases<=64&&
        std::isfinite(s.distanceThreshold)&&s.distanceThreshold>=.0001f&&s.distanceThreshold<=.1f&&
        std::isfinite(s.normalThresholdDegrees)&&s.normalThresholdDegrees>=1&&s.normalThresholdDegrees<=90&&
        std::isfinite(s.movingMaxFrames)&&s.movingMaxFrames>=1&&s.movingMaxFrames<=64&&
        std::isfinite(s.motionThresholdPixels)&&s.motionThresholdPixels>=.01f&&s.motionThresholdPixels<=64&&
        std::isfinite(s.clampSigma)&&s.clampSigma>=.5f&&s.clampSigma<=10;
}
// First-bounce diffuse GI: adaptive screen probes integrate Surface Cache
// radiance, followed by geometry-aware reconstruction. The independent
// per-pixel software BVH path remains a reference. Optional final-gather history
// reprojects the reconstructed signal; raw GI remains available independently.
struct SurfaceCacheGiSettings {
    bool enabled=false,probeDiagnostics=false,useScreenProbes=true;
    std::uint32_t view=0,raysPerPixel=1; // total, cache, indirect, coverage, atlas, history length
    float maxDistance=30,intensity=1,rayBias=.01f;
    std::shared_ptr<const SurfaceCacheScene> cache;
    std::shared_ptr<const ProbeSdfScene> scene;
    SurfaceCacheTemporalSettings temporal;
};
inline bool ValidateSurfaceCacheGiSettings(const SurfaceCacheGiSettings& s) {
    return s.view<=5&&s.raysPerPixel>=1&&s.raysPerPixel<=32&&ValidateSurfaceCacheTemporalSettings(s.temporal)&&
        std::isfinite(s.maxDistance)&&s.maxDistance>=.1f&&s.maxDistance<=100&&
        std::isfinite(s.intensity)&&s.intensity>=0&&s.intensity<=10&&
        std::isfinite(s.rayBias)&&s.rayBias>=.0001f&&s.rayBias<=.2f&&
        (!s.enabled||(s.cache&&s.scene));
}
struct SurfaceCacheGiConstants {
    glm::mat4 inverseView{1};
    glm::ivec4 atlas{1024,1024,0,0}; // width, plane stride, texel count, cards
    glm::ivec4 geometry{0,1024,0,0}; // triangles offset, width, nodes, triangles
    glm::ivec4 screen{1,1,1,0};
    glm::vec4 parameters{30,.01f,1,0};
    glm::vec4 sunDirectionIntensity{0,-1,0,0},sunColor{1};
    SpotLightConstants spot;
};
static_assert(sizeof(SurfaceCacheGiConstants)==208);
inline constexpr std::uint32_t SurfaceCacheGiBinding=12;
struct SurfaceCacheTemporalConstants {
    glm::mat4 currentInverseView{1},previousViewProjection{1},previousView{1};
    glm::ivec4 screen{1,1,0,10}; // width, height, history valid, frame limit
    glm::vec4 reject{.01f,.70710678f,2,1}; // distance, normal cosine, moving frame limit, motion pixels
    glm::vec4 clamp{3,1,.002f,0}; // sigma, moving-region clamp, absolute distance floor, reserved
};
static_assert(sizeof(SurfaceCacheTemporalConstants)==240);
inline constexpr std::uint32_t SurfaceCacheTemporalBinding=13;
}
