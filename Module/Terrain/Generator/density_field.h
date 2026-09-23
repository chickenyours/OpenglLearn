#pragma once

// Minecraft-1.18-style density field, first version.
//
// Deterministic 3D value noise in [-1, 1], combined with the existing surface
// height to form density(x, y, z). Positive density means solid, non-positive
// means air. Everything here is a pure function of (x, y, z, seed, surface),
// so chunk borders and repeated generation are automatically consistent.
//
// This file intentionally contains no biome / cave / aquifer / ore logic.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Terrain::DensityField {

// ---------------------------------------------------------------------------
// 3D hash + value noise
// ---------------------------------------------------------------------------

inline std::uint32_t Hash3(int x, int y, int z, std::uint64_t seed) {
    std::uint64_t h = static_cast<std::uint64_t>(static_cast<std::uint32_t>(x))
        * 0x9E3779B185EBCA87ULL;
    h ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(y))
        * 0xC2B2AE3D27D4EB4FULL;
    h ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(z))
        * 0x165667B19E3779F9ULL;
    h ^= seed * 0x27D4EB2F165667C5ULL;
    h ^= h >> 33u;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33u;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33u;
    return static_cast<std::uint32_t>(h);
}

inline float Hash01(int x, int y, int z, std::uint64_t seed) {
    return static_cast<float>(Hash3(x, y, z, seed) & 0x00ffffffu)
        / static_cast<float>(0x01000000u);
}

inline float Fade(float t) {
    return t * t * (3.0f - 2.0f * t);
}

// Trilinear value noise over the integer lattice. Returns [-1, 1].
inline float ValueNoise3D(float x, float y, float z, std::uint64_t seed) {
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int z0 = static_cast<int>(std::floor(z));
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;
    const int z1 = z0 + 1;

    const float tx = Fade(x - static_cast<float>(x0));
    const float ty = Fade(y - static_cast<float>(y0));
    const float tz = Fade(z - static_cast<float>(z0));

    const float c000 = Hash01(x0, y0, z0, seed);
    const float c100 = Hash01(x1, y0, z0, seed);
    const float c010 = Hash01(x0, y1, z0, seed);
    const float c110 = Hash01(x1, y1, z0, seed);
    const float c001 = Hash01(x0, y0, z1, seed);
    const float c101 = Hash01(x1, y0, z1, seed);
    const float c011 = Hash01(x0, y1, z1, seed);
    const float c111 = Hash01(x1, y1, z1, seed);

    const float x00 = c000 + (c100 - c000) * tx;
    const float x10 = c010 + (c110 - c010) * tx;
    const float x01 = c001 + (c101 - c001) * tx;
    const float x11 = c011 + (c111 - c011) * tx;

    const float y0v = x00 + (x10 - x00) * ty;
    const float y1v = x01 + (x11 - x01) * ty;

    return (y0v + (y1v - y0v) * tz) * 2.0f - 1.0f;
}

// ---------------------------------------------------------------------------
// Density formula (first version, deliberately simple)
// ---------------------------------------------------------------------------

inline constexpr float NoiseFrequency = 0.095f; // lattice ~10.5 blocks
inline constexpr float NoiseAmplitude = 8.0f;   // surface can move ~+/- 8 blocks
inline constexpr float InfluenceRange = 9.0f;   // noise fades out this far from the surface
inline constexpr float VerticalScale = 1.0f;
inline constexpr int BedrockDepth = 3;          // force solid near the world floor
inline constexpr float BedrockBoost = 20.0f;

// density = baseNoise * amplitude * influence + (surface - y) * verticalScale + bedrock
//
// heightBias   = surface - y          (positive below the surface)
// influence    = vertical falloff of the noise (1 at the surface, 0 far away),
//                which guarantees solid deep underground and air high above and
//                keeps the 3D variation near the surface.
// bedrock      = strong positive bias in the bottom few blocks (no holes).
inline float Density(int worldX, int worldY, int worldZ,
                     std::uint64_t seed, int surfaceHeight) {
    const float depth = static_cast<float>(surfaceHeight - worldY);
    const float influence = std::clamp(
        1.0f - std::abs(depth) / InfluenceRange, 0.0f, 1.0f);

    const float baseNoise = ValueNoise3D(
        static_cast<float>(worldX) * NoiseFrequency,
        static_cast<float>(worldY) * NoiseFrequency,
        static_cast<float>(worldZ) * NoiseFrequency,
        seed);

    const float heightBias = depth * VerticalScale;
    const float bedrock = worldY < BedrockDepth
        ? static_cast<float>(BedrockDepth - worldY) * BedrockBoost
        : 0.0f;

    return baseNoise * NoiseAmplitude * influence + heightBias + bedrock;
}

} // namespace Terrain::DensityField
