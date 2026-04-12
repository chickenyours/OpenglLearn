#include "Render/Private/RHI/rhi_backend_resource_table.h"
#include "Render/Private/Backend/Opengl/gl_buffer.h"
#include "Render/Private/Backend/Opengl/gl_texture.h"
#include "Render/Private/Backend/Opengl/gl_input_layout.h"

namespace Render::RHI {

    // Buffer 操作
    void RHIBackendResourceTable::RegisterBuffer(BufferHandle handle, std::unique_ptr<Backend::OpenGL::GLBuffer> obj) {
        buffers_[handle.id] = std::move(obj);
    }

    Backend::OpenGL::GLBuffer* RHIBackendResourceTable::FindBuffer(BufferHandle handle) {
        auto it = buffers_.find(handle.id);
        if (it != buffers_.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    void RHIBackendResourceTable::RemoveBuffer(BufferHandle handle) {
        buffers_.erase(handle.id);
    }

    // Texture 操作
    void RHIBackendResourceTable::RegisterTexture(TextureHandle handle, std::unique_ptr<Backend::OpenGL::GLTexture> obj) {
        textures_[handle.id] = std::move(obj);
    }

    Backend::OpenGL::GLTexture* RHIBackendResourceTable::FindTexture(TextureHandle handle) {
        auto it = textures_.find(handle.id);
        if (it != textures_.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    void RHIBackendResourceTable::RemoveTexture(TextureHandle handle) {
        textures_.erase(handle.id);
    }

    // InputLayout 操作
    void RHIBackendResourceTable::RegisterInputLayout(InputLayoutHandle handle, std::unique_ptr<Backend::OpenGL::GLInputLayout> obj) {
        inputLayouts_[handle.id] = std::move(obj);
    }

    Backend::OpenGL::GLInputLayout* RHIBackendResourceTable::FindInputLayout(InputLayoutHandle handle) {
        auto it = inputLayouts_.find(handle.id);
        if (it != inputLayouts_.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    void RHIBackendResourceTable::RemoveInputLayout(InputLayoutHandle handle) {
        inputLayouts_.erase(handle.id);
    }

} // namespace Render::RHI
