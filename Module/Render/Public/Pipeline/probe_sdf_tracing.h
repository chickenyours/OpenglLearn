#pragma once
#include <memory>
#include <cstdint>
#include <array>
#include <cstddef>
#include <cmath>
#include <glm/glm.hpp>
namespace Render {
class ProbeSdfScene;
struct ProbeSdfTracingSettings {
    bool enabled=false,useMesh=true,useGlobal=true,showHitPoints=true;
    bool automaticQuality=false,visualizeSelected=false,showVoxelGrid=false;
    bool clipmapsEnabled=true,showClipmapLevels=true,showClipmapBounds=true;
    int clipmapLevel=-1; // Debug volume/slice selection; fallback traces remain automatic.
    std::uint32_t meshQuality=1,visualMode=0;
    float visualEpsilon=.006f;
    std::shared_ptr<const ProbeSdfScene> scene;
    float meshTraceDistance=2,hitEpsilon=.003f;
    std::uint32_t maxSteps=192,sliceView=0,selectedObject=2,sliceAxis=1;
    float slicePosition=.5f;
    // Exact integer address specialization for the resident 1024-wide atlases.
    bool fastAtlasAddressing=false;
    // Reuse already evaluated trace data without changing the ray estimator.
    bool reuseTraceSamples=false;
    // Opt-in triangle fallback through the resident Surface Cache AS. When
    // unavailable, mismatched, compacted or geometry-shader based, use SDFs.
    bool hardwareRayTracing=false;
    // Skip distance-field GPU mirrors only while the matching Surface Cache
    // native fragment/MRT path is actually available. Software/debug paths
    // retain their original uploads and catch up after skipped generations.
    bool nativeUploadPruning=false;
    // Reuse a strict native candidate only when software certification reaches
    // that original triangle. Bounds, nearest/tie order and fallback stay intact.
    bool reuseNativeCandidate=false;
    // In the supported shared-geometry triangle profile, replace hardware
    // candidate plus certification with one original software nearest query.
    // This bypasses distance-field stepping and overrides candidate hint reuse.
    bool directSoftwareRefinement=false;
    // Untimed diagnostic variant: direction.w records actually entered
    // PSRefine calls. Ordinary programs contain no counter instructions.
    bool collectRayDiagnostics=false;
    // Budget-changing native nearest policy: strict original-triangle interior
    // hits are accepted without a second complete software BVH certification.
    // Misses, edges, endpoints and invalid candidates keep software fallback.
    bool nativeInteriorTracing=false;
    bool deferUnusedGlobalField=false; // lazy CPU field rebuild; software/debug reads always catch up
    bool incrementalGeometry=false; // retain transformed triangles for unchanged instances
    bool snapshotRowCatchup=false; // catch retired immutable snapshot storage up through certified dirty rows
};
inline bool ValidateProbeSdfTracingSettings(const ProbeSdfTracingSettings& s) {
    return std::isfinite(s.meshTraceDistance)&&s.meshTraceDistance>=0&&s.meshTraceDistance<=100&&
        std::isfinite(s.hitEpsilon)&&s.hitEpsilon>=.0001f&&s.hitEpsilon<=.1f&&s.maxSteps>=8&&s.maxSteps<=512&&
        s.sliceView<=2&&s.sliceAxis<=2&&std::isfinite(s.slicePosition)&&s.slicePosition>=0&&s.slicePosition<=1&&
        s.meshQuality<=2&&s.visualMode<=3&&s.clipmapLevel>=-1&&s.clipmapLevel<=3&&
        std::isfinite(s.visualEpsilon)&&s.visualEpsilon>=.0001f&&s.visualEpsilon<=.1f;
}
struct ProbeSdfTraceConstants {
    glm::vec4 globalOrigin{0},globalSpacing{1};
    glm::ivec4 globalCounts{1},atlas{1},geometry{0};
    glm::vec4 trace{2,192,.003f,.1f};
    glm::ivec4 display{0,2,1,0};
    glm::vec4 slice{.5f,0,0,0};
    glm::ivec4 flags{1,1,12,0};
    std::array<glm::vec4,4> clipOrigins{}; // world origin.xyz / narrow-band max distance.w
    std::array<glm::vec4,4> clipSpacing{}; // voxel spacing.xyz / source+trilinear error.w
    std::array<glm::ivec4,4> clipCounts{}; // logical counts.xyz / packed linear voxel offset.w
    std::array<glm::ivec4,4> clipScroll{}; // toroidal physical scroll.xyz / ready flag.w
    glm::ivec4 clipmap{0,-1,1,1}; // count(0=legacy), selected debug level, level colors, bounds
    std::array<glm::ivec4,4> clipRanges{}; // brick counts.xyz / min-max atlas offset.w
    glm::ivec4 rangeAtlas{1024,4,0,0}; // width, brick cell size, available, reserved
    // CPU-projected clipmap box edges: NDC xy/zw endpoints, 12 per level.
    // Invalid/fully clipped edges have coordinates >= 1e10.
    std::array<glm::vec4,48> boundLines{};
};
static_assert(offsetof(ProbeSdfTraceConstants,clipOrigins)==144);
static_assert(sizeof(ProbeSdfTraceConstants)==1264);
inline constexpr std::uint32_t ProbeSdfTracingBinding=10;
}
