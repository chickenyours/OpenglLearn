#pragma once
#include <cstddef>
#include <memory>
#include <glad/glad.h>

namespace Graphic {

    typedef size_t SortKey; 

    class GPUBuffer {
    public:
        enum class Usage { STATIC, DYNAMIC, STREAM };
        virtual ~GPUBuffer() = default;

        virtual bool IsValid() const = 0;
        virtual void Resize(size_t sizeInBytes) = 0;
        virtual void Update(const void* data, size_t size, size_t offset = 0) = 0;
        virtual size_t GetSize() const = 0;
    };

    enum class Texture2DBufferType { UNKNOW, RGBA8, RGBA16F, DEPTH24_STENCIL8 };

    class Texture2DBuffer {
    protected:
        size_t width = 0;
        size_t height = 0;
        Texture2DBufferType format = Texture2DBufferType::UNKNOW;

    public:
        virtual ~Texture2DBuffer() = default;

        virtual bool IsValid() const = 0;
        virtual void Resize(size_t newWidth, size_t newHeight) = 0;

        size_t GetWidth() const { return width; }
        size_t GetHeight() const { return height; }
        Texture2DBufferType GetFormat() const { return format; }
    };

    // 工厂函数
    std::shared_ptr<GPUBuffer> CreateGPUBuffer(size_t size, GPUBuffer::Usage usage);
    std::shared_ptr<Texture2DBuffer> CreateTexture2DBuffer(
        size_t width, size_t height, Texture2DBufferType format
    );

class SSBO {
    GLuint id = 0;
    size_t numElements = 0;
    size_t elementSize = 0;

public:
    SSBO() = default;
    // 渲染线程调用
    void Init(size_t numElements, size_t elementSize, void* data = nullptr) {
        this->numElements = numElements;
        this->elementSize = elementSize;
        if (id != 0) return;
        glGenBuffers(1, &id);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);
        glBufferData(GL_SHADER_STORAGE_BUFFER, numElements * elementSize, data, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    void Destroy() {
        if (id != 0) {
            glDeleteBuffers(1, &id);
            id = 0;
        }
    }

    GLuint GetID() const { return id; }
    size_t GetNumElements() const { return numElements; }
    size_t GetElementSize() const { return elementSize; }

    // 绑定到 shader 的 binding point
    void BindBase(GLuint binding) const {
        if (id != 0)
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, id);
    }
};
}
