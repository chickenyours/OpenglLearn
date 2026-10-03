#pragma once
#include "materials.h"
#include <algorithm>
#include <array>
#include <vector>

namespace PixelSandbox {
enum class PaletteGroup { All, Solid, Powder, Liquid, Gas, Device, Count };
inline constexpr std::array<const char*, 6> PaletteGroupNames{
    "ALL", "SOLID", "POWDER", "LIQUID", "GAS", "DEVICE"};
inline constexpr int PalettePageSize = 20;

inline PaletteGroup GroupOf(Material material) {
    if (IsDevice(material)) return PaletteGroup::Device;
    switch (Info(material).phase) {
    case Matter::Solid: return PaletteGroup::Solid;
    case Matter::Powder: return PaletteGroup::Powder;
    case Matter::Liquid: return PaletteGroup::Liquid;
    case Matter::Gas: return PaletteGroup::Gas;
    default: return PaletteGroup::All;
    }
}

// Catalog-derived palette: new materials need no separate hard-coded UI list.
inline std::vector<Material> PaletteMaterials(PaletteGroup group) {
    std::vector<Material> result;
    for (std::size_t i = 1; i < MaterialCount; ++i) {
        const auto material = static_cast<Material>(i);
        if (group == PaletteGroup::All || GroupOf(material) == group) result.push_back(material);
    }
    return result;
}
inline int PalettePages(PaletteGroup group) {
    return std::max(1, (int(PaletteMaterials(group).size()) + PalettePageSize - 1) / PalettePageSize);
}
inline std::vector<Material> PaletteMaterials(PaletteGroup group, int page) {
    const auto materials = PaletteMaterials(group);
    page = std::clamp(page, 0, std::max(0, (int(materials.size()) - 1) / PalettePageSize));
    const auto begin = materials.begin() + std::min(int(materials.size()), page * PalettePageSize);
    const auto end = begin + std::min(int(materials.end() - begin), PalettePageSize);
    return {begin, end};
}
} // namespace PixelSandbox
