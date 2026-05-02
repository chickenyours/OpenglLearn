#pragma once

#include "Render/Public/RHI/Upload/Texture/RHI_texture_desc.h"

namespace Render::Upload{
    struct RHITexture{
        unsigned int id;
        RHITextureDesc desc_;
    };
}