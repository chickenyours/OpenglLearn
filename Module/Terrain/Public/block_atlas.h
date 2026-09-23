#pragma once

// Phase 1 Terrain Visual Foundation: original voxel block material atlas.
//
// Produces a deterministic, 16x16-per-tile RGBA atlas (no external/Minecraft
// assets) and maps each block id to its top / side / bottom tile. The tile ids
// are expanded into the existing BlockRenderInfo::tile[6] array, so the mesher
// and its vertex layout are unchanged.
//
// This file does not touch the generator, ECS, JobSystem or the mesher.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "Terrain/Public/terrain_components.h"
#include "Terrain/Public/terrain_generator.h"

namespace Terrain {

// One entry per 16x16 tile in the atlas.
enum class BlockTile : std::uint16_t {
    GrassTop = 0,
    GrassSide,
    Dirt,
    Stone,
    Sand,
    SandstoneTop,
    SandstoneSide,
    SandstoneBottom,
    Snow,
    SnowSide,
    Ice,
    Water,
    Gravel,
    Bedrock,
    Count,
};

// Extra block ids introduced for the material set. They are intentionally not
// part of the legacy generator enum, so the generator is left untouched.
namespace BlockIds {
inline constexpr BlockId Sandstone = 8;
inline constexpr BlockId Snow = 9;
inline constexpr BlockId Ice = 10;
inline constexpr BlockId Gravel = 11;
inline constexpr BlockId Bedrock = 12;
}

struct BlockFaceTiles {
    std::uint16_t top = 0;
    std::uint16_t side = 0;
    std::uint16_t bottom = 0;
};

// Surface material themes (used by future biome/palette selection; the atlas
// itself is what this phase provides).
enum class SurfaceTheme : std::uint8_t { Temperate, Desert, Snow };

struct SurfacePalette {
    BlockId top = 0;
    BlockId filler = 0;
};

inline SurfacePalette SurfaceForTheme(SurfaceTheme theme) {
    switch (theme) {
    case SurfaceTheme::Desert:
        return {static_cast<BlockId>(Block::Sand), BlockIds::Sandstone};
    case SurfaceTheme::Snow:
        return {BlockIds::Snow, BlockIds::Ice};
    case SurfaceTheme::Temperate:
    default:
        return {static_cast<BlockId>(Block::Grass), static_cast<BlockId>(Block::Dirt)};
    }
}

namespace AtlasDetail {

inline std::uint32_t Hash(int x, int y, std::uint32_t seed) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 0x8da6b343u;
    h ^= static_cast<std::uint32_t>(y) * 0xd8163841u;
    h ^= seed * 0xcb1ab31fu;
    h ^= h >> 16u;
    h *= 0x7feb352du;
    h ^= h >> 15u;
    h *= 0x846ca68bu;
    h ^= h >> 16u;
    return h;
}

inline float Rand01(int x, int y, std::uint32_t seed) {
    return static_cast<float>(Hash(x, y, seed) & 0x00ffffffu)
        / static_cast<float>(0x01000000u);
}

inline std::uint8_t Byte(float v) {
    return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 255.0f));
}

} // namespace AtlasDetail

// Procedurally generated, deterministic 16x16 voxel textures.
class BlockAtlas {
public:
    static constexpr int TileSize = 16;
    static constexpr int Columns = 8;
    static constexpr int Rows = 2;
    static constexpr int Width = TileSize * Columns;
    static constexpr int Height = TileSize * Rows;

    // RGBA8, row-major, top-left origin. Built once (thread-safe static init).
    static const std::vector<std::uint8_t>& Pixels() {
        static const std::vector<std::uint8_t> pixels = BuildPixels();
        return pixels;
    }

    static constexpr int TileCount() { return static_cast<int>(BlockTile::Count); }

    // Normalized UV rect of a tile, for a future textured pipeline.
    static void TileRect(BlockTile tile, float& u0, float& v0, float& u1, float& v1) {
        const int index = static_cast<int>(tile);
        const int col = index % Columns;
        const int row = index / Columns;
        u0 = static_cast<float>(col * TileSize) / static_cast<float>(Width);
        v0 = static_cast<float>(row * TileSize) / static_cast<float>(Height);
        u1 = static_cast<float>((col + 1) * TileSize) / static_cast<float>(Width);
        v1 = static_cast<float>((row + 1) * TileSize) / static_cast<float>(Height);
    }

