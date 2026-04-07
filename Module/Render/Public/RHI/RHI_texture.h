#pragma once

#include <cstdint>

namespace Render::RHI {

    enum class TextureDimension {
        Tex1D,
        Tex2D,
        Tex3D,
        Cube
    };

    enum class Format {
        Unknown,

        R8_UNorm,
        RG8_UNorm,
        RGB8_UNorm,
        RGBA8_UNorm,
        RGBA8_SRGB,

        R16_Float,
        RG16_Float,
        RGBA16_Float,

        R32_Float,
        RG32_Float,
        RGBA32_Float,

        D24S8,
        D32_Float
    };

    enum class TextureUsage : uint32_t {
        None            = 0,
        ShaderResource  = 1 << 0,
        RenderTarget    = 1 << 1,
        DepthStencil    = 1 << 2,
        Storage         = 1 << 3,
        TransferSrc     = 1 << 4,
        TransferDst     = 1 << 5
    };

    inline TextureUsage operator|(TextureUsage a, TextureUsage b) {
        return static_cast<TextureUsage>(
            static_cast<uint32_t>(a) | static_cast<uint32_t>(b)
        );
    }

    inline TextureUsage operator&(TextureUsage a, TextureUsage b) {
        return static_cast<TextureUsage>(
            static_cast<uint32_t>(a) & static_cast<uint32_t>(b)
        );
    }

    struct TextureDesc {
        TextureDimension dimension = TextureDimension::Tex2D;
        Format format = Format::RGBA8_UNorm;

        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t depth = 1;

        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        uint32_t sampleCount = 1;

        uint32_t usage = static_cast<uint32_t>(TextureUsage::ShaderResource);

        bool generateMips = false;
    };

    class Texture {
    public:
        explicit Texture(const TextureDesc& desc) : desc_(desc) {}
        virtual ~Texture() = default;

        const TextureDesc& GetDesc() const noexcept { return desc_; }

        virtual bool Check() const noexcept = 0;

    protected:
        TextureDesc desc_;
    };

}
