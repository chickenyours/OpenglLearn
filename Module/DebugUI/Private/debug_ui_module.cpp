#include "DebugUI/Public/debug_ui_module.h"
#include "DebugUI/Public/debug_ui_draw_data.h"
#include "DebugUI/Public/debug_ui_texture_registry.h"
#include "Render/Private/rhi_device.h"

#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <utility>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>

namespace DebugUI {
namespace {
using namespace Render;
using namespace std::chrono_literals;

constexpr const char* VertexShader = R"(#version 450 core
layout(location=0) in vec2 position;
layout(location=1) in vec2 texcoord;
layout(location=2) in vec4 vertexColor;
out vec2 vUV;
out vec4 vColor;
void main(){gl_Position=vec4(position,0,1);vUV=texcoord;vColor=vertexColor;})";
constexpr const char* FragmentShader = R"(#version 450 core
layout(binding=0) uniform sampler2D uiTexture;
in vec2 vUV;
in vec4 vColor;
out vec4 color;
void main(){color=texture(uiTexture,vUV)*vColor;})";

struct ResourceLease {
    RHIDevice& device;
    std::vector<RenderResourceHandle<VertexBufferSpec>> meshes;
    std::vector<RenderResourceHandle<RHITextureSpec>> textures;
    std::vector<RenderResourceHandle<ShaderSourceSpec>> sources;
    std::vector<RenderResourceHandle<ShaderProgramSpec>> programs;
    std::vector<RenderResourceHandle<PipelineSpec>> pipelines;
    explicit ResourceLease(RHIDevice& value) : device(value) {}
    ~ResourceLease() {
        if (!device.IsRunning()) return;
        for (auto handle : pipelines) device.async_DeletePipeline(handle);
        for (auto handle : meshes) device.async_DeleteVertexBuffer(handle);
        for (auto handle : textures) device.async_DeleteTexture(handle);
        for (auto handle : programs) device.async_DeleteShaderProgram(handle);
        for (auto handle : sources) device.async_DeleteShaderSource(handle);
    }
    template<class Spec> void Adopt(RenderResourceHandle<Spec> handle) {
        if (!handle.IsValid()) return;
        if constexpr (std::is_same_v<Spec, VertexBufferSpec>) meshes.push_back(handle);
        else if constexpr (std::is_same_v<Spec, RHITextureSpec>) textures.push_back(handle);
        else if constexpr (std::is_same_v<Spec, ShaderSourceSpec>) sources.push_back(handle);
        else if constexpr (std::is_same_v<Spec, ShaderProgramSpec>) programs.push_back(handle);
        else if constexpr (std::is_same_v<Spec, PipelineSpec>) pipelines.push_back(handle);
    }
};

// Each callback owns both completion and lease: a timeout may unwind the caller
// before the RHI finishes, without losing a late successful allocation.
template<class Spec, class Submit>
RenderResourceHandle<Spec> Create(RHIDevice& device, const std::shared_ptr<ResourceLease>& lease,
                                  Submit submit, const char* label) {
    struct Completion { RenderResourceHandle<Spec> handle; bool done = false; };
    auto completion = std::make_shared<Completion>();
    submit([completion, lease](RenderResourceHandle<Spec> handle) {
        lease->Adopt(handle);
        completion->handle = handle;
        completion->done = true;
    });
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!completion->done) {
        device.returnSystem.DrainCallbacks();
        if (completion->done) break;
        if (!device.IsRunning() || std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error(std::string("DebugUI RHI creation timed out: ") + label);
        // Do not poll GLFW here: callbacks could reenter the application while
        // ImGui is between NewFrame and Render or while settings are edited.
        device.returnSystem.WaitForCallbacks(2ms);
    }
    if (!completion->handle.IsValid())
        throw std::runtime_error(std::string("DebugUI RHI creation failed: ") + label);
    return completion->handle;
}

struct MeshSlot {
    std::shared_ptr<ResourceLease> owner;
    RenderResourceHandle<VertexBufferSpec> mesh;
};