    // UV rect inset to texel centres. Nearest sampling of this rect never reads
    // a neighbouring tile in the atlas.
    static void TileTexelRect(BlockTile tile, float& u0, float& v0, float& u1, float& v1) {
        const int index = static_cast<int>(tile);
        const int col = index % Columns;
        const int row = index / Columns;
        u0 = (static_cast<float>(col * TileSize) + 0.5f) / static_cast<float>(Width);
        v0 = (static_cast<float>(row * TileSize) + 0.5f) / static_cast<float>(Height);
        u1 = (static_cast<float>((col + 1) * TileSize) - 0.5f) / static_cast<float>(Width);
        v1 = (static_cast<float>((row + 1) * TileSize) - 0.5f) / static_cast<float>(Height);
    }

    // Block id -> top / side / bottom tile.
    static BlockFaceTiles TilesFor(BlockId id) {
        const auto t = [](BlockTile tile) { return static_cast<std::uint16_t>(tile); };

        if (id == static_cast<BlockId>(Block::Grass)) return {t(BlockTile::GrassTop), t(BlockTile::GrassSide), t(BlockTile::Dirt)};
        if (id == static_cast<BlockId>(Block::Dirt)) return {t(BlockTile::Dirt), t(BlockTile::Dirt), t(BlockTile::Dirt)};
        if (id == static_cast<BlockId>(Block::Stone)) return {t(BlockTile::Stone), t(BlockTile::Stone), t(BlockTile::Stone)};
        if (id == static_cast<BlockId>(Block::Sand)) return {t(BlockTile::Sand), t(BlockTile::Sand), t(BlockTile::Sand)};
        if (id == static_cast<BlockId>(Block::Water)) return {t(BlockTile::Water), t(BlockTile::Water), t(BlockTile::Water)};
        if (id == BlockIds::Sandstone) return {t(BlockTile::SandstoneTop), t(BlockTile::SandstoneSide), t(BlockTile::SandstoneBottom)};
        if (id == BlockIds::Snow) return {t(BlockTile::Snow), t(BlockTile::SnowSide), t(BlockTile::Dirt)};
        if (id == BlockIds::Ice) return {t(BlockTile::Ice), t(BlockTile::Ice), t(BlockTile::Ice)};
        if (id == BlockIds::Gravel) return {t(BlockTile::Gravel), t(BlockTile::Gravel), t(BlockTile::Gravel)};
        if (id == BlockIds::Bedrock) return {t(BlockTile::Bedrock), t(BlockTile::Bedrock), t(BlockTile::Bedrock)};

        // Wood / Leaves and unknown ids: Stone tiles is a safe solid fallback.
        return {t(BlockTile::Stone), t(BlockTile::Stone), t(BlockTile::Stone)};
    }

private:
    static std::vector<std::uint8_t> BuildPixels() {
        std::vector<std::uint8_t> pixels(
            static_cast<std::size_t>(Width) * Height * 4u, 0);
        for (int i = 0; i < TileCount(); ++i) {
            PaintTile(pixels, static_cast<BlockTile>(i));
        }
        return pixels;
    }

