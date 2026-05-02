#pragma once
#include <cstdint>
#include "texture_types.h"

namespace Render::Resource
{
    struct TextureDesc
    {
        TextureDimension dimension = TextureDimension::Tex2D;
        TextureFormat format = TextureFormat::RGBA8_UNorm;

        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t depth = 1;

        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;

        TextureUsage usage = TextureUsage::ShaderResource;
    };
}