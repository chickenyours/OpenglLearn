// Async remesh correctness for block edits (Terrain interaction phase):
//  - a mesh job already in flight is discarded when the chunk's blocks change
//    (revision mismatch), so a stale mesh can never overwrite a fresh break
//  - the chunk is rescheduled and the next committed mesh reflects the edit
//
// Runs the real MeshingSystem through the ECS pipeline. With no worker pool
// configured it dispatches inline, which is enough to exercise the token /
// revision bookkeeping deterministically.

#include <cstdint>
#include <cstdio>

#include <glm/glm.hpp>

#include "engine/ECS/Scene/scene.h"
#include "engine/ECS/System/system_pipeline.h"
#include "Terrain/Public/terrain_components.h"
#include "Terrain/Public/terrain_edit.h"
#include "Terrain/Public/terrain_generator.h"
#include "Terrain/Systems/terrain_systems.h"

using namespace Terrain;

namespace {

bool Expect(bool condition, const char* what) {
    std::printf("  %-56s %s\n", what, condition ? "ok" : "FAIL");
    return condition;
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
    auto archetype = scene.CreateArchType(description, 8);
    const auto entity = scene.CreateEntity(archetype);

    auto location = scene.GetActiveComponent<ChunkLocation>(entity.GetID());
    location.Get()->section = glm::ivec3(0, 0, 0);
    auto blocksHandle = scene.GetActiveComponent<ChunkBlocks>(entity.GetID());
    ChunkBlocks* blocks = blocksHandle.Get();
    for (int y = 0; y < 4; ++y) {
        for (int z = 0; z < SectionSize; ++z) {
            for (int x = 0; x < SectionSize; ++x) {
                blocks->Set(x, y, z, static_cast<BlockId>(Block::Stone));
            }
        }
    }
    blocks->generated = true;

    TerrainWorld world;
    world.SetScene(&scene);
    world.RegisterChunk(glm::ivec3(0, 0, 0), entity.GetID());

    BlockRenderRegistry registry;
    BlockRenderInfo stone{};
    stone.visible = true;
    stone.occludes = true;
    stone.layer = Layer::Opaque;
    stone.color.fill(glm::vec3(0.5f));
    registry.Set(static_cast<BlockId>(Block::Stone), stone);

    ECS::System::Context context;
    context.scene = &scene;
    context.SetService(&registry);
    context.SetService(&world);

    ECS::System::Pipeline pipeline;
    pipeline.Add<Terrain::System::MeshingSystem>();

    auto tick = [&](std::uint64_t frame) {
        context.frameIndex = frame;
        pipeline.Tick(context);
    };

    // Tick 1 schedules + (inline) builds the initial mesh and queues it.
    // Tick 2 commits it, giving a baseline for the comparison below.
    tick(1);
    tick(2);
    ChunkMesh* mesh = scene.GetActiveComponent<ChunkMesh>(entity.GetID()).Get();
    const std::uint32_t indicesBefore =
        static_cast<std::uint32_t>(mesh->layers[0].indices.size());
    const std::uint64_t committedRevision = mesh->sourceRevision;
    failures += !Expect(indicesBefore > 0, "baseline mesh committed");

    // Break an interior block and schedule a remesh (R2) on tick 3.
    world.SetBlockWorld(glm::ivec3(5, 1, 1), static_cast<BlockId>(Block::Air));
    tick(3);

    // Break a second block before R2 is committed, making R2 stale.
    world.SetBlockWorld(glm::ivec3(5, 2, 1), static_cast<BlockId>(Block::Air));

    // Tick 4: the in-flight R2 must be discarded, not committed.
    tick(4);
    failures += !Expect(mesh->sourceRevision == committedRevision,
                        "stale in-flight mesh discarded (mesh unchanged)");
    failures += !Expect(mesh->sourceRevision != blocks->revision,
                        "discarded result left the newer revision pending");
    failures += !Expect(mesh->meshPending,
                        "discarded result rescheduled a fresh mesh job");

    // Tick 5 commits the mesh rebuilt from both edits.
    tick(5);
    const std::uint32_t indicesAfter = static_cast<std::uint32_t>(mesh->layers[0].indices.size());
    std::printf("  interior break mesh indices: before=%u after=%u\n",
                indicesBefore, indicesAfter);
    failures += !Expect(mesh->sourceRevision == blocks->revision,
                        "fresh mesh committed after reschedule");
    failures += !Expect(indicesAfter > indicesBefore,
                        "fresh mesh reflects the broken blocks (new faces)");

    pipeline.Stop(context);
    std::printf("%s\n", failures == 0 ? "async edit test OK" : "async edit test FAILED");
    return failures == 0 ? 0 : 1;
}
