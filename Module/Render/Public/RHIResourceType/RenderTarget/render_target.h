#pragma once

#include <cstdint>
#include <array>
#include "Render/Public/rhi_resource_handle.h"
#include "Render/Public/RHIResourceType/Texture/texture.h"

namespace Render {

// Attachments are borrowed. Their owning leases must outlive this framebuffer.
struct RenderTargetSpec {
    std::uint32_t width = 0, height = 0;
    std::uint32_t rhi_id = 0;
    RenderResourceHandle<RHITextureSpec> color;
    RenderResourceHandle<RHITextureSpec> depth;
    std::uint32_t samples = 1;
    // Optional dense color attachments 1..3. Attachment zero remains color.
    std::array<RenderResourceHandle<RHITextureSpec>,3> additionalColors{};
};

struct CreateRenderTargetDesc {
    RenderResourceHandle<RHITextureSpec> color;
    RenderResourceHandle<RHITextureSpec> depth;
    std::array<RenderResourceHandle<RHITextureSpec>,3> additionalColors{};
};

inline bool AreRenderTargetAttachmentsCompatible(const RHITextureSpec* color,
                                                 const RHITextureSpec* depth) {
    if (!color && !depth) return false;
    const auto validSize = [](const RHITextureSpec* texture) {
        return !texture || (texture->width != 0 && texture->height != 0 && !texture->mipmaps &&
                            IsValidTextureSampleCount(texture->samples));
    };
    if (!validSize(color) || !validSize(depth)) return false;
    if (color && (IsDepthFormat(color->textureDataStoreType) ||
                  GetTextureFormatByteSize(color->textureDataStoreType) == 0)) return false;
    if (depth && !IsDepthFormat(depth->textureDataStoreType)) return false;
    return !color || !depth || (color->width == depth->width && color->height == depth->height &&
                               color->samples == depth->samples);
}

inline bool AreRenderTargetAttachmentsCompatible(const RHITextureSpec* color,
                                                 const RHITextureSpec* depth,
                                                 const std::array<const RHITextureSpec*,3>& additionalColors) {
    if (!AreRenderTargetAttachmentsCompatible(color,depth)) return false;
    bool gap=false;
    for (std::size_t i=0;i<additionalColors.size();++i) {
        const auto* attachment=additionalColors[i];
        if (!attachment) { gap=true; continue; }
        if (gap || !color || !AreRenderTargetAttachmentsCompatible(attachment,nullptr) ||
            attachment->width!=color->width || attachment->height!=color->height ||
            attachment->samples!=color->samples) return false;
        if (attachment==color) return false;
        for (std::size_t earlier=0;earlier<i;++earlier)
            if (attachment==additionalColors[earlier]) return false;
    }
    return true;
}

} // namespace Render
