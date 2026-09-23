#pragma once

// Minecraft-style Noise Router (Terrain-2).
//
// Combines a set of low-frequency 2D noises into a terrain height, giving large
// scale landforms: oceans, coasts, plains, mountains and valleys. Everything is a
// pure function of (x, z, seed) so chunks and borders stay deterministic.
//
// This file intentionally contains no biome / surface / cave / aquifer / ore logic.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Terrain::NoiseRouter {

// ---------------------------------------------------------------------------
// deterministic 2D hash + value noise
// ---------------------------------------------------------------------------

inline std::uint32_t Hash2(int x, int z, std::uint64_t seed) {
    std::uint64_t h = static_cast<std::uint64_t>(static_cast<std::uint32_t>(x))
        * 0x9E3779B185EBCA87ULL;
    h ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(z))
        * 0xC2B2AE3D27D4EB4FULL;
    h ^= seed * 0x27D4EB2F165667C5ULL;
    h ^= h >> 33u;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33u;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33u;
    return static_cast<std::uint32_t>(h);
}

inline float Hash01(int x, int z, std::uint64_t seed) {
    return static_cast<float>(Hash2(x, z, seed) & 0x00ffffffu)
        / static_cast<float>(0x01000000u);
}

inline float Fade(float t) {
    return t * t * (3.0f - 2.0f * t);
}

// Bilinear value noise over the integer lattice. Returns [-1, 1].
inline float ValueNoise2D(float x, float z, std::uint64_t seed) {
    const int x0 = static_cast<int>(std::floor(x));
    const int z0 = static_cast<int>(std::floor(z));
    const int x1 = x0 + 1;
    const int z1 = z0 + 1;

    const float tx = Fade(x - static_cast<float>(x0));
    const float tz = Fade(z - static_cast<float>(z0));

    const float a = Hash01(x0, z0, seed);
    const float b = Hash01(x1, z0, seed);
    const float c = Hash01(x0, z1, seed);
    const float d = Hash01(x1, z1, seed);

    const float ab = a + (b - a) * tx;
    const float cd = c + (d - c) * tx;
    return (ab + (cd - ab) * tz) * 2.0f - 1.0f;
}

inline float Smoothstep(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// ---------------------------------------------------------------------------
// Parameters (no scattered magic numbers)
// ---------------------------------------------------------------------------

struct Config {
    // Frequencies are per-block (smaller = larger landforms). Continental scale:
    // a major landform change every few hundred blocks.
    float continentalFrequency = 0.0020f; // ~500 block continents / oceans
    float erosionFrequency     = 0.0045f; // ~220 block erosion regions
    float ridgeFrequency       = 0.0075f; // ~133 block continuous ridges
    float detailFrequency      = 0.1100f; // ~9 block local variation
    float weirdnessFrequency   = 0.0060f; // reserved modulation
    float valleyFrequency      = 0.0050f; // ~200 block valleys

    // Amplitudes in blocks, scaled to the 128-tall chunk column.
    float continentalAmplitude = 45.0f;
    float ridgeAmplitude       = 65.0f; // mountain amplitude
    float erosionAmplitude     = 10.0f; // small erosion adjustment
    float detailAmplitude      = 3.0f;
    float valleyDepth          = 25.0f;
    // Land bias so oceans stay a minority of the world.
    float continentalBias      = 0.72f;

    int seaLevel = 48;
    int minHeight = 0;
    int maxHeight = 128;
};

inline const Config& DefaultConfig() {
    static const Config config{};
    return config;
}

// ---------------------------------------------------------------------------
// Router
// ---------------------------------------------------------------------------

struct Sample {
    float continentalness = 0.0f; // [-1,1] ocean <-> inland
    float erosion = 0.0f;         // [-1,1] smooth <-> weathered
    float ridge = 0.0f;           // [0,1]  peaks / ridges
    float detail = 0.0f;          // [-1,1] small local variation
    float weirdness = 0.0f;       // [-1,1] reserved
    float valley = 0.0f;          // [-1,1] valley carving
};

inline Sample SampleRouter(int worldX, int worldZ, std::uint64_t seed,
                           const Config& config = DefaultConfig()) {
    Sample sample;
    sample.continentalness = ValueNoise2D(
        static_cast<float>(worldX) * config.continentalFrequency,
        static_cast<float>(worldZ) * config.continentalFrequency,
        seed ^ 0x11111111ULL);
    sample.erosion = ValueNoise2D(
        static_cast<float>(worldX) * config.erosionFrequency,
        static_cast<float>(worldZ) * config.erosionFrequency,
        seed ^ 0x22222222ULL);

    const float ridgeNoise = ValueNoise2D(
        static_cast<float>(worldX) * config.ridgeFrequency,
        static_cast<float>(worldZ) * config.ridgeFrequency,
        seed ^ 0x33333333ULL);
    sample.ridge = 1.0f - std::abs(ridgeNoise); // [0,1]

    sample.detail = ValueNoise2D(
        static_cast<float>(worldX) * config.detailFrequency,
        static_cast<float>(worldZ) * config.detailFrequency,
        seed ^ 0x44444444ULL);
    sample.weirdness = ValueNoise2D(
        static_cast<float>(worldX) * config.weirdnessFrequency,
        static_cast<float>(worldZ) * config.weirdnessFrequency,
        seed ^ 0x55555555ULL);
    sample.valley = ValueNoise2D(
        static_cast<float>(worldX) * config.valleyFrequency,
        static_cast<float>(worldZ) * config.valleyFrequency,
        seed ^ 0x66666666ULL);
    return sample;
}

// Terrain surface height from a router sample (lets callers reuse one Sample).
inline float TerrainHeightFromSample(const Sample& sample,
                                     const Config& config = DefaultConfig()) {
    const float landness = std::clamp(
        sample.continentalness + config.continentalBias, -1.0f, 1.0f);

    float height = static_cast<float>(config.seaLevel)
        + landness * config.continentalAmplitude;

    // Mountains: ridge only builds where the continent is high AND erosion is low,
    // so ranges stay continuous instead of becoming isolated spikes.
    const float mountainMask =
        Smoothstep(0.15f, 0.60f, landness)
        * (1.0f - Smoothstep(-0.25f, 0.55f, sample.erosion));
    height += sample.ridge * config.ridgeAmplitude * mountainMask;

    // Erosion only slightly adjusts height; its main job is the mask above.
    height += sample.erosion * config.erosionAmplitude;
    height += sample.detail * config.detailAmplitude;

    // Valley: carve low ground between hills (only subtract, never raise).
    height -= std::max(0.0f, sample.valley) * config.valleyDepth;

    // Keep land above and ocean below the sea line so noise cannot punch isolated
    // lakes into continents or islands into the ocean.
    if (landness > 0.0f) {
        height = std::max(height, static_cast<float>(config.seaLevel) + 1.0f);
    } else {
        height = std::min(height, static_cast<float>(config.seaLevel) - 1.0f);
    }

    return std::clamp(height,
                      static_cast<float>(config.minHeight),
                      static_cast<float>(config.maxHeight));
}

// Terrain surface height in blocks.
inline float TerrainHeight(int worldX, int worldZ, std::uint64_t seed,
                           const Config& config = DefaultConfig()) {
    return TerrainHeightFromSample(SampleRouter(worldX, worldZ, seed, config), config);
}

} // namespace Terrain::NoiseRouter
