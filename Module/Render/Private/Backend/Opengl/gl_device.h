#pragma once

#include "Render/Public/RHI/RHI_device.h"
#include "Render/Private/Backend/Opengl/gl_buffer.h"
#include "Render/Private/Backend/Opengl/gl_input_layout.h"
#include "Render/Private/Backend/Opengl/gl_texture.h"

namespace Render::Backend::OpenGL {

    /**
     * @brief OpenGL 后端设备实现
     *
     * 职责：只负责 GPU 端操作和 OpenGL API 调用
     * - 创建/销毁 GPU 纹理资源
     * - 上传纹理数据到 GPU
     * - 创建/销毁 GPU 缓冲区
     * - 绑定缓冲区和输入布局
     *
     * 注意：不负责 CPU 端资源加载（如 TextureAsset 加载）
     */
    class GLDevice final : public Render::RHI::Device {
    public:
        // ==================== Texture GPU 操作 ====================

        /**
         * @brief 创建空的 GPU 纹理资源
         */
        std::unique_ptr<Render::RHI::RHITexture> CreateEmptyTexture(
            const Render::RHI::TextureDesc& desc
        ) override;

        /**
         * @brief 从 TextureAsset 创建 GPU 纹理资源
         */
        std::unique_ptr<Render::RHI::RHITexture> CreateTextureFromAsset(
            const Render::RHI::TextureAsset& asset
        ) override;

        /**
         * @brief 将纹理数据上传到 GPU
         */
        void UploadTexture(
            Render::RHI::RHITexture* texture,
            const Render::RHI::TextureAsset& asset
        ) override;

        /**
         * @brief 局部更新纹理数据
         */
        void UpdateTexture(
            Render::RHI::RHITexture* texture,
            const void* data,
            uint32_t mipLevel,
            uint32_t xOffset,
            uint32_t yOffset,
            uint32_t zOffset,
            uint32_t width,
            uint32_t height,
            uint32_t depth
        ) override;

        /**
         * @brief 销毁 GPU 纹理资源
         */
        void DestroyTexture(Render::RHI::RHITexture* texture) override;

        // ==================== Buffer GPU 操作 ====================

        Render::RHI::Buffer* CreateBuffer(
            const Render::RHI::BufferDesc& desc,
            const void* initialData = nullptr
        ) override;

        void DestroyBuffer(Render::RHI::Buffer* buffer) override;

        bool CopyBuffer(
            Render::RHI::Buffer* src,
            Render::RHI::Buffer* dst,
            uint64_t size,
            uint64_t srcOffset = 0,
            uint64_t dstOffset = 0
        ) override;

        bool BindVertexBuffer(Render::RHI::Buffer* buffer, uint32_t slot, uint64_t offset = 0) override;
        bool BindIndexBuffer(Render::RHI::Buffer* buffer, Render::RHI::IndexFormat format, uint64_t offset = 0) override;
        bool BindConstantBuffer(Render::RHI::Buffer* buffer, uint32_t slot, const Render::RHI::BufferRange* range = nullptr) override;
        bool BindStorageBuffer(Render::RHI::Buffer* buffer, uint32_t slot, const Render::RHI::BufferRange* range = nullptr) override;

        // ==================== InputLayout GPU 操作 ====================

        Render::RHI::InputLayout* CreateInputLayout(const Render::RHI::InputLayoutDesc& desc) override;
        void DestroyInputLayout(Render::RHI::InputLayout* layout) override;
        bool BindInputLayout(Render::RHI::InputLayout* layout) override;
    };

}
