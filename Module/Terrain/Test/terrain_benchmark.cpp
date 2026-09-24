// Task 0.1 benchmark: establishes the ECS / Terrain performance baseline.
//
// Headless: runs the exact same 8-system ECS pipeline as terrain_render_demo
// (Streaming -> Generation -> Meshing -> MeshUpload -> RenderExtract -> Publish
// -> RenderPipeline) without a window, so the numbers are the CPU ECS/Terrain
// cost, not GPU cost. The mesh upload queue copies vertex/index bytes like the
// real RHI queue, so MeshUploadSystem cost is representative.
//
// Scenario: viewer flies along +X at a constant speed so new chunks stream in
// every frame. After a warm-up that fills the initial load radius, the profiler
// is reset and a fixed number of steady-state frames is measured.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#include "engine/ECS/Core/Kernel/kernel.h"
#include "engine/ECS/Profiling/profiler.h"
#include "engine/ECS/Query/query.h"
#include "engine/ECS/Scene/scene.h"
#include "engine/ECS/System/system_pipeline.h"

#include "Render/Public/Pipeline/mesh_upload_queue.h"
#include "Render/Public/Pipeline/render_pipeline.h"
#include "Render/Public/RHICommand/FrameCommand/rhi_frame_command_buffer.h"
#include "Render/Systems/render_pipeline_system.h"

#include "Terrain/Public/terrain_components.h"
#include "Terrain/Public/terrain_edit.h"
#include "Terrain/Public/terrain_generator.h"
#include "Terrain/Systems/terrain_systems.h"

namespace {

using TerrainChunkQuery = ECS::Core::ChunkQuery<
    ECS::Core::Require<Terrain::ChunkLocation, Terrain::ChunkBlocks,
                       Terrain::ChunkMesh, Terrain::ChunkRender>,
    ECS::Core::Optional<>, ECS::Core::Exclude<>>;

// Mirrors the CPU-side work of Render::RHIMeshUploadQueue (copy into the command)
// without needing a live GL device.
class CopyingUploader final : public Render::IMeshUploadQueue {
public:
    void Create(const Render::CreateMeshBufferDesc& desc, CreateCallback callback) override {
        Copy(desc.vertexData, desc.vertexByteSize);
        Copy(desc.indexData, desc.indexByteSize);
        callback({nextId_++, 0});
    }

    void Update(Render::RenderResourceHandle<Render::VertexBufferSpec>,
                const Render::UpdateMeshBufferDesc& desc,
                UpdateCallback callback) override {
        Copy(desc.vertexData, desc.vertexByteSize);
        Copy(desc.indexData, desc.indexByteSize);
        callback(true);
    }

    void Destroy(Render::RenderResourceHandle<Render::VertexBufferSpec>) override {}

private:
    void Copy(const void* data, std::size_t bytes) {
        if (data == nullptr || bytes == 0) return;
        storage_.resize(bytes);
        std::memcpy(storage_.data(), data, bytes);
    }

    std::vector<std::byte> storage_;
    std::uint32_t nextId_ = 10;
};

std::size_t CountChunks(ECS::Core::Scene& scene) {
    TerrainChunkQuery query;
    query.Refresh(scene);
    std::size_t count = 0;
    for (auto view : query) count += view.count;
    return count;
}

std::size_t CountGenerated(ECS::Core::Scene& scene) {
    TerrainChunkQuery query;
    query.Refresh(scene);
    std::size_t count = 0;
    for (auto view : query) {
        auto* blocks = view.Get<Terrain::ChunkBlocks>();
        if (blocks == nullptr) continue;
        for (std::size_t i = 0; i < view.count; ++i) {
            if (blocks[i].generated) ++count;
        }
    }
    return count;
}

std::size_t CountMeshed(ECS::Core::Scene& scene) {
    TerrainChunkQuery query;
    query.Refresh(scene);
    std::size_t count = 0;
    for (auto view : query) {
        auto* blocks = view.Get<Terrain::ChunkBlocks>();
        auto* meshes = view.Get<Terrain::ChunkMesh>();
        if (blocks == nullptr || meshes == nullptr) continue;
        for (std::size_t i = 0; i < view.count; ++i) {
            if (blocks[i].generated && meshes[i].sourceRevision == blocks[i].revision) {
                ++count;
            }
        }
    }
    return count;
}

} // namespace

