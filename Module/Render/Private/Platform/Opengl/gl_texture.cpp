#include "gl_texture.h"
#include "gl_texture_convert.h"

#include <utility>
#include <glad/glad.h>

namespace Render::Backend::OpenGL {

    GLTexture::GLTexture(const Render::RHI::TextureDesc& desc)
        : Render::RHI::Texture(desc) {
    }

    GLTexture::~GLTexture() {
        Destroy();
    }

    GLTexture::GLTexture(GLTexture&& other) noexcept
        : Render::RHI::Texture(other.desc_) {
        handle_ = other.handle_;
        target_ = other.target_;
        other.handle_ = 0;
        other.target_ = 0;
    }

    GLTexture& GLTexture::operator=(GLTexture&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        Destroy();

        desc_ = other.desc_;
        handle_ = other.handle_;
        target_ = other.target_;

        other.handle_ = 0;
        other.target_ = 0;
        return *this;
    }

    bool GLTexture::Check() const noexcept {
        return handle_ != 0;
    }

    bool GLTexture::ValidateDesc() const noexcept {
        if (desc_.width == 0 || desc_.height == 0 || desc_.depth == 0) {
            return false;
        }

        if (desc_.format == Render::RHI::Format::Unknown) {
            return false;
        }

        if (desc_.mipLevels == 0) {
            return false;
        }

        // 一阶段限制
        if (desc_.arrayLayers != 1) {
            return false;
        }

        if (desc_.sampleCount != 1) {
            return false;
        }

        return true;
    }

    bool GLTexture::Create() {
        Destroy();

        if (!ValidateDesc()) {
            return false;
        }

        const auto formatInfo = ConvertFormat(desc_.format);
        const auto targetInfo = ConvertTarget(desc_);

        if (!formatInfo.valid || !targetInfo.valid) {
            return false;
        }

        target_ = targetInfo.target;

        glCreateTextures(static_cast<GLenum>(target_), 1, reinterpret_cast<GLuint*>(&handle_));
        if (handle_ == 0) {
            return false;
        }

        switch (target_) {
        case GL_TEXTURE_1D:
            glTextureStorage1D(
                static_cast<GLuint>(handle_),
                static_cast<GLsizei>(desc_.mipLevels),
                static_cast<GLenum>(formatInfo.internalFormat),
                static_cast<GLsizei>(desc_.width)
            );
            break;

        case GL_TEXTURE_2D:
        case GL_TEXTURE_CUBE_MAP:
            glTextureStorage2D(
                static_cast<GLuint>(handle_),
                static_cast<GLsizei>(desc_.mipLevels),
                static_cast<GLenum>(formatInfo.internalFormat),
                static_cast<GLsizei>(desc_.width),
                static_cast<GLsizei>(desc_.height)
            );
            break;

        case GL_TEXTURE_3D:
            glTextureStorage3D(
                static_cast<GLuint>(handle_),
                static_cast<GLsizei>(desc_.mipLevels),
                static_cast<GLenum>(formatInfo.internalFormat),
                static_cast<GLsizei>(desc_.width),
                static_cast<GLsizei>(desc_.height),
                static_cast<GLsizei>(desc_.depth)
            );
            break;

        default:
            Destroy();
            return false;
        }

        SetupDefaultSamplerState();

        if (desc_.generateMips && desc_.mipLevels > 1) {
            GenerateMips();
        }

        return true;
    }

    void GLTexture::Destroy() {
        if (handle_ != 0) {
            GLuint h = static_cast<GLuint>(handle_);
            glDeleteTextures(1, &h);
            handle_ = 0;
            target_ = 0;
        }
    }

    void GLTexture::SetupDefaultSamplerState() {
        if (handle_ == 0) {
            return;
        }

        const bool hasMip = desc_.mipLevels > 1;

        glTextureParameteri(static_cast<GLuint>(handle_), GL_TEXTURE_MIN_FILTER,
            hasMip ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTextureParameteri(static_cast<GLuint>(handle_), GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glTextureParameteri(static_cast<GLuint>(handle_), GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTextureParameteri(static_cast<GLuint>(handle_), GL_TEXTURE_WRAP_T, GL_REPEAT);

        if (target_ == GL_TEXTURE_3D || target_ == GL_TEXTURE_CUBE_MAP) {
            glTextureParameteri(static_cast<GLuint>(handle_), GL_TEXTURE_WRAP_R, GL_REPEAT);
        }
    }

    bool GLTexture::SetData(
        const void* data,
        uint32_t mipLevel,
        uint32_t xOffset,
        uint32_t yOffset,
        uint32_t zOffset,
        uint32_t uploadWidth,
        uint32_t uploadHeight,
        uint32_t uploadDepth
    ) {
        if (handle_ == 0 || data == nullptr) {
            return false;
        }

        const auto formatInfo = ConvertFormat(desc_.format);
        if (!formatInfo.valid) {
            return false;
        }

        if (uploadWidth == 0)  uploadWidth  = desc_.width  >> mipLevel;
        if (uploadHeight == 0) uploadHeight = desc_.height >> mipLevel;
        if (uploadDepth == 0)  uploadDepth  = desc_.depth  >> mipLevel;

        if (uploadWidth == 0)  uploadWidth = 1;
        if (uploadHeight == 0) uploadHeight = 1;
        if (uploadDepth == 0)  uploadDepth = 1;

        switch (target_) {
        case GL_TEXTURE_1D:
            glTextureSubImage1D(
                static_cast<GLuint>(handle_),
                static_cast<GLint>(mipLevel),
                static_cast<GLint>(xOffset),
                static_cast<GLsizei>(uploadWidth),
                static_cast<GLenum>(formatInfo.format),
                static_cast<GLenum>(formatInfo.type),
                data
            );
            break;

        case GL_TEXTURE_2D:
            glTextureSubImage2D(
                static_cast<GLuint>(handle_),
                static_cast<GLint>(mipLevel),
                static_cast<GLint>(xOffset),
                static_cast<GLint>(yOffset),
                static_cast<GLsizei>(uploadWidth),
                static_cast<GLsizei>(uploadHeight),
                static_cast<GLenum>(formatInfo.format),
                static_cast<GLenum>(formatInfo.type),
                data
            );
            break;

        case GL_TEXTURE_3D:
            glTextureSubImage3D(
                static_cast<GLuint>(handle_),
                static_cast<GLint>(mipLevel),
                static_cast<GLint>(xOffset),
                static_cast<GLint>(yOffset),
                static_cast<GLint>(zOffset),
                static_cast<GLsizei>(uploadWidth),
                static_cast<GLsizei>(uploadHeight),
                static_cast<GLsizei>(uploadDepth),
                static_cast<GLenum>(formatInfo.format),
                static_cast<GLenum>(formatInfo.type),
                data
            );
            break;

        case GL_TEXTURE_CUBE_MAP:
            // 一阶段先不提供统一六面上传
            return false;

        default:
            return false;
        }

        if (desc_.generateMips && desc_.mipLevels > 1 && mipLevel == 0) {
            GenerateMips();
        }

        return true;
    }

    void GLTexture::GenerateMips() {
        if (handle_ != 0 && desc_.mipLevels > 1) {
            glGenerateTextureMipmap(static_cast<GLuint>(handle_));
        }
    }

    void GLTexture::Bind(uint32_t unit) const {
        if (handle_ != 0) {
            glBindTextureUnit(unit, static_cast<GLuint>(handle_));
        }
    }

}
