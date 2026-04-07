#include "Render/Public/render_module.h"

#include "Application/public/application_module.h"

namespace Render {

    RenderModule::RenderModule() = default;

    RenderModule::~RenderModule() {
        Shutdown();
    }

    const char* RenderModule::GetName() const noexcept {
        return "Render";
    }

    bool RenderModule::Startup() {
        if (started_) {
            return true;
        }

        if (!CheckStartupRequirements()) {
            state_ = GraphicsLibraryPlatformState::Error;
            return false;
        }

        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glProcLoader_))) {
            state_ = GraphicsLibraryPlatformState::Error;
            return false;
        }

        functionLoaderReady_ = true;
        state_ = GraphicsLibraryPlatformState::FunctionLoaderReady;

        device_ = Render::RHI::CreateOpenGLDevice();
        if (!device_) {
            state_ = GraphicsLibraryPlatformState::Error;
            return false;
        }

        started_ = true;
        state_ = GraphicsLibraryPlatformState::Ready;
        return true;
    }

    bool RenderModule::InitializeFromApplication(const Application::ApplicationModule& app) {
        if (started_) {
            return true;
        }

        if (!app.IsStarted()) {
            state_ = GraphicsLibraryPlatformState::Error;
            return false;
        }

        SetPlatformReady(true);
        SetContextReady(true);
        SetGLProcAddressLoader(app.GetGLProcAddressLoader());

        return Startup();
    }

    void RenderModule::Shutdown() {
        if (!started_) {
            return;
        }

        // 1. 释放 device_
        device_.reset();

        // 2. 将 started_ 设回 false
        started_ = false;

        // 3. 清理加载器指针状态
        functionLoaderReady_ = false;

        // 4. 把平台状态改回合适值
        if (platformReady_) {
            state_ = contextReady_
                ? GraphicsLibraryPlatformState::ContextReady
                : GraphicsLibraryPlatformState::PlatformReady;
        } else {
            state_ = GraphicsLibraryPlatformState::Uninitialized;
        }
    }

    bool RenderModule::IsStarted() const noexcept {
        return started_;
    }

    void RenderModule::SetPlatformReady(bool value) noexcept {
        platformReady_ = value;
        if (value && state_ == GraphicsLibraryPlatformState::Uninitialized) {
            state_ = GraphicsLibraryPlatformState::PlatformReady;
        }
    }

    void RenderModule::SetContextReady(bool value) noexcept {
        contextReady_ = value;
        if (value && state_ == GraphicsLibraryPlatformState::PlatformReady) {
            state_ = GraphicsLibraryPlatformState::ContextReady;
        }
    }

    void RenderModule::SetGLProcAddressLoader(GLProcAddressLoader loader) noexcept {
        glProcLoader_ = loader;
    }

    GLProcAddressLoader RenderModule::GetGLProcAddressLoader() const noexcept {
        return glProcLoader_;
    }

    GraphicsLibraryPlatformState RenderModule::GetPlatformState() const noexcept {
        return state_;
    }

    bool RenderModule::CheckStartupRequirements() const noexcept {
        return platformReady_ && contextReady_ && glProcLoader_ != nullptr;
    }

    Render::RHI::Device* RenderModule::GetDevice() noexcept {
        return device_.get();
    }

    const Render::RHI::Device* RenderModule::GetDevice() const noexcept {
        return device_.get();
    }

}
