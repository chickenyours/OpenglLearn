#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>
#include <glm/glm.hpp>

namespace Render::SpriteDetail {

struct AtlasTile {
    int width = 0, height = 0;
    std::vector<unsigned char> rgba;
};

struct AtlasPixels {
    int width = 0, height = 0;
    std::vector<unsigned char> rgba;
    // Normalized top-left (u, v) and extent (width, height), input tile order.
    std::vector<glm::vec4> regions;
};

// Deterministic shelf packing with extruded edges to prevent neighboring sprites
// bleeding into each other under bilinear sampling. Independent of the RHI/GPU.
inline AtlasPixels PackAtlas(const std::vector<AtlasTile>& tiles, int maxEdge = 4096, int border = 2) {
    if (tiles.empty() || maxEdge < 1 || maxEdge > 4096 || (maxEdge & (maxEdge - 1)) || border < 1 || border > 16)
        throw std::invalid_argument("Invalid sprite atlas packing parameters");
    for (const auto& tile : tiles) {
        if (tile.width < 1 || tile.height < 1 || tile.width > maxEdge - 2 * border ||
            tile.height > maxEdge - 2 * border || tile.rgba.size() != size_t(tile.width) * tile.height * 4)
            throw std::invalid_argument("Invalid or oversized sprite atlas tile");
    }
    std::vector<size_t> order(tiles.size());
    std::iota(order.begin(), order.end(), size_t(0));
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return tiles[a].height > tiles[b].height;
    });
    std::vector<glm::ivec2> positions(tiles.size());
    AtlasPixels atlas;
    for (atlas.width = std::min(256, maxEdge);; atlas.width *= 2) {
        int x = 0, y = 0, row = 0;
        bool fits = true;
        for (size_t index : order) {
            const auto& tile = tiles[index];
            const int w = tile.width + 2 * border, h = tile.height + 2 * border;
            if (w > atlas.width) { fits = false; break; }
            if (x + w > atlas.width) { x = 0; y += row; row = 0; }
            if (y + h > maxEdge) { fits = false; break; }
            positions[index] = {x + border, y + border};
            x += w;
            row = std::max(row, h);
        }
        atlas.height = 1;
        while (atlas.height < y + row) atlas.height *= 2;
        if (fits && atlas.height <= maxEdge) break;
        if (atlas.width == maxEdge) throw std::runtime_error("Sprite atlas exceeds its maximum texture size");
    }
    atlas.rgba.resize(size_t(atlas.width) * atlas.height * 4, 0);
    atlas.regions.resize(tiles.size());
    for (size_t i = 0; i < tiles.size(); ++i) {
        const auto& tile = tiles[i];
        const auto position = positions[i];
        for (int y = -border; y < tile.height + border; ++y) {
            for (int x = -border; x < tile.width + border; ++x) {
                const size_t source = (size_t(std::clamp(y, 0, tile.height - 1)) * tile.width + std::clamp(x, 0, tile.width - 1)) * 4;
                const size_t target = (size_t(position.y + y) * atlas.width + position.x + x) * 4;
                std::memcpy(atlas.rgba.data() + target, tile.rgba.data() + source, 4);
            }
        }
        atlas.regions[i] = {float(position.x) / atlas.width, float(position.y) / atlas.height,
            float(tile.width) / atlas.width, float(tile.height) / atlas.height};
    }
    return atlas;
}

struct SpriteVertex {
    glm::vec2 position, uv;
    glm::vec4 color;
    float nearest = 0;
};
static_assert(sizeof(SpriteVertex) == 9 * sizeof(float));

// Positions use y-up world coordinates; image row zero remains at the top.
inline std::array<SpriteVertex, 4> MakeQuad(glm::vec2 center, glm::vec2 size, float radians,
    glm::vec4 region, glm::vec4 tint, glm::vec2 halfExtent, glm::vec2 cameraCenter = {},
    bool flipX = false, bool flipY = false, bool nearest = false) {
    if (!(halfExtent.x > 0 && halfExtent.y > 0) || !std::isfinite(halfExtent.x) || !std::isfinite(halfExtent.y))
        throw std::invalid_argument("Sprite view extents must be positive and finite");
    const float cosine = std::cos(radians), sine = std::sin(radians);
    const std::array<glm::vec2, 4> corners = {glm::vec2(0, 0), {1, 0}, {1, 1}, {0, 1}};
    std::array<SpriteVertex, 4> vertices;
    for (size_t i = 0; i < corners.size(); ++i) {
        const auto local = (corners[i] - .5f) * size;
        const auto position = center + glm::vec2(cosine * local.x - sine * local.y, sine * local.x + cosine * local.y);
        auto uv = glm::vec2(corners[i].x, 1.f - corners[i].y);
        if (flipX) uv.x = 1.f - uv.x;
        if (flipY) uv.y = 1.f - uv.y;
        vertices[i] = {(position - cameraCenter) / halfExtent,
            glm::vec2(region) + uv * glm::vec2(region.z, region.w), tint, nearest ? 1.f : 0.f};
    }
    return vertices;
}

} // namespace Render::SpriteDetail
