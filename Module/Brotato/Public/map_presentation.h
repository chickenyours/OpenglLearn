#pragma once

#include "content_catalog.h"
#include "map_layout_data.h"
#include "Render/Public/Sprite/sprite_batch.h"
#include "Render/Public/Sprite/sprite_nine_slice.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace Brotato {

inline void AppendMapImages(std::vector<Render::SpriteImage>& images, const std::filesystem::path& assets) {
    const auto append = [&](const char* name) {
        if (std::none_of(images.begin(), images.end(), [&](const auto& image) { return image.name == name; }))
            images.push_back({name, assets / (std::string(name) + ".png")});
    };
    for (const auto& map : Maps) append(map.floorImage);
    for (const auto& decoration : Decorations) append(decoration.image);
}

namespace MapPresentation {
inline glm::vec2 Rotate(glm::vec2 point, float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    return {point.x * c - point.y * s, point.x * s + point.y * c};
}

// One painter-ordered map submission, usable both in the world and menu cards.
// Positions already contain the imported Sprite pivot correction. Decorations
// are visual only, matching the source's lack of collision components.
inline void Draw(Render::SpriteBatch2D& batch, std::size_t index, glm::vec2 offset, float scale) {
    if (index >= Maps.size() || !std::isfinite(scale) || scale <= 0)
        throw std::invalid_argument("Brotato: invalid map presentation");
    const auto& map = Maps[index];
    const auto patches = Render::SpriteDetail::BuildNineSlice(map.floorSize, MapFloorNativeSize, MapFloorBorder);
    for (const auto& patch : patches) {
        auto local = patch.center;
        if (map.floorFlipY) local.y = -local.y;
        const auto position = map.floorCenter + Rotate(local, map.floorAngle);
        if (!batch.SpriteRegion(map.floorImage, offset + position * scale, patch.size * scale, patch.region,
                map.floorAngle, map.background, false, map.floorFlipY))
            throw std::runtime_error("Brotato: map floor submission failed: " + batch.Error());
    }
    for (const auto& decoration : Decorations) {
        if (decoration.map != index) continue;
        if (!batch.Sprite(decoration.image, offset + decoration.position * scale, decoration.size * scale,
                decoration.angle, decoration.tint, decoration.flipX, decoration.flipY))
            throw std::runtime_error("Brotato: map decoration submission failed: " + batch.Error());
    }
}
} // namespace MapPresentation

inline void DrawArena(Render::SpriteBatch2D& batch, std::size_t map) {
    MapPresentation::Draw(batch, map, {}, 1);
}

inline void DrawMapPreview(Render::SpriteBatch2D& batch, std::size_t map, glm::vec2 center, glm::vec2 extent) {
    if (map >= Maps.size() || extent.x <= 0 || extent.y <= 0) return;
    const auto& definition = Maps[map];
    const float c = std::abs(std::cos(definition.floorAngle)), s = std::abs(std::sin(definition.floorAngle));
    const glm::vec2 worldSize{c * definition.floorSize.x + s * definition.floorSize.y,
                              s * definition.floorSize.x + c * definition.floorSize.y};
    const float scale = std::min(extent.x / worldSize.x, extent.y / worldSize.y);
    MapPresentation::Draw(batch, map, center - definition.floorCenter * scale, scale);
}
} // namespace Brotato
