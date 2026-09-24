#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/ECS/JobSystem/job_system.h"
#include "engine/ECS/Profiling/profiler.h"
#include "engine/ECS/Query/query.h"
#include "engine/ECS/System/system.h"
#include "Render/Public/Pipeline/mesh_upload_queue.h"
#include "Render/Public/Pipeline/render_world.h"
#include "Render/Public/Pipeline/render_pipeline.h"
#include "Render/Public/Pipeline/view_frustum.h"
#include "Terrain/Public/terrain_components.h"
#include "Terrain/Public/terrain_edit.h"
#include "Terrain/Public/terrain_generator.h"
#include "Terrain/Public/terrain_mesher.h"

namespace Terrain::System {

using ECS::EntityID;

// Task 2.0: per-chunk meshing probe. Each job carries its own record in its
// result; the main thread aggregates them at commit time, so no shared profiler
// state is touched by workers and no lock/atomic is needed.
struct MeshChunkProbe {
    int sectionX = 0;
    int sectionZ = 0;
    double buildMs = 0.0;
    std::size_t vertexCount = 0;
    std::size_t indexCount = 0;
    TerrainMeshStats mesh;   // Task 2.3 per-Build breakdown
};

// Bounded sample of the most recently meshed chunks, for reporting only.
// Main-thread access; never written by workers directly.
inline std::vector<MeshChunkProbe>& MeshingProbeSamples() {
    static std::vector<MeshChunkProbe> samples;
    return samples;
}

using TerrainQuery = ECS::Core::ChunkQuery<
    ECS::Core::Require<ChunkLocation, ChunkBlocks, ChunkMesh, ChunkRender>,
    ECS::Core::Optional<>, ECS::Core::Exclude<>>;

using ViewerQuery = ECS::Core::ChunkQuery<
    ECS::Core::Require<TerrainViewer>,
    ECS::Core::Optional<>, ECS::Core::Exclude<>>;

// A small thread-safe one-way result queue. Workers push; the main thread drains
// once per tick without waiting for workers. The queue outlives
// every job (systems WaitIdle in OnEnd before they are destroyed).
template <typename T>
class WorkCompletionQueue {
public:
    void Push(T&& value) {
        auto item = std::make_unique<T>(std::move(value));
        std::lock_guard<std::mutex> lock(mutex_);
        items_.push_back(std::move(item));
    }
    bool TryPop(T& out) {
        std::unique_ptr<T> item;
        {
            std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
            if(!lock.owns_lock() || items_.empty()) return false;
            item = std::move(items_.front());
            items_.pop_front();
        }
        // Large block copies and old mesh destruction happen outside the lock.
        out = std::move(*item);
        return true;
    }
    bool Empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return items_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::deque<std::unique_ptr<T>> items_;
};

// A finished generation job. `blocks` is worker-owned until the main thread
// commits it; the worker never touches ECS storage.
struct GeneratedChunkResult {
    EntityID entity = 0;
    glm::ivec3 section{0};
    std::uint64_t token = 0;
    ChunkBlocks blocks{};
#if ECS_PROFILING_ENABLED
    double buildMs = 0.0;
#endif
};

// A finished meshing job. The worker built `mesh` from an independent snapshot.
struct MeshedChunkResult {
    EntityID entity = 0;
    glm::ivec3 section{0};
    std::uint64_t token = 0;
    std::uint64_t sourceRevision = 0;
    ChunkMesh mesh{};
#if ECS_PROFILING_ENABLED
    double buildMs = 0.0;
    TerrainMeshStats stats{};
#endif
};

// Submits a batch of jobs and dispatches them WITHOUT waiting. The main thread
// returns immediately; workers deliver their results through each system's
// completion queue and the next tick commits them. Falls back to inline
// execution when no worker pool is configured (e.g. the headless test), in which
// case results still flow through the same queue. `stage` labels the profiling
// counters and does not affect behavior.
inline void DispatchJobs(ECS::Core::Scene* scene,
                         std::vector<ECS::Core::ExecuteTask>& jobs,
                         const char* stage) {
    if(jobs.empty()) return;

#if ECS_PROFILING_ENABLED
    auto& profiler = ECS::Profiling::Profiler::Get();
    profiler.Count("Jobs submitted", static_cast<double>(jobs.size()));
    profiler.Count(std::string(stage) + " jobs", static_cast<double>(jobs.size()));
#endif

    if(scene != nullptr) {
        if(auto* jobSystem = scene->GetJobSystem()) {
            if(jobSystem->GetSchedule() != nullptr) {
                for(auto& job : jobs) jobSystem->Submit(std::move(job));
#if ECS_PROFILING_ENABLED
                const auto dispatchStart = std::chrono::steady_clock::now();
#endif
                const std::size_t dispatched = jobSystem->DispatchAll();
                (void)dispatched;
#if ECS_PROFILING_ENABLED
                profiler.Accumulate(std::string(stage) + " dispatch",
                    std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - dispatchStart).count());
                profiler.Count("Jobs dispatched", static_cast<double>(dispatched));
                profiler.Count(std::string(stage) + " dispatched",
                               static_cast<double>(dispatched));
#endif
                // Deliberately no WaitIdle(): the async pipeline commits results
                // as they arrive instead of stalling the main thread.
                return;
            }
        }
    }

