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
        ValidateScreenProbeTraceSettings(s.trace);
}

// All three debug atlases are RGBA32F. One logical probe uses two adjacent
// horizontal texels: (world position, status), then (world normal, view depth).
// Uniform status: 0 invalid, 1 valid. Marked: 0 invalid, 1 covered, 2 uncovered.
// Spawned: 0 rejected, 1 active. The second texel's alpha is positive depth.
// Candidate slot (tile.x*candidateCount+sample, tile.y) is sparse, not compacted.
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
};
static_assert(sizeof(ScreenProbePlacementConstants)==368);
inline constexpr std::uint32_t ScreenProbePlacementBinding=9;
} // namespace Render
