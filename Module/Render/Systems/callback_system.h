#pragma once

#include "ECS/System/system.h"

#include "Render/module.h"

namespace Render::System{
    class CallBackSystem : public ECS::System{
        private:
            ObjectWeakPtr<RHIDevice> device;
        public:
            virtual void OnStart() override {
                device = RenderModule::GetRHIDevice();
            }
            virtual void OnTick() override {
                if(device){
                    RHICommandReturnSystem& returnSystem = device->returnSystem;
                    ThreadSafeQueue<std::function<void()>>& callbacks = returnSystem.callbacks;
                    while(!callbacks.empty()){
                        auto callback = callbacks.wait_pop(); 
                        callback();
                    }
                }
            }
            virtual void OnEnd() override {
                
            }
    };
}