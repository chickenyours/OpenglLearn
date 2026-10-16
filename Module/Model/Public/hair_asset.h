#pragma once
#include "Model/Public/skeletal_asset.h"
#include <memory>
#include <span>

namespace Model {
inline constexpr std::uint32_t HairAssetVersion=1;
struct HairBoneLink {
    std::uint32_t bone=0;
    glm::dvec3 restTip{0}; // bone-local source units, including virtual terminal tips
    double radius=.002; // meters, centerline broad-phase only
    double stiffness=12, damping=4; // per-second response, not raw PMX coefficients
    double maxAngle=.9; // runtime swing cone in radians; not the PMX joint's per-axis Euler limits
    std::int32_t sourceRigidBody=-1, sourceJoint=-1;
};
struct HairChain {
    std::string name;
    std::int32_t anchorBone=-1;
    std::vector<HairBoneLink> links; // proximal to distal, all required ancestors retained
};
struct HairProbeInfluence {
    std::uint32_t bone=0;
    double weight=0;
    glm::dvec3 weightedPosition{0}; // boneFromMesh * vertex, premultiplied by weight
};
struct HairSurfaceProbe {
    std::uint32_t mesh=0,vertex=0,chain=0;
    bool triangleSample=false;
    std::array<std::uint32_t,3> triangle{0,0,0};
    glm::dvec3 barycentric{1,0,0};
    std::vector<HairProbeInfluence> influences;
};
struct HairSolverSettings {
    double fixedTimeStep=1.0/60.0;
    std::uint32_t substeps=2,iterations=6;
    double thickness=.0015; // meters, actual rendered surface contact margin
    double friction=.15,inertia=.85;
    glm::dvec3 gravity{0,-9.81,0},wind{0};
    bool collision=true;
};
struct HairAttachmentVertex {
    std::uint32_t mesh=0,vertex=0,chain=0;
};
struct HairAsset {
    std::uint32_t version=HairAssetVersion;
    std::string id,name,skeletonId,bindingHash;
    double unitsPerMeter=1;
    HairSolverSettings settings;
    std::vector<HairChain> chains;
    std::vector<std::uint32_t> hairMeshes; // includes rigid scalp hair; excluded from body collision
    std::vector<HairSurfaceProbe> probes; // representative actual skin-weighted rendered points
    // Authored scalp-embedded, root-dominant region, classified in bind pose;
    // descendant motion capacity is independently bounded below 0.5 mm.
    // These hidden attachment points never become free collision probes.
    std::vector<HairAttachmentVertex> attachments;
    std::vector<std::string> diagnostics;
    // Original PMX record order is retained so selected source indices remain
    // valid; the independent hair asset is usable without the source PMX.
    std::vector<SourceRigidBody> sourceRigidBodies;
    std::vector<SourcePhysicsJoint> sourceJoints;
};
struct HairBuildOptions {
    std::string name="Hair";
    std::size_t maxProbes=512;
};
std::optional<HairAsset> BuildHairAsset(const SkeletalAssetPackage& source,
    const SkeletalAssetPackage& target,const HairBuildOptions& options={},std::string* error=nullptr);
std::string HairBindingHash(const SkeletalAssetPackage& target);
bool ValidateHairAsset(const HairAsset& asset,const SkeletalAssetPackage& target,std::string* error=nullptr);
bool SaveHairAsset(const HairAsset& asset,const SkeletalAssetPackage& target,
    const std::filesystem::path& path,std::string* error=nullptr);
std::optional<HairAsset> LoadHairAsset(const std::filesystem::path& path,
    const SkeletalAssetPackage& target,std::string* error=nullptr);
struct HairSimulationStats {
    std::uint64_t steps=0,resets=0,sleepTicks=0; // steps is logical fixed clock; active solves = steps - sleepTicks
    bool sleeping=false;
    std::size_t chains=0,bones=0,probes=0,contacts=0,bodyTriangles=0;
    std::size_t collisionQueries=0,clearanceSkips=0; // this Step, exact queries / certified no-contact reuse
    std::size_t measurementReuses=0; // exact point, current body geometry and contact settings; retains inside residuals
    std::uint32_t worstPenetrationProbe=UINT32_MAX;
    std::size_t adaptiveQueries=0,adaptiveRounds=0;
    bool adaptiveBudgetExhausted=false;
    std::size_t surfaceVertices=0,surfaceClusters=0,surfaceContacts=0,surfaceSweeps=0,surfaceBroadphaseRejects=0;
    double surfaceMilliseconds=0,maxSurfacePenetration=0,maxMeshCorrection=0;
    double simulationMilliseconds=0,collisionMilliseconds=0;
    double maxPenetration=0,maxTipDisplacement=0,maxRootError=0; // meters
    double sourcePenetration=0,maxSurfaceCorrection=0; // meters; source overlap is reported, never subtracted
    double maxAttachmentDrift=0; // meters relative to the current animated scalp attachment
};
struct HairSurfaceOffset {
    std::uint32_t mesh=0,vertex=0,anchorBone=0;
    glm::dvec3 displacement{0}; // anchor-local SOURCE units, applied after ordinary skeletal skinning
    bool operator==(const HairSurfaceOffset&) const=default;
};
bool ApplyHairSurfaceOffsets(const SkeletalAssetPackage& target,const SkeletalPose& pose,
    std::span<const HairSurfaceOffset> offsets,StaticModel& skinned,std::string* error=nullptr);
class HairSimulator {
public:
    HairSimulator();~HairSimulator();
    HairSimulator(HairSimulator&&) noexcept;
    HairSimulator& operator=(HairSimulator&&) noexcept;
    HairSimulator(const HairSimulator&)=delete;
    HairSimulator& operator=(const HairSimulator&)=delete;
    bool Reset(const HairAsset& asset,const SkeletalAssetPackage& target,
        const SkeletalPose& animatedPose,std::string* error=nullptr);
    bool Step(const HairAsset& asset,const SkeletalAssetPackage& target,
        const SkeletalPose& animatedPose,double deltaSeconds,const HairSolverSettings& settings,
        SkeletalPose& outputPose,std::string* error=nullptr);
    const HairSimulationStats& Stats() const;
    const std::vector<glm::dvec3>& Positions() const; // source-space chain nodes for debugging
    const std::vector<HairSurfaceOffset>& SurfaceOffsets() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace Model
