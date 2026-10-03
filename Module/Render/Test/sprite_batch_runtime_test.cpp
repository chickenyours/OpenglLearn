#include "Render/Public/Sprite/sprite_batch.h"
#include "Render/Public/Sprite/sprite_batch_geometry.h"
#include "Render/Private/rhi_device.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

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
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool Near(float first, float second) { return std::abs(first - second) < .00001f; }

// No window or GL context. The real RHIDevice resource and frame queues execute
// against this recording backend, including their actual callback dispatch.
class RecordingBackend final : public OpenglBackend {
public:
    RecordingBackend() : OpenglBackend({}) {}
    unsigned creates = 0, deletes = 0, uploads = 0, draws = 0, frames = 0;
    bool failUpload = false;
    uint32_t uploadedIndices = 0;
    std::vector<SpriteDetail::SpriteVertex> uploadedVertices;
    std::vector<unsigned char> atlasPixels;
    int atlasWidth = 0, atlasHeight = 0;
    template<class T> RenderResourceHandle<T> Handle() { return {++creates, 0}; }
    RenderResourceHandle<VertexBufferSpec> CreateVertexBuffer(const CreateVertexBufferCommand&) override {
        return Handle<VertexBufferSpec>();
    }
    bool UpdateVertexBuffer(const UpdateVertexBufferCommand& command) override {
        ++uploads;
        uploadedIndices = command.numIndex;
        Require(command.vertexData.size() > 0 && command.indexData.size() == command.numIndex * sizeof(uint32_t), "mesh upload lost copied bytes");
        Require(command.vertexData.size() == command.numVertex * sizeof(SpriteDetail::SpriteVertex), "sprite vertex stride changed");
        uploadedVertices.resize(command.numVertex);
        std::memcpy(uploadedVertices.data(), command.vertexData.data(), command.vertexData.size());
        return !failUpload;
    }
    RenderResourceHandle<RHITextureSpec> CreateTexture(const CreateTextureCommand& command) override {
        Require(command.data.size() == size_t(command.spec.width) * command.spec.height * 4, "atlas upload lost copied bytes");
        atlasWidth = command.spec.width; atlasHeight = command.spec.height;
        atlasPixels.resize(command.data.size());
        std::memcpy(atlasPixels.data(), command.data.data(), command.data.size());
        return Handle<RHITextureSpec>();
    }
    RenderResourceHandle<ShaderSourceSpec> CreateShaderSource(const CreateShaderSourceCommand&) override {
        return Handle<ShaderSourceSpec>();
    }
    RenderResourceHandle<ShaderProgramSpec> CreateGraphicShaderProgram(const CreateGraphicShaderProgramCommand&) override {
        return Handle<ShaderProgramSpec>();
    }
    RenderResourceHandle<PipelineSpec> CreatePipeline(const CreatePipelineCommand& command) override {
        Require(!command.desc.spec.depthTest && command.desc.spec.blendEnable, "sprite pipeline depth/blend state incorrect");
        return Handle<PipelineSpec>();
    }
    void DeleteVertexBuffer(const DeleteVertexBufferCommand&) override { ++deletes; }
    void DeleteTexture(const DeleteTextureCommand&) override { ++deletes; }
    void DeleteShaderSource(const DeleteShaderSourceCommand&) override { ++deletes; }
    void DeleteShaderProgram(const DeleteShaderProgramCommand&) override { ++deletes; }
    void DeletePipeline(const DeletePipelineCommand&) override { ++deletes; }
    void BeginFrame(const RHICommand::BeginFrame&) override { ++frames; }
    void EndFrame(const RHICommand::EndFrame&) override {}
    void SetPipeline(const RHICommand::SetPipeline&) override {}
    void SetVertexBuffer(const RHICommand::SetVertexBuffer&) override {}
    void BindTexture(const RHICommand::BindTexture&) override {}
    void DrawIndexed(const RHICommand::DrawIndexed& command) override {
        Require(uploads > 0 && command.indexCount == uploadedIndices, "draw executed without its mesh upload");
        ++draws;
    }
};

struct Fixture {
    ObjectPtr<RHIDevice> device{new RHIDevice};
    RecordingBackend* backend = new RecordingBackend;
    Fixture() { RHIDeviceTestAccess::Install(*device, backend); }
    void Pump(int iterations = 1) {
        for (int i = 0; i < iterations; ++i) {
            RHIDeviceTestAccess::Pump(*device);
            device->returnSystem.DrainCallbacks();
        }
    }
};

struct TemporarySheet {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("sprite-region-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".png");
    TemporarySheet() {
        std::array<unsigned char, 8 * 4 * 4> pixels{};
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 8; ++x) {
            auto* pixel = pixels.data() + (y * 8 + x) * 4;
            pixel[0] = x < 4 ? 255 : 0;
            pixel[1] = x < 4 ? 0 : static_cast<unsigned char>(20 + (x - 4) * 50);
            pixel[2] = x < 4 ? 0 : static_cast<unsigned char>(20 + y * 50);
            pixel[3] = 255;
        }
        std::ofstream output(path, std::ios::binary);
        Require(bool(output), "cannot create test sprite sheet");
        const auto write = [](void* context, void* data, int size) {
            static_cast<std::ofstream*>(context)->write(static_cast<const char*>(data), size);
        };
        Require(stbi_write_png_to_func(write, &output, 8, 4, 4, pixels.data(), 8 * 4) != 0 && bool(output),
                "cannot encode test sprite sheet");
    }
    ~TemporarySheet() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
};

