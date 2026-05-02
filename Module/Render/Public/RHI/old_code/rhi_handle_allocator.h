#pragma once

#include <cstdint>
#include <atomic>
#include "Render/Public/RHI/rhi_handles.h"

namespace Render::RHI {

    /**
     * @brief 逻辑资源句柄分配器
     * 线程安全，使用原子操作
     */
    class RHIHandleAllocator {
    public:
        static RHIHandleAllocator& Instance() {
            static RHIHandleAllocator instance;
            return instance;
        }

        BufferHandle AllocateBufferHandle() {
            uint64_t id = nextHandle_.fetch_add(1, std::memory_order_relaxed);
            return BufferHandle{id};
        }

        TextureHandle AllocateTextureHandle() {
            uint64_t id = nextHandle_.fetch_add(1, std::memory_order_relaxed);
            return TextureHandle{id};
        }

        InputLayoutHandle AllocateInputLayoutHandle() {
            uint64_t id = nextHandle_.fetch_add(1, std::memory_order_relaxed);
            return InputLayoutHandle{id};
        }

    private:
        RHIHandleAllocator() = default;
        ~RHIHandleAllocator() = default;

        RHIHandleAllocator(const RHIHandleAllocator&) = delete;
        RHIHandleAllocator& operator=(const RHIHandleAllocator&) = delete;

    private:
        std::atomic<uint64_t> nextHandle_{1}; // 0 保留为无效句柄
    };

} // namespace Render::RHI
