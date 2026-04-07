#include "gl_device.h"
#include "gl_texture.h"
#include "Render/Public/RHI/RHI_device.h"

namespace Render::Backend::OpenGL {

    std::unique_ptr<Render::RHI::Texture> GLDevice::CreateTexture(
        const Render::RHI::TextureDesc& desc
    ) {
        auto texture = std::make_unique<GLTexture>(desc);
        if (!texture->Create()) {
            return nullptr;
        }
        return texture;
    }

}

namespace Render::RHI {

    std::unique_ptr<Device> CreateOpenGLDevice() {
        return std::make_unique<Render::Backend::OpenGL::GLDevice>();
    }

}