    static void PaintTile(std::vector<std::uint8_t>& pixels, BlockTile tile) {
        const int index = static_cast<int>(tile);
        const int col = index % Columns;
        const int row = index / Columns;
        const std::uint32_t seed = 0xA53C9E1u + static_cast<std::uint32_t>(index) * 0x9E3779B9u;

        const auto put = [&](int x, int y, float r, float g, float b) {
            const std::size_t o =
                (static_cast<std::size_t>(row * TileSize + y) * Width
                 + static_cast<std::size_t>(col * TileSize + x)) * 4u;
            pixels[o + 0] = AtlasDetail::Byte(r);
            pixels[o + 1] = AtlasDetail::Byte(g);
            pixels[o + 2] = AtlasDetail::Byte(b);
            pixels[o + 3] = 255;
        };

        // Base fill with deterministic per-pixel brightness variation. `chunk`
        // groups pixels (>=2) for a coarser, pebble-like look.
        const auto fill = [&](float r, float g, float b, float variation, float chunk) {
            for (int y = 0; y < TileSize; ++y) {
                for (int x = 0; x < TileSize; ++x) {
                    const int sx = static_cast<int>(static_cast<float>(x) / chunk);
                    const int sy = static_cast<int>(static_cast<float>(y) / chunk);
                    const float n = AtlasDetail::Rand01(sx, sy, seed);
                    const float v = 1.0f + (n - 0.5f) * variation;
                    put(x, y, r * v, g * v, b * v);
                }
            }
        };

        switch (tile) {
        case BlockTile::GrassTop:
            fill(88.0f, 142.0f, 60.0f, 0.20f, 1.0f);
            for (int y = 0; y < TileSize; ++y)
                for (int x = 0; x < TileSize; ++x) {
                    const float n = AtlasDetail::Rand01(x, y, seed ^ 0x1234u);
                    if (n < 0.10f) put(x, y, 70.0f, 118.0f, 48.0f);
                    else if (n > 0.90f) put(x, y, 108.0f, 162.0f, 74.0f);
                }
            break;

        case BlockTile::GrassSide:
        case BlockTile::SnowSide: {
            const bool snow = tile == BlockTile::SnowSide;
            const float tr = snow ? 236.0f : 88.0f;
            const float tg = snow ? 240.0f : 142.0f;
            const float tb = snow ? 248.0f : 60.0f;
            for (int x = 0; x < TileSize; ++x) {
                const int strip = 3 + static_cast<int>(AtlasDetail::Rand01(x, col, seed) * 3.0f);
                for (int y = 0; y < TileSize; ++y) {
                    const float n = AtlasDetail::Rand01(x, y, seed);
                    const float v = 1.0f + (n - 0.5f) * 0.16f;
                    if (y < strip) put(x, y, tr * v, tg * v, tb * v);
                    else put(x, y, 122.0f * v, 86.0f * v, 56.0f * v);
                }
            }
            break;
        }

        case BlockTile::Dirt:
            fill(124.0f, 86.0f, 56.0f, 0.18f, 1.0f);
            for (int y = 0; y < TileSize; ++y)
                for (int x = 0; x < TileSize; ++x)
                    if (AtlasDetail::Rand01(x, y, seed ^ 0x55u) < 0.08f)
                        put(x, y, 98.0f, 66.0f, 42.0f);
            break;

        case BlockTile::Stone:
            fill(128.0f, 128.0f, 134.0f, 0.12f, 1.0f);
            for (int y = 0; y < TileSize; ++y)
                for (int x = 0; x < TileSize; ++x) {
                    const float n = AtlasDetail::Rand01(x, y, seed ^ 0x77u);
                    if (n < 0.05f) put(x, y, 100.0f, 100.0f, 106.0f);
                    else if (n > 0.95f) put(x, y, 150.0f, 150.0f, 156.0f);
                }
            break;

        case BlockTile::Sand:
            fill(222.0f, 205.0f, 142.0f, 0.10f, 1.0f);
            break;

        case BlockTile::SandstoneTop:
            fill(224.0f, 208.0f, 148.0f, 0.05f, 1.0f);
            break;

        case BlockTile::SandstoneSide:
            fill(222.0f, 205.0f, 142.0f, 0.08f, 1.0f);
            for (int y = 0; y < TileSize; ++y)
                for (int x = 0; x < TileSize; ++x)
                    if ((y % 5) == 0)
                        put(x, y, 196.0f, 178.0f, 120.0f);
            break;

        case BlockTile::SandstoneBottom:
            fill(214.0f, 196.0f, 134.0f, 0.08f, 2.0f);
            break;

        case BlockTile::Snow:
            fill(238.0f, 242.0f, 248.0f, 0.05f, 1.0f);
            for (int y = 0; y < TileSize; ++y)
                for (int x = 0; x < TileSize; ++x)
                    if (AtlasDetail::Rand01(x, y, seed ^ 0x9Au) > 0.93f)
                        put(x, y, 214.0f, 226.0f, 240.0f);
            break;

        case BlockTile::Ice:
            fill(150.0f, 200.0f, 236.0f, 0.10f, 1.0f);
            for (int y = 0; y < TileSize; ++y)
                for (int x = 0; x < TileSize; ++x)
                    if (AtlasDetail::Rand01(x, y, seed ^ 0xBCu) > 0.90f)
                        put(x, y, 180.0f, 222.0f, 246.0f);
            break;

        case BlockTile::Water:
            fill(58.0f, 108.0f, 188.0f, 0.08f, 1.0f);
            for (int y = 0; y < TileSize; ++y)
                for (int x = 0; x < TileSize; ++x) {
                    const float wave = 1.0f + 0.08f * std::sin(
                        (static_cast<float>(x) + static_cast<float>(y) * 0.5f) * 0.7f);
                    const float n = AtlasDetail::Rand01(x, y, seed);
                    const float v = wave * (1.0f + (n - 0.5f) * 0.06f);
                    put(x, y, 58.0f * v, 108.0f * v, 188.0f * v);
                }
            break;

        case BlockTile::Gravel:
            fill(120.0f, 116.0f, 110.0f, 0.30f, 2.0f);
            for (int y = 0; y < TileSize; y += 2)
                for (int x = 0; x < TileSize; x += 2) {
                    const float n = AtlasDetail::Rand01(x, y, seed ^ 0xDDu);
                    if (n > 0.75f) {
                        const float v = 0.85f + n * 0.3f;
                        for (int dy = 0; dy < 2; ++dy)
                            for (int dx = 0; dx < 2; ++dx)
                                put(x + dx, y + dy, 138.0f * v, 126.0f * v, 108.0f * v);
                    }
                }
            break;

        case BlockTile::Bedrock:
            fill(58.0f, 58.0f, 62.0f, 0.55f, 2.0f);
            for (int y = 0; y < TileSize; y += 2)
                for (int x = 0; x < TileSize; x += 2)
                    if (AtlasDetail::Rand01(x, y, seed ^ 0xEEu) > 0.7f)
                        for (int dy = 0; dy < 2; ++dy)
                            for (int dx = 0; dx < 2; ++dx)
                                put(x + dx, y + dy, 30.0f, 30.0f, 34.0f);
            break;

        default:
            fill(128.0f, 128.0f, 128.0f, 0.0f, 1.0f);
            break;
        }
    }
};

