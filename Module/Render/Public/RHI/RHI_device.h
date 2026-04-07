#pragma once

#include <memory>
#include "Render/Public/RHI/rhi_texture.h"

namespace Render::RHI {

    class Device {
    public:
        virtual ~Device() = default;

        virtual std::unique_ptr<Texture> CreateTexture(const TextureDesc& desc) = 0;
    };

    std::unique_ptr<Device> CreateOpenGLDevice();

}