    for(auto& job : jobs) job();
}

// Bound snapshot copying and queued terrain work even with a large worker pool.
// The worker pool remains shared with other scene systems. 1 when inline.
inline std::size_t TerrainWorkerCount(ECS::Core::Scene* scene) {
    if(scene != nullptr) {
        if(auto* jobSystem = scene->GetJobSystem()) {
            if(auto* schedule = jobSystem->GetSchedule()) {
                return std::clamp<std::size_t>(schedule->GetWorkerCount(), 1, 4);
            }
        }
    }
    return 1;
}

// Viewer position used to prioritise the nearest chunks. Zero when no viewer.
inline glm::vec3 TerrainViewerCenter(ECS::Core::Scene& scene, ViewerQuery& query) {
    glm::vec3 center{0.0f};
    query.RefreshIfNeeded(scene);
    for(auto view : query) {
        auto* viewers = view.Get<TerrainViewer>();
        if(viewers != nullptr && view.count > 0) {
            center = viewers[0].position;
            break;
        }
    }
    return center;
}

struct IVec2Hash {
    std::size_t operator()(const glm::ivec2& v) const noexcept {
        std::uint64_t x = static_cast<std::uint32_t>(v.x);
        std::uint64_t y = static_cast<std::uint32_t>(v.y);
        std::uint64_t h = (x << 32u) ^ y;
        h ^= h >> 33u;
        h *= 0xff51afd7ed558ccdULL;
        h ^= h >> 33u;
        return static_cast<std::size_t>(h);
    }
};

// Called while the uploader service is still alive, before removing ECS state.
// A late create callback observes cancellation and retires its new handle.
inline void ReleaseChunkRender(ChunkRender& render, Render::IMeshUploadQueue& uploader) {
    for(auto& gpu : render.layers) {
        Render::RenderResourceHandle<Render::VertexBufferSpec> pendingHandle;
        if(auto pending = gpu.pending) {
            std::lock_guard<std::mutex> lock(pending->mutex);
            pending->cancelled = true;
            if(pending->done && !pending->updating) {
                pendingHandle = pending->handle;
                pending->handle = {};
            }
        }
        if(pendingHandle.IsValid() && pendingHandle != gpu.handle) uploader.Destroy(pendingHandle);
        if(gpu.handle.IsValid()) uploader.Destroy(gpu.handle);
        gpu = GpuSubmesh{};
    }
}

// Creates and destroys chunk entities around the viewer. Entity lifetime is a
// structural change, so this is the only terrain system that must stay on the
// main thread.
class StreamingSystem final : public ECS::System::System {
public:
    StreamingSystem(ObjectWeakPtr<ECS::Core::ArchType> archtype)
        : ECS::System::System("TerrainStreamingSystem", ECS::System::Phase::Update),
          chunkArchtype_(std::move(archtype)) {
        Reads<TerrainViewer>(); Writes<ChunkLocation>();
    }

    void OnTick() override {
        auto* context = GetContext();
        if(context == nullptr || context->scene == nullptr) return;
        auto* scene = context->scene;
        auto* world = context->GetService<TerrainWorld>();

        glm::vec3 center{0.0f};
        bool hasViewer = false;
        viewerQuery_.RefreshIfNeeded(*scene);
        for(auto view : viewerQuery_) {
            auto* viewers = view.Get<TerrainViewer>();
            if(viewers != nullptr && view.count > 0) {
                center = viewers[0].position;
                hasViewer = true;
                break;
            }
        }
        if(!hasViewer) return;

        const glm::ivec2 centerChunk{
            static_cast<int>(std::floor(center.x / static_cast<float>(SectionSize))),
            static_cast<int>(std::floor(center.z / static_cast<float>(SectionSize)))};

        constexpr int keepRadius = kLoadRadius + 2;

        chunkQuery_.RefreshIfNeeded(*scene);

        struct StaleChunk { EntityID entity; glm::ivec3 section; };
        std::unordered_set<glm::ivec2, IVec2Hash> active;
        std::vector<StaleChunk> stale;
        for(auto view : chunkQuery_) {
            auto* locations = view.Get<ChunkLocation>();
            if(locations == nullptr) continue;
            const auto& entities = view.archType->GetIndexEntities();
            for(std::size_t i = 0; i < view.count; ++i) {
                const glm::ivec3 section = locations[i].section;
                const glm::ivec2 coord{section.x, section.z};
                active.insert(coord);

                // Keep the world lookup registry in sync with entity lifetime.
                if(world != nullptr) {
                    world->RegisterChunk(section, entities[view.beginIndex + i]);
                }

                const int distance = std::max(
                    std::abs(coord.x - centerChunk.x),
                    std::abs(coord.y - centerChunk.y));
                if(distance > keepRadius) {
                    stale.push_back(StaleChunk{entities[view.beginIndex + i], section});
                }
            }
        }

        std::size_t removed = 0;
        for(const StaleChunk& entry : stale) {
            if(removed++ >= kMaxChunkCreationsPerFrame) break;
            if(auto* uploader = context->GetService<Render::IMeshUploadQueue>()) {
                if(auto* render = scene->GetActiveComponent<ChunkRender>(entry.entity).Get())
                    ReleaseChunkRender(*render, *uploader);
            }
            if(world != nullptr) world->UnregisterChunk(entry.section);
            scene->DeleteEntity(ECS::EntityHandle(entry.entity));
        }

        struct Wanted { glm::ivec2 coord; int distance; };
        std::vector<Wanted> wanted;
        for(int dz = -kLoadRadius; dz <= kLoadRadius; ++dz) {
            for(int dx = -kLoadRadius; dx <= kLoadRadius; ++dx) {
                const glm::ivec2 coord{centerChunk.x + dx, centerChunk.y + dz};
                if(active.find(coord) != active.end()) continue;
                wanted.push_back(Wanted{coord, dx * dx + dz * dz});
            }
        }
        std::sort(wanted.begin(), wanted.end(), [](const Wanted& a, const Wanted& b) {
            return a.distance < b.distance;
        });

        int created = 0;
        for(const Wanted& candidate : wanted) {
            if(created >= kMaxChunkCreationsPerFrame) break;
            if(chunkArchtype_ == nullptr) break;

            const auto handle = scene->CreateEntity(chunkArchtype_);
            if(handle.GetID() == ECS::INVALID_ENTITY) continue;

            auto location = scene->GetActiveComponent<ChunkLocation>(handle.GetID());
            if(auto* component = location.Get()) {
                component->section = glm::ivec3(candidate.coord.x, 0, candidate.coord.y);
            }
            ++created;
        }

#if ECS_PROFILING_ENABLED
        ECS::Profiling::Profiler::Get().Count(
            "Chunks streamed", static_cast<double>(created));
#endif
    }

private:
    static constexpr int kLoadRadius = 6;
    static constexpr int kMaxChunkCreationsPerFrame = 8;

