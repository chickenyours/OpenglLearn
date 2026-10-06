#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace Brotato {
enum class UpgradeKind { Damage, AttackSpeed, MoveSpeed, MaxHealth, Armor, PickupRange, Count };
inline constexpr std::size_t UpgradeCount = std::size_t(UpgradeKind::Count);
enum class ItemKind { Lens, Coffee, Beanie, Vest, Plant, Cake, Bandana, Bat, Sunglasses, Sausage, Count };
inline constexpr std::size_t LegacyItemCount = 6;
inline constexpr std::size_t ItemCount = std::size_t(ItemKind::Count), ShopSlots = 4;
inline constexpr int MaxItemStack = 99, MaxMaterials = 1000000;
struct AttributeBonus {
    int damage = 0, health = 0, armor = 0;
    float attackSpeed = 0, moveSpeed = 0, pickupRange = 0, regeneration = 0;
    float criticalChance = 0, lifeSteal = 0;
    int pierce = 0, burning = 0;
};
struct UpgradeDefinition { const char* name; const char* value; const char* description; AttributeBonus bonus; };
inline constexpr std::array<UpgradeDefinition,UpgradeCount> UpgradeDefinitions{{
    {"DAMAGE","+1","ALL WEAPONS +1 DAMAGE",{1}},
    {"ATTACK SPEED","+20%","ATTACK RATE +20%",{0,0,0,.2f}},
    {"MOVE SPEED","+15%","MOVEMENT SPEED +15%",{0,0,0,0,.15f}},
    {"MAX HEALTH","+10","MAX HP +10 / HEAL +10",{0,10}},
    {"ARMOR","+1","INCOMING DAMAGE -1",{0,0,1}},
    {"PICKUP RANGE","+15%","COLLECTION RADIUS +15%",{0,0,0,0,0,.15f}},
}};
struct ItemDefinition { const char* name; const char* image; const char* description; int price; AttributeBonus bonus; };
// Newly designed effects, using recovered source art. No commercial rules implied.
inline constexpr std::array<ItemDefinition,ItemCount> ItemDefinitions{{
    {"LENS","item_lens","ALL WEAPONS +1 DAMAGE",6,{1}},
    {"COFFEE","item_coffee","ATTACK RATE +15%",7,{0,0,0,.15f}},
    {"BEANIE","item_beanie","MOVE SPEED +10%",6,{0,0,0,0,.1f}},
    {"LEATHER VEST","item_vest","INCOMING DAMAGE -1",8,{0,0,1}},
    {"PLANT","item_plant","REGEN .5 HP/S / PICKUP +25%",8,{0,0,0,0,0,.25f,.5f}},
    {"CAKE","item_cake","MAX HP +10 / HEAL +10",7,{0,10}},
    {"BANDANA","item_bandana","PROJECTILES PIERCE +1",12,{0,0,0,0,0,0,0,0,0,1}},
    {"BAT","item_bat","DIRECT HIT LIFE STEAL +5%",10,{0,0,0,0,0,0,0,0,.05f}},
    {"SUNGLASSES","item_sunglasses","CRITICAL CHANCE +10%",10,{0,0,0,0,0,0,0,.1f}},
    {"SCARED SAUSAGE","item_sausage","BURN DAMAGE +1 / IGNITES HITS",12,{0,0,0,0,0,0,0,0,0,0,1}},
}};
// Independent component-owned random streams: reward/shop draws never consume
// the spawn/drop engine. A zero seed is normalized before the first draw.
inline std::uint32_t BuildRandom(std::uint32_t& state) {
    if (!state) state=0x6d2b79f5u;
    state^=state<<13; state^=state>>17; state^=state<<5; return state;
}
} // namespace Brotato
