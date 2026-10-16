#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <imgui.h>
#include "module_base.h"
#include "Render/Public/render_backend_context.h"
#include "Render/Public/rhi_resource_handle.h"

struct GLFWwindow;
namespace Render { class RHIDevice; class RHIFrameEncoder; struct RHITextureSpec; }

namespace DebugUI {

struct FrameStatistics {
    std::uint32_t vertices = 0, indices = 0, draws = 0;
};

// All methods, including destruction, belong to the window's application thread.
// The device and GLFW window must outlive this module and any recorded frames.
class DebugUIModule final : public IModule {
public:
    DebugUIModule(Render::RHIDevice& device, GLFWwindow* window, Render::BackendType backend);
    ~DebugUIModule() override;
    DebugUIModule(const DebugUIModule&) = delete;
    DebugUIModule& operator=(const DebugUIModule&) = delete;

    const char* GetName() const noexcept override { return "DebugUI"; }
    bool Startup() override;
    void Shutdown() override;
    bool IsStarted() const noexcept override;

    bool BeginFrame();
    bool EndFrame();
    // Balance an abandoned BeginFrame and discard prepared work without upload.
    void CancelFrame() noexcept;
    bool Record(Render::RHIFrameEncoder& encoder, std::uint32_t framebufferWidth,
                std::uint32_t framebufferHeight);
    bool WantsMouse() const;
    bool WantsKeyboard() const;
    ImGuiContext* GetContext() const noexcept;
    const std::string& LastError() const noexcept;
    FrameStatistics LastFrameStatistics() const noexcept;

    // IDs are opaque registry keys, never native OpenGL/Vulkan objects. A strong
    // owner is required and pinned by prepared and recorded frames. Unregister
    // prevents future use without invalidating already prepared snapshots.
    ImTextureID RegisterTexture(Render::RenderResourceHandle<Render::RHITextureSpec> texture,
                                std::shared_ptr<const void> owner);
    bool UnregisterTexture(ImTextureID id);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace DebugUI
