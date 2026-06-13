#pragma once

#include "Render/Public/RHI/rhi_texture_validator.h"
#include "Render/Public/RHI/RHI_texture.h"

namespace Render::Backend::OpenGL {

    /**
     * @brief 注册 OpenGL 纹理验证器
     * 
     * 该验证器使用 OpenGL API 验证纹理是否有效：
     * - 检查纹理句柄是否非零
     * - 使用 glIsTexture 验证
     * - 获取 OpenGL 纹理参数进行比对
     */
    void RegisterOpenGLTextureValidator();

    /**
     * @brief OpenGL 纹理验证器实现
     * @param texture 要验证的 GPU 纹理资源对象 (RHITexture)
     * @return 验证结果
     */
    Render::RHI::TextureValidationResult ValidateOpenGLTexture(const Render::RHI::RHITexture& texture);

} // namespace Render::Backend::OpenGL
