#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Render/Public/RHI/RHI_resource.h"

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

    inline TextureUsage& operator|=(TextureUsage& a, TextureUsage b) {
        a = a | b;
        return a;
    }

    inline TextureUsage& operator&=(TextureUsage& a, TextureUsage b) {
        a = a & b;
        return a;
    }

    inline bool HasTextureUsage(TextureUsage usage, TextureUsage flag) {
        return (usage & flag) != TextureUsage::None;
    }

    enum class ResourceState {
        Unknown,
        ShaderRead,
        RenderTarget,
        DepthWrite,
        DepthRead,
        StorageReadWrite,
        TransferSrc,
        TransferDst,
        Present
    };

    enum class WrapMode {
        Repeat,
        ClampToEdge,
        MirroredRepeat,
        ClampToBorder
    };

    enum class FilterMode {
        Nearest,
        Linear,
        NearestMipmapNearest,
        LinearMipmapNearest,
        NearestMipmapLinear,
        LinearMipmapLinear
    };

    enum class TextureDataType {
        UInt8,
        Float32
    };

    struct TextureDesc {
        TextureDimension dimension = TextureDimension::Tex2D;
        Format format = Format::RGBA8_UNorm;

        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t depth = 1;

        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        uint32_t sampleCount = 1;

        TextureUsage usage = TextureUsage::ShaderResource;

        bool generateMips = false;
        bool flipVerticalOnLoad = true;

        WrapMode wrapS = WrapMode::Repeat;
        WrapMode wrapT = WrapMode::Repeat;
        WrapMode wrapR = WrapMode::Repeat;

        FilterMode minFilter = FilterMode::Linear;
        FilterMode magFilter = FilterMode::Linear;
    };

    struct TextureSourceData {
        std::vector<std::uint8_t> rawData;

        TextureDataType dataType = TextureDataType::UInt8;

        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t depth = 1;

        uint32_t channelCount = 0;
        uint32_t bytesPerChannel = 1;

        bool valid = false;

        void Reset() {
            rawData.clear();
            dataType = TextureDataType::UInt8;
            width = 0;
            height = 0;
            depth = 1;
            channelCount = 0;
            bytesPerChannel = 1;
            valid = false;
        }

        const void* GetData() const noexcept {
            return rawData.empty() ? nullptr : rawData.data();
        }

        std::size_t GetSize() const noexcept {
            return rawData.size();
        }
    };

    class TextureAsset {
    public:
        TextureAsset() = default;
        explicit TextureAsset(const TextureDesc& desc) : desc_(desc) {}
        virtual ~TextureAsset() = default;

        const TextureDesc& GetDesc() const noexcept { return desc_; }
        TextureDesc& GetDesc() noexcept { return desc_; }

        const TextureSourceData& GetSource() const noexcept { return source_; }
        TextureSourceData& GetSource() noexcept { return source_; }

        const std::string& GetSourcePath() const noexcept { return sourcePath_; }
        const std::string& GetConfigPath() const noexcept { return configPath_; }

        virtual bool Check() const noexcept { return source_.valid; }

        bool LoadFromConfigFile(const std::string& configFile);
        bool LoadFromImageFile2D(const std::string& imagePath);

        void Reset() noexcept;

        /**
         * @brief 计算 mipmap 层级数
         * @param width 纹理宽度
         * @param height 纹理高度
         * @param depth 纹理深度
         * @return mipmap 层级数
         */
        static uint32_t CalculateMipLevels(uint32_t width, uint32_t height, uint32_t depth = 1);

    protected:
        static WrapMode ParseWrapMode(const std::string& text);
        static FilterMode ParseFilterMode(const std::string& text);

        bool LoadImageFile2DInternal(
            const std::string& imagePath,
            bool needHDR,
            bool needMipMap,
            bool flipVerticalOnLoad
        );

        static Format DeduceFormatFromChannels(uint32_t channelCount, bool hdr);

    protected:
        TextureDesc desc_{};
        TextureSourceData source_{};

        std::string sourcePath_;
        std::string configPath_;
    };

    class RHITexture : public RHIResource {
    public:
        explicit RHITexture(const TextureDesc& desc)
            : desc_(desc) {
        }

        virtual ~RHITexture() override = default;

        const TextureDesc& GetDesc() const noexcept { return desc_; }

        TextureDimension GetDimension() const noexcept { return desc_.dimension; }
        Format GetFormat() const noexcept { return desc_.format; }

        uint32_t GetWidth() const noexcept { return desc_.width; }
        uint32_t GetHeight() const noexcept { return desc_.height; }
        uint32_t GetDepth() const noexcept { return desc_.depth; }

        uint32_t GetMipLevels() const noexcept { return desc_.mipLevels; }
        uint32_t GetArrayLayers() const noexcept { return desc_.arrayLayers; }
        uint32_t GetSampleCount() const noexcept { return desc_.sampleCount; }

        TextureUsage GetUsage() const noexcept { return desc_.usage; }
        bool HasUsage(TextureUsage flag) const noexcept { return HasTextureUsage(desc_.usage, flag); }

        bool ShouldGenerateMips() const noexcept { return desc_.generateMips; }

        ResourceState GetState() const noexcept { return state_; }
        void SetState(ResourceState newState) noexcept { state_ = newState; }

        bool IsValid() const noexcept { return valid_; }

        virtual void* GetNativeHandle() const noexcept = 0;
        virtual bool Create() = 0;

        virtual bool SetData(
            const void* data,
            uint32_t mipLevel = 0,
            uint32_t xOffset = 0,
            uint32_t yOffset = 0,
            uint32_t zOffset = 0,
            uint32_t uploadWidth = 0,
            uint32_t uploadHeight = 0,
            uint32_t uploadDepth = 0
        ) = 0;

        virtual bool CreateFromAsset(const TextureAsset& asset) {
            if (!asset.Check()) {
                return false;
            }

            if (asset.GetDesc().dimension != desc_.dimension ||
                asset.GetDesc().format != desc_.format ||
                asset.GetDesc().width != desc_.width ||
                asset.GetDesc().height != desc_.height ||
                asset.GetDesc().depth != desc_.depth) {
                return false;
            }

            if (!Create()) {
                return false;
            }

            if (!SetData(asset.GetSource().GetData(), 0, 0, 0, 0,
                         asset.GetSource().width,
                         asset.GetSource().height,
                         asset.GetSource().depth)) {
                Release();
                return false;
            }

            valid_ = true;
            return true;
        }

        void Release() override {
            valid_ = false;
            state_ = ResourceState::Unknown;
        }

    protected:
        TextureDesc desc_;
        ResourceState state_ = ResourceState::Unknown;
        bool valid_ = false;
    };

} // namespace Render::RHI