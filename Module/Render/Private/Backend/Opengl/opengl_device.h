#pragma once

#include "Render/Private/Backend/rhi_backend_executor.h"

namespace Render{
    class OpenglDevice : public IRHIBackendExecutor {
        public:
            virtual void bindDevice(RHIBackendExecutorBinding binding) override;
            virtual void initialize() override;
            virtual void executeFrame() override;
            virtual void shutdown() override;
            OpenglDevice();
            
    };
}