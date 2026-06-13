#pragma once

#include <thread>

#include "object_ptr.h"
#include "Render/Public/render_backend_context.h"
#include "Render/Private/Backend/rhi_backend_context.h"
#include "Render/Private/BackEnd/backend.h"
#include "Render/Private/rhi_command_return_system.h"
#include "Render/Private/Backend/Opengl/backend.h"


namespace Render{
    
    

    using RHICommands =  RHICommandSystem<
    CreateVertexBufferCommand,
    DeleteVertexBufferCommand,
    BackDoorExecutionCommand
>;

    class RHIDevice{
        private:
            RHICommands commandSystem_;
            RHIResourcePool resourcePool_;
            ObjectPtr<IBackend> backend_;
            std::thread renderThread_;
            BackendType backendType;
            std::condition_variable rendercv_;
            std::mutex mtx;
            bool isRun = false;
        public:
            RHICommandReturnSystem returnSystem;
        private:
            void thread_run(BackendType type, OpenglBackendContext specData){
                // if(!specData) return; 
                std::unique_lock<std::mutex> lock(mtx);
                thread_InitBackend(type, &specData);
                if(!backend_) return;
                // 线程知道自己是否完全消费队列
                // bool done = true;
                while(isRun){
                    rendercv_.wait(lock, [&]() {
                        return !isRun || commandSystem_.hasPendingCommands();
                    });

                    if (!isRun) {
                        break;
                    }

                    commandSystem_.thread_SwitchQueues();

                    thread_ProcessCurrentQueue();
                }
                LOG_INFO("thread_run","out");
            }

            bool thread_ProcessCurrentQueue(){
                // 处理策略

                // 处理加载命令
                while(!commandSystem_.GetQueue<CreateVertexBufferCommand>().thread_IsConsumeQueueEmpty()){
                    CreateVertexBufferCommand command = commandSystem_.GetQueue<CreateVertexBufferCommand>().thread_Pop();
                    RenderResourceHandle<VertexBufferSpec> specHandle = backend_->CreateVertexBuffer(command);
                    // 返回结果队列
                    CreateVertexBufferCallback callback = command.OnFinish;
                    if(callback){
                        auto OnFinishCallback = [callback = std::move(callback),specHandle](){
                            callback(specHandle);
                        };
                        returnSystem.callbacks.push(OnFinishCallback);
                    }
                }

                // 处理后门命令
                while(!commandSystem_.GetQueue<BackDoorExecutionCommand>().thread_IsConsumeQueueEmpty()){
                    BackDoorExecutionCommand command = commandSystem_.GetQueue<BackDoorExecutionCommand>().thread_Pop();
                    // 直接执行命令
                    command.execution();
                    OnFinishCallback callback = command.OnFinish;
                    if(callback){
                        auto OnFinishCallback = [callback = std::move(callback)](){
                            callback();
                        };
                        returnSystem.callbacks.push(OnFinishCallback);
                    }
                }
                return true;
            }

            void thread_InitBackend(BackendType type, void* specData){
                RHIBackContext context;
                // context.commandSystem = &this->commandSystem_;
                context.resourcePool = &this->resourcePool_;
                switch (type)
                {
                case BackendType::Opengl :
                    backend_ = ObjectPtr<IBackend>(new OpenglBackend(context));
                    backend_->Init(specData);
                    break;
                
                default:
                    break;
                }
            }
        public:
            RHIDevice(){}
            ~RHIDevice(){
                StopAndRelease();
            }

            
            void Run(BackendType type, OpenglBackendContext specData){
                if(!isRun){
                    backendType = type;
                    isRun = true;
                    renderThread_ = std::thread(&RHIDevice::thread_run,this,type,specData);
                }
            }
            void StopAndRelease(){
                if(isRun){
                    isRun = false;
                    rendercv_.notify_one();
                    renderThread_.join();
                }
                
            }

        public:
            const RHIResourcePool& GetResourcePool(){return resourcePool_;}

        public:
            void async_CreateVertexBuffer(
                VertexLayout vertexLayout,
                uint32_t numVertex = 0,
                void* Data = nullptr,
                CreateVertexBufferCallback callback = nullptr
            ){
                CreateVertexBufferCommand commandPedding;
                commandPedding.vertexLayout = vertexLayout;
                commandPedding.numVertex = numVertex;
                commandPedding.Data = Data;
                commandPedding.OnFinish = callback;
                commandSystem_.GetQueue<CreateVertexBufferCommand>().threadAny_Push(1,&commandPedding);
                rendercv_.notify_one();
            }

            // void async_DeleteVertexBuffer(RenderResourceHandle<VertexBufferSpec> handle){
            //     DeleteVertexBufferCommand commandPedding;
            //     commandPedding.handle = handle;
            //     commandSystem_.
            // }

// #ifdef DEBUG
            void async_ExecuteCode(
                std::function<void()> execution,
                std::function<void()> OnFinish = nullptr
            ){
                BackDoorExecutionCommand commandPedding;
                commandPedding.execution = execution;
                commandPedding.OnFinish = OnFinish;
                commandSystem_.GetQueue<BackDoorExecutionCommand>().threadAny_Push(1,&commandPedding);
                rendercv_.notify_one();
            }
// #else
// #endif
        };

}