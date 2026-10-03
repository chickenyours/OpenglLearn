#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>
#include <glm/glm.hpp>

namespace Render::SpriteDetail {

struct NineSlicePatch {
    glm::vec2 center, size;
    // Normalized top-left u/v and width/height within the original image frame.
    glm::vec4 region;
};

inline constexpr std::size_t MaxNineSlicePatches = 4096;

namespace NineSliceDetail {
struct AxisPatch { double center, size, source, extent; };

inline std::vector<AxisPatch> BuildAxis(double target, double native, double low, double high, bool tiled) {
    if (low + high > native)
        throw std::invalid_argument("Nine-slice borders exceed the native image size");
    const double border = low + high;
    const bool compressed = border > target;
    const double outputLow = compressed ? target * low / border : low;
    const double outputHigh = compressed ? target - outputLow : high;
    const double middle = compressed ? 0 : target - outputLow - outputHigh;
    const double sourceMiddle = native - low - high;
    if (middle > 0 && sourceMiddle <= 0)
        throw std::invalid_argument("Nine-slice has no source center to fill its target");
    const double repeats = middle <= 0 ? 0 : tiled ? std::ceil(middle / sourceMiddle) : 1;
    const std::size_t edges = std::size_t(outputLow > 0) + std::size_t(outputHigh > 0);
    if (repeats > double(MaxNineSlicePatches - edges))
        throw std::length_error("Nine-slice exceeds its patch capacity");
    std::vector<AxisPatch> result;
    result.reserve(std::size_t(repeats) + edges);
    if (outputLow > 0) result.push_back({(-target + outputLow) * .5, outputLow, 0, low / native});
    for (std::size_t index = 0; index < std::size_t(repeats); ++index) {
        const double offset = tiled ? double(index) * sourceMiddle : 0;
        const double extent = tiled ? std::min(sourceMiddle, middle - offset) : middle;
        result.push_back({-target * .5 + outputLow + offset + extent * .5,
                          extent, low / native, (tiled ? extent : sourceMiddle) / native});
    }
    if (outputHigh > 0)
        result.push_back({(target - outputHigh) * .5, outputHigh, (native - high) / native, high / native});
    return result;
}
} // namespace NineSliceDetail

// Border order is left, bottom, right, top, in the same world units as native.
// Corners retain native size; undersized targets proportionally compress the
// opposing borders and omit the middle. Tiled middle rows/columns repeat at
// native scale, clipping only the final repetition. Non-tiled middle stretches.
// Centers are local y-up coordinates relative to the target rectangle's center.
// Invalid input throws invalid_argument; more than 4096 patches throws length_error.
inline std::vector<NineSlicePatch> BuildNineSlice(glm::vec2 targetSize, glm::vec2 nativeSize,
    glm::vec4 border, bool tiled = true) {
    for (float value : {targetSize.x, targetSize.y, nativeSize.x, nativeSize.y})
        if (!std::isfinite(value) || value <= 0)
            throw std::invalid_argument("Nine-slice sizes must be positive and finite");
    for (float value : {border.x, border.y, border.z, border.w})
        if (!std::isfinite(value) || value < 0)
            throw std::invalid_argument("Nine-slice borders must be nonnegative and finite");
    const auto horizontal = NineSliceDetail::BuildAxis(targetSize.x, nativeSize.x, border.x, border.z, tiled);
    const auto vertical = NineSliceDetail::BuildAxis(targetSize.y, nativeSize.y, border.y, border.w, tiled);
    if (horizontal.size() > MaxNineSlicePatches / vertical.size())
        throw std::length_error("Nine-slice exceeds its patch capacity");
    std::vector<NineSlicePatch> result;
    result.reserve(horizontal.size() * vertical.size());
    for (const auto& y : vertical) {
        for (const auto& x : horizontal) {
            const float left = float(std::clamp(x.source, 0., 1.));
            const float right = float(std::clamp(x.source + x.extent, 0., 1.));
            const float top = float(std::clamp(1 - y.source - y.extent, 0., 1.));
            const float bottom = float(std::clamp(1 - y.source, 0., 1.));
            if (!(right > left && bottom > top))
                throw std::invalid_argument("Nine-slice region is too small to represent");
            result.push_back({{float(x.center), float(y.center)}, {float(x.size), float(y.size)},
                {left, top, right - left, bottom - top}});
        }
    }
    return result;
}

} // namespace Render::SpriteDetail
