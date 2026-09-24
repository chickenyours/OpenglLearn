#pragma once

// Deterministic 3D cave field (Terrain cave phase).
//
// The cave field is a pure function of (worldX, worldY, worldZ, seed, surface
// height, config), so chunk borders, repeated generation and different worker
// threads all agree. It is intentionally independent from biome / surface /
// aquifer logic; the DensityField integration lives in terrain_generator.h.
//
// Structure (first version, deliberately simple):
//   * Cheese cave   : one low-frequency 3D noise, carved where noise > threshold.
//   * Spaghetti cave: two higher-frequency 3D noises, carved along the tubes
//                     where both fields cross zero (|n| < width).
// Height guards keep the surface sealed and the world floor intact.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "Terrain/Generator/density_field.h"

namespace Terrain::CaveGenerator {

// Scenario-tuned parameters, kept in one place (no scattered magic numbers).
struct CaveConfig {
    // Cheese cave: large open caverns.
    float cheeseFrequency = 0.038f;  // ~26 block features
    float cheeseThreshold = 0.66f;   // carve where noise is above this

    // Spaghetti cave: long winding tunnels.
    float spaghettiFrequency = 0.060f; // ~17 block features
    float spaghettiWidth = 0.045f;     // carve where |n| < width

    int minY = 3;               // y <= minY is never carved (world floor)
    int surfaceProtection = 6;  // no cave within this many blocks of the surface
};

inline const CaveConfig& DefaultCaveConfig() {
    static const CaveConfig config{};
    return config;
}

// Highest y that may be carved for a column with the given surface height.
// (Caves only exist strictly below this; the surface stays sealed.)
inline int CaveCeiling(float surfaceHeight,
                       const CaveConfig& config = DefaultCaveConfig()) {
    return static_cast<int>(std::floor(surfaceHeight)) - config.surfaceProtection;
}

struct CaveSample {
    float cheese = 0.0f;
    float spaghetti = 0.0f;  // min(|tubeA|, |tubeB|); small means inside a tube
    bool carve = false;
};

// Raw noise sample: world-coordinate deterministic, no height guard. Exposed so
// tests (and callers) can reason about the field without the surface rules.
inline CaveSample SampleCave(int worldX, int worldY, int worldZ,
                             std::uint64_t seed,
                             const CaveConfig& config = DefaultCaveConfig()) {
    const float fx = static_cast<float>(worldX);
    const float fy = static_cast<float>(worldY);
    const float fz = static_cast<float>(worldZ);

    CaveSample sample;
    sample.cheese = DensityField::ValueNoise3D(
        fx * config.cheeseFrequency,
        fy * config.cheeseFrequency,
        fz * config.cheeseFrequency,
        seed ^ 0xCA7E0001ULL);

    const float sx = fx * config.spaghettiFrequency;
    const float sy = fy * config.spaghettiFrequency;
    const float sz = fz * config.spaghettiFrequency;
    const float tubeA = DensityField::ValueNoise3D(sx, sy, sz, seed ^ 0xCA7E0002ULL);
    const float tubeB = DensityField::ValueNoise3D(
        sx + 17.0f, sy + 31.0f, sz + 47.0f, seed ^ 0xCA7E0003ULL);
    sample.spaghetti = std::min(std::abs(tubeA), std::abs(tubeB));
    sample.carve = sample.cheese > config.cheeseThreshold
        || sample.spaghetti < config.spaghettiWidth;
    return sample;
}

// Full sample with the height / floor guard applied to `carve`.
inline CaveSample Sample(int worldX, int worldY, int worldZ, float surfaceHeight,
                         std::uint64_t seed,
                         const CaveConfig& config = DefaultCaveConfig()) {
    CaveSample sample = SampleCave(worldX, worldY, worldZ, seed, config);
    sample.carve = sample.carve
        && worldY > config.minY
        && worldY <= CaveCeiling(surfaceHeight, config);
    return sample;
}

// Height-guarded carve decision used by world generation and reference queries.
inline bool ShouldCarve(int worldX, int worldY, int worldZ, float surfaceHeight,
                        std::uint64_t seed,
                        const CaveConfig& config = DefaultCaveConfig()) {
    if (worldY <= config.minY) return false;
    if (worldY > CaveCeiling(surfaceHeight, config)) return false;
    return SampleCave(worldX, worldY, worldZ, seed, config).carve;
}

// ---------------------------------------------------------------------------
// Per-chunk lattice cache
// ---------------------------------------------------------------------------
//
// The cave frequencies are low (0.035 / 0.055), so a whole 16x128x16 chunk spans
// only a handful of noise lattice cells per axis. Evaluating the (expensive)
// 8-hash trilinear ValueNoise3D per voxel is wasteful; caching the lattice
// corner values once per chunk and interpolating is *exactly* the same result
// (same Fade, same Hash01, same lerp order) while removing almost all hashing.
//
// Lifetime: built and used inside one generation job, never shared.
class CaveField {
public:
    void Build(const glm::ivec3& section, int sizeX, int sizeY, int sizeZ,
               std::uint64_t seed,
               const CaveConfig& config = DefaultCaveConfig()) {
        config_ = config;
        const int baseX = section.x * sizeX;
        const int baseY = section.y * sizeY;
        const int baseZ = section.z * sizeZ;

        BuildLattice(cheese_, baseX, baseY, baseZ, sizeX, sizeY, sizeZ,
                     config.cheeseFrequency, seed ^ 0xCA7E0001ULL,
                     glm::vec3(0.0f));
        BuildLattice(tubeA_, baseX, baseY, baseZ, sizeX, sizeY, sizeZ,
                     config.spaghettiFrequency, seed ^ 0xCA7E0002ULL,
                     glm::vec3(0.0f));
        BuildLattice(tubeB_, baseX, baseY, baseZ, sizeX, sizeY, sizeZ,
                     config.spaghettiFrequency, seed ^ 0xCA7E0003ULL,
                     glm::vec3(17.0f, 31.0f, 47.0f));
    }

