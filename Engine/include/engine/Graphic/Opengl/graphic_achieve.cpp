#include "Graphic.h"
#include <glad/glad.h>

namespace Graphic {

    class GLGPUBuffer : public GPUBuffer {
        GLuint handle = 0;
        size_t size = 0;
        Usage usage;

        GLenum ToGLUsage(Usage u) {
            switch(u) {
                case Usage::STATIC: return GL_STATIC_DRAW;
                case Usage::DYNAMIC: return GL_DYNAMIC_DRAW;
                case Usage::STREAM: return GL_STREAM_DRAW;
            }
            return GL_STATIC_DRAW;
        }

    public:
        GLGPUBuffer(size_t s, Usage u) : size(s), usage(u) {
            glGenBuffers(1, &handle);
            glBindBuffer(GL_ARRAY_BUFFER, handle);
            glBufferData(GL_ARRAY_BUFFER, size, nullptr, ToGLUsage(usage));
        }

        GLGPUBuffer(const void* data, size_t s, Usage u) : size(s), usage(u) {
            glGenBuffers(1, &handle);
            glBindBuffer(GL_ARRAY_BUFFER, handle);
            glBufferData(GL_ARRAY_BUFFER, size, data, ToGLUsage(usage));
        }

        ~GLGPUBuffer() override { if(handle) glDeleteBuffers(1, &handle); }

        bool IsValid() const override { return handle != 0; }

        void Resize(size_t newSize) override {
            if (!IsValid()) return;
            size = newSize;
            glBindBuffer(GL_ARRAY_BUFFER, handle);
            glBufferData(GL_ARRAY_BUFFER, size, nullptr, ToGLUsage(usage));
        }

        void Update(const void* data, size_t s, size_t offset = 0) override {
            if (!IsValid() || offset + s > size) return;
            glBindBuffer(GL_ARRAY_BUFFER, handle);
            glBufferSubData(GL_ARRAY_BUFFER, offset, s, data);
        }

        size_t GetSize() const override { return size; }
        GLuint GetHandle() const { return handle; }
    };

    class GLTexture2DBuffer : public Texture2DBuffer {
        GLuint handle = 0;

        GLenum ToGLInternalFormat(Texture2DBufferType t) {
            switch(t) {
                case Texture2DBufferType::RGBA8: return GL_RGBA8;
                case Texture2DBufferType::RGBA16F: return GL_RGBA16F;
                case Texture2DBufferType::DEPTH24_STENCIL8: return GL_DEPTH24_STENCIL8;
                default: return GL_RGBA8;
            }
        }

        GLenum ToGLFormat(Texture2DBufferType t) {
            switch(t) {
                case Texture2DBufferType::DEPTH24_STENCIL8: return GL_DEPTH_STENCIL;
                default: return GL_RGBA;
            }
        }

        GLenum ToGLType(Texture2DBufferType t) {
            switch(t) {
                case Texture2DBufferType::RGBA16F: return GL_FLOAT;
                case Texture2DBufferType::DEPTH24_STENCIL8: return GL_UNSIGNED_INT_24_8;
                default: return GL_UNSIGNED_BYTE;
            }
        }

    public:
        GLTexture2DBuffer(size_t w, size_t h, Texture2DBufferType f)
        {
            width = w; height = h; format = f;
            glGenTextures(1, &handle);
            glBindTexture(GL_TEXTURE_2D, handle);
            glTexImage2D(GL_TEXTURE_2D, 0, ToGLInternalFormat(f), width, height, 0,
                         ToGLFormat(f), ToGLType(f), nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }

        ~GLTexture2DBuffer() override { if(handle) glDeleteTextures(1, &handle); }

        bool IsValid() const override { return handle != 0; }

        void Resize(size_t newWidth, size_t newHeight) override {
            if (!IsValid()) return;
            width = newWidth; height = newHeight;
            glBindTexture(GL_TEXTURE_2D, handle);
            glTexImage2D(GL_TEXTURE_2D, 0, ToGLInternalFormat(format), width, height, 0,
                         ToGLFormat(format), ToGLType(format), nullptr);
        }

        GLuint GetHandle() const { return handle; }
    };

    std::shared_ptr<GPUBuffer> CreateGPUBuffer(size_t size, GPUBuffer::Usage usage) {
        return std::make_shared<GLGPUBuffer>(size, usage);
    }

    std::shared_ptr<Texture2DBuffer> CreateTexture2DBuffer(size_t width, size_t height, Texture2DBufferType format) {
        return std::make_shared<GLTexture2DBuffer>(width, height, format);
    }

}
