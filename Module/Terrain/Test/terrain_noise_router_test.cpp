// Task Terrain-2 verification for the Noise Router terrain generator:
//  - deterministic (same seed == same result), seed changes result
//  - chunk boundary consistency + density continuity at x=15/16 and z=15/16
//  - height distribution and density range are reasonable
//  - density is monotonic in y (no caves / floating islands)
//  - ASCII height map to show plains / ocean / mountains / valleys

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "Terrain/Public/terrain_generator.h"

using namespace Terrain;

namespace {

std::uint64_t FnvBytes(std::uint64_t hash, const void* data, std::size_t bytes) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < bytes; ++i) { hash ^= p[i]; hash *= 1099511628211ULL; }
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

bool Expect(bool condition, const char* what) {
    std::printf("  %-56s %s\n", what, condition ? "ok" : "FAIL");
    return condition;
}

void PrintHeightMap(int radius) {
    std::printf("height map (seed=%llu, '@' water, '.' low, '+' mid, '^' high):\n",
                static_cast<unsigned long long>(GetWorldSeed()));
    const int seaLevel = DensitySeaLevel();
    for (int z = -radius; z < radius; z += 2) {
        for (int x = -radius; x < radius; ++x) {
            const int h = static_cast<int>(std::floor(RouterTerrainHeight(x, z)));
            char c = '.';
            if (h < seaLevel) c = '@';
            else if (h < seaLevel + 15) c = '.';
            else if (h < seaLevel + 35) c = '+';
            else c = '^';
            std::putchar(c);
        }
        std::putchar('\n');
    }
}

} // namespace

