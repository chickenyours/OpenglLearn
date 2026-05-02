#pragma once
#include <cstdint>

namespace Render::Resource
{
    enum class TextureDimension : uint8_t
    {
        Tex1D,
        Tex2D,
        Tex3D,
        Cube
    };

    enum class TextureFormat : uint16_t
    {
        Unknown = 0,
        R8_UNorm,
        RG8_UNorm,
        RGB8_UNorm,
        RGBA8_UNorm,
        RGBA8_SRGB,
        R16_Float,
        RGBA16_Float,
        RGBA32_Float,
        D24S8
    };

    enum class TextureUsage : uint32_t
    {
        None           = 0,
        ShaderResource = 1 << 0,
        RenderTarget   = 1 << 1,
        DepthStencil   = 1 << 2,
        Storage        = 1 << 3,
        TransferSrc    = 1 << 4,
        TransferDst    = 1 << 5
    };

    enum class TextureState : uint8_t
    {
        Empty,
        Ready,
        Failed
    };
}