#pragma once

#include "engine/ECS/Component/component_loader_registry.h"
#include "engine/ECS/Entity/entity.h"
#include "weapon_definitions.h"
#include "trait_definitions.h"
#include "build_definitions.h"
#include "run_definitions.h"
#include "content_catalog.h"
#include "Render/Public/Sprite/sprite_animation.h"
#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>
#include <algorithm>

namespace Brotato {
static_assert(CharacterProfiles.size()==Characters.size());

template<class T> struct Component : ECS::Component::Component<T> {
    bool LoadFromMetaDataImpl(const Json::Value&, Log::StackLogErrorHandle) { return false; }
};

struct Transform : Component<Transform> { glm::vec2 position{}, previous{}; };
struct Velocity : Component<Velocity> { glm::vec2 value{}; };
struct Circle : Component<Circle> { float radius = .3f; };
struct Sprite : Component<Sprite> {
    Image image = Image::Player;
    glm::vec2 size{1};
    bool flipX = false;
    float angle = 0;
};
struct Player : Component<Player> {
    int level = 0, experience = 0, materials = 0;
    // Integer XP: 2 / 100 replaces source's 0.02 float increments.
};
struct CharacterProfile : Component<CharacterProfile> { std::size_t kind = 0; };
struct RunRules : Component<RunRules> { std::size_t difficulty = 0; };
struct Health : Component<Health> {
    int current = 1, maximum = 1;
    float flash = 0, invulnerable = 0;
};
struct Damage : Component<Damage> {
    int amount = 1; float knockback = 0, splashRadius = 0;
    ECS::EntityHandle owner{0};
    int burning = 0, pierce = 0; float slow = 1, lifeSteal = 0;
    bool critical = false;
};
struct Knockback : Component<Knockback> { glm::vec2 velocity{}; float remaining = 0; };
struct CombatStats : Component<CombatStats> {
    int bonusDamage = 0;
    float attackSpeed = 1, moveSpeed = 1;
    int armor = 0;
    float pickupRange = 1, regeneration = 0;
    float criticalChance = 0, lifeSteal = 0;
    int pierce = 0, burning = 0;
    std::array<int,FamilyCount> families{};
    std::array<int,WeaponCount> weaponDamage{};
    int elementalBurning = 0;
};
struct BuildBase : Component<BuildBase> { int maximumHealth = 150; float regenerationCredit = 0, lifeStealCredit = 0; };
struct Growth : Component<Growth> {
    int pending = 0;
    std::array<int, UpgradeCount> upgrades{};
    std::array<UpgradeKind,3> choices{UpgradeKind::Damage,UpgradeKind::AttackSpeed,UpgradeKind::MoveSpeed};
    std::uint32_t randomState = 1;
};
struct OwnedItem : Component<OwnedItem> { ECS::EntityHandle owner{0}; ItemKind kind = ItemKind::Lens; int count = 1; };
enum class OfferType { Item, Weapon };
struct ShopOffer {
    ItemKind kind = ItemKind::Count; int price = 0; bool sold = true, locked = false;
    OfferType type = OfferType::Item;
    WeaponKind weapon = WeaponKind::Wand; int tier = 0;
    WeaponAffix affix = WeaponAffix::None;
};
enum class ShopResult { Ready, Bought, Rerolled, Locked, Unlocked, NotEnough, Sold, StackFull, Capacity, AllLocked, Invalid, Merged, WeaponSold, NoMatch, LastWeapon, MaxTier };
struct Shop : Component<Shop> {
    std::array<ShopOffer,ShopSlots> offers{};
    std::uint32_t randomState = 1;
    int visits = 0, rerolls = 0, waveBonus = 0, restoredHealth = 0;
    ShopResult feedback = ShopResult::Ready;
};
enum class EnemyKind { Normal, Fast, Armored, Ranged, Charger, Elite, Healer, Summoner, Count };
inline constexpr std::size_t EnemyCount = static_cast<std::size_t>(EnemyKind::Count);

enum class ChampionKind { None, Swift, Bulwark };
enum class EncounterEvent { None, Swarm, Crossfire, Stampede, Reinforcements };
// Schedule and random stream belong to an ECS entity, independently of loot.
struct SpawnDirector : Component<SpawnDirector> {
    int wave = 1, packs = 0, events = 0, blockedPacks = 0;
    double elapsed = 0, packTimer = 1, eventTimer = 7, eventRemaining = 0;
    EncounterEvent event = EncounterEvent::None;
    std::uint32_t randomState = 1;
};
enum class EnemyAction { Approach, Windup, Charging, Recovering };
struct EnemyBrain : Component<EnemyBrain> {
    EnemyAction action = EnemyAction::Approach;
    float cooldown = .8f, remaining = 0;
    glm::vec2 direction{1, 0}; // Locked when windup begins; never tracks during a dash.
};
enum class BossAction { Approach, Windup, Charging, Recovering };
enum class BossAttack { Fan, Ring, Charge };
struct BossBrain : Component<BossBrain> {
    BossAction action = BossAction::Approach; BossAttack attack = BossAttack::Fan;
    glm::vec2 direction{1,0}; float remaining = 0, cooldown = 1;
    int sequence = 0, wave = 0; bool enraged = false, objective = false, finalBoss = false;
};
enum class RunOutcome { None, Victory, Defeat };
enum class RunEndReason { None, PlayerDied, BossEscaped };
struct RunResult {
    RunOutcome outcome = RunOutcome::None; RunEndReason reason = RunEndReason::None;
    int wave = 0, kills = 0, bossKills = 0, level = 0, materials = 0, health = 0, maximumHealth = 0;
    std::size_t weapons = 0, items = 0; double seconds = 0;
    std::size_t character = 0, difficulty = 0;
};
struct RunProgress : Component<RunProgress> {
    int objectiveWave = 0; bool bossSpawned = false, bossDefeated = false;
    std::uint64_t elapsedTicks = 0; RunResult result;
};
struct EnemyDefinition {
    const char* name;
    int health, experience;
    float speed, contactDamage, radius;
};
inline constexpr std::array<EnemyDefinition, EnemyCount> EnemyDefinitions{{
    {"NORMAL", 2, 2, 1, 1, .43f},
    {"RUNNER", 1, 3, 1.65f, .7f, .31f},
    {"ARMORED", 6, 6, .65f, 2, .59f},
    {"SHOOTER", 3, 4, .85f, .7f, .40f},
    {"CHARGER", 4, 5, .9f, 1.5f, .43f},
    {"ELITE", 12, 10, .8f, 2.5f, .70f},
    {"HEALER", 5, 5, .75f, .6f, .42f},
    {"SUMMONER", 8, 7, .5f, .8f, .50f},
}};
inline int ExperienceThreshold(int level, bool expanded) {
    return expanded ? 8 + std::min(std::max(level, 0), 1000) * 4 : 100;
}
enum class WeaponPhase { Idle, Outbound, Returning, Beam };
struct Weapon : Component<Weapon> {
    ECS::EntityHandle owner{0}, target{0};
    WeaponKind kind = WeaponKind::Wand;
    int tier = 0, equipmentSlot = -1; // Inventory membership and rank live on this entity.
    WeaponAffix affix = WeaponAffix::None; std::uint32_t randomState = 1;
    WeaponPhase phase = WeaponPhase::Idle;
    float cooldown = 0, angle = 0, elapsed = 0, flash = 0;
    // The source's initial/returned world pose can reflect its local XY plane.
    // Aiming assigns a world Z rotation and removes that reflection.
    bool reflected = false;
    int activeSegments = 0;
    // Cooldowns continue across selection, so switching cannot bypass recovery.
    std::array<float, WeaponCount> cooldowns{};
    // Per-attack relationships, never an entity membership list for the world.
    std::vector<ECS::EntityHandle> hitTargets;
};
enum class EnemyPhase { Spawning, Alive, Dying };
struct Enemy : Component<Enemy> {
    EnemyPhase phase = EnemyPhase::Spawning;
    float remaining = 1.5f;
    bool touchingPlayer = false;
    EnemyKind kind = EnemyKind::Normal;
    int experience = 2;
    float speed = 1;
    int birthWave = 1, damagePercent = 100;
    float growthSpeed = 1;
    ChampionKind champion = ChampionKind::None;
    bool rewards = true; // Summoned minions cannot produce infinite XP/materials.
    ECS::EntityHandle summonedBy{0};
    float slowMultiplier = 1; bool burning = false; // Rebuilt by StatusSystem each tick.
};
struct Projectile : Component<Projectile> {
    float remaining = 4, age = 0, gravity = -9.81f, halfLength = 0, heading = 0;
    bool consumed = false;
    WeaponKind kind = WeaponKind::Wand;
    int path = -1;
    glm::vec2 origin{};
    int pierces = 0;
    std::vector<ECS::EntityHandle> hitTargets;
};
enum class StatusKind { Burn, Slow };
struct TimedStatus : Component<TimedStatus> {
    ECS::EntityHandle target{0}, owner{0}; StatusKind kind = StatusKind::Burn;
    WeaponKind weapon = WeaponKind::Wand;
    float remaining = 2, pulse = 0, slow = 1; int damage = 0;
};
struct HostileProjectile : Component<HostileProjectile> {
    float remaining = 3;
    ECS::EntityHandle source{0}; // Attribution survives the shooter's retirement.
};
struct Pickup : Component<Pickup> { int value = 1; bool collected = false; };
enum class ActorClip { PlayerIdle, PlayerMove, EnemyMove, EnemyDeath };
struct ActorAnimation : Component<ActorAnimation> {
    static constexpr std::size_t MaxNodes = 16;
    ActorClip clip = ActorClip::PlayerIdle;
    double time = 0;
    std::array<Render::Animation::Pose, MaxNodes> nodes{};
};
enum class EffectKind { HitParticle, DamageText, BlastRing };
struct Effect : Component<Effect> {
    EffectKind kind = EffectKind::HitParticle;
    double age = 0;
    float lifetime = .5f, initialSize = 1;
    float radius = 0;
    int value = 1;
    bool damage = false;
    ECS::EntityHandle owner{0}; // Damage text belongs to the original corpse generation.
    Render::Animation::Pose pose{};
};

inline void RegisterComponents() {
    static std::once_flag registered;
    std::call_once(registered, [] {
        REGISTER_COMPONENT("brotato_transform", Transform);
        REGISTER_COMPONENT("brotato_velocity", Velocity);
        REGISTER_COMPONENT("brotato_circle", Circle);
        REGISTER_COMPONENT("brotato_sprite", Sprite);
        REGISTER_COMPONENT("brotato_player", Player);
        REGISTER_COMPONENT("brotato_character_profile", CharacterProfile);
        REGISTER_COMPONENT("brotato_run_rules", RunRules);
        REGISTER_COMPONENT("brotato_health", Health);
        REGISTER_COMPONENT("brotato_damage", Damage);
        REGISTER_COMPONENT("brotato_knockback", Knockback);
        REGISTER_COMPONENT("brotato_enemy_brain", EnemyBrain);
        REGISTER_COMPONENT("brotato_boss_brain", BossBrain);
        REGISTER_COMPONENT("brotato_run_progress", RunProgress);
        REGISTER_COMPONENT("brotato_combat_stats", CombatStats);
        REGISTER_COMPONENT("brotato_growth", Growth);
        REGISTER_COMPONENT("brotato_build_base", BuildBase);
        REGISTER_COMPONENT("brotato_owned_item", OwnedItem);
        REGISTER_COMPONENT("brotato_shop", Shop);
        REGISTER_COMPONENT("brotato_weapon", Weapon);
        REGISTER_COMPONENT("brotato_enemy", Enemy);
        REGISTER_COMPONENT("brotato_spawn_director", SpawnDirector);
        REGISTER_COMPONENT("brotato_projectile", Projectile);
        REGISTER_COMPONENT("brotato_timed_status", TimedStatus);
        REGISTER_COMPONENT("brotato_hostile_projectile", HostileProjectile);
        REGISTER_COMPONENT("brotato_pickup", Pickup);
        REGISTER_COMPONENT("brotato_actor_animation", ActorAnimation);
        REGISTER_COMPONENT("brotato_effect", Effect);
    });
}

struct Input {
    float horizontal = 0, vertical = 0;
    bool pause = false, restart = false, nextWave = false, revive = false;
    int selectWeapon = 0; // 0: unchanged, 1..6: source w1..w6.
    int chooseUpgrade = 0; // 0: unchanged, 1..3: the currently displayed reward slot.
    int buySlot = 0, lockSlot = 0; // 1..4: shop cards, 0: unchanged.
    bool rerollShop = false;
    int mergeSlot = 0, sellSlot = 0; // 1..6: equipped slots, only in the shop.
};
enum class State { Playing, Paused, WaveComplete, Dead, LevelUp, Shop, Victory, Defeat };
struct Config {
    // The source keeps appearance-only choices; profiles enables the new rules.
    // GameModule requires a concrete map; SessionModule resolves RandomMap.
    std::size_t character = 0, map = 0;
    std::size_t difficulty = 0;
    glm::vec2 minimum{2.84f, -10.6f}, maximum{18.9f, 10.51f}, playerStart{10.80735f, -.1775364f};
    float playerSpeed = 5, enemySpeed = 2.3f;
    int initialHealth = 150, contactDamage = 10;
    std::array<WeaponDefinition, WeaponCount> weapons = DefaultWeapons();
    WeaponKind initialWeapon = WeaponKind::Wand;
    float dropChance = .5f;
    float spawnWarning = 1.5f, deathDelay = 1.5f, waveSeconds = 20, waveIncrement = 7;
    std::uint32_t seed = 2026;
    std::size_t maxEnemies = 256, maxProjectiles = 256, maxPickups = 512, maxEffects = 512;
    std::size_t maxHostileProjectiles = 128;
    std::size_t maxOwnedItems = 128;
    std::size_t maxWeapons = 128;
    std::size_t maxStatuses = 256;
    bool spawning = true, armed = true;
    bool expanded = false; // Source rules stay available for migration regressions.
    bool builds = false; // Reward pool/items/shop; legacy growth remains testable.
    bool arsenal = false; // Active with builds; source and historical fixtures remain separate.
    bool traits = false; // Family/affix/status rules; historical fixtures remain reproducible.
    bool campaign = false;
    bool profiles = false;
    bool encounters = false;
    int campaignWaves = 6, bossHealth = 70, finalBossHealth = 180;
};
inline bool ProfileRulesEnabled(const Config& config) { return config.expanded && config.builds && config.profiles; }
struct RunSetupSnapshot { std::size_t character = 0, difficulty = 0; bool profiles = false; };
struct Statistics {
    std::uint64_t ticks = 0;
    int wave = 1, kills = 0, shots = 0;
    int hits = 0;
    std::array<int, EnemyCount> spawned{};
    int enemyVolleys = 0, charges = 0, splashHits = 0;
    std::size_t enemies = 0, projectiles = 0, pickups = 0, weapons = 0, effects = 0;
    std::size_t hostileProjectiles = 0;
    std::size_t ownedItems = 0;
    std::size_t statuses = 0; int criticalAttacks = 0, burnHits = 0, piercedHits = 0, stolenHealth = 0;
    double remaining = 20;
    int bossSpawns = 0, bossKills = 0, bossAttacks = 0;
    int championSpawns = 0, healedEnemies = 0, summonedEnemies = 0;
};
struct DrawSprite {
    Image image;
    glm::vec2 position, size;
    float angle = 0;
    glm::vec4 tint{1};
    bool flipX = false;
    bool flipY = false;
    glm::vec2 axisX{}, axisY{};
    bool affine = false;
};
struct DrawDamageText {
    int value = 1;
    glm::vec2 position{};
    float size = .24f;
    glm::vec4 tint{1};
    bool damage = false;
};
struct DrawEnemyStatus {
    EnemyKind kind;
    glm::vec2 position;
    int health, maximum;
    bool burning = false; float slow = 1;
    ChampionKind champion = ChampionKind::None;
};
struct DrawBossStatus { int health = 0, maximum = 0; bool enraged = false, finalBoss = false; };
enum class CombatCueKind { Aim, Charge, Blast, BossFan, BossRing, Heal, Summon };
struct DrawCombatCue {
    CombatCueKind kind;
    glm::vec2 position{}, end{};
    float radius = 0, progress = 0;
};
struct BuildSnapshot {
    std::array<int,ItemCount> items{};
    CombatStats stats;
    int health = 0, maximumHealth = 0, materials = 0;
};
inline constexpr std::size_t EquipmentSlots = 6;
struct EquippedWeaponSnapshot {
    ECS::EntityHandle entity{0}; WeaponKind kind = WeaponKind::Wand;
    int tier = 0, damage = 0, sellPrice = 0; float cooldown = 0;
    WeaponAffix affix = WeaponAffix::None;
    bool present = false, canMerge = false;
};
struct EquipmentSnapshot { std::array<EquippedWeaponSnapshot,EquipmentSlots> slots{}; std::size_t count = 0; };
struct ShopSnapshot { Shop shop; BuildSnapshot build; int rerollCost = 0; EquipmentSnapshot equipment; };

} // namespace Brotato
