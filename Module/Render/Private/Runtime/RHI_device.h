#pragma once

#include <vector>
#include <deque>
#include <atomic>
#include <thread>
#include <mutex>
#include <optional>
#include <utility>
#include <functional>
#include <memory>
#include <string>
#include <variant>

#include "Render/Private/Runtime/RHI_command_system.h"
#include "Render/Private/Runtime/RHI_resouurce_registry.h"
#include "Render/Public/RHI/DeviceCommand/command_kit.h"
#include "Render/Public/RHI/RHI_graphic_api_desc.h"
#include "Render/Private/Backend/Opengl/opengl_device.h"
// #include "Render/Private/Backend/Opengl/opengl_device.h"
#include "engine/DebugTool/ConsoleHelp/color_log.h"

template<class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;


namespace Render{

    class RHIDevice;



   

    class RHIBackendDeviceBinding{
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

    
    

    class RenderThread{
        
        
    };


    class RHIDevice{
        private:
            RHICommandSystem c_;
            RHIResourceRegistry r_;
            RHIDeviceBackendType type = RHIDeviceBackendType::UnInit;
            std::variant<OpenGLDesc> graphicDesc;
        public:
            RHIDevice();
            bool Usable() const {

            }
            bool InitAsOpenGL(const OpenGLDesc desc){
                if(!desc.valid()){
                    return false;
                }
                type = RHIDeviceBackendType::OpenGL;
                graphicDesc = desc;
            }
            bool Activate(){
                if(type == RHIDeviceBackendType::UnInit){
                    LOG_ERROR("RHIDevice","Backend unInit!");
                    return false;
                }
                // 启动线程
                RunThread();
            }
        // ====================RenderThread====================
        private:
            std::thread renderThread_;
            std::atomic<bool> isRun_ = false;           // 线程是否应该运行信号
            std::atomic<bool> isThreadActive = false;   // 线程存活信号
            // thread state
            IRHIBackendExecutor* executor_;
        public:
            void RunThread(){
                // while(isRun_){
                //     SpinOnce();
                // }
                // isThreadActive = false;
                if(!std::exchange(isRun_,true)){
                    if(!isThreadActive){
                        renderThread_ = std::thread(&RHIDevice::thread_RenderMain,this);
                    }
                }
            }
        private:
            void thread_RenderMain(){
                isThreadActive = true;

                bool grahicContextTakeOk = false;
                std::visit(overloaded{
                    [&](const OpenGLDesc& desc){
                        grahicContextTakeOk = thread_TackOverGraphicExecutionAsOpengl(desc);
                    }
                },graphicDesc);

                if(grahicContextTakeOk){
                    LOG_ERROR("thread_RenderMain","error grahicContextTake");
                }

                // 初始化成功表示判断
                if(grahicContextTakeOk){
                    // 主逻辑
                    // 采用外部唤醒执行而非空转
                    thread_DoTask();
                }

                isThreadActive = false;
            }

            void thread_DoTask(){
                // 渲染线程唤醒时要做的任务
            }

            bool thread_TackOverGraphicExecutionAsOpengl(const OpenGLDesc& desc){
                executor_ = new OpenglDevice();
                return desc.makeCurrent();
            }
        // =====================================================  
    };
};