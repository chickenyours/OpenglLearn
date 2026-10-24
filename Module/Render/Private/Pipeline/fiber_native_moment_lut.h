#pragma once
#include "Render/Public/Pipeline/strand_rendering.h"
#include <array>
#include <cmath>

namespace Render::PipelineDetail {
inline constexpr unsigned FiberNativeMomentThetaCount=64;
inline constexpr unsigned FiberNativeMomentRoughnessCount=64;
inline constexpr unsigned FiberNativeMomentAbsorptionCount=16;
inline constexpr unsigned FiberNativeMomentAtlasWidth=64;
inline constexpr unsigned FiberNativeMomentAtlasHeight=1024;
inline constexpr unsigned FiberNativeMomentTexelCount=65536;
// The source generator reads IOR and f0 independently. The recovered CPU writer
// at 0x14035304b..0x140353079 computes f0 = ((IOR - 1)/(IOR + 1)) squared.
using FiberNativeMomentMaterialKey=std::array<float,4>; // IOR, f0, azimuth, tilt
inline FiberNativeMomentMaterialKey MakeFiberNativeMomentMaterialKey(const StrandMaterial& material) {
    const float ratio=(material.ior-1.f)/(material.ior+1.f);
    return {material.ior,ratio*ratio,material.azimuthalRoughness,material.cuticleShift};
}
inline bool ValidFiberNativeMomentTable(const StrandTextureTable& table) {
    if(table.width!=FiberNativeMomentAtlasWidth||table.height!=FiberNativeMomentAtlasHeight||
       table.texels.size()!=FiberNativeMomentTexelCount)return false;
    for(const auto& texel:table.texels)for(int channel=0;channel<4;++channel)
        if(!std::isfinite(texel[channel])||texel[channel]<0||texel[channel]>1)return false;
    return true;
}
// Longitudinal roughness deliberately is not in the material key: all native
// Y slices are stored and the fragment shader samples its current width.
inline bool FiberNativeMomentKeyMatches(const StrandMaterial& material,const FiberNativeMomentMaterialKey& assetKey) {
    return MakeFiberNativeMomentMaterialKey(material)==assetKey;
}
} // namespace Render::PipelineDetail
