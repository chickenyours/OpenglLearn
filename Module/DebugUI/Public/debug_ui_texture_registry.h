#pragma once

#include <limits>
#include <map>
#include <memory>
#include <imgui.h>
#include "Render/Public/rhi_resource_handle.h"

namespace Render { struct RHITextureSpec; }

namespace DebugUI::Detail {
struct TextureBinding {
    Render::RenderResourceHandle<Render::RHITextureSpec> texture;
    std::shared_ptr<const void> owner;
};

// Shared immutable bindings let a frame retain a registration after removal.
class TextureRegistry {
public:
    ImTextureID Register(Render::RenderResourceHandle<Render::RHITextureSpec> texture,
                         std::shared_ptr<const void> owner) {
        if (!texture.IsValid() || !owner || next_ == std::numeric_limits<ImTextureID>::max()) return 0;
        const auto id = next_++;
        bindings_.emplace(id, std::make_shared<const TextureBinding>(TextureBinding{texture, std::move(owner)}));
        return id;
    }
    std::shared_ptr<const TextureBinding> Find(ImTextureID id) const {
        auto found = bindings_.find(id);
        return found == bindings_.end() ? nullptr : found->second;
    }
    bool Unregister(ImTextureID id) { return bindings_.erase(id) != 0; }
    void Clear() { bindings_.clear(); } // IDs are never recycled.
private:
    ImTextureID next_ = 1;
    std::map<ImTextureID, std::shared_ptr<const TextureBinding>> bindings_;
};
} // namespace DebugUI::Detail
