#pragma once

#include "Model/Public/static_model.h"
#include "Model/Public/mesh_regions.h"
#include <glm/gtc/quaternion.hpp>
#include <map>
#include <optional>

namespace Model {

inline constexpr std::uint32_t SkeletalAssetVersion = 1;
struct BoneTransform {
    glm::dvec3 translation{0};
    glm::dquat rotation{1,0,0,0};
    glm::dvec3 scale{1};
};
struct SkeletonBone {
    std::string name, englishName;
    std::int32_t parent = -1;
    BoneTransform restLocal;
    glm::dmat4 inverseBind{1}; // skeleton-space inverse global rest transform
    std::uint32_t sourceFlags = 0;
    glm::dvec3 fixedAxis{0}, localAxisX{1,0,0}, localAxisZ{0,0,1};
    std::int32_t inheritParent = -1;
    float inheritWeight = 0;
};
struct SkeletonAsset {
    std::string id, name;
    std::vector<SkeletonBone> bones;
};
enum class SkinningMode : std::uint8_t { Bdef1, Bdef2, Bdef4, Sdef, Qdef };
struct VertexSkin {
    std::array<std::uint32_t,4> joints{0,0,0,0};
    glm::vec4 weights{1,0,0,0};
    SkinningMode mode = SkinningMode::Bdef4;
    glm::vec3 sdefC{0}, sdefR0{0}, sdefR1{0};
};
struct SkinInfluence { std::uint32_t joint=0; float weight=0; };
struct MeshSkin {
    std::vector<VertexSkin> vertices; // one per geometry vertex
    std::vector<std::uint32_t> sourceVertexIds;
    glm::dmat4 bindTransform{1}; // imported mesh frame; glTF skins use identity per spec
    // Optional mesh-specific offset matrices. Empty uses SkeletonBone.inverseBind.
    // Local skin = inverse(bindTransform) * animatedGlobalBone * boneFromMesh.
    std::vector<glm::dmat4> boneFromMesh;
    // Optional exact variable-length skinning. Fixed four-wide vertices remain
    // compatibility summaries; legacy/PMX packages leave these arrays empty.
    std::vector<std::uint32_t> influenceOffsets;
    std::vector<SkinInfluence> influences;
};
template<class Visitor>
bool ForEachSkinInfluence(const MeshSkin& skin,std::size_t vertex,Visitor&& visitor) {
    if(vertex>=skin.vertices.size())return false;
    if(skin.influenceOffsets.empty()&&skin.influences.empty()) {
        for(std::size_t i=0;i<4;++i)visitor(skin.vertices[vertex].joints[i],skin.vertices[vertex].weights[i]);
        return true;
    }
    if(skin.influenceOffsets.size()!=skin.vertices.size()+1)return false;
    const auto begin=skin.influenceOffsets[vertex],end=skin.influenceOffsets[vertex+1];
    if(begin>=end||end>skin.influences.size())return false;
    for(auto i=begin;i<end;++i)visitor(skin.influences[i].joint,skin.influences[i].weight);
    return true;
}
struct MorphVertexDelta {
    std::uint32_t mesh = 0, vertex = 0;
    glm::dvec3 position{0};
    glm::vec3 normal{0};
};
struct MorphTarget {
    std::string name, englishName;
    std::uint8_t panel = 4, sourceType = 1;
    std::vector<MorphVertexDelta> deltas;
};

struct VectorKey { double time = 0; glm::dvec3 value{0}; };
struct RotationKey { double time = 0; glm::dquat value{1,0,0,0}; };
struct ScalarKey { double time = 0; double value = 0; };
struct BoneAnimationTrack {
    std::uint32_t bone = 0;
    std::vector<VectorKey> translations, scales;
    std::vector<RotationKey> rotations;
};
struct MorphAnimationTrack { std::uint32_t morph = 0; std::vector<ScalarKey> keys; };
struct AnimationSequenceAsset {
    std::string id, name, skeletonId, source;
    double duration = 0, framesPerSecond = 30;
    bool generated = false;
    std::vector<BoneAnimationTrack> boneTracks;
    std::vector<MorphAnimationTrack> morphTracks;
    bool baked = false;
    std::string sourceSkeletonId;
    std::vector<std::string> diagnostics;
};

struct HumanoidBoneMapping {
    std::string role;
    std::int32_t bone = -1;
    float confidence = 0;
    std::string method;
};
struct IkLink {
    std::uint32_t bone = 0;
    bool limited = false;
    glm::dvec3 minimum{0}, maximum{0}; // engine-space Euler radians
};
struct IkChain {
    std::string name, role;
    std::int32_t root = -1, effector = -1, targetBone = -1;
    glm::dvec3 target{0}; // skeleton-space rest target if targetBone is absent
    std::uint32_t iterations = 32;
    double angleLimit = 0.25;
    bool native = false, enabled = true;
    std::vector<IkLink> links; // distal to proximal for CCD
};
struct IkRigAsset {
    std::string id, name, skeletonId;
    std::vector<HumanoidBoneMapping> humanoid;
    std::vector<IkChain> chains;
    std::vector<std::string> diagnostics;
};
// PMX physical metadata in engine right-handed Y-up coordinates. Positions and
// dimensions retain source units; Euler angles are radians. Reflection of Z
// maps polar vectors (x,y,-z) and axial rotations (-x,-y,z). Spring coefficients,
// mass (kg), damping, restitution and friction retain their source values.
struct SourceRigidBody {
    std::string name, englishName;
    std::int32_t bone=-1;
    std::uint8_t collisionGroup=0,shape=0,mode=0;
    std::uint16_t collisionMask=0;
    glm::dvec3 dimensions{0},position{0},rotationEuler{0};
    double mass=0,linearDamping=0,angularDamping=0,restitution=0,friction=0;
};
struct SourcePhysicsJoint {
    std::string name,englishName;
    std::uint8_t type=0;
    std::int32_t rigidBodyA=-1,rigidBodyB=-1;
    glm::dvec3 position{0},rotationEuler{0};
    glm::dvec3 translationMinimum{0},translationMaximum{0};
    glm::dvec3 rotationMinimum{0},rotationMaximum{0};
    glm::dvec3 translationSpring{0},rotationSpring{0};
};
struct SkeletalAssetPackage {
    std::uint32_t version = SkeletalAssetVersion;
    std::string id, name, sourceFormat;
    std::filesystem::path sourcePath;
    std::shared_ptr<StaticModel> geometry;
    SkeletonAsset skeleton;
    std::vector<MeshSkin> skins;
    std::vector<MorphTarget> morphs;
    std::vector<AnimationSequenceAsset> animations;
    IkRigAsset ikRig;
    std::uint32_t sourceRigidBodies = 0, sourceJoints = 0;
    std::vector<SourceRigidBody> rigidBodies; // optional for legacy count-only packages
    std::vector<SourcePhysicsJoint> physicsJoints;
    std::vector<MeshRegionInfo> meshRegions; // optional, persistent section annotations
    std::vector<std::string> warnings;
};

std::shared_ptr<SkeletalAssetPackage> ImportSkeletalSource(const std::filesystem::path& source,
    const LoadOptions& options = {}, std::string* error = nullptr);
bool ValidateSkeletalAsset(const SkeletalAssetPackage& asset, std::string* error = nullptr);
bool SaveSkeletalAssetPackage(const SkeletalAssetPackage& asset, const std::filesystem::path& directory,
    std::string* error = nullptr);
std::shared_ptr<SkeletalAssetPackage> LoadSkeletalAssetPackage(const std::filesystem::path& manifestOrDirectory,
    std::string* error = nullptr);
// Generate a constant local-space fitting sequence from the captured SourcePose.
// A versioned JSON profile must name this exact source asset and its corrections.
// Geometry, complete skin weights, rest transforms and SourcePose stay unchanged;
// optional guarded vertex deltas become a separate corrective MorphTarget.
bool ApplySkeletalFitProfile(SkeletalAssetPackage& asset, const std::filesystem::path& profilePath,
    std::string* error = nullptr);

// CPU runtime and recognition. Skeleton order need not be topological.
glm::dmat4 TransformMatrix(const BoneTransform& transform);
std::vector<glm::dmat4> GlobalBoneTransforms(const SkeletonAsset& skeleton,
    const std::vector<BoneTransform>& localPose);
std::vector<BoneTransform> RestPose(const SkeletonAsset& skeleton);
struct SkeletalPose {
    std::vector<BoneTransform> local;
    std::vector<glm::dmat4> global;
    std::vector<float> morphWeights;
};
SkeletalPose EvaluateSkeletalPose(const SkeletalAssetPackage& asset,
    const AnimationSequenceAsset* sequence, double time, bool loop = true);
// Output reuses geometry vectors; textures/materials need not be copied.
bool SkinSkeletalMesh(const SkeletalAssetPackage& asset, const SkeletalPose& pose,
    StaticModel& output, std::string* error = nullptr);
bool SolveIk(const SkeletonAsset& skeleton, const IkChain& chain, const glm::dvec3& target,
    SkeletalPose& pose, std::string* error = nullptr);
IkRigAsset RecognizeHumanoidRig(const SkeletonAsset& skeleton);
void AddGeneratedPreviewSequences(SkeletalAssetPackage& asset);

struct RetargetBoneMapping {
    std::string role;
    std::int32_t sourceBone = -1, targetBone = -1;
    float confidence = 0;
    glm::dquat basisCorrection{1,0,0,0};
    std::string method;
};
struct RetargetMap {
    std::string sourceSkeletonId, targetSkeletonId;
    double translationScale = 1;
    std::vector<RetargetBoneMapping> bones;
    std::vector<std::string> diagnostics;
};
RetargetMap BuildRetargetMap(const SkeletonAsset& source, const IkRigAsset& sourceRig,
    const SkeletonAsset& target, const IkRigAsset& targetRig);
SkeletalPose RetargetPose(const SkeletonAsset& source, const SkeletalPose& sourcePose,
    const SkeletonAsset& target, const RetargetMap& mapping);

} // namespace Model
