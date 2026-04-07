#pragma once

#include <cstdint>
#include "Render/Public/RHI/RHI_texture.h"

namespace Render::Backend::OpenGL {

    class GLTexture final : public Render::RHI::Texture {
    public:
        explicit GLTexture(const Render::RHI::TextureDesc& desc);
        ~GLTexture() override;

        GLTexture(const GLTexture&) = delete;
        GLTexture& operator=(const GLTexture&) = delete;

        GLTexture(GLTexture&& other) noexcept;
        GLTexture& operator=(GLTexture&& other) noexcept;

        bool Check() const noexcept override;

        bool Create();
        void Destroy();

        bool SetData(
            const void* data,
            uint32_t mipLevel = 0,
            uint32_t xOffset = 0,
            uint32_t yOffset = 0,
            uint32_t zOffset = 0,
            uint32_t uploadWidth = 0,
            uint32_t uploadHeight = 0,
            uint32_t uploadDepth = 0
        );

        void GenerateMips();
        void Bind(uint32_t unit) const;

        uint32_t GetHandle() const noexcept { return handle_; }
        uint32_t GetTarget() const noexcept { return target_; }

    private:
        bool ValidateDesc() const noexcept;
        void SetupDefaultSamplerState();

    private:
        uint32_t handle_ = 0;
        uint32_t target_ = 0;
    };

}
