#include "Brotato/Public/game_module.h"
#include "Brotato/Public/expanded_gameplay.h"
#include "Brotato/Systems/game_systems.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
using namespace Brotato;
struct Extra : Component<Extra> { int value = 37; };
void Check(bool test, const char* message) { if (!test) throw std::runtime_error(message); }
void Near(double a, double b, const char* message) { Check(std::isfinite(a) && std::abs(a - b) < 1e-5, message); }
template<class... T> auto Type(ECS::Core::Scene& scene) {
    auto description = scene.CreateArchTypeDescription();
    (description->AddComponentArray<T>(), ...);
    return scene.CreateArchType(description, 2);
}
Config Quiet(WeaponKind kind = WeaponKind::Wand) {
    auto c = ExpandedGameplay();c.encounters=false; // Isolate historical mechanics from encounter scheduling.

    c.builds=false; // Preserve the phase-7 fixed reward compatibility checks.
    c.minimum = {-100, -100}; c.maximum = {100, 100}; c.playerStart = {0, 0};
    c.enemySpeed = 0; c.waveSeconds = 600; c.spawning = c.armed = false;
    c.contactDamage = 0; c.dropChance = 0; c.deathDelay = float(SimulationStep * 2);
    c.initialWeapon = kind;
    for (auto& weapon : c.weapons) weapon.gravity = 0;
    return c;
}
void Start(GameModule& g) { Check(g.Startup(), g.Error().c_str()); }
void Tick(GameModule& g, int count, Input input = {}) { while (count-- > 0) g.FixedTick(input); }
Player& PlayerData(GameModule& g) { return g.Get<Player>(g.PlayerEntity()); }
CombatStats& Stats(GameModule& g) { return g.Get<CombatStats>(g.PlayerEntity()); }
Growth& GrowthData(GameModule& g) { return g.Get<Growth>(g.PlayerEntity()); }
void Grant(GameModule& g, int xp) { PlayerData(g).experience += xp; g.FixedTick(); }
ECS::EntityHandle Hit(GameModule& g, ECS::EntityHandle enemy) {
    const auto bullet = g.SpawnProjectile(g.Get<Transform>(enemy).position, {});
    g.FixedTick();
    Check(!g.Scene()->IsAlive(bullet), "projectile is consumed even by a nonlethal hit");
    return bullet;
}

void TestEnemyRoles() {
    auto c = Quiet(); c.enemySpeed = 2; c.contactDamage = 10;
    GameModule g(c); Start(g);
    const auto normal = g.SpawnEnemy({10, 0}, true);
    const auto fast = g.SpawnEnemy({10, 2}, true, EnemyKind::Fast);
    const auto armor = g.SpawnEnemy({10, -2}, true, EnemyKind::Armored);
    Check(g.Get<Health>(normal).current == 2 && g.Get<Health>(fast).current == 1 && g.Get<Health>(armor).current == 6,
          "three enemy roles have distinct health");
    Check(g.Get<Damage>(fast).amount == 7 && g.Get<Damage>(armor).amount == 20, "contact damage belongs to enemy components");
    g.FixedTick();
    Near(glm::length(g.Get<Velocity>(fast).value), 3.3, "runner moves faster");
    Near(glm::length(g.Get<Velocity>(armor).value), 1.3, "armored enemy moves slower");
    Check(g.Get<Sprite>(fast).image == Image::EnemyFast && g.Get<Sprite>(armor).image == Image::EnemyArmored,
          "distinct recovered art is used for the new roles");
    Check(g.ExtractEnemyStatus().size() == 3 && g.Stats().spawned == std::array<int,EnemyCount>{1,1,1}, "UI reports all roles");
    Check(g.SpawnEnemy({5, 0}, true, EnemyKind::Count).GetID() == 0, "invalid kind does not create an entity");
}

void TestRepeatedDamageAndExperience() {
    auto c = Quiet(); c.dropChance = 1;
    GameModule g(c); Start(g);
    const auto enemy = g.SpawnEnemy({4, 0}, true, EnemyKind::Armored);
    Hit(g, enemy);
    Check(g.Get<Health>(enemy).current == 4 && g.Stats().kills == 0 && g.Stats().hits == 1, "armor survives first hit");
    auto text = g.ExtractDamageText();
    Check(text.size() == 1 && text[0].damage && text[0].value == 2, "displayed damage is the actual health loss");
    Hit(g, enemy); Hit(g, enemy);
    Check(g.Get<Health>(enemy).current == 0 && g.Stats().kills == 1 && g.Stats().pickups == 1, "third hit kills and drops once");
    Check(PlayerData(g).experience == 0, "XP waits for the corpse to retire");
    Tick(g, 2);
    Check(!g.Scene()->IsAlive(enemy) && PlayerData(g).experience == 6, "armored corpse credits its XP once");
    Tick(g, 60);
    Check(PlayerData(g).experience == 6 && g.Stats().kills == 1 && g.ExtractDamageText().empty(), "damage text expires and XP is not repeated");
    const auto warning = g.SpawnEnemy({7, 0}, false);
    g.SpawnProjectile({7, 0}, {}); g.FixedTick();
    Check(g.Get<Health>(warning).current == 2, "spawn warnings cannot be damaged");
}

