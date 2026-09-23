#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
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
#include "Terrain/Public/terrain_components.h"
#include "Terrain/Public/terrain_generator.h"
#include "Terrain/Public/terrain_mesher.h"

namespace Terrain::System {

using ECS::EntityID;

// Task 2.0: per-chunk meshing probe. Each job writes exactly one slot (its own
// task-local record); the main thread aggregates them only after WaitIdle(), so
// no shared profiler state is touched by workers and no lock/atomic is needed.
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

// Runs a batch of chunk jobs on the scene's JobSystem. Falls back to running
// them inline when no worker pool is configured (e.g. the headless test), so the
// systems stay usable with or without an ECSKernel. `stage` labels the profiling
// counters ("Generation" / "Meshing") and does not affect behavior.
inline void RunJobs(ECS::Core::Scene* scene,
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
                const auto waitStart = std::chrono::steady_clock::now();
#endif
                jobSystem->WaitIdle();
#if ECS_PROFILING_ENABLED
                const double waitMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - waitStart).count();
                profiler.Accumulate(std::string(stage) + " wait", waitMs);
#endif
                return;
            }
        }
    }

    for(auto& job : jobs) job();
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

        std::unordered_set<glm::ivec2, IVec2Hash> active;
        std::vector<EntityID> stale;
        for(auto view : chunkQuery_) {
            auto* locations = view.Get<ChunkLocation>();
            if(locations == nullptr) continue;
            for(std::size_t i = 0; i < view.count; ++i) {
                const glm::ivec3 section = locations[i].section;
                const glm::ivec2 coord{section.x, section.z};
                active.insert(coord);

                const int distance = std::max(
                    std::abs(coord.x - centerChunk.x),
                    std::abs(coord.y - centerChunk.y));
                if(distance > keepRadius) {
                    stale.push_back(view.archType->GetIndexEntities()[view.beginIndex + i]);
                }
            }
        }

        for(const EntityID id : stale) scene->DeleteEntity(ECS::EntityHandle(id));

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

// Fills ChunkBlocks for every chunk that has not been generated yet, one job per
// terrain entity so independent chunks can be generated on different workers.
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

        query_.RefreshIfNeeded(*scene);

#if ECS_PROFILING_ENABLED
        std::size_t pending = 0;
#endif
        std::vector<ECS::Core::ExecuteTask> jobs;
        for(auto view : query_) {
            auto* blocks = view.Get<ChunkBlocks>();
            auto* locations = view.Get<ChunkLocation>();
            if(blocks == nullptr || locations == nullptr || view.count == 0) continue;

            for(std::size_t i = 0; i < view.count; ++i) {
                if(blocks[i].generated) continue;
#if ECS_PROFILING_ENABLED
                ++pending;
#endif
                // One job per entity: the job owns exactly this ChunkBlocks slot,
                // and no structural change can move it before WaitIdle() returns.
                ChunkBlocks* block = &blocks[i];
                const glm::ivec3 section = locations[i].section;
                jobs.emplace_back([block, section]() {
                    GenerateChunkBlocks(section, *block);
                });
            }
        }

#if ECS_PROFILING_ENABLED
        ECS::Profiling::Profiler::Get().Count(
            "Chunks generated", static_cast<double>(pending));
#endif
        RunJobs(scene, jobs, "Generation");
    }

private:
    TerrainQuery query_;
};

#if ECS_PROFILING_ENABLED
// Aggregates one frame of per-chunk probes. Runs on the main thread after
// WaitIdle(), which provides the happens-before edge for the worker writes.
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

// Rebuilds CPU meshes for chunks whose blocks changed. One job per terrain entity;
// neighbouring blocks across the chunk border come from the deterministic
// generator, so the job reads only its own entity and there are no seams.
class MeshingSystem final : public ECS::System::System {
public:
    MeshingSystem()
        : ECS::System::System("TerrainMeshingSystem", ECS::System::Phase::Update) {
        Reads<ChunkBlocks>(); Writes<ChunkMesh>();
    }