    ObjectWeakPtr<ECS::Core::ArchType> chunkArchtype_;
    ViewerQuery viewerQuery_;
    TerrainQuery chunkQuery_;
};

// Fills ChunkBlocks for chunks that have not been generated yet. Generation runs
// asynchronously: workers fill an independent result that is delivered through a
// completion queue, and only the main thread ever writes ECS storage.
class GenerationSystem final : public ECS::System::System {
public:
    GenerationSystem()
        : ECS::System::System("TerrainGenerationSystem", ECS::System::Phase::Update) {
        Reads<ChunkLocation>(); Writes<ChunkBlocks>();
    }

    void OnTick() override {
        auto* context = GetContext();
        if(context == nullptr || context->scene == nullptr) return;
        auto* scene = context->scene;
        auto* world = context->GetService<TerrainWorld>();

        query_.RefreshIfNeeded(*scene);
        CommitResults(*scene, world);
        ScheduleJobs(*scene);
    }

    void OnEnd() override {
        // No worker may still reference this system's queue after it is gone.
        if(auto* context = GetContext()) {
            if(context->scene != nullptr) {
                if(auto* jobSystem = context->scene->GetJobSystem()) {
                    if(jobSystem->GetSchedule() != nullptr) jobSystem->WaitIdle();
                }
            }
        }
        GeneratedChunkResult discard;
        while(generationQueue_.TryPop(discard)) {}
    }

    // Test / benchmark hook: true while generation work is queued or running.
    bool HasPendingWork() const {
        return generationInFlight_ > 0 || !generationQueue_.Empty();
    }

private:
    struct Candidate {
        EntityID entity = 0;
        glm::ivec3 section{0};
        ChunkBlocks* blocks = nullptr;
        int distance = 0;
    };

    static int ChunkDistance(const glm::ivec3& section, int centerX, int centerZ) {
        const int dx = section.x - centerX;
        const int dz = section.z - centerZ;
        return dx * dx + dz * dz;
    }

    void CommitResults(ECS::Core::Scene& scene, TerrainWorld* world) {
        GeneratedChunkResult result;
        for(std::size_t n = 0; n < 4 && generationQueue_.TryPop(result); ++n) {
            if(generationInFlight_ > 0) --generationInFlight_;

            auto blocksHandle = scene.GetActiveComponent<ChunkBlocks>(result.entity);
            if(blocksHandle.owner == 0) continue;                 // entity unloaded
            ChunkBlocks* blocks = blocksHandle.Get();
            if(blocks == nullptr) continue;
            if(blocks->generationToken != result.token) continue; // stale result

            *blocks = std::move(result.blocks);
            blocks->generationToken = result.token;
            blocks->generationPending = false;

            // User edits win over the procedural result. Applied here on the main
            // thread so no worker ever touches the edit map, and so a chunk that
            // is unloaded and later reloaded keeps the player's changes.
            if(world != nullptr) {
                const std::size_t applied = world->Edits().Apply(result.section, *blocks);
                if(applied > 0) {
                    world->MarkNeighborsDirty(result.section);
                }
            }

#if ECS_PROFILING_ENABLED
            auto& profiler = ECS::Profiling::Profiler::Get();
            profiler.Count("Chunks generated (committed)", 1.0);
            profiler.Accumulate("Generation build", result.buildMs);
#endif
        }
    }

