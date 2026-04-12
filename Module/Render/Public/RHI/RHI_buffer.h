#pragma once

#include <cstdint>

namespace Render::RHI {

    enum class BufferUsage : uint32_t {
        None            = 0,
        Vertex          = 1 << 0,
        Index           = 1 << 1,
        Constant        = 1 << 2,
        Structured      = 1 << 3,
        Storage         = 1 << 4,
        Indirect        = 1 << 5,
        TransferSrc     = 1 << 6,
        TransferDst     = 1 << 7,
        Readback        = 1 << 8
    };

    inline BufferUsage operator|(BufferUsage a, BufferUsage b) {
        return static_cast<BufferUsage>(
            static_cast<uint32_t>(a) | static_cast<uint32_t>(b)
        );
    }

    inline BufferUsage operator&(BufferUsage a, BufferUsage b) {
        return static_cast<BufferUsage>(
            static_cast<uint32_t>(a) & static_cast<uint32_t>(b)
        );
    }

    enum class MemoryUsage {
        GpuOnly,
        CpuToGpu,
        GpuToCpu,
        CpuOnly
    };

    enum class CpuAccessMode : uint32_t {
        None    = 0,
        Read    = 1 << 0,
        Write   = 1 << 1
    };

    inline CpuAccessMode operator|(CpuAccessMode a, CpuAccessMode b) {
        return static_cast<CpuAccessMode>(
            static_cast<uint32_t>(a) | static_cast<uint32_t>(b)
        );
    }

    inline CpuAccessMode operator&(CpuAccessMode a, CpuAccessMode b) {
        return static_cast<CpuAccessMode>(
            static_cast<uint32_t>(a) & static_cast<uint32_t>(b)
        );
    }

    enum class MapMode {
        Read,
        Write,
        ReadWrite,
        WriteDiscard,
        WriteNoOverwrite
    };

    enum class IndexFormat {
        UInt16,
        UInt32
    };

    struct BufferDesc {
        uint64_t size = 0;
        uint32_t stride = 0;
        uint32_t usage = static_cast<uint32_t>(BufferUsage::None);

        MemoryUsage memoryUsage = MemoryUsage::GpuOnly;
        uint32_t cpuAccess = static_cast<uint32_t>(CpuAccessMode::None);

        bool createMapped = false;
    };

    struct BufferRange {
        uint64_t offset = 0;
        uint64_t size = 0;
    };

    struct BufferViewDesc {
        uint64_t offset = 0;
        uint64_t size = 0;
        uint32_t stride = 0;
    };

    inline bool HasBufferUsage(uint32_t flags, BufferUsage usage) {
        return (flags & static_cast<uint32_t>(usage)) != 0;
    }

    inline bool HasCpuAccess(uint32_t flags, CpuAccessMode mode) {
        return (flags & static_cast<uint32_t>(mode)) != 0;
    }

    class Buffer {
    public:
        explicit Buffer(const BufferDesc& desc) : desc_(desc) {}
        virtual ~Buffer() = default;

        const BufferDesc& GetDesc() const noexcept { return desc_; }

        virtual bool Check() const noexcept = 0;
        virtual bool Update(const void* data, uint64_t size, uint64_t offset = 0) = 0;
        virtual void* Map(MapMode mode, uint64_t offset = 0, uint64_t size = 0) = 0;
        virtual void Unmap() = 0;

    protected:
        BufferDesc desc_;
    };

}
