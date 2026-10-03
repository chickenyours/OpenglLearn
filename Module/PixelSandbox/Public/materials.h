#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace PixelSandbox {
enum class Material : std::uint8_t {
    Empty, Wall, Sand, Water, Stone, Wood, Fire, Smoke, Steam, Oil, Lava,
    Ice, Acid, Metal, Spark, Gunpowder, Plant, Seed, Salt, Brine, Glass, Coal, Gas,
    // Append IDs: existing saves retain the original material numbering.
    Soil, Mud, Snow, Wax, MoltenWax, Oxygen, Hydrogen, CarbonDioxide, DryIce,
    Fuse, Battery, Heater, Cooler, Clone, Count
};
enum class Matter : std::uint8_t { Empty, Solid, Powder, Liquid, Gas };
struct MaterialInfo {
    const char* name;
    Matter phase;
    int density;
    float conductivity;
    float temperature;
    std::uint16_t lifetime;
    std::uint8_t dispersion;
    bool flammable;
    bool conductive;
    std::uint32_t color; // 0xRRGGBB, display encoded.
};
inline constexpr std::size_t MaterialCount = static_cast<std::size_t>(Material::Count);
const MaterialInfo& Info(Material material);
std::span<const MaterialInfo> Materials();
bool IsMaterial(Material material) noexcept;
bool IsDevice(Material material) noexcept;
const char* MaterialHint(Material material) noexcept;
// One compact particle record; it travels with the material when cells swap.
struct Cell {
    Material material = Material::Empty;
    std::uint8_t variant = 0;
    std::uint16_t life = 0;
    float temperature = 20.0f; // Celsius, simplified equal heat capacities.
    std::uint32_t stamp = 0;
    std::uint16_t charge = 0; // pulse plus refractory countdown for conductors.
    std::uint16_t flags = 0;
};
static_assert(sizeof(Cell) == 16);
class SandboxModule;
// Returns true if the source particle was replaced/consumed. The engine then
// skips movement. Rules use the module mutation API to wake neighboring chunks.
bool ApplyMaterialRule(SandboxModule& world, int x, int y);
} // namespace PixelSandbox