    void ScheduleJobs(ECS::Core::Scene& scene) {
        const std::size_t workers = TerrainWorkerCount(&scene);
        const std::size_t inFlightLimit = std::max<std::size_t>(2, workers * 2);
        if(generationInFlight_ >= inFlightLimit) return;
        const std::size_t budget = std::max<std::size_t>(1, workers);

        const glm::vec3 center = TerrainViewerCenter(scene, viewerQuery_);
        const int centerChunkX =
            static_cast<int>(std::floor(center.x / static_cast<float>(SectionSize)));
        const int centerChunkZ =
            static_cast<int>(std::floor(center.z / static_cast<float>(SectionSize)));

        std::vector<Candidate> candidates;
        for(auto view : query_) {
            auto* blocks = view.Get<ChunkBlocks>();
            auto* locations = view.Get<ChunkLocation>();
            if(blocks == nullptr || locations == nullptr || view.count == 0) continue;
            const auto& entities = view.archType->GetIndexEntities();

            for(std::size_t i = 0; i < view.count; ++i) {
                if(blocks[i].generated || blocks[i].generationPending) continue;
                candidates.push_back(Candidate{
                    entities[view.beginIndex + i], locations[i].section, &blocks[i],
                    ChunkDistance(locations[i].section, centerChunkX, centerChunkZ)});
            }
        }
        if(candidates.empty()) return;

        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate& a, const Candidate& b) {
                      return a.distance < b.distance;
                  });

        const std::size_t remaining = inFlightLimit - generationInFlight_;
        const std::size_t count = std::min({candidates.size(), budget, remaining});

        std::vector<ECS::Core::ExecuteTask> jobs;
        jobs.reserve(count);
        for(std::size_t i = 0; i < count; ++i) {
            ChunkBlocks* blocks = candidates[i].blocks;
            const glm::ivec3 section = candidates[i].section;
            const EntityID entity = candidates[i].entity;

            blocks->generationPending = true;
            blocks->generationToken = ++generationTokenCounter_;
            const std::uint64_t token = blocks->generationToken;

            WorkCompletionQueue<GeneratedChunkResult>* queue = &generationQueue_;
            jobs.emplace_back([section, entity, token, queue]() {
                GeneratedChunkResult result;
                result.entity = entity;
                result.section = section;
                result.token = token;
#if ECS_PROFILING_ENABLED
                const auto buildStart = std::chrono::steady_clock::now();
                GenerateChunkBlocks(section, result.blocks);
                result.buildMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - buildStart).count();
#else
                GenerateChunkBlocks(section, result.blocks);
#endif
                queue->Push(std::move(result));
            });
            ++generationInFlight_;
        }

#if ECS_PROFILING_ENABLED
        auto& profiler = ECS::Profiling::Profiler::Get();
        profiler.Count("Chunks generated", static_cast<double>(count));
        profiler.Count("Generation in flight", static_cast<double>(generationInFlight_));
#endif
        DispatchJobs(&scene, jobs, "Generation");
    }

    TerrainQuery query_;
    ViewerQuery viewerQuery_;
    WorkCompletionQueue<GeneratedChunkResult> generationQueue_;
    std::uint64_t generationTokenCounter_ = 0;
    std::size_t generationInFlight_ = 0;
};

