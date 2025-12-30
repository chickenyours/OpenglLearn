#pragma once
#include <cstddef>
#include <memory>
#include <glad/glad.h>

namespace Graphic {

    typedef size_t SortKey; 

    class GPUBuffer {
    public:
        enum class Usage {
            STATIC,
            DYNAMIC,
            STREAM
        };

        virtual ~GPUBuffer() = default;

        // 判断资源是否有效
        virtual bool IsValid() const = 0;

        // 分配 / 重新分配 GPU 内存
        virtual void Resize(size_t sizeInBytes) = 0;

        // 更新 buffer 内容
        virtual void Update(const void* data, size_t size, size_t offset = 0) = 0;

        // 获取 buffer 大小
        virtual size_t GetSize() const = 0;
    };

    enum class Texture2DBufferType {
        UNKNOW,
        RGBA8,
        RGBA16F,
        DEPTH24_STENCIL8
    };

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

    // 工厂函数接口
    std::shared_ptr<GPUBuffer> CreateGPUBuffer(size_t size, GPUBuffer::Usage usage);
    std::shared_ptr<Texture2DBuffer> CreateTexture2DBuffer(
        size_t width, size_t height, Texture2DBufferType format
    );

    class SSBO{
        GLuint id = 0;
        public:
            SSBO(size_t size){
                glGenBuffers(1, &id);
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);
                glBufferData(
                    GL_SHADER_STORAGE_BUFFER,
                    size,
                    nullptr,               // 不初始化
                    GL_DYNAMIC_DRAW        // Compute 会写
                );
            }
            SSBO(size_t size, void *data){
                glGenBuffers(1, &id);
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);
                glBufferData(
                    GL_SHADER_STORAGE_BUFFER,
                    size,
                    data,               // 不初始化
                    GL_DYNAMIC_DRAW        // Compute 会写
                );
            }
            ~SSBO(){
                if(id != 0){
                    glDeleteBuffers(1,&id);
                }
            }
    };
}
