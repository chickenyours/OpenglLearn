#pragma once

#include <cstdint>
#include "Render/Public/RHI/RHI_texture.h"

namespace Render::Backend::OpenGL {

    struct GLTextureFormatInfo {
        uint32_t internalFormat = 0;
        uint32_t format = 0;
        uint32_t type = 0;
        bool valid = false;
        bool isDepthStencil = false;
    };

    struct GLTextureTargetInfo {
        uint32_t target = 0;
        bool valid = false;
    };

    uint32_t ConvertWrapMode(Render::RHI::WrapMode mode);
    uint32_t ConvertFilterMode(Render::RHI::FilterMode mode);

    GLTextureFormatInfo ConvertFormat(Render::RHI::Format format);
    GLTextureTargetInfo ConvertTarget(const Render::RHI::TextureDesc& desc);

}