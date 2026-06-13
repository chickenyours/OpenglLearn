#pragma once

#include "Render/Private/Backend/rhi_backend_executor.h"

namespace Render{
    class OpenglDevice : public IRHIBackendExecutor {
        private:
            RHIBackendExecutorBinding binding_;
        public:
            OpenglDevice(){}

            virtual void bindDevice(RHIBackendExecutorBinding binding) override {
                binding_ = binding;
            }
            virtual void Init() override {}
            virtual void executeFrame() override {
                // 消费队列
                
            }
            virtual void Shutdown() override {

            }
            
            
    };
}