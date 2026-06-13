#pragma once

#include "module_base.h"
#include "object_ptr.h"
#include "Render/Private/rhi_device.h"

// 依赖ApplicationWindow模块
#include "ApplicationWindow/module.h"

namespace Render{
    class RenderModule : public IModule{
        private:
            inline static ObjectPtr<RHIDevice> device = nullptr;
            bool isStarted = false;
        public:

        virtual const char* GetName() const noexcept override{
            return "RenderModule";
        }
        virtual bool Startup() override{
            // 启动RHIDevice
            device = ObjectPtr<RHIDevice>(new RHIDevice());
            ObjectWeakPtr<ApplicationWindow::Window> window =
             ApplicationWindow::ApplicationWindowModule::GetCurrentWindow();
            if(window){
                auto spec = window->GetRenderContextAsOpengl();
                if(spec.has_value()){
                    OpenglBackendContext backendspecPedding = *spec;
                    device->Run(BackendType::Opengl, backendspecPedding);
                    isStarted = true;
                }
            }
            return false;
          
        }
        virtual void Shutdown() override{
            
        }
        virtual bool IsStarted() const noexcept override{
            return isStarted;
        }

        static ObjectWeakPtr<RHIDevice> GetRHIDevice(){
            if(device){
                return device.GenWeakPtr();
            }
            return nullptr;
        }
    };
}