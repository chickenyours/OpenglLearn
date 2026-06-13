#pragma once

#include "module_base.h"

#include "object_ptr.h"
#include "ApplicationWindow/public/window.h"

namespace ApplicationWindow{
    class ApplicationWindowModule : public IModule {
        private:
            inline static ObjectPtr<Window> window_ = nullptr;
            bool isStarted = false;
        public:
            virtual const char* GetName() const noexcept override {
                return "ApplicationWindowModule";
            }
            virtual bool Startup() override {
                // 创建窗口
                window_ = ObjectPtr<Window>(new Window());
                isStarted = window_->Activate();
                return isStarted;
            }
            virtual void Shutdown() override {
                
            }
            virtual bool IsStarted() const noexcept override {
                return isStarted;
            }
            static ObjectWeakPtr<Window> GetCurrentWindow(){
                if(window_){
                    return window_.GenWeakPtr();
                }
                return nullptr;
            }
    };
}