#if ECS_PROFILING_ENABLED
// Aggregates one frame of per-chunk probes. Runs on the main thread over results
// popped from the completion queue, which provides the happens-before edge for
// the worker writes.
inline void AggregateMeshingProbes(const std::deque<MeshChunkProbe>& probes) {
    if(probes.empty()) return;

    auto& profiler = ECS::Profiling::Profiler::Get();
    double totalBuildMs = 0.0;
    std::size_t totalVertices = 0;
    std::size_t totalIndices = 0;
    TerrainMeshStats mesh{};
    for(const MeshChunkProbe& probe : probes) {
        totalBuildMs += probe.buildMs;
        totalVertices += probe.vertexCount;
        totalIndices += probe.indexCount;

        const TerrainMeshStats& s = probe.mesh;
        mesh.totalMs += s.totalMs;
        mesh.cacheMs += s.cacheMs;
        mesh.sampleMs += s.sampleMs;
        mesh.faceLoopMs += s.faceLoopMs;
        mesh.emitMs += s.emitMs;
        mesh.emitVertexMs += s.emitVertexMs;
        mesh.emitIndexMs += s.emitIndexMs;
        mesh.blocksVisited += s.blocksVisited;
        mesh.invisibleBlocks += s.invisibleBlocks;
        mesh.faceChecks += s.faceChecks;
        mesh.neighborSamples += s.neighborSamples;
        mesh.outOfBoundsSamples += s.outOfBoundsSamples;
        mesh.visibleFaces += s.visibleFaces;
        mesh.emittedFaces += s.emittedFaces;
        mesh.vertexPushes += s.vertexPushes;
        mesh.indexPushes += s.indexPushes;
        mesh.vertexReallocs += s.vertexReallocs;
        mesh.indexReallocs += s.indexReallocs;
    }

    profiler.Accumulate("Meshing build", totalBuildMs);
    profiler.Count("Mesh chunks", static_cast<double>(probes.size()));
    profiler.Count("Mesh verts", static_cast<double>(totalVertices));
    profiler.Count("Mesh indices", static_cast<double>(totalIndices));

    // Task 2.3 decomposition. faceLoop covers sampling + culling + emit; peel the
    // measured out-of-bounds sampling and EmitFace off it to get the face test,
    // and peel the whole face loop off the total to get the block iteration.
    const double faceTestMs = mesh.faceLoopMs - mesh.emitMs - mesh.sampleMs;
    const double iterationMs = mesh.totalMs - mesh.cacheMs - mesh.faceLoopMs;

    profiler.Accumulate("Mesh build total", mesh.totalMs);
    profiler.Accumulate("Mesh cache build", mesh.cacheMs);
    profiler.Accumulate("Mesh iteration", iterationMs);
    profiler.Accumulate("Mesh sampling (oob)", mesh.sampleMs);
    profiler.Accumulate("Mesh face test", faceTestMs);
    profiler.Accumulate("Mesh EmitFace", mesh.emitMs);
    profiler.Accumulate("Mesh Emit vertex", mesh.emitVertexMs);
    profiler.Accumulate("Mesh Emit index", mesh.emitIndexMs);

    profiler.Count("Mesh blocks visited", static_cast<double>(mesh.blocksVisited));
    profiler.Count("Mesh invisible blocks", static_cast<double>(mesh.invisibleBlocks));
    profiler.Count("Mesh face checks", static_cast<double>(mesh.faceChecks));
    profiler.Count("Mesh neighbor samples", static_cast<double>(mesh.neighborSamples));
    profiler.Count("Mesh oob samples", static_cast<double>(mesh.outOfBoundsSamples));
    profiler.Count("Mesh visible faces", static_cast<double>(mesh.visibleFaces));
    profiler.Count("Mesh emitted faces", static_cast<double>(mesh.emittedFaces));
    profiler.Count("Mesh vertex pushes", static_cast<double>(mesh.vertexPushes));
    profiler.Count("Mesh index pushes", static_cast<double>(mesh.indexPushes));
    profiler.Count("Mesh vertex reallocs", static_cast<double>(mesh.vertexReallocs));
    profiler.Count("Mesh index reallocs", static_cast<double>(mesh.indexReallocs));

    auto& samples = MeshingProbeSamples();
    for(const MeshChunkProbe& probe : probes) {
        if(samples.size() >= 8) break;
        samples.push_back(probe);
    }
}
#endif

// Rebuilds CPU meshes for chunks whose blocks changed. Meshing runs
// asynchronously: the scheduling step snapshots the chunk's blocks so the worker
// never dereferences ECS storage, and the resulting CPU mesh is committed on the
// main thread. Neighbouring blocks across the chunk border come from the
// deterministic generator, so the snapshot plus generator equals the old result.
class MeshingSystem final : public ECS::System::System {
public:
    MeshingSystem()
        : ECS::System::System("TerrainMeshingSystem", ECS::System::Phase::Update) {
        Reads<ChunkBlocks>(); Writes<ChunkMesh>();
    }

    void OnTick() override {
        auto* context = GetContext();
        auto* registry = context ? context->GetService<BlockRenderRegistry>() : nullptr;
        auto* world = context ? context->GetService<TerrainWorld>() : nullptr;
        if(context == nullptr || context->scene == nullptr || registry == nullptr) return;
        auto* scene = context->scene;

        query_.RefreshIfNeeded(*scene);

#if ECS_PROFILING_ENABLED
        std::deque<MeshChunkProbe> probes;
        CommitResults(*scene, &probes);
#else
        CommitResults(*scene, nullptr);
#endif
        ScheduleJobs(*scene, registry, world);
#if ECS_PROFILING_ENABLED
        AggregateMeshingProbes(probes);
#endif
    }

    void OnEnd() override {
        // No worker may still reference this system's queue after it is gone.
        if(auto* context = GetContext()) {
            if(context->scene != nullptr) {
                if(auto* jobSystem = context->scene->GetJobSystem()) {
                    if(jobSystem->GetSchedule() != nullptr) jobSystem->WaitIdle();
                }
            }
        }
        MeshedChunkResult discard;
        while(meshQueue_.TryPop(discard)) {}
    }

    // Test / benchmark hook: true while meshing work is queued or running.
    bool HasPendingWork() const {
        return meshInFlight_ > 0 || !meshQueue_.Empty();
    }

private:
    struct Candidate {
        EntityID entity = 0;
        glm::ivec3 section{0};
        ChunkBlocks* blocks = nullptr;
        ChunkMesh* mesh = nullptr;
        int distance = 0;
    };

    static int ChunkDistance(const glm::ivec3& section, int centerX, int centerZ) {
        const int dx = section.x - centerX;
        const int dz = section.z - centerZ;
        return dx * dx + dz * dz;
    }

