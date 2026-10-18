#pragma once
#include <cmath>
#include <cstdint>
#include <memory>
#include <glm/glm.hpp>
#include "Render/Public/Pipeline/spot_light.h"
#include "Render/Public/Pipeline/area_light_shadows.h"
namespace Render {
class SurfaceCacheScene;
class ProbeSdfScene;
// Probe-radiance filtering precedes integration, reconstruction and temporal
// accumulation. Visibility and hit-angle checks preserve geometry/contact edges.
struct SurfaceCacheSpatialSettings {
    bool enabled=true,visibilityCheck=true;
    std::uint32_t passes=3,halfKernelSize=1;
    float positionWeightScale=1000,maxHitAngleDegrees=10,normalThresholdDegrees=30;
    float strength=1,planeDistanceThreshold=.01f;
};
inline bool ValidateSurfaceCacheSpatialSettings(const SurfaceCacheSpatialSettings& s) {
    return s.passes>=1&&s.passes<=4&&s.halfKernelSize>=1&&s.halfKernelSize<=2&&
        std::isfinite(s.positionWeightScale)&&s.positionWeightScale>=1&&s.positionWeightScale<=100000&&
        std::isfinite(s.maxHitAngleDegrees)&&s.maxHitAngleDegrees>=1&&s.maxHitAngleDegrees<=60&&
        std::isfinite(s.normalThresholdDegrees)&&s.normalThresholdDegrees>=1&&s.normalThresholdDegrees<=90&&
        std::isfinite(s.strength)&&s.strength>=0&&s.strength<=1&&
        std::isfinite(s.planeDistanceThreshold)&&s.planeDistanceThreshold>=.0001f&&s.planeDistanceThreshold<=.1f;
}
// Final-gather history, independent of TAA. Moving objects currently invalidate
// the whole history; camera motion reprojects and validates individual taps.
struct SurfaceCacheTemporalSettings {
    // Use a gentle moving clamp on interiors and a short, conservative history
    // at moving geometry edges. Both remain independent of final-image TAA.
    bool enabled=true,reset=false,neighborhoodClamp=true,edgeProtection=true,clampSlowMotion=true;
    std::uint32_t maxFrames=10,directionPhases=8;
    float distanceThreshold=.01f,normalThresholdDegrees=45;
    float movingMaxFrames=8,motionThresholdPixels=3,clampSigma=6,edgeMaxFrames=2;
};
inline bool ValidateSurfaceCacheTemporalSettings(const SurfaceCacheTemporalSettings& s) {
    return s.maxFrames>=1&&s.maxFrames<=64&&s.directionPhases>=1&&s.directionPhases<=64&&
        std::isfinite(s.distanceThreshold)&&s.distanceThreshold>=.0001f&&s.distanceThreshold<=.1f&&
        std::isfinite(s.normalThresholdDegrees)&&s.normalThresholdDegrees>=1&&s.normalThresholdDegrees<=90&&
        std::isfinite(s.movingMaxFrames)&&s.movingMaxFrames>=1&&s.movingMaxFrames<=64&&
        std::isfinite(s.motionThresholdPixels)&&s.motionThresholdPixels>=.01f&&s.motionThresholdPixels<=64&&
        std::isfinite(s.clampSigma)&&s.clampSigma>=.5f&&s.clampSigma<=10&&
        std::isfinite(s.edgeMaxFrames)&&s.edgeMaxFrames>=1&&s.edgeMaxFrames<=64;
}
// First-bounce diffuse GI: adaptive screen probes integrate Surface Cache
// radiance, followed by geometry-aware reconstruction. The independent
// per-pixel software BVH path remains a reference. Optional final-gather history
// reprojects the reconstructed signal; raw GI remains available independently.
struct SurfaceCacheGiSettings {
    bool enabled=false,probeDiagnostics=false,useScreenProbes=true;
    bool cacheBilinear=true; // same-card lighting filtering with depth/normal visibility
    bool collectProbeCoverage=true; // optional diagnostic; coverage view always collects it
    std::uint32_t reconstructionDebug=0; // indirect view only: 0 real, 1 constant, 2 base/adaptive contrast
    // Keep cached rectangle-light depth shadows available when diffuse GI is off.
    // Requires the opt-in opaque PBR Surface Cache area-shadow shader variant.
    bool areaLightShadows=false;
    AreaLightShadowSettings areaShadows;
    std::uint32_t view=0,raysPerPixel=1; // total, cache, indirect, coverage, atlas, history length, probe coverage
    float maxDistance=30,intensity=1,rayBias=.01f;
    std::shared_ptr<const SurfaceCacheScene> cache;
    std::shared_ptr<const ProbeSdfScene> scene;
    SurfaceCacheTemporalSettings temporal;
    SurfaceCacheSpatialSettings spatial;
};
inline bool ValidateSurfaceCacheGiSettings(const SurfaceCacheGiSettings& s) {
    return s.view<=6&&s.reconstructionDebug<=2&&s.raysPerPixel>=1&&s.raysPerPixel<=32&&ValidateSurfaceCacheTemporalSettings(s.temporal)&&ValidateSurfaceCacheSpatialSettings(s.spatial)&&
        ValidateAreaLightShadowSettings(s.areaShadows)&&s.areaShadows.nearPlane<s.maxDistance&&
        std::isfinite(s.maxDistance)&&s.maxDistance>=.1f&&s.maxDistance<=100&&
        std::isfinite(s.intensity)&&s.intensity>=0&&s.intensity<=10&&
        std::isfinite(s.rayBias)&&s.rayBias>=.0001f&&s.rayBias<=.2f&&
        (!(s.enabled||s.areaLightShadows)||(s.cache&&s.scene));
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
    glm::vec4 reject{.01f,.70710678f,8,3}; // distance, normal cosine, moving frame limit, motion pixels
    glm::vec4 clamp{6,2,.002f,2}; // sigma, clamp mode (0 off / 1 fast / 2 moving), distance floor, edge cap (0 disables)
};
static_assert(sizeof(SurfaceCacheTemporalConstants)==240);
inline constexpr std::uint32_t SurfaceCacheTemporalBinding=13;
struct SurfaceCacheSpatialConstants {
    glm::ivec4 options{0,1,1,0}; // pass, radius, visibility test, reserved
    glm::vec4 filter{1000,.174532925f,.8660254f,1}; // depth scale, hit radians, normal cosine, blend
    glm::vec4 parameters{.01f,.01f,0,0}; // ray bias, relative plane tolerance, reserved
};
static_assert(sizeof(SurfaceCacheSpatialConstants)==48);
inline constexpr std::uint32_t SurfaceCacheSpatialBinding=14;
}