void TestSpriteRegionUploadsFrameRelativeUVs() {
    TemporarySheet sheet;
    Fixture fixture;
    SpriteBatch2D batch(fixture.device.GenWeakPtr());
    Require(batch.Initialize({{"sheet", sheet.path, 2, 1}}), "cannot initialize real PNG sprite sheet");
    fixture.Pump(10);
    Require(batch.Ready() && batch.Begin(10, 10), "cannot begin region test batch");
    Require(batch.Sprite("sheet", {-6, 0}, {4, 4}, 0, {1, 1, 1, 1}, false, false, 0), "cannot draw first frame");
    Require(batch.Sprite("sheet", {0, 0}, {4, 4}, 0, {1, 1, 1, 1}, false, false, 1), "cannot draw second frame");
    const glm::vec4 child{.25f, .5f, .5f, .25f}, tint{.5f, .75f, 1, .8f};
    Require(batch.SpriteRegion("sheet", {6, 0}, {4, 2}, child, 1.57079632679f, tint, true, true, 1),
            "cannot draw subregion in selected frame");
    auto frame = fixture.device->BeginFrame({});
    Require(batch.Flush(frame) && frame.End(false) && batch.Submit(frame.GetCommandBuffer()), "cannot submit region frame");
    fixture.Pump(4);
    const auto& vertices = fixture.backend->uploadedVertices;
    Require(fixture.backend->draws == 1 && vertices.size() == 12 && fixture.backend->uploadedIndices == 18,
            "sprite region did not produce one ordinary indexed quad");
    const auto frameTopLeft = vertices[7].uv;
    const auto frameExtent = vertices[5].uv - frameTopLeft;
    Require(Near(frameExtent.x * fixture.backend->atlasWidth, 4) && Near(frameExtent.y * fixture.backend->atlasHeight, 4),
            "sprite sheet was not packed as independent extruded frames");
    const glm::vec2 childTopLeft = frameTopLeft + glm::vec2(child) * frameExtent;
    const glm::vec2 childExtent = glm::vec2(child.z, child.w) * frameExtent;
    // Both flips: bottom-left geometry samples top-right of the selected crop.
    Require(Near(vertices[8].uv.x, childTopLeft.x + childExtent.x) && Near(vertices[8].uv.y, childTopLeft.y),
            "uploaded region UVs are not relative to the selected atlas frame");
    Require(Near(vertices[10].uv.x, childTopLeft.x) && Near(vertices[10].uv.y, childTopLeft.y + childExtent.y),
            "uploaded region flips escaped its crop");
    Require(Near(vertices[8].position.x, .7f) && Near(vertices[8].position.y, -.2f) && vertices[8].color == tint,
            "region lost rotation, placement or tint during actual mesh upload");
    for (std::size_t index = 8; index < 12; ++index)
        Require(vertices[index].uv.x >= frameTopLeft.x && vertices[index].uv.x <= frameTopLeft.x + frameExtent.x &&
                vertices[index].uv.y >= frameTopLeft.y && vertices[index].uv.y <= frameTopLeft.y + frameExtent.y,
                "region UVs leaked into another atlas frame");
    auto redAt = [&](glm::vec2 uv) {
        const int x = int(uv.x * fixture.backend->atlasWidth), y = int(uv.y * fixture.backend->atlasHeight);
        return fixture.backend->atlasPixels[(std::size_t(y) * fixture.backend->atlasWidth + x) * 4];
    };
    Require(redAt((vertices[0].uv + vertices[2].uv) * .5f) == 255 &&
            redAt((vertices[4].uv + vertices[6].uv) * .5f) == 0,
            "decoded PNG frame selection does not match uploaded UVs");
    Require(batch.Begin(10, 10), "cannot begin validation frame");
    for (glm::vec4 bad : {glm::vec4(-.1f, 0, 1, 1), {0, 0, 0, 1}, {.75f, 0, .5f, 1},
                         {0, 0, 1, std::numeric_limits<float>::infinity()}})
        Require(!batch.SpriteRegion("sheet", {}, {1, 1}, bad), "runtime accepted an invalid normalized region");
    Require(!batch.Error().empty(), "invalid runtime region did not report an error");
    Require(!batch.SpriteRegion("sheet", {}, {1, 1}, {0, 0, 1, 1}, 0, {1, 1, 1, 1}, false, false, 2),
            "runtime accepted an invalid sprite sheet frame");
    batch.Shutdown(); fixture.Pump(3);
    Require(fixture.backend->creates == fixture.backend->deletes, "region frame or error path leaked GPU resources");
}

