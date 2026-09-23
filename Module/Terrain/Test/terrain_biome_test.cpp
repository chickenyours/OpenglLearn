// Task Terrain-3 / Terrain-4 verification: TerrainRegion + Biome + SurfaceRules
// and the large-scale (128 high) world.
//  - deterministic region/biome/block output, seed sensitive
//  - chunk border consistency
//  - region distribution (each non-empty), biome distribution (>=3)
//  - height statistics + histogram, max height > seaLevel + 40
//  - debug maps: heightmap.ppm, terrain_region_map.ppm, biome_map.ppm
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
    std::printf("  %-52s %s\n", what, condition ? "ok" : "FAIL");
    return condition;
}

struct Rgb { std::uint8_t r, g, b; };

glm::vec3 Mix(glm::vec3 a, glm::vec3 b, float t) {
    return a + (b - a) * std::clamp(t, 0.0f, 1.0f);
}

void WriteMap(const char* path, int size, const std::vector<std::uint8_t>& rgb) {
    if (std::FILE* f = std::fopen(path, "wb")) {
        std::fprintf(f, "P6\n%d %d\n255\n", size, size);
        std::fwrite(rgb.data(), 1, rgb.size(), f);
        std::fclose(f);
    }
}

} // namespace

int main() {
    int failures = 0;
    SetWorldSeed(12345);
    SetTerrainGeneratorMode(TerrainGeneratorMode::DensityField);
    const NoiseRouter::Config& router = NoiseRouter::DefaultConfig();

    // 1) determinism
    const std::uint64_t checksumA = ThreeChunkChecksum();
    const std::uint64_t checksumB = ThreeChunkChecksum();
    std::printf("three-chunk checksum (seed=12345): %llu / %llu\n",
                static_cast<unsigned long long>(checksumA),
                static_cast<unsigned long long>(checksumB));
    failures += !Expect(checksumA == checksumB, "same seed -> identical blocks");

    SetWorldSeed(54321);
    const std::uint64_t checksumOther = ThreeChunkChecksum();
    SetWorldSeed(12345);
    failures += !Expect(checksumOther != checksumA, "different seed -> different blocks");

    // 2) chunk border consistency
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
        }
    }
    for (int x = 0; x < SectionSize; ++x) {
        for (int y = 0; y < SectionHeight; ++y) {
            if (chunk00.Get(x, y, 15) != DensityBlockAt(x, y, 15)) ++boundaryMismatches;
            if (chunk01.Get(x, y, 0) != DensityBlockAt(x, y, 16)) ++boundaryMismatches;
        }
    }
    failures += !Expect(boundaryMismatches == 0, "chunk borders match the world function");

    // 3) region + biome distribution (large sample: 65x65 chunks ~ 1040 blocks)
    constexpr int kRadius = 32;
    RegionStatistics regions;
    BiomeStatistics biomes;
    AccumulateTerrainClassification(kRadius, regions, biomes);

    std::printf("region distribution (%lld columns):\n", regions.total);
    int nonEmptyRegions = 0;
    for (int i = 0; i < static_cast<int>(TerrainRegion::Count); ++i) {
        const double pct = 100.0 * static_cast<double>(regions.counts[i])
            / static_cast<double>(regions.total);
        std::printf("  %-9s %5.1f%%\n", TerrainRegionName(static_cast<TerrainRegion>(i)), pct);
        if (regions.counts[i] > 0) ++nonEmptyRegions;
    }
    failures += !Expect(nonEmptyRegions == static_cast<int>(TerrainRegion::Count),
                        "every terrain region is present");

    std::printf("biome distribution:\n");
    int nonEmptyBiomes = 0;
    for (int i = 0; i < static_cast<int>(Biome::Count); ++i) {
        const double pct = 100.0 * static_cast<double>(biomes.counts[i])
            / static_cast<double>(biomes.total);
        std::printf("  %-9s %5.1f%%\n", BiomeName(static_cast<Biome>(i)), pct);
        if (biomes.counts[i] > 0) ++nonEmptyBiomes;
    }
    failures += !Expect(nonEmptyBiomes >= 3, "at least 3 biomes present");

    // 4) height statistics + histogram
    const int size = 512;
    const int step = 2; // map covers size*step = 1024 blocks
    const int bins = 8;
    long long histogram[bins] = {0};
    int minHeight = SectionHeight;
    int maxHeight = 0;
    double heightSum = 0.0;
    const Rgb regionColors[7] = {
        {38, 89, 191},   // Ocean
        {89, 179, 77},   // Lowland
        {64, 150, 120},  // Valley
        {217, 204, 77},  // Hills
        {196, 140, 80},  // Plateau
        {130, 130, 140}, // Mountain
        {242, 242, 247}, // Peak
    };
    const Rgb biomeColors[5] = {
        {38, 89, 191}, {102, 179, 77}, {31, 107, 38}, {217, 204, 89}, {242, 245, 250}};

    std::vector<std::uint8_t> heightMap(static_cast<std::size_t>(size) * size * 3u);
    std::vector<std::uint8_t> regionMap(static_cast<std::size_t>(size) * size * 3u);
    std::vector<std::uint8_t> biomeMap(static_cast<std::size_t>(size) * size * 3u);
    std::vector<std::uint8_t> continentalMap(static_cast<std::size_t>(size) * size * 3u);
    for (int z = 0; z < size; ++z) {
        for (int x = 0; x < size; ++x) {
            const int worldX = (x - size / 2) * step;
            const int worldZ = (z - size / 2) * step;
            const ColumnTerrain column = ComputeColumnTerrain(worldX, worldZ);
            const int h = static_cast<int>(std::lround(column.terrainHeight));
            minHeight = std::min(minHeight, h);
            maxHeight = std::max(maxHeight, h);
            heightSum += h;
            const int bin = std::clamp(h * bins / (router.maxHeight + 1), 0, bins - 1);
            ++histogram[bin];

            const Rgb rc = regionColors[static_cast<int>(column.region)];
            const Rgb bc = biomeColors[static_cast<int>(column.biome)];
            const float continentalness = NoiseRouter::SampleRouter(
                worldX, worldZ, GetWorldSeed()).continentalness;

            glm::vec3 hc;
            if (h < router.seaLevel) {
                hc = Mix(glm::vec3(0.02f, 0.02f, 0.06f),
                         glm::vec3(0.18f, 0.40f, 0.80f),
                         static_cast<float>(h) / static_cast<float>(router.seaLevel));
            } else if (h < router.seaLevel + 20) {
                hc = Mix(glm::vec3(0.18f, 0.40f, 0.80f), glm::vec3(0.35f, 0.70f, 0.28f),
                         static_cast<float>(h - router.seaLevel) / 20.0f);
            } else if (h < router.seaLevel + 40) {
                hc = Mix(glm::vec3(0.35f, 0.70f, 0.28f), glm::vec3(0.85f, 0.80f, 0.30f),
                         static_cast<float>(h - router.seaLevel - 20) / 20.0f);
            } else if (h < router.seaLevel + 65) {
                hc = Mix(glm::vec3(0.85f, 0.80f, 0.30f), glm::vec3(0.55f, 0.55f, 0.58f),
                         static_cast<float>(h - router.seaLevel - 40) / 25.0f);
            } else {
                hc = Mix(glm::vec3(0.55f, 0.55f, 0.58f), glm::vec3(0.97f, 0.97f, 1.0f),
                         static_cast<float>(h - router.seaLevel - 65) / 40.0f);
            }

            const std::size_t o = (static_cast<std::size_t>(z) * size + x) * 3u;
            heightMap[o + 0] = static_cast<std::uint8_t>(hc.r * 255.0f);
            heightMap[o + 1] = static_cast<std::uint8_t>(hc.g * 255.0f);
            heightMap[o + 2] = static_cast<std::uint8_t>(hc.b * 255.0f);
            regionMap[o + 0] = rc.r; regionMap[o + 1] = rc.g; regionMap[o + 2] = rc.b;
            biomeMap[o + 0] = bc.r; biomeMap[o + 1] = bc.g; biomeMap[o + 2] = bc.b;

            // Continentalness: deep blue ocean -> tan land.
            const glm::vec3 cc = continentalness < 0.0f
                ? Mix(glm::vec3(0.02f, 0.05f, 0.25f), glm::vec3(0.20f, 0.45f, 0.85f),
                      continentalness + 1.0f)
                : Mix(glm::vec3(0.35f, 0.70f, 0.40f), glm::vec3(0.75f, 0.65f, 0.35f),
                      continentalness);
            continentalMap[o + 0] = static_cast<std::uint8_t>(cc.r * 255.0f);
            continentalMap[o + 1] = static_cast<std::uint8_t>(cc.g * 255.0f);
            continentalMap[o + 2] = static_cast<std::uint8_t>(cc.b * 255.0f);
        }
    }

    const double columns = static_cast<double>(size) * size;
    std::printf("height: min=%d max=%d avg=%.2f (seaLevel=%d)\n",
                minHeight, maxHeight, heightSum / columns, router.seaLevel);
    std::printf("height histogram (%d bins over 0..%d):\n", bins, router.maxHeight);
    for (int i = 0; i < bins; ++i) {
        const double pct = 100.0 * static_cast<double>(histogram[i]) / columns;
        std::printf("  %3d-%3d : %5.1f%%\n",
                    i * (router.maxHeight + 1) / bins,
                    (i + 1) * (router.maxHeight + 1) / bins - 1, pct);
    }
    failures += !Expect(maxHeight > router.seaLevel + 40, "max height > seaLevel + 40");

    WriteMap("heightmap.ppm", size, heightMap);
    WriteMap("terrain_region_map.ppm", size, regionMap);
    WriteMap("biome_map.ppm", size, biomeMap);
    WriteMap("continental_map.ppm", size, continentalMap);

    std::printf("%s\n", failures == 0 ? "biome test OK" : "biome test FAILED");
    return failures == 0 ? 0 : 1;
}