    void CommitResults(ECS::Core::Scene& scene, std::deque<MeshChunkProbe>* probes) {
        MeshedChunkResult result;
        for(std::size_t n = 0; n < 4 && meshQueue_.TryPop(result); ++n) {
            if(meshInFlight_ > 0) --meshInFlight_;

            auto meshHandle = scene.GetActiveComponent<ChunkMesh>(result.entity);
            if(meshHandle.owner == 0) continue;               // entity unloaded
            ChunkMesh* mesh = meshHandle.Get();
            if(mesh == nullptr) continue;
            if(mesh->meshToken != result.token) {
#if ECS_PROFILING_ENABLED
                ECS::Profiling::Profiler::Get().Count(
                    "Stale mesh results discarded", 1.0);
#endif
                continue;                                     // stale result
            }

            auto blocksHandle = scene.GetActiveComponent<ChunkBlocks>(result.entity);
            ChunkBlocks* blocks = blocksHandle.Get();
            if(blocks == nullptr || blocks->revision != result.sourceRevision) {
                // Blocks changed while meshing (e.g. the player broke a block):
                // drop this mesh and let the next tick reschedule against the
                // fresh revision, so a stale mesh can never overwrite the edit.
                mesh->meshPending = false;
#if ECS_PROFILING_ENABLED
                ECS::Profiling::Profiler::Get().Count(
                    "Stale mesh results discarded", 1.0);
#endif
                continue;
            }

            const std::uint64_t nextMeshRevision = mesh->meshRevision + 1;
            *mesh = std::move(result.mesh);
            mesh->sourceRevision = result.sourceRevision;
            mesh->meshRevision = nextMeshRevision;
            mesh->meshToken = result.token;
            mesh->meshPending = false;

#if ECS_PROFILING_ENABLED
            if(probes != nullptr) {
                MeshChunkProbe probe;
                probe.sectionX = result.section.x;
                probe.sectionZ = result.section.z;
                probe.buildMs = result.buildMs;
                for(const CpuSubmesh& layer : mesh->layers) {
                    probe.vertexCount += layer.vertices.size();
                    probe.indexCount += layer.indices.size();
                }
                probe.mesh = result.stats;
                probes->push_back(probe);
            }
            ECS::Profiling::Profiler::Get().Count("Meshes rebuilt", 1.0);
#endif
        }
    }

    void ScheduleJobs(ECS::Core::Scene& scene, BlockRenderRegistry* registry,
                      TerrainWorld* world) {
        const std::size_t workers = TerrainWorkerCount(&scene);
        const std::size_t inFlightLimit = std::max<std::size_t>(2, workers * 2);
        if(meshInFlight_ >= inFlightLimit) return;
        const std::size_t budget = std::max<std::size_t>(1, workers);

        const glm::vec3 center = TerrainViewerCenter(scene, viewerQuery_);
        const int centerChunkX =
            static_cast<int>(std::floor(center.x / static_cast<float>(SectionSize)));
        const int centerChunkZ =
            static_cast<int>(std::floor(center.z / static_cast<float>(SectionSize)));

        std::vector<Candidate> candidates;
        for(auto view : query_) {
            auto* blocks = view.Get<ChunkBlocks>();
            auto* locations = view.Get<ChunkLocation>();
            auto* meshes = view.Get<ChunkMesh>();
            if(blocks == nullptr || locations == nullptr || meshes == nullptr
               || view.count == 0) {
                continue;
            }
            const auto& entities = view.archType->GetIndexEntities();

            for(std::size_t i = 0; i < view.count; ++i) {
                if(!blocks[i].generated) continue;
                if(meshes[i].sourceRevision == blocks[i].revision) continue;
                if(meshes[i].meshPending) continue;
                candidates.push_back(Candidate{
                    entities[view.beginIndex + i], locations[i].section, &blocks[i],
                    &meshes[i],
                    ChunkDistance(locations[i].section, centerChunkX, centerChunkZ)});
            }
        }
        if(candidates.empty()) return;

        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate& a, const Candidate& b) {
                      return a.distance < b.distance;
                  });

        const std::size_t remaining = inFlightLimit - meshInFlight_;
        const std::size_t count = std::min({candidates.size(), budget, remaining});

        std::vector<ECS::Core::ExecuteTask> jobs;
        jobs.reserve(count);
        for(std::size_t i = 0; i < count; ++i) {
            ChunkBlocks* blocks = candidates[i].blocks;
            ChunkMesh* mesh = candidates[i].mesh;
            const glm::ivec3 section = candidates[i].section;
            const EntityID entity = candidates[i].entity;

            mesh->meshPending = true;
            mesh->meshToken = ++meshTokenCounter_;
            const std::uint64_t token = mesh->meshToken;
            const std::uint64_t revision = blocks->revision;

            // The worker owns private snapshots, so entity create/delete cannot
            // move the storage out from under it. The neighbourhood is copied on
            // the main thread so border faces see the real (edited) world.
            auto snapshot = std::make_shared<ChunkBlocks>(*blocks);
            auto neighbors = std::make_shared<ChunkNeighborhood>();
            if(world != nullptr) world->CollectNeighborhood(section, *neighbors);
            WorkCompletionQueue<MeshedChunkResult>* queue = &meshQueue_;
            jobs.emplace_back([snapshot, neighbors, section, entity, token,
                               revision, registry, queue]() {
                MeshedChunkResult result;
                result.entity = entity;
                result.section = section;
                result.token = token;
                result.sourceRevision = revision;
#if ECS_PROFILING_ENABLED
                const auto buildStart = std::chrono::steady_clock::now();
                TerrainMesher::Build(*snapshot, section, *registry, result.mesh,
                                     &result.stats, neighbors.get());
                result.buildMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - buildStart).count();
#else
                TerrainMesher::Build(*snapshot, section, *registry, result.mesh,
                                     nullptr, neighbors.get());
#endif
                queue->Push(std::move(result));
            });
            ++meshInFlight_;
        }

