#pragma once

#include <array>
#include <cstddef>
#include <memory>

#include <glm/gtc/matrix_transform.hpp>

#include "engine/ECS/Query/query.h"
#include "engine/ECS/System/system.h"
#include "Render/Public/Pipeline/mesh_upload_queue.h"
#include "Render/Public/Pipeline/render_world.h"
#include "Terrain/Public/terrain_components.h"
#include "Terrain/Public/terrain_mesher.h"

namespace Terrain::System {

using TerrainQuery = ECS::Core::ChunkQuery<
    ECS::Core::Require<ChunkLocation, ChunkBlocks, ChunkMesh, ChunkRender>,
    ECS::Core::Optional<>, ECS::Core::Exclude<>>;

class MeshingSystem final : public ECS::System::System {
public:
    MeshingSystem() : ECS::System::System("TerrainMeshingSystem", ECS::System::Phase::Update) {
        Reads<ChunkBlocks>(); Writes<ChunkMesh>();
    }
    void OnTick() override {
        auto* context = GetContext();
        auto* registry = context ? context->GetService<BlockRenderRegistry>() : nullptr;
        if(context == nullptr || context->scene == nullptr || registry == nullptr) return;
        query_.RefreshIfNeeded(*context->scene);
        for(auto chunk : query_) {
            auto* blocks = chunk.Get<ChunkBlocks>();
            auto* meshes = chunk.Get<ChunkMesh>();
            for(std::size_t i = 0; i < chunk.count; ++i) {
                if(meshes[i].sourceRevision != blocks[i].revision) TerrainMesher::Build(blocks[i], *registry, meshes[i]);
            }
        }
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
        return {{Render::VertexFieldType::Vec3, Render::VertexFieldType::Vec3,
                 Render::VertexFieldType::Vec2, Render::VertexFieldType::Int}};
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
