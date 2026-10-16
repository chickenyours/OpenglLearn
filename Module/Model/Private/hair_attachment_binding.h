#pragma once
#include "Model/Public/hair_asset.h"
#include <algorithm>
#include <cmath>

namespace Model::detail {
// Import/load-only geometry classification. Never called from a solver tick.
void GenerateHairAttachments(HairAsset& asset,const SkeletalAssetPackage& target,
    const std::vector<glm::dmat4>& restGlobals);
struct HairAttachmentWeights {double root=0,anchor=0,distalWeight=0,rootLever=0,distalCapacity=0;};
inline bool AuthoredHairBlend(const SkeletalAssetPackage& target,const HairChain& chain,
    std::uint32_t mesh,std::uint32_t vertex,double units,HairAttachmentWeights& result){
    result={};if(chain.links.empty()||chain.anchorBone<0||mesh>=target.skins.size()||vertex>=target.skins[mesh].vertices.size())return false;
    if(!std::isfinite(units)||units<=0)return false;const auto& meshSkin=target.skins[mesh];const auto& skin=meshSkin.vertices[vertex];double total=0;for(int k=0;k<4;++k)if(skin.weights[k]>0)total+=skin.weights[k];if(!(total>0))return false;
    std::vector<double> scale(chain.links.size()),prefix(chain.links.size(),1);for(std::size_t j=0;j<chain.links.size();++j){const auto& s=target.skeleton.bones[chain.links[j].bone].restLocal.scale;scale[j]=std::max({std::abs(s.x),std::abs(s.y),std::abs(s.z)});if(j)prefix[j]=prefix[j-1]*scale[j-1];}
    const auto point=target.geometry->meshes[mesh].vertices[vertex].position;
    for(int k=0;k<4;++k)if(skin.weights[k]>0){const auto b=skin.joints[k];const double weight=skin.weights[k]/total;if(b==static_cast<std::uint32_t>(chain.anchorBone)){result.anchor+=weight;continue;}
        const auto found=std::find_if(chain.links.begin(),chain.links.end(),[&](const auto& l){return l.bone==b;});if(found==chain.links.end())return false;const auto i=static_cast<std::size_t>(found-chain.links.begin());if(i==0)result.root+=weight;else result.distalWeight+=weight;
        const auto offset=b<meshSkin.boneFromMesh.size()?meshSkin.boneFromMesh[b]:target.skeleton.bones[b].inverseBind*meshSkin.bindTransform;const double radial=glm::length(glm::dvec3(offset*glm::dvec4(point,1)));
        // Telescope local joint rotations from proximal to distal. A later
        // joint can fold the strand, so use articulated path length rather
        // than the shorter rest-space pivot-to-vertex chord. Scale operator
        // norms make the same bound conservative for authored local scales.
        for(std::size_t j=0;j<=i;++j){double radius=scale[i]*radial;for(std::size_t t=i;t>j;--t)radius=scale[t-1]*(glm::length(target.skeleton.bones[chain.links[t].bone].restLocal.translation)+radius);const double lever=weight*prefix[j]*radius/units;if(j==0)result.rootLever+=lever;else result.distalCapacity+=lever*2*std::sin(std::min(chain.links[j].maxAngle,3.14159265358979323846)*.5);}
    }
    return result.root>1e-6&&result.root>=result.distalWeight&&std::isfinite(result.rootLever)&&std::isfinite(result.distalCapacity)&&result.distalCapacity<=.0005;
}
inline bool HairRigidAnchorVertex(const SkeletalAssetPackage& target,const HairChain& chain,
    std::uint32_t mesh,std::uint32_t vertex){
    if(chain.anchorBone<0||mesh>=target.skins.size()||vertex>=target.skins[mesh].vertices.size())return false;
    bool weighted=false;const auto& skin=target.skins[mesh].vertices[vertex];for(int k=0;k<4;++k)if(skin.weights[k]>0){weighted=true;if(skin.joints[k]!=static_cast<std::uint32_t>(chain.anchorBone))return false;}return weighted;
}
inline glm::dvec3 HairRestVertex(const SkeletalAssetPackage& target,const std::vector<glm::dmat4>& globals,
    std::uint32_t mesh,std::uint32_t vertex){
    const auto& skin=target.skins[mesh];const auto& weights=skin.vertices[vertex];const auto point=target.geometry->meshes[mesh].vertices[vertex].position;glm::dvec3 result(0);double total=0;
    for(int k=0;k<4;++k)if(weights.weights[k]>0){const auto b=weights.joints[k];const auto offset=b<skin.boneFromMesh.size()?skin.boneFromMesh[b]:target.skeleton.bones[b].inverseBind*skin.bindTransform;result+=glm::dvec3(globals[b]*offset*glm::dvec4(point,1))*double(weights.weights[k]);total+=weights.weights[k];}return total>0?result/total:point;
}
inline double HairAttachmentAngleBound(double maximumLeverMeters,double distalCapacity=0){return maximumLeverMeters>0?2*std::asin(std::clamp((.0015-distalCapacity)/(2*maximumLeverMeters),0.,1.)):3.14159265358979323846;}
}