int main(int argc, char** argv) {
    constexpr int kMeasuredFrames = 360;
    constexpr int kWarmupFrames = 90;
    constexpr float kViewerSpeedPerFrame = 8.0f; // ~0.5 chunk per frame
    constexpr std::size_t kEntitiesPerChunk = 16;

    // argv[1] optionally selects the worker count. Absent / 0 / "auto" -> policy.
    std::size_t requestedWorkers = 0;
    if (argc > 1) {
        requestedWorkers = static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10));
    }

    ECS::Core::ECSKernel kernel;
    kernel.Init(requestedWorkers);

    ECS::Core::JobSystemSchedule* schedule = ECS::Core::globalECSCoreContext.jobSystemSchedule;
    const unsigned hardwareThreads = std::thread::hardware_concurrency();
    const std::size_t resolvedWorkers =
        ECS::Core::JobSystemSchedule::ResolveWorkerCount(requestedWorkers);
    const std::size_t actualWorkers = schedule != nullptr ? schedule->GetWorkerCount() : 0;

    // Generator mode: argv[2] or TERRAIN_MODE ("density" default, "heightmap").
    // TERRAIN_SEED optionally overrides the world seed.
    const char* modeArg = argc > 2 ? argv[2] : std::getenv("TERRAIN_MODE");
    const bool heightmapMode = modeArg != nullptr
        && (std::strcmp(modeArg, "heightmap") == 0
            || std::strcmp(modeArg, "height") == 0);
    Terrain::SetTerrainGeneratorMode(heightmapMode
        ? Terrain::TerrainGeneratorMode::Heightmap
        : Terrain::TerrainGeneratorMode::DensityField);
    if (const char* seed = std::getenv("TERRAIN_SEED")) {
        Terrain::SetWorldSeed(std::strtoull(seed, nullptr, 10));
    }

    Terrain::RegisterTerrainComponents();

    ECS::Core::Scene scene;

    Terrain::TerrainWorld terrainWorld;
    terrainWorld.SetScene(&scene);

    auto chunkDescription = scene.CreateArchTypeDescription();
    chunkDescription->AddComponentArray<Terrain::ChunkLocation>();
    chunkDescription->AddComponentArray<Terrain::ChunkBlocks>();
    chunkDescription->AddComponentArray<Terrain::ChunkMesh>();
    chunkDescription->AddComponentArray<Terrain::ChunkRender>();
    auto chunkArchetype = scene.CreateArchType(chunkDescription, kEntitiesPerChunk);

    auto viewerDescription = scene.CreateArchTypeDescription();
    viewerDescription->AddComponentArray<Terrain::TerrainViewer>();
    auto viewerArchetype = scene.CreateArchType(viewerDescription, 1);
    const auto viewerEntity = scene.CreateEntity(viewerArchetype);

    glm::vec3 viewerPosition{8.0f, 21.0f, 28.0f};
    {
        auto viewer = scene.GetActiveComponent<Terrain::TerrainViewer>(viewerEntity.GetID());
        if (auto* component = viewer.Get()) component->position = viewerPosition;
    }

    Terrain::BlockRenderRegistry blockRegistry;
    Terrain::RegisterDefaultTerrainBlocks(blockRegistry);

    CopyingUploader uploader;
    Render::RenderWorld renderWorld;
    Render::RHIFrameCommandBufferPool commandPool;

    Terrain::System::RenderSettings terrainSettings{};
    terrainSettings.pipelines[0] = {2, 0};
    terrainSettings.pipelines[2] = {4, 0};

    ECS::System::Context context;
    context.scene = &scene;
    context.SetService(&blockRegistry);
    context.SetService<Render::IMeshUploadQueue>(&uploader);
    context.SetService(&renderWorld);
    context.SetService(&terrainSettings);
    context.SetService(&terrainWorld);

    ECS::System::Pipeline pipeline;
    pipeline.Add<Terrain::System::StreamingSystem>(chunkArchetype);
    pipeline.Add<Terrain::System::GenerationSystem>();
    pipeline.Add<Terrain::System::MeshingSystem>();
    pipeline.Add<Terrain::System::MeshUploadSystem>();
    pipeline.Add<Render::System::RenderExtractBeginSystem>();
    pipeline.Add<Terrain::System::RenderExtractSystem>();
    pipeline.Add<Render::System::RenderPublishSystem>();
    pipeline.Add<Render::System::RenderPipelineSystem>();
    pipeline.RunBefore<Terrain::System::StreamingSystem, Terrain::System::GenerationSystem>();
    pipeline.RunBefore<Terrain::System::GenerationSystem, Terrain::System::MeshingSystem>();
    pipeline.RunBefore<Render::System::RenderExtractBeginSystem, Terrain::System::RenderExtractSystem>();
    pipeline.RunBefore<Terrain::System::RenderExtractSystem, Render::System::RenderPublishSystem>();

    std::printf("scenario: headless ECS terrain pipeline\n");
    std::printf("  hardware threads: %u\n", hardwareThreads);
    std::printf("  configured workers: %zu (requested %zu, 0 = auto)\n",
                resolvedWorkers, requestedWorkers);
    std::printf("  actual workers: %zu\n", actualWorkers);
    std::printf("  generator mode: %s\n", heightmapMode ? "heightmap" : "density");
    std::printf("  archetype entities/chunk: %zu, load radius: 6, measured frames: %d\n",
                kEntitiesPerChunk, kMeasuredFrames);
    std::printf("  viewer speed: %.1f units/frame, warmup frames: %d\n",
                kViewerSpeedPerFrame, kWarmupFrames);

    auto tickFrame = [&](std::uint64_t frameIndex) {
        auto buffer = commandPool.threadAny_GetBuffer();
        Render::RHIFrameEncoder encoder(buffer);
        encoder.Begin({.frameIndex = frameIndex});

        Render::RenderView renderView{};
        Render::RenderFrameService frameService{&encoder, renderView};

        context.frameIndex = frameIndex;
        context.SetService(&frameService);

        ECS::Profiling::Profiler::Get().BeginFrame();
        {
#if ECS_PROFILING_ENABLED
            ECS::Profiling::ScopedTimer total("ECS total");
#endif
            pipeline.Tick(context);
        }
        ECS::Profiling::Profiler::Get().EndFrame();

        encoder.End(false);
        commandPool.threadAny_Recycle(buffer);
    };

    std::uint64_t frameIndex = 1;

    // Warm-up: fill the initial load radius and let uploads settle.
    for (int i = 0; i < kWarmupFrames; ++i) {
        tickFrame(frameIndex++);
    }
    const std::size_t warmupChunks = CountChunks(scene);
    ECS::Profiling::Profiler::Get().Reset();

    // Steady-state measurement while continuously entering new territory.
    const auto measuredStart = std::chrono::steady_clock::now();
    for (int i = 0; i < kMeasuredFrames; ++i) {
        viewerPosition.x += kViewerSpeedPerFrame;
        auto viewer = scene.GetActiveComponent<Terrain::TerrainViewer>(viewerEntity.GetID());
        if (auto* component = viewer.Get()) component->position = viewerPosition;

        tickFrame(frameIndex++);
    }
    const double measuredSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - measuredStart).count();

    const std::size_t finalChunks = CountChunks(scene);
    const std::size_t profiledFrames = ECS::Profiling::Profiler::Get().FrameCount();
    const auto genCommittedStats =
        ECS::Profiling::Profiler::Get().Stats("Chunks generated (committed)");
    const auto meshCommittedStats =
        ECS::Profiling::Profiler::Get().Stats("Meshes rebuilt");
    const double generatedCommitted =
        genCommittedStats.average * static_cast<double>(genCommittedStats.samples);
    const double meshedCommitted =
        meshCommittedStats.average * static_cast<double>(meshCommittedStats.samples);

    std::printf("\nwarmup chunks: %zu, final chunks: %zu\n", warmupChunks, finalChunks);
    if (schedule != nullptr) {
        std::printf("workers used (peak concurrent): %zu / %zu\n",
                    schedule->GetPeakActiveWorkers(), schedule->GetWorkerCount());
    }

    ECS::Profiling::Profiler::Get().Report(
        "Terrain ECS baseline - steady flight (times in ms, counts are raw)");

    // Async generation: schedule/completion/in-flight. The main thread never
    // waits for a batch, so total minus build stays close to scheduling cost.
    {
        const auto& profiler = ECS::Profiling::Profiler::Get();
        std::printf("\nGenerationSystem (avg per frame):\n");
        std::printf("  total            %8.4f ms\n",
                    profiler.Stats("TerrainGenerationSystem").average);
        std::printf("  build (worker)   %8.4f ms\n",
                    profiler.Stats("Generation build").average);
        std::printf("  scheduled/frame  %8.2f\n", profiler.Stats("Chunks generated").average);
        std::printf("  committed/frame  %8.2f\n",
                    profiler.Stats("Chunks generated (committed)").average);
        std::printf("  in flight (avg)  %8.2f\n",
                    profiler.Stats("Generation in flight").average);
    }

    // Task 2.0: MeshingSystem cost decomposition.
    {
        const auto& profiler = ECS::Profiling::Profiler::Get();
        const double chunksPerFrame = profiler.Stats("Mesh chunks").average;
        const double buildPerFrame = profiler.Stats("Meshing build").average;
        const double total = profiler.Stats("TerrainMeshingSystem").average;
        const double wait = profiler.Stats("Meshing wait").average;

        std::printf("\nMeshingSystem (avg per frame):\n");
        std::printf("  total          %8.4f ms\n", total);
        std::printf("  dispatch       %8.4f ms\n", profiler.Stats("Meshing dispatch").average);
        std::printf("  wait           %8.4f ms (async: expected ~0)\n", wait);
        std::printf("  build (worker) %8.4f ms\n", buildPerFrame);
        std::printf("  main-thread    %8.4f ms (total - wait)\n", total - wait);
        std::printf("  jobs submitted %8.2f\n", profiler.Stats("Meshing jobs").average);
        std::printf("  scheduled/frame%8.2f, committed/frame %8.2f\n",
                    profiler.Stats("Mesh jobs scheduled").average,
                    profiler.Stats("Meshes rebuilt").average);
        std::printf("  in flight (avg)%8.2f\n", profiler.Stats("Meshing in flight").average);

        if (chunksPerFrame > 0.0) {
            std::printf("Per chunk (avg):\n");
            std::printf("  build          %8.4f ms\n", buildPerFrame / chunksPerFrame);
            std::printf("  vertices       %8.1f\n",
                        profiler.Stats("Mesh verts").average / chunksPerFrame);
            std::printf("  indices        %8.1f\n",
                        profiler.Stats("Mesh indices").average / chunksPerFrame);
        }

        std::printf("Sample chunks:\n");
        for (const auto& sample : Terrain::System::MeshingProbeSamples()) {
            std::printf("  chunk (%d,%d): build %7.4f ms, verts %6zu, indices %6zu\n",
                        sample.sectionX, sample.sectionZ, sample.buildMs,
                        sample.vertexCount, sample.indexCount);
        }
    }

    // Task 2.3: TerrainMesher::Build internal breakdown.
    {
        const auto& profiler = ECS::Profiling::Profiler::Get();
        const double chunksPerFrame = profiler.Stats("Mesh chunks").average;
        const double totalMs = profiler.Stats("Mesh build total").average;
        const double iteration = profiler.Stats("Mesh iteration").average;
        const double cacheBuild = profiler.Stats("Mesh cache build").average;
        const double sampling = profiler.Stats("Mesh sampling (oob)").average;
        const double faceTest = profiler.Stats("Mesh face test").average;
        const double emit = profiler.Stats("Mesh EmitFace").average;

        const auto perChunk = [chunksPerFrame](double v) {
            return chunksPerFrame > 0.0 ? v / chunksPerFrame : 0.0;
        };

        std::printf("\nTerrainMesher Build (avg per frame / per chunk):\n");
        std::printf("  total          %8.4f / %8.4f ms\n", totalMs, perChunk(totalMs));
        std::printf("  iteration      %8.4f / %8.4f ms\n", iteration, perChunk(iteration));
        std::printf("  cache build    %8.4f / %8.4f ms\n", cacheBuild, perChunk(cacheBuild));
        std::printf("  sampling (oob) %8.4f / %8.4f ms\n", sampling, perChunk(sampling));
        std::printf("  face test      %8.4f / %8.4f ms\n", faceTest, perChunk(faceTest));
        std::printf("  EmitFace       %8.4f / %8.4f ms\n", emit, perChunk(emit));
        std::printf("  (allocation is inside EmitFace; Task 2.2 A-B realloc ~3-10%% of Build)\n");

        std::printf("Faces (avg per chunk):\n");
        std::printf("  checked        %8.1f\n", perChunk(profiler.Stats("Mesh face checks").average));
        std::printf("  visible        %8.1f\n", perChunk(profiler.Stats("Mesh visible faces").average));
        std::printf("  emitted        %8.1f\n", perChunk(profiler.Stats("Mesh visible faces").average));
        std::printf("  blocks         %8.1f visited, %8.1f skipped\n",
                    perChunk(profiler.Stats("Mesh blocks visited").average),
                    perChunk(profiler.Stats("Mesh invisible blocks").average));
        std::printf("  samples        %8.1f total, %8.1f oob\n",
                    perChunk(profiler.Stats("Mesh neighbor samples").average),
                    perChunk(profiler.Stats("Mesh oob samples").average));
        std::printf("  pushes         %8.1f verts, %8.1f indices\n",
                    perChunk(profiler.Stats("Mesh vertex pushes").average),
                    perChunk(profiler.Stats("Mesh index pushes").average));
        std::printf("  reallocs       %8.2f verts, %8.2f indices\n",
                    perChunk(profiler.Stats("Mesh vertex reallocs").average),
                    perChunk(profiler.Stats("Mesh index reallocs").average));

        // Task 2.5: EmitFace internals.
        const double emitVertex = profiler.Stats("Mesh Emit vertex").average;
        const double emitIndex = profiler.Stats("Mesh Emit index").average;
        const double emittedFaces = profiler.Stats("Mesh emitted faces").average;
        const double verticesPushed = profiler.Stats("Mesh vertex pushes").average;
        const double indicesPushed = profiler.Stats("Mesh index pushes").average;
        std::printf("EmitFace breakdown (avg per frame / per chunk):\n");
        std::printf("  EmitFace       %8.4f / %8.4f ms\n", emit, perChunk(emit));
        std::printf("  vertex loop    %8.4f / %8.4f ms (%.1f%% of EmitFace)\n",
                    emitVertex, perChunk(emitVertex),
                    emit > 0.0 ? emitVertex / emit * 100.0 : 0.0);
        std::printf("  index loop     %8.4f / %8.4f ms (%.1f%% of EmitFace)\n",
                    emitIndex, perChunk(emitIndex),
                    emit > 0.0 ? emitIndex / emit * 100.0 : 0.0);
        std::printf("  faces          %8.1f /chunk\n", perChunk(emittedFaces));
        std::printf("  vertex pushes  %8.1f /chunk (%.1f per face, Vertex temporaries)\n",
                    perChunk(verticesPushed),
                    emittedFaces > 0.0 ? verticesPushed / emittedFaces : 0.0);
        std::printf("  index pushes   %8.1f /chunk (%.1f per face)\n",
                    perChunk(indicesPushed),
                    emittedFaces > 0.0 ? indicesPushed / emittedFaces : 0.0);
    }

    // Async pipeline flush: the measured loop is not frame-rate limited, so
    // workers may still be catching up. Drain the backlog (this is an explicit
    // benchmark sync point, not part of the realtime tick) and report the
    // aggregate streaming throughput over the whole run.
    auto* generationSystem = pipeline.Get<Terrain::System::GenerationSystem>();
    auto* meshingSystem = pipeline.Get<Terrain::System::MeshingSystem>();
    const auto flushStart = std::chrono::steady_clock::now();
    std::size_t flushFrames = 0;
    constexpr std::size_t kMaxFlushFrames = 500000;
    for (; flushFrames < kMaxFlushFrames; ++flushFrames) {
        const std::size_t total = CountChunks(scene);
        const bool complete = CountGenerated(scene) >= total
            && CountMeshed(scene) >= total
            && !(generationSystem != nullptr && generationSystem->HasPendingWork())
            && !(meshingSystem != nullptr && meshingSystem->HasPendingWork());
        if (complete) break;
        if (schedule != nullptr) schedule->WaitIdle();
        tickFrame(frameIndex++);
    }
    if (schedule != nullptr) schedule->WaitIdle();

    const double flushSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - flushStart).count();
    const std::size_t flushedGenerated = CountGenerated(scene);
    const std::size_t flushedMeshed = CountMeshed(scene);

    std::printf("\nAsync flush (explicit sync, benchmark-only; not in realtime tick):\n");
    std::printf("  measurement wall:  %8.3f s, %.0f fps (%zu frames)\n",
                measuredSeconds,
                measuredSeconds > 0.0
                    ? static_cast<double>(profiledFrames) / measuredSeconds : 0.0,
                profiledFrames);
    std::printf("  committed in window:%7.0f generated, %.0f meshed\n",
                generatedCommitted, meshedCommitted);
    if (measuredSeconds > 0.0) {
        std::printf("  generation rate:   %8.0f chunks/s (window)\n",
                    generatedCommitted / measuredSeconds);
    }
    std::printf("  flush:             %8zu frames, %.3f s\n", flushFrames, flushSeconds);
    std::printf("  final generated:   %8zu / %zu loaded chunks\n",
                flushedGenerated, CountChunks(scene));
    std::printf("  final meshed:      %8zu / %zu loaded chunks\n",
                flushedMeshed, CountChunks(scene));

    pipeline.Stop(context);
    return 0;
}