void TestUploadDependencyAndInFlightExclusion() {
    Fixture fixture;
    SpriteBatch2D batch(fixture.device.GenWeakPtr());
    Require(batch.Initialize({}), "initialization was not accepted");
    Require(!batch.Ready(), "batch ready before GPU callbacks");
    fixture.Pump(10);
    Require(batch.Ready() && fixture.backend->creates == 6, "GPU creation chain did not finish");
    Require(batch.Begin(10, 10), "cannot begin ready batch");
    batch.Rect({0, 0}, {2, 2}, {1, 1, 1, 1});
    batch.Text("HP: 12\nWAVE 1", {-9, 8}, .2f, {1, 1, 1, 1});
    auto frame = fixture.device->BeginFrame({});
    Require(batch.Flush(frame) && frame.End(false), "cannot record batch");
    int completions = 0;
    Require(batch.Submit(frame.GetCommandBuffer(), [&] { ++completions; }), "cannot submit batch");
    Require(!batch.Ready() && !batch.Begin(10, 10), "in-flight dynamic mesh can be overwritten");
    RHIDeviceTestAccess::Pump(*fixture.device);
    Require(fixture.backend->uploads == 1 && fixture.backend->draws == 0, "draw submitted before upload callback");
    fixture.device->returnSystem.DrainCallbacks();
    Require(fixture.backend->draws == 0 && completions == 0, "draw/completion occurred before frame execution");
    fixture.Pump();
    Require(fixture.backend->draws == 1 && completions == 1 && batch.Ready(), "frame did not retire correctly");

    // Empty frames still submit/present without uploading an empty invalid mesh.
    Require(batch.Begin(10, 10), "cannot begin empty batch");
    auto empty = fixture.device->BeginFrame({});
    Require(batch.Flush(empty) && empty.End(false), "cannot record empty frame");
    Require(batch.Submit(empty.GetCommandBuffer(), [&] { ++completions; }), "cannot submit empty frame");
    fixture.Pump();
    Require(fixture.backend->uploads == 1 && fixture.backend->frames == 2 && completions == 2, "empty frame behavior incorrect");
    batch.Shutdown();
    fixture.Pump(3);
    Require(fixture.backend->deletes == fixture.backend->creates, "completed batch leaked GPU resources");
}

void TestShutdownDuringInitialization() {
    Fixture fixture;
    {
        SpriteBatch2D batch(fixture.device.GenWeakPtr());
        Require(batch.Initialize({}), "initialization was not accepted");
        batch.Shutdown();
    }
    fixture.Pump(10);
    Require(fixture.backend->creates == 3 && fixture.backend->deletes == 3, "shutdown continued shader chain or leaked pending resources");
}

void TestShutdownRetainsSubmittedFrame() {
    Fixture fixture;
    auto batch = std::make_unique<SpriteBatch2D>(fixture.device.GenWeakPtr());
    Require(batch->Initialize({}), "initialization was not accepted");
    fixture.Pump(10);
    Require(batch->Begin(10, 10), "cannot begin ready batch");
    batch->Rect({0, 0}, {2, 2}, {1, 1, 1, 1});
    auto frame = fixture.device->BeginFrame({});
    Require(batch->Flush(frame) && frame.End(false), "cannot record frame");
    int completions = 0;
    Require(batch->Submit(frame.GetCommandBuffer(), [&] { ++completions; }), "cannot submit frame");
    fixture.Pump(); // upload completes and the callback submits the frame
    batch.reset();
    Require(fixture.backend->deletes == 0, "resources released before their recorded frame");
    fixture.Pump(5);
    Require(fixture.backend->draws == 1 && completions == 1, "shutdown discarded accepted GPU frame");
    Require(fixture.backend->deletes == fixture.backend->creates, "submitted frame lifetime leaked resources");
}

void TestFailedUploadCancelsFrame() {
    Fixture fixture;
    SpriteBatch2D batch(fixture.device.GenWeakPtr());
    Require(batch.Initialize({}), "initialization was not accepted");
    fixture.Pump(10);
    Require(batch.Begin(10, 10), "cannot begin ready batch");
    batch.Rect({0, 0}, {2, 2}, {1, 1, 1, 1});
    auto frame = fixture.device->BeginFrame({});
    Require(batch.Flush(frame) && frame.End(false), "cannot record frame");
    fixture.backend->failUpload = true;
    int completions = 0;
    Require(batch.Submit(frame.GetCommandBuffer(), [&] { ++completions; }), "cannot submit frame");
    fixture.Pump(3);
    Require(completions == 1 && fixture.backend->draws == 0 && !batch.Error().empty(), "failed upload drew stale geometry or lost its completion");
    batch.Shutdown();
    fixture.Pump(3);
    Require(fixture.backend->deletes == fixture.backend->creates, "failed upload kept its frame/resource lease alive");
}
}

int main() {
    try {
        TestUploadDependencyAndInFlightExclusion();
        TestShutdownDuringInitialization();
        TestShutdownRetainsSubmittedFrame();
        TestFailedUploadCancelsFrame();
        TestSpriteRegionUploadsFrameRelativeUVs();
        std::cout << "Sprite RHI dependency, lifecycle and region upload tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
