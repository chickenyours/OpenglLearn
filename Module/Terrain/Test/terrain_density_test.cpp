// Task Terrain-1 verification:
//  - density mode determinism (same seed+chunk -> same checksum, repeated)
//  - chunk-boundary consistency (neighbour sampling == stored neighbour block)
//  - legacy heightmap mode still deterministic and switchable
//  - density generation throughput (chunk generation time, blocks/sec)

#include <chrono>
#include <cstdint>
#include <cstdio>

#include "Terrain/Public/terrain_generator.h"

using namespace Terrain;
using Clock = std::chrono::steady_clock;

namespace {

std::uint64_t FnvBytes(std::uint64_t hash, const void* data, std::size_t bytes) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < bytes; ++i) {
        hash ^= p[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::uint64_t ChunkChecksum(std::uint64_t hash, const glm::ivec3& section) {
    ChunkBlocks blocks;
    GenerateChunkBlocks(section, blocks);
    return FnvBytes(hash, blocks.blocks.data(),
                    blocks.blocks.size() * sizeof(BlockId));
}

std::uint64_t ThreeChunkChecksum() {
    std::uint64_t hash = 1469598103934665603ULL;
    hash = ChunkChecksum(hash, glm::ivec3(0, 0, 0));
    hash = ChunkChecksum(hash, glm::ivec3(1, 0, 0));
    hash = ChunkChecksum(hash, glm::ivec3(0, 0, 1));
    return hash;
}

} // namespace

int main() {
    int failures = 0;

    // ---- Density mode: determinism ----------------------------------------
    SetWorldSeed(12345);
    SetTerrainGeneratorMode(TerrainGeneratorMode::DensityField);

    const std::uint64_t densityA = ThreeChunkChecksum();
    const std::uint64_t densityB = ThreeChunkChecksum();
    std::printf("density checksum (seed=12345): %llu / %llu -> %s\n",
                static_cast<unsigned long long>(densityA),
                static_cast<unsigned long long>(densityB),
                densityA == densityB ? "deterministic" : "MISMATCH");
    if (densityA != densityB) ++failures;

    const std::uint64_t densityOtherSeed = ThreeChunkChecksum();
    SetWorldSeed(54321);
    const std::uint64_t densitySeed2 = ThreeChunkChecksum();
    SetWorldSeed(12345);
    std::printf("  seed changes result: %s\n",
                densitySeed2 != densityOtherSeed ? "yes" : "NO");
    if (densitySeed2 == densityOtherSeed) ++failures;

    // ---- Density mode: chunk boundary consistency -------------------------
    ChunkBlocks chunk00;
    ChunkBlocks chunk10;
    ChunkBlocks chunk01;
    GenerateChunkBlocks(glm::ivec3(0, 0, 0), chunk00);
    GenerateChunkBlocks(glm::ivec3(1, 0, 0), chunk10);
    GenerateChunkBlocks(glm::ivec3(0, 0, 1), chunk01);

    int boundaryMismatches = 0;
    for (int z = 0; z < SectionSize; ++z) {
        for (int y = 0; y < SectionHeight; ++y) {
            // Chunk (0,0) edge x=15 vs the world function / chunk (1,0) x=0.
            if (chunk00.Get(15, y, z) != DensityBlockAt(15, y, z)) ++boundaryMismatches;
            if (chunk10.Get(0, y, z) != DensityBlockAt(16, y, z)) ++boundaryMismatches;
            // Mesher samples the neighbour across the seam: must match stored data.
            if (DensityBlockAt(16, y, z) != chunk10.Get(0, y, z)) ++boundaryMismatches;
            if (DensityBlockAt(15, y, z) != chunk00.Get(15, y, z)) ++boundaryMismatches;
        }
    }
    for (int x = 0; x < SectionSize; ++x) {
        for (int y = 0; y < SectionHeight; ++y) {
            if (chunk00.Get(x, y, 15) != DensityBlockAt(x, y, 15)) ++boundaryMismatches;
            if (chunk01.Get(x, y, 0) != DensityBlockAt(x, y, 16)) ++boundaryMismatches;
        }
    }
    std::printf("boundary mismatches: %d -> %s\n", boundaryMismatches,
                boundaryMismatches == 0 ? "consistent" : "FAIL");
    if (boundaryMismatches != 0) ++failures;

    // ---- Content summary (visual sanity) ----------------------------------
    long counts[8] = {0};
    for (BlockId id : chunk00.blocks) {
        if (id < 8) ++counts[id];
    }
    std::printf("chunk(0,0) blocks: air=%ld stone=%ld dirt=%ld grass=%ld sand=%ld water=%ld wood=%ld leaves=%ld\n",
                counts[0], counts[1], counts[2], counts[3], counts[4], counts[5], counts[6], counts[7]);

    // ---- Legacy heightmap mode: still switchable + deterministic ----------
    SetTerrainGeneratorMode(TerrainGeneratorMode::Heightmap);
    const std::uint64_t heightA = ThreeChunkChecksum();
    const std::uint64_t heightB = ThreeChunkChecksum();
    std::printf("heightmap checksum: %llu / %llu -> %s\n",
                static_cast<unsigned long long>(heightA),
                static_cast<unsigned long long>(heightB),
                heightA == heightB ? "deterministic" : "MISMATCH");
    if (heightA != heightB) ++failures;
    if (heightA == densityA) {
        std::printf("  WARNING: heightmap and density checksums are equal\n");
    }

    // ---- Density throughput ----------------------------------------------
    SetTerrainGeneratorMode(TerrainGeneratorMode::DensityField);
    constexpr int kGrid = 16; // 16x16 = 256 chunks
    const auto start = Clock::now();
    std::uint64_t perfHash = 1469598103934665603ULL;
    for (int cz = 0; cz < kGrid; ++cz) {
        for (int cx = 0; cx < kGrid; ++cx) {
            perfHash = ChunkChecksum(perfHash, glm::ivec3(cx, 0, cz));
        }
    }
    const double ms = std::chrono::duration<double, std::milli>(
        Clock::now() - start).count();
    const double chunks = static_cast<double>(kGrid * kGrid);
    const double blocks = chunks * static_cast<double>(SectionVolume);
    std::printf("density generation: %.3f ms/chunk, %.0f blocks/sec (%d chunks, hash=%llu)\n",
                ms / chunks, blocks / (ms / 1000.0), kGrid * kGrid,
                static_cast<unsigned long long>(perfHash));

    std::printf("%s\n", failures == 0 ? "density test OK" : "density test FAILED");
    return failures == 0 ? 0 : 1;
}
