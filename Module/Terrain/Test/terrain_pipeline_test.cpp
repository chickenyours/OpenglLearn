#include <cassert>
#include <cstddef>
#include <cstring>
#include <vector>

#include "engine/ECS/Scene/scene.h"
#include "engine/ECS/System/system_pipeline.h"
#include "Render/Public/Pipeline/mesh_upload_queue.h"
#include "Render/Public/Pipeline/render_pipeline.h"
#include "Render/Systems/render_pipeline_system.h"
#include "Terrain/Public/terrain_components.h"
#include "Terrain/Systems/terrain_systems.h"

namespace {
class ImmediateUploader final : public Render::IMeshUploadQueue {
public:
    void Create(const Render::CreateMeshBufferDesc& desc, CreateCallback callback) override {
        assert(desc.vertexByteSize > 0 && desc.indexByteSize > 0);
        callback({nextId_++, 0});
    }
    void Update(Render::RenderResourceHandle<Render::VertexBufferSpec>,
                const Render::UpdateMeshBufferDesc&, UpdateCallback callback) override { callback(true); }
    void Destroy(Render::RenderResourceHandle<Render::VertexBufferSpec>) override {}
private:
    std::uint32_t nextId_ = 10;
};
}

int main() {
    Terrain::RegisterTerrainComponents(); 
    ECS::Core::Scene scene;
    auto description = scene.CreateArchTypeDescription();
    description->AddComponentArray<Terrain::ChunkLocation>();
    description->AddComponentArray<Terrain::ChunkBlocks>();
    description->AddComponentArray<Terrain::ChunkMesh>();
    description->AddComponentArray<Terrain::ChunkRender>();
    auto archetype = scene.CreateArchType(description, 8);
    const auto entity = scene.CreateEntity(archetype);
    auto blocks = scene.GetActiveComponent<Terrain::ChunkBlocks>(entity.GetID());
    blocks.Get()->Set(1, 1, 1, 1);

    Terrain::BlockRenderRegistry blockRegistry;
    Terrain::BlockRenderInfo stone{};
    stone.visible = true; stone.occludes = true; stone.layer = Terrain::Layer::Opaque;
    blockRegistry.Set(1, stone);

    ImmediateUploader uploader;
    Render::RenderWorld renderWorld;
    Terrain::System::RenderSettings terrainSettings{};
    terrainSettings.pipelines[0] = {2, 0};
    terrainSettings.atlas = {3, 0};

    Render::RHIFrameCommandBufferPool commandPool;
    Render::RHIFrameEncoder encoder(commandPool.threadAny_GetBuffer());
    assert(encoder.Begin({.frameIndex = 1}));
    Render::RenderFrameService frameService{&encoder, {}};

    ECS::System::Context context;
    context.scene = &scene;
    context.frameIndex = 1;
    context.SetService(&blockRegistry);
    context.SetService<Render::IMeshUploadQueue>(&uploader);
    context.SetService(&renderWorld);
    context.SetService(&terrainSettings);
    context.SetService(&frameService);

    ECS::System::Pipeline pipeline;
    pipeline.Add<Terrain::System::MeshingSystem>();
    pipeline.Add<Terrain::System::MeshUploadSystem>();
    pipeline.Add<Render::System::RenderExtractBeginSystem>();
    pipeline.Add<Terrain::System::RenderExtractSystem>();
    pipeline.Add<Render::System::RenderPublishSystem>();
    pipeline.Add<Render::System::RenderPipelineSystem>();
    assert((pipeline.RunBefore<Render::System::RenderExtractBeginSystem, Terrain::System::RenderExtractSystem>()));
    assert((pipeline.RunBefore<Terrain::System::RenderExtractSystem, Render::System::RenderPublishSystem>()));
    assert(pipeline.Tick(context));
    ++context.frameIndex;
    assert(pipeline.Tick(context));

    auto mesh = scene.GetActiveComponent<Terrain::ChunkMesh>(entity.GetID());
    auto render = scene.GetActiveComponent<Terrain::ChunkRender>(entity.GetID());
    assert(mesh.Get()->layers[0].vertices.size() == 24);
    assert(mesh.Get()->layers[0].indices.size() == 36);
    assert(!render.Get()->layers[0].pending);
    assert(render.Get()->layers[0].handle.IsValid());
    assert(encoder.End(false));
    pipeline.Stop(context);
    return 0;
}