    bool ShouldCarve(int worldX, int worldY, int worldZ, float surfaceHeight) const {
        if (worldY <= config_.minY) return false;
        if (worldY > CaveCeiling(surfaceHeight, config_)) return false;

        const float cheese = Eval(cheese_, worldX, worldY, worldZ);
        if (cheese > config_.cheeseThreshold) return true;

        const float a = Eval(tubeA_, worldX, worldY, worldZ);
        const float b = Eval(tubeB_, worldX, worldY, worldZ);
        return std::min(std::abs(a), std::abs(b)) < config_.spaghettiWidth;
    }

    float Cheese(int worldX, int worldY, int worldZ) const {
        return Eval(cheese_, worldX, worldY, worldZ);
    }
    float Spaghetti(int worldX, int worldY, int worldZ) const {
        return std::min(std::abs(Eval(tubeA_, worldX, worldY, worldZ)),
                        std::abs(Eval(tubeB_, worldX, worldY, worldZ)));
    }

private:
    struct Lattice {
        float frequency = 0.0f;
        glm::vec3 offset{0.0f};
        std::uint64_t seed = 0;
        int origin[3] = {0, 0, 0};
        int count[3] = {0, 0, 0};
        std::vector<float> values;
        bool valid = false;
    };

    static std::size_t Index(const Lattice& lattice, int ix, int iy, int iz) {
        return (static_cast<std::size_t>(iy) * lattice.count[2] + iz)
            * lattice.count[0] + ix;
    }

