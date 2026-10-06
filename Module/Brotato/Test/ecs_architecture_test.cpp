#include "Brotato/Public/game_module.h"
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
void Near(float a, float b, const char* message) { Check(std::abs(a - b) < 1e-5f, message); }
template<class... T> auto Type(ECS::Core::Scene& scene) {
    auto description = scene.CreateArchTypeDescription();
    (description->AddComponentArray<T>(), ...);
    return scene.CreateArchType(description, 2); // Exercise partial and full chunks.
}
Config Quiet() {
    Config c;
    c.playerStart = {0, 0}; c.minimum = {-100, -100}; c.maximum = {100, 100};
    c.spawning = false; c.armed = false; c.enemySpeed = 2; c.contactDamage = 0;
    c.dropChance = 0; c.waveSeconds = 600;
    return c;
}
ECS::EntityHandle EnemyAt(GameModule& game, ObjectWeakPtr<ECS::Core::ArchType>& type, glm::vec2 p) {
    auto entity = game.Scene()->CreateEntity(type);
    auto& transform = game.Get<Transform>(entity); transform.position = transform.previous = p;
    game.Get<Enemy>(entity).phase = EnemyPhase::Alive;
    game.Get<Damage>(entity).amount = game.Settings().contactDamage;
    game.Get<Circle>(entity).radius = .43f;
    game.Get<Sprite>(entity).image = Image::Enemy;
    game.Get<ActorAnimation>(entity).clip = ActorClip::EnemyMove;
    return entity;
}
void TestQueryMembershipAndCompaction() {
    GameModule game(Quiet()); Check(game.Startup(), "start");
    auto type = Type<Extra, Enemy, Transform, Velocity, Circle, Sprite, ActorAnimation, Health, Damage>(*game.Scene());
    std::vector<ECS::EntityHandle> entities;
    for (int i = 0; i < 7; ++i) entities.push_back(EnemyAt(game, type, {10.f, float(i)}));
    auto decoration = Type<Transform, Sprite>(*game.Scene());
    const auto unrelated = game.Scene()->CreateEntity(decoration);
    game.Get<Transform>(unrelated).position = {20, 20};
    game.FixedTick();
    Check(game.Stats().enemies == 7, "query discovers entities created outside the game factory");
    for (auto entity : entities) {
        Check(game.Get<Transform>(entity).position.x < 10, "movement covers every chunk of another archetype");
        Check(game.Get<ActorAnimation>(entity).time > 0, "animation discovers another archetype");
        Check(game.Get<Extra>(entity).value == 37, "unrelated component survives system execution");
    }
    Near(game.Get<Transform>(unrelated).position.x, 20, "unrelated component combination is not moved");
    game.Scene()->DeleteEntity(entities[1]);
    auto laterType = Type<Enemy, Extra, ActorAnimation, Circle, Velocity, Transform, Sprite, Health, Damage>(*game.Scene());
    const auto later = EnemyAt(game, laterType, {12, 0});
    game.FixedTick();
    Check(game.Stats().enemies == 7, "new archetype discovered after a previous tick and deletion");
    Check(game.Get<Transform>(later).position.x < 12, "new matching archetype participates");
    Check(!game.Scene()->IsAlive(entities[1]), "old generation stays dead");
    const auto sprites = game.Extract();
    Check(std::count_if(sprites.begin(), sprites.end(), [](const auto& s) { return s.image == Image::Enemy; }) == 7,
          "render extraction queries all matching actor archetypes");
    game.Restart();
    Check(game.Stats().enemies == 0 && !game.Scene()->IsAlive(later), "restart clears queried transients");
    Check(game.Scene()->IsAlive(unrelated), "restart preserves unrelated entities");
}
void TestExternalProjectileAndPickup() {
    auto config = Quiet(); config.enemySpeed = 0; config.deathDelay = .025f;
    GameModule game(config); Check(game.Startup(), "start");
    auto enemies = Type<Extra, Enemy, Transform, Velocity, Circle, Sprite, ActorAnimation, Health, Damage>(*game.Scene());
    const auto target = EnemyAt(game, enemies, {1, 0});
    auto bullets = Type<Extra, Projectile, Transform, Velocity, Circle, Sprite, Damage>(*game.Scene());
    const auto bullet = game.Scene()->CreateEntity(bullets);
    game.Get<Velocity>(bullet).value = {120, 0};
    game.Get<Projectile>(bullet).gravity = 0;
    game.Get<Circle>(bullet).radius = .22f;
    auto pickups = Type<Extra, Pickup, Circle, Sprite, Transform>(*game.Scene());
    const auto pickup = game.Scene()->CreateEntity(pickups);
    game.Get<Pickup>(pickup).value = 3;
    game.FixedTick();
    Check(game.Stats().kills == 1, "collision sees external projectile and target archetypes");
    Check(game.Get<Enemy>(target).phase == EnemyPhase::Dying, "death state stored on component");
    Check(!game.Scene()->IsAlive(bullet) && !game.Scene()->IsAlive(pickup), "structural commands commit after systems");
    Check(game.Get<Player>(game.PlayerEntity()).materials == 3, "pickup system queries external archetype");
    Check(game.Stats().effects > 0, "death produces deferred ECS effects");
    for (int i = 0; i < 3; ++i) game.FixedTick();
    Check(!game.Scene()->IsAlive(target), "wave lifecycle retires external corpse");
    Check(game.Get<Player>(game.PlayerEntity()).experience == 2, "corpse experience credited once");
    Check(game.ExtractDamageText().empty(), "owner-linked text retires with external corpse");
}
void TestCapacityAndWeaponDiscovery() {
    auto config = Quiet(); config.armed = true; config.enemySpeed = 0;
    config.maxProjectiles = 1; config.maxEnemies = 1;
    GameModule game(config); Check(game.Startup(), "start");
    auto enemies = Type<Extra, Enemy, Transform, Velocity, Circle, Sprite, ActorAnimation, Health, Damage>(*game.Scene());
    EnemyAt(game, enemies, {5, 0});
    Check(game.SpawnEnemy({6, 0}, true).GetID() == 0, "factory capacity includes externally created entities");
    auto weapons = Type<Extra, Weapon, Transform, Sprite, Damage>(*game.Scene());
    const auto second = game.Scene()->CreateEntity(weapons);
    game.Get<Weapon>(second).owner = game.PlayerEntity();
    game.FixedTick();
    Check(game.Stats().weapons == 2, "weapon query discovers another archetype without registration");
    Check(game.Stats().shots == 1 && game.Stats().projectiles == 1, "deferred capacity reservations prevent overflow");
    game.Scene()->DeleteEntity(game.WeaponEntity());
    // The additional weapon is still independently driven, without the module's primary handle.
    Query<Projectile> query; query.Refresh(*game.Scene());
    ECS::Core::CommandBuffer remove(*game.Scene());
    for (auto chunk : query) for (std::size_t row = 0; row < chunk.count; ++row) remove.Destroy(chunk.Entity(row, *game.Scene()));
    remove.Playback();
    game.FixedTick();
    Check(game.Get<Weapon>(second).cooldown > 0, "each queried weapon uses its own component state");
    Check(game.Stats().shots == 2 && game.Stats().weapons == 1, "remaining weapon operates after primary entity deletion");
}
void TestStandaloneSystem() {
    GameWorld world(Quiet());
    world.scene = std::make_unique<ECS::Core::Scene>();
    auto players = Type<Player, Transform, Velocity, Sprite>(*world.scene);
    world.player = world.scene->CreateEntity(players);
    auto enemies = Type<Extra, Enemy, Transform, Velocity, Sprite, Health, Damage>(*world.scene);
    const auto enemy = world.scene->CreateEntity(enemies);
    world.Get<Enemy>(enemy).phase = EnemyPhase::Alive;
    world.Get<Transform>(enemy).position = {10, 0};
    world.input.horizontal = 1;
    ECS::System::Context context; context.scene = world.scene.get(); context.SetService(&world);
    context.deltaSeconds = SimulationStep;
    ECS::System::Pipeline pipeline; pipeline.Add<MovementSystem>();
    Check(pipeline.Start(context) && pipeline.Tick(context), "system executes without GameModule service");
    Near(world.Get<Transform>(world.player).position.x, world.config.playerSpeed * float(SimulationStep), "standalone player movement");
    Check(world.Get<Transform>(enemy).position.x < 10, "standalone system queries enemy data");
    pipeline.Stop(context);
}
void TestExternalEffects() {
    GameModule game(Quiet()); Check(game.Startup(), "start");
    auto type = Type<Extra, Effect, Transform, Velocity, Sprite>(*game.Scene());
    const auto particle = game.Scene()->CreateEntity(type);
    auto& effect = game.Get<Effect>(particle);
    effect.kind = EffectKind::HitParticle; effect.initialSize = .2f;
    effect.lifetime = float(SimulationStep * 2);
    game.Get<Velocity>(particle).value = {6, 0};
    game.Get<Sprite>(particle).image = Image::HitParticle;
    game.FixedTick();
    Near(game.Get<Transform>(particle).position.x, .05f, "external effect advances from query");
    const auto draws = game.Extract();
    Check(std::count_if(draws.begin(), draws.end(), [](const auto& draw) { return draw.image == Image::HitParticle; }) == 1,
          "render extraction discovers external effect");
    game.FixedTick();
    Check(!game.Scene()->IsAlive(particle) && game.Stats().effects == 0, "external effect retires at command boundary");
}
void TestEnemyWithoutPresentation() {
    auto config = Quiet(); config.deathDelay = float(SimulationStep); config.enemySpeed = 0;
    GameModule game(config); Check(game.Startup(), "start");
    auto type = Type<Enemy, Transform, Circle, Extra, Health, Damage>(*game.Scene());
    const auto enemy = game.Scene()->CreateEntity(type);
    game.Get<Enemy>(enemy).phase = EnemyPhase::Alive;
    game.Get<Transform>(enemy).position = game.Get<Transform>(enemy).previous = {4, 0};
    game.SpawnProjectile({4, 0}, {});
    game.FixedTick();
    Check(game.Stats().kills == 1 && game.Get<Enemy>(enemy).phase == EnemyPhase::Dying,
          "collision does not require animation or velocity components on its target");
    game.FixedTick();
    Check(!game.Scene()->IsAlive(enemy) && game.Get<Player>(game.PlayerEntity()).experience == 2,
          "wave lifecycle works for an enemy with no presentation components");
}
} // namespace
int main() {
    RegisterComponents(); REGISTER_COMPONENT("brotato_ecs_test_extra", Extra);
    const std::pair<const char*, std::function<void()>> tests[] = {
        {"query membership and compaction", TestQueryMembershipAndCompaction},
        {"external projectile and pickup lifecycle", TestExternalProjectileAndPickup},
        {"capacity and weapon discovery", TestCapacityAndWeaponDiscovery},
        {"standalone system", TestStandaloneSystem},
        {"external effects", TestExternalEffects},
        {"enemy without presentation", TestEnemyWithoutPresentation},
    };
    for (const auto& [name, test] : tests) {
        try { test(); std::cout << "[pass] " << name << '\n'; }
        catch (const std::exception& error) { std::cerr << "[fail] " << name << ": " << error.what() << '\n'; return 1; }
    }
    return 0;
}
