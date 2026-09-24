// Block edit / overlay / boundary remesh verification:
//  - Break (Stone -> Air) updates the live ChunkBlocks and bumps its revision
//  - a boundary break also bumps the adjacent chunk's revision (mesh dirty)
//  - user edits survive an unload/reload (edit overlay re-applied on generate)
//  - negative world / chunk / local coordinates
//  - the mesher uses the live neighbour snapshot, so a boundary break exposes
//    the neighbour's previously hidden face

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>

#include <glm/glm.hpp>

#include "engine/ECS/Scene/scene.h"
#include "Terrain/Public/terrain_components.h"
#include "Terrain/Public/terrain_edit.h"
#include "Terrain/Public/terrain_generator.h"
#include "Terrain/Public/terrain_mesher.h"

using namespace Terrain;

namespace {

bool Expect(bool condition, const char* what) {
    std::printf("  %-56s %s\n", what, condition ? "ok" : "FAIL");
    return condition;
}

void FillStone(ChunkBlocks& blocks, int yMax) {
    for (int y = 0; y < yMax; ++y) {
        for (int z = 0; z < SectionSize; ++z) {
            for (int x = 0; x < SectionSize; ++x) {
                blocks.Set(x, y, z, static_cast<BlockId>(Block::Stone));
            }
        }
    }
}

std::uint32_t LayerIndices(const ChunkMesh& mesh, std::size_t layer) {
    return static_cast<std::uint32_t>(mesh.layers[layer].indices.size());
}

} // namespace

