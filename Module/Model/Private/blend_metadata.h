#pragma once
#include "Model/Private/garment_recognition.h"
#include <filesystem>

namespace Model::detail {
// Maps exported nodes and material primitives back to their source object.
// A missing/ambiguous mapping produces an explicit review diagnostic.
std::vector<SourceMeshSemantics> ReadBlendMeshSemantics(SkeletalAssetPackage& asset,
    const std::filesystem::path& metadataPath);
// Captures the evaluated source pose as a constant generated preview sequence.
// This preserves the imported rest skeleton and its inverse-bind matrices.
// Metadata written by older bridges has no sourcePose and is left unchanged.
void ImportBlendSourcePose(SkeletalAssetPackage& asset,
    const std::filesystem::path& metadataPath);
}
