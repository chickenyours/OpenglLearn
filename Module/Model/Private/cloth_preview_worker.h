#pragma once

#include "Model/Public/cloth_asset.h"
#include <chrono>
#include <memory>

namespace Model::detail {

struct ClothPreviewSnapshot {
    // revision changes for every partial/final publication. serial advances only
    // when an entire submitted interval has completed (or failed explicitly).
    std::uint64_t generation=0,serial=0,revision=0;
    std::vector<glm::dvec3> positions;
    ClothSimulationStats stats;
    bool hairEnabled=false;
    bool clothEnabled=true;
    HairSimulationStats hairStats;
    std::vector<glm::dvec3> hairPositions;
    // Post-skin mesh corrections belong to this exact completed pose/revision.
    std::vector<HairSurfaceOffset> hairSurfaceOffsets;
    // CPU geometry only: meshes, instances and bounds. Material/texture storage
    // remains owned by the renderer's original garment resource.
    StaticModel garment;
    SkeletalPose pose;
    // submittedSeconds includes consumed input time shorter than one fixed tick.
    // pose/garment/positions always describe the last completed physical tick;
    // the unspent fraction is retained for subsequent submissions.
    double submittedSeconds=0,simulatedSeconds=0;
    std::uint64_t cumulativeIterations=0,totalFixedSteps=0;
    std::string error;
    double solveMilliseconds=0;
};

// One worker owns the simulator. Submissions replace the pending pose while
// accumulating every submitted second; reset invalidates previous generations.
// Render-rate fractions are accumulated outside the solver. A zero-tick input
// completes its serial while retaining the whole last physical snapshot; even
// zero-time/frozen pose changes cannot pair newer bones with older cloth pins.
// No renderer/UI/GL call is made on this thread.
class ClothPreviewWorker final {
public:
    ClothPreviewWorker();
    ~ClothPreviewWorker();
    ClothPreviewWorker(const ClothPreviewWorker&)=delete;
    ClothPreviewWorker& operator=(const ClothPreviewWorker&)=delete;

    // simulateCloth=false retains animated garment geometry and advances only
    // hair on this same worker; changing mode starts a fresh generation.
    // Coupled mode uses the cloth clock for hair too, preserving its internal
    // substep rate so no intermediate body pose reuses stale mesh contact.
    // Keep garmentSource immutable for this generation. It enables exact CPU
    // skinning at intermediate quaternion poses; absent a source, vertex input
    // follows linear trajectories between submitted geometry endpoints.
    std::uint64_t Submit(const ClothAsset& cloth,const SkeletalPose& pose,
        const StaticModel& animatedGarment,double delta,const ClothSolverSettings& settings,bool reset=false,
        std::shared_ptr<const SkeletalAssetPackage> garmentSource={},
        std::shared_ptr<const HairAsset> hairAsset={},std::shared_ptr<const SkeletalAssetPackage> bodySource={},
        const HairSolverSettings& hairSettings={},bool simulateCloth=true);
    // Nonblocking focus suspension: discard pending/fractional input time and
    // stop after at most the current fixed tick. Its complete pose may
    // still publish. Submissions while paused are ignored. The first resumed
    // submission rebases input time without resetting cloth or spending delta.
    void SetPaused(bool paused);
    std::shared_ptr<const ClothPreviewSnapshot> Latest() const;
    std::uint64_t Generation() const;
    std::uint64_t SubmissionSerial() const;
    // Testing/shutdown coordination only; the interactive render loop never waits.
    // Returns false on timeout or when another generation supersedes the request.
    bool Wait(std::uint64_t generation,std::uint64_t submissionSerial,std::chrono::milliseconds timeout) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Model::detail
