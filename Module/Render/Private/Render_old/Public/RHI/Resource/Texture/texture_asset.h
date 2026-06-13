#pragma once
#include <string>
#include "texture_desc.h"

namespace Render::Resource
{
    class TextureAsset
    {
    public:
        TextureAsset() = default;
        explicit TextureAsset(std::string path, TextureDesc desc = {})
            : path_(std::move(path)), desc_(desc)
        {
        }

        const std::string& GetPath() const noexcept { return path_; }
        const TextureDesc& GetDesc() const noexcept { return desc_; }
        TextureState GetState() const noexcept { return state_; }

        bool IsReady() const noexcept { return state_ == TextureState::Ready; }
        bool IsFailed() const noexcept { return state_ == TextureState::Failed; }

        uint32_t GetWidth() const noexcept { return desc_.width; }
        uint32_t GetHeight() const noexcept { return desc_.height; }
        TextureFormat GetFormat() const noexcept { return desc_.format; }

    private:
        friend class ResourceManager;

        void SetDesc(const TextureDesc& desc) noexcept { desc_ = desc; }
        void SetState(TextureState state) noexcept { state_ = state; }

    private:
        std::string path_;
        TextureDesc desc_{};
        TextureState state_ = TextureState::Empty;
    };
}