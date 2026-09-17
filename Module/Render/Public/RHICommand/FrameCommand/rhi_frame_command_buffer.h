#pragma once

#include <cstddef>
#include <cstring>
#include <mutex>
#include <type_traits>
#include <vector>
#include "object_ptr.h"
#include "DebugTool/ConsoleHelp/color_log.h"
#include "Render/Public/RHICommand/FrameCommand/rhi_command_dispatcher.h"
#include "Render/Public/RHICommand/FrameCommand/rhi_frame_command.h"

namespace Render{
    class RHIFrameCommandBuffer{
        friend class RHIFrameCommandBufferPool;
        friend class RHIDevice;
        private:
            RHIFrameCommandBuffer() = default;
            std::vector<std::byte> bytebuffer_;
            size_t memory_p = 0;
            size_t commandCount = 0;
            bool submitted_ = false;
            void Reset(){
                memory_p = 0;
                commandCount = 0;
                submitted_ = false;
                if(bytebuffer_.capacity() < 4096){
                    bytebuffer_.reserve(4096);
                }
            }
            bool Seal(){
                if(submitted_) return false;
                submitted_ = true;
                return true;
            }
        public:
            RHIFrameCommandBuffer(const RHIFrameCommandBuffer&) = delete;
            RHIFrameCommandBuffer(RHIFrameCommandBuffer&& other) = delete;
            size_t GetCommandCount() const {return commandCount;}
            bool IsSubmitted() const {return submitted_;}
            // 放置命令
            template <typename T>
            bool PushCommand(const T& command){
                static_assert(RHICommand::FrameCommandsSet::contains<T>);
                static_assert(std::is_trivially_copyable_v<T>,
                    "frame commands must be trivially copyable");
                if(submitted_){
                    LOG_ERROR("RHIFrameCommandBuffer", "cannot record after submission");
                    return false;
                }
                const size_t required = memory_p + sizeof(T) + sizeof(CommandId);
                if(required > bytebuffer_.size()){
                    bytebuffer_.resize(required);
                }
                // push命令编号
                CommandId id = RHICommand::FrameCommandsSet::id_of<T>();
                std::memcpy(bytebuffer_.data() + memory_p, &id, sizeof(CommandId));
                memory_p += sizeof(CommandId);
                // push命令数据
                std::memcpy(bytebuffer_.data() + memory_p, &command, sizeof(T));
                memory_p += sizeof(T);
                commandCount++;
                return true;
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
                result->Reset();
                return result;
            }

            void threadAny_Recycle(ObjectWeakPtr<RHIFrameCommandBuffer> buffer){
                std::lock_guard lock(m);
                freeBuffers.push_back(buffer);
            }
    };
}
