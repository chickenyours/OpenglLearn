#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

#include "Terrain/Public/terrain_components.h"

namespace Terrain {

// Block ids used by the generated terrain. Air is the default (0), so a freshly
// created ChunkBlocks is already empty.
enum class Block : BlockId {
    Air = 0,
    Stone = 1,
    Dirt = 2,
    Grass = 3,
    Sand = 4,
    Water = 5,
    Wood = 6,
    Leaves = 7,
};

// Hydrology is a single global water level: every column whose ground dips
// below it is filled with water (no flow simulation).
inline constexpr int WaterLevel = 6;
inline constexpr int TerrainMinHeight = 2;
inline constexpr int TerrainMaxHeight = 19;

// Rivers follow the 0.5 contour of a broad noise field.
inline constexpr float RiverGrid = 52.0f;
inline constexpr float RiverWidth = 0.055f;
inline constexpr float RiverDepth = 7.0f;

// Ponds are the low basins of a second, wider noise field.
inline constexpr float PondGrid = 70.0f;
inline constexpr float PondThreshold = 0.16f;
inline constexpr float PondDepth = 8.0f;

// Chance that a suitable grass column grows an oak.
inline constexpr float TreeDensity = 0.011f;

namespace Detail {

inline std::uint32_t Hash2(int x, int z, std::uint32_t seed) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 0x8da6b343u;
    h ^= static_cast<std::uint32_t>(z) * 0xd8163841u;
    h ^= seed * 0xcb1ab31fu;
    h ^= h >> 16u;
    h *= 0x7feb352du;
    h ^= h >> 15u;
    h *= 0x846ca68bu;
    h ^= h >> 16u;
    return h;
}

inline float Hash01(int x, int z, std::uint32_t seed) {
    return static_cast<float>(Hash2(x, z, seed) & 0x00ffffffu)
        / static_cast<float>(0x01000000u);
}

inline float Fade(float t) {
    return t * t * (3.0f - 2.0f * t);
}

inline float ValueNoise(float x, float z, float grid, std::uint32_t seed) {
    const float gx = x / grid;
    const float gz = z / grid;

    const int x0 = static_cast<int>(std::floor(gx));
    const int z0 = static_cast<int>(std::floor(gz));
    const int x1 = x0 + 1;
    const int z1 = z0 + 1;

    const float tx = Fade(gx - static_cast<float>(x0));
    const float tz = Fade(gz - static_cast<float>(z0));

    const float a = Hash01(x0, z0, seed);
    const float b = Hash01(x1, z0, seed);
    const float c = Hash01(x0, z1, seed);
    const float d = Hash01(x1, z1, seed);

    const float ab = a + (b - a) * tx;
    const float cd = c + (d - c) * tx;
    return ab + (cd - ab) * tz;
}

} // namespace Detail

// Base rolling hills before any water carving.
inline int BaseHeight(int worldX, int worldZ) {
    const float large = Detail::ValueNoise(
        static_cast<float>(worldX), static_cast<float>(worldZ), 31.0f, 0x13572468u);
    const float medium = Detail::ValueNoise(
        static_cast<float>(worldX), static_cast<float>(worldZ), 12.0f, 0x24681357u);
    const float small = Detail::ValueNoise(
        static_cast<float>(worldX), static_cast<float>(worldZ), 5.0f, 0x00c0ffeeu);

    const float h = 4.0f + large * 8.0f + medium * 3.5f + small * 1.5f;
    return std::clamp(
        static_cast<int>(std::floor(h)), TerrainMinHeight, TerrainMaxHeight);
}

// How deep a river channel digs at this column.
inline float RiverCarve(int worldX, int worldZ) {
    const float field = Detail::ValueNoise(
        static_cast<float>(worldX), static_cast<float>(worldZ), RiverGrid, 0x5eed1a2bu);

    const float distance = std::abs(field - 0.5f);
    if (distance >= RiverWidth) return 0.0f;

    const float t = 1.0f - distance / RiverWidth;
    return Detail::Fade(t) * RiverDepth;
}

// How deep a pond basin digs at this column.
inline float PondCarve(int worldX, int worldZ) {
    const float field = Detail::ValueNoise(
        static_cast<float>(worldX), static_cast<float>(worldZ), PondGrid, 0x0dc0ffeeu);

    if (field >= PondThreshold) return 0.0f;

    const float t = 1.0f - field / PondThreshold;
    return Detail::Fade(t) * PondDepth;
}

// Final ground height (blocks below this are solid terrain).
inline int WorldHeight(int worldX, int worldZ) {
    const int base = BaseHeight(worldX, worldZ);
    const float carve =
        std::max(RiverCarve(worldX, worldZ), PondCarve(worldX, worldZ));
    const int height = base - static_cast<int>(carve + 0.5f);
    return std::clamp(height, TerrainMinHeight, TerrainMaxHeight);
}