// Applies the atlas tile mapping to a block registry. Preserves colors/layer/
// occlusion already configured (e.g. by RegisterDefaultTerrainBlocks) and only
// fills them in for blocks that are not yet visible.
inline void ApplyDefaultBlockAtlas(BlockRenderRegistry& registry) {
    const auto apply = [&](BlockId id, BlockFaceTiles tiles, glm::vec3 fallbackColor,
                           Layer layer, bool occludes) {
        BlockRenderInfo info = registry.Get(id); // copy (Get returns a reference)
        (void)fallbackColor;
        if (!info.visible) {
            info.visible = true;
            info.occludes = occludes;
            info.layer = layer;
        }
        // Textured rendering: the atlas already carries the material colour, so the
        // per-face vertex tint stays neutral (white). A future biome tint can
        // multiply this without double-tinting the material.
        info.color.fill(glm::vec3(1.0f));
        info.topTile = tiles.top;
        info.sideTile = tiles.side;
        info.bottomTile = tiles.bottom;
        info.ApplyFaceTiles();
        registry.Set(id, info);
    };

    const auto tiles = [](BlockId id) { return BlockAtlas::TilesFor(id); };

    apply(static_cast<BlockId>(Block::Grass), tiles(static_cast<BlockId>(Block::Grass)),
          glm::vec3(0.25f, 0.63f, 0.20f), Layer::Opaque, true);
    apply(static_cast<BlockId>(Block::Dirt), tiles(static_cast<BlockId>(Block::Dirt)),
          glm::vec3(0.45f, 0.31f, 0.18f), Layer::Opaque, true);
    apply(static_cast<BlockId>(Block::Stone), tiles(static_cast<BlockId>(Block::Stone)),
          glm::vec3(0.42f, 0.45f, 0.49f), Layer::Opaque, true);
    apply(static_cast<BlockId>(Block::Sand), tiles(static_cast<BlockId>(Block::Sand)),
          glm::vec3(0.76f, 0.70f, 0.50f), Layer::Opaque, true);
    apply(static_cast<BlockId>(Block::Water), tiles(static_cast<BlockId>(Block::Water)),
          glm::vec3(0.15f, 0.43f, 0.63f), Layer::Transparent, false);

    apply(BlockIds::Sandstone, tiles(BlockIds::Sandstone),
          glm::vec3(0.85f, 0.78f, 0.58f), Layer::Opaque, true);
    apply(BlockIds::Snow, tiles(BlockIds::Snow),
          glm::vec3(0.94f, 0.95f, 0.97f), Layer::Opaque, true);
    apply(BlockIds::Ice, tiles(BlockIds::Ice),
          glm::vec3(0.62f, 0.80f, 0.92f), Layer::Opaque, true);
    apply(BlockIds::Gravel, tiles(BlockIds::Gravel),
          glm::vec3(0.47f, 0.45f, 0.43f), Layer::Opaque, true);
    apply(BlockIds::Bedrock, tiles(BlockIds::Bedrock),
          glm::vec3(0.24f, 0.24f, 0.26f), Layer::Opaque, true);
}

} // namespace Terrain
