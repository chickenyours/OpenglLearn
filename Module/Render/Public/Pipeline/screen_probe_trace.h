#pragma once
#include <cstdint>
#include <limits>
#include <cmath>
#include "Render/Public/Pipeline/probe_sdf_tracing.h"
namespace Render {
enum class ScreenProbeTraceStatus : std::uint32_t {
    Inactive=0, Hit=1, OffScreen=2, NoHit=3, UncertainDepth=4,
    Backface=5, OriginUnavailable=6, InvalidRadiance=7, StepBudget=8,
    MeshHit=9,GlobalHit=10,MeshHitUnreadable=11,GlobalHitUnreadable=12,
    SdfNoHit=13,OutsideGlobalCoverage=14,SdfStepBudget=15,SdfOriginInvalid=16,
    ScreenHitUnreadable=17
};
struct ScreenProbeTraceSettings {
    bool enabled=false, showPanel=true;
    std::uint32_t rayResolution=8, maxSteps=128, maxMip=10, displayMode=0;
    float maxDistance=12, thickness=.01f, normalBias=.02f, relativeThickness=.02f;
    std::uint32_t selectedProbe=std::numeric_limits<std::uint32_t>::max();
    // GI rays cover a normal-oriented cosine hemisphere. The original
    // equal-area sphere remains available for the tracing diagnostic study.
    bool cosineHemisphere=false;
    // Experimental: trace once per ray in a geometry invocation and rasterize
    // its original four atlas texels. Unsupported backends retain fragment tracing.
    bool singleInvocation=false;
    // Trace once in a fragment invocation, write four outputs, then pack the
    // unchanged public ray atlas. This retains the ordinary fragment estimator.
    bool singlePassOutputs=false;
    // GPU-only classify/certify + compact pending SDF/GDF work. Requires the
    // single-evaluation output contract; unsupported backends retain MRT tracing.
    bool compactSdfRays=false;
    // Independent two-dimensional jitter per probe and angular stratum. A
    // fixed seed is useful without temporal accumulation; callers may change
    // it explicitly to inspect convergence without reusing any history.
    std::uint32_t samplingSeed=0;
    ProbeSdfTracingSettings sdf;
};
inline bool ValidateScreenProbeTraceSettings(const ScreenProbeTraceSettings& s) {
    return (s.rayResolution==4||s.rayResolution==8||s.rayResolution==16)&&s.maxSteps>=8&&s.maxSteps<=512&&s.maxMip<=10&&s.displayMode<=1&&s.samplingSeed<=0x1fffffffu&&
        std::isfinite(s.maxDistance)&&s.maxDistance>=.1f&&s.maxDistance<=100&&
        std::isfinite(s.thickness)&&s.thickness>=0&&s.thickness<=1&&
        std::isfinite(s.normalBias)&&s.normalBias>=.0001f&&s.normalBias<=.2f&&
        std::isfinite(s.relativeThickness)&&s.relativeThickness>=0&&s.relativeThickness<=.1f&&ValidateProbeSdfTracingSettings(s.sdf);
}
inline constexpr std::uint32_t ScreenProbeTraceMaxHzbLevels=11, ScreenProbeTraceTexelsPerRay=4;
}
