// Cave generation verification (Terrain cave phase):
//  - determinism of SampleCave / ShouldCarve (same seed + coordinate)
//  - seed sensitivity
//  - the per-chunk lattice cache is exactly equal to the direct sampler
//  - chunk border consistency (GenerateChunkBlocks == DensityBlockAt)
//  - surface protection (no cave within surfaceProtection of the column top)
//  - world floor protection (y <= minY never carved)
//  - caves actually exist, but do not consume the whole underground
//  - generation throughput

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "Terrain/Generator/cave_generator.h"
#include "Terrain/Public/terrain_generator.h"

using namespace Terrain;
using Clock = std::chrono::steady_clock;

namespace {

bool Expect(bool condition, const char* what) {
    std::printf("  %-56s %s\n", what, condition ? "ok" : "FAIL");
    return condition;
}

std::uint64_t FnvBytes(std::uint64_t hash, const void* data, std::size_t bytes) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < bytes; ++i) { hash ^= p[i]; hash *= 1099511628211ULL; }
    return hash;
}

} // namespace

int main() {
    int failures = 0;
    constexpr std::uint64_t kSeed = 12345;
    SetWorldSeed(kSeed);
    SetTerrainGeneratorMode(TerrainGeneratorMode::DensityField);

    const CaveGenerator::CaveConfig& config = CaveGenerator::DefaultCaveConfig();

    // 1) Determinism: same seed + coordinate -> identical sample.
    {
        bool deterministic = true;
        for (int i = 0; i < 4096; ++i) {
            const int x = (i * 37) % 501 - 250;
            const int y = (i * 53) % 120 + 1;
            const int z = (i * 71) % 501 - 250;
            const CaveGenerator::CaveSample a =
                CaveGenerator::SampleCave(x, y, z, kSeed);
            const CaveGenerator::CaveSample b =
                CaveGenerator::SampleCave(x, y, z, kSeed);
            if (a.cheese != b.cheese || a.spaghetti != b.spaghetti || a.carve != b.carve) {
                deterministic = false;
                break;
            }
        }
        failures += !Expect(deterministic, "same seed+coord -> identical cave sample");
    }

    // 2) Seed sensitivity over a decent volume.
    {
        int differing = 0;
        int carvedA = 0;
        for (int y = 4; y < 40; ++y) {
            for (int z = 0; z < 24; ++z) {
                for (int x = 0; x < 24; ++x) {
                    const bool a = CaveGenerator::SampleCave(x, y, z, kSeed).carve;
                    const bool b = CaveGenerator::SampleCave(x, y, z, 54321).carve;
                    if (a) ++carvedA;
                    if (a != b) ++differing;
                }
            }
        }
        std::printf("  seed 12345 carved %d voxels in sample; %d differ vs seed 54321\n",
                    carvedA, differing);
        failures += !Expect(differing > 50, "different seed -> different cave pattern");
    }

    // 3) Lattice cache exactness + chunk border consistency.
    {
        const glm::ivec3 section(0, 0, 0);
        CaveGenerator::CaveField field;
        field.Build(section, SectionSize, SectionHeight, SectionSize, kSeed);

        int cacheMismatches = 0;
        for (int z = 0; z < SectionSize; ++z) {
            for (int x = 0; x < SectionSize; ++x) {
                const ColumnTerrain column = ComputeColumnTerrain(x, z);
                for (int y = 0; y < SectionHeight; ++y) {
                    const bool cached = field.ShouldCarve(x, y, z, column.terrainHeight);
                    const bool direct = CaveGenerator::ShouldCarve(
                        x, y, z, column.terrainHeight, kSeed);
                    if (cached != direct) ++cacheMismatches;
                }
            }
        }
        failures += !Expect(cacheMismatches == 0,
                            "lattice cave cache == direct sampler (all voxels)");

        ChunkBlocks chunk00;
        ChunkBlocks chunk10;
        GenerateChunkBlocks(glm::ivec3(0, 0, 0), chunk00);
        GenerateChunkBlocks(glm::ivec3(1, 0, 0), chunk10);
        int borderMismatches = 0;
        for (int z = 0; z < SectionSize; ++z) {
            for (int y = 0; y < SectionHeight; ++y) {
                if (chunk00.Get(15, y, z) != DensityBlockAt(15, y, z)) ++borderMismatches;
                if (chunk10.Get(0, y, z) != DensityBlockAt(16, y, z)) ++borderMismatches;
            }
        }
        failures += !Expect(borderMismatches == 0, "cave is consistent across chunk borders");
    }

    // 4) Surface + floor protection and cave existence, over one chunk.
    {
        ChunkBlocks chunk;
        GenerateChunkBlocks(glm::ivec3(0, 0, 0), chunk);

        long long carved = 0;             // base solid but stored Air
        long long protectedLeaks = 0;     // carved at/above the cave ceiling
        long long floorLeaks = 0;         // carved at/below minY
        long long solidBase = 0;

        for (int z = 0; z < SectionSize; ++z) {
            for (int x = 0; x < SectionSize; ++x) {
                const ColumnTerrain column = ComputeColumnTerrain(x, z);
                const int caveCeiling = CaveGenerator::CaveCeiling(column.terrainHeight);
                for (int y = 0; y < SectionHeight; ++y) {
                    const float density = DensityField::DensityWithHeight(
                        column.terrainHeight, x, y, z, kSeed);
                    if (density <= 0.0f) continue;
                    ++solidBase;
                    if (chunk.Get(x, y, z) != 0) continue;
                    ++carved;
                    if (y > caveCeiling) ++protectedLeaks;
                    if (y <= config.minY) ++floorLeaks;
                }
            }
        }
        std::printf("  chunk(0,0): %lld carved cave voxels of %lld solid-base voxels\n",
                    carved, solidBase);
        failures += !Expect(carved > 0, "caves exist below the surface");
        failures += !Expect(carved * 4 < solidBase, "caves do not eat the whole underground");
        failures += !Expect(protectedLeaks == 0, "no cave within surfaceProtection");
        failures += !Expect(floorLeaks == 0, "no cave at/below minY");
    }

    // 4b) Cave walls are never painted as surface material (Grass/Sand/Snow/Ice)
    // despite the cave air above them. Scan a patch of chunks to include land.
    {
        const BlockId grass = static_cast<BlockId>(Block::Grass);
        const BlockId sand = static_cast<BlockId>(Block::Sand);
        const BlockId snow = static_cast<BlockId>(Block::Snow);
        const BlockId ice = static_cast<BlockId>(Block::Ice);
        long long surfaceInCave = 0;
        long long surfaceAbove = 0;
        for (int cz = -3; cz <= 3; ++cz) {
            for (int cx = -3; cx <= 3; ++cx) {
                ChunkBlocks chunk;
                GenerateChunkBlocks(glm::ivec3(cx, 0, cz), chunk);
                for (int z = 0; z < SectionSize; ++z) {
                    for (int x = 0; x < SectionSize; ++x) {
                        const ColumnTerrain column = ComputeColumnTerrain(
                            cx * SectionSize + x, cz * SectionSize + z);
                        const int caveCeiling = CaveGenerator::CaveCeiling(column.terrainHeight);
                        for (int y = 0; y < SectionHeight; ++y) {
                            const BlockId id = chunk.Get(x, y, z);
                            const bool surfaceMaterial =
                                id == grass || id == sand || id == snow || id == ice;
                            if (!surfaceMaterial) continue;
                            if (y <= caveCeiling) ++surfaceInCave;
                            else ++surfaceAbove;
                        }
                    }
                }
            }
        }
        std::printf("  surface material: %lld above cave ceiling, %lld inside cave range\n",
                    surfaceAbove, surfaceInCave);
        failures += !Expect(surfaceAbove > 0, "surface material still generated normally");
        failures += !Expect(surfaceInCave == 0,
                            "no surface material on cave walls");
    }

    // 5) Throughput: full density generation with caves.
    {
        constexpr int kGrid = 12;
        const auto start = Clock::now();
        std::uint64_t hash = 1469598103934665603ULL;
        for (int cz = 0; cz < kGrid; ++cz) {
            for (int cx = 0; cx < kGrid; ++cx) {
                ChunkBlocks blocks;
                GenerateChunkBlocks(glm::ivec3(cx, 0, cz), blocks);
                hash = FnvBytes(hash, blocks.blocks.data(),
                                blocks.blocks.size() * sizeof(BlockId));
            }
        }
        const double ms = std::chrono::duration<double, std::milli>(
            Clock::now() - start).count();
        const double chunks = static_cast<double>(kGrid * kGrid);
        std::printf("  cave generation: %.3f ms/chunk (%d chunks, hash=%llu)\n",
                    ms / chunks, kGrid * kGrid,
                    static_cast<unsigned long long>(hash));
    }

    std::printf("%s\n", failures == 0 ? "cave test OK" : "cave test FAILED");
    return failures == 0 ? 0 : 1;
}
