#pragma once

#include "Render/Public/RHI/RHI_buffer.h"

namespace Render::Backend::OpenGL {

    class GLBuffer final : public Render::RHI::Buffer {
    public:
        explicit GLBuffer(const Render::RHI::BufferDesc& desc);
        ~GLBuffer() override;

        GLBuffer(const GLBuffer&) = delete;
        GLBuffer& operator=(const GLBuffer&) = delete;

        GLBuffer(GLBuffer&& other) noexcept;
        GLBuffer& operator=(GLBuffer&& other) noexcept;

        bool Initialize(const void* initialData = nullptr);
        bool Create();
        void Destroy();

        bool Check() const noexcept override;
        bool Update(const void* data, uint64_t size, uint64_t offset = 0) override;
        void* Map(Render::RHI::MapMode mode, uint64_t offset = 0, uint64_t size = 0) override;
        void Unmap() override;

        uint32_t GetHandle() const noexcept { return handle_; }
        uint32_t GetTarget() const noexcept { return target_; }

        bool BindAsVertexBuffer() const;
        bool BindAsIndexBuffer() const;
        bool BindAsConstantBuffer(uint32_t slot, uint64_t offset = 0, uint64_t size = 0) const;
        bool BindAsStorageBuffer(uint32_t slot, uint64_t offset = 0, uint64_t size = 0) const;

        bool CopyTo(GLBuffer* dst, uint64_t size, uint64_t srcOffset = 0, uint64_t dstOffset = 0) const;

    private:
        uint32_t handle_ = 0;
        uint32_t target_ = 0;
        bool mapped_ = false;
    };

}
