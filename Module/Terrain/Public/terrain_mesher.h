#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>

#include <glm/glm.hpp>

#include "Terrain/Public/block_atlas.h"
#include "Terrain/Public/terrain_components.h"
#include "Terrain/Public/terrain_generator.h"

namespace Terrain {

// Task 2.3: profiling breakdown filled by one TerrainMesher::Build call.
// Written by the calling job into its own task-local slot; the main thread
// aggregates after WaitIdle(). No shared state, no synchronization.
struct TerrainMeshStats {
    // Phase timings (ms). total = cache + iteration + faceTest + emit + sample.
    double totalMs = 0.0;
    double cacheMs = 0.0;      // TerrainSampleCache::Build
    double sampleMs = 0.0;     // out-of-bounds cache.At() calls only
    double faceLoopMs = 0.0;   // per visible block: 6-face loop (sample+test+emit)
    double emitMs = 0.0;       // EmitFace calls (includes vertex/index pushes)

    // Task 2.5: EmitFace internals (both include the push_back into the vector).
    double emitVertexMs = 0.0; // 4x Vertex construction + push_back
    double emitIndexMs = 0.0;  // 6x index push_back

    // Counts.
    std::uint64_t blocksVisited = 0;      // all cells scanned
    std::uint64_t invisibleBlocks = 0;    // air / not-visible cells skipped
    std::uint64_t faceChecks = 0;         // 6 per visible block
    std::uint64_t neighborSamples = 0;    // in-bounds + out-of-bounds
    std::uint64_t outOfBoundsSamples = 0; // went through the cache
    std::uint64_t visibleFaces = 0;       // passed culling (== emitted)
    std::uint64_t emittedFaces = 0;       // EmitFace calls
    std::uint64_t vertexPushes = 0;
    std::uint64_t indexPushes = 0;
    std::uint64_t vertexReallocs = 0;     // capacity growth events
    std::uint64_t indexReallocs = 0;
};

// Turns a chunk column of block ids into CPU submeshes. Faces are culled with a
// chunk-local TerrainSampleCache, which replaces repeated per-coordinate
// WorldHeight()/IsTreeAt()/BlockAt() recomputation across the chunk shell.
//
// The cache is created and destroyed inside Build(), owned by the calling job and
// never shared between threads. `stats` is optional and only used for profiling.
class TerrainMesher {
public:
    static void Build(const ChunkBlocks& blocks,
                      const glm::ivec3& section,
                      const BlockRenderRegistry& registry,
                      ChunkMesh& out,
                      TerrainMeshStats* stats = nullptr,
                      const ChunkNeighborhood* neighbors = nullptr) {
        const Clock::time_point buildStart = stats ? Clock::now() : Clock::time_point{};

        for(auto& layer : out.layers) layer.Clear();

        TerrainSampleCache cache;
        if(stats) {
            const Clock::time_point cacheStart = Clock::now();
            cache.Build(section);
            stats->cacheMs += Millis(Clock::now() - cacheStart);
        } else {
            cache.Build(section);
        }

        const glm::ivec3 base =
            glm::ivec3(section.x * SectionSize, 0, section.z * SectionSize);

        const auto sample = [&blocks, &cache, base, stats,
                             neighbors](int x, int y, int z) -> BlockId {
            if(stats) ++stats->neighborSamples;
            if(ChunkBlocks::InBounds(x, y, z)) return blocks.Get(x, y, z);

            // Border sampling prefers the live border layer of an *edited*
            // neighbour; unedited/unloaded neighbours fall back to the
            // deterministic procedural cache (which equals their generator
            // output). Corners (both axes out of range) also fall back.
            if(neighbors != nullptr && y >= 0 && y < SectionHeight) {
                BlockId border = 0;
                bool haveBorder = false;
                if(x == -1 && z >= 0 && z < SectionSize && neighbors->hasNegativeX) {
                    border = neighbors->NegativeX(z, y);
                    haveBorder = true;
                } else if(x == SectionSize && z >= 0 && z < SectionSize
                          && neighbors->hasPositiveX) {
                    border = neighbors->PositiveX(z, y);
                    haveBorder = true;
                } else if(z == -1 && x >= 0 && x < SectionSize
                          && neighbors->hasNegativeZ) {
                    border = neighbors->NegativeZ(x, y);
                    haveBorder = true;
                } else if(z == SectionSize && x >= 0 && x < SectionSize
                          && neighbors->hasPositiveZ) {
                    border = neighbors->PositiveZ(x, y);
                    haveBorder = true;
                }
                if(haveBorder) {
                    if(stats) ++stats->outOfBoundsSamples;
                    return border;
                }
            }

            if(stats) {
                ++stats->outOfBoundsSamples;
                const Clock::time_point sampleStart = Clock::now();
                const BlockId id = cache.At(base.x + x, base.y + y, base.z + z);
                stats->sampleMs += Millis(Clock::now() - sampleStart);
                return id;
            }
            return cache.At(base.x + x, base.y + y, base.z + z);
        };

        // Only scan the occupied vertical band. `minNonAirY`/`maxNonAirY` are
        // tracked while the chunk is generated (or by Set()), so the usual empty
        // upper half of the 128-tall column costs nothing.
        const int yBegin = std::max(0, blocks.minNonAirY);
        const int yEnd = std::min(SectionHeight - 1, blocks.maxNonAirY);
        for(int y = yBegin; y <= yEnd; ++y) {
            for(int z = 0; z < SectionSize; ++z) {
                for(int x = 0; x < SectionSize; ++x) {
                    if(stats) ++stats->blocksVisited;

                    const BlockId id = blocks.Get(x, y, z);
                    const BlockRenderInfo& block = registry.Get(id);
                    if(!block.visible) {
                        if(stats) ++stats->invisibleBlocks;
                        continue;
                    }

                    const Clock::time_point faceLoopStart =
                        stats ? Clock::now() : Clock::time_point{};

                    for(std::size_t face = 0; face < 6; ++face) {
                        if(stats) ++stats->faceChecks;

                        const glm::ivec3 direction = directions_[face];
                        const BlockId neighborId = sample(
                            x + direction.x, y + direction.y, z + direction.z);
                        const BlockRenderInfo& neighbor = registry.Get(neighborId);

                        if(block.layer == Layer::Transparent) {
                            // Water only draws a face against air. Against land it
                            // would z-fight with the bank, against water it would
                            // blend on top of itself.
                            if(neighborId != static_cast<BlockId>(0)) continue;
                        } else if(neighbor.occludes) {
                            continue;
                        }

                        if(stats) ++stats->visibleFaces;

                        const float variation = BlockVariation(
                            base.x + x, base.y + y, base.z + z);

                        const std::size_t layerIndex =
                            static_cast<std::size_t>(block.layer);

                        if(stats) {
                            const std::size_t vertexCapacity =
                                out.layers[layerIndex].vertices.capacity();
                            const std::size_t indexCapacity =
                                out.layers[layerIndex].indices.capacity();
                            const Clock::time_point emitStart = Clock::now();

                            EmitFace(
                                glm::vec3(static_cast<float>(x),
                                          static_cast<float>(y),
                                          static_cast<float>(z)),
                                face,
                                block,
                                block.color[face] * variation,
                                out.layers[layerIndex],
                                stats);

                            stats->emitMs += Millis(Clock::now() - emitStart);
                            stats->vertexPushes += 4;
                            stats->indexPushes += 6;
                            if(out.layers[layerIndex].vertices.capacity() != vertexCapacity) {
                                ++stats->vertexReallocs;
                            }
                            if(out.layers[layerIndex].indices.capacity() != indexCapacity) {
                                ++stats->indexReallocs;
                            }
                        } else {
                            EmitFace(
                                glm::vec3(static_cast<float>(x),
                                          static_cast<float>(y),
                                          static_cast<float>(z)),
                                face,
                                block,
                                block.color[face] * variation,
                                out.layers[layerIndex],
                                nullptr);
                        }
                    }

                    if(stats) stats->faceLoopMs += Millis(Clock::now() - faceLoopStart);
                }
            }
        }

        out.sourceRevision = blocks.revision;
        ++out.meshRevision;

        if(stats) stats->totalMs += Millis(Clock::now() - buildStart);
    }

private:
    using Clock = std::chrono::steady_clock;