int main() {
    int failures = 0;

    RegisterTerrainComponents();
    ECS::Core::Scene scene;
    auto description = scene.CreateArchTypeDescription();
    description->AddComponentArray<ChunkLocation>();
    description->AddComponentArray<ChunkBlocks>();
    description->AddComponentArray<ChunkMesh>();
    description->AddComponentArray<ChunkRender>();
    auto archetype = scene.CreateArchType(description, 16);

    TerrainWorld world;
    world.SetScene(&scene);

    const auto makeChunk = [&](int cx, int cz) {
        auto handle = scene.CreateEntity(archetype);
        auto location = scene.GetActiveComponent<ChunkLocation>(handle.GetID());
        if (auto* component = location.Get()) {
            component->section = glm::ivec3(cx, 0, cz);
        }
        auto blocks = scene.GetActiveComponent<ChunkBlocks>(handle.GetID());
        if (auto* component = blocks.Get()) {
            component->generated = true;
        }
        world.RegisterChunk(glm::ivec3(cx, 0, cz), handle.GetID());
        return handle.GetID();
    };

    const ECS::EntityID entityA = makeChunk(0, 0);
    const ECS::EntityID entityB = makeChunk(1, 0);

    ChunkBlocks* blocksA = scene.GetActiveComponent<ChunkBlocks>(entityA).Get();
    ChunkBlocks* blocksB = scene.GetActiveComponent<ChunkBlocks>(entityB).Get();
    FillStone(*blocksA, 8);
    FillStone(*blocksB, 8);
    blocksA->revision = 100;  // deterministic starting point for assertions
    blocksB->revision = 200;

    // --- Interior break: only the owning chunk is dirtied -------------------
    {
        const BlockEditResult result = world.SetBlockWorld(
            glm::ivec3(7, 3, 4), static_cast<BlockId>(Block::Air));
        failures += !Expect(result.foundChunk && result.changed, "interior break changes live chunk");
        failures += !Expect(blocksA->Get(7, 3, 4) == 0, "interior break writes Air");
        failures += !Expect(blocksA->revision == 101, "interior break bumps revision once");
        failures += !Expect(result.dirtyChunks == 1, "interior break dirties only the chunk");
        failures += !Expect(blocksB->revision == 200, "interior break leaves neighbour revision");
        failures += !Expect(world.GetBlockWorld(7, 3, 4) == 0, "GetBlockWorld sees the edit");
        failures += !Expect(world.GetBlockWorld(6, 3, 4) != 0, "GetBlockWorld reads live solids");
    }

    // --- Boundary break: owning chunk + neighbour are dirtied ---------------
    {
        const BlockEditResult result = world.SetBlockWorld(
            glm::ivec3(15, 3, 4), static_cast<BlockId>(Block::Air));
        failures += !Expect(result.changed && result.dirtyChunks == 2,
                            "boundary break dirties self + neighbour");
        failures += !Expect(blocksA->revision == 102, "boundary break bumps self revision");
        failures += !Expect(blocksB->revision == 201, "boundary break bumps neighbour revision");
    }

    // --- Unload / reload override persistence -------------------------------
    {
        // The edit is in the overlay already. Simulate the generator producing a
        // fresh stone chunk, then committing it (main-thread ApplyEdits).
        ChunkBlocks regenerated;
        FillStone(regenerated, 8);
        failures += !Expect(regenerated.Get(15, 3, 4) != 0,
                            "freshly generated chunk is solid (pre-apply)");
        const std::size_t applied = world.Edits().Apply(glm::ivec3(0, 0, 0), regenerated);
        failures += !Expect(applied >= 1, "edit overlay re-applied on reload");
        failures += !Expect(regenerated.Get(15, 3, 4) == 0, "reloaded chunk keeps broken block");
        failures += !Expect(regenerated.Get(7, 3, 4) == 0, "reloaded chunk keeps all edits");
        failures += !Expect(world.Edits().EditCount() == 2, "overlay stores both edits");
    }

    // --- Negative coordinates ----------------------------------------------
    {
        failures += !Expect(WorldToChunk(-1, 0) == glm::ivec2(-1, 0),
                            "WorldToChunk(-1,0) == (-1,0)");
        failures += !Expect(WorldToChunk(-17, -16) == glm::ivec2(-2, -1),
                            "WorldToChunk(-17,-16) == (-2,-1)");
        failures += !Expect(WorldToLocal(-1, 5, -17) == glm::ivec3(15, 5, 15),
                            "WorldToLocal(-1,5,-17) == (15,5,15)");

        makeChunk(-1, 0);
        ChunkBlocks* blocksNeg = world.FindBlocks(glm::ivec3(-1, 0, 0));
        failures += !Expect(blocksNeg != nullptr, "negative chunk registered");
        FillStone(*blocksNeg, 8);
        // World x=-1 -> local x=15 in chunk -1; z=5 keeps us in chunk z=0.
        world.SetBlockWorld(glm::ivec3(-1, 3, 5), static_cast<BlockId>(Block::Air));
        failures += !Expect(world.GetBlockWorld(-1, 3, 5) == 0,
                            "negative-coordinate break reaches the right local block");
        failures += !Expect(world.GetBlockWorld(-2, 3, 5) != 0,
                            "negative-coordinate neighbour still solid");
    }

    // --- Mesher sees the live neighbour (boundary face appears) -------------
    {
        BlockRenderRegistry registry;
        BlockRenderInfo stone{};
        stone.visible = true;
        stone.occludes = true;
        stone.layer = Layer::Opaque;
        stone.color.fill(glm::vec3(0.5f, 0.5f, 0.5f));
        registry.Set(static_cast<BlockId>(Block::Stone), stone);

        // Restore A's boundary block so the before/after comparison is clean.
        world.SetBlockWorld(glm::ivec3(15, 3, 4), static_cast<BlockId>(Block::Stone));

        ChunkNeighborhood before;
        world.CollectNeighborhood(glm::ivec3(1, 0, 0), before);
        ChunkMesh meshBefore;
        TerrainMesher::Build(*blocksB, glm::ivec3(1, 0, 0), registry, meshBefore,
                             nullptr, &before);
        const std::uint32_t indicesBefore = LayerIndices(meshBefore, 0);

        // Break the boundary block in A; B's -X face must now be emitted.
        const auto breakStart = std::chrono::steady_clock::now();
        world.SetBlockWorld(glm::ivec3(15, 3, 4), static_cast<BlockId>(Block::Air));

        ChunkNeighborhood after;
        world.CollectNeighborhood(glm::ivec3(1, 0, 0), after);
        ChunkMesh meshAfter;
        TerrainMesher::Build(*blocksB, glm::ivec3(1, 0, 0), registry, meshAfter,
                             nullptr, &after);
        const double remeshMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - breakStart).count();
        const std::uint32_t indicesAfter = LayerIndices(meshAfter, 0);

        std::printf("  neighbour mesh indices: before=%u after=%u\n",
                    indicesBefore, indicesAfter);
        std::printf("  break -> CPU mesh latency: %.3f ms (one chunk, debug build)\n",
                    remeshMs);
        failures += !Expect(indicesAfter == indicesBefore + 6,
                            "boundary break exposes the neighbour's hidden face");
    }

    std::printf("%s\n", failures == 0 ? "interaction test OK" : "interaction test FAILED");
    return failures == 0 ? 0 : 1;
}
