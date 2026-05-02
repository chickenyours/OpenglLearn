#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "texture_desc.h"

namespace Render::Resource
{
    struct TextureData
    {
        TextureDesc desc{};

        // 解码后的实际通道数，可选
        uint32_t channelCount = 0;

        // 原始像素字节数据
        std::vector<std::byte> pixels;

        bool IsValid() const noexcept
        {
            return desc.width > 0 &&
                   desc.height > 0 &&
                   !pixels.empty();
        }

        bool Empty() const noexcept
        {
            return pixels.empty();
        }

        const std::byte* Data() const noexcept
        {
            return pixels.empty() ? nullptr : pixels.data();
        }

        std::byte* Data() noexcept
        {
            return pixels.empty() ? nullptr : pixels.data();
        }

        size_t SizeInBytes() const noexcept
        {
            return pixels.size();
        }
    };
}