#if ECS_PROFILING_ENABLED
        auto& profiler = ECS::Profiling::Profiler::Get();
        profiler.Count("Mesh jobs scheduled", static_cast<double>(count));
        profiler.Count("Meshing in flight", static_cast<double>(meshInFlight_));
#endif
        DispatchJobs(&scene, jobs, "Meshing");
    }

    TerrainQuery query_;
    ViewerQuery viewerQuery_;
    WorkCompletionQueue<MeshedChunkResult> meshQueue_;
    std::uint64_t meshTokenCounter_ = 0;
    std::size_t meshInFlight_ = 0;
};

class MeshUploadSystem final : public ECS::System::System {
public:
    MeshUploadSystem() : ECS::System::System("TerrainMeshUploadSystem", ECS::System::Phase::PostUpdate) {
        Reads<ChunkMesh>(); Writes<ChunkRender>();
    }
    void OnTick() override {
        auto* context = GetContext();
        auto* uploader = context ? context->GetService<Render::IMeshUploadQueue>() : nullptr;
        if(context == nullptr || context->scene == nullptr || uploader == nullptr) return;
        query_.RefreshIfNeeded(*context->scene);
        UploadBudget budget;
        for(auto chunk : query_) {
            auto* meshes = chunk.Get<ChunkMesh>();
            auto* renders = chunk.Get<ChunkRender>();
            for(std::size_t i = 0; i < chunk.count; ++i) Process(meshes[i], renders[i], *uploader, budget);
        }
    }

    void OnEnd() override {
        auto* context = GetContext();
        auto* uploader = context ? context->GetService<Render::IMeshUploadQueue>() : nullptr;
        if(context == nullptr || context->scene == nullptr || uploader == nullptr) return;
        query_.RefreshIfNeeded(*context->scene);
        for(auto chunk : query_) {
            auto* renders = chunk.Get<ChunkRender>();
            for(std::size_t i = 0; i < chunk.count; ++i) ReleaseChunkRender(renders[i], *uploader);
        }
    }

private:
    struct UploadBudget {
        std::size_t count = 0;
        std::size_t bytes = 0;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        bool Take(std::size_t size) {
            // Allow one oversized mesh so it cannot remain queued forever.
            if(count >= 4 || (count > 0 && (bytes + size > 4 * 1024 * 1024 ||
                std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(2)))) return false;
            ++count;
            bytes += size;
            return true;
        }
    };

    static Render::VertexLayout Layout() {
        return TerrainVertexLayout();
    }

