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
    const int columns = BlockAtlas::Columns;
    const int size = BlockAtlas::TileSize;
    const int col = tileIndex % columns;
    const int row = tileIndex / columns;
    std::uint64_t hash = 1469598103934665603ULL;
    for (int y = 0; y < size; ++y) {
        const std::size_t offset =
            (static_cast<std::size_t>(row * size + y) * BlockAtlas::Width
             + static_cast<std::size_t>(col * size)) * 4u;
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

    WritePpm("atlas.ppm");
    std::printf("wrote atlas.ppm (%dx%d)\n", BlockAtlas::Width, BlockAtlas::Height);

    std::printf("%s\n", failures == 0 ? "block atlas test OK" : "block atlas test FAILED");
    return failures == 0 ? 0 : 1;
}