void TestDamageSnapshotAndCapacity() {
    auto c = Quiet(); c.maxEffects = 0;
    GameModule g(c); Start(g);
    const auto enemy = g.SpawnEnemy({4, 0}, true, EnemyKind::Armored);
    const auto before = g.SpawnProjectile({4, 0}, {});
    Stats(g).bonusDamage = 1;
    Check(g.Get<Damage>(before).amount == 2, "in-flight damage stays at its launch value");
    g.FixedTick(); Check(g.Get<Health>(enemy).current == 4, "later stat changes do not alter an existing projectile");
    Hit(g, enemy); Check(g.Get<Health>(enemy).current == 1, "new projectile uses increased damage");
    Hit(g, enemy);
    Check(g.Stats().kills == 1 && g.Stats().effects == 0, "effect capacity never blocks combat");

    c.armed = true;
    GameModule external(c); Start(external);
    external.Scene()->DeleteEntity(external.WeaponEntity());
    auto owners = Type<Transform, CombatStats, Extra>(*external.Scene());
    const auto owner = external.Scene()->CreateEntity(owners);
    external.Get<CombatStats>(owner).bonusDamage = 4;
    auto weapons = Type<Weapon, Transform, Sprite, Damage, Extra>(*external.Scene());
    const auto weapon = external.Scene()->CreateEntity(weapons);
    external.Get<Weapon>(weapon).owner = owner;
    external.SpawnEnemy({5, 0}, true, EnemyKind::Armored); external.FixedTick();
    Query<Projectile, Damage> bullets; bullets.Refresh(*external.Scene());
    Check(bullets.Count() == 1, "external weapon fires through the same ECS pipeline");
    for (auto chunk : bullets) for (std::size_t row = 0; row < chunk.count; ++row)
        Check(chunk.Get<Damage>()[row].amount == 6, "queued projectile snapshots its actual owner's stats");
}

void TestOneHitPerAttack() {
    for (auto kind : {WeaponKind::Torch, WeaponKind::Knife, WeaponKind::Laser}) {
        auto c = Quiet(kind); c.armed = true;
        GameModule g(c); Start(g);
        const auto enemy = g.SpawnEnemy({2, -.05f}, true, EnemyKind::Armored);
        g.Get<Health>(enemy).current = g.Get<Health>(enemy).maximum = 100;
        for (int i = 0; i < 60; ++i) g.FixedTick();
        Check(g.Stats().shots == 1 && g.Stats().hits == 1, "thrust return and beam segments hit each target once per attack");
        const int afterFirst = g.Get<Health>(enemy).current;
        for (int i = 0; i < 140 && g.Stats().hits < 2; ++i) g.FixedTick();
        Check(g.Stats().hits == 2 && g.Get<Health>(enemy).current < afterFirst, "a subsequent attack may hit the same enemy again");
    }
}

void TestGenerationAndVectorCompaction() {
    auto c = Quiet(WeaponKind::Laser); c.armed = true;
    GameModule g(c); Start(g);
    auto enemy = g.SpawnEnemy({2, -.05f}, true, EnemyKind::Armored);
    for (int i = 0; i < 20 && g.Stats().hits == 0; ++i) g.FixedTick();
    Check(g.Stats().hits == 1, "beam hits original generation");
    g.Scene()->DeleteEntity(enemy);
    const auto replacement = g.SpawnEnemy({2, -.05f}, true, EnemyKind::Armored);
    Check(replacement.GetID() == enemy.GetID() && !g.Scene()->IsAlive(enemy), "fixture reuses the entity slot with a new generation");
    Tick(g, 3);
    Check(g.Stats().hits == 2 && g.Get<Health>(replacement).current == 5, "new generation can be hit during the same beam");

    auto weapons = Type<Weapon, Damage, Transform, Sprite, Extra>(*g.Scene());
    const auto first = g.Scene()->CreateEntity(weapons);
    const auto second = g.Scene()->CreateEntity(weapons);
    g.Get<Weapon>(first).hitTargets.push_back(replacement);
    auto& remaining = g.Get<Weapon>(second);
    remaining.owner = g.PlayerEntity(); remaining.kind = WeaponKind::Gun;
    remaining.hitTargets.assign(64, replacement);
    g.Scene()->DeleteEntity(first);
    Check(g.Get<Weapon>(second).hitTargets.size() == 64 && g.Get<Extra>(second).value == 37,
          "ECS compaction preserves ownership of vector-bearing components");
    g.FixedTick();
    Check(g.Get<Weapon>(second).hitTargets.empty() && g.Get<Weapon>(second).cooldown > 0,
          "compacted weapon remains query-driven and can start an attack");
}