    void Process(const ChunkMesh& mesh, ChunkRender& render, Render::IMeshUploadQueue& uploader,
                 UploadBudget& budget) {
        for(std::size_t layer = 0; layer < mesh.layers.size(); ++layer) {
            const CpuSubmesh& cpu = mesh.layers[layer];
            GpuSubmesh& gpu = render.layers[layer];
            if(auto pending = gpu.pending) {
                bool done = false;
                bool succeeded = false;
                Render::RenderResourceHandle<Render::VertexBufferSpec> completedHandle;
                std::uint32_t completedIndexCount = 0;
                std::uint64_t completedRevision = 0;
                {
                    std::lock_guard<std::mutex> lock(pending->mutex);
                    done = pending->done;
                    if(done) {
                        succeeded = pending->succeeded;
                        completedHandle = pending->handle;
                        completedIndexCount = pending->indexCount;
                        completedRevision = pending->revision;
                    }
                }

                // Reset only after the lock has been released. `pending` also
                // keeps UploadResult (and its mutex) alive until this scope ends.
                if(done) {
                    if(succeeded) {
                        if(completedHandle.IsValid()) gpu.handle = completedHandle;
                        gpu.indexCount = completedIndexCount;
                        gpu.uploadedRevision = completedRevision;
                    }
                    if(gpu.pending == pending) gpu.pending.reset();
                }
            }
            if(gpu.pending || gpu.uploadedRevision == mesh.meshRevision) continue;
            const auto byteSize = cpu.vertices.size() * sizeof(Vertex) +
                cpu.indices.size() * sizeof(std::uint32_t);
            if(inFlight_->load() >= 16 || !budget.Take(byteSize)) continue;
            if(cpu.indices.empty()) {
                if(gpu.handle.IsValid()) uploader.Destroy(gpu.handle);
                gpu = GpuSubmesh{};
                gpu.uploadedRevision = mesh.meshRevision;
                continue;
            }

            auto result = std::make_shared<UploadResult>();
            result->revision = mesh.meshRevision;
            result->indexCount = static_cast<std::uint32_t>(cpu.indices.size());
            gpu.pending = result;
            auto inFlight = inFlight_;
            ++*inFlight;
            if(gpu.handle.IsValid()) {
                result->updating = true;
                Render::UpdateMeshBufferDesc desc{};
                desc.vertexCount = static_cast<std::uint32_t>(cpu.vertices.size());
                desc.vertexData = cpu.vertices.data(); desc.vertexByteSize = cpu.vertices.size() * sizeof(Vertex);
                desc.indexCount = static_cast<std::uint32_t>(cpu.indices.size());
                desc.indexData = cpu.indices.data(); desc.indexByteSize = cpu.indices.size() * sizeof(std::uint32_t);
                uploader.Update(gpu.handle, desc, [result, inFlight](bool ok) {
                    std::lock_guard<std::mutex> lock(result->mutex); result->succeeded = ok; result->done = true;
                    --*inFlight;
                });
            } else {
                Render::CreateMeshBufferDesc desc{};
                desc.vertexLayout = Layout();
                desc.vertexCount = static_cast<std::uint32_t>(cpu.vertices.size());
                desc.vertexData = cpu.vertices.data(); desc.vertexByteSize = cpu.vertices.size() * sizeof(Vertex);
                desc.indexCount = static_cast<std::uint32_t>(cpu.indices.size());
                desc.indexData = cpu.indices.data(); desc.indexByteSize = cpu.indices.size() * sizeof(std::uint32_t);
                desc.usage = Render::BufferUsage::Dynamic;
                uploader.Create(desc, [result, inFlight, &uploader](Render::RenderResourceHandle<Render::VertexBufferSpec> handle) {
                    bool cancelled;
                    {
                        std::lock_guard<std::mutex> lock(result->mutex);
                        cancelled = result->cancelled;
                        result->handle = cancelled ? decltype(handle){} : handle;
                        result->succeeded = handle.IsValid(); result->done = true;
                    }
                    if(cancelled && handle.IsValid()) uploader.Destroy(handle);
                    --*inFlight;
                });
            }
        }
    }
    TerrainQuery query_;
    // Callbacks may outlive the system or the chunk that initiated an upload.
    std::shared_ptr<std::atomic_size_t> inFlight_ = std::make_shared<std::atomic_size_t>(0);
};

struct RenderSettings {
    std::array<Render::RenderResourceHandle<Render::PipelineSpec>, 3> pipelines;
    Render::RenderResourceHandle<Render::RHITextureSpec> atlas;
};

class RenderExtractSystem final : public ECS::System::System {
public:
    RenderExtractSystem() : ECS::System::System("TerrainRenderExtractSystem", ECS::System::Phase::RenderExtract) {
        Reads<ChunkLocation>(); Reads<ChunkRender>();
    }
    void OnTick() override {
        auto* context = GetContext();
        auto* world = context ? context->GetService<Render::RenderWorld>() : nullptr;
        auto* settings = context ? context->GetService<RenderSettings>() : nullptr;
        if(context == nullptr || context->scene == nullptr || world == nullptr || settings == nullptr) return;
        auto* frame = context->GetService<Render::RenderFrameService>();
        const Render::ViewFrustum frustum(frame ? frame->view.viewProjection : glm::mat4(1.0f));
        query_.RefreshIfNeeded(*context->scene);
        for(auto chunk : query_) {
            auto* locations = chunk.Get<ChunkLocation>();
            auto* renders = chunk.Get<ChunkRender>();
            for(std::size_t i = 0; i < chunk.count; ++i) {
                if(!renders[i].visible) continue;
                const glm::vec3 origin = locations[i].WorldOrigin();
                const glm::vec3 extent(SectionSize, SectionHeight, SectionSize);
                if(frame && !frustum.Intersects(origin, origin + extent)) continue;
                for(std::size_t layer = 0; layer < renders[i].layers.size(); ++layer) {
                    const GpuSubmesh& gpu = renders[i].layers[layer];
                    if(!gpu.handle.IsValid() || gpu.indexCount == 0 || !settings->pipelines[layer].IsValid()) continue;
                    Render::RenderItem item{};
                    item.mesh = gpu.handle; item.pipeline = settings->pipelines[layer]; item.texture = settings->atlas;
                    item.model = glm::translate(glm::mat4(1.0f), origin);
                    // This handle is updated in place on the render thread.
                    // A queued frame may outlive a shrink/grow upload, so draw
                    // the whole current buffer instead of a stale CPU count.
                    item.draw.indexCount = 0;
                    item.layer = layer == 0 ? Render::RenderLayer::Opaque :
                                 layer == 1 ? Render::RenderLayer::Cutout : Render::RenderLayer::Transparent;
                    item.viewDepth = renders[i].viewDepth;
                    if(frame) {
                        const auto delta = origin + extent * 0.5f - glm::vec3(frame->view.cameraPosition);
                        item.viewDepth = glm::dot(delta, delta);
                    }
                    item.sortKey = (static_cast<std::uint64_t>(item.pipeline.id) << 32u) | item.texture.id;
                    world->Add(std::move(item));
                }
            }
        }
    }
private:
    TerrainQuery query_;
};

} // namespace Terrain::System
