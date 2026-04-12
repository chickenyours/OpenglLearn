#include "gl_texture_validator.h"
#include "gl_texture.h"

#include <glad/glad.h>

namespace Render::Backend::OpenGL {
namespace {

    const char* kOpenGLValidatorName = "OpenGL";

} // namespace

    Render::RHI::TextureValidationResult ValidateOpenGLTexture(const Render::RHI::RHITexture& texture) {
        Render::RHI::TextureValidationResult result;

        // 检查纹理资源是否有效
        if (!texture.IsValid()) {
            result.errorMessage = "GPU texture resource is not valid";
            return result;
        }

        // 尝试转换为 GLTexture
        const auto* glTexture = dynamic_cast<const GLTexture*>(&texture);
        if (!glTexture) {
            result.errorMessage = "Texture is not an OpenGL texture";
            return result;
        }

        GLuint handle = glTexture->GetHandle();
        result.nativeHandle = static_cast<std::size_t>(handle);

        // 检查句柄是否非零
        if (handle == 0) {
            result.errorMessage = "OpenGL texture handle is 0";
            return result;
        }

        // 使用 glIsTexture 验证
        if (!glIsTexture(handle)) {
            result.errorMessage = "glIsTexture returned false";
            return result;
        }

        // 获取 OpenGL 纹理参数进行验证（使用 level 0）
        GLint glWidth = 0, glHeight = 0;
        glGetTextureLevelParameteriv(handle, 0, GL_TEXTURE_WIDTH, &glWidth);
        glGetTextureLevelParameteriv(handle, 0, GL_TEXTURE_HEIGHT, &glHeight);

        // 检查 OpenGL 查询是否成功
        if (glGetError() != GL_NO_ERROR) {
            result.errorMessage = "OpenGL error while querying texture parameters";
            result.isValid = false;
            return result;
        }

        const auto& desc = texture.GetDesc();
        result.width = static_cast<uint32_t>(glWidth);
        result.height = static_cast<uint32_t>(glHeight);
        result.mipLevels = desc.mipLevels;
        result.isValid = (glWidth > 0 && glHeight > 0);

        if (!result.isValid) {
            result.errorMessage = "OpenGL texture has invalid dimensions";
        }

        return result;
    }

    void RegisterOpenGLTextureValidator() {
        RegisterTextureValidator(kOpenGLValidatorName, ValidateOpenGLTexture);
    }

} // namespace Render::Backend::OpenGL