// Classic (pre-fancy) oak. One tree per suitable level grass column.
inline bool IsTreeAt(int worldX, int worldZ) {
    if (Detail::Hash01(worldX, worldZ, 0x5bd1e995u) > TreeDensity) return false;

    const int h = WorldHeight(worldX, worldZ);
    if (h < 4) return false;
    if (h <= WaterLevel) return false; // Never grow a tree out of the water.

    const int west = WorldHeight(worldX - 1, worldZ);
    const int east = WorldHeight(worldX + 1, worldZ);
    const int north = WorldHeight(worldX, worldZ - 1);
    const int south = WorldHeight(worldX, worldZ + 1);

    return std::abs(west - h) <= 1
        && std::abs(east - h) <= 1
        && std::abs(north - h) <= 1
        && std::abs(south - h) <= 1;
}

inline int TreeTrunkHeight(int trunkX, int trunkZ) {
    return 4 + static_cast<int>(Detail::Hash2(trunkX, trunkZ, 0x51ab3c7du) % 3u);
}

// Returns Wood/Leaves when (x, y, z) belongs to the canopy of a nearby oak.
inline BlockId TreeBlockAt(int x, int y, int z) {
    bool leaf = false;
    for (int tz = z - 2; tz <= z + 2; ++tz) {
        for (int tx = x - 2; tx <= x + 2; ++tx) {
            if (!IsTreeAt(tx, tz)) continue;

            const int trunkGround = WorldHeight(tx, tz);
            const int trunkHeight = TreeTrunkHeight(tx, tz);

            if (tx == x && tz == z
                && y >= trunkGround && y < trunkGround + trunkHeight) {
                return static_cast<BlockId>(Block::Wood);
            }

            for (int layer = 0; layer < 4; ++layer) {
                const int layerY = trunkGround + trunkHeight - 3 + layer;
                if (layerY != y) continue;

                const int radius = layer < 2 ? 2 : 1;
                const int dx = x - tx;
                const int dz = z - tz;
                if (std::abs(dx) > radius || std::abs(dz) > radius) continue;
                if (std::abs(dx) == radius && std::abs(dz) == radius) continue;
                leaf = true;
            }
        }
    }
    return leaf ? static_cast<BlockId>(Block::Leaves)
                : static_cast<BlockId>(Block::Air);
}

// Authoritative block lookup at any world coordinate. Used both to fill a chunk
// and to sample neighbours across chunk borders while meshing.
inline BlockId BlockAt(int worldX, int worldY, int worldZ) {
    const int h = WorldHeight(worldX, worldZ);

    if (worldY < h) {
        const int depth = h - 1 - worldY;
        if (depth == 0) {
            return static_cast<BlockId>(h <= WaterLevel + 1 ? Block::Sand : Block::Grass);
        }
        if (depth <= 2) return static_cast<BlockId>(Block::Dirt);
        return static_cast<BlockId>(Block::Stone);
    }

    if (h < WaterLevel && worldY < WaterLevel) {
        return static_cast<BlockId>(Block::Water);
    }

    return TreeBlockAt(worldX, worldY, worldZ);
}

