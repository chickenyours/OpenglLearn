#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

#include "Render/Public/Pipeline/render_pipeline.h"
#include "Render/Private/rhi_device.h"

namespace Render {
struct RHIDeviceTestAccess {
    static void Install(RHIDevice& device, IBackend* backend) {
        device.backend_ = ObjectPtr<IBackend>(backend);
        device.isRun_.store(true);
    }
    static void Pump(RHIDevice& device) {
        device.commandSystem_.thread_SwitchQueues();
        device.thread_ProcessCurrentQueue();
    }
};
}

namespace {
using namespace Render;
using namespace Render::Material;

struct DrawRecord {
    std::uint32_t pipeline = 0, mesh = 0;
    float value = 0;
    std::map<std::uint32_t, RenderResourceHandle<RHITextureSpec>> textures;
};

class RecordingBackend final : public OpenglBackend {
public:
    RecordingBackend() : OpenglBackend({}) {}
    std::vector<DrawRecord> draws;
    DrawRecord current;
    std::map<std::uint32_t, float> values;
    int uniformUpdates = 0;
    void BeginFrame(const RHICommand::BeginFrame&) override { current = {}; }
    void EndFrame(const RHICommand::EndFrame&) override {}
    void SetPipeline(const RHICommand::SetPipeline& c) override { current.pipeline = c.pipeline.id; }
    void SetVertexBuffer(const RHICommand::SetVertexBuffer& c) override { current.mesh = c.buffer.id; }
    void BindTexture(const RHICommand::BindTexture& c) override { current.textures[c.slot] = c.texture; }
    void UpdateUniformBuffer(const RHICommand::UpdateUniformBuffer& c) override {
        assert(c.size == 16 && c.offset == 0);
        std::memcpy(&values[c.buffer.id], c.data.data(), sizeof(float));
        ++uniformUpdates;
    }
    void BindUniformBuffer(const RHICommand::BindUniformBuffer& c) override {
        assert(c.binding == 2);
        current.value = values[c.buffer.id];
    }
    void DrawIndexed(const RHICommand::DrawIndexed&) override { draws.push_back(current); }
};

std::shared_ptr<const MaterialAsset> Asset(Domain domain = Domain::Surface) {
    MaterialTemplateDesc desc;
    desc.name = "test";
    desc.domain = domain;
    ParameterDesc parameter;
    parameter.name = "amount";
    parameter.type = ParameterType::Float;
    parameter.defaultValue = 1.0f;
    parameter.perObject = true;
    desc.parameters.push_back(parameter);
    desc.textures.push_back({"base", 0, true});
    desc.textures.push_back({"detail", 3, false});
    return MaterialAsset::Create(MaterialTemplate::Create(desc));
}

MaterialPassResources Resources(const std::shared_ptr<const MaterialAsset>& asset,
    std::shared_ptr<const void> owner, std::uint32_t pipeline = 10) {
    MaterialPassResources resources;
    resources.expectedTemplate = asset->Parameters().GetTemplate();
    resources.pipeline = {pipeline, 0};
    resources.parameterBuffer = {20, 0};
    resources.parameterBufferBytes = 16;
    resources.textures = {{0, {30, 0}}};
    resources.lifetime = std::move(owner);
    return resources;
}

float Value(const MaterialSnapshot& snapshot) {
    float result = 0;
    std::memcpy(&result, snapshot.Parameters().Bytes().data(), sizeof(result));
    return result;
}

void TestPublicationAndValidation() {
    MaterialService service;
    auto asset = Asset();
    auto resources = Resources(asset, std::make_shared<int>(0));
    const auto handle = service.Register(asset, resources);
    assert(handle.IsValid());
    auto old = service.Capture(handle);
    assert(old && Value(*old) == 1);
    assert(old == service.Capture(handle)); // unchanged instances share snapshots
    assert(service.SetParameter(handle, "amount", 0.5f));
    auto current = service.Capture(handle);
    assert(current != old && Value(*current) == 0.5f && Value(*old) == 1);
    auto object = service.Capture(handle, {{"amount", 0.25f}});
    assert(Value(*object) == 0.25f && Value(*current) == 0.5f);
    assert(!service.SetParameter(handle, "amount", glm::vec4(1)));
    assert(service.Capture(handle) == current);

    auto invalid = resources;
    invalid.textures.clear();
    std::string error;
    assert(!service.Replace(handle, asset, invalid, &error) && !error.empty());
    assert(service.Capture(handle) == current);
    invalid = resources;
    invalid.parameterBufferBytes = 32;
    assert(!service.Replace(handle, asset, invalid));
    invalid = resources;
    invalid.parameterBinding = 3;
    assert(!service.Replace(handle, asset, invalid));
    invalid = resources;
    invalid.parameterBinding = UINT32_MAX;
    assert(!service.Replace(handle, asset, invalid));
    assert(!service.Replace(handle, Asset(), resources)); // equal size is not pipeline/schema compatibility
    invalid = resources;
    invalid.parameterBinding = 1;
    assert(!service.Replace(handle, asset, invalid));
    invalid = resources;
    invalid.textures.push_back({0, {31, 0}});
    assert(!service.Replace(handle, asset, invalid));
    invalid = resources;
    invalid.textures.push_back({7, {31, 0}});
    assert(!service.Replace(handle, asset, invalid));
    invalid = resources;
    invalid.lifetime.reset();
    assert(!service.Replace(handle, asset, invalid));
    assert(service.Remove(handle));
    assert(!service.Capture(handle));
    auto replacement = service.Register(asset, resources);
    assert(replacement.index == handle.index && replacement.generation != handle.generation);
    assert(Value(*old) == 1);
    service.Clear();
    assert(!service.Capture(replacement));
    assert(service.Register(asset, resources) != replacement);
}

void TestRecordingAndRetention() {
    RHIDevice device;
    auto* backend = new RecordingBackend();
    RHIDeviceTestAccess::Install(device, backend);
    int released = 0;
    auto owner = std::shared_ptr<const void>(new int(1), [&](const void* value) {
        ++released;
        delete static_cast<const int*>(value);
    });
    MaterialService service;
    auto spriteAsset = Asset(Domain::Sprite);
    const auto a = service.Register(spriteAsset, Resources(spriteAsset, owner, 10));
    auto bResources = Resources(spriteAsset, owner, 11);
    bResources.textures.push_back({3, {31, 0}});
    const auto b = service.Register(spriteAsset, bResources);
    assert(service.SetParameter(b, "amount", 0.75f));
    owner.reset(); bResources.lifetime.reset();
    RenderFrame extracted;
    for(auto handle : {a, b, a}) {
        RenderItem item;
        item.mesh = {static_cast<std::uint32_t>(extracted.items.size() + 40), 0};
        item.material = service.Capture(handle);
        item.layer = RenderLayer::Transparent;
        item.viewDepth = static_cast<float>(extracted.items.size()); // must not reorder sprites
        extracted.items.push_back(std::move(item));
    }
    auto first = device.BeginFrame({.frameIndex = 1});
    ForwardRenderPipeline pipeline;
    assert(pipeline.Record(first, extracted, {}));
    assert(first.End(false));
    assert(device.async_SubmitFrameCommands(first.GetCommandBuffer()));
    extracted.items.clear(); service.Clear();
    assert(released == 0); // command buffer owns the snapshots, independent of ECS
    RHIDeviceTestAccess::Pump(device);
    assert(released == 1);
    assert(backend->draws.size() == 3);
    assert(backend->draws[0].pipeline == 10 && backend->draws[1].pipeline == 11 &&
        backend->draws[2].pipeline == 10);
    assert(backend->draws[0].value == 1 && backend->draws[1].value == 0.75f &&
        backend->draws[2].value == 1); // A/B/A share the UBO; each switch restores values
    assert(backend->draws[1].textures[3].IsValid());
    assert(!backend->draws[2].textures[3].IsValid()); // absent optional slot actively cleared

    // Reliable material-free calls retain their pipeline/texture behavior.
    RenderFrame legacy;
    RenderItem textured;
    textured.mesh = {40, 0}; textured.pipeline = {10, 0}; textured.texture = {30, 0};
    textured.order = RenderOrder::Submission;
    legacy.items.push_back(textured);
    textured.texture = {};
    legacy.items.push_back(textured);
    auto second = device.BeginFrame({.frameIndex = 2});
    assert(pipeline.Record(second, legacy, {}));
    assert(second.End(false));
    assert(device.async_SubmitFrameCommands(second.GetCommandBuffer()));
    RHIDeviceTestAccess::Pump(device);
    assert(!backend->draws.back().textures[0].IsValid());
}

void TestLatestFrameContainsCompleteParameters() {
    RHIDevice device;
    auto* backend = new RecordingBackend();
    RHIDeviceTestAccess::Install(device, backend);
    MaterialService service;
    auto asset = Asset();
    auto handle = service.Register(asset, Resources(asset, std::make_shared<int>(1)));
    ForwardRenderPipeline pipeline;
    for(int i = 0; i < 3; ++i) {
        assert(service.SetParameter(handle, "amount", float(i + 2)));
        RenderFrame extracted;
        RenderItem item;
        item.mesh = {40, 0}; item.material = service.Capture(handle);
        extracted.items.push_back(item);
        auto frame = device.BeginFrame({.frameIndex = static_cast<std::uint64_t>(i)});
        assert(pipeline.Record(frame, extracted, {}));
        assert(frame.End(false));
        assert(device.async_SubmitLatestFrameCommands(frame.GetCommandBuffer()));
    }
    RHIDeviceTestAccess::Pump(device);
    assert(backend->draws.size() == 1 && backend->draws[0].value == 4);
    assert(backend->uniformUpdates == 1);
}
}

int main() {
    TestPublicationAndValidation();
    TestRecordingAndRetention();
    TestLatestFrameContainsCompleteParameters();
    std::cout << "Material runtime tests passed\n";
}
