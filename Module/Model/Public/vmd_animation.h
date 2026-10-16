#pragma once
#include "Model/Public/skeletal_asset.h"

namespace Model {

// VMD motion has names and local deltas, but no source rest skeleton or units.
// Positions/quaternions below are converted from MMD to right-handed Y-up.
// Frames use the VMD time base of 30 fps; Bezier controls belong to the arrival key.
struct VmdBezier { double x1=0, y1=0, x2=1, y2=1; };
struct VmdBoneKey {
    std::uint32_t frame=0;
    glm::dvec3 translation{0};
    glm::dquat rotation{1,0,0,0};
    std::array<VmdBezier,4> interpolation{}; // X, Y, Z, quaternion interpolation
};
struct VmdBoneTrack { std::string name; std::vector<VmdBoneKey> keys; };
struct VmdMorphKey { std::uint32_t frame=0; double weight=0; };
struct VmdMorphTrack { std::string name; std::vector<VmdMorphKey> keys; };
struct VmdIkToggle { std::string name; bool enabled=true; };
struct VmdIkKey { std::uint32_t frame=0; bool visible=true; std::vector<VmdIkToggle> states; };
struct VmdMotion {
    std::string id, name, modelName;
    std::vector<std::filesystem::path> sourcePaths;
    std::vector<VmdBoneTrack> bones;
    std::vector<VmdMorphTrack> morphs;
    std::vector<VmdIkKey> ikKeys;
    double duration=0;
    std::uint32_t cameraKeys=0, lightKeys=0, shadowKeys=0;
    std::vector<std::string> warnings;
};
std::optional<VmdMotion> LoadVmdMotion(const std::filesystem::path& path, std::string* error=nullptr);
std::optional<VmdMotion> MergeVmdMotions(const std::vector<VmdMotion>& motions,
    const std::string& name, std::string* error=nullptr);
BoneTransform SampleVmdBone(const VmdBoneTrack& track, double seconds);
double SampleVmdMorph(const VmdMorphTrack& track, double seconds);

struct AnimationTrackMatch {
    std::string sourceName, targetName, method;
    std::int32_t target=-1;
    float confidence=0;
    bool morph=false;
};
struct AnimationBakeOptions {
    double framesPerSecond=30;
    double translationScale=1;
    bool solveNativeIk=true;
    bool evaluateAppend=true;
};
struct AnimationBakeReport {
    std::vector<AnimationTrackMatch> matches;
    std::vector<std::string> warnings;
    std::size_t samples=0, matchedBones=0, matchedMorphs=0, unmatchedBones=0, unmatchedMorphs=0;
};
std::optional<AnimationSequenceAsset> BakeVmdAnimation(const VmdMotion& motion,
    const SkeletalAssetPackage& target, const AnimationBakeOptions& options,
    const std::string& name, AnimationBakeReport* report=nullptr, std::string* error=nullptr);
std::optional<AnimationSequenceAsset> BakeRetargetedAnimation(const SkeletalAssetPackage& source,
    const AnimationSequenceAsset& sequence, const SkeletalAssetPackage& target,
    const RetargetMap& mapping, const AnimationBakeOptions& options,
    const std::string& name, AnimationBakeReport* report=nullptr, std::string* error=nullptr);

// Standalone .anim.json files use the package AnimationSequence schema and bind
// to the supplied target skeleton/morph layout; source VMD is not needed to play.
bool SaveAnimationSequenceAsset(const AnimationSequenceAsset& sequence,
    const SkeletalAssetPackage& target, const std::filesystem::path& path, std::string* error=nullptr);
std::optional<AnimationSequenceAsset> LoadAnimationSequenceAsset(const std::filesystem::path& path,
    const SkeletalAssetPackage& target, std::string* error=nullptr);

} // namespace Model
