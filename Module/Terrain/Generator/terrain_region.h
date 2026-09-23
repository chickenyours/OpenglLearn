#pragma once

// Terrain Region classification (Terrain-3 / Terrain-4).
//
// Maps the continuous NoiseRouter signals (height, ridge, erosion, valley) to a
// small set of contiguous landform regions. Classification is a pure function of
// the router signals, so region borders are smooth contours (no per-cell
// randomness) and chunk borders stay consistent.

#include <cstdint>

#include "Terrain/Generator/noise_router.h"

namespace Terrain {

enum class TerrainRegion : std::uint8_t {
    Ocean = 0,
    Lowland = 1,
    Valley = 2,
    Hills = 3,
    Plateau = 4,
    Mountain = 5,
    Peak = 6,
    Count = 7,
};

struct TerrainRegionConfig {
    // Heights are offsets from seaLevel (seaLevel is ~48 in the new scale).
    int lowlandHeight = 24;    // seaLevel .. seaLevel+24          -> Lowland
    int hillsHeight = 24;      // seaLevel+24 ..                   -> Hills
    int plateauHeight = 30;    // high + low ridge + weathered     -> Plateau
    int mountainHeight = 54;   // high enough                      -> Mountain
    int peakHeight = 82;       // very high                        -> Peak

    float valleySignal = 0.45f;    // valley noise at/above this -> Valley
    float peakRidge = 0.98f;       // ridge at/above this        -> Peak
    float mountainRidge = 0.93f;   // ridge at/above this        -> Mountain
    float plateauRidge = 0.45f;    // ridge at/below this        -> Plateau
    float plateauErosion = 0.25f;  // erosion at/above this      -> Plateau
    float hillsErosion = 0.55f;    // erosion at/above this      -> Hills
};

inline const TerrainRegionConfig& DefaultTerrainRegionConfig() {
    static const TerrainRegionConfig config{};
    return config;
}

inline TerrainRegion ClassifyTerrainRegion(
    float terrainHeight,
    const NoiseRouter::Sample& sample,
    int seaLevel,
    const TerrainRegionConfig& config = DefaultTerrainRegionConfig()) {
    if (terrainHeight < static_cast<float>(seaLevel)) {
        return TerrainRegion::Ocean;
    }

    if (terrainHeight >= static_cast<float>(seaLevel + config.peakHeight)
        || sample.ridge >= config.peakRidge) {
        return TerrainRegion::Peak;
    }

    if (terrainHeight >= static_cast<float>(seaLevel + config.mountainHeight)
        || sample.ridge >= config.mountainRidge) {
        return TerrainRegion::Mountain;
    }

    // Plateau: high but flat -- high ground with low ridge and weathered erosion.
    if (terrainHeight >= static_cast<float>(seaLevel + config.plateauHeight)
        && sample.ridge <= config.plateauRidge
        && sample.erosion >= config.plateauErosion) {
        return TerrainRegion::Plateau;
    }

    // Valley: the valley noise carves low ground between the hills.
    if (sample.valley >= config.valleySignal
        && terrainHeight < static_cast<float>(seaLevel + config.plateauHeight)) {
        return TerrainRegion::Valley;
    }

    if (terrainHeight >= static_cast<float>(seaLevel + config.hillsHeight)
        || sample.erosion >= config.hillsErosion) {
        return TerrainRegion::Hills;
    }

    if (terrainHeight < static_cast<float>(seaLevel + config.lowlandHeight)) {
        return TerrainRegion::Lowland;
    }

    return TerrainRegion::Hills;
}

inline const char* TerrainRegionName(TerrainRegion region) {
    switch (region) {
    case TerrainRegion::Ocean: return "Ocean";
    case TerrainRegion::Lowland: return "Lowland";
    case TerrainRegion::Valley: return "Valley";
    case TerrainRegion::Hills: return "Hills";
    case TerrainRegion::Plateau: return "Plateau";
    case TerrainRegion::Mountain: return "Mountain";
    case TerrainRegion::Peak: return "Peak";
    default: return "?";
    }
}

} // namespace Terrain
