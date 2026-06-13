#include "gl_texture_convert.h"
#include <glad/glad.h>

namespace Render::Backend::OpenGL {

    uint32_t ConvertWrapMode(Render::RHI::WrapMode mode) {
        using Render::RHI::WrapMode;

        switch (mode) {
        case WrapMode::Repeat:          return GL_REPEAT;
        case WrapMode::ClampToEdge:     return GL_CLAMP_TO_EDGE;
        case WrapMode::MirroredRepeat:  return GL_MIRRORED_REPEAT;
        case WrapMode::ClampToBorder:   return GL_CLAMP_TO_BORDER;
        default:                        return GL_REPEAT;
        }
    }

    uint32_t ConvertFilterMode(Render::RHI::FilterMode mode) {
        using Render::RHI::FilterMode;

        switch (mode) {
        case FilterMode::Nearest:               return GL_NEAREST;
        case FilterMode::Linear:                return GL_LINEAR;
        case FilterMode::NearestMipmapNearest:  return GL_NEAREST_MIPMAP_NEAREST;
        case FilterMode::LinearMipmapNearest:   return GL_LINEAR_MIPMAP_NEAREST;
        case FilterMode::NearestMipmapLinear:   return GL_NEAREST_MIPMAP_LINEAR;
        case FilterMode::LinearMipmapLinear:    return GL_LINEAR_MIPMAP_LINEAR;
        default:                                return GL_LINEAR;
        }
    }

    GLTextureFormatInfo ConvertFormat(Render::RHI::Format format) {
        using Render::RHI::Format;

        switch (format) {
        case Format::R8_UNorm:
            return { GL_R8, GL_RED, GL_UNSIGNED_BYTE, true, false };

        case Format::RG8_UNorm:
            return { GL_RG8, GL_RG, GL_UNSIGNED_BYTE, true, false };

        case Format::RGB8_UNorm:
            return { GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, true, false };

        case Format::RGBA8_UNorm:
            return { GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, true, false };

        case Format::RGBA8_SRGB:
            return { GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, true, false };

        case Format::R16_Float:
            return { GL_R16F, GL_RED, GL_HALF_FLOAT, true, false };

        case Format::RG16_Float:
            return { GL_RG16F, GL_RG, GL_HALF_FLOAT, true, false };

        case Format::RGBA16_Float:
            return { GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, true, false };

        case Format::R32_Float:
            return { GL_R32F, GL_RED, GL_FLOAT, true, false };

        case Format::RG32_Float:
            return { GL_RG32F, GL_RG, GL_FLOAT, true, false };

        case Format::RGBA32_Float:
            return { GL_RGBA32F, GL_RGBA, GL_FLOAT, true, false };

        case Format::D24S8:
            return { GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, true, true };

        case Format::D32_Float:
            return { GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, true, true };

        default:
            return {};
        }
    }

    GLTextureTargetInfo ConvertTarget(const Render::RHI::TextureDesc& desc) {
        using Render::RHI::TextureDimension;

        switch (desc.dimension) {
        case TextureDimension::Tex1D:
            return { GL_TEXTURE_1D, true };

        case TextureDimension::Tex2D:
            return { GL_TEXTURE_2D, true };

        case TextureDimension::Tex3D:
            return { GL_TEXTURE_3D, true };

        case TextureDimension::Cube:
            return { GL_TEXTURE_CUBE_MAP, true };

        default:
            return {};
        }
    }

}