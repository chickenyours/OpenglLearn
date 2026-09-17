#pragma once

#include <cstdint>

namespace Render{
    enum class RHITextureFormat {
        RGB8,
        RGBA8,
        RGB32F,
        RGBA32F
    };



    enum class RHIFilterMode {
        Nearest, // 最近点采样，像素风格、硬边
        Linear   // 线性插值，平滑
    };

    enum class RHIMipmapMode {
        Nearest, // 选择最近的 mip 层
        Linear   // 在两个 mip 层之间插值
    };

    enum class RHIAddressMode {
        Repeat,         // 重复平铺
        MirroredRepeat, // 镜像重复
        ClampToEdge,    // 边缘拉伸
        ClampToBorder   // 超出范围使用边框颜色
    };

    inline uint32_t GetTextureFormatByteSize(RHITextureFormat format) {
        switch (format) {
        case RHITextureFormat::RGB8: return 3;
        case RHITextureFormat::RGBA8: return 4;
        case RHITextureFormat::RGB32F: return 12;
        case RHITextureFormat::RGBA32F: return 16;
        }
        return 0;
    }

    struct RHITextureSpec
    {
        uint32_t width = 0;
        uint32_t height = 0;
        RHITextureFormat textureDataStoreType = RHITextureFormat::RGBA8;
        RHITextureFormat textureUseType = RHITextureFormat::RGBA8;
        RHIFilterMode filterMode = RHIFilterMode::Nearest;
        RHIMipmapMode mipmapMode = RHIMipmapMode::Nearest;
        RHIAddressMode addressMode = RHIAddressMode::Repeat;
        uint32_t rhi_id = 0;
    };

    struct CreateRHITextureSpec{
        uint32_t width = 0;
        uint32_t height = 0;
        RHITextureFormat textureDataStoreType = RHITextureFormat::RGBA8;
        RHITextureFormat textureUseType = RHITextureFormat::RGBA8;
        RHIFilterMode filterMode = RHIFilterMode::Nearest;
        RHIMipmapMode mipmapMode = RHIMipmapMode::Nearest;
        RHIAddressMode addressMode = RHIAddressMode::Repeat;
        const void* data = nullptr;
    };
}
