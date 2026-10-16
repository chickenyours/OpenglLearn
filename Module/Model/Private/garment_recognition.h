#pragma once

#include "Model/Public/mesh_regions.h"
#include "Model/Public/skeletal_asset.h"
#include <span>

namespace Model::detail {

struct SourceModifierSemantics {
    std::string name, type;
    bool showViewport = true, showRender = true;
    std::string clothPinGroup;
};

// One entry per imported material section. The bridge maps a source object
// and material slot to this mesh; the classifier does not guess that mapping.
struct SourceMeshSemantics {
    std::uint32_t mesh = 0;
    std::string sourceObject, sourceMesh, sourceMaterial;
    std::vector<std::string> collections, vertexGroups;
    std::vector<SourceModifierSemantics> modifiers;
    std::uint32_t sourceObjectMaterialCount = 1;
};

// Pure offline recognition. One report for every render material section,
// including unknown/body/hair sections; leaves source and skinning untouched.
std::vector<MeshRegionInfo> RecognizeGarmentSections(const SkeletalAssetPackage& source,
    std::span<const SourceMeshSemantics> semantics = {});

} // namespace Model::detail
