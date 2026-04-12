#include "gl_input_layout.h"
#include <glad/glad.h>

namespace Render::Backend::OpenGL {

    namespace {

        GLenum GetFormatType(Render::RHI::VertexElementFormat format) {
            using VertexElementFormat = Render::RHI::VertexElementFormat;

            switch (format) {
                case VertexElementFormat::UInt1:
                case VertexElementFormat::UInt2:
                case VertexElementFormat::UInt3:
                case VertexElementFormat::UInt4:
                    return GL_UNSIGNED_INT;
                case VertexElementFormat::UByte4_Norm:
                    return GL_UNSIGNED_BYTE;
                default:
                    return GL_FLOAT;
            }
        }

        GLint GetFormatSize(Render::RHI::VertexElementFormat format) {
            using VertexElementFormat = Render::RHI::VertexElementFormat;

            switch (format) {
                case VertexElementFormat::Float1:
                case VertexElementFormat::UInt1:
                    return 1;
                case VertexElementFormat::Float2:
                case VertexElementFormat::UInt2:
                    return 2;
                case VertexElementFormat::Float3:
                case VertexElementFormat::UInt3:
                    return 3;
                case VertexElementFormat::Float4:
                case VertexElementFormat::UInt4:
                case VertexElementFormat::UByte4_Norm:
                    return 4;
                default:
                    return 3;
            }
        }

        GLboolean IsNormalized(Render::RHI::VertexElementFormat format) {
            return format == Render::RHI::VertexElementFormat::UByte4_Norm ? GL_TRUE : GL_FALSE;
        }

    }

    GLInputLayout::GLInputLayout(const Render::RHI::InputLayoutDesc& desc)
        : InputLayout(desc) {
    }

    GLInputLayout::~GLInputLayout() {
        if (handle_ != 0) {
            glDeleteVertexArrays(1, &handle_);
            handle_ = 0;
        }
    }

    GLInputLayout::GLInputLayout(GLInputLayout&& other) noexcept
        : InputLayout(other.desc_),
          handle_(other.handle_) {
        other.handle_ = 0;
    }

    GLInputLayout& GLInputLayout::operator=(GLInputLayout&& other) noexcept {
        if (this != &other) {
            if (handle_ != 0) {
                glDeleteVertexArrays(1, &handle_);
            }

            desc_ = other.desc_;
            handle_ = other.handle_;
            other.handle_ = 0;
        }
        return *this;
    }

    bool GLInputLayout::Initialize() {
        if (desc_.attributes.empty()) {
            return false;
        }

        glGenVertexArrays(1, &handle_);
        if (handle_ == 0) {
            return false;
        }

        glBindVertexArray(handle_);

        for (const auto& attr : desc_.attributes) {
            glEnableVertexAttribArray(attr.location);

            GLint size = GetFormatSize(attr.format);
            GLenum type = GetFormatType(attr.format);
            GLboolean normalized = IsNormalized(attr.format);

            if (type == GL_UNSIGNED_INT) {
                glVertexAttribIPointer(attr.location, size, type,
                                       static_cast<GLsizei>(desc_.bindings[attr.binding].stride),
                                       reinterpret_cast<const void*>(attr.offset));
            } else {
                glVertexAttribPointer(attr.location, size, type, normalized,
                                      static_cast<GLsizei>(desc_.bindings[attr.binding].stride),
                                      reinterpret_cast<const void*>(attr.offset));
            }

            if (desc_.bindings.size() > attr.binding &&
                desc_.bindings[attr.binding].inputRate == Render::RHI::VertexInputRate::PerInstance) {
                glVertexAttribDivisor(attr.location, 1);
            } else {
                glVertexAttribDivisor(attr.location, 0);
            }
        }

        glBindVertexArray(0);

        return true;
    }

    bool GLInputLayout::Create() {
        return Initialize();
    }

    void GLInputLayout::Destroy() {
        if (handle_ != 0) {
            glDeleteVertexArrays(1, &handle_);
            handle_ = 0;
        }
    }

    bool GLInputLayout::Check() const noexcept {
        return handle_ != 0;
    }

    bool GLInputLayout::Bind() const {
        if (!Check()) {
            return false;
        }
        glBindVertexArray(handle_);
        return true;
    }

    void GLInputLayout::Unbind() const {
        glBindVertexArray(0);
    }

}
