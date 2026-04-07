#pragma once

#include "Render/Public/RHI/RHI_device.h"

namespace Render::Backend::OpenGL {

    class GLDevice final : public Render::RHI::Device {
    public:
        std::unique_ptr<Render::RHI::Texture> CreateTexture(
            const Render::RHI::TextureDesc& desc
        ) override;
    };

}