struct PreparedFrame {
    Detail::DrawSnapshot data;
    std::shared_ptr<MeshSlot> slot;
    std::shared_ptr<ResourceLease> commonOwner;
    RenderResourceHandle<PipelineSpec> pipeline;
    std::vector<std::shared_ptr<const Detail::TextureBinding>> textures;
};
}

struct DebugUIModule::Impl {
    Render::RHIDevice& device;
    GLFWwindow* window;
    Render::BackendType backend;
    std::thread::id thread = std::this_thread::get_id();
    ImGuiContext* context = nullptr;
    ImGuiContext* previousContext = nullptr;
    bool platformInitialized = false, begun = false, started = false;
    std::string error;
    Detail::TextureRegistry registry;
    ImTextureID font = 0;
    std::shared_ptr<ResourceLease> commonOwner;
    RenderResourceHandle<PipelineSpec> pipeline;
    std::shared_ptr<PreparedFrame> prepared;
    std::vector<std::shared_ptr<MeshSlot>> meshSlots;
    FrameStatistics statistics;

    Impl(Render::RHIDevice& value, GLFWwindow* target, Render::BackendType type)
        : device(value), window(target), backend(type) {}
    bool Fail(std::string value) { error = std::move(value); return false; }
    bool OnThread() {
        return std::this_thread::get_id() == thread || Fail("DebugUI must run on its application thread");
    }
    void MakeCurrent() const { ImGui::SetCurrentContext(context); }

    std::shared_ptr<MeshSlot> AcquireMeshSlot(const CreateMeshBufferDesc& mesh) {
        constexpr std::size_t MaxMeshSlots = 3;
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        for (;;) {
            for (auto& candidate : meshSlots) {
                // The pool's reference is the only owner exactly when every
                // prepared/recorded frame and pending upload has retired. Only
                // this application thread can acquire a new slot reference.
                if (candidate.use_count() != 1) continue;
                auto slot = candidate;
                UpdateMeshBufferDesc update;
                update.vertexCount = mesh.vertexCount;
                update.vertexData = mesh.vertexData;
                update.vertexByteSize = mesh.vertexByteSize;
                update.indexCount = mesh.indexCount;
                update.indexData = mesh.indexData;
                update.indexByteSize = mesh.indexByteSize;
                update.indexType = mesh.indexType;
                struct Completion { bool done = false, succeeded = false; };
                auto completion = std::make_shared<Completion>();
                device.async_UpdateMeshBuffer(slot->mesh, update, [slot, completion](bool succeeded) {
                    // Retain the slot through a late callback even if this
                    // bounded wait times out or the module shuts down.
                    completion->succeeded = succeeded;
                    completion->done = true;
                });
                const auto uploadDeadline = std::chrono::steady_clock::now() + 10s;
                while (!completion->done) {
                    device.returnSystem.DrainCallbacks();
                    if (completion->done) break;
                    if (!device.IsRunning() || std::chrono::steady_clock::now() >= uploadDeadline)
                        throw std::runtime_error("DebugUI RHI frame geometry update timed out");
                    device.returnSystem.WaitForCallbacks(2ms);
                }
                if (!completion->succeeded)
                    throw std::runtime_error("DebugUI RHI frame geometry update failed");
                // Resource and frame queues are independent. Publication only
                // after this callback guarantees Record sees completed data.
                return slot;
            }
            if (meshSlots.size() < MaxMeshSlots) {
                auto slot = std::make_shared<MeshSlot>();
                slot->owner = std::make_shared<ResourceLease>(device);
                slot->mesh = Create<VertexBufferSpec>(device, slot->owner,
                    [&](auto cb) { device.async_CreateMeshBuffer(mesh, std::move(cb)); }, "frame geometry");
                // A timed-out creation never enters the reusable pool; its
                // callback lease still deletes a late successful allocation.
                meshSlots.push_back(slot);
                return slot;
            }
            device.returnSystem.DrainCallbacks();
            if (!device.IsRunning() || std::chrono::steady_clock::now() >= deadline)
                throw std::runtime_error("DebugUI mesh pool is busy; submit or discard recorded frames before preparing more UI");
            device.returnSystem.WaitForCallbacks(2ms);
        }
    }
};

