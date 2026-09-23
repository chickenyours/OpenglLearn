#pragma once

// Biome classification (Terrain-3).
//
// A low-frequency climate map (temperature + humidity) combined with the terrain
// region and altitude selects a biome. Everything is a pure function of
// (x, z, seed), so biomes are deterministic and chunk-border consistent.

#include <algorithm>
#include <cstdint>

#include "Terrain/Generator/noise_router.h"
#include "Terrain/Generator/terrain_region.h"

namespace Terrain {

enum class Biome : std::uint8_t {
    Ocean = 0,
    Plains = 1,
    Forest = 2,
    Desert = 3,
    Snow = 4,
    Count = 5,
};

struct ClimateSample {
    float temperature = 0.5f; // [0,1]
    float humidity = 0.5f;    // [0,1]
};

struct BiomeConfig {
    float temperatureFrequency = 0.0040f; // ~250 block climate bands
    float humidityFrequency = 0.0050f;

    float snowTemperature = 0.32f;  // colder than this + high altitude -> Snow
    int snowAltitude = 45;          // seaLevel + this = "high altitude"
    int peakSnowAltitude = 60;      // seaLevel + this = always Snow
    float desertTemperature = 0.66f;
    float desertHumidity = 0.34f;
    float forestHumidity = 0.56f;
};

inline const BiomeConfig& DefaultBiomeConfig() {
    static const BiomeConfig config{};
    return config;
}

inline ClimateSample SampleClimate(int worldX, int worldZ, std::uint64_t seed,
                                   const BiomeConfig& config = DefaultBiomeConfig()) {
    const float rawTemperature = NoiseRouter::ValueNoise2D(
        static_cast<float>(worldX) * config.temperatureFrequency,
        static_cast<float>(worldZ) * config.temperatureFrequency,
        seed ^ 0x7A11E4ULL);
    const float rawHumidity = NoiseRouter::ValueNoise2D(
        static_cast<float>(worldX) * config.humidityFrequency,
        static_cast<float>(worldZ) * config.humidityFrequency,
        seed ^ 0x5B0B1EULL);

    ClimateSample climate;
    climate.temperature = std::clamp(rawTemperature * 0.5f + 0.5f, 0.0f, 1.0f);
    climate.humidity = std::clamp(rawHumidity * 0.5f + 0.5f, 0.0f, 1.0f);
    return climate;
}

// Priority: Ocean > Snow > Desert > Forest > Plains.
inline Biome ClassifyBiome(TerrainRegion region,
                           const ClimateSample& climate,
                           int surfaceHeight,
                           int seaLevel,
                           const BiomeConfig& config = DefaultBiomeConfig()) {
    if (region == TerrainRegion::Ocean) {
        return Biome::Ocean;
    }

    // High peaks are always snowy, regardless of climate.
    if (surfaceHeight >= seaLevel + config.peakSnowAltitude) {
        return Biome::Snow;
    }

    const bool highAltitude = surfaceHeight >= seaLevel + config.snowAltitude;
    if (climate.temperature < config.snowTemperature && highAltitude) {
        return Biome::Snow;
    }

    if (climate.temperature > config.desertTemperature
        && climate.humidity < config.desertHumidity) {
        return Biome::Desert;
    }

    if (climate.humidity > config.forestHumidity) {
        return Biome::Forest;
    }

    return Biome::Plains;
}

inline const char* BiomeName(Biome biome) {
    switch (biome) {
    case Biome::Ocean: return "Ocean";
    case Biome::Plains: return "Plains";
    case Biome::Forest: return "Forest";
    case Biome::Desert: return "Desert";
    case Biome::Snow: return "Snow";
    default: return "?";
    }
}

} // namespace Terrain
