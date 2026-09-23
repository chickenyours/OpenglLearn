#pragma once

#include <cstdint>

#include "Render/Private/rhi_device.h"
#include "Render/Public/RHIResourceType/Texture/texture.h"
#include "Render/Public/rhi_resource_handle.h"
#include "Terrain/Public/block_atlas.h"

namespace Terrain {

// Uploads the CPU BlockAtlas to a GPU texture.
//
// RGBA8, nearest filtering (pixel-art) and clamp-to-edge so that sampling never
// wraps into a neighbouring atlas tile.
class TerrainAtlasTexture {
public:
    explicit TerrainAtlasTexture(Render::RHIDevice* device) : device_(device) {}

    void Start() {
        if (device_ == nullptr) {
            failed_ = true;
            return;
        }

        Render::CreateRHITextureSpec spec{};
        spec.width = static_cast<std::uint32_t>(BlockAtlas::Width);
        spec.height = static_cast<std::uint32_t>(BlockAtlas::Height);
        spec.textureDataStoreType = Render::RHITextureFormat::RGBA8;
        spec.textureUseType = Render::RHITextureFormat::RGBA8;
        spec.filterMode = Render::RHIFilterMode::Nearest;
        spec.mipmapMode = Render::RHIMipmapMode::Nearest;
        spec.addressMode = Render::RHIAddressMode::ClampToEdge;
        spec.data = BlockAtlas::Pixels().data();

        device_->async_CreateTexture(
            spec,
            [this](Render::RenderResourceHandle<Render::RHITextureSpec> handle) {
                handle_ = handle;
                if (!handle.IsValid()) failed_ = true;
            });
    }

    bool Ready() const { return handle_.IsValid(); }
    bool Failed() const { return failed_; }
    Render::RenderResourceHandle<Render::RHITextureSpec> Handle() const { return handle_; }

    void Shutdown() {
        if (handle_.IsValid()) {
            device_->async_DeleteTexture(handle_);
            handle_ = {};
        }
    }

private:
    Render::RHIDevice* device_ = nullptr;
    Render::RenderResourceHandle<Render::RHITextureSpec> handle_{};
    bool failed_ = false;
};

} // namespace Terrain
