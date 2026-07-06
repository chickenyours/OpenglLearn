#pragma once

#include <thread>

#include "object_ptr.h"
#include "Render/Public/render_backend_context.h"
#include "Render/Public/RHICommand/FrameCommand/rhi_frame_command.h"
#include "Render/Public/RHICommand/FrameCommand/rhi_frame_command_buffer.h"
#include "Render/Public/RHICommand/rhi_pipeline_command.h"
#include "Render/Public/RHICommand/rhi_shader_command.h"
#include "Render/Private/Backend/rhi_backend_context.h"
#include "Render/Private/BackEnd/backend.h"
#include "Render/Private/rhi_command_return_system.h"
#include "Render/Private/Backend/Opengl/backend.h"
#include "Render/Public/RHIResourceType/Shader/shader.h"


namespace Render{
    
    

    using RHICommands =  RHICommandSystem<
    CreateVertexBufferCommand,
    CreateShaderSourceCommand,
    CreateGraphicShaderProgramCommand,
    CreatePipelineCommand,
    DeletePipelineCommand,
    DeleteVertexBufferCommand,
    BackDoorExecutionCommand,
    FrameCommands
>;

    class RHIDevice{
        private:
            RHICommands commandSystem_;
            RHIResourcePool resourcePool_;
            RHIFrameCommandBufferPool frameCommandPool;
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

