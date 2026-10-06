#include "Brotato/Public/game_module.h"
#include "Brotato/Systems/game_systems.h"
#include <algorithm>
#include <cmath>

namespace Brotato {
namespace {
bool Finite(glm::vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }
template<class... T> auto Archetype(ECS::Core::Scene& scene) {
    auto description = scene.CreateArchTypeDescription();
    (description->AddComponentArray<T>(), ...);
    return scene.CreateArchType(description, 128);
}
}
GameModule::GameModule(Config config) : world_(std::move(config)) {
    pipeline_.Add<WaveSystem>(); pipeline_.Add<EncounterSystem>(); pipeline_.Add<BuildSystem>(); pipeline_.Add<HealthSystem>(); pipeline_.Add<StatusSystem>(); pipeline_.Add<EnemyAISystem>(); pipeline_.Add<MovementSystem>(); pipeline_.Add<WeaponSystem>();
    pipeline_.Add<BossAISystem>(); pipeline_.Add<ProjectileSystem>(); pipeline_.Add<ContactSystem>(); pipeline_.Add<PickupSystem>();
    pipeline_.Add<PresentationSystem>(); pipeline_.Add<GrowthSystem>(); pipeline_.Add<RunSystem>();
    pipeline_.RunBefore<WaveSystem, EncounterSystem>();
    pipeline_.RunBefore<EncounterSystem, BuildSystem>();
    pipeline_.RunBefore<BuildSystem, HealthSystem>();
    pipeline_.RunBefore<HealthSystem, StatusSystem>();
    pipeline_.RunBefore<StatusSystem, EnemyAISystem>();
    pipeline_.RunBefore<EnemyAISystem, BossAISystem>();
    pipeline_.RunBefore<BossAISystem, MovementSystem>();
    pipeline_.RunBefore<MovementSystem, WeaponSystem>();
    pipeline_.RunBefore<WeaponSystem, ProjectileSystem>();
    pipeline_.RunBefore<ProjectileSystem, ContactSystem>();
    pipeline_.RunBefore<ContactSystem, PickupSystem>();
    pipeline_.RunBefore<PickupSystem, PresentationSystem>();
    pipeline_.RunBefore<PresentationSystem, GrowthSystem>();
    pipeline_.RunBefore<GrowthSystem, RunSystem>();
}
void GameModule::Validate() const {
    if (world_.config.character >= Characters.size() || world_.config.map >= Maps.size() ||
        world_.config.difficulty >= (ProfileRulesEnabled(world_.config)?RunDifficulties.size():1))
        throw std::invalid_argument("Brotato: invalid character/map selection");
    if (!Finite(world_.config.minimum) || !Finite(world_.config.maximum) || !Finite(world_.config.playerStart) ||
        world_.config.minimum.x >= world_.config.maximum.x || world_.config.minimum.y >= world_.config.maximum.y)
        throw std::invalid_argument("Brotato: invalid arena bounds");
    for (float value : {world_.config.playerSpeed, world_.config.enemySpeed, world_.config.spawnWarning,
                       world_.config.deathDelay, world_.config.waveIncrement})
        if (!std::isfinite(value) || value < 0) throw std::invalid_argument("Brotato: invalid nonnegative setting");
    for (float value : {world_.config.waveSeconds})
        if (!std::isfinite(value) || value <= 0) throw std::invalid_argument("Brotato: invalid positive setting");
    if (!std::isfinite(world_.config.dropChance) || world_.config.dropChance < 0 || world_.config.dropChance > 1 ||
        world_.config.initialHealth <= 0 || world_.config.contactDamage < 0 ||
        world_.config.maxEnemies > 100000 || world_.config.maxProjectiles > 100000 || world_.config.maxPickups > 100000 || world_.config.maxEffects > 100000 || world_.config.maxHostileProjectiles > 100000 || world_.config.maxOwnedItems > 100000 || world_.config.maxWeapons > 100000 ||
        world_.config.maxStatuses > 100000 || world_.config.campaignWaves<1 || world_.config.campaignWaves>20 ||
        world_.config.bossHealth<1 || world_.config.bossHealth>100000 || world_.config.finalBossHealth<1 || world_.config.finalBossHealth>100000 ||
        (UsesCampaign(world_) && world_.config.maxEnemies<1) || (UsesArsenal(world_) && world_.config.maxWeapons<1) ||
        (world_.config.builds && (!world_.config.expanded || world_.config.initialHealth>100000)))
        throw std::invalid_argument("Brotato: invalid combat/capacity setting");
    if (!ValidWeapon(world_.config.initialWeapon)) throw std::invalid_argument("Brotato: invalid initial weapon");
    for (const auto& weapon : world_.config.weapons) {
        if (weapon.name.empty() || weapon.damage < 1 || weapon.damage > 100000 || !std::isfinite(weapon.knockback) || weapon.knockback < 0 || weapon.knockback > 100 ||
            !std::isfinite(weapon.splashRadius) || weapon.splashRadius < 0 || weapon.splashRadius > 20 || !Finite(weapon.size) || !Finite(weapon.attackSize) ||
            weapon.size.x <= 0 || weapon.size.y <= 0 || weapon.attackSize.x <= 0 || weapon.attackSize.y <= 0 ||
            !std::isfinite(weapon.cooldown) || weapon.cooldown <= 0 || !std::isfinite(weapon.range) || weapon.range < 0 ||
            !std::isfinite(weapon.speed) || weapon.speed < 0 ||
            (weapon.mode != AttackMode::AuthoredBurst && weapon.speed <= 0) ||
            !std::isfinite(weapon.gravity) || !std::isfinite(weapon.lifetime) || weapon.lifetime <= 0 ||
            !std::isfinite(weapon.radius) || weapon.radius <= 0 || !std::isfinite(weapon.halfLength) || weapon.halfLength < 0 ||
            !std::isfinite(weapon.hitOffset) || !std::isfinite(weapon.segmentSeconds) || weapon.segmentSeconds < FixedStep ||
            !std::isfinite(weapon.animationSeconds) || weapon.animationSeconds <= 0 ||
            static_cast<unsigned>(weapon.mode) > static_cast<unsigned>(AttackMode::AuthoredBurst))
            throw std::invalid_argument("Brotato: invalid weapon definition");
    }
}

bool GameModule::Startup() {
    if (started_) return true;
    try {
        Validate(); RegisterComponents();
        auto& world = world_;
        world.events.reserve(MaxPendingEvents);
        world.scene = std::make_unique<ECS::Core::Scene>();
        world.commands.emplace(*world.scene); world.effectCommands.emplace(*world.scene);
        world.playerType = Archetype<Transform, Velocity, Circle, Sprite, Player, CharacterProfile, RunRules, SpawnDirector, ActorAnimation, Health, CombatStats, Growth, BuildBase, Shop, RunProgress>(*world.scene);
        world.weaponType = Archetype<Transform, Sprite, Weapon, Damage>(*world.scene);
        world.enemyType = Archetype<Transform, Velocity, Circle, Sprite, Enemy, ActorAnimation, Health, Damage, EnemyBrain, Knockback>(*world.scene);
        world.bossType = Archetype<Transform, Velocity, Circle, Sprite, Enemy, ActorAnimation, Health, Damage, EnemyBrain, Knockback, BossBrain>(*world.scene);
        world.projectileType = Archetype<Transform, Velocity, Circle, Sprite, Projectile, Damage>(*world.scene);
        world.pickupType = Archetype<Transform, Circle, Sprite, Pickup>(*world.scene);
        world.effectType = Archetype<Transform, Velocity, Sprite, Effect>(*world.scene);
        world.hostileType = Archetype<HostileProjectile, Transform, Velocity, Circle, Sprite, Damage>(*world.scene);
        world.itemType = Archetype<OwnedItem>(*world.scene);
        world.statusType = Archetype<TimedStatus>(*world.scene);
        world.player = world.scene->CreateEntity(world.playerType);
        world.weapon = world.scene->CreateEntity(world.weaponType);
        context_.scene = world.scene.get(); context_.SetService(&world); context_.deltaSeconds = FixedStep;
        if (!pipeline_.Start(context_)) throw std::runtime_error("Brotato system pipeline failed to start");
        started_ = true; Restart(); error_.clear(); return true;
    } catch (const std::exception& error) { error_ = error.what(); Shutdown(); return false; }
}
void GameModule::Shutdown() {
    pipeline_.Stop(context_);
    auto& world = world_;
    ResetPresentation(world);
    world.commands.reset(); world.effectCommands.reset();
    world.playerType = nullptr; world.weaponType = nullptr; world.enemyType = nullptr;
    world.projectileType = nullptr; world.pickupType = nullptr; world.effectType = nullptr;
    world.hostileType = nullptr;
    world.itemType = nullptr;
    world.statusType = nullptr; world.reservedStatuses = 0;
    world.bossType = nullptr;
    world.scene.reset(); world.player = ECS::EntityHandle(0); world.weapon = ECS::EntityHandle(0);
    world.reservedEnemies = world.reservedProjectiles = world.reservedPickups = world.reservedEffects = 0;
    world.reservedHostileProjectiles = 0;
    context_.scene = nullptr; context_.SetService<GameWorld>(nullptr); context_.frameIndex = 0;
    started_ = false; accumulator_ = 0; RefreshCounts(world);
}
void GameModule::Restart() {
    if (!started_) return;
    auto& world = world_;
    ClearTransient(world); ClearOwnedItems(world); world.random.seed(world.config.seed);
    world.stats = {}; world.stats.remaining = world.config.waveSeconds;
    world.state = State::Playing; accumulator_ = 0; world.spawnSequence = 0; context_.frameIndex = 0;
    Get<Player>(world.player) = Player{};
    Get<CharacterProfile>(world.player).kind=world.config.character;
    Get<RunRules>(world.player).difficulty=world.config.difficulty;
    world.spawnTimer=SpawnDelay(world,double(world.random()%2));
    auto& health = Get<Health>(world.player); health = Health{}; health.current = health.maximum = world.config.initialHealth;
    Get<CombatStats>(world.player) = CombatStats{}; Get<Growth>(world.player) = Growth{};
    Get<Growth>(world.player).randomState=world.config.seed^0x7f4a7c15u;
    Get<BuildBase>(world.player)=BuildBase{}; Get<BuildBase>(world.player).maximumHealth=world.config.initialHealth;
    Get<Shop>(world.player)=Shop{}; Get<Shop>(world.player).randomState=world.config.seed^0x9e3779b9u;
    Get<RunProgress>(world.player)=RunProgress{}; BeginCampaignWave(world);
    BeginEncounterWave(world);
    auto& transform = Get<Transform>(world.player);
    transform.position = glm::clamp(world.config.playerStart, world.config.minimum, world.config.maximum);
    transform.previous = transform.position;
    Get<Velocity>(world.player).value = {}; Get<Circle>(world.player).radius = .46f;
    auto& sprite = Get<Sprite>(world.player); sprite = Sprite{}; sprite.size = Characters[world.config.character].size;
    ResetActorAnimation(world, world.player, ActorClip::PlayerIdle);
    ResetEquipment(world); RecomputeBuild(world,world.player); ResetWeapon(world, true); RefreshCounts(world);
}
void GameModule::NextWave() {
    auto& world = world_;
    if (!started_ || (world.state != State::WaveComplete && world.state != State::Shop)) return;
    ClearTransient(world); ++world.stats.wave;
    world.stats.remaining = double(world.config.waveSeconds) + double(world.config.waveIncrement) * (world.stats.wave - 1);
    world.state = State::Playing; accumulator_ = 0; world.spawnTimer = SpawnDelay(world,double(world.random() % 2));
    BeginCampaignWave(world);
    BeginEncounterWave(world);
}
void GameModule::Revive() {
    if (started_ && world_.state == State::Dead) {
        auto& health = Get<Health>(world_.player); health.current = health.maximum; health.invulnerable = health.flash = 0;
        world_.state = State::Playing; accumulator_ = 0;
    }
}
void GameModule::SetPaused(bool paused) {
    if (paused && world_.state == State::Playing) world_.state = State::Paused;
    else if (!paused && world_.state == State::Paused) world_.state = State::Playing;
    accumulator_ = 0;
}
void GameModule::HandleInput(Input input) {
    if(world_.state==State::Victory || world_.state==State::Defeat){if(input.restart)Restart();return;}
    if (world_.state == State::LevelUp && input.chooseUpgrade >= 1 && input.chooseUpgrade <= 3) {
        SelectUpgrade(Get<Growth>(world_.player).choices[std::size_t(input.chooseUpgrade-1)]); return;
    }
    if(world_.state==State::Shop) {
        if(input.restart) Restart();
        else if(input.nextWave) NextWave();
        else if(input.buySlot>=1 && input.buySlot<=int(ShopSlots)) BuyShopOffer(std::size_t(input.buySlot-1));
        else if(input.lockSlot>=1 && input.lockSlot<=int(ShopSlots)) ToggleShopLock(std::size_t(input.lockSlot-1));
        else if(input.rerollShop) RerollShop();
        else if(input.mergeSlot>=1 && input.mergeSlot<=int(EquipmentSlots)) MergeWeapon(std::size_t(input.mergeSlot-1));
        else if(input.sellSlot>=1 && input.sellSlot<=int(EquipmentSlots)) SellWeapon(std::size_t(input.sellSlot-1));
        return;
    }
    if (input.selectWeapon >= 1 && input.selectWeapon <= int(WeaponCount)) {
        if(UsesArsenal(world_))SelectWeaponSlot(std::size_t(input.selectWeapon-1));
        else EquipWeapon(WeaponKind(input.selectWeapon - 1));
    }
    if (input.restart) Restart();
    else if (input.nextWave) NextWave();
    else if (input.revive) Revive();
    else if (input.pause) SetPaused(world_.state != State::Paused);
}
void GameModule::FixedTick(Input input) {
    if (!started_) return;
    const bool choosing = world_.state == State::LevelUp && input.chooseUpgrade != 0;
    const bool shopping = world_.state == State::Shop;
    HandleInput(input);
    if (!choosing && !shopping) Step(input);
}
void GameModule::Advance(double seconds, Input input) {
    if (!started_) return;
    const bool choosing = world_.state == State::LevelUp && input.chooseUpgrade != 0;
    const bool shopping = world_.state == State::Shop;
    HandleInput(input);
    if (choosing || shopping) return;
    if (world_.state != State::Playing || !std::isfinite(seconds) || seconds <= 0) return;
    accumulator_ += std::min(seconds, .25);
    int steps = 0;
    while (accumulator_ + 1e-12 >= FixedStep && steps++ < 8 && world_.state == State::Playing) {
        accumulator_ -= FixedStep; Step(input);
    }
    if (accumulator_ >= FixedStep) accumulator_ = std::fmod(accumulator_, FixedStep);
}
void GameModule::Step(Input input) {
    auto& world = world_;
    if (world.state != State::Playing) return;
    world.input = input; ++world.stats.ticks; context_.frameIndex = world.stats.ticks;
    if (!pipeline_.Tick(context_)) throw std::runtime_error("Brotato pipeline tick failed");
    // No query pointers survive this boundary. Normal spawns retain their
    // ordering before cosmetic spawns; retirement always precedes creation.
    if(world.state==State::Victory || world.state==State::Defeat)ClearTransient(world);
    else if (world.state == State::WaveComplete) { ClearTransient(world); OpenShop(world); }
    else {
        world.commands->Playback(); world.effectCommands->Playback();
        world.reservedEnemies = world.reservedProjectiles = world.reservedPickups = world.reservedEffects = 0;
        world.reservedHostileProjectiles = 0;
        world.reservedStatuses = 0;
        RefreshCounts(world);
    }
}
bool GameModule::EquipWeapon(WeaponKind kind) { return started_ && Brotato::EquipWeapon(world_, kind); }
bool GameModule::SelectUpgrade(UpgradeKind kind) {
    if (!started_ || !Brotato::SelectUpgrade(world_, kind)) return false;
    accumulator_ = 0; return true;
}
ShopResult GameModule::BuyShopItem(std::size_t slot) { return started_ ? Brotato::BuyShopItem(world_,slot) : ShopResult::Invalid; }
ShopResult GameModule::BuyShopOffer(std::size_t slot) { return started_ ? Brotato::BuyShopOffer(world_,slot) : ShopResult::Invalid; }
ShopResult GameModule::MergeWeapon(std::size_t slot) { return started_ ? Brotato::MergeWeapon(world_,slot) : ShopResult::Invalid; }
ShopResult GameModule::SellWeapon(std::size_t slot) { return started_ ? Brotato::SellWeapon(world_,slot) : ShopResult::Invalid; }
EquipmentSnapshot GameModule::ExtractEquipment() { return started_ ? Brotato::ExtractEquipment(world_,world_.player) : EquipmentSnapshot{}; }
bool GameModule::SelectWeaponSlot(std::size_t slot) { return started_ && Brotato::SelectWeaponSlot(world_,slot); }
ShopResult GameModule::RerollShop() { return started_ ? Brotato::RerollShop(world_) : ShopResult::Invalid; }
ShopResult GameModule::ToggleShopLock(std::size_t slot) { return started_ ? Brotato::ToggleShopLock(world_,slot) : ShopResult::Invalid; }
ShopSnapshot GameModule::ExtractShop() { return started_ ? Brotato::ExtractShop(world_) : ShopSnapshot{}; }
BuildSnapshot GameModule::ExtractBuild() { return started_ ? Brotato::ExtractBuild(world_,world_.player) : BuildSnapshot{}; }
RunResult GameModule::ExtractResult(){return started_?Get<RunProgress>(world_.player).result:RunResult{};}
RunSetupSnapshot GameModule::ExtractRunSetup(){return started_?Brotato::ExtractRunSetup(world_):RunSetupSnapshot{};}
EncounterSnapshot GameModule::ExtractEncounter(){return started_?Brotato::ExtractEncounter(world_):EncounterSnapshot{};}
std::vector<DrawBossStatus> GameModule::ExtractBossStatus(){return started_?Brotato::ExtractBossStatus(world_):std::vector<DrawBossStatus>{};}
ECS::EntityHandle GameModule::SpawnBoss(glm::vec2 position,bool ready,bool finalBoss){return started_?Brotato::SpawnBoss(world_,position,ready,finalBoss):ECS::EntityHandle(0);}
ECS::EntityHandle GameModule::SpawnEnemy(glm::vec2 position, bool ready, EnemyKind kind, ChampionKind champion) {
    return started_ ? Brotato::SpawnEnemy(world_, position, ready, kind, champion) : ECS::EntityHandle(0);
}
ECS::EntityHandle GameModule::SpawnPickup(glm::vec2 position) {
    return started_ ? Brotato::SpawnPickup(world_, position) : ECS::EntityHandle(0);
}
ECS::EntityHandle GameModule::SpawnProjectile(glm::vec2 position, glm::vec2 velocity) {
    return started_ ? CreateProjectile(world_, position, velocity, world_.selected, -1, std::atan2(velocity.y, velocity.x)) : ECS::EntityHandle(0);
}
std::vector<DrawSprite> GameModule::Extract() { return started_ ? ExtractSprites(world_) : std::vector<DrawSprite>{}; }
std::vector<DrawDamageText> GameModule::ExtractDamageText() { return started_ ? Brotato::ExtractDamageText(world_) : std::vector<DrawDamageText>{}; }
std::vector<DrawEnemyStatus> GameModule::ExtractEnemyStatus() { return started_ ? Brotato::ExtractEnemyStatus(world_) : std::vector<DrawEnemyStatus>{}; }
std::vector<DrawCombatCue> GameModule::ExtractCombatCues() { return started_ ? Brotato::ExtractCombatCues(world_) : std::vector<DrawCombatCue>{}; }
ECS::EntityHandle GameModule::SpawnHostileProjectile(glm::vec2 position, glm::vec2 velocity, int damage) {
    return started_ ? CreateHostileProjectile(world_,position,velocity,damage) : ECS::EntityHandle(0);
}
std::vector<GameEvent> GameModule::DrainEvents() {
    std::vector<GameEvent> result(world_.events.begin(), world_.events.end());
    world_.events.clear(); return result;
}
} // namespace Brotato
