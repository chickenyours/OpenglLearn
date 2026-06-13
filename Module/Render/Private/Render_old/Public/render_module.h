#pragma once

#include <memory>
#include "module.h"
#include "Render/Public/RHI/RHI_device.h"

namespace Application {
    class ApplicationModule;
}

namespace Render::RHI {
    class RenderThread;
}

namespace Render {

    enum class GraphicsLibraryPlatformState {
        Uninitialized,
        PlatformReady,
        ContextReady,
        FunctionLoaderReady,
        Ready,
        Error
    };

    using GLProcAddressLoader = void* (*)(const char* name);

    class RenderModule final : public ::IModule {
    public:
        RenderModule();
        ~RenderModule() override;

        RenderModule(const RenderModule&) = delete;
        RenderModule& operator=(const RenderModule&) = delete;

        const char* GetName() const noexcept override;
        bool Startup() override;
        void Shutdown() override;
        bool IsStarted() const noexcept override;

        void SetPlatformReady(bool value) noexcept;
        void SetContextReady(bool value) noexcept;

        void SetGLProcAddressLoader(GLProcAddressLoader loader) noexcept;
        GLProcAddressLoader GetGLProcAddressLoader() const noexcept;

        GraphicsLibraryPlatformState GetPlatformState() const noexcept;

        bool CheckStartupRequirements() const noexcept;

        Render::RHI::Device* GetDevice() noexcept;
        const Render::RHI::Device* GetDevice() const noexcept;

        /**
         * @brief 获取渲染线程指针
         */
        Render::RHI::RenderThread* GetRenderThread() noexcept;
        const Render::RHI::RenderThread* GetRenderThread() const noexcept;

        bool InitializeFromApplication(const Application::ApplicationModule& app);

    private:
        bool started_ = false;
        bool platformReady_ = false;
        bool contextReady_ = false;
        bool functionLoaderReady_ = false;

        GLProcAddressLoader glProcLoader_ = nullptr;
        std::unique_ptr<Render::RHI::RenderThread> renderThread_;
        std::unique_ptr<Render::RHI::Device> device_;
        GraphicsLibraryPlatformState state_ = GraphicsLibraryPlatformState::Uninitialized;
    };

}