    static void BuildLattice(Lattice& lattice, int baseX, int baseY, int baseZ,
                             int sizeX, int sizeY, int sizeZ, float frequency,
                             std::uint64_t seed, glm::vec3 offset) {
        lattice.frequency = frequency;
        lattice.offset = offset;
        lattice.seed = seed;

        const auto axis = [](float lo, float hi, int& origin, int& count) {
            origin = static_cast<int>(std::floor(lo));
            const int top = static_cast<int>(std::floor(hi)) + 1;
            count = top - origin + 1;
        };
        axis(baseX * frequency + offset.x,
             (baseX + sizeX - 1) * frequency + offset.x,
             lattice.origin[0], lattice.count[0]);
        axis(baseY * frequency + offset.y,
             (baseY + sizeY - 1) * frequency + offset.y,
             lattice.origin[1], lattice.count[1]);
        axis(baseZ * frequency + offset.z,
             (baseZ + sizeZ - 1) * frequency + offset.z,
             lattice.origin[2], lattice.count[2]);

        if (lattice.count[0] <= 1 || lattice.count[1] <= 1 || lattice.count[2] <= 1) {
            lattice.valid = false;
            return;
        }
        lattice.values.resize(static_cast<std::size_t>(lattice.count[0])
                              * lattice.count[1] * lattice.count[2]);
        for (int iy = 0; iy < lattice.count[1]; ++iy) {
            for (int iz = 0; iz < lattice.count[2]; ++iz) {
                for (int ix = 0; ix < lattice.count[0]; ++ix) {
                    lattice.values[Index(lattice, ix, iy, iz)] = DensityField::Hash01(
                        lattice.origin[0] + ix,
                        lattice.origin[1] + iy,
                        lattice.origin[2] + iz, seed);
                }
            }
        }
        lattice.valid = true;
    }

    static float Eval(const Lattice& lattice, int x, int y, int z) {
        if (!lattice.valid) return 0.0f;

        const float sx = static_cast<float>(x) * lattice.frequency + lattice.offset.x;
        const float sy = static_cast<float>(y) * lattice.frequency + lattice.offset.y;
        const float sz = static_cast<float>(z) * lattice.frequency + lattice.offset.z;

        const int ix0 = static_cast<int>(std::floor(sx));
        const int iy0 = static_cast<int>(std::floor(sy));
        const int iz0 = static_cast<int>(std::floor(sz));
        const int lx = ix0 - lattice.origin[0];
        const int ly = iy0 - lattice.origin[1];
        const int lz = iz0 - lattice.origin[2];

        if (lx < 0 || ly < 0 || lz < 0
            || lx + 1 >= lattice.count[0]
            || ly + 1 >= lattice.count[1]
            || lz + 1 >= lattice.count[2]) {
            // Outside the cached box (should not happen for a chunk-sized
            // region); fall back to the exact direct evaluation.
            return DensityField::ValueNoise3D(sx, sy, sz, lattice.seed);
        }

        const float tx = DensityField::Fade(sx - static_cast<float>(ix0));
        const float ty = DensityField::Fade(sy - static_cast<float>(iy0));
        const float tz = DensityField::Fade(sz - static_cast<float>(iz0));

        const float c000 = lattice.values[Index(lattice, lx, ly, lz)];
        const float c100 = lattice.values[Index(lattice, lx + 1, ly, lz)];
        const float c010 = lattice.values[Index(lattice, lx, ly + 1, lz)];
        const float c110 = lattice.values[Index(lattice, lx + 1, ly + 1, lz)];
        const float c001 = lattice.values[Index(lattice, lx, ly, lz + 1)];
        const float c101 = lattice.values[Index(lattice, lx + 1, ly, lz + 1)];
        const float c011 = lattice.values[Index(lattice, lx, ly + 1, lz + 1)];
        const float c111 = lattice.values[Index(lattice, lx + 1, ly + 1, lz + 1)];

        const float x00 = c000 + (c100 - c000) * tx;
        const float x10 = c010 + (c110 - c010) * tx;
        const float x01 = c001 + (c101 - c001) * tx;
        const float x11 = c011 + (c111 - c011) * tx;
        const float y0v = x00 + (x10 - x00) * ty;
        const float y1v = x01 + (x11 - x01) * ty;
        return (y0v + (y1v - y0v) * tz) * 2.0f - 1.0f;
    }

    CaveConfig config_{};
    Lattice cheese_{};
    Lattice tubeA_{};
    Lattice tubeB_{};
};

} // namespace Terrain::CaveGenerator