void TestLevelUpFreezeAndChoice() {
    GameModule g(Quiet()); Start(g);
    for (int i = 0; i < 4; ++i) Hit(g, g.SpawnEnemy({4.f + i * 2, 0}, true));
    Tick(g, 3);
    Check(g.GetState() == State::LevelUp && PlayerData(g).level == 1 && PlayerData(g).experience == 0 && GrowthData(g).pending == 1,
          "actual kills open the first three-choice level-up");
    const auto ticks = g.Stats().ticks; const auto time = g.Stats().remaining;
    const auto position = g.Get<Transform>(g.PlayerEntity()).position;
    const auto damageText = g.ExtractDamageText();
    Tick(g, 100, {1, 1}); g.Advance(10, {1, 1}); g.SetPaused(false);
    Check(g.Stats().ticks == ticks && g.Stats().remaining == time && g.Get<Transform>(g.PlayerEntity()).position == position,
          "level-up freezes simulation and wave countdown");
    Check(g.ExtractDamageText().size() == damageText.size(), "cosmetic lifetime also freezes");
    Check(!g.EquipWeapon(WeaponKind::Gun) && !g.SelectUpgrade(UpgradeKind::Count), "weapon changes and invalid choices cannot bypass level-up");
    Input choose; choose.chooseUpgrade = 1; g.Advance(100, choose);
    Check(g.GetState() == State::Playing && Stats(g).bonusDamage == 1 && GrowthData(g).pending == 0 && g.Stats().ticks == ticks,
          "choosing resumes without simulating paused time");
    Check(!g.SelectUpgrade(UpgradeKind::Damage), "one reward cannot be claimed twice");
    g.FixedTick({1, 0}); Check(g.Stats().ticks == ticks + 1, "normal ticking resumes after the choice");
}

void TestOverflowAndStatEffects() {
    auto c = Quiet(WeaponKind::Gun); c.armed = true;
    GameModule g(c); Start(g);
    Grant(g, 40);
    Check(PlayerData(g).level == 3 && PlayerData(g).experience == 4 && GrowthData(g).pending == 3, "large XP awards preserve all rewards and remainder");
    Check(g.SelectUpgrade(UpgradeKind::Damage) && g.GetState() == State::LevelUp, "remaining choices keep combat frozen");
    Check(g.SelectUpgrade(UpgradeKind::AttackSpeed) && g.SelectUpgrade(UpgradeKind::MoveSpeed), "all queued choices can be taken");
    Check(GrowthData(g).upgrades == std::array<int,UpgradeCount>{1,1,1}, "selected growth is stored on the ECS player");
    Near(Stats(g).attackSpeed, 1.2, "attack speed increased"); Near(Stats(g).moveSpeed, 1.15, "movement increased");
    g.FixedTick({1, 0}); Near(g.Get<Transform>(g.PlayerEntity()).position.x, c.playerSpeed * 1.15 * SimulationStep, "movement uses grown stats");
    const auto target = g.SpawnEnemy({5, 0}, true, EnemyKind::Armored);
    g.Get<Health>(target).current = g.Get<Health>(target).maximum = 100;
    g.FixedTick(); const auto firstShot = g.Stats().ticks;
    for (int i = 0; i < 30 && g.Stats().shots < 2; ++i) g.FixedTick();
    Check(g.Stats().shots == 2 && g.Stats().ticks - firstShot == 20, "attack speed shortens the gun recovery from 24 to 20 ticks");
    Check(g.Get<Health>(target).current <= 98, "weapon fire incorporates damage growth");
}

