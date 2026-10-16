#pragma once
#include "Model/Public/hair_asset.h"
#include "hair_body_collision.h"

namespace Model::detail {
// A post-skin contact layer. The caller owns/updates the body's shared BVH;
// this object never modifies a skeleton, an animation, or source geometry.
class HairSurfaceContact {
public:
    HairSurfaceContact();~HairSurfaceContact();
    HairSurfaceContact(HairSurfaceContact&&) noexcept;
    HairSurfaceContact& operator=(HairSurfaceContact&&) noexcept;
    HairSurfaceContact(const HairSurfaceContact&)=delete;
    HairSurfaceContact& operator=(const HairSurfaceContact&)=delete;
    bool Initialize(const HairAsset& asset,const SkeletalAssetPackage& target,std::string* error=nullptr);
    bool Reset(const HairAsset& asset,const SkeletalAssetPackage& target,const SkeletalPose& solvedPose,
        const HairBodyCollision& body,HairSimulationStats& stats,std::string* error=nullptr);
    bool Step(const HairAsset& asset,const SkeletalAssetPackage& target,const SkeletalPose& solvedPose,
        const HairBodyCollision& body,double deltaSeconds,const HairSolverSettings& settings,
        HairSimulationStats& stats,std::string* error=nullptr);
    const std::vector<HairSurfaceOffset>& Offsets() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