DebugUIModule::DebugUIModule(Render::RHIDevice& device, GLFWwindow* window, Render::BackendType backend)
    : impl_(std::make_unique<Impl>(device, window, backend)) {}
DebugUIModule::~DebugUIModule() { Shutdown(); }

bool DebugUIModule::Startup() {
    auto& s = *impl_;
    if (!s.OnThread()) return false;
    if (s.started) return true;
    s.error.clear();
    if (!s.window || !s.device.IsRunning() || s.device.GetBackendType() != s.backend)
        return s.Fail("DebugUI requires a live GLFW window and matching running RHI device");
    try {
        IMGUI_CHECKVERSION();
        s.previousContext = ImGui::GetCurrentContext();
        s.context = ImGui::CreateContext();
        s.MakeCurrent();
        ImGui::StyleColorsDark();
        auto& io = ImGui::GetIO();
        io.BackendRendererName = "DebugUI_RHI";
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        // Callers may opt into persistence explicitly; do not create imgui.ini
        // in whichever working directory launched an engine tool.
        io.IniFilename = nullptr;
        s.platformInitialized = s.backend == BackendType::Vulkan
            ? ImGui_ImplGlfw_InitForVulkan(s.window, true)
            : ImGui_ImplGlfw_InitForOpenGL(s.window, true);
        if (!s.platformInitialized) throw std::runtime_error("DebugUI GLFW platform initialization failed");
        // Single-window UI only. Platform viewports need additional RHI window
        // targets and submissions, so they are deliberately not enabled here.
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        s.commonOwner = std::make_shared<ResourceLease>(s.device);
        CreateRHITextureSpec atlas;
        atlas.width = std::uint32_t(width);
        atlas.height = std::uint32_t(height);
        atlas.data = pixels;
        atlas.filterMode = RHIFilterMode::Linear;
        atlas.addressMode = RHIAddressMode::ClampToEdge;
        atlas.mipmaps = false;
        auto texture = Create<RHITextureSpec>(s.device, s.commonOwner,
            [&](auto cb) { s.device.async_CreateTexture(atlas, std::move(cb)); }, "font atlas");
        s.font = s.registry.Register(texture, s.commonOwner);
        if (!s.font) throw std::runtime_error("DebugUI font registration failed");
        io.Fonts->SetTexID(s.font);
        const auto source = [&](ShaderSourceType type, const char* code) {
            return Create<ShaderSourceSpec>(s.device, s.commonOwner,
                [&](auto cb) { s.device.async_CreateShaderSource({type, code, std::strlen(code)}, std::move(cb)); }, "shader source");
        };
        CreateGraphicShaderDesc shader;
        shader.vertexShaderSource = source(ShaderSourceType::Vertex, VertexShader);
        shader.fragmentShaderSource = source(ShaderSourceType::Fragment, FragmentShader);
        const auto program = Create<ShaderProgramSpec>(s.device, s.commonOwner,
            [&](auto cb) { s.device.async_CreateGraphicShader(shader, std::move(cb)); }, "shader program");
        CreatePipelineDesc pipeline;
        pipeline.spec.shaderProgram = program;
        pipeline.spec.expectVertexLayout.typeSlots = {VertexFieldType::Vec2, VertexFieldType::Vec2, VertexFieldType::Vec4};
        pipeline.spec.cullMode = CullMode::None;
        pipeline.spec.depthTest = false;
        pipeline.spec.depthWrite = false;
        pipeline.spec.blendEnable = true;
        pipeline.spec.blendMode = BlendMode::AlphaComposite;
        s.pipeline = Create<PipelineSpec>(s.device, s.commonOwner,
            [&](auto cb) { s.device.async_CreatePipeline(pipeline, std::move(cb)); }, "pipeline");
        s.started = true;
        return true;
    } catch (const std::exception& failure) {
        s.error = failure.what();
        Shutdown();
        return false;
    }
}

