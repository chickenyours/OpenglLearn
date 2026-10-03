#include "materials.h"
#include "sandbox_module.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace PixelSandbox {
namespace {
// Original rules for this module. The reference games informed the feature set,
// but their source code and numeric rule tables are not incorporated here.
constexpr std::array<MaterialInfo, MaterialCount> catalog{{
    {"Empty",     Matter::Empty,     0, 0.00f,   20,   0, 0, false, false, 0x111924},
    {"Wall",      Matter::Solid, 10000, 0.00f,   20,   0, 0, false, false, 0x738294},
    {"Sand",      Matter::Powder, 1600, 0.14f,   20,   0, 1, false, false, 0xE6BD6B},
    {"Water",     Matter::Liquid, 1000, 0.20f,   20,   0, 5, false, true,  0x398EE8},
    {"Stone",     Matter::Solid,  2600, 0.18f,   20,   0, 0, false, false, 0x85828C},
    {"Wood",      Matter::Solid,   700, 0.05f,   20,   0, 0, true,  false, 0x956237},
    {"Fire",      Matter::Gas,       2, 0.09f,  650,  45, 2, false, false, 0xFF842D},
    {"Smoke",     Matter::Gas,       3, 0.02f,   90, 210, 4, false, false, 0x707480},
    {"Steam",     Matter::Gas,       5, 0.06f,  125,   0, 4, false, false, 0xCBDDE4},
    {"Oil",       Matter::Liquid,  800, 0.06f,   20,   0, 4, true,  false, 0xB0923E},
    {"Lava",      Matter::Liquid, 3000, 0.20f, 1200,   0, 1, false, false, 0xFF4F26},
    {"Ice",       Matter::Solid,   920, 0.15f,  -15,   0, 0, false, false, 0x8DDEF1},
    {"Acid",      Matter::Liquid, 1050, 0.12f,   20,  36, 3, false, false, 0xC3EF54},
    {"Metal",     Matter::Solid,  7800, 0.42f,   20,   0, 0, false, true,  0xA9BDCC},
    {"Spark",     Matter::Gas,       1, 0.12f,  950,   7, 1, false, false, 0xFFF09A},
    {"Gunpowder", Matter::Powder, 1400, 0.08f,   20,   0, 1, true,  false, 0x676079},
    {"Plant",     Matter::Solid,   600, 0.06f,   20,   0, 0, true,  false, 0x49B467},
    {"Seed",      Matter::Powder,  850, 0.05f,   20,   0, 1, true,  false, 0xAFCE62},
    {"Salt",      Matter::Powder, 1800, 0.12f,   20,   0, 1, false, false, 0xF0E9D6},
    {"Brine",     Matter::Liquid, 1100, 0.24f,   20,   0, 4, false, true,  0x659ECA},
    {"Glass",     Matter::Solid,  2400, 0.05f,   20,   0, 0, false, false, 0x77B5AC},
    {"Coal",      Matter::Powder, 1300, 0.10f,   20,   0, 1, true,  false, 0x45434D},
    {"Gas",       Matter::Gas,       4, 0.03f,   20,   0, 5, true,  false, 0xC290C9},
    {"Soil",      Matter::Powder, 1500, 0.08f,   20,   0, 1, false, false, 0x87633E},
    {"Mud",       Matter::Liquid, 1700, 0.14f,   20,   0, 1, false, false, 0x5F4836},
    {"Snow",      Matter::Powder,  400, 0.04f,  -12,   0, 1, false, false, 0xE5F4FF},
    {"Wax",       Matter::Solid,   900, 0.04f,   20,   0, 0, true,  false, 0xEACE92},
    {"Molten Wax",Matter::Liquid,  850, 0.04f,   80,   0, 2, true,  false, 0xF0AF56},
    {"Oxygen",    Matter::Gas,       6, 0.03f,   20,   0, 4, false, false, 0x98CFE8},
    {"Hydrogen",  Matter::Gas,       1, 0.02f,   20,   0, 5, true,  false, 0xE8ACD8},
    {"CO2",       Matter::Gas,       9, 0.03f,   20,   0, 3, false, false, 0xB6B7C1},
    {"Dry Ice",   Matter::Powder, 1550, 0.12f,  -90,   0, 1, false, false, 0xB7BBEC},
    {"Fuse",      Matter::Solid,  1200, 0.02f,   20,   0, 0, true,  false, 0xCA985C},
    {"Battery",   Matter::Solid,  5000, 0.10f,   20,   0, 0, false, false, 0xE5CF50},
    {"Heater",    Matter::Solid,  6500, 0.12f,   20,   0, 0, false, true,  0xD96552},
    {"Cooler",    Matter::Solid,  6500, 0.12f,   20,   0, 0, false, true,  0x5B9ED2},
    {"Clone",     Matter::Solid,  9000, 0.00f,   20,   0, 0, false, false, 0xC18CEC},
}};
constexpr int dx[8] = {0, 1, 0, -1, 1, 1, -1, -1};
constexpr int dy[8] = {-1, 0, 1, 0, -1, 1, 1, -1};
constexpr std::uint16_t Burning = 1;
constexpr std::uint16_t Pulse = 24;
constexpr std::uint16_t PropagateAt = 22;

bool IsWater(Material material) {
    return material == Material::Water || material == Material::Brine;
}

bool EmitsIgnitionPulse(Material material) {
    return material != Material::Heater && material != Material::Cooler;
}

float IgnitionTemperature(Material material) {
    switch (material) {
    case Material::Wood: return 260;
    case Material::Oil: return 220;
    case Material::Coal: return 350;
    case Material::Gunpowder: return 250;
    case Material::Wax: case Material::MoltenWax: return 300;
    case Material::Fuse: return 200;
    case Material::Hydrogen: return 220;
    default: return 180;
    }
}

std::uint16_t FuelLife(Material material) {
    switch (material) {
    case Material::Wood: return 140;
    case Material::Oil: return 75;
    case Material::Coal: return 180;
    case Material::Plant: return 65;
    case Material::Wax: case Material::MoltenWax: return 110;
    case Material::Fuse: return 18;
    default: return 45;
    }
}

// Ignition changes state, not material. Stamp the receiver so an ignition wave
// cannot traverse an arbitrary number of neighbors in one scan.
void Ignite(SandboxModule& world, int x, int y, std::uint32_t stamp) {
    const auto cell = world.At(x, y);
    if (!Info(cell.material).flammable || (cell.flags & Burning)) return;
    auto& target = world.Edit(x, y);
    target.temperature = std::max(target.temperature, IgnitionTemperature(cell.material) + 20);
    target.flags |= Burning;
    target.life = FuelLife(cell.material);
    target.stamp = stamp;
}

void Energize(SandboxModule& world, int x, int y, std::uint32_t stamp) {
    const auto& cell = world.At(x, y);
    if (!Info(cell.material).conductive || cell.charge != 0) return;
    auto& target = world.Edit(x, y);
    target.charge = Pulse;
    target.stamp = stamp;
    target.temperature = std::min(2000.0f, target.temperature + 2.0f);
}

void UpdateConductor(SandboxModule& world, int x, int y) {
    const auto source = world.At(x, y);
    if (!source.charge) return;
    world.Edit(x, y).charge--;
    if (source.charge == PropagateAt) {
        for (int n = 0; n < 4; ++n) {
            Energize(world, x + dx[n], y + dy[n], source.stamp);
            // Temperature devices affect fuel through their thermal rule. In
            // particular, a powered cooler must never act as a spark igniter.
            if (EmitsIgnitionPulse(source.material))
                Ignite(world, x + dx[n], y + dy[n], source.stamp);
        }
    }
}

bool Corrodible(Material material) {
    switch (material) {
    case Material::Sand: case Material::Stone: case Material::Wood:
    case Material::Metal: case Material::Gunpowder: case Material::Plant:
    case Material::Seed: case Material::Coal: case Material::Ice:
    case Material::Soil: case Material::Mud: case Material::Snow:
    case Material::Wax: case Material::MoltenWax: case Material::Fuse:
    case Material::Battery: case Material::Heater: case Material::Cooler:
        return true;
    default: return false;
    }
}

bool HasIgniter(const SandboxModule& world, int x, int y) {
    for (int n = 0; n < 8; ++n) {
        const auto& neighbor = world.At(x + dx[n], y + dy[n]);
        if (neighbor.material == Material::Fire || neighbor.material == Material::Spark ||
            neighbor.material == Material::Lava ||
            (Info(neighbor.material).conductive && neighbor.charge > 16 && EmitsIgnitionPulse(neighbor.material))) return true;
    }
    return false;
}

bool BurnFuel(SandboxModule& world, int x, int y) {
    auto source = world.At(x, y);
    // Contact with liquid extinguishes burning solid fuel. Hot liquid still
    // transfers heat through the separate heat system.
    for (int n = 0; n < 4; ++n) {
        const int nx = x + dx[n], ny = y + dy[n];
        if (IsWater(world.At(nx, ny).material)) {
            auto& fuel = world.Edit(x, y);
            fuel.flags &= static_cast<std::uint16_t>(~Burning);
            fuel.life = 0;
            fuel.temperature = std::min(fuel.temperature, 90.0f);
            if (world.Chance(x, y, 100 + n, 0.12f))
                world.SetCell(nx, ny, Material::Steam, 120);
            return false;
        }
    }
    if (!source.life) {
        const bool charcoal = source.material == Material::Wood && world.Chance(x, y, 110, 0.35f);
        world.SetCell(x, y, charcoal ? Material::Coal : Material::Smoke, charcoal ? 140.0f : 110.0f);
        return true;
    }
    auto& fuel = world.Edit(x, y);
    --fuel.life;
    fuel.temperature = std::max(fuel.temperature, 430.0f);
    world.AddPressure(x, y, 0.015f);
    const int start = static_cast<int>(world.Random(x, y, 111) & 7u);
    bool emitted = false;
    for (int k = 0; k < 8; ++k) {
        const int n = (start + k) & 7;
        const int nx = x + dx[n], ny = y + dy[n];
        const auto neighbor = world.At(nx, ny);
        if (neighbor.material == Material::Empty && !emitted && world.Chance(x, y, 120 + n, 0.12f)) {
            world.SetCell(nx, ny, Material::Fire, 600);
            emitted = true;
        } else if (Info(neighbor.material).flammable && world.Chance(x, y, 130 + n, 0.08f)) {
            Ignite(world, nx, ny, source.stamp);
        }
    }
    return false;
}

bool UpdateFire(SandboxModule& world, int x, int y) {
    const auto source = world.At(x, y);
    for (int n = 0; n < 4; ++n) {
        const int nx = x + dx[n], ny = y + dy[n];
        if (IsWater(world.At(nx, ny).material)) {
            world.SetCell(x, y, Material::Smoke, 90);
            if (world.Chance(x, y, 200 + n, 0.18f))
                world.SetCell(nx, ny, Material::Steam, 120);
            return true;
        }
    }
    if (source.life <= 1 || source.temperature < 110) {
        world.SetCell(x, y, Material::Smoke, std::min(source.temperature, 100.0f));
        return true;
    }
    auto& fire = world.Edit(x, y);
    --fire.life;
    fire.temperature = std::max(fire.temperature, 480.0f);
    world.AddPressure(x, y, 0.025f);
    for (int n = 0; n < 8; ++n) {
        const int nx = x + dx[n], ny = y + dy[n];
        const auto neighbor = world.At(nx, ny);
        if (Info(neighbor.material).flammable)
            Ignite(world, nx, ny, source.stamp);
        else if (neighbor.material != Material::Empty && neighbor.material != Material::Wall)
            world.SetTemperature(nx, ny, std::min(2000.0f, neighbor.temperature + 4.0f));
    }
    return false;
}

bool UpdateAcid(SandboxModule& world, int x, int y) {
    if (!world.At(x, y).life) {
        world.SetCell(x, y, Material::Empty);
        return true;
    }
    const int start = static_cast<int>(world.Random(x, y, 300) & 7u);
    for (int k = 0; k < 8; ++k) {
        const int n = (start + k) & 7;
        const int nx = x + dx[n], ny = y + dy[n];
        const auto neighbor = world.At(nx, ny);
        if (!Corrodible(neighbor.material)) continue;
        world.Wake(x, y);
        const float rate = neighbor.material == Material::Stone ? 0.045f : 0.18f;
        if (!world.Chance(x, y, 310 + n, rate)) continue;
        world.SetCell(nx, ny, Material::Empty);
        auto& acid = world.Edit(x, y);
        --acid.life;
        acid.temperature = std::min(200.0f, acid.temperature + 3.0f);
        if (!acid.life) {
            world.SetCell(x, y, Material::Empty);
            return true;
        }
        break; // At most one dissolved cell per acid particle and tick.
    }
    return false;
}

bool UpdateVegetation(SandboxModule& world, int x, int y) {
    const auto source = world.At(x, y);
    if (source.temperature < 2 || source.temperature > 75) return false;
    if (source.material == Material::Seed) {
        for (int n = 0; n < 8; ++n) {
            const int nx = x + dx[n], ny = y + dy[n];
            if (world.At(nx, ny).material != Material::Mud) continue;
            world.Wake(x, y);
            if (!world.Chance(x, y, 450 + n, .12f)) continue;
            world.SetCell(nx, ny, Material::Soil, world.At(nx, ny).temperature);
            world.SetCell(x, y, Material::Plant, source.temperature);
            return true;
        }
    }
    const int start = static_cast<int>(world.Random(x, y, 400) & 7u);
    for (int k = 0; k < 8; ++k) {
        const int n = (start + k) & 7;
        const int nx = x + dx[n], ny = y + dy[n];
        if (world.At(nx, ny).material != Material::Water) continue;
        world.Wake(x, y);
        if (!world.Chance(x, y, 410 + n, source.material == Material::Seed ? 0.15f : 0.035f)) continue;
        if (source.material == Material::Seed) {
            world.SetCell(nx, ny, Material::Empty);
            world.SetCell(x, y, Material::Plant, source.temperature);
            return true;
        }
        // Growth consumes one water pixel and occupies one adjacent empty pixel.
        for (int g = 0; g < 8; ++g) {
            const int d = (start + g) & 7;
            const int gx = x + dx[d], gy = y + dy[d];
            if (world.At(gx, gy).material == Material::Empty) {
                world.SetCell(nx, ny, Material::Empty);
                world.SetCell(gx, gy, Material::Plant, source.temperature);
                return false;
            }
        }
        break;
    }
    return false;
}

bool UpdateSoil(SandboxModule& world, int x, int y) {
    for (int n = 0; n < 4; ++n) {
        const int nx = x + dx[n], ny = y + dy[n];
        if (world.At(nx, ny).material != Material::Water) continue;
        const float temperature = (world.At(x, y).temperature + world.At(nx, ny).temperature) * .5f;
        world.SetCell(nx, ny, Material::Empty);
        world.SetCell(x, y, Material::Mud, temperature);
        return true;
    }
    return false;
}

bool UpdateFuse(SandboxModule& world, int x, int y) {
    const auto source = world.At(x, y);
    for (int n = 0; n < 4; ++n) {
        if (!IsWater(world.At(x + dx[n], y + dy[n]).material)) continue;
        if (source.flags & Burning) {
            auto& fuse = world.Edit(x, y);
            fuse.flags &= static_cast<std::uint16_t>(~Burning);
            fuse.life = 0;
            fuse.temperature = std::min(90.0f, source.temperature);
        }
        return false;
    }
    if (!(source.flags & Burning)) {
        if (source.temperature >= IgnitionTemperature(Material::Fuse) || HasIgniter(world, x, y))
            Ignite(world, x, y, source.stamp);
        return false;
    }
    if (source.life > 1) { --world.Edit(x, y).life; return false; }
    // A segment finishes before lighting the next; no same-tick chain traversal.
    world.SetCell(x, y, Material::Fire, 650);
    for (int n = 0; n < 4; ++n) Ignite(world, x + dx[n], y + dy[n], source.stamp);
    return true;
}

bool UpdateDevice(SandboxModule& world, int x, int y) {
    const auto source = world.At(x, y);
    if (source.material == Material::Battery) {
        for (int n = 0; n < 4; ++n) {
            const int nx = x + dx[n], ny = y + dy[n];
            if (!Info(world.At(nx, ny).material).conductive) continue;
            world.Wake(x, y); // Retry after the conductor's refractory interval.
            Energize(world, nx, ny, source.stamp);
        }
    } else if (source.material == Material::Heater || source.material == Material::Cooler) {
        if (source.charge > 16) {
            const float amount = source.material == Material::Heater ? 25.0f : -25.0f;
            const float low = source.material == Material::Heater ? -273.0f : -100.0f;
            const float high = source.material == Material::Heater ? 450.0f : 4000.0f;
            for (int n = 0; n < 4; ++n) {
                const int nx = x + dx[n], ny = y + dy[n];
                const auto& target = world.At(nx, ny);
                if (target.material == Material::Empty || target.material == Material::Wall || IsDevice(target.material)) continue;
                // Heating cannot cool an already hotter target (and vice versa).
                if ((amount > 0 && target.temperature < high) || (amount < 0 && target.temperature > low))
                    world.SetTemperature(nx, ny, std::clamp(target.temperature + amount, low, high));
            }
        }
    } else if (source.material == Material::Clone) {
        auto target = static_cast<Material>(source.flags >> 8);
        if (target == Material::Empty) {
            for (int n = 0; n < 8; ++n) {
                const auto candidate = world.At(x + dx[n], y + dy[n]).material;
                if (candidate == Material::Empty || candidate == Material::Wall || IsDevice(candidate)) continue;
                target = candidate;
                world.Edit(x, y).flags = std::uint16_t(std::uint8_t(target)) << 8;
                break;
            }
        }
        if (!IsMaterial(target) || target == Material::Empty || target == Material::Wall || IsDevice(target)) return false;
        const int start = int(world.Random(x, y, 580) & 7u);
        for (int k = 0; k < 8; ++k) {
            const int n = (start + k) & 7, nx = x + dx[n], ny = y + dy[n];
            if (world.At(nx, ny).material != Material::Empty) continue;
            world.Wake(x, y);
            // At most one new pixel per device per eight ticks; never overwrite.
            if ((world.Stats().tick & 7u) == 0) world.SetCell(nx, ny, target);
            break;
        }
    }
    return false;
}

bool UpdateCarbonDioxide(SandboxModule& world, int x, int y) {
    for (int n = 0; n < 8; ++n) {
        const int nx = x + dx[n], ny = y + dy[n];
        const auto neighbor = world.At(nx, ny);
        if (neighbor.material == Material::Fire) world.SetCell(nx, ny, Material::Smoke, 60);
        else if (neighbor.flags & Burning) {
            auto& fuel = world.Edit(nx, ny);
            fuel.flags &= static_cast<std::uint16_t>(~Burning);
            fuel.life = 0;
            fuel.temperature = std::min(neighbor.temperature, 90.0f);
            fuel.stamp = world.At(x, y).stamp;
        }
    }
    return false;
}
} // namespace