    static double Millis(Clock::duration duration) {
        return std::chrono::duration<double, std::milli>(duration).count();
    }

    // Small deterministic tint so large flat areas are not a single color.
    static float BlockVariation(int x, int y, int z) {
        std::uint32_t h = static_cast<std::uint32_t>(x) * 0x8da6b343u;
        h ^= static_cast<std::uint32_t>(y) * 0x9e3779b9u;
        h ^= static_cast<std::uint32_t>(z) * 0xd8163841u;
        h ^= h >> 15u;
        h *= 0x2c1b3c6du;
        h ^= h >> 12u;
        return 0.92f + 0.12f
            * (static_cast<float>(h & 0x00ffffffu)
               / static_cast<float>(0x01000000u));
    }

    static void EmitFace(glm::vec3 base,
                         std::size_t face,
                         const BlockRenderInfo& block,
                         glm::vec3 color,
                         CpuSubmesh& out,
                         TerrainMeshStats* stats) {
        const std::uint32_t first = static_cast<std::uint32_t>(out.vertices.size());

        const Clock::time_point vertexStart = stats ? Clock::now() : Clock::time_point{};
        const std::uint16_t tileIndex = block.tile[face];
        const BlockTile atlasTile = static_cast<BlockTile>(tileIndex);

        out.vertices.resize(static_cast<std::size_t>(first) + 4);
        Vertex* vertices = out.vertices.data() + first;
        for(std::size_t i = 0; i < 4; ++i) {
            float u = 0.0f;
            float v = 0.0f;
            BlockAtlas::FaceCornerUv(atlasTile, uvs_[i].x, uvs_[i].y, u, v);
            vertices[i] = Vertex{
                base + corners_[face][i], normals_[face], color, glm::vec2(u, v), tileIndex};
        }
        if(stats) stats->emitVertexMs += Millis(Clock::now() - vertexStart);

        const Clock::time_point indexStart = stats ? Clock::now() : Clock::time_point{};
        const std::uint32_t indexBase = static_cast<std::uint32_t>(out.indices.size());
        out.indices.resize(static_cast<std::size_t>(indexBase) + 6);
        std::uint32_t* indices = out.indices.data() + indexBase;
        constexpr std::array<std::uint32_t, 6> pattern{0, 1, 2, 0, 2, 3};
        for(std::size_t i = 0; i < 6; ++i) {
            indices[i] = first + pattern[i];
        }
        if(stats) {
            stats->emitIndexMs += Millis(Clock::now() - indexStart);
            ++stats->emittedFaces;
        }
    }