void DebugUIModule::Shutdown() {
    if (!impl_ || !impl_->OnThread()) return;
    auto& s = *impl_;
    CancelFrame();
    s.meshSlots.clear();
    s.registry.Clear();
    s.commonOwner.reset();
    s.pipeline = {};
    s.font = 0;
    if (s.context) {
        s.MakeCurrent();
        if (s.platformInitialized) ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext(s.context);
        ImGui::SetCurrentContext(s.previousContext);
    }
    s.context = nullptr;
    s.previousContext = nullptr;
    s.platformInitialized = false;
    s.begun = false;
    s.started = false;
    s.statistics = {};
}

bool DebugUIModule::IsStarted() const noexcept { return impl_->started; }
const std::string& DebugUIModule::LastError() const noexcept { return impl_->error; }
ImGuiContext* DebugUIModule::GetContext() const noexcept { return impl_->context; }
FrameStatistics DebugUIModule::LastFrameStatistics() const noexcept { return impl_->statistics; }

bool DebugUIModule::BeginFrame() {
    auto& s = *impl_;
    if (!s.OnThread()) return false;
    if (!s.started || s.begun) return s.Fail("DebugUI BeginFrame requires a started module without an open frame");
    s.error.clear();
    s.prepared.reset();
    s.MakeCurrent();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    s.begun = true;
    return true;
}

void DebugUIModule::CancelFrame() noexcept {
    auto& s = *impl_;
    if (std::this_thread::get_id() != s.thread) return;
    if (s.begun && s.context) {
        s.MakeCurrent();
        auto& io = ImGui::GetIO();
        const bool recovery = io.ConfigErrorRecovery;
        const bool recoveryAssert = io.ConfigErrorRecoveryEnableAssert;
        const bool recoveryLog = io.ConfigErrorRecoveryEnableDebugLog;
        // A panel exception may leave Begin/PushID stacks open. The default
        // recovery assert aborts before upstream can unwind those stacks, so
        // suppress it only during cancellation and retain diagnostic logging.
        io.ConfigErrorRecovery = true;
        io.ConfigErrorRecoveryEnableAssert = false;
        io.ConfigErrorRecoveryEnableDebugLog = true;
        // Keep cleanup noexcept even if an application-installed hook throws.
        try { ImGui::EndFrame(); } catch (...) {}
        io.ConfigErrorRecoveryEnableAssert = recoveryAssert;
        io.ConfigErrorRecoveryEnableDebugLog = recoveryLog;
        io.ConfigErrorRecovery = recovery;
    }
    s.begun = false;
    s.prepared.reset();
    s.statistics = {};
}

bool DebugUIModule::EndFrame() {
    auto& s = *impl_;
    if (!s.OnThread()) return false;
    if (!s.started || !s.begun) return s.Fail("DebugUI EndFrame requires BeginFrame");
    s.MakeCurrent();
    ImGui::Render();
    s.begun = false;
    s.prepared.reset();
    s.statistics = {};
    auto frame = std::make_shared<PreparedFrame>();
    if (!Detail::BuildDrawSnapshot(*ImGui::GetDrawData(), frame->data, s.error)) return false;
    frame->textures.reserve(frame->data.commands.size());
    for (const auto& command : frame->data.commands) {
        auto texture = command.resetState ? nullptr : s.registry.Find(command.texture);
        if (!command.resetState && !texture) {
            s.statistics = {};
            return s.Fail("DebugUI draw references an unregistered texture ID");
        }
        frame->textures.push_back(std::move(texture));
        if (!command.resetState) ++s.statistics.draws;
    }
    s.statistics.vertices = std::uint32_t(frame->data.vertices.size());
    s.statistics.indices = std::uint32_t(frame->data.indices.size());
    if (!s.statistics.draws) return true;
    try {
        frame->commonOwner = s.commonOwner;
        frame->pipeline = s.pipeline;
        CreateMeshBufferDesc mesh;
        mesh.vertexLayout.typeSlots = {VertexFieldType::Vec2, VertexFieldType::Vec2, VertexFieldType::Vec4};
        mesh.vertexCount = s.statistics.vertices;
        mesh.vertexData = frame->data.vertices.data();
        mesh.vertexByteSize = frame->data.vertices.size() * sizeof(Detail::Vertex);
        mesh.indexCount = s.statistics.indices;
        mesh.indexData = frame->data.indices.data();
        mesh.indexByteSize = frame->data.indices.size() * sizeof(std::uint32_t);
        mesh.indexType = IndexType::UInt32;
        mesh.usage = BufferUsage::Stream;
        // Stable RHI mesh identities also bound Vulkan's mesh-keyed native
        // pipeline cache, while frame-owned slot pins prevent early overwrite.
        frame->slot = s.AcquireMeshSlot(mesh);
        s.prepared = std::move(frame);
        return true;
    } catch (const std::exception& failure) { s.statistics = {}; return s.Fail(failure.what()); }
}