// Fill one chunk column from the deterministic generator. Runs on a JobSystem
// worker, so it only touches `out`.
inline void GenerateChunkBlocks(const glm::ivec3& section, ChunkBlocks& out) {
    out.blocks.fill(0);

    const int baseX = section.x * SectionSize;
    const int baseZ = section.z * SectionSize;

    const auto write = [&](int x, int y, int z, BlockId id) {
        if (!ChunkBlocks::InBounds(x, y, z)) return;
        out.blocks[ChunkBlocks::Index(x, y, z)] = id;
    };

    // Terrain columns plus water.
    for (int z = 0; z < SectionSize; ++z) {
        for (int x = 0; x < SectionSize; ++x) {
            const int worldX = baseX + x;
            const int worldZ = baseZ + z;
            const int h = WorldHeight(worldX, worldZ);

            for (int y = 0; y < h; ++y) {
                BlockId id = static_cast<BlockId>(Block::Stone);
                if (y == h - 1) {
                    id = static_cast<BlockId>(
                        h <= WaterLevel + 1 ? Block::Sand : Block::Grass);
                } else if (y >= h - 3) {
                    id = static_cast<BlockId>(Block::Dirt);
                }
                write(x, y, z, id);
            }

            for (int y = h; y < WaterLevel; ++y) {
                write(x, y, z, static_cast<BlockId>(Block::Water));
            }
        }
    }

    // Oaks whose trunk is near this chunk; only the part landing inside the
    // chunk is written, and the mesher samples the rest deterministically.
    for (int tz = -2; tz < SectionSize + 2; ++tz) {
        for (int tx = -2; tx < SectionSize + 2; ++tx) {
            const int trunkX = baseX + tx;
            const int trunkZ = baseZ + tz;
            if (!IsTreeAt(trunkX, trunkZ)) continue;

            const int ground = WorldHeight(trunkX, trunkZ);
            const int trunkHeight = TreeTrunkHeight(trunkX, trunkZ);

            for (int y = 0; y < trunkHeight; ++y) {
                write(tx, ground + y, tz, static_cast<BlockId>(Block::Wood));
            }

            for (int layer = 0; layer < 4; ++layer) {
                const int y = ground + trunkHeight - 3 + layer;
                const int radius = layer < 2 ? 2 : 1;
                for (int dz = -radius; dz <= radius; ++dz) {
                    for (int dx = -radius; dx <= radius; ++dx) {
                        if (std::abs(dx) == radius && std::abs(dz) == radius) continue;
                        const int lx = tx + dx;
                        const int lz = tz + dz;
                        if (!ChunkBlocks::InBounds(lx, y, lz)) continue;
                        // Leaves never replace terrain or water.
                        if (out.blocks[ChunkBlocks::Index(lx, y, lz)] != 0) continue;
                        out.blocks[ChunkBlocks::Index(lx, y, lz)] =
                            static_cast<BlockId>(Block::Leaves);
                    }
                }
            }
        }
    }

    out.generated = true;
    ++out.revision;
}

// Colors and layers for the generated block ids. Kept in the module so the demo
// only has to call this once.
inline void RegisterDefaultTerrainBlocks(BlockRenderRegistry& registry) {
    constexpr std::size_t kNegativeY = static_cast<std::size_t>(Face::NegativeY);
    constexpr std::size_t kPositiveY = static_cast<std::size_t>(Face::PositiveY);

    const auto setBlock = [&](BlockId id, glm::vec3 color, Layer layer, bool occludes) {
        BlockRenderInfo info;
        info.visible = true;
        info.occludes = occludes;
        info.layer = layer;
        info.color.fill(color);
        registry.Set(id, info);
    };

    const glm::vec3 stone(0.42f, 0.45f, 0.49f);
    const glm::vec3 dirt(0.45f, 0.31f, 0.18f);
    const glm::vec3 grass(0.25f, 0.63f, 0.20f);
    const glm::vec3 sand(0.76f, 0.70f, 0.50f);
    const glm::vec3 bark(0.42f, 0.30f, 0.18f);
    const glm::vec3 barkTop(0.60f, 0.47f, 0.31f);
    const glm::vec3 foliage(0.20f, 0.56f, 0.17f);

    setBlock(static_cast<BlockId>(Block::Stone), stone, Layer::Opaque, true);
    setBlock(static_cast<BlockId>(Block::Dirt), dirt, Layer::Opaque, true);
    setBlock(static_cast<BlockId>(Block::Sand), sand, Layer::Opaque, true);

    {
        BlockRenderInfo info;
        info.visible = true;
        info.occludes = true;
        info.layer = Layer::Opaque;
        info.color.fill(dirt);
        info.color[kPositiveY] = grass;
        info.color[kNegativeY] = dirt;
        registry.Set(static_cast<BlockId>(Block::Grass), info);
    }

    {
        BlockRenderInfo info;
        info.visible = true;
        info.occludes = true;
        info.layer = Layer::Opaque;
        info.color.fill(bark);
        info.color[kPositiveY] = barkTop;
        info.color[kNegativeY] = barkTop;
        registry.Set(static_cast<BlockId>(Block::Wood), info);
    }

    {
        BlockRenderInfo info;
        info.visible = true;
        info.occludes = true;
        info.layer = Layer::Opaque;
        info.color.fill(foliage);
        info.color[kPositiveY] = foliage * 1.12f;
        info.color[kNegativeY] = foliage * 0.72f;
        registry.Set(static_cast<BlockId>(Block::Leaves), info);
    }

    {
        // Water is translucent and does not hide the faces of its neighbours,
        // so banks and riverbeds stay visible through it.
        BlockRenderInfo info;
        info.visible = true;
        info.occludes = false;
        info.layer = Layer::Transparent;
        info.color.fill(glm::vec3(0.11f, 0.33f, 0.52f));
        info.color[kPositiveY] = glm::vec3(0.15f, 0.43f, 0.63f);
        registry.Set(static_cast<BlockId>(Block::Water), info);
    }
}

} // namespace Terrain
