#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/ECS/JobSystem/job_system.h"
#include "engine/ECS/Query/query.h"
#include "engine/ECS/System/system.h"
#include "Render/Public/Pipeline/mesh_upload_queue.h"
#include "Render/Public/Pipeline/render_world.h"
#include "Terrain/Public/terrain_components.h"
#include "Terrain/Public/terrain_generator.h"
#include "Terrain/Public/terrain_mesher.h"

namespace Terrain::System {

using ECS::EntityID;

using TerrainQuery = ECS::Core::ChunkQuery<
    ECS::Core::Require<ChunkLocation, ChunkBlocks, ChunkMesh, ChunkRender>,
    ECS::Core::Optional<>, ECS::Core::Exclude<>>;

using ViewerQuery = ECS::Core::ChunkQuery<
    ECS::Core::Require<TerrainViewer>,
    ECS::Core::Optional<>, ECS::Core::Exclude<>>;

// Runs a batch of chunk jobs on the scene's JobSystem. Falls back to running
// them inline when no worker pool is configured (e.g. the headless test), so the
// systems stay usable with or without an ECSKernel.
inline void RunJobs(ECS::Core::Scene* scene,
                    std::vector<ECS::Core::ExecuteTask>& jobs) {
    if(jobs.empty()) return;

    if(scene != nullptr) {
        if(auto* jobSystem = scene->GetJobSystem()) {
            if(jobSystem->GetSchedule() != nullptr) {
                for(auto& job : jobs) jobSystem->Submit(std::move(job));
                jobSystem->DispatchAll();
                jobSystem->WaitIdle();
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
    }

private:
    static constexpr int kLoadRadius = 6;
    static constexpr int kMaxChunkCreationsPerFrame = 8;

    ObjectWeakPtr<ECS::Core::ArchType> chunkArchtype_;
    ViewerQuery viewerQuery_;
    TerrainQuery chunkQuery_;
};

// Fills ChunkBlocks for every chunk that has not been generated yet, one job per
// archetype chunk.
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

        std::vector<ECS::Core::ExecuteTask> jobs;
        for(auto view : query_) {
            auto* blocks = view.Get<ChunkBlocks>();
            auto* locations = view.Get<ChunkLocation>();
            if(blocks == nullptr || locations == nullptr || view.count == 0) continue;

            bool needsWork = false;
            for(std::size_t i = 0; i < view.count; ++i) {
                if(!blocks[i].generated) { needsWork = true; break; }
            }
            if(!needsWork) continue;

            const std::size_t count = view.count;
            jobs.emplace_back([blocks, locations, count]() {
                for(std::size_t i = 0; i < count; ++i) {
                    if(blocks[i].generated) continue;
                    GenerateChunkBlocks(locations[i].section, blocks[i]);
                }
            });
        }

        RunJobs(scene, jobs);
    }

private:
    TerrainQuery query_;
};

// Rebuilds CPU meshes for chunks whose blocks changed. Neighbouring blocks across
// the chunk border come from the deterministic generator, so there are no seams.
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

        std::vector<ECS::Core::ExecuteTask> jobs;
        for(auto view : query_) {
            auto* blocks = view.Get<ChunkBlocks>();
            auto* locations = view.Get<ChunkLocation>();
            auto* meshes = view.Get<ChunkMesh>();
            if(blocks == nullptr || locations == nullptr || meshes == nullptr) continue;

            bool needsWork = false;
            for(std::size_t i = 0; i < view.count; ++i) {
                if(meshes[i].sourceRevision != blocks[i].revision) { needsWork = true; break; }
            }
            if(!needsWork) continue;

            const std::size_t count = view.count;
            jobs.emplace_back([blocks, locations, meshes, registry, count]() {
                for(std::size_t i = 0; i < count; ++i) {
                    if(meshes[i].sourceRevision == blocks[i].revision) continue;

                    ChunkBlocks& chunkBlocks = blocks[i];
                    const glm::ivec3 base = locations[i].section * SectionSize;
                    const auto sampler = [&chunkBlocks, base](int x, int y, int z) {
                        if(ChunkBlocks::InBounds(x, y, z)) return chunkBlocks.Get(x, y, z);
                        return BlockAt(base.x + x, base.y + y, base.z + z);
                    };

                    TerrainMesher::Build(
                        chunkBlocks, locations[i].section, *registry, meshes[i], sampler);
                }
            });
        }

        RunJobs(scene, jobs);
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
