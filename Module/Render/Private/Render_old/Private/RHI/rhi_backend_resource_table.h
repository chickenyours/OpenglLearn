#pragma once

#include <unordered_map>
#include <memory>
#include <mutex>

#include "Render/Public/RHI/rhi_handles.h"

namespace Render::Backend::OpenGL {
    class GLBuffer;
    class GLTexture;
    class GLInputLayout;
}

namespace Render::RHI {

    /**
     * @brief 后端资源表
     * 只在渲染线程内使用，保存逻辑句柄 -> 真实后端对象的映射
     */
    class RHIBackendResourceTable {
    public:
        RHIBackendResourceTable() = default;
        ~RHIBackendResourceTable() = default;

        RHIBackendResourceTable(const RHIBackendResourceTable&) = delete;
        RHIBackendResourceTable& operator=(const RHIBackendResourceTable&) = delete;

        // Buffer 操作
        void RegisterBuffer(BufferHandle handle, std::unique_ptr<Backend::OpenGL::GLBuffer> obj);
        Backend::OpenGL::GLBuffer* FindBuffer(BufferHandle handle);
        void RemoveBuffer(BufferHandle handle);

        // Texture 操作
        void RegisterTexture(TextureHandle handle, std::unique_ptr<Backend::OpenGL::GLTexture> obj);
        Backend::OpenGL::GLTexture* FindTexture(TextureHandle handle);
        void RemoveTexture(TextureHandle handle);

        // InputLayout 操作
        void RegisterInputLayout(InputLayoutHandle handle, std::unique_ptr<Backend::OpenGL::GLInputLayout> obj);
        Backend::OpenGL::GLInputLayout* FindInputLayout(InputLayoutHandle handle);
        void RemoveInputLayout(InputLayoutHandle handle);

    private:
        std::unordered_map<uint64_t, std::unique_ptr<Backend::OpenGL::GLBuffer>> buffers_;
        std::unordered_map<uint64_t, std::unique_ptr<Backend::OpenGL::GLTexture>> textures_;
        std::unordered_map<uint64_t, std::unique_ptr<Backend::OpenGL::GLInputLayout>> inputLayouts_;
    };

} // namespace Render::RHI