bool IsMaterial(Material material) noexcept {
    return static_cast<std::size_t>(material) < MaterialCount;
}

const MaterialInfo& Info(Material material) {
    return catalog[IsMaterial(material) ? static_cast<std::size_t>(material) : 0];
}

std::span<const MaterialInfo> Materials() { return catalog; }

bool IsDevice(Material material) noexcept {
    return material == Material::Battery || material == Material::Heater ||
           material == Material::Cooler || material == Material::Clone;
}

const char* MaterialHint(Material material) noexcept {
    switch (material) {
    case Material::Soil: return "WATER + SOIL = MUD";
    case Material::Mud: return "GROWS SEEDS / HEAT TO DRY";
    case Material::Snow: return "POWDER SNOW MELTS INTO WATER";
    case Material::Wax: return "MELTS AT 62 C / FLAMMABLE";
    case Material::MoltenWax: return "COOLS INTO WAX BELOW 52 C";
    case Material::Oxygen: return "BOOSTS FIRE / REACTS WITH HYDROGEN";
    case Material::Hydrogen: return "IGNITE WITH OXYGEN FOR STEAM";
    case Material::CarbonDioxide: return "EXTINGUISHES FIRE AND BURNING FUEL";
    case Material::DryIce: return "SUBLIMATES INTO CO2 ABOVE -78 C";
    case Material::Fuse: return "DELAYED FLAME / WATER STOPS IT";
    case Material::Battery: return "REPEATING PULSES INTO CONDUCTORS";
    case Material::Heater: return "POWER TO HEAT NEIGHBORING MATTER";
    case Material::Cooler: return "POWER TO COOL NEIGHBORING MATTER";
    case Material::Clone: return "LEARNS A NEIGHBOR / COPIES TO EMPTY";
    case Material::Water: return "FREEZES / BOILS / PULSE ELECTROLYSIS";
    default: return "COMBINE MATERIALS, HEAT AND CURRENT";
    }
}

