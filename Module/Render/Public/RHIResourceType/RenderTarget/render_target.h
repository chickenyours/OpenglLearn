#pragma once

#include <cstdint>
#include "Render/Public/rhi_resource_handle.h"
#include "Render/Public/RHIResourceType/Texture/texture.h"

namespace Render {

// Attachments are borrowed. Their owning leases must outlive this framebuffer.
struct RenderTargetSpec {
    std::uint32_t width = 0, height = 0;
    std::uint32_t rhi_id = 0;
    RenderResourceHandle<RHITextureSpec> color;
    RenderResourceHandle<RHITextureSpec> depth;
};

struct CreateRenderTargetDesc {
    RenderResourceHandle<RHITextureSpec> color;
    RenderResourceHandle<RHITextureSpec> depth;
};

inline bool AreRenderTargetAttachmentsCompatible(const RHITextureSpec* color,
                                                 const RHITextureSpec* depth) {
    if (!color && !depth) return false;
    const auto validSize = [](const RHITextureSpec* texture) {
        return !texture || (texture->width != 0 && texture->height != 0 && !texture->mipmaps);
    };
    if (!validSize(color) || !validSize(depth)) return false;
    if (color && (IsDepthFormat(color->textureDataStoreType) ||
                  GetTextureFormatByteSize(color->textureDataStoreType) == 0)) return false;
    if (depth && !IsDepthFormat(depth->textureDataStoreType)) return false;
    return !color || !depth || (color->width == depth->width && color->height == depth->height);
}

} // namespace Render