int main() {
    int failures = 0;

    SetWorldSeed(12345);
    SetTerrainGeneratorMode(TerrainGeneratorMode::DensityField);

    // 1) determinism
    const std::uint64_t checksumA = ThreeChunkChecksum();
    const std::uint64_t checksumB = ThreeChunkChecksum();
    std::printf("checksum (seed=12345): %llu / %llu\n",
                static_cast<unsigned long long>(checksumA),
                static_cast<unsigned long long>(checksumB));
    failures += !Expect(checksumA == checksumB, "same seed -> identical result");

    SetWorldSeed(54321);
    const std::uint64_t checksumOther = ThreeChunkChecksum();
    SetWorldSeed(12345);
    failures += !Expect(checksumOther != checksumA, "different seed -> different result");

    // 2) chunk boundary consistency
    ChunkBlocks chunk00;
    ChunkBlocks chunk10;
    ChunkBlocks chunk01;
    GenerateChunkBlocks(glm::ivec3(0, 0, 0), chunk00);
    GenerateChunkBlocks(glm::ivec3(1, 0, 0), chunk10);
    GenerateChunkBlocks(glm::ivec3(0, 0, 1), chunk01);

    int boundaryMismatches = 0;
    for (int z = 0; z < SectionSize; ++z) {
        for (int y = 0; y < SectionHeight; ++y) {
            if (chunk00.Get(15, y, z) != DensityBlockAt(15, y, z)) ++boundaryMismatches;
            if (chunk10.Get(0, y, z) != DensityBlockAt(16, y, z)) ++boundaryMismatches;
            if (chunk10.Get(0, y, z) != chunk00.Get(15, y, z)
                && std::abs(chunk10.Get(0, y, z) - chunk00.Get(15, y, z)) > 1) {
                // neighbouring columns may differ by one block height; not a "mismatch"
            }
        }
    }
    for (int x = 0; x < SectionSize; ++x) {
        for (int y = 0; y < SectionHeight; ++y) {
            if (chunk00.Get(x, y, 15) != DensityBlockAt(x, y, 15)) ++boundaryMismatches;
            if (chunk01.Get(x, y, 0) != DensityBlockAt(x, y, 16)) ++boundaryMismatches;
        }
    }
    failures += !Expect(boundaryMismatches == 0, "chunk borders match the world function");

    // density continuity across the seam
    float maxSeamDelta = 0.0f;
    for (int z = 0; z < SectionSize; ++z) {
        maxSeamDelta = std::max(maxSeamDelta,
            std::abs(RouterTerrainHeight(15, z) - RouterTerrainHeight(16, z)));
        maxSeamDelta = std::max(maxSeamDelta,
            std::abs(RouterTerrainHeight(z, 15) - RouterTerrainHeight(z, 16)));
    }
    std::printf("max seam height delta: %.3f blocks\n", maxSeamDelta);
    failures += !Expect(maxSeamDelta <= 3.0f, "density continuous at chunk borders");

    // 3) density monotonic in y (no caves / floating islands)
    int monotonicViolations = 0;
    float minDensity = 1e9f;
    float maxDensity = -1e9f;
    for (int z = -16; z < 16; ++z) {
        for (int x = -16; x < 16; ++x) {
            float previous = Density(x, 0, z);
            for (int y = 1; y < SectionHeight; ++y) {
                const float current = Density(x, y, z);
                if (current >= previous) ++monotonicViolations;
                previous = current;
                minDensity = std::min(minDensity, current);
                maxDensity = std::max(maxDensity, current);
            }
        }
    }
    std::printf("density range: [%.2f, %.2f]\n", minDensity, maxDensity);
    failures += !Expect(monotonicViolations == 0, "density strictly decreases with y (no caves)");
    failures += !Expect(minDensity > -160.0f && maxDensity < 160.0f, "density range reasonable");

    // 4) height distribution
    const TerrainStatistics stats = ComputeTerrainStatistics(8);
    std::printf("statistics: min=%d max=%d avg=%.2f solid=%lld water=%lld air=%lld\n",
                stats.minHeight, stats.maxHeight, stats.averageHeight,
                stats.solidBlocks, stats.waterBlocks, stats.airBlocks);
    const NoiseRouter::Config& config = NoiseRouter::DefaultConfig();
    failures += !Expect(stats.minHeight >= config.minHeight - 2
                        && stats.maxHeight <= config.maxHeight + 4,
                        "height within router bounds");
    failures += !Expect(stats.minHeight <= config.seaLevel - 1,
                        "low areas / ocean floor present");
    // Continental-scale noise means a small sample can be entirely ocean, so scan
    // a wide area for mountains.
    float wideMaxHeight = 0.0f;
    for (int z = -600; z <= 600; z += 4) {
        for (int x = -600; x <= 600; x += 4) {
            wideMaxHeight = std::max(wideMaxHeight, RouterTerrainHeight(x, z));
        }
    }
    std::printf("wide max height: %.0f\n", wideMaxHeight);
    failures += !Expect(wideMaxHeight >= config.seaLevel + 40, "mountains present");
    failures += !Expect(stats.averageHeight > config.seaLevel - 20.0f
                        && stats.averageHeight < config.seaLevel + 40.0f,
                        "average height reasonable");
    failures += !Expect(stats.solidBlocks > 0 && stats.airBlocks > 0 && stats.waterBlocks > 0,
                        "solid, water and air all present (ocean + land)");

    PrintHeightMap(80);

    // Colored height-map screenshot (256x256 blocks centred on the origin).
    {
        const int size = 256;
        const int seaLevel = DensitySeaLevel();
        const int maxHeight = NoiseRouter::DefaultConfig().maxHeight;
        std::vector<std::uint8_t> image(
            static_cast<std::size_t>(size) * size * 3u, 0);
        const auto mix = [](glm::vec3 a, glm::vec3 b, float t) {
            return a + (b - a) * t;
        };
        for (int z = 0; z < size; ++z) {
            for (int x = 0; x < size; ++x) {
                const float h = RouterTerrainHeight(x - size / 2, z - size / 2);
                glm::vec3 color;
                if (h < static_cast<float>(seaLevel)) {
                    const float t = std::clamp(h / static_cast<float>(seaLevel), 0.0f, 1.0f);
                    color = mix(glm::vec3(0.04f, 0.09f, 0.30f),
                                glm::vec3(0.25f, 0.52f, 0.78f), t);
                } else {
                    const float t = std::clamp(
                        (h - static_cast<float>(seaLevel))
                            / static_cast<float>(maxHeight - seaLevel), 0.0f, 1.0f);
                    color = mix(glm::vec3(0.33f, 0.60f, 0.24f),
                                glm::vec3(0.87f, 0.88f, 0.92f), t);
                }
                const std::size_t o =
                    (static_cast<std::size_t>(z) * size + x) * 3u;
                image[o + 0] = static_cast<std::uint8_t>(color.r * 255.0f);
                image[o + 1] = static_cast<std::uint8_t>(color.g * 255.0f);
                image[o + 2] = static_cast<std::uint8_t>(color.b * 255.0f);
            }
        }
        if (std::FILE* file = std::fopen("terrain_heightmap.ppm", "wb")) {
            std::fprintf(file, "P6\n%d %d\n255\n", size, size);
            std::fwrite(image.data(), 1, image.size(), file);
            std::fclose(file);
        }
    }

    std::printf("%s\n", failures == 0 ? "noise router test OK" : "noise router test FAILED");
    return failures == 0 ? 0 : 1;
}
