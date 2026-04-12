#include "gl_device.h"
#include "gl_texture.h"
#include "gl_buffer.h"
#include "gl_input_layout.h"

namespace Render::Backend::OpenGL {

    // ==================== Texture GPU 操作 ====================

    std::unique_ptr<Render::RHI::RHITexture> GLDevice::CreateEmptyTexture(
        const Render::RHI::TextureDesc& desc
    ) {
        // 创建空的 GLTexture 对象
        auto texture = std::make_unique<GLTexture>(desc);
        
        // 创建 OpenGL 纹理资源（分配 GPU 句柄，但不上传数据）
        if (!texture->Create()) {
            return nullptr;
        }

        return texture;
    }

    std::unique_ptr<Render::RHI::RHITexture> GLDevice::CreateTextureFromAsset(
        const Render::RHI::TextureAsset& asset
    ) {
        // 检查资产是否有效
        if (!asset.Check()) {
            return nullptr;
        }

        const auto& desc = asset.GetDesc();
        auto texture = std::make_unique<GLTexture>(desc);
        
        // 创建 OpenGL 纹理资源（分配 GPU 句柄）
        if (!texture->Create()) {
            return nullptr;
        }

        return texture;
    }

    void GLDevice::UploadTexture(
        Render::RHI::RHITexture* texture,
        const Render::RHI::TextureAsset& asset
    ) {
        if (!texture || !asset.Check()) {
            return;
        }

        auto* glTexture = dynamic_cast<GLTexture*>(texture);
        if (!glTexture) {
            return;
        }

        const auto& source = asset.GetSource();
        if (!source.valid) {
            return;
        }

        // 上传像素数据到 GPU
        glTexture->SetData(
            source.GetData(),
            0, 0, 0, 0,
            source.width,
            source.height,
            source.depth
        );
    }

    void GLDevice::UpdateTexture(
        Render::RHI::RHITexture* texture,
        const void* data,
        uint32_t mipLevel,
        uint32_t xOffset,
        uint32_t yOffset,
        uint32_t zOffset,
        uint32_t width,
        uint32_t height,
        uint32_t depth
    ) {
        if (!texture || !data) {
            return;
        }

        auto* glTexture = dynamic_cast<GLTexture*>(texture);
        if (!glTexture) {
            return;
        }

        // 使用 SetData 进行局部更新
        glTexture->SetData(
            data,
            mipLevel,
            xOffset,
            yOffset,
            zOffset,
            width,
            height,
            depth
        );
    }

    void GLDevice::DestroyTexture(Render::RHI::RHITexture* texture) {
        if (texture != nullptr) {
            auto* glTexture = dynamic_cast<GLTexture*>(texture);
            if (glTexture != nullptr) {
                glTexture->Destroy();
            }
            delete texture;
        }
    }

    // ==================== Buffer GPU 操作 ====================

    Render::RHI::Buffer* GLDevice::CreateBuffer(
        const Render::RHI::BufferDesc& desc,
        const void* initialData
    ) {
        auto buffer = std::make_unique<GLBuffer>(desc);
        if (!buffer->Initialize(initialData)) {
            return nullptr;
        }
        return buffer.release();
    }

    void GLDevice::DestroyBuffer(Render::RHI::Buffer* buffer) {
        if (buffer != nullptr) {
            auto* glBuffer = dynamic_cast<GLBuffer*>(buffer);
            if (glBuffer != nullptr) {
                glBuffer->Destroy();
            }
            delete buffer;
        }
    }

    bool GLDevice::CopyBuffer(
        Render::RHI::Buffer* src,
        Render::RHI::Buffer* dst,
        uint64_t size,
        uint64_t srcOffset,
        uint64_t dstOffset
    ) {
        if (src == nullptr || dst == nullptr) {
            return false;
        }

        auto* glSrc = dynamic_cast<GLBuffer*>(src);
        auto* glDst = dynamic_cast<GLBuffer*>(dst);
        if (glSrc == nullptr || glDst == nullptr) {
            return false;
        }

        return glSrc->CopyTo(glDst, size, srcOffset, dstOffset);
    }

    bool GLDevice::BindVertexBuffer(Render::RHI::Buffer* buffer, uint32_t slot, uint64_t offset) {
        if (buffer == nullptr) {
            return false;
        }

        auto* glBuffer = dynamic_cast<GLBuffer*>(buffer);
        if (glBuffer == nullptr) {
            return false;
        }

        return glBuffer->BindAsVertexBuffer();
    }

    bool GLDevice::BindIndexBuffer(Render::RHI::Buffer* buffer, Render::RHI::IndexFormat format, uint64_t offset) {
        if (buffer == nullptr) {
            return false;
        }

        auto* glBuffer = dynamic_cast<GLBuffer*>(buffer);
        if (glBuffer == nullptr) {
            return false;
        }

        return glBuffer->BindAsIndexBuffer();
    }

    bool GLDevice::BindConstantBuffer(Render::RHI::Buffer* buffer, uint32_t slot, const Render::RHI::BufferRange* range) {
        if (buffer == nullptr) {
            return false;
        }

        auto* glBuffer = dynamic_cast<GLBuffer*>(buffer);
        if (glBuffer == nullptr) {
            return false;
        }

        uint64_t offset = (range != nullptr) ? range->offset : 0;
        uint64_t size = (range != nullptr) ? range->size : 0;

        return glBuffer->BindAsConstantBuffer(slot, offset, size);
    }

    bool GLDevice::BindStorageBuffer(Render::RHI::Buffer* buffer, uint32_t slot, const Render::RHI::BufferRange* range) {
        if (buffer == nullptr) {
            return false;
        }

        auto* glBuffer = dynamic_cast<GLBuffer*>(buffer);
        if (glBuffer == nullptr) {
            return false;
        }

        uint64_t offset = (range != nullptr) ? range->offset : 0;
        uint64_t size = (range != nullptr) ? range->size : 0;

        return glBuffer->BindAsStorageBuffer(slot, offset, size);
    }

    // ==================== InputLayout GPU 操作 ====================

    Render::RHI::InputLayout* GLDevice::CreateInputLayout(const Render::RHI::InputLayoutDesc& desc) {
        auto layout = std::make_unique<GLInputLayout>(desc);
        if (!layout->Initialize()) {
            return nullptr;
        }
        return layout.release();
    }

    void GLDevice::DestroyInputLayout(Render::RHI::InputLayout* layout) {
        if (layout != nullptr) {
            auto* glLayout = dynamic_cast<GLInputLayout*>(layout);
            if (glLayout != nullptr) {
                glLayout->Destroy();
            }
            delete layout;
        }
    }

    bool GLDevice::BindInputLayout(Render::RHI::InputLayout* layout) {
        if (layout == nullptr) {
            return false;
        }

        auto* glLayout = dynamic_cast<GLInputLayout*>(layout);
        if (glLayout == nullptr) {
            return false;
        }

        return glLayout->Bind();
    }

}

namespace Render::RHI {

    std::unique_ptr<Device> CreateOpenGLDevice() {
        return std::make_unique<Render::Backend::OpenGL::GLDevice>();
    }

}