    inline static constexpr std::array<glm::ivec3, 6> directions_{
        glm::ivec3{-1,0,0}, {1,0,0}, {0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1}};
    inline static const std::array<glm::vec3, 6> normals_{
        glm::vec3{-1,0,0}, {1,0,0}, {0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1}};
    inline static const std::array<glm::vec2, 4> uvs_{glm::vec2{0,0}, {1,0}, {1,1}, {0,1}};
    inline static const std::array<std::array<glm::vec3,4>,6> corners_{
        std::array<glm::vec3,4>{glm::vec3{0,0,1}, {0,0,0}, {0,1,0}, {0,1,1}},
        std::array<glm::vec3,4>{glm::vec3{1,0,0}, {1,0,1}, {1,1,1}, {1,1,0}},
        std::array<glm::vec3,4>{glm::vec3{0,0,0}, {0,0,1}, {1,0,1}, {1,0,0}},
        std::array<glm::vec3,4>{glm::vec3{0,1,1}, {0,1,0}, {1,1,0}, {1,1,1}},
        std::array<glm::vec3,4>{glm::vec3{1,0,0}, {0,0,0}, {0,1,0}, {1,1,0}},
        std::array<glm::vec3,4>{glm::vec3{0,0,1}, {1,0,1}, {1,1,1}, {0,1,1}}
    };
};

} // namespace Terrain
