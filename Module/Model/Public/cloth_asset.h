#pragma once
#include "Model/Public/skeletal_asset.h"
#include "Model/Public/hair_asset.h"
#include <memory>

namespace Model {
inline constexpr std::uint32_t ClothAssetVersion=3;
struct ClothParticle {
    glm::dvec3 restPosition{0}; // character model space, source units
    double inverseMass=1; // 0 = animated kinematic anchor
    double maxDistance=0; // source units from the animated skinned position
    double animationDrive=0; // 0..1, stronger near the attached waist
};
struct ClothDistanceConstraint { std::uint32_t a=0,b=0; double restLength=0; };
struct ClothTether { std::uint32_t particle=0,anchor=0; double restLength=0; };
struct ClothRenderBinding { std::uint32_t mesh=0,vertex=0,particle=0; };
struct ClothCapsule {
    std::string name;
    std::int32_t boneA=-1,boneB=-1;
    glm::dvec3 localA{0},localB{0};
    double radius=0; // source units
    bool referenceValid=false;
    glm::dvec3 restA{0},restB{0}; // neutral character-model endpoints for startup sweep
};
struct ClothConvexPlane {
    glm::dvec3 normal{0,1,0};
    double distance=0; // outward unit normal; normal dot bone-local point <= distance
    bool collision=true; // artificial partition caps still clip membership but cannot push cloth
};
struct ClothConvexInfluence {
    std::int32_t bone=-1;
    glm::dvec3 weightedPosition{0}; // bone-local source position already multiplied by weight
    double weight=0;
};
struct ClothConvexCollider {
    std::string name;
    std::int32_t bone=-1;
    std::vector<glm::dvec3> vertices; // complete source support cloud, in the named bone's local space
    std::vector<std::array<std::uint32_t,3>> triangles; // closed, outward winding
    std::vector<ClothConvexPlane> planes; // one supporting plane per triangle, in the same order
    std::vector<std::vector<ClothConvexInfluence>> skin; // empty: rigid; otherwise one normalized influence set per vertex
    glm::dmat4 referenceTransform{1}; // neutral bone-local to character-model transform
    bool referenceValid=false;
};
struct ClothSolverSettings {
    double fixedTimeStep=1.0/60.0;
    std::uint32_t substeps=3,iterations=16;
    double convergenceTolerance=.00025; // meters; 0 always uses the iteration limit
    double stretchCompliance=1e-7,bendCompliance=100,animationCompliance=3e-3;
    double areaCompliance=1e-7;
    double damping=.65,localDamping=1.0,animationDamping=.15; // 1/seconds
    double friction=.3,thickness=.008; // coefficient, meters
    glm::dvec3 gravity{0,-9.81,0},wind{0}; // SI acceleration, meters/s velocity
    bool collision=true,selfCollision=false;
};
struct ClothAsset {
    std::uint32_t version=ClothAssetVersion;
    std::string id,name,skeletonId,garmentId,topologyHash;
    double unitsPerMeter=1;
    // Prefer the actual attachment-ring fit for dynamic rest goals. This bone
    // is the fallback when no fit is available; skirt/leg skinning is excluded.
    // -1 retains legacy skinned goals. Pins always use authored skinning.
    std::int32_t motionBone=-1;
    glm::dmat4 motionInverseBind{1};
    ClothSolverSettings settings;
    std::vector<ClothParticle> particles;
    std::vector<std::array<std::uint32_t,3>> triangles;
    std::vector<ClothDistanceConstraint> stretch,bend;
    std::vector<ClothTether> tethers;
    std::vector<ClothRenderBinding> renderBindings;
    std::vector<ClothCapsule> capsules;
    std::vector<ClothConvexCollider> convexes;
    std::vector<std::string> diagnostics;
};
struct GarmentSlotReference {
    std::string slot="skirt",garmentId;
    std::filesystem::path package,cloth;
    bool enabled=true;
};
struct ModularCharacterAsset {
    std::string id,name,skeletonId;
    std::filesystem::path bodyPackage;
    std::filesystem::path hairPhysics; // optional independent secondary-motion asset
    std::vector<GarmentSlotReference> slots;
};
struct ModularCharacterBundle {
    ModularCharacterAsset assembly;
    std::shared_ptr<SkeletalAssetPackage> body,garment;
    ClothAsset cloth;
    std::optional<HairAsset> hair;
    std::vector<std::string> diagnostics;
};
struct GarmentBuildOptions {
    std::string name="Skirt",slot="skirt";
    double skirtWeightThreshold=.02,waistPinFraction=.14;
    double weldTolerance=1e-5; // source units; weld render seams for simulation
    std::uint32_t refinementLevels=1; // 0..2 linear midpoint subdivisions, garment only
    double surfaceDensity=.25; // kg per square meter, invariant under refinement
    std::vector<std::uint32_t> materialIndices; // optional explicit region restriction
};
std::optional<ModularCharacterBundle> BuildSkirtGarment(const SkeletalAssetPackage& source,
    const GarmentBuildOptions& options={},std::string* error=nullptr);
std::string ClothTopologyHash(const SkeletalAssetPackage& garment);
bool ValidateClothAsset(const ClothAsset& cloth,const SkeletalAssetPackage& garment,std::string* error=nullptr);
bool SaveClothAsset(const ClothAsset& cloth,const SkeletalAssetPackage& garment,
    const std::filesystem::path& path,std::string* error=nullptr);
std::optional<ClothAsset> LoadClothAsset(const std::filesystem::path& path,
    const SkeletalAssetPackage& garment,std::string* error=nullptr);
bool SaveModularCharacter(const ModularCharacterBundle& bundle,const std::filesystem::path& directory,std::string* error=nullptr);
std::optional<ModularCharacterBundle> LoadModularCharacter(const std::filesystem::path& manifest,std::string* error=nullptr);
// Replacements share the exact rest skeleton and morph layout. Different rigs
// must be retargeted offline, rather than silently pairing bone indices.
bool GarmentCompatible(const SkeletalAssetPackage& body,const SkeletalAssetPackage& garment,std::string* error=nullptr);

struct ClothWorldCapsule { glm::dvec3 a{0},b{0}; double radius=0; };
std::vector<ClothWorldCapsule> EvaluateClothCapsules(const ClothAsset& cloth,const SkeletalPose& pose);
struct ClothWorldConvex {
    std::vector<glm::dvec3> vertices;
    std::vector<std::array<std::uint32_t,3>> triangles;
    std::vector<ClothConvexPlane> planes;
    glm::dmat4 transform{1};
    glm::dvec3 minimum{0},maximum{0};
};
std::vector<ClothWorldConvex> EvaluateClothConvexes(const ClothAsset& cloth,const SkeletalPose& pose);
struct ClothSimulationStats {
    std::uint64_t steps=0,resets=0;
    std::uint64_t constraintIterations=0; // iterations executed by the last Step, including warmup
    std::size_t particles=0,pinned=0,contacts=0,selfContacts=0;
    // Potential conflicts in the animated targets, including when collision is disabled.
    std::size_t infeasibleAnimatedTargets=0,kinematicColliderOverlaps=0;
    // Displacement and penetration use source units; stretch error is relative.
    double maxDisplacement=0,maxStretchError=0,maxPenetration=0,simulationMilliseconds=0;
    // Absolute dynamic edge errors use source units; fixed-edge strain is relative.
    double maxDynamicEdgeError=0,dynamicEdgeError95=0,maxKinematicStretchError=0;
    double maxSurfacePenetration=0,maxBendDeviation=0; // source units, radians
    // Raw surface/pin conflicts remain visible; only the resolvable metric
    // allows the fixed seam's authored body depth in its adjacent triangles.
    double maxResolvableSurfacePenetration=0,maxKinematicColliderPenetration=0; // source units, includes full collision margin
    double maxAttachmentError=0; // source units: fixed particles vs current skinned seam
    std::size_t collapsedTriangles=0;
};
class ClothSimulator {
public:
    ClothSimulator();~ClothSimulator();
    ClothSimulator(ClothSimulator&&) noexcept;
    ClothSimulator& operator=(ClothSimulator&&) noexcept;
    ClothSimulator(const ClothSimulator&)=delete;
    ClothSimulator& operator=(const ClothSimulator&)=delete;
    bool Reset(const ClothAsset& cloth,const StaticModel& animatedGarment,std::string* error=nullptr);
    bool Reset(const ClothAsset& cloth,const SkeletalPose& pose,const StaticModel& animatedGarment,std::string* error=nullptr);
    bool Step(const ClothAsset& cloth,const SkeletalPose& pose,const StaticModel& animatedGarment,
        double deltaSeconds,const ClothSolverSettings& settings,std::string* error=nullptr);
    bool Apply(const ClothAsset& cloth,StaticModel& garment,std::string* error=nullptr) const;
    const std::vector<glm::dvec3>& Positions() const;
    const ClothSimulationStats& Stats() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace Model
