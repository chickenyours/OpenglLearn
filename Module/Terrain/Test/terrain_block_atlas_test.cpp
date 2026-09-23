// Phase 1 Terrain Visual Foundation verification:
//  - atlas image is well-formed, deterministic and each tile is distinct
//  - block -> top/side/bottom mapping (grass, sand, sandstone, snow, ice, ...)
//  - surface themes (Desert = Sand/Sandstone, Snow = Snow/Ice)
//  - ApplyDefaultBlockAtlas fills the legacy tile[6] array correctly
//  - TerrainMesher output structure is unchanged (only tile values differ)
//  - writes atlas.ppm for visual inspection

#include <cstdint>
#include <cstdio>
#include <vector>

#include "Terrain/Public/block_atlas.h"
#include "Terrain/Public/terrain_generator.h"
#include "Terrain/Public/terrain_mesher.h"

using namespace Terrain;

namespace {

std::uint64_t FnvBytes(std::uint64_t hash, const void* data, std::size_t bytes) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < bytes; ++i) {
        hash ^= p[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::uint64_t TileChecksum(int tileIndex) {
    const auto& pixels = BlockAtlas::Pixels();
    const int size = BlockAtlas::TileSize;
    int originX = 0;
    int originY = 0;
    BlockAtlas::TilePixelOrigin(static_cast<BlockTile>(tileIndex), originX, originY);
    std::uint64_t hash = 1469598103934665603ULL;
    for (int y = 0; y < size; ++y) {
        const std::size_t offset =
            (static_cast<std::size_t>(originY + y) * BlockAtlas::Width
             + static_cast<std::size_t>(originX)) * 4u;
        hash = FnvBytes(hash, pixels.data() + offset, static_cast<std::size_t>(size) * 4u);
    }
    return hash;
}

void WritePpm(const char* path) {
    const auto& pixels = BlockAtlas::Pixels();
    std::FILE* file = std::fopen(path, "wb");
    if (file == nullptr) return;
    std::fprintf(file, "P6\n%d %d\n255\n", BlockAtlas::Width, BlockAtlas::Height);
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        const std::uint8_t rgb[3] = {pixels[i + 0], pixels[i + 1], pixels[i + 2]};
        std::fwrite(rgb, 1, 3, file);
    }
    std::fclose(file);
}

bool Expect(bool condition, const char* what) {
    std::printf("  %-52s %s\n", what, condition ? "ok" : "FAIL");
    return condition;
}

} // namespace

