#pragma once

#include <vector>
#include "object_ptr.h"
#include "DebugTool/ConsoleHelp/color_log.h"
#include "Render/Public/RHICommand/FrameCommand/rhi_command_dispatcher.h"
#include "Render/Public/RHICommand/FrameCommand/rhi_frame_command.h"
#include "Render/Private/rhi_device.h"

namespace Render{
    class RHIFrameCommandBuffer{
        friend class RHIFrameCommandBufferPool;
        friend class RHIDevice;
        private:
            RHIFrameCommandBuffer() = default;
            std::vector<std::byte> bytebuffer_;
            size_t memory_p = 0;
            size_t commandCount = 0;
            void Reset(){
                memory_p = 0;
                commandCount = 0;
            }
        public:
            RHIFrameCommandBuffer(const RHIFrameCommandBuffer&) = delete;
            RHIFrameCommandBuffer(RHIFrameCommandBuffer&& other) = delete;
            size_t GetCommandCount(){return commandCount;}
            // 放置命令
            template <typename T>
            void PushCommand(const T& command){
                static_assert(RHICommand::FrameCommandsSet::contains<T>);
                if(memory_p + sizeof(T) + sizeof(CommandId) > bytebuffer_.size()){
                    bytebuffer_.resize(memory_p + sizeof(T) + (memory_p + sizeof(T) + sizeof(CommandId)) / 2);
                }
                // push命令编号
                CommandId id = RHICommand::FrameCommandsSet::id_of<T>();
                std::memcpy(bytebuffer_.data() + memory_p, &id, sizeof(CommandId));
                memory_p += sizeof(CommandId);
                // push命令数据
                std::memcpy(bytebuffer_.data() + memory_p, &command, sizeof(T));
                memory_p += sizeof(T);
                commandCount++;
            }
    };
    class RHIFrameCommandBufferPool{
        friend class RHIDevice;
        private:
            std::vector<ObjectPtr<RHIFrameCommandBuffer>> buffers;
            std::vector<ObjectWeakPtr<RHIFrameCommandBuffer>> freeBuffers;
            std::mutex m;
        public:
            ObjectWeakPtr<RHIFrameCommandBuffer> threadAny_GetBuffer(){
                std::lock_guard lock(m);
                ObjectWeakPtr<RHIFrameCommandBuffer> result;
                if(!freeBuffers.empty()){
                    result = freeBuffers.back();
                    freeBuffers.pop_back();
                }
                else{
                    buffers.emplace_back();
                    buffers.back() = new RHIFrameCommandBuffer();
                    result = buffers.back().GenWeakPtr();
                }
                return result;
            }

            void threadAny_Recycle(ObjectWeakPtr<RHIFrameCommandBuffer> buffer){
                std::lock_guard lock(m);
                freeBuffers.push_back(buffer);
            }
    };
}