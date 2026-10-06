#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <glm/vec2.hpp>

namespace Brotato::MenuLayout {
inline constexpr float HalfHeight = 6.f;
inline constexpr float HalfWidth = HalfHeight * 16.f / 9.f;
inline constexpr std::size_t Columns = 4, PageSize = 12;

struct Rect {
    glm::vec2 center{}, size{};
    bool Contains(glm::vec2 point) const {
        const auto lower = center - size * .5f, upper = center + size * .5f;
        return point.x >= lower.x && point.x < upper.x && point.y >= lower.y && point.y < upper.y;
    }
};

// OpenGL's viewport origin is at the bottom left; GLFW cursor coordinates are
// logical window pixels from the top left. Both rendering and hit tests use this.
struct Viewport { int x = 0, y = 0, width = 0, height = 0; };
inline Viewport Fit(int framebufferWidth, int framebufferHeight) {
    if (framebufferWidth < 1 || framebufferHeight < 1) return {};
    int width = framebufferWidth, height = int(std::round(width * 9.0 / 16.0));
    if (height > framebufferHeight) {
        height = framebufferHeight;
        width = int(std::round(height * 16.0 / 9.0));
    }
    width = std::max(1, width); height = std::max(1, height);
    return {(framebufferWidth - width) / 2, (framebufferHeight - height) / 2, width, height};
}

inline std::optional<glm::vec2> CursorToCanvas(double cursorX, double cursorY,
    int windowWidth, int windowHeight, int framebufferWidth, int framebufferHeight) {
    if (windowWidth < 1 || windowHeight < 1 || framebufferWidth < 1 || framebufferHeight < 1 ||
        !std::isfinite(cursorX) || !std::isfinite(cursorY)) return std::nullopt;
    const auto viewport = Fit(framebufferWidth, framebufferHeight);
    const double x = cursorX * framebufferWidth / windowWidth - viewport.x;
    const double y = cursorY * framebufferHeight / windowHeight - (framebufferHeight - viewport.y - viewport.height);
    if (x < 0 || y < 0 || x >= viewport.width || y >= viewport.height) return std::nullopt;
    return glm::vec2(float(x / viewport.width * 2 - 1) * HalfWidth,
        float(1 - y / viewport.height * 2) * HalfHeight);
}

inline Rect Card(std::size_t slot) {
    return {{-8.9f + float(slot % Columns) * 2.30f, 3.05f - float(slot / Columns) * 2.05f}, {2.10f, 1.84f}};
}
inline Rect UpgradeCard(std::size_t slot) { return {{-5.2f + float(slot) * 5.2f, -.65f}, {4.7f, 2.3f}}; }
inline Rect ShopCard(std::size_t slot) { return {{-7.2f+float(slot)*4.8f,.65f},{4.25f,3.55f}}; }
inline Rect ShopLock(std::size_t slot) { return {{-7.2f+float(slot)*4.8f,-1.55f},{4.25f,.50f}}; }
inline Rect EquipmentCard(std::size_t slot) { return {{-8.25f+float(slot)*3.3f,.65f},{3.05f,3.55f}}; }
inline constexpr Rect ShopStockTab{{-3.2f,2.83f},{5.9f,.44f}};
inline constexpr Rect ShopEquipmentTab{{3.2f,2.83f},{5.9f,.44f}};
inline constexpr Rect ShopSell{{0,-3.92f},{3.7f,.68f}};
inline constexpr Rect ShopReroll{{-5.7f,-3.92f},{4.5f,.68f}};
inline constexpr Rect ShopNext{{5.3f,-3.92f},{6.0f,.68f}};
inline constexpr Rect Back{{-8.2f, -4.9f}, {3.2f, .76f}};
inline constexpr Rect Forward{{7.8f, -4.9f}, {4.2f, .76f}};
inline constexpr Rect PreviousPage{{-5.5f, -3.75f}, {1.1f, .58f}};
inline constexpr Rect NextPage{{-2.0f, -3.75f}, {1.1f, .58f}};
} // namespace Brotato::MenuLayout
