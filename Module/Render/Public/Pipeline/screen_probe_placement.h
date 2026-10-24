#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <glm/glm.hpp>
#include "Render/Public/Pipeline/screen_probe_trace.h"

namespace Render {
// A placement experiment, independent of GI. Visible GBuffer samples become
// world-space probes. Optional screen tracing is diagnostic and does not feed GI.
struct ScreenProbePlacementSettings {
    bool enabled=false, adaptive=true, freeze=false, reset=false;
    std::uint32_t baseSpacing=32, candidateCount=8;
    // At this relative distance to the query surface plane, a probe's depth
    // weight is .01. The default gives UE 5.7.1's exp2(-200 * distance/depth).
    float relativeDepthThreshold=.033219281f;
    float markerRadiusPixels=3;
    // GI-only junction refinement. The GI allocator additionally reserves two
    // distinct visible surface representatives per subcell within candidateCount.
    // Reconstruction uses their actual positions and subcell footprint; support
    // limits the junction allocation band. Standalone placement is unchanged.
    bool cornerRefinement=true;
    // Three captured-depth segment taps reject local occluders between the
    // receiver and a probe. Shared by allocation/reconstruction; no new GI rays.
    bool reconstructionVisibility=true;
    // GI-only coarse interpolation between the actual projected uniform-probe
    // pixels. Disable to compare the legacy expanded bilinear fractions.
    bool continuousInterpolation=true;
    // GI lighting reuse follows a continuous surface's midpoint tangent plane.
    // Physical surface clustering keeps its stricter two-endpoint plane test.
    // Disable for the original reconstruction/spatial geometry weights.
    bool curvatureAwareWeights=true;
    float cornerDetectionRadius=.75f,cornerProbeSupport=.5f,cornerNormalAngleDegrees=45;
    // GI-only continuous-surface refinement. Reuse a reserved representative
    // as a subcell or its coarse support approaches this geometric error.
    // Only curvature-only weights smoothly activate from half to 1.5 times
    // the normalized error budget; at the budget they have half weight.
    // Plane error is in screen-pixel surface footprints, not world units.
    bool curvatureRefinement=true;
    float curvatureNormalAngleDegrees=10,curvaturePlaneErrorPixels=.75f;
    // 0: active markers; 1: all candidates; 2: live screen coverage.
    // Once frozen, coverage falls back to active world markers because its
    // screen tile indices belong to the original placement camera.
    std::uint32_t debugView=0;
    // Automatic placement advances an eight-frame sequence after completion.
    // An explicit value selects a fixed phase (modulo eight).
    std::uint32_t frameIndex=std::numeric_limits<std::uint32_t>::max();
    ScreenProbeTraceSettings trace;
};

inline bool ValidateScreenProbePlacementSettings(const ScreenProbePlacementSettings& s) {
    return s.baseSpacing>=4 && s.baseSpacing<=128 &&
        (s.candidateCount==4 || s.candidateCount==8 || s.candidateCount==16) &&
        std::isfinite(s.relativeDepthThreshold) && s.relativeDepthThreshold>=.0001f && s.relativeDepthThreshold<=1 &&
        std::isfinite(s.markerRadiusPixels) && s.markerRadiusPixels>=1 && s.markerRadiusPixels<=16 && s.debugView<=2&&
        std::isfinite(s.cornerDetectionRadius)&&s.cornerDetectionRadius>=.25f&&s.cornerDetectionRadius<=2&&
        std::isfinite(s.cornerProbeSupport)&&s.cornerProbeSupport>=.25f&&s.cornerProbeSupport<=1.5f&&
        std::isfinite(s.cornerNormalAngleDegrees)&&s.cornerNormalAngleDegrees>=15&&s.cornerNormalAngleDegrees<=75&&
        std::isfinite(s.curvatureNormalAngleDegrees)&&s.curvatureNormalAngleDegrees>=1&&s.curvatureNormalAngleDegrees<=60&&
        std::isfinite(s.curvaturePlaneErrorPixels)&&s.curvaturePlaneErrorPixels>=.05f&&s.curvaturePlaneErrorPixels<=8&&
        ValidateScreenProbeTraceSettings(s.trace);
}

// All three debug atlases are RGBA32F. One logical probe uses two adjacent
// horizontal texels: (world position, status), then (world normal, view depth).
// Uniform: 0 invalid, 1+intersectionStrength valid (strength in [0,1]).
// Marked: 0 invalid, 1 covered, 2 coverage / distinct-surface obligation,
//         3+strength GI junction refinement, 5+activation curvature refinement.
// Spawned: 0 rejected, 1 coverage representative, 2+strength GI junction refinement,
//          4+activation curvature-only representative (activation in (0,1]).
// The second texel's alpha is positive depth.
// Candidate slot (tile.x*candidateCount+sample, tile.y) is sparse, not compacted.
// GI: sample/2 identifies a subcell, sample%2 its two surface representatives;
// 4/8/16 slots retain the same budget with 2x1/2x2/4x2 subcells respectively.
// Atlas capacities are not active counts. Read back even texels to count probes.
struct ScreenProbePlacementConstants {
    glm::mat4 inverseView{1}, inverseProjection{1}, viewProjection{1}, view{1};
    glm::ivec4 grid{1,1,8,32}; // uniform width/height, candidate count, spacing
    glm::ivec4 screen{1,1,0,0}; // source width/height, temporal phase, debug view
    glm::vec4 options{.033219281f,3,1,0}; // plane threshold, radius, adaptive, draw set
    glm::ivec4 tracing{8,128,10,0}; // ray resolution, max steps, max mip, enabled
    glm::vec4 traceParams{12,.01f,.02f,.02f}; // max distance, absolute thickness, normal bias, relative thickness
    glm::ivec4 traceAtlas{1,1,0,0}; // width, height, uniform slots, total probe slots
    glm::ivec4 traceDisplay{0,-1,1,1}; // radiance/status mode, selected probe, panel enabled, HZB levels
    glm::vec4 refinement{0,.75f,.5f,.70710678f}; // flags: bit0 junctions/bit1 visibility/bit2 curvature/bit3 curved weights/bit4 runtime matching-frame filtering; detection/support, cosine
    glm::vec4 curvature{.98480775f,.75f,0,1}; // normal cosine, plane pixel error, safe origins (2: native-only directions), continuous interpolation
};
static_assert(sizeof(ScreenProbePlacementConstants)==400);
inline constexpr std::uint32_t ScreenProbePlacementBinding=9;
} // namespace Render
