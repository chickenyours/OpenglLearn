// Task Terrain-5 verification: continental-scale terrain.
//  1 deterministic
//  2 chunk border consistency
//  3 height distribution
//  4 region distribution (target ranges)
//  5 large-scale continuity (region run lengths -- no random jumps)
//  + exploration info (seed, max-height / mountain / peak coordinates)
//  + debug maps (continental, height, region, biome) over 1024 blocks
//
// Seed comes from the TERRAIN_SEED environment variable (default 12345).

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

double MeanRunLength(int fixed, bool alongX, int from, int to) {
    int changes = 0;
    TerrainRegion previous = TerrainRegion::Count;
    for (int v = from; v <= to; ++v) {
        const int x = alongX ? v : fixed;
        const int z = alongX ? fixed : v;
        const TerrainRegion region = ComputeColumnTerrain(x, z).region;
        if (previous != TerrainRegion::Count && region != previous) ++changes;
        previous = region;
    }
    const int cells = to - from + 1;
    return changes > 0 ? static_cast<double>(cells) / changes : cells;
}

} // namespace

int main() {
    int failures = 0;

    std::uint64_t seed = 12345;
    if (const char* env = std::getenv("TERRAIN_SEED")) {
        seed = std::strtoull(env, nullptr, 10);
    }
    SetWorldSeed(seed);
    SetTerrainGeneratorMode(TerrainGeneratorMode::DensityField);
    const NoiseRouter::Config& router = NoiseRouter::DefaultConfig();
    std::printf("seed: %llu  seaLevel: %d\n",
                static_cast<unsigned long long>(seed), router.seaLevel);

    // 1) determinism
    const std::uint64_t checksumA = ThreeChunkChecksum();
    const std::uint64_t checksumB = ThreeChunkChecksum();
    std::printf("checksum: %llu / %llu\n",
                static_cast<unsigned long long>(checksumA),
                static_cast<unsigned long long>(checksumB));
    failures += !Expect(checksumA == checksumB, "deterministic (same seed)");

    SetWorldSeed(seed + 1);
    const std::uint64_t checksumOther = ThreeChunkChecksum();
    SetWorldSeed(seed);
    failures += !Expect(checksumOther != checksumA, "seed sensitive");

    // 2) chunk border consistency
    ChunkBlocks chunk00;
    ChunkBlocks chunk10;
    GenerateChunkBlocks(glm::ivec3(0, 0, 0), chunk00);
    GenerateChunkBlocks(glm::ivec3(1, 0, 0), chunk10);
    int boundaryMismatches = 0;
    for (int z = 0; z < SectionSize; ++z) {
        for (int y = 0; y < SectionHeight; ++y) {
            if (chunk00.Get(15, y, z) != DensityBlockAt(15, y, z)) ++boundaryMismatches;
            if (chunk10.Get(0, y, z) != DensityBlockAt(16, y, z)) ++boundaryMismatches;
        }
    }
    failures += !Expect(boundaryMismatches == 0, "chunk border consistency");

    // 4) region + biome distribution over ~1040 blocks
    constexpr int kRadius = 32;
    RegionStatistics regions;
    BiomeStatistics biomes;
    AccumulateTerrainClassification(kRadius, regions, biomes);
    const auto pct = [](long long count, long long total) {
        return 100.0 * static_cast<double>(count) / static_cast<double>(total);
    };
    std::printf("region distribution:\n");
    int nonEmptyRegions = 0;
    for (int i = 0; i < static_cast<int>(TerrainRegion::Count); ++i) {
        std::printf("  %-9s %5.1f%%\n",
                    TerrainRegionName(static_cast<TerrainRegion>(i)),
                    pct(regions.counts[i], regions.total));
        if (regions.counts[i] > 0) ++nonEmptyRegions;
    }
    failures += !Expect(nonEmptyRegions == static_cast<int>(TerrainRegion::Count),
                        "all regions present");
    const double oceanPct = pct(regions.counts[static_cast<int>(TerrainRegion::Ocean)], regions.total);
    const double lowlandPct = pct(regions.counts[static_cast<int>(TerrainRegion::Lowland)], regions.total);
    const double hillsPct = pct(regions.counts[static_cast<int>(TerrainRegion::Hills)], regions.total);
    const double mountainPct = pct(regions.counts[static_cast<int>(TerrainRegion::Mountain)], regions.total);
    const double peakPct = pct(regions.counts[static_cast<int>(TerrainRegion::Peak)], regions.total);
    failures += !Expect(oceanPct >= 10.0 && oceanPct <= 35.0, "ocean 10-35%");
    failures += !Expect(lowlandPct >= 25.0 && lowlandPct <= 55.0, "lowland 25-55%");
    failures += !Expect(hillsPct >= 10.0 && hillsPct <= 30.0, "hills 10-30%");
    failures += !Expect(mountainPct >= 8.0 && mountainPct <= 30.0, "mountain 8-30%");
    failures += !Expect(peakPct > 0.0, "peak present");

    std::printf("biome distribution:\n");
    int nonEmptyBiomes = 0;
    for (int i = 0; i < static_cast<int>(Biome::Count); ++i) {
        std::printf("  %-9s %5.1f%%\n", BiomeName(static_cast<Biome>(i)),
                    pct(biomes.counts[i], biomes.total));
        if (biomes.counts[i] > 0) ++nonEmptyBiomes;
    }
    failures += !Expect(nonEmptyBiomes >= 4, "at least 4 biomes present");

    // 5) large-scale continuity: regions must persist across many blocks.
    const double runX = MeanRunLength(0, true, -1024, 1024);
    const double runZ = MeanRunLength(0, false, -1024, 1024);
    std::printf("mean region run length: x=%.0f blocks, z=%.0f blocks\n", runX, runZ);
    failures += !Expect(runX >= 24.0 && runZ >= 24.0, "regions are large-scale (run >= 24)");

    // 3) height statistics + histogram + exploration coordinates
    const int size = 256;
    const int step = 4; // map covers 1024 blocks
    const int bins = 8;
    long long histogram[bins] = {0};
    int minHeight = SectionHeight;
    int maxHeight = 0;
    double heightSum = 0.0;
    int maxHX = 0, maxHZ = 0;
    int mountainX = 0, mountainZ = 0;
    int peakX = 0, peakZ = 0;
    const Rgb regionColors[7] = {
        {38, 89, 191}, {89, 179, 77}, {64, 150, 120}, {217, 204, 77},
        {196, 140, 80}, {130, 130, 140}, {242, 242, 247}};
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
            heightSum += h;
            if (h > maxHeight) { maxHeight = h; maxHX = worldX; maxHZ = worldZ; }
            if (column.region == TerrainRegion::Mountain) { mountainX = worldX; mountainZ = worldZ; }
            if (column.region == TerrainRegion::Peak) { peakX = worldX; peakZ = worldZ; }
            ++histogram[std::clamp(h * bins / (router.maxHeight + 1), 0, bins - 1)];

            const Rgb rc = regionColors[static_cast<int>(column.region)];
            const Rgb bc = biomeColors[static_cast<int>(column.biome)];
            const float continentalness =
                NoiseRouter::SampleRouter(worldX, worldZ, seed).continentalness;
            glm::vec3 hc;
            if (h < router.seaLevel) {
                hc = Mix(glm::vec3(0.02f, 0.02f, 0.06f), glm::vec3(0.18f, 0.40f, 0.80f),
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
            const glm::vec3 cc = continentalness < 0.0f
                ? Mix(glm::vec3(0.02f, 0.05f, 0.25f), glm::vec3(0.20f, 0.45f, 0.85f),
                      continentalness + 1.0f)
                : Mix(glm::vec3(0.35f, 0.70f, 0.40f), glm::vec3(0.75f, 0.65f, 0.35f),
                      continentalness);

            const std::size_t o = (static_cast<std::size_t>(z) * size + x) * 3u;
            heightMap[o] = static_cast<std::uint8_t>(hc.r * 255.0f);
            heightMap[o + 1] = static_cast<std::uint8_t>(hc.g * 255.0f);
            heightMap[o + 2] = static_cast<std::uint8_t>(hc.b * 255.0f);
            regionMap[o] = rc.r; regionMap[o + 1] = rc.g; regionMap[o + 2] = rc.b;
            biomeMap[o] = bc.r; biomeMap[o + 1] = bc.g; biomeMap[o + 2] = bc.b;
            continentalMap[o] = static_cast<std::uint8_t>(cc.r * 255.0f);
            continentalMap[o + 1] = static_cast<std::uint8_t>(cc.g * 255.0f);
            continentalMap[o + 2] = static_cast<std::uint8_t>(cc.b * 255.0f);
        }
    }
    const double columns = static_cast<double>(size) * size;
    std::printf("height: min=%d max=%d avg=%.2f (seaLevel=%d)\n",
                minHeight, maxHeight, heightSum / columns, router.seaLevel);
    std::printf("height histogram:\n");
    for (int i = 0; i < bins; ++i) {
        std::printf("  %3d-%3d : %5.1f%%\n", i * (router.maxHeight + 1) / bins,
                    (i + 1) * (router.maxHeight + 1) / bins - 1,
                    100.0 * static_cast<double>(histogram[i]) / columns);
    }
    std::printf("explore: max height %d at (%d,%d), mountain (%d,%d), peak (%d,%d)\n",
                maxHeight, maxHX, maxHZ, mountainX, mountainZ, peakX, peakZ);
    failures += !Expect(maxHeight > router.seaLevel + 40, "max height > seaLevel + 40");
    failures += !Expect(minHeight < router.seaLevel, "ocean floor present");
    failures += !Expect(heightSum / columns > router.seaLevel - 10.0
                        && heightSum / columns < router.seaLevel + 45.0,
                        "average height reasonable");

    WriteMap("continental_map.ppm", size, continentalMap);
    WriteMap("heightmap.ppm", size, heightMap);
    WriteMap("terrain_region_map.ppm", size, regionMap);
    WriteMap("biome_map.ppm", size, biomeMap);

    std::printf("%s\n", failures == 0 ? "large scale test OK" : "large scale test FAILED");
    return failures == 0 ? 0 : 1;
}
