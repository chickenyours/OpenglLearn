#pragma once

#include "game_components.h"
#include "game_events.h"
#include "engine/ECS/Command/command_buffer.h"
#include "engine/ECS/Query/query.h"
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <vector>

namespace Brotato {
inline constexpr double SimulationStep = 1.0 / 120.0;
inline constexpr std::size_t MaxPendingGameEvents = 256;

template<class... T>
using Query = ECS::Core::ChunkQuery<ECS::Core::Require<T...>, ECS::Core::Optional<>, ECS::Core::Exclude<>>;
template<class Required, class... Optional>
using OptionalQuery = ECS::Core::ChunkQuery<Required, ECS::Core::Optional<Optional...>, ECS::Core::Exclude<>>;

// Shared simulation data, injected into systems. Entity membership belongs to
// Scene/archetypes; this resource deliberately has no per-kind entity lists and
// no simulation update methods. Handles here represent explicit relationships.
struct GameWorld {
    explicit GameWorld(Config settings = {})
        : config(std::move(settings)), selected(config.initialWeapon), random(config.seed) {}
    Config config;
    Statistics stats;
    std::unique_ptr<ECS::Core::Scene> scene;
    ObjectWeakPtr<ECS::Core::ArchType> playerType, weaponType, enemyType, projectileType, pickupType, effectType;
    ObjectWeakPtr<ECS::Core::ArchType> hostileType;
    ObjectWeakPtr<ECS::Core::ArchType> itemType;
    ObjectWeakPtr<ECS::Core::ArchType> statusType;
    ObjectWeakPtr<ECS::Core::ArchType> bossType;
    ECS::EntityHandle player{0}, weapon{0};
    WeaponKind selected = WeaponKind::Wand;
    State state = State::Playing;
    Input input;
    std::mt19937 random;
    double spawnTimer = 0;
    std::uint64_t spawnSequence = 0;
    std::optional<ECS::Core::CommandBuffer> commands, effectCommands;
    std::size_t reservedEnemies = 0, reservedProjectiles = 0, reservedPickups = 0, reservedEffects = 0;
    std::size_t reservedHostileProjectiles = 0;
    std::size_t reservedStatuses = 0;
    std::vector<GameEvent> events;
    std::uint64_t presentationEpoch = 0, droppedEvents = 0;

