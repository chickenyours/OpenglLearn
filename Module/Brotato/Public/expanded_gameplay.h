#pragma once
#include "game_components.h"

namespace Brotato {
// New gameplay balance, separate from the recovered source's one-hit rules.
inline Config ExpandedGameplay(Config config = {}) {
    config.expanded = true;
    config.builds = true;
    config.arsenal = true;
    config.traits = true;
    config.campaign = true;
    config.profiles = true;
    config.encounters = true;
    constexpr int damage[]{2,3,1,2,1,1};
    for (std::size_t i = 0; i < WeaponCount; ++i) config.weapons[i].damage = damage[i];
    constexpr float knockback[]{4,9,0,6,2,5};
    for (std::size_t i = 0; i < WeaponCount; ++i) config.weapons[i].knockback = knockback[i];
    config.weapons[WeaponIndex(WeaponKind::Wand)].splashRadius = 1.15f;
    return config;
}
} // namespace Brotato
