#include "Render/Private/Backend/Opengl/gl_buffer.h"
#include <glad/glad.h>

namespace Render::Backend::OpenGL {

    GLBuffer::GLBuffer(const Render::RHI::BufferDesc& desc) 
        : Render::RHI::Buffer(desc) {
    }

    GLBuffer::~GLBuffer() {
        Destroy();
    }

    GLBuffer::GLBuffer(GLBuffer&& other) noexcept 
        : Render::RHI::Buffer(other.desc_), handle_(other.handle_), target_(other.target_), mapped_(other.mapped_) {
        other.handle_ = 0;
        other.target_ = 0;
        other.mapped_ = false;
    }

    GLBuffer& GLBuffer::operator=(GLBuffer&& other) noexcept {
        if (this != &other) {
            Destroy();
            desc_ = other.desc_;
            handle_ = other.handle_;
            target_ = other.target_;
            mapped_ = other.mapped_;
            other.handle_ = 0;
            other.target_ = 0;
            other.mapped_ = false;
        }
        return *this;
    }

    bool GLBuffer::Initialize(const void* initialData) {
        if (handle_ != 0) {
            return true; // 已经初始化
        }

        // 确定目标类型
        if (HasBufferUsage(desc_.usage, Render::RHI::BufferUsage::Vertex)) {
            target_ = GL_ARRAY_BUFFER;
        } else if (HasBufferUsage(desc_.usage, Render::RHI::BufferUsage::Index)) {
            target_ = GL_ELEMENT_ARRAY_BUFFER;
        } else if (HasBufferUsage(desc_.usage, Render::RHI::BufferUsage::Constant)) {
            target_ = GL_UNIFORM_BUFFER;
        } else if (HasBufferUsage(desc_.usage, Render::RHI::BufferUsage::Storage)) {
            target_ = GL_SHADER_STORAGE_BUFFER;
        } else {
            target_ = GL_ARRAY_BUFFER; // 默认
        }

        glCreateBuffers(1, &handle_);
        if (handle_ == 0) {
            return false;
        }

        GLenum usage = GL_STATIC_DRAW;
        if (HasBufferUsage(desc_.usage, Render::RHI::BufferUsage::TransferSrc) ||
            HasBufferUsage(desc_.usage, Render::RHI::BufferUsage::TransferDst)) {
            usage = GL_DYNAMIC_DRAW;
        }

        glNamedBufferData(handle_, static_cast<GLsizeiptr>(desc_.size), initialData, usage);
        return handle_ != 0;
    }

    bool GLBuffer::Create() {
        return Initialize(nullptr);
    }

    void GLBuffer::Destroy() {
        if (handle_ != 0) {
            glDeleteBuffers(1, &handle_);
            handle_ = 0;
            target_ = 0;
            mapped_ = false;
        }
    }

    bool GLBuffer::Check() const noexcept {
        return handle_ != 0 && glIsBuffer(handle_);
    }

    bool GLBuffer::Update(const void* data, uint64_t size, uint64_t offset) {
        if (!Check()) {
            return false;
        }

        glNamedBufferSubData(handle_, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), data);
        return true;
    }

    void* GLBuffer::Map(Render::RHI::MapMode mode, uint64_t offset, uint64_t size) {
        if (!Check() || mapped_) {
            return nullptr;
        }

        GLenum access = GL_READ_WRITE;
        switch (mode) {
            case Render::RHI::MapMode::Read:
                access = GL_READ_ONLY;
                break;
            case Render::RHI::MapMode::Write:
                access = GL_WRITE_ONLY;
                break;
            case Render::RHI::MapMode::ReadWrite:
                access = GL_READ_WRITE;
                break;
            case Render::RHI::MapMode::WriteDiscard:
            case Render::RHI::MapMode::WriteNoOverwrite:
                access = GL_WRITE_ONLY;
                break;
        }

        void* ptr = glMapNamedBufferRange(handle_, static_cast<GLintptr>(offset), 
                                          size > 0 ? static_cast<GLsizeiptr>(size) : static_cast<GLsizeiptr>(desc_.size), access);
        if (ptr) {
            mapped_ = true;
        }
        return ptr;
    }

    void GLBuffer::Unmap() {
        if (!Check() || !mapped_) {
            return;
        }

        glUnmapNamedBuffer(handle_);
        mapped_ = false;
    }

    bool GLBuffer::BindAsVertexBuffer() const {
        if (!Check()) {
            return false;
        }
        glBindBuffer(target_, handle_);
        return true;
    }

    bool GLBuffer::BindAsIndexBuffer() const {
        if (!Check()) {
            return false;
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, handle_);
        return true;
    }

    bool GLBuffer::BindAsConstantBuffer(uint32_t slot, uint64_t offset, uint64_t size) const {
        if (!Check()) {
            return false;
        }
        glBindBufferBase(GL_UNIFORM_BUFFER, slot, handle_);
        return true;
    }

    bool GLBuffer::BindAsStorageBuffer(uint32_t slot, uint64_t offset, uint64_t size) const {
        if (!Check()) {
            return false;
        }
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, slot, handle_);
        return true;
    }

    bool GLBuffer::CopyTo(GLBuffer* dst, uint64_t size, uint64_t srcOffset, uint64_t dstOffset) const {
        if (!Check() || !dst || !dst->Check()) {
            return false;
        }

        glCopyNamedBufferSubData(handle_, dst->GetHandle(), 
                                 static_cast<GLintptr>(srcOffset), 
                                 static_cast<GLintptr>(dstOffset), 
                                 static_cast<GLsizeiptr>(size));
        return true;
    }

} // namespace Render::Backend::OpenGL
