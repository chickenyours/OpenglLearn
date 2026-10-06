#pragma once
#include "weapon_definitions.h"

namespace Brotato {
enum class WeaponFamily { Elemental, Precision, Ballistic, Count };
inline constexpr std::size_t FamilyCount = std::size_t(WeaponFamily::Count);
inline constexpr const char* FamilyNames[]{"ELEMENTAL","PRECISION","BALLISTIC"};
inline WeaponFamily Family(WeaponKind kind) {
    constexpr WeaponFamily families[]{WeaponFamily::Elemental,WeaponFamily::Elemental,WeaponFamily::Precision,
        WeaponFamily::Precision,WeaponFamily::Ballistic,WeaponFamily::Ballistic};
    return ValidWeapon(kind)?families[WeaponIndex(kind)]:WeaponFamily::Count;
}
enum class WeaponAffix { None, Burning, Chilling, Piercing, Vampiric, Count };
inline constexpr const char* AffixNames[]{"STANDARD","BURNING","CHILLING","PIERCING","VAMPIRIC"};
inline constexpr const char* AffixDescriptions[]{"STANDARD / NO EXTRA EFFECT", "BURN 2S / TICKS EVERY 0.5S / STRONGER AT TIER 3",
    "SLOW 1.5S / 35-50% BY TIER", "PIERCE 1 TARGET / 2 AT TIER 3", "HEAL 10% OF DIRECT DAMAGE"};
inline bool ValidAffix(WeaponKind kind,WeaponAffix affix) {
    return ValidWeapon(kind) && std::size_t(affix)<std::size_t(WeaponAffix::Count) &&
        (affix!=WeaponAffix::Piercing || kind==WeaponKind::Wand || kind==WeaponKind::Gun || kind==WeaponKind::Burst);
}
} // namespace Brotato
