#pragma once

#include "Model/Private/garment_recognition.h"
#include <filesystem>
#include <span>

namespace Model::detail {

// Optional, exact-FBX-bound provenance and reviewed classifications. A present
// malformed/stale sidecar fails the import; absent metadata uses node semantics.
// Computes the SHA-256 from the bytes already read for the source asset ID.
void ApplyMaxSceneMetadata(SkeletalAssetPackage& asset,
    std::span<const SourceMeshSemantics> nodeSemantics,
    const std::filesystem::path& metadataPath,
    std::span<const std::uint8_t> fbxBytes);

} // namespace Model::detail