void TestWaveRestartAndDeath() {
    auto c = Quiet(); c.waveSeconds = .1f;
    GameModule g(c); Start(g);
    Grant(g, 8); Check(g.SelectUpgrade(UpgradeKind::Damage), "first growth selected");
    PlayerData(g).experience = 3; g.Get<Health>(g.PlayerEntity()).current = 111;
    Tick(g, 20); Check(g.GetState() == State::WaveComplete, "wave ends after choice");
    g.NextWave();
    Check(PlayerData(g).level == 1 && PlayerData(g).experience == 3 && Stats(g).bonusDamage == 1 && g.Get<Health>(g.PlayerEntity()).current == 111,
          "growth and health carry into the next wave");
    g.Get<Health>(g.PlayerEntity()).current = 0; PlayerData(g).experience = 12; g.FixedTick();
    Check(g.GetState() == State::Dead && GrowthData(g).pending == 0, "death takes precedence over new level-up");
    g.Revive(); g.FixedTick();
    Check(g.GetState() == State::LevelUp && Stats(g).bonusDamage == 1, "revive retains growth and then resolves earned XP");
    g.Restart();
    Check(g.GetState() == State::Playing && PlayerData(g).level == 0 && PlayerData(g).experience == 0 && GrowthData(g).pending == 0 && Stats(g).bonusDamage == 0,
          "restart clears the entire run's growth");
    Check(g.Get<Health>(g.PlayerEntity()).current == c.initialHealth, "restart restores maximum health");
}

void TestContactInvulnerabilityAndNaturalMix() {
    auto c = Quiet(); c.contactDamage = 10;
    GameModule g(c); Start(g);
    g.SpawnEnemy({0, 0}, true); g.SpawnEnemy({0, 0}, true, EnemyKind::Armored);
    g.FixedTick();
    const auto after = g.Get<Health>(g.PlayerEntity()).current;
    Check(after == c.initialHealth - 10, "overlapping enemies cannot stack damage during invulnerability");
    Tick(g, 40); Check(g.Get<Health>(g.PlayerEntity()).current == after, "contact damage respects invulnerability time");
    Tick(g, 52); Check(g.Get<Health>(g.PlayerEntity()).current < after, "continued overlap hurts again after protection expires");

    c.spawning = true; c.contactDamage = 0;
    GameModule mix(c); Start(mix); Tick(mix, 1800);
    const auto first = mix.Stats().spawned;
    Check(first[0] > 0 && first[1] > 0 && first[2] > 0, "ordinary waves naturally spawn all three types");
    mix.Restart(); Tick(mix, 1800); Check(mix.Stats().spawned == first, "seeded enemy mix is reproducible on restart");
}

void TestStandaloneGrowthAndHealthSystems() {
    GameWorld world(Quiet()); world.scene = std::make_unique<ECS::Core::Scene>();
    auto players = Type<Extra, Player, Growth, CombatStats, Health>(*world.scene);
    world.player = world.scene->CreateEntity(players);
    world.Get<Player>(world.player).experience = 8; world.Get<Health>(world.player).flash = .1f;
    ECS::System::Context context; context.scene = world.scene.get(); context.SetService(&world); context.deltaSeconds = SimulationStep;
    ECS::System::Pipeline pipeline; pipeline.Add<HealthSystem>(); pipeline.Add<GrowthSystem>(); pipeline.RunBefore<HealthSystem, GrowthSystem>();
    Check(pipeline.Start(context) && pipeline.Tick(context), "growth and health systems execute without the application facade");
    Check(world.state == State::LevelUp && world.Get<Growth>(world.player).pending == 1 && world.Get<Player>(world.player).level == 1,
          "standalone systems own the growth rules");
    Near(world.Get<Health>(world.player).flash, .1 - SimulationStep, "health timer is driven by its ECS query");
    pipeline.Stop(context);
}
}
int main() {
    RegisterComponents(); REGISTER_COMPONENT("brotato_growth_test_extra", Extra);
    const std::pair<const char*, std::function<void()>> tests[] = {
        {"enemy roles", TestEnemyRoles}, {"repeated damage and experience", TestRepeatedDamageAndExperience},
        {"damage snapshots and effect capacity", TestDamageSnapshotAndCapacity}, {"one hit per attack", TestOneHitPerAttack},
        {"generation reuse and vector compaction", TestGenerationAndVectorCompaction}, {"level-up freeze and choice", TestLevelUpFreezeAndChoice},
        {"overflow and stat effects", TestOverflowAndStatEffects}, {"wave restart and death", TestWaveRestartAndDeath},
        {"contact protection and natural mix", TestContactInvulnerabilityAndNaturalMix}, {"standalone growth and health", TestStandaloneGrowthAndHealthSystems},
    };
    for (const auto& [name, test] : tests) {
        try { test(); std::cout << "[pass] " << name << '\n'; }
        catch (const std::exception& error) { std::cerr << "[fail] " << name << ": " << error.what() << '\n'; return 1; }
    }
}
