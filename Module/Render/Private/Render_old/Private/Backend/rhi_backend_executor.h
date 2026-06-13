#pragma once

namespace Render{

    class RHICommandSystem;
    class RHIResourceRegistry;
    class RHIDevice;

    class RHIBackendExecutorBinding{
        public:
            RHICommandSystem& commands() const {
                return *commands_;
            }
            RHIResourceRegistry& resources() const {
                return *resources_;
            }
        private:
            friend class RHIDevice;
            RHICommandSystem* commands_ = nullptr;
            RHIResourceRegistry* resources_ = nullptr;
    };

    class IRHIBackendExecutor{
        public:
            virtual ~IRHIBackendExecutor() = default;
            virtual void bindDevice(RHIBackendExecutorBinding binding) = 0;
            virtual void Init() = 0;
            virtual void executeFrame() = 0;
            virtual void Shutdown() = 0;
            IRHIBackendExecutor();
    };
}