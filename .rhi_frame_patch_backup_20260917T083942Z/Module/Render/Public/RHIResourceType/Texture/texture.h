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

    struct RHITextureSpec
    {
        uint32_t width;
        uint32_t height;
        RHITextureFormat textureDataStoreType;
        RHITextureFormat textureUseType;
        RHIFilterMode filterMode;
        RHIMipmapMode mipmapMode;
        RHIAddressMode addressMode;
        uint32_t rhi_id;
    };
    
    struct CreateRHITextureSpec{
        uint32_t width;
        uint32_t height;
        RHITextureFormat textureDataStoreType;
        RHITextureFormat textureUseType;
        RHIFilterMode filterMode;
        RHIMipmapMode mipmapMode;
        RHIAddressMode addressMode;
        const void* data = nullptr;
    };
}