bool DebugUIModule::Record(Render::RHIFrameEncoder& encoder, std::uint32_t width, std::uint32_t height) {
    auto& s = *impl_;
    if (!s.OnThread()) return false;
    if (!s.prepared) return true; // Scene warmup / non-interactive rendering.
    if (!width || !height) { s.prepared.reset(); return true; }
    auto frame = s.prepared;
    if (frame->data.width != width || frame->data.height != height)
        return s.Fail("DebugUI framebuffer resized after BeginFrame; rebuild the UI frame before recording");
    if (!encoder.IsRecording() || !encoder.KeepAlive(frame)) return s.Fail("DebugUI requires a recording RHI encoder");
    const auto reset = [&] {
        return encoder.SetRenderTarget({{}, width, height, RHICommand::ClearNone}) &&
               encoder.SetViewport({0, 0, width, height}) &&
               encoder.SetScissor({false, 0, 0, width, height}) &&
               encoder.BindPipeline(frame->pipeline) && encoder.BindMesh(frame->slot->mesh);
    };
    if (!reset()) return s.Fail("DebugUI failed to record RHI render state");
    for (std::size_t i = 0; i < frame->data.commands.size(); ++i) {
        const auto& command = frame->data.commands[i];
        if (command.resetState) {
            if (!reset()) return s.Fail("DebugUI failed to reset RHI render state");
            continue;
        }
        if (!encoder.SetScissor({true, command.scissorX, command.scissorY, command.scissorWidth, command.scissorHeight}) ||
            !encoder.BindTexture(frame->textures[i]->texture, 0) ||
            !encoder.DrawIndexed({command.indexCount, command.firstIndex, command.baseVertex}))
            return s.Fail("DebugUI failed to record RHI draw commands");
    }
    if (!encoder.SetScissor({false, 0, 0, width, height})) return s.Fail("DebugUI failed to disable scissor");
    s.prepared.reset();
    return true;
}

bool DebugUIModule::WantsMouse() const {
    const auto& s = *impl_;
    if (!s.started) return false;
    s.MakeCurrent();
    return ImGui::GetIO().WantCaptureMouse;
}
bool DebugUIModule::WantsKeyboard() const {
    const auto& s = *impl_;
    if (!s.started) return false;
    s.MakeCurrent();
    return ImGui::GetIO().WantCaptureKeyboard;
}
ImTextureID DebugUIModule::RegisterTexture(Render::RenderResourceHandle<Render::RHITextureSpec> texture,
                                          std::shared_ptr<const void> owner) {
    auto& s = *impl_;
    if (!s.OnThread()) return 0;
    const auto id = s.registry.Register(texture, std::move(owner));
    if (!id) s.Fail("DebugUI texture registration requires a valid RHI handle and strong lifetime owner");
    return id;
}
bool DebugUIModule::UnregisterTexture(ImTextureID id) {
    auto& s = *impl_;
    if (!s.OnThread()) return false;
    if (id == s.font && id) return s.Fail("DebugUI owns the font atlas registration");
    return s.registry.Unregister(id);
}

} // namespace DebugUI
