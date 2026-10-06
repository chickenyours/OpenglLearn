#pragma once
#include "build_definitions.h"
#include "weapon_definitions.h"
#include <array>

namespace Brotato {
// New rules for the recovered five character appearances. These do not claim
// to reproduce the commercial game's professions or the source UI labels.
struct CharacterProfileDefinition {
    const char* strength;
    const char* attributes;
    AttributeBonus bonus;
    std::array<int,WeaponCount> weaponDamage{};
    int elementalBurning = 0;
};
inline constexpr std::array<CharacterProfileDefinition,5> CharacterProfiles{{
    {"BALANCED START / ANY WEAPON", "HP 150 / NO STAT PENALTIES", {}},
    {"MELEE DAMAGE +2 / ARMOR +2", "HP +30 / MOVE SPEED -10%", {.health=30,.armor=2,.moveSpeed=-.1f}, {0,2,0,2,0,0}},
    {"CRITICAL +15% / MOVE +15%", "HP -20 / ATTACK SPEED +10%", {.health=-20,.attackSpeed=.1f,.moveSpeed=.15f,.criticalChance=.15f}},
    {"RANGED DAMAGE +1 / PICKUP +25%", "HP -25 / MELEE HAS NO BONUS", {.health=-25,.pickupRange=.25f}, {1,0,1,0,1,1}},
    {"ELEMENTAL WEAPONS BURN +2", "HP -10 / ATTACK SPEED -10%", {.health=-10,.attackSpeed=-.1f}, {}, 2},
}};
struct RunDifficultyDefinition {
    const char* name;
    int enemyHealth = 100, enemyDamage = 100, enemySpeed = 100;
    int spawnDelay = 100, bossHealth = 100, shopHealing = 25;
};
inline constexpr std::array<RunDifficultyDefinition,3> RunDifficulties{{
    {"STANDARD",100,100,100,100,100,25},
    {"DANGER 1",120,115,105,90,120,20},
    {"DANGER 2",145,135,110,80,145,15},
}};
} // namespace Brotato