            void thread_ProcessFrameCommands(RHIFrameCommandBuffer& buffer){
                std::vector<std::byte>& bitbuffer = buffer.bytebuffer_;
                size_t cur_memory_p = 0;
                for(size_t i = 0; i < buffer.commandCount; i++){
                    // 获取命令
                    CommandId* id = reinterpret_cast<CommandId*>(bitbuffer.data() + cur_memory_p);
                    cur_memory_p += sizeof(CommandId);
                    // 处理命令
                    void* commandAddr;
                    switch (id->value)
                    {
                    case 0:
                        static_assert(RHICommand::FrameCommandsSet::id_is<CommandId{0},RHICommand::SetBackgroundColor>,
                            "id 0 should be SetBackgroundColorCommand"
                        );
                        commandAddr = bitbuffer.data() + cur_memory_p;
                        cur_memory_p += sizeof(RHICommand::SetBackgroundColor);
                        backend_->SetBackgroundColor(*reinterpret_cast<RHICommand::SetBackgroundColor*>(commandAddr));
                        break;
                    case 1:
                        static_assert(RHICommand::FrameCommandsSet::id_is<CommandId{1},RHICommand::Flip>,
                            "id 1 should be FlipCommand"
                        );
                        commandAddr = bitbuffer.data() + cur_memory_p;
                        cur_memory_p += sizeof(RHICommand::Flip);
                        backend_->Flip(*reinterpret_cast<RHICommand::Flip*>(commandAddr));
                        break;
                    case 2:
                        static_assert(RHICommand::FrameCommandsSet::id_is<CommandId{2},RHICommand::SetVertexBuffer>,
                            "id 2 should be FlipCommand"
                        );
                        commandAddr = bitbuffer.data() + cur_memory_p;
                        cur_memory_p += sizeof(RHICommand::SetVertexBuffer);
                        backend_->SetVertexBuffer(*reinterpret_cast<RHICommand::SetVertexBuffer*>(commandAddr));
                        break;
                    case 3:
                        static_assert(RHICommand::FrameCommandsSet::id_is<CommandId{3},RHICommand::SetPipeline>,
                            "id 3 should be FlipCommand"
                        );
                        commandAddr = bitbuffer.data() + cur_memory_p;
                        cur_memory_p += sizeof(RHICommand::SetPipeline);
                        backend_->SetPipeline(*reinterpret_cast<RHICommand::SetPipeline*>(commandAddr));
                        break;
                    case 4:
                        static_assert(RHICommand::FrameCommandsSet::id_is<CommandId{4},RHICommand::Draw>,
                            "id 4 should be Draw"
                        );
                        commandAddr = bitbuffer.data() + cur_memory_p;
                        cur_memory_p += sizeof(RHICommand::Draw);
                        backend_->Draw(*reinterpret_cast<RHICommand::Draw*>(commandAddr));
                        break;
                    case 5:
                        static_assert(RHICommand::FrameCommandsSet::id_is<CommandId{5},RHICommand::DrawRect>,
                            "id 5 should be Draw"
                        );
                        commandAddr = bitbuffer.data() + cur_memory_p;
                        cur_memory_p += sizeof(RHICommand::DrawRect);
                        backend_->DrawRect(*reinterpret_cast<RHICommand::DrawRect*>(commandAddr));
                        break;
                    default:
                        LOG_FATAL("invaild command","666");
                        break;
                    }
                }
                // buffer.bytebuffer_
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

                // 处理着色器命令
                while(!commandSystem_.GetQueue<CreateGraphicShaderProgramCommand>().thread_IsConsumeQueueEmpty()){
                    CreateGraphicShaderProgramCommand command = commandSystem_.GetQueue<CreateGraphicShaderProgramCommand>().thread_Pop();
                    RenderResourceHandle<ShaderProgramSpec> specHandle = backend_->CreateGraphicShaderProgram(command);
                    auto& callback = command.OnFinish;
                    if(callback){
                        auto OnFinishCallback = [callback = std::move(callback),specHandle](){
                            callback(specHandle);
                        };
                        returnSystem.callbacks.push(OnFinishCallback);
                    }
                }
                while(!commandSystem_.GetQueue<CreateShaderSourceCommand>().thread_IsConsumeQueueEmpty()){
                    CreateShaderSourceCommand command = commandSystem_.GetQueue<CreateShaderSourceCommand>().thread_Pop();
                    RenderResourceHandle<ShaderSourceSpec> specHandle = backend_->CreateShaderSource(command);
                    auto& callback = command.OnFinish;
                    if(callback){
                        auto OnFinishCallback = [callback = std::move(callback),specHandle](){
                            callback(specHandle);
                        };
                        returnSystem.callbacks.push(OnFinishCallback);
                    }
                }

                // 处理管线命令
                while(!commandSystem_.GetQueue<CreatePipelineCommand>().thread_IsConsumeQueueEmpty()){
                    CreatePipelineCommand command = commandSystem_.GetQueue<CreatePipelineCommand>().thread_Pop();
                    RenderResourceHandle<PipelineSpec> specHandle = backend_->CreatePipeline(command);
                    auto& callback = command.OnFinish;
                    if(callback){
                        auto OnFinishCallback = [callback = std::move(callback),specHandle](){
                            callback(specHandle);
                        };
                        returnSystem.callbacks.push(OnFinishCallback);
                    }
                }
                while(!commandSystem_.GetQueue<DeletePipelineCommand>().thread_IsConsumeQueueEmpty()){
                    DeletePipelineCommand command = commandSystem_.GetQueue<DeletePipelineCommand>().thread_Pop();
                    backend_->DeletePipeline(command);
                    auto& callback = command.OnFinish;
                    returnSystem.callbacks.push(command.OnFinish);
                }
                // 处理帧渲染命令
                while(!commandSystem_.GetQueue<FrameCommands>().thread_IsConsumeQueueEmpty()){
                    FrameCommands command = commandSystem_.GetQueue<FrameCommands>().thread_Pop();
                    ObjectWeakPtr<RHIFrameCommandBuffer>& buffer = command.buffer;
                    if(buffer){
                        thread_ProcessFrameCommands(*buffer);
                        buffer->Reset();
                        frameCommandPool.threadAny_Recycle(buffer);
                        if(command.OnFinish){
                            returnSystem.callbacks.push(command.OnFinish);
                        }
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
                CreateVertexBufferCommand commandPending;
                commandPending.vertexLayout = vertexLayout;
                commandPending.numVertex = numVertex;
                commandPending.Data = Data;
                commandPending.OnFinish = callback;
                commandSystem_.GetQueue<CreateVertexBufferCommand>().threadAny_Push(1,&commandPending);
                rendercv_.notify_one();
            }

            // 基本异步命令,外部需要提前异步加载ShaderSourceSpec
            void async_CreateGraphicShader(
                const CreateGraphicShaderDesc& desc,
                OnCreateGraphicShaderFinish OnFinish = nullptr
            ){
                CreateGraphicShaderProgramCommand commandPending;
                commandPending.createDesc = desc;
                commandPending.OnFinish = OnFinish;
                commandSystem_.GetQueue<CreateGraphicShaderProgramCommand>().threadAny_Push(1,&commandPending);
                rendercv_.notify_one();
            }

            void async_CreateShaderSource(
                const CreateShaderSourceDesc& desc,
                OnCreateShaderSourceFinish OnFinish = nullptr
            ){
                CreateShaderSourceCommand commandPedding;
                commandPedding.createDesc = desc;
                commandPedding.OnFinish = OnFinish;
                commandSystem_.GetQueue<CreateShaderSourceCommand>().threadAny_Push(1,&commandPedding);
                rendercv_.notify_one();
            }

            void async_CreatePipeline(
                const CreatePipelineDesc& desc,
                OnCreatePipelineCommandFinish OnFinish
            ){
                CreatePipelineCommand commandPedding;
                commandPedding.desc = desc;
                commandPedding.OnFinish = OnFinish;
                commandSystem_.GetQueue<CreatePipelineCommand>().threadAny_Push(1,&commandPedding);
                rendercv_.notify_one();
            }

            void async_DeletePipeline(
               RenderResourceHandle<PipelineSpec> handle,
               OnDeletePipelineCommandFinish OnFinish
            ){
                DeletePipelineCommand commandPedding;
                commandPedding.handle = handle;
                commandPedding.OnFinish = OnFinish;
                commandSystem_.GetQueue<DeletePipelineCommand>().threadAny_Push(1,&commandPedding);
                rendercv_.notify_one();
            }

            void async_SubmitFrameCommands(
                ObjectWeakPtr<RHIFrameCommandBuffer> commandBuffer,
                std::function<void()> OnFinish = nullptr
            ){
                FrameCommands commandPending;
                commandPending.buffer = commandBuffer;
                commandPending.OnFinish = OnFinish;
                commandSystem_.GetQueue<FrameCommands>().threadAny_Push(1,&commandPending);
                rendercv_.notify_one();
            }

            // void async_DeleteVertexBuffer(RenderResourceHandle<VertexBufferSpec> handle){
            //     DeleteVertexBufferCommand commandPedding;
            //     commandPedding.handle = handle;
            //     commandSystem_.
            // }

            void async_ExecuteCode(
                std::function<void()> execution,
                std::function<void()> OnFinish = nullptr
            ){
                BackDoorExecutionCommand commandPending;
                commandPending.execution = execution;
                commandPending.OnFinish = OnFinish;
                commandSystem_.GetQueue<BackDoorExecutionCommand>().threadAny_Push(1,&commandPending);
                rendercv_.notify_one();
            }

            RHIFrameCommandBufferPool* GetRHIFrameCommandBufferPool() { return &frameCommandPool; }

        };

}