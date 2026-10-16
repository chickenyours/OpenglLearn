#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Model {

// Classification is import evidence for an editor selection, never permission
// to start a cloth solver. A ClothAsset still needs explicit construction.
enum class MeshRegionKind : std::uint8_t { Unknown, Body, Hair, Garment, Accessory };

struct MeshRegionInfo {
    std::uint32_t mesh = 0; // material section in StaticModel::meshes
    MeshRegionKind kind = MeshRegionKind::Unknown;
    float confidence = 0;
    bool authoredCloth = false; // source has a Cloth modifier, even if disabled
    bool requiresReview = true;
    bool userOverride = false;
    std::string sourceObject, sourceMesh, sourceMaterial;
    std::vector<std::string> collections, vertexGroups, modifiers, evidence;
    std::string clothPinGroup;
};

const char* MeshRegionKindName(MeshRegionKind kind);

} // namespace Model