    void OnTick() override {
        auto* context = GetContext();
        auto* registry = context ? context->GetService<BlockRenderRegistry>() : nullptr;
        if(context == nullptr || context->scene == nullptr || registry == nullptr) return;
        auto* scene = context->scene;

        query_.RefreshIfNeeded(*scene);

#if ECS_PROFILING_ENABLED
        std::size_t pending = 0;
        // Task-local probe slots: std::deque keeps element addresses stable while
        // it grows, so each job writes its own record lock-free. The main thread
        // reads them only after WaitIdle().
        std::deque<MeshChunkProbe> probes;
#endif
        std::vector<ECS::Core::ExecuteTask> jobs;
        for(auto view : query_) {
            auto* blocks = view.Get<ChunkBlocks>();
            auto* locations = view.Get<ChunkLocation>();
            auto* meshes = view.Get<ChunkMesh>();
            if(blocks == nullptr || locations == nullptr || meshes == nullptr) continue;

            for(std::size_t i = 0; i < view.count; ++i) {
                if(meshes[i].sourceRevision == blocks[i].revision) continue;
#if ECS_PROFILING_ENABLED
                ++pending;
#endif
                // One job per entity: it reads only this ChunkBlocks slot and
                // writes only this ChunkMesh slot.
                ChunkBlocks* block = &blocks[i];
                ChunkMesh* mesh = &meshes[i];
                const glm::ivec3 section = locations[i].section;
                MeshChunkProbe* probe = nullptr;
#if ECS_PROFILING_ENABLED
                probes.push_back(MeshChunkProbe{section.x, section.z, 0.0, 0, 0});
                probe = &probes.back();
#endif
                jobs.emplace_back([block, mesh, section, registry, probe]() {
                    if(mesh->sourceRevision == block->revision) return;
#if ECS_PROFILING_ENABLED
                    const auto buildStart = std::chrono::steady_clock::now();
#endif
                    TerrainMesher::Build(*block, section, *registry, *mesh,
                                         probe ? &probe->mesh : nullptr);

#if ECS_PROFILING_ENABLED
                    probe->buildMs = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - buildStart).count();
                    for(const CpuSubmesh& layer : mesh->layers) {
                        probe->vertexCount += layer.vertices.size();
                        probe->indexCount += layer.indices.size();
                    }
#endif
                });
            }
        }

#if ECS_PROFILING_ENABLED
        ECS::Profiling::Profiler::Get().Count(
            "Meshes rebuilt", static_cast<double>(pending));
#endif
        RunJobs(scene, jobs, "Meshing");
#if ECS_PROFILING_ENABLED
        AggregateMeshingProbes(probes);
#endif
    }

private:
    TerrainQuery query_;
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
        for(auto chunk : query_) {
            auto* meshes = chunk.Get<ChunkMesh>();
            auto* renders = chunk.Get<ChunkRender>();
            for(std::size_t i = 0; i < chunk.count; ++i) Process(meshes[i], renders[i], *uploader);
        }
    }

private:
    static Render::VertexLayout Layout() {
        return TerrainVertexLayout();
    }

    static void Process(const ChunkMesh& mesh, ChunkRender& render, Render::IMeshUploadQueue& uploader) {
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
            if(gpu.handle.IsValid()) {
                result->updating = true;
                Render::UpdateMeshBufferDesc desc{};
                desc.vertexCount = static_cast<std::uint32_t>(cpu.vertices.size());
                desc.vertexData = cpu.vertices.data(); desc.vertexByteSize = cpu.vertices.size() * sizeof(Vertex);
                desc.indexCount = static_cast<std::uint32_t>(cpu.indices.size());
                desc.indexData = cpu.indices.data(); desc.indexByteSize = cpu.indices.size() * sizeof(std::uint32_t);
                uploader.Update(gpu.handle, desc, [result](bool ok) {
                    std::lock_guard<std::mutex> lock(result->mutex); result->succeeded = ok; result->done = true;
                });
            } else {
                Render::CreateMeshBufferDesc desc{};
                desc.vertexLayout = Layout();
                desc.vertexCount = static_cast<std::uint32_t>(cpu.vertices.size());
                desc.vertexData = cpu.vertices.data(); desc.vertexByteSize = cpu.vertices.size() * sizeof(Vertex);
                desc.indexCount = static_cast<std::uint32_t>(cpu.indices.size());
                desc.indexData = cpu.indices.data(); desc.indexByteSize = cpu.indices.size() * sizeof(std::uint32_t);
                desc.usage = Render::BufferUsage::Dynamic;
                uploader.Create(desc, [result](Render::RenderResourceHandle<Render::VertexBufferSpec> handle) {
                    std::lock_guard<std::mutex> lock(result->mutex); result->handle = handle;
                    result->succeeded = handle.IsValid(); result->done = true;
                });
            }
        }
    }
    TerrainQuery query_;
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
        query_.RefreshIfNeeded(*context->scene);
        for(auto chunk : query_) {
            auto* locations = chunk.Get<ChunkLocation>();
            auto* renders = chunk.Get<ChunkRender>();
            for(std::size_t i = 0; i < chunk.count; ++i) {
                if(!renders[i].visible) continue;
                for(std::size_t layer = 0; layer < renders[i].layers.size(); ++layer) {
                    const GpuSubmesh& gpu = renders[i].layers[layer];
                    if(!gpu.handle.IsValid() || gpu.indexCount == 0 || !settings->pipelines[layer].IsValid()) continue;
                    Render::RenderItem item{};
                    item.mesh = gpu.handle; item.pipeline = settings->pipelines[layer]; item.texture = settings->atlas;
                    item.model = glm::translate(glm::mat4(1.0f), locations[i].WorldOrigin());
                    item.draw.indexCount = gpu.indexCount;
                    item.layer = layer == 0 ? Render::RenderLayer::Opaque :
                                 layer == 1 ? Render::RenderLayer::Cutout : Render::RenderLayer::Transparent;
                    item.viewDepth = renders[i].viewDepth;
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
