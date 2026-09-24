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

class DelayedUploader final : public Render::IMeshUploadQueue {
public:
    std::vector<std::function<void()>> pending;
    std::vector<Render::RenderResourceHandle<Render::VertexBufferSpec>> destroyed;
    int submissions = 0;
    void Create(const Render::CreateMeshBufferDesc&, CreateCallback callback) override {
        const auto id = static_cast<std::uint32_t>(++submissions + 10);
        pending.push_back([callback, id] { callback({id, 0}); });
    }
    void Update(Render::RenderResourceHandle<Render::VertexBufferSpec>,
                const Render::UpdateMeshBufferDesc&, UpdateCallback callback) override {
        ++submissions;
        pending.push_back([callback] { callback(true); });
    }
    void Destroy(Render::RenderResourceHandle<Render::VertexBufferSpec> handle) override {
        assert(std::find(destroyed.begin(), destroyed.end(), handle) == destroyed.end());
        destroyed.push_back(handle);
    }
    void Complete() {
        auto callbacks = std::move(pending);
        pending.clear();
        for(auto& callback : callbacks) callback();
    }
};

void TestUploadBackpressure() {
    ECS::Core::Scene scene;
    auto description = scene.CreateArchTypeDescription();
    description->AddComponentArray<Terrain::ChunkLocation>();
    description->AddComponentArray<Terrain::ChunkBlocks>();
    description->AddComponentArray<Terrain::ChunkMesh>();
    description->AddComponentArray<Terrain::ChunkRender>();
    auto archetype = scene.CreateArchType(description, 64);
    std::vector<ECS::EntityID> entities;
    for(int i = 0; i < 40; ++i) {
        const auto id = scene.CreateEntity(archetype).GetID();
        entities.push_back(id);
        auto* mesh = scene.GetActiveComponent<Terrain::ChunkMesh>(id).Get();
        mesh->meshRevision = 1;
        mesh->layers[0].vertices.resize(4);
        mesh->layers[0].indices = {0, 1, 2};
        auto* render = scene.GetActiveComponent<Terrain::ChunkRender>(id).Get();
        render->layers[1].uploadedRevision = render->layers[2].uploadedRevision = 1;
    }
    DelayedUploader uploader;
    ECS::System::Context context;
    context.scene = &scene;
    context.SetService<Render::IMeshUploadQueue>(&uploader);
    ECS::System::Pipeline pipeline;
    pipeline.Add<Terrain::System::MeshUploadSystem>();
    for(int tick = 0; tick < 20; ++tick) {
        const int before = uploader.submissions;
        assert(pipeline.Tick(context));
        assert(uploader.submissions - before <= 4);
        assert(uploader.pending.size() <= 16);
    }
    assert(uploader.submissions == 16); // slow GPU must stop further submissions
    for(int tick = 0; tick < 40; ++tick) {
        uploader.Complete();
        assert(pipeline.Tick(context));
    }
    assert(uploader.submissions == 40);
    for(auto id : entities) {
        const auto* gpu = scene.GetActiveComponent<Terrain::ChunkRender>(id).Get();
        assert(gpu->layers[0].uploadedRevision == 1 && !gpu->layers[0].pending);
    }
    // An oversized upload makes progress, but cannot share its tick with another.
    for(int i = 0; i < 2; ++i) {
        auto* mesh = scene.GetActiveComponent<Terrain::ChunkMesh>(entities[i]).Get();
        mesh->layers[0].vertices.resize(5 * 1024 * 1024 / sizeof(Terrain::Vertex) + 1);
        mesh->meshRevision = 2;
        auto* gpu = scene.GetActiveComponent<Terrain::ChunkRender>(entities[i]).Get();
        gpu->layers[1].uploadedRevision = gpu->layers[2].uploadedRevision = 2;
    }
    assert(pipeline.Tick(context));
    assert(uploader.submissions == 41);
    uploader.Complete();
    assert(pipeline.Tick(context));
    assert(uploader.submissions == 42);
    uploader.Complete();
    assert(pipeline.Tick(context));
    pipeline.Stop(context);
    assert(uploader.destroyed.size() == 40);

    // Both unload races: callback before ECS commit, and callback after unload.
    uploader.destroyed.clear();
    for(auto id : entities) {
        auto* mesh = scene.GetActiveComponent<Terrain::ChunkMesh>(id).Get();
        mesh->meshRevision = 3;
    }
    assert(pipeline.Tick(context));
    assert(!uploader.pending.empty());
    pipeline.Stop(context); // cancel in-flight creates
    uploader.Complete();
    assert(!uploader.destroyed.empty());
    const auto cancelledCount = uploader.destroyed.size();
    assert(pipeline.Tick(context));
    uploader.Complete(); // completed handles have not yet been committed
    pipeline.Stop(context);
    assert(uploader.destroyed.size() > cancelledCount);
}
}

int main() {
    Terrain::RegisterTerrainComponents(); 
    TestUploadBackpressure();
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
    // No GenerationSystem runs in this test, so mark the chunk as generated to
    // let the async MeshingSystem pick it up (mirrors a committed generation).
    blocks.Get()->generated = true;

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
    // Meshing is async: tick 1 schedules, tick 2 commits + starts the upload,
    // tick 3 observes the completed upload.
    ++context.frameIndex;
    assert(pipeline.Tick(context));

    auto mesh = scene.GetActiveComponent<Terrain::ChunkMesh>(entity.GetID());
    auto render = scene.GetActiveComponent<Terrain::ChunkRender>(entity.GetID());
    assert(mesh.Get()->layers[0].vertices.size() == 24);
    assert(mesh.Get()->layers[0].indices.size() == 36);
    // Both triangles on each of the six block faces must face outward.
    const auto& solid = mesh.Get()->layers[0];
    for(std::size_t i = 0; i < solid.indices.size(); i += 3) {
        const auto& a = solid.vertices[solid.indices[i]];
        const auto& b = solid.vertices[solid.indices[i + 1]];
        const auto& c = solid.vertices[solid.indices[i + 2]];
        assert(glm::dot(glm::cross(b.position - a.position, c.position - a.position), a.normal) > 0);
    }
    assert(!render.Get()->layers[0].pending);
    assert(render.Get()->layers[0].handle.IsValid());
    assert(encoder.End(false));

    // Extract a frame with the old CPU count, then simulate a smaller/larger
    // in-place GPU buffer before callback metadata catches up. Both packets
    // must request the full live buffer (indexCount == 0), never the old count.
    ECS::System::Pipeline extract;
    extract.Add<Render::System::RenderExtractBeginSystem>();
    extract.Add<Terrain::System::RenderExtractSystem>();
    extract.Add<Render::System::RenderPublishSystem>();
    assert((extract.RunBefore<Render::System::RenderExtractBeginSystem, Terrain::System::RenderExtractSystem>()));
    assert((extract.RunBefore<Terrain::System::RenderExtractSystem, Render::System::RenderPublishSystem>()));
    for(std::uint32_t staleCount : {36u, 18u, 72u}) {
        render.Get()->layers[0].indexCount = staleCount;
        assert(extract.Tick(context));
        Render::RenderFrame snapshot;
        assert(renderWorld.Consume(snapshot));
        assert(snapshot.items.size() == 1);
        assert(snapshot.items[0].draw.indexCount == 0 && snapshot.items[0].draw.firstIndex == 0);
    }
    extract.Stop(context);
    pipeline.Stop(context);
    return 0;
}