int main() {
    int failures = 0;

    // ---- Atlas image --------------------------------------------------------
    const auto& pixels = BlockAtlas::Pixels();
    const std::size_t expectedBytes =
        static_cast<std::size_t>(BlockAtlas::Width) * BlockAtlas::Height * 4u;
    std::printf("atlas: %dx%d, %d tiles, %zu bytes\n",
                BlockAtlas::Width, BlockAtlas::Height, BlockAtlas::TileCount(),
                pixels.size());
    failures += !Expect(pixels.size() == expectedBytes, "size == width*height*4");
    failures += !Expect(BlockAtlas::Pixels().data() == pixels.data(), "deterministic (same buffer)");

    std::vector<std::uint64_t> tileHashes;
    for (int i = 0; i < BlockAtlas::TileCount(); ++i) tileHashes.push_back(TileChecksum(i));
    bool allDistinct = true;
    for (std::size_t i = 0; i < tileHashes.size(); ++i)
        for (std::size_t j = i + 1; j < tileHashes.size(); ++j)
            if (tileHashes[i] == tileHashes[j]) allDistinct = false;
    failures += !Expect(allDistinct, "all tiles distinct");

    // ---- Mapping ------------------------------------------------------------
    const auto tile = [](BlockTile t) { return static_cast<std::uint16_t>(t); };
    const BlockFaceTiles grass = BlockAtlas::TilesFor(static_cast<BlockId>(Block::Grass));
    const BlockFaceTiles sand = BlockAtlas::TilesFor(static_cast<BlockId>(Block::Sand));
    const BlockFaceTiles sandstone = BlockAtlas::TilesFor(BlockIds::Sandstone);
    const BlockFaceTiles snow = BlockAtlas::TilesFor(BlockIds::Snow);
    const BlockFaceTiles ice = BlockAtlas::TilesFor(BlockIds::Ice);

    failures += !Expect(grass.top == tile(BlockTile::GrassTop)
                        && grass.side == tile(BlockTile::GrassSide)
                        && grass.bottom == tile(BlockTile::Dirt),
                        "grass top=GrassTop side=GrassSide bottom=Dirt");
    failures += !Expect(sand.top == tile(BlockTile::Sand)
                        && sand.side == tile(BlockTile::Sand)
                        && sand.bottom == tile(BlockTile::Sand), "sand all Sand");
    failures += !Expect(sandstone.top == tile(BlockTile::SandstoneTop)
                        && sandstone.side == tile(BlockTile::SandstoneSide)
                        && sandstone.bottom == tile(BlockTile::SandstoneBottom),
                        "sandstone top/side/bottom");
    failures += !Expect(snow.top == tile(BlockTile::Snow)
                        && snow.side == tile(BlockTile::SnowSide), "snow top=Snow side=SnowSide");
    failures += !Expect(ice.top == tile(BlockTile::Ice)
                        && ice.side == tile(BlockTile::Ice), "ice all Ice");

    // ---- Surface themes -----------------------------------------------------
    const SurfacePalette desert = SurfaceForTheme(SurfaceTheme::Desert);
    const SurfacePalette snowy = SurfaceForTheme(SurfaceTheme::Snow);
    const SurfacePalette temperate = SurfaceForTheme(SurfaceTheme::Temperate);
    failures += !Expect(desert.top == static_cast<BlockId>(Block::Sand)
                        && desert.filler == BlockIds::Sandstone, "Desert = Sand / Sandstone");
    failures += !Expect(snowy.top == BlockIds::Snow
                        && snowy.filler == BlockIds::Ice, "Snow = Snow / Ice");
    failures += !Expect(temperate.top == static_cast<BlockId>(Block::Grass)
                        && temperate.filler == static_cast<BlockId>(Block::Dirt),
                        "Temperate = Grass / Dirt");

    // ---- Registry integration ----------------------------------------------
    BlockRenderRegistry plain;
    RegisterDefaultTerrainBlocks(plain);
    BlockRenderRegistry atlas;
    RegisterDefaultTerrainBlocks(atlas);
    ApplyDefaultBlockAtlas(atlas);

    const BlockRenderInfo& grassInfo = atlas.Get(static_cast<BlockId>(Block::Grass));
    const BlockRenderInfo& gravelInfo = atlas.Get(BlockIds::Gravel);
    failures += !Expect(grassInfo.tile[static_cast<std::size_t>(Face::PositiveY)] == tile(BlockTile::GrassTop),
                        "registry grass +Y == GrassTop");
    failures += !Expect(grassInfo.tile[static_cast<std::size_t>(Face::NegativeX)] == tile(BlockTile::GrassSide)
                        && grassInfo.tile[static_cast<std::size_t>(Face::PositiveZ)] == tile(BlockTile::GrassSide),
                        "registry grass sides == GrassSide");
    failures += !Expect(grassInfo.tile[static_cast<std::size_t>(Face::NegativeY)] == tile(BlockTile::Dirt),
                        "registry grass -Y == Dirt");
    failures += !Expect(grassInfo.color[static_cast<std::size_t>(Face::PositiveY)] == glm::vec3(1.0f),
                        "atlas sets neutral white tint");
    failures += !Expect(gravelInfo.visible && gravelInfo.occludes, "new gravel block registered");

    // ---- Mesher output unchanged (structure) -------------------------------
    ChunkBlocks blocks;
    GenerateChunkBlocks(glm::ivec3(0, 0, 0), blocks); // default heightmap mode

    ChunkMesh meshPlain;
    ChunkMesh meshAtlas;
    TerrainMesher::Build(blocks, glm::ivec3(0, 0, 0), plain, meshPlain);
    TerrainMesher::Build(blocks, glm::ivec3(0, 0, 0), atlas, meshAtlas);

    bool structureSame = true;
    long tileDifferences = 0;
    long uvDifferences = 0;
    long colorDifferences = 0;
    for (std::size_t l = 0; l < 3 && structureSame; ++l) {
        const CpuSubmesh& a = meshPlain.layers[l];
        const CpuSubmesh& b = meshAtlas.layers[l];
        if (a.vertices.size() != b.vertices.size() || a.indices != b.indices) {
            structureSame = false;
            break;
        }
        for (std::size_t i = 0; i < a.vertices.size(); ++i) {
            if (a.vertices[i].position != b.vertices[i].position
                || a.vertices[i].normal != b.vertices[i].normal) {
                structureSame = false;
                break;
            }
            if (a.vertices[i].tile != b.vertices[i].tile) ++tileDifferences;
            if (a.vertices[i].uv != b.vertices[i].uv) ++uvDifferences;
            if (a.vertices[i].color != b.vertices[i].color) ++colorDifferences;
        }
    }
    failures += !Expect(structureSame, "mesher geometry/order/indices unchanged");
    failures += !Expect(tileDifferences > 0, "atlas tiles flow into the mesh");
    failures += !Expect(uvDifferences > 0, "atlas UVs generated on the CPU");
    failures += !Expect(colorDifferences > 0, "atlas applies neutral tint");

    // ---- Face UV orientation (Task Texture-2) ------------------------------
    const auto sampleFace = [](BlockTile tile, float s, float t) -> glm::vec3 {
        float u = 0.0f, v = 0.0f;
        BlockAtlas::FaceCornerUv(tile, s, t, u, v);
        const auto& px = BlockAtlas::Pixels();
        const int x = std::clamp(static_cast<int>(u * BlockAtlas::Width), 0, BlockAtlas::Width - 1);
        const int y = std::clamp(static_cast<int>(v * BlockAtlas::Height), 0, BlockAtlas::Height - 1);
        const std::size_t o = (static_cast<std::size_t>(y) * BlockAtlas::Width + x) * 4u;
        return glm::vec3(px[o + 0], px[o + 1], px[o + 2]);
    };
    const auto sampleFaceUnflipped = [](BlockTile tile, float s, float t) -> glm::vec3 {
        float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
        BlockAtlas::TileTexelRect(tile, u0, v0, u1, v1);
        const float u = (s > 0.5f) ? u1 : u0;
        const float v = (t > 0.5f) ? v1 : v0;
        const auto& px = BlockAtlas::Pixels();
        const int x = std::clamp(static_cast<int>(u * BlockAtlas::Width), 0, BlockAtlas::Width - 1);
        const int y = std::clamp(static_cast<int>(v * BlockAtlas::Height), 0, BlockAtlas::Height - 1);
        const std::size_t o = (static_cast<std::size_t>(y) * BlockAtlas::Width + x) * 4u;
        return glm::vec3(px[o + 0], px[o + 1], px[o + 2]);
    };

    glm::vec3 sideTop(0.0f);
    glm::vec3 sideBottom(0.0f);
    for (float s = 0.05f; s < 1.0f; s += 0.1f) {
        sideTop += sampleFace(BlockTile::GrassSide, s, 0.9f);
        sideBottom += sampleFace(BlockTile::GrassSide, s, 0.1f);
    }
    failures += !Expect(sideTop.g > sideTop.r && sideTop.g > sideTop.b,
                        "grass side: green section on top");
    failures += !Expect(sideBottom.r > sideBottom.g, "grass side: dirt section below");

    const glm::vec3 grassTopPixel = sampleFace(BlockTile::GrassTop, 0.5f, 0.5f);
    const glm::vec3 grassBottomPixel = sampleFace(BlockTile::Dirt, 0.5f, 0.5f);
    failures += !Expect(grassTopPixel.g > grassTopPixel.r, "grass top is green");
    failures += !Expect(grassBottomPixel.r > grassBottomPixel.g, "grass bottom is dirt");

    const glm::vec3 buggyTop = sampleFaceUnflipped(BlockTile::GrassSide, 0.5f, 0.9f);
    failures += !Expect(buggyTop.r > buggyTop.g,
                        "unflipped V mapping is indeed inverted (regression guard)");

    // ---- Gutter padding / mip bleed (Task Texture-3) -----------------------
    std::printf("atlas layout: cell=%d centre=%d gutter=%d atlas=%dx%d\n",
                BlockAtlas::PaddedTileSize, BlockAtlas::TileSize, BlockAtlas::Gutter,
                BlockAtlas::Width, BlockAtlas::Height);

    int gutterMismatches = 0;
    int bleedMismatches = 0;
    const auto& atlasPixels = BlockAtlas::Pixels();
    for (int t = 0; t < BlockAtlas::TileCount(); ++t) {
        int ox = 0;
        int oy = 0;
        BlockAtlas::TilePixelOrigin(static_cast<BlockTile>(t), ox, oy);
        const int cellX = (t % BlockAtlas::Columns) * BlockAtlas::PaddedTileSize;
        const int cellY = (t / BlockAtlas::Columns) * BlockAtlas::PaddedTileSize;

        // The gutter must duplicate the tile's edge texels.
        for (int i = 0; i < BlockAtlas::TileSize; ++i) {
            const auto* leftCenter = atlasPixels.data() + ((oy + i) * BlockAtlas::Width + ox) * 4;
            const auto* leftGutter = atlasPixels.data() + ((oy + i) * BlockAtlas::Width + (ox - 1)) * 4;
            const auto* rightCenter = atlasPixels.data() + ((oy + i) * BlockAtlas::Width + (ox + BlockAtlas::TileSize - 1)) * 4;
            const auto* rightGutter = atlasPixels.data() + ((oy + i) * BlockAtlas::Width + (ox + BlockAtlas::TileSize)) * 4;
            const auto* topCenter = atlasPixels.data() + (oy * BlockAtlas::Width + (ox + i)) * 4;
            const auto* topGutter = atlasPixels.data() + ((oy - 1) * BlockAtlas::Width + (ox + i)) * 4;
            const auto* bottomCenter = atlasPixels.data() + ((oy + BlockAtlas::TileSize - 1) * BlockAtlas::Width + (ox + i)) * 4;
            const auto* bottomGutter = atlasPixels.data() + ((oy + BlockAtlas::TileSize) * BlockAtlas::Width + (ox + i)) * 4;
            for (int c = 0; c < 4; ++c) {
                if (leftGutter[c] != leftCenter[c] || rightGutter[c] != rightCenter[c]
                    || topGutter[c] != topCenter[c] || bottomGutter[c] != bottomCenter[c]) {
                    ++gutterMismatches;
                }
            }
        }

        // Every mip 1 / mip 2 block that contains a sampled (centre) texel must lie
        // entirely inside this tile's padded cell, so it cannot average a neighbour.
        for (int level = 1; level <= 2; ++level) {
            const int block = 1 << level;
            const int xs[2] = {ox, ox + BlockAtlas::TileSize - 1};
            const int ys[2] = {oy, oy + BlockAtlas::TileSize - 1};
            for (int k = 0; k < 2; ++k) {
                const int bx = (xs[k] / block) * block;
                const int by = (ys[k] / block) * block;
                if (bx < cellX || bx + block > cellX + BlockAtlas::PaddedTileSize) ++bleedMismatches;
                if (by < cellY || by + block > cellY + BlockAtlas::PaddedTileSize) ++bleedMismatches;
            }
        }
    }
    failures += !Expect(gutterMismatches == 0, "gutter duplicates tile edge texels");
    failures += !Expect(bleedMismatches == 0, "mip1/mip2 blocks stay inside the tile cell");

    // CPU mip chain sanity: the mip2 texel containing Dirt's right edge centre must
    // stay brown (not average in the grey Stone tile next to it).
    {
        const int W = BlockAtlas::Width;
        const int H = BlockAtlas::Height;
        std::vector<std::uint8_t> mip = atlasPixels;
        for (int level = 1; level <= 2; ++level) {
            const int w = W >> level;
            const int h = H >> level;
            std::vector<std::uint8_t> next(static_cast<std::size_t>(w) * h * 4u, 0);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    int sum[4] = {0, 0, 0, 0};
                    for (int dy = 0; dy < 2; ++dy) {
                        for (int dx = 0; dx < 2; ++dx) {
                            const std::size_t s =
                                (static_cast<std::size_t>((y * 2 + dy) * (W >> (level - 1)) + (x * 2 + dx))) * 4u;
                            for (int c = 0; c < 4; ++c) sum[c] += mip[s + c];
                        }
                    }
                    const std::size_t d = (static_cast<std::size_t>(y) * w + x) * 4u;
                    for (int c = 0; c < 4; ++c) next[d + c] = static_cast<std::uint8_t>(sum[c] / 4);
                }
            }
            mip.swap(next);
        }
        int dirtOx = 0;
        int dirtOy = 0;
        BlockAtlas::TilePixelOrigin(BlockTile::Dirt, dirtOx, dirtOy);
        const int edgeX = (dirtOx + BlockAtlas::TileSize - 1) >> 2;
        const int edgeY = dirtOy >> 2;
        const int W2 = W >> 2;
        const std::size_t o = (static_cast<std::size_t>(edgeY) * W2 + edgeX) * 4u;
        const glm::vec3 mip2Edge(mip[o], mip[o + 1], mip[o + 2]);
        failures += !Expect(mip2Edge.r > mip2Edge.b && mip2Edge.r > mip2Edge.g,
                            "mip2 tile edge stays dirt (no Stone contamination)");
    }

    // CPU-rendered face preview: grass top | grass side | grass bottom | sandstone side.
    {
        // Continuous face sampling (what the GPU does by interpolating the 4
        // corner UVs between the tile's texel-centre rect and the V flip).
        const auto sampleFaceInterp = [](BlockTile tile, float s, float t) -> glm::vec3 {
            float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
            BlockAtlas::TileTexelRect(tile, u0, v0, u1, v1);
            const float u = u0 + s * (u1 - u0);
            const float v = v1 + t * (v0 - v1); // t=1 (top) -> v0, t=0 -> v1
            const auto& px = BlockAtlas::Pixels();
            const int x = std::clamp(static_cast<int>(u * BlockAtlas::Width), 0, BlockAtlas::Width - 1);
            const int y = std::clamp(static_cast<int>(v * BlockAtlas::Height), 0, BlockAtlas::Height - 1);
            const std::size_t o = (static_cast<std::size_t>(y) * BlockAtlas::Width + x) * 4u;
            return glm::vec3(px[o + 0], px[o + 1], px[o + 2]);
        };

        const int size = BlockAtlas::TileSize;
        const int panels = 4;
        const int width = size * panels;
        std::vector<std::uint8_t> image(
            static_cast<std::size_t>(width) * size * 3u, 0);
        const BlockTile tiles[4] = {
            BlockTile::GrassTop, BlockTile::GrassSide, BlockTile::Dirt, BlockTile::SandstoneSide};
        for (int p = 0; p < panels; ++p) {
            for (int y = 0; y < size; ++y) {
                const float t = 1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
                for (int x = 0; x < size; ++x) {
                    const float s = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
                    const glm::vec3 c = sampleFaceInterp(tiles[p], s, t);
                    const std::size_t o =
                        (static_cast<std::size_t>(y) * width + p * size + x) * 3u;
                    image[o + 0] = static_cast<std::uint8_t>(c.r);
                    image[o + 1] = static_cast<std::uint8_t>(c.g);
                    image[o + 2] = static_cast<std::uint8_t>(c.b);
                }
            }
        }
        std::FILE* file = std::fopen("block_preview.ppm", "wb");
        if (file != nullptr) {
            std::fprintf(file, "P6\n%d %d\n255\n", width, size);
            std::fwrite(image.data(), 1, image.size(), file);
            std::fclose(file);
        }
    }

    WritePpm("atlas.ppm");
    std::printf("wrote atlas.ppm (%dx%d)\n", BlockAtlas::Width, BlockAtlas::Height);

    std::printf("%s\n", failures == 0 ? "block atlas test OK" : "block atlas test FAILED");
    return failures == 0 ? 0 : 1;
}