    template<class T> T& Get(ECS::EntityHandle entity) {
        if (!scene || !scene->IsAlive(entity)) throw std::logic_error("Brotato: stale entity handle");
        auto* component = scene->TryGetComponent<T>(entity);
        if (!component) throw std::logic_error("Brotato: missing component");
        return *component;
    }
    const WeaponDefinition& Definition(WeaponKind kind) const { return config.weapons.at(WeaponIndex(kind)); }
};

template<class... T> std::size_t Count(GameWorld& world) {
    if (!world.scene) return 0;
    Query<T...> query;
    query.Refresh(*world.scene);
    return query.Count();
}

ECS::EntityHandle SpawnEnemy(GameWorld&, glm::vec2 position, bool ready = false, EnemyKind = EnemyKind::Normal, ChampionKind = ChampionKind::None);
ECS::EntityHandle SpawnPickup(GameWorld&, glm::vec2 position);
ECS::EntityHandle CreateProjectile(GameWorld&, glm::vec2 position, glm::vec2 velocity, WeaponKind, int path, float heading, int damage = -1);
void QueueEnemy(GameWorld&, glm::vec2 position, EnemyKind = EnemyKind::Normal);
struct EnemySpawn { glm::vec2 position{}; EnemyKind kind = EnemyKind::Normal; ChampionKind champion = ChampionKind::None; bool rewards = true; ECS::EntityHandle summonedBy{0}; };
bool QueueEnemyPack(GameWorld&, std::span<const EnemySpawn>);
bool UsesEncounters(const GameWorld&);
void BeginEncounterWave(GameWorld&);
int EnemyAttackDamage(const GameWorld&, const Enemy&, int base);
int WaveHealthPercent(int wave);
struct EncounterSnapshot { int wave = 1, phase = 0, packs = 0, events = 0; EncounterEvent event = EncounterEvent::None; double seconds = 0; };
EncounterSnapshot ExtractEncounter(GameWorld&);
void QueuePickup(GameWorld&, glm::vec2 position);
void QueueProjectile(GameWorld&, glm::vec2 position, glm::vec2 velocity, WeaponKind, int path, float heading, int damage = -1);
void QueueAttackProjectile(GameWorld&, glm::vec2 position, glm::vec2 velocity, WeaponKind, int path, float heading, Damage);
ECS::EntityHandle CreateHostileProjectile(GameWorld&, glm::vec2 position, glm::vec2 velocity, int damage, ECS::EntityHandle source = ECS::EntityHandle(0));
bool QueueHostileVolley(GameWorld&, ECS::EntityHandle source, glm::vec2 position, glm::vec2 direction, int count, int damage);
bool QueueHostilePattern(GameWorld&, ECS::EntityHandle source, glm::vec2 position, glm::vec2 direction, int count, int damage, bool ring);
ECS::EntityHandle SpawnBoss(GameWorld&, glm::vec2 position, bool ready = false, bool finalBoss = false, bool objective = false);
void QueueBoss(GameWorld&);
bool UsesCampaign(const GameWorld&);
const CharacterProfileDefinition& CharacterRules(const GameWorld&, ECS::EntityHandle owner);
const RunDifficultyDefinition& DifficultyRules(const GameWorld&);
int ScaleRuleValue(int value, int percent);
int EnemyDamage(const GameWorld&, int base);
float EnemySpeed(const GameWorld&);
double SpawnDelay(const GameWorld&, double base);
RunSetupSnapshot ExtractRunSetup(const GameWorld&);
bool IsBossWave(const GameWorld&);
void BeginCampaignWave(GameWorld&);
std::vector<DrawBossStatus> ExtractBossStatus(GameWorld&);
void RefreshCounts(GameWorld&);
void ClearTransient(GameWorld&);
bool EquipWeapon(GameWorld&, WeaponKind);
void ResetWeapon(GameWorld&, bool resetCooldowns);
bool AliveEnemy(GameWorld&, ECS::EntityHandle);
void Kill(GameWorld&, ECS::EntityHandle, WeaponKind, int damage = 0);
bool ApplyDamage(GameWorld&, ECS::EntityHandle, int amount, WeaponKind = WeaponKind::Wand);
int AttackDamage(GameWorld&, ECS::EntityHandle owner, WeaponKind);
Damage AttackPayload(GameWorld&, ECS::EntityHandle owner, WeaponKind);
bool ResolveHit(GameWorld&, ECS::EntityHandle target, const Damage&, WeaponKind, glm::vec2 direction);
bool SelectUpgrade(GameWorld&, UpgradeKind);
void PrepareGrowthChoices(GameWorld&, Growth&);
void RecomputeBuild(GameWorld&, ECS::EntityHandle);
void ClearOwnedItems(GameWorld&);
void OpenShop(GameWorld&);
ShopResult BuyShopItem(GameWorld&, std::size_t slot);
ShopResult BuyShopOffer(GameWorld&, std::size_t slot);
ShopResult MergeWeapon(GameWorld&, std::size_t slot);
ShopResult SellWeapon(GameWorld&, std::size_t slot);
EquipmentSnapshot ExtractEquipment(GameWorld&, ECS::EntityHandle owner);
bool SelectWeaponSlot(GameWorld&, std::size_t slot);
void ResetEquipment(GameWorld&);
bool UsesArsenal(const GameWorld&);
int WeaponPrice(WeaponKind, int tier);
int RankedDamage(GameWorld&, const Weapon&);
float RankedCooldown(GameWorld&, const Weapon&);
glm::vec2 WeaponMount(const Weapon&);
void InitializeEquipment(GameWorld&, ECS::EntityHandle, ECS::EntityHandle owner, WeaponKind, int tier, int slot, WeaponAffix = WeaponAffix::None);
bool UsesTraits(const GameWorld&);
Damage WeaponPayload(GameWorld&, Weapon&, bool rollCritical = true);
void QueueStatus(GameWorld&, ECS::EntityHandle target, const Damage&, WeaponKind);
ShopResult RerollShop(GameWorld&);
ShopResult ToggleShopLock(GameWorld&, std::size_t slot);
int ShopRerollCost(const Shop&);
BuildSnapshot ExtractBuild(GameWorld&, ECS::EntityHandle);
ShopSnapshot ExtractShop(GameWorld&);
void ResetActorAnimation(GameWorld&, ECS::EntityHandle, ActorClip);
void SampleActorAnimation(GameWorld&, ECS::EntityHandle);
void QueueHitEffects(GameWorld&, ECS::EntityHandle, int damage = 0);
void QueueBlastEffect(GameWorld&, glm::vec2 position, float radius);
void EmitEvent(GameWorld&, GameEventKind, ECS::EntityHandle, glm::vec2 position, WeaponKind = WeaponKind::Wand);
void ResetPresentation(GameWorld&);
std::vector<DrawSprite> ExtractSprites(GameWorld&);
std::vector<DrawDamageText> ExtractDamageText(GameWorld&);
std::vector<DrawEnemyStatus> ExtractEnemyStatus(GameWorld&);
std::vector<DrawCombatCue> ExtractCombatCues(GameWorld&);
} // namespace Brotato
