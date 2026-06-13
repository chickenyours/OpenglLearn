#include "Render/Public/render_module.h"

#include "Application/public/application_module.h"
#include "Render/Private/Runtime/render_thread.h"

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

        functionLoaderReady_ = true;
        state_ = GraphicsLibraryPlatformState::FunctionLoaderReady;

        // 启动渲染线程
        renderThread_ = std::make_unique<Render::RHI::RenderThread>();
        if (!renderThread_->Start()) {
            state_ = GraphicsLibraryPlatformState::Error;
            return false;
        }

        // 创建 OpenGL 设备，传入渲染线程
        device_ = Render::RHI::CreateOpenGLDevice(renderThread_.get());
        if (!device_) {
            renderThread_->Stop();
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

        // 获取 OpenGL 上下文和窗口指针
        void* glContext = const_cast<Application::ApplicationModule&>(app).GetGLContext();
        GLFWwindow* window = const_cast<Application::ApplicationModule&>(app).GetWindow();

        if (!glContext || !window) {
            state_ = GraphicsLibraryPlatformState::Error;
            return false;
        }

        // 先创建渲染线程（但不启动）
        renderThread_ = std::make_unique<Render::RHI::RenderThread>();
        
        // 设置 OpenGL 上下文和窗口（在启动前设置）
        renderThread_->SetGLContext(window, glContext);

        // 释放主线程的 OpenGL 上下文，交给渲染线程使用
        const_cast<Application::ApplicationModule&>(app).ReleaseGLContext();

        // 现在启动渲染线程
        if (!renderThread_->Start()) {
            state_ = GraphicsLibraryPlatformState::Error;
            return false;
        }

        // 创建 OpenGL 设备，传入渲染线程
        device_ = Render::RHI::CreateOpenGLDevice(renderThread_.get());
        if (!device_) {
            renderThread_->Stop();
            state_ = GraphicsLibraryPlatformState::Error;
            return false;
        }

        started_ = true;
        state_ = GraphicsLibraryPlatformState::Ready;
        return true;
    }

    void RenderModule::Shutdown() {
        if (!started_) {
            return;
        }

        // 1. 释放 device_
        device_.reset();

        // 2. 停止渲染线程
        if (renderThread_) {
            renderThread_->Stop();
            renderThread_.reset();
        }

        // 3. 将 started_ 设回 false
        started_ = false;

        // 4. 清理加载器指针状态
        functionLoaderReady_ = false;

        // 5. 把平台状态改回合适值
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

    Render::RHI::RenderThread* RenderModule::GetRenderThread() noexcept {
        return renderThread_.get();
    }

    const Render::RHI::RenderThread* RenderModule::GetRenderThread() const noexcept {
        return renderThread_.get();
    }

}
