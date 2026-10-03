#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <vector>
#include "object_ptr.h"
#include "DebugTool/ConsoleHelp/color_log.h"
#include "Render/Public/RHICommand/FrameCommand/rhi_command_dispatcher.h"
#include "Render/Public/RHICommand/FrameCommand/rhi_frame_command.h"

namespace Render{
    class RHIFrameCommandBufferPool;
    class RHIFrameCommandBuffer{
        friend class RHIFrameCommandBufferPool;
        friend class RHIDevice;
        friend class RHIFrameEncoder;
        private:
            RHIFrameCommandBuffer() = default;
            std::vector<std::byte> bytebuffer_;
            size_t memory_p = 0;
            size_t commandCount = 0;
            std::atomic_bool submitted_{false};
            RHIFrameCommandBufferPool* pool_ = nullptr;
            // Encoders may still query their old lease while the render thread
            // retires or reuses its buffer. Command recording remains single-owner.
            std::atomic_uint64_t recordingGeneration_{0};
            std::atomic_bool available_{false};
            std::atomic_bool recycling_{false};
            std::vector<std::shared_ptr<const void>> keepAlive_;
            bool IsCurrentRecording(uint64_t generation) const {
                return recordingGeneration_.load() == generation && !available_.load() &&
                    !recycling_.load() && !submitted_.load() &&
                    recordingGeneration_.load() == generation;
            }
            bool KeepAlive(std::shared_ptr<const void> resource, uint64_t generation) {
                if (!resource || !IsCurrentRecording(generation)) return false;
                keepAlive_.push_back(std::move(resource));
                return true;
            }
            bool Discard(ObjectWeakPtr<RHIFrameCommandBuffer> self, uint64_t generation);
            void Reset(){
                memory_p = 0;
                commandCount = 0;
                submitted_.store(false);
                if(bytebuffer_.capacity() < 4096){
                    bytebuffer_.reserve(4096);
                }
                // Destructors may enqueue RHI commands or acquire another frame.
                // All Reset callers must be outside queue and pool locks.
                std::vector<std::shared_ptr<const void>> retired;
                retired.swap(keepAlive_);
            }
            bool Seal(){
                if(available_.load() || recycling_.load()) return false;
                bool expected = false;
                return submitted_.compare_exchange_strong(expected, true);
            }
        public:
            RHIFrameCommandBuffer(const RHIFrameCommandBuffer&) = delete;
            RHIFrameCommandBuffer(RHIFrameCommandBuffer&& other) = delete;
            size_t GetCommandCount() const {return commandCount;}
            bool IsSubmitted() const {return submitted_.load();}
            // 放置命令
            template <typename T>
            bool PushCommand(const T& command){
                static_assert(RHICommand::FrameCommandsSet::contains<T>);
                static_assert(std::is_trivially_copyable_v<T>,
                    "frame commands must be trivially copyable");
                if(submitted_.load() || available_.load() || recycling_.load()){
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
        friend class RHIFrameCommandBuffer;
        private:
            std::vector<ObjectPtr<RHIFrameCommandBuffer>> buffers;
            std::vector<ObjectWeakPtr<RHIFrameCommandBuffer>> freeBuffers;
            std::mutex m;
            bool Recycle(ObjectWeakPtr<RHIFrameCommandBuffer> buffer,
                         bool onlyUnsubmitted, uint64_t generation = 0) {
                {
                    std::lock_guard lock(m);
                    if (!buffer || buffer->pool_ != this || buffer->available_.load() || buffer->recycling_.load())
                        return false;
                    if (onlyUnsubmitted && !buffer->IsCurrentRecording(generation)) return false;
                    buffer->recycling_.store(true);
                }
                buffer->Reset();
                {
                    std::lock_guard lock(m);
                    buffer->available_.store(true);
                    buffer->recycling_.store(false);
                    freeBuffers.push_back(buffer);
                }
                return true;
            }
        public:
            size_t GetAllocatedBufferCount() {
                std::lock_guard lock(m);
                return buffers.size();
            }
            // Call after producers stop recording/submitting new frames. Submitted
            // work is still owned by the render queue and will be drained normally.
            void DiscardUnsubmitted() {
                std::vector<std::pair<ObjectWeakPtr<RHIFrameCommandBuffer>, uint64_t>> abandoned;
                {
                    std::lock_guard lock(m);
                    abandoned.reserve(buffers.size());
                    for (auto& buffer : buffers) {
                        const auto generation = buffer->recordingGeneration_.load();
                        if (buffer->IsCurrentRecording(generation))
                            abandoned.emplace_back(buffer.GenWeakPtr(), generation);
                    }
                }
                // Pins may enqueue resource deletion, so neither pool nor device
                // synchronization may be held while cancelling these snapshots.
                for (auto& [buffer, generation] : abandoned)
                    Recycle(buffer, true, generation);
            }
            ObjectWeakPtr<RHIFrameCommandBuffer> threadAny_GetBuffer(){
                ObjectWeakPtr<RHIFrameCommandBuffer> result;
                {
                    std::lock_guard lock(m);
                    if(!freeBuffers.empty()){
                        result = freeBuffers.back();
                        freeBuffers.pop_back();
                    }
                    else{
                        buffers.emplace_back();
                        buffers.back() = new RHIFrameCommandBuffer();
                        result = buffers.back().GenWeakPtr();
                        result->pool_ = this;
                    }
                    ++result->recordingGeneration_;
                    result->available_.store(false);
                }
                result->Reset();
                return result;
            }

            void threadAny_Recycle(ObjectWeakPtr<RHIFrameCommandBuffer> buffer){
                Recycle(buffer, false);
            }
    };

    inline bool RHIFrameCommandBuffer::Discard(
        ObjectWeakPtr<RHIFrameCommandBuffer> self, uint64_t generation) {
        return pool_ && pool_->Recycle(self, true, generation);
    }
}