bool ApplyMaterialRule(SandboxModule& world, int x, int y) {
    const auto source = world.At(x, y);
    const auto material = source.material;
    if (material == Material::Empty || material == Material::Wall) return false;
    if (Info(material).conductive) UpdateConductor(world, x, y);

    // Keep the extended dispatch out of the original materials' hot path.
    if (material >= Material::Soil) {
        if (IsDevice(material)) return UpdateDevice(world, x, y);
        if (material == Material::Fuse) return UpdateFuse(world, x, y);
        if (material == Material::Soil) return UpdateSoil(world, x, y);
        if (material == Material::CarbonDioxide) return UpdateCarbonDioxide(world, x, y);
        if (material == Material::Snow && source.temperature >= 2) {
            world.SetCell(x, y, Material::Water, source.temperature);
            return true;
        }
        if (material == Material::DryIce && source.temperature >= -78) {
            world.SetCell(x, y, Material::CarbonDioxide, source.temperature);
            world.AddPressure(x, y, .2f);
            return true;
        }
        if (material == Material::Mud && source.temperature >= 100) {
            // Wait for room to release the stored water instead of silently deleting it.
            for (int n = 0; n < 4; ++n) {
                if (world.At(x + dx[n], y + dy[n]).material != Material::Empty) continue;
                world.SetCell(x + dx[n], y + dy[n], Material::Steam, 110);
                world.SetCell(x, y, Material::Soil, source.temperature);
                return true;
            }
        }
        if ((material == Material::Wax && source.temperature >= 62) ||
            (material == Material::MoltenWax && source.temperature <= 52)) {
            world.SetCell(x, y, material == Material::Wax ? Material::MoltenWax : Material::Wax, source.temperature);
            auto& changed = world.Edit(x, y);
            changed.flags = source.flags; changed.life = source.life; // Preserve ongoing combustion.
            return true;
        }
        if (material == Material::Oxygen) {
            for (int n = 0; n < 8; ++n) {
                const auto& neighbor = world.At(x + dx[n], y + dy[n]);
                if (neighbor.material != Material::Fire && !(neighbor.flags & Burning)) continue;
                world.SetCell(x, y, Material::Fire, 850);
                world.AddPressure(x, y, .15f);
                return true;
            }
        }
        if (material == Material::Hydrogen && (source.temperature >= 220 || HasIgniter(world, x, y))) {
            for (int n = 0; n < 8; ++n) {
                const int nx = x + dx[n], ny = y + dy[n];
                if (world.At(nx, ny).material != Material::Oxygen) continue;
                world.Explode(x, y, 5, 2.8f);
                world.SetCell(x, y, Material::Steam, 600);
                world.SetCell(nx, ny, Material::Steam, 600);
                return true;
            }
            world.SetCell(x, y, Material::Fire, 650);
            return true;
        }
    }

    // Temperature transitions have a small hysteresis to prevent flickering.
    if (IsWater(material)) {
        const bool brine = material == Material::Brine;
        if (source.temperature <= (brine ? -8.0f : 0.0f)) {
            world.SetCell(x, y, Material::Ice, source.temperature);
            return true;
        }
        if (!brine && source.charge == PropagateAt) {
            for (int n = 0; n < 4; ++n) {
                const int nx = x + dx[n], ny = y + dy[n];
                if (world.At(nx, ny).material != Material::Empty) continue;
                world.SetCell(x, y, Material::Hydrogen, source.temperature);
                world.SetCell(nx, ny, Material::Oxygen, source.temperature);
                return true;
            }
        }
        if (source.temperature >= (brine ? 105.0f : 100.0f)) {
            world.SetCell(x, y, Material::Steam, std::max(110.0f, source.temperature));
            world.AddPressure(x, y, 0.3f);
            if (brine) {
                for (int n = 0; n < 4; ++n) {
                    if (world.At(x + dx[n], y + dy[n]).material == Material::Empty) {
                        world.SetCell(x + dx[n], y + dy[n], Material::Salt, source.temperature);
                        break;
                    }
                }
            }
            return true;
        }
        for (int n = 0; n < 8; ++n) {
            const int nx = x + dx[n], ny = y + dy[n];
            const auto neighbor = world.At(nx, ny);
            if (neighbor.material == Material::Fire) {
                world.SetCell(nx, ny, Material::Smoke, 90);
                world.SetTemperature(x, y, std::min(99.0f, source.temperature + 8));
            } else if (neighbor.material == Material::Salt && !brine) {
                world.SetCell(nx, ny, Material::Empty);
                world.SetCell(x, y, Material::Brine, source.temperature);
                return true;
            } else if (neighbor.flags & Burning) {
                auto& fuel = world.Edit(nx, ny);
                fuel.flags &= static_cast<std::uint16_t>(~Burning);
                fuel.life = 0;
                fuel.temperature = std::min(fuel.temperature, 90.0f);
            }
        }
        return false;
    }
    if (material == Material::Ice && source.temperature >= 2) {
        world.SetCell(x, y, Material::Water, source.temperature);
        return true;
    }
    if (material == Material::Steam && source.temperature <= 92) {
        world.SetCell(x, y, Material::Water, source.temperature);
        return true;
    }
    if (material == Material::Sand && source.temperature >= 1200) {
        world.SetCell(x, y, Material::Glass, source.temperature);
        return true;
    }
    if (material == Material::Lava) {
        if (source.temperature <= 650) {
            world.SetCell(x, y, Material::Stone, source.temperature);
            return true;
        }
        for (int n = 0; n < 8; ++n) {
            const int nx = x + dx[n], ny = y + dy[n];
            const auto neighbor = world.At(nx, ny);
            if (IsWater(neighbor.material) || neighbor.material == Material::Ice) {
                world.SetCell(nx, ny, Material::Steam, 160);
                world.SetCell(x, y, Material::Stone, 550);
                world.AddPressure(x, y, 1.5f);
                return true;
            }
            if (Info(neighbor.material).flammable) Ignite(world, nx, ny, source.stamp);
        }
        return false;
    }
    if (material == Material::Salt) {
        for (int n = 0; n < 8; ++n) {
            const int nx = x + dx[n], ny = y + dy[n];
            if (world.At(nx, ny).material == Material::Water) {
                world.SetCell(nx, ny, Material::Brine, world.At(nx, ny).temperature);
                world.SetCell(x, y, Material::Empty);
                return true;
            }
        }
    }
    if (material == Material::Fire) return UpdateFire(world, x, y);
    if (material == Material::Smoke) {
        if (source.life <= 1) {
            world.SetCell(x, y, Material::Empty);
            return true;
        }
        --world.Edit(x, y).life;
        return false;
    }
    if (material == Material::Spark) {
        if (source.life <= 1) {
            world.SetCell(x, y, Material::Empty);
            return true;
        }
        --world.Edit(x, y).life;
        for (int n = 0; n < 8; ++n) {
            Energize(world, x + dx[n], y + dy[n], source.stamp);
            Ignite(world, x + dx[n], y + dy[n], source.stamp);
        }
        return false;
    }
    if (material == Material::Acid) return UpdateAcid(world, x, y);
    if (Info(material).flammable) {
        const bool ignites = (source.flags & Burning) ||
            source.temperature >= IgnitionTemperature(material) || HasIgniter(world, x, y);
        if (ignites) {
            if (material == Material::Gas || material == Material::Gunpowder) {
                // Removing the explosive before the impulse also bounds the
                // source reaction; neighbors react on subsequent stamped ticks.
                world.SetCell(x, y, Material::Fire, 850);
                world.Explode(x, y, material == Material::Gas ? 5 : 7,
                              material == Material::Gas ? 2.4f : 4.0f);
                return true;
            }
            if (!(source.flags & Burning)) {
                auto& fuel = world.Edit(x, y);
                fuel.flags |= Burning;
                fuel.life = FuelLife(material);
            }
            return BurnFuel(world, x, y);
        }
    }
    if (material == Material::Plant || material == Material::Seed)
        return UpdateVegetation(world, x, y);
    return false;
}
} // namespace PixelSandbox
