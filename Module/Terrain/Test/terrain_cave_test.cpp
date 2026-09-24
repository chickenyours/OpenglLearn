// Cave generation verification (Terrain cave phase):
//  - determinism of SampleCave / ShouldCarve (same seed + coordinate)
//  - seed sensitivity
//  - the per-chunk lattice cache is exactly equal to the direct sampler
//  - chunk border consistency (GenerateChunkBlocks == DensityBlockAt)
//  - protected sea floor, selected surface entrances connected to deep tunnels
//  - world floor protection (y <= minY never carved)
//  - caves actually exist, but do not consume the whole underground
//  - generation throughput

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
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

    {
        using namespace CaveGenerator;
        failures += !Expect(TubeDistance(0.0f, 0.8f) > config.spaghettiWidth,
                            "one zero field alone cannot carve a flat sheet");
        failures += !Expect(std::abs(TubeDistance(0.12f, 0.16f) - 0.20f) < 0.00001f,
                            "tube cross-section uses circular distance");
        CaveSample tube;
        tube.spaghetti = config.spaghettiWidth * 0.5f;
        failures += !Expect(!CarveWithGuards(tube, 80, 80, 0, config)
                            && CarveWithGuards(tube, 80, 80, 1, config),
                            "only selected entrance tubes reach the surface");
        failures += !Expect(!CarveWithGuards(tube, 74, 80, 0, config)
                            && CarveWithGuards(tube, 65, 80, 0, config),
                            "sealed tunnel narrows gradually towards roof");
    }

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

        // Negative coordinates and out-of-cache fallback, including the entire
        // entrance band at an inland synthetic surface and a sealed config.
        for(bool entrances : {false, true}) {
            auto settings = config;
            settings.surfaceEntrances = entrances;
            field.Build({-2, 0, -1}, SectionSize, SectionHeight, SectionSize, kSeed, settings);
            for(int z = -18; z <= 1; z += 2) {
                for(int x = -34; x <= -13; x += 2) {
                    for(int y = 0; y < 100; y += 3) {
                        if(field.ShouldCarve(x, y, z, 80) !=
                           CaveGenerator::ShouldCarve(x, y, z, 80, kSeed, settings)) ++cacheMismatches;
                        if(!entrances && y > CaveGenerator::CaveCeiling(80, settings) &&
                           field.ShouldCarve(x, y, z, 80)) ++cacheMismatches;
                    }
                }
            }
        }
        failures += !Expect(cacheMismatches == 0, "negative coords / fallback / sealed mode agree");
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
        failures += !Expect(protectedLeaks == 0, "low coastal column keeps its protected roof");
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

    // 5) Actual block-world entrances, not just noise-mask holes: a carved
    // surface voxel must connect through carved rock to a walkable deep tunnel.
    {
        constexpr int chunks = 12, width = chunks * SectionSize;
        constexpr int height = SectionHeight;
        // The origin is coastal for this seed. Locate inland terrain rather
        // than weakening sea-floor protection to make a coastal fixture pass.
        glm::ivec2 center{0};
        bool foundLand = false;
        for(int z = -1024; z <= 1024 && !foundLand; z += 64) {
            for(int x = -1024; x <= 1024; x += 64) {
                if(ComputeColumnTerrain(x, z).terrainHeight < 64.0f) continue;
                center = {x, z}; foundLand = true; break;
            }
        }
        failures += !Expect(foundLand, "inland entrance test region located");
        const int originX = center.x - width / 2, originZ = center.y - width / 2;
        const auto index = [](int x, int y, int z) {
            return (y * width + z) * width + x;
        };
        std::vector<std::uint8_t> cells(width * width * height, 0);
        int openings = 0;
        int entranceBorderMismatches = 0;
        float maxSurface = 0.0f;
        for(int cz = 0; cz < chunks; ++cz) {
            for(int cx = 0; cx < chunks; ++cx) {
                ChunkBlocks blocks;
                GenerateChunkBlocks({originX / SectionSize + cx, 0, originZ / SectionSize + cz}, blocks);
                for(int z = 0; z < SectionSize; ++z) {
                    for(int x = 0; x < SectionSize; ++x) {
                        const int px = cx * SectionSize + x, pz = cz * SectionSize + z;
                        const int wx = originX + px, wz = originZ + pz;
                        const auto column = ComputeColumnTerrain(wx, wz);
                        maxSurface = std::max(maxSurface, column.terrainHeight);
                        int top = std::min(height - 1, static_cast<int>(std::ceil(column.terrainHeight + DensityField::Noise3DAmplitude)));
                        while(top >= 0 && DensityField::DensityWithHeight(column.terrainHeight, wx, top, wz, kSeed) <= 0) --top;
                        if(x == 0 || x == SectionSize - 1) {
                            for(int y = std::max(0, top - 10); y <= std::min(height - 1, top + 3); ++y) {
                                if(blocks.Get(x, y, z) != DensityBlockFromColumn(column, wx, y, wz, kSeed))
                                    ++entranceBorderMismatches;
                            }
                        }
                        for(int y = 0; y <= top; ++y) {
                            if(blocks.Get(x, y, z) != 0) continue;
                            cells[index(px, y, pz)] = 1;
                            if(y == top && y >= DensitySeaLevel()) {
                                cells[index(px, y, pz)] |= 2;
                                ++openings;
                            }
                        }
                    }
                }
            }
        }
        int connectedEntrances = 0;
        for(int start = 0; start < static_cast<int>(cells.size()); ++start) {
            if(cells[start] != 1 && cells[start] != 3) continue;
            std::deque<int> frontier{start};
            cells[start] |= 4;
            int mouths = 0, minY = height, mouthY = -1, minX = width, maxX = 0, minZ = width, maxZ = 0;
            glm::ivec3 example{0};
            while(!frontier.empty()) {
                const int at = frontier.front(); frontier.pop_front();
                const int x = at % width, z = (at / width) % width, y = at / (width * width);
                minY = std::min(minY, y);
                minX = std::min(minX, x); maxX = std::max(maxX, x);
                minZ = std::min(minZ, z); maxZ = std::max(maxZ, z);
                if(cells[at] & 2) { ++mouths; mouthY = std::max(mouthY, y); example = {originX + x, y, originZ + z}; }
                for(auto direction : {glm::ivec3{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}}) {
                    const auto next = glm::ivec3(x,y,z) + direction;
                    if(next.x < 0 || next.x >= width || next.z < 0 || next.z >= width || next.y < 0 || next.y >= height) continue;
                    const int neighbor = index(next.x, next.y, next.z);
                    if((cells[neighbor] & 1) && !(cells[neighbor] & 4)) {
                        cells[neighbor] |= 4;
                        frontier.push_back(neighbor);
                    }
                }
            }
            if(mouths >= 4 && mouthY - minY >= 10 && std::max(maxX - minX, maxZ - minZ) >= 16) {
                if(connectedEntrances == 0) std::printf("  example entrance (%d,%d,%d), connected depth %d\n", example.x, example.y, example.z, mouthY - minY);
                ++connectedEntrances;
            }
        }
        std::printf("  surface openings: %d voxels, %d deep connected cave systems, max height %.1f\n", openings, connectedEntrances, maxSurface);
        failures += !Expect(openings > 0 && openings < width * width / 8, "visible entrances exist without perforating most terrain");
        failures += !Expect(connectedEntrances > 0, "entrances connect >=10 blocks down and >=16 across");
        failures += !Expect(entranceBorderMismatches == 0, "surface-band carving matches reference across inland borders");
    }

    // The demo's default seed must also produce real above-water entrances.
    {
        SetWorldSeed(0);
        bool found = false;
        for(int z = -1024; z <= 1024 && !found; z += 8) {
            for(int x = -1024; x <= 1024; x += 8) {
                const auto column = ComputeColumnTerrain(x, z);
                if(column.terrainHeight <= config.entranceMinHeight) continue;
                int y = std::min(SectionHeight - 1, static_cast<int>(std::ceil(column.terrainHeight + DensityField::Noise3DAmplitude)));
                while(y > 0 && DensityField::DensityWithHeight(column.terrainHeight, x, y, z, 0) <= 0) --y;
                if(DensityBlockFromColumn(column, x, y, z, 0) != 0) continue;
                std::printf("  default seed entrance candidate (%d,%d,%d)\n", x, y, z);
                found = true; break;
            }
        }
        failures += !Expect(found, "demo default seed has exposed cave mouths");
        SetWorldSeed(kSeed);
    }

    // 6) Throughput: full density generation with caves.
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
