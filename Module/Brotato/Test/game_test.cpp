#include "Brotato/Public/game_module.h"

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace Brotato;

void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void Near(double actual, double expected, double tolerance, const char* message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(std::string(message) + ": expected " +
                                 std::to_string(expected) + ", got " + std::to_string(actual));
    }
}

Config QuietConfig() {
    Config config;
    config.minimum = {-100, -100};
    config.maximum = {100, 100};
    config.playerStart = {0, 0};
    config.enemySpeed = 0;
    config.waveSeconds = 600;
    config.dropChance = 1;
    config.weapons[WeaponIndex(WeaponKind::Wand)].gravity = 0;
    config.spawning = false;
    config.armed = false;
    return config;
}

void Start(GameModule& game) {
    Check(game.Startup(), "Startup: " + game.Error());
    Check(game.IsStarted() && game.Scene(), "startup creates the ECS scene");
}

Player& PlayerData(GameModule& game) { return game.Get<Player>(game.PlayerEntity()); }
Transform& PlayerTransform(GameModule& game) { return game.Get<Transform>(game.PlayerEntity()); }

constexpr double StepSeconds() { return GameModule::FixedStep; }

void TickUntil(GameModule& game, const std::function<bool()>& complete,
               int limit, const char* message) {
    for (int tick = 0; tick < limit && !complete(); ++tick) game.FixedTick();
    Check(complete(), message);
}

void TestMovementAndBounds() {
    auto config = QuietConfig();
    config.minimum = {-1, -1};
    config.maximum = {1, 1};
    config.playerSpeed = 6;
    GameModule game(config);
    Start(game);
    game.FixedTick({1, 0});
    const auto axial = PlayerTransform(game).position;
    Near(axial.x, config.playerSpeed * StepSeconds(), 1e-5, "axial movement uses units per second");
    Near(axial.y, 0, 1e-6, "horizontal input does not move vertically");
    game.Restart();
    game.FixedTick({1, 1});
    const auto diagonal = PlayerTransform(game).position;
    Near(glm::length(diagonal), glm::length(axial), 1e-5, "diagonal movement is normalized");
    Near(diagonal.x, diagonal.y, 1e-6, "diagonal movement treats axes equally");
    for (int i = 0; i < 180; ++i) game.FixedTick({1, 1});
    const auto maximum = PlayerTransform(game).position;
    Check(maximum.x <= config.maximum.x && maximum.y <= config.maximum.y,
          "player remains inside the positive arena boundary");
    Check(maximum.x > 0 && maximum.y > 0, "player can reach the positive arena edge");
    for (int i = 0; i < 180; ++i) game.FixedTick({-1, -1});
    const auto minimum = PlayerTransform(game).position;
    Check(minimum.x >= config.minimum.x && minimum.y >= config.minimum.y,
          "player remains inside the negative arena boundary");
    Check(minimum.x < 0 && minimum.y < 0, "player can reach the negative arena edge");
}

void TestPauseDeathAndRevive() {
    auto config = QuietConfig();
    config.initialHealth = config.contactDamage = 10;
    GameModule game(config);
    Start(game);
    game.SetPaused(true);
    Check(game.GetState() == State::Paused, "pause changes the game state");
    const double remaining = game.Stats().remaining;
    const auto ticks = game.Stats().ticks;
    game.FixedTick({1, 0});
    game.Advance(.2, {1, 0});
    Near(game.Stats().remaining, remaining, 1e-9, "pause stops the wave clock");
    Check(game.Stats().ticks == ticks, "pause does not execute simulation ticks");
    Near(glm::length(PlayerTransform(game).position), 0, 1e-6, "pause stops movement");
    game.SetPaused(false);
    Check(game.GetState() == State::Playing, "unpause resumes the game");
    game.SpawnEnemy(PlayerTransform(game).position, true);
    game.FixedTick();
    Check(game.GetState() == State::Dead && game.Get<Health>(game.PlayerEntity()).current <= 0,
          "lethal contact transitions to dead");
    const double deadRemaining = game.Stats().remaining;
    const auto deadTicks = game.Stats().ticks;
    const auto deadPosition = PlayerTransform(game).position;
    for (int i = 0; i < 10; ++i) game.FixedTick({1, 0});
    game.Advance(.2, {1, 0});
    Near(game.Stats().remaining, deadRemaining, 1e-9, "death stops the wave clock");
    Check(game.Stats().ticks == deadTicks, "death stops simulation ticks");
    Near(glm::length(PlayerTransform(game).position - deadPosition), 0, 1e-6,
         "death stops movement");
    game.Revive();
    Check(game.GetState() == State::Playing && game.Get<Health>(game.PlayerEntity()).current > 0,
          "revive restores a living, playable player");
}

void TestSpawnWarningAndContactEnter() {
    auto config = QuietConfig();
    config.initialHealth = 100;
    config.contactDamage = 7;
    config.spawnWarning = .2f;
    GameModule game(config);
    Start(game);
    const auto enemy = game.SpawnEnemy({0, 0});
    game.FixedTick();
    Check(game.Get<Enemy>(enemy).phase == EnemyPhase::Spawning,
          "spawn warning precedes the live enemy");
    Check(game.Get<Health>(game.PlayerEntity()).current == 100, "spawn warnings do not deal contact damage");
    TickUntil(game, [&] { return game.Get<Health>(game.PlayerEntity()).current < 100; }, 120,
              "enemy can damage the player after its warning");
    Check(game.Get<Health>(game.PlayerEntity()).current == 93, "first contact deals exactly one damage event");
    for (int i = 0; i < 20; ++i) game.FixedTick();
    Check(game.Get<Health>(game.PlayerEntity()).current == 93, "continuous overlap does not repeat OnTriggerEnter damage");
    PlayerTransform(game).position = {4, 0};
    game.FixedTick();
    PlayerTransform(game).position = {0, 0};
    game.FixedTick();
    Check(game.Get<Health>(game.PlayerEntity()).current == 86, "leaving and re-entering causes a fresh contact event");
}

void TestSweptProjectileHit() {
    auto config = QuietConfig();
    config.playerStart = {-20, 0};
    config.deathDelay = 1;
    GameModule game(config);
    Start(game);
    const auto enemy = game.SpawnEnemy({0, 0}, true);
    game.SpawnProjectile({-3, 0}, {1000, 0});
    game.FixedTick();
    Check(game.Get<Enemy>(enemy).phase == EnemyPhase::Dying,
          "a fast projectile hits an enemy between its old and new positions");
    Check(game.Stats().projectiles == 0, "a hitting projectile is consumed once");
    Check(game.Stats().kills == 1 && PlayerData(game).experience == 0,
          "a projectile hit records the kill immediately but delays experience");
    Near(glm::length(PlayerTransform(game).position - config.playerStart), 0, 1e-6,
         "projectile collision does not move the player");
}

void TestDelayedDeathAndIndependentPickup() {
    auto config = QuietConfig();
    config.playerStart = {-20, 0};
    config.deathDelay = .2f;
    GameModule game(config);
    Start(game);
    const auto enemy = game.SpawnEnemy({0, 0}, true);
    game.SpawnProjectile({-2, 0}, {1000, 0});
    game.FixedTick();
    Check(game.Get<Enemy>(enemy).phase == EnemyPhase::Dying, "hit enemy enters delayed death");
    Check(game.Stats().kills == 1 && game.Stats().pickups == 1 && PlayerData(game).experience == 0,
          "hit immediately records the kill and material drop while XP waits for death completion");
    TickUntil(game, [&] { return game.Stats().enemies == 0; }, 120,
              "death animation eventually retires the enemy");
    Check(game.Stats().kills == 1 && PlayerData(game).experience == 2,
          "completed death awards two integer XP without counting the kill twice");
    Check(game.Stats().pickups == 1 && PlayerData(game).materials == 0,
          "material remains on the ground until collected");
    Check(!game.Scene()->IsAlive(enemy), "retired enemies have invalidated ECS handles");
    bool staleRejected = false;
    try { (void)game.Get<Enemy>(enemy); }
    catch (const std::exception&) { staleRejected = true; }
    Check(staleRejected, "component lookup rejects a stale entity handle");
    game.SpawnPickup(PlayerTransform(game).position);
    game.FixedTick();
    Check(PlayerData(game).materials == 1 && PlayerData(game).experience == 2,
          "collecting an independent material does not grant kill XP");
    PlayerTransform(game).position = {0, 0};
    game.FixedTick();
    Check(PlayerData(game).materials == 2 && game.Stats().pickups == 0,
          "player can collect the material dropped when the enemy was hit");
    Check(PlayerData(game).experience == 2 && game.Stats().kills == 1,
          "collecting the death drop does not double-count XP or kills");
}

void TestZeroDropChance() {
    auto config = QuietConfig();
    config.playerStart = {-20, 0};
    config.dropChance = 0;
    config.deathDelay = .2f;
    GameModule game(config);
    Start(game);
    game.SpawnEnemy({0, 0}, true);
    game.SpawnProjectile({-2, 0}, {1000, 0});
    game.FixedTick();
    Check(game.Stats().kills == 1 && game.Stats().pickups == 0,
          "zero drop chance suppresses materials without suppressing the kill");
    Check(PlayerData(game).experience == 0, "experience still waits for death completion");
    TickUntil(game, [&] { return game.Stats().enemies == 0; }, 120,
              "a non-dropping enemy still completes its death delay");
    Check(PlayerData(game).experience == 2 && game.Stats().pickups == 0,
          "XP is independent of material drop probability");
}

void TestFiftyKillsLevelUp() {
    auto config = QuietConfig();
    config.playerStart = {-20, 0};
    config.deathDelay = .001f;
    GameModule game(config);
    Start(game);
    for (int kill = 0; kill < 50; ++kill) {
        game.SpawnEnemy({0, 0}, true);
        game.SpawnProjectile({-2, 0}, {1000, 0});
        TickUntil(game, [&] { return game.Stats().enemies == 0; }, 10,
                  "each defeated enemy completes its death delay");
        Check(game.Stats().kills == kill + 1, "each enemy contributes one kill");
        if (kill == 48) {
            Check(PlayerData(game).level == 0 && PlayerData(game).experience == 98,
                  "49 kills remain below the first level threshold");
        }
    }
    Check(PlayerData(game).level == 1 && PlayerData(game).experience == 0,
          "50 kills consume exactly 100 XP for one level");
    Check(PlayerData(game).materials == 0 && game.Stats().pickups == 50,
          "level progression does not require collecting materials");
}

void TestWaveTransitionAndCleanup() {
    auto config = QuietConfig();
    config.waveSeconds = 20;
    config.waveIncrement = 7;
    GameModule game(config);
    Start(game);
    Near(game.Stats().remaining, 20, 1e-6, "first wave is 20 seconds");
    game.Get<Health>(game.PlayerEntity()).current = 97;
    PlayerData(game).level = 3;
    PlayerData(game).experience = 24;
    PlayerData(game).materials = 11;
    TickUntil(game, [&] { return game.Stats().remaining <= StepSeconds() * 1.5; }, 3000,
              "wave clock reaches its final tick");
    game.SpawnEnemy({20, 0}, true);
    game.SpawnProjectile({40, 0}, {1, 0});
    game.SpawnPickup({60, 0});
    Check(game.Stats().enemies == 1 && game.Stats().projectiles == 1 && game.Stats().pickups == 1,
          "wave completion is exercised with live transient entities");
    TickUntil(game, [&] { return game.GetState() == State::WaveComplete; }, 3,
              "wave countdown transitions to wave complete");
    Near(game.Stats().remaining, 0, 1e-6, "completed wave clock is clamped to zero");
    Check(game.Stats().enemies == 0 && game.Stats().projectiles == 0 && game.Stats().pickups == 0,
          "wave completion clears enemies, projectiles, and materials");
    const auto completedTicks = game.Stats().ticks;
    game.FixedTick({1, 0});
    Check(game.Stats().ticks == completedTicks, "wave complete awaits the next wave");
    game.NextWave();
    Check(game.GetState() == State::Playing && game.Stats().wave == 2,
          "next wave resumes gameplay with wave two");
    Near(game.Stats().remaining, 27, 1e-6, "second wave adds seven seconds");
    Check(game.Get<Health>(game.PlayerEntity()).current == 97 && PlayerData(game).level == 3 &&
          PlayerData(game).experience == 24 && PlayerData(game).materials == 11,
          "wave transitions preserve player health and progression");
    Check(game.Stats().enemies == 0 && game.Stats().projectiles == 0 && game.Stats().pickups == 0,
          "next wave starts without stale transient entities");
}

void TestRestartLifecycleAndMultipleInstances() {
    auto firstConfig = QuietConfig();
    firstConfig.playerSpeed = 2;
    auto secondConfig = QuietConfig();
    secondConfig.playerSpeed = 5;
    GameModule first(firstConfig), second(secondConfig);
    Start(first);
    Start(second);
    Check(first.Startup(), "startup is idempotent");
    first.FixedTick({1, 0});
    Near(PlayerTransform(second).position.x, 0, 1e-6, "one module does not move another module's player");
    Check(second.Stats().ticks == 0, "one module does not advance another module's clock");
    second.FixedTick({1, 0});
    Near(PlayerTransform(first).position.x, firstConfig.playerSpeed * StepSeconds(), 1e-5,
         "first scene keeps its own player components");
    Near(PlayerTransform(second).position.x, secondConfig.playerSpeed * StepSeconds(), 1e-5,
         "second scene keeps its own player components");
    first.SpawnEnemy({20, 0}, true);
    first.SpawnProjectile({40, 0}, {1, 0});
    first.SpawnPickup({60, 0});
    first.Get<Health>(first.PlayerEntity()).current = 1;
    PlayerData(first).level = 4;
    PlayerData(first).experience = 32;
    PlayerData(first).materials = 17;
    first.SetPaused(true);
    first.Restart();
    Check(first.GetState() == State::Playing && first.Stats().wave == 1,
          "restart returns to the first playing wave");
    Check(first.Stats().ticks == 0 && first.Stats().kills == 0 && first.Stats().shots == 0,
          "restart resets run statistics");
    Check(first.Stats().enemies == 0 && first.Stats().projectiles == 0 && first.Stats().pickups == 0,
          "restart removes transient entities");
    Check(first.Get<Health>(first.PlayerEntity()).current == firstConfig.initialHealth && PlayerData(first).level == 0 &&
          PlayerData(first).experience == 0 && PlayerData(first).materials == 0,
          "restart resets player progression");
    Near(glm::length(PlayerTransform(first).position - firstConfig.playerStart), 0, 1e-6,
         "restart restores the player spawn point");
    Check(second.Stats().ticks == 1, "restart does not reset other module instances");
    first.Shutdown();
    first.Shutdown();
    Check(!first.IsStarted() && first.Scene() == nullptr, "shutdown is idempotent and releases the scene");
    Start(first);
    first.FixedTick();
    Check(first.Stats().ticks == 1, "a stopped module can start and tick again");
}

void TestFixedAccumulationAndInvalidTime() {
    GameModule accumulated(QuietConfig()), direct(QuietConfig());
    Start(accumulated);
    Start(direct);
    const double step = StepSeconds();
    accumulated.Advance(step * .25, {1, 0});
    accumulated.Advance(step * .25, {1, 0});
    Check(accumulated.Stats().ticks == 0, "fractional elapsed time waits for a full step");
    accumulated.Advance(step, {1, 0});
    Check(accumulated.Stats().ticks == 1, "accumulated elapsed time runs exactly one available step");
    direct.FixedTick({1, 0});
    Near(PlayerTransform(accumulated).position.x, PlayerTransform(direct).position.x, 1e-6,
         "advance and direct fixed ticking apply the same movement");
    const double remaining = accumulated.Stats().remaining;
    accumulated.Advance(-1, {1, 0});
    accumulated.Advance(std::numeric_limits<double>::infinity(), {1, 0});
    accumulated.Advance(std::numeric_limits<double>::quiet_NaN(), {1, 0});
    Check(accumulated.Stats().ticks == 1, "invalid elapsed times do not advance simulation");
    Near(accumulated.Stats().remaining, remaining, 1e-9, "invalid times do not change the wave clock");
    accumulated.Advance(step, {1, 0});
    direct.FixedTick({1, 0});
    Check(accumulated.Stats().ticks == 2, "invalid times do not poison the fractional accumulator");
    Near(PlayerTransform(accumulated).position.x, PlayerTransform(direct).position.x, 1e-6,
         "valid fixed stepping resumes after invalid elapsed input");
}

void TestEntityCaps() {
    auto config = QuietConfig();
    config.maxEnemies = 2;
    config.maxProjectiles = 3;
    config.maxPickups = 4;
    GameModule game(config);
    Start(game);
    for (int i = 0; i < 8; ++i) {
        const auto enemy = game.SpawnEnemy({20 + float(i), 0}, true);
        const auto projectile = game.SpawnProjectile({40 + float(i), 0}, {1, 0});
        const auto pickup = game.SpawnPickup({60 + float(i), 0});
        Check((enemy.GetID() != 0) == (i < 2), "enemy cap returns an invalid handle on overflow");
        Check((projectile.GetID() != 0) == (i < 3), "projectile cap returns an invalid handle on overflow");
        Check((pickup.GetID() != 0) == (i < 4), "material cap returns an invalid handle on overflow");
    }
    Check(game.Stats().enemies == 2 && game.Stats().projectiles == 3 && game.Stats().pickups == 4,
          "entity statistics obey independently configured spawn caps");
    game.FixedTick();
    Check(game.Stats().enemies == 2 && game.Stats().projectiles == 3 && game.Stats().pickups == 4,
          "ordinary simulation preserves capped entities away from collisions");
    game.Restart();
    Check(game.SpawnEnemy({20, 0}, true).GetID() != 0 &&
          game.SpawnProjectile({40, 0}, {1, 0}).GetID() != 0 && game.SpawnPickup({60, 0}).GetID() != 0,
          "restart releases all entity capacity");
}

void TestAutomaticTargetingRangeAndCooldown() {
    auto config = QuietConfig();
    config.armed = true;
    config.weapons[WeaponIndex(WeaponKind::Wand)].range = 6;
    config.weapons[WeaponIndex(WeaponKind::Wand)].cooldown = .1f;
    config.weapons[WeaponIndex(WeaponKind::Wand)].speed = .1f;
    config.spawnWarning = 10;
    GameModule game(config);
    Start(game);
    game.SpawnEnemy({7, 0}, true);
    game.SpawnEnemy({.5f, 0}, false);
    game.FixedTick();
    Check(game.Stats().shots == 0 && game.Stats().projectiles == 0,
          "automatic fire ignores out-of-range enemies and spawn warnings");
    game.SpawnEnemy({4, 0}, true);
    game.SpawnEnemy({0, 2}, true);
    game.FixedTick();
    Check(game.Stats().shots == 1 && game.Stats().projectiles == 1,
          "automatic weapon fires at a live enemy within range");
    const float aim = std::atan2(2.029f, -.014f);
    Near(game.Get<Weapon>(game.WeaponEntity()).angle, aim, 1e-6,
         "automatic weapon aims at the nearest eligible enemy");
    bool foundProjectile = false;
    for (const auto& sprite : game.Extract()) {
        if (sprite.image != Image::Projectile) continue;
        foundProjectile = true;
        const glm::vec2 local{.692806249f + (.5f - .5002063f) * .47867508f,
                              .0135370124f + (.5f - .48976415f) * .48847755f};
        const glm::vec2 expected = glm::vec2(.014f, -.029f) +
            glm::vec2(local.x * std::cos(aim) - local.y * std::sin(aim),
                      local.x * std::sin(aim) + local.y * std::cos(aim));
        Near(glm::length(sprite.position - expected), 0, 1e-6,
             "projectile snapshot preserves the source fire point and Sprite pivot");
    }
    Check(foundProjectile, "automatic projectiles are visible in the render snapshot");
    for (int i = 0; i < 10; ++i) game.FixedTick();
    Check(game.Stats().shots == 1, "weapon cooldown prevents firing on every fixed tick");
    TickUntil(game, [&] { return game.Stats().shots == 2; }, 5,
              "weapon fires again once its configured cooldown expires");
}

void TestProjectileLifetimeBoundary() {
    auto config = QuietConfig();
    config.playerStart = {-20, 0};
    config.weapons[WeaponIndex(WeaponKind::Wand)].lifetime = .001f;
    GameModule game(config);
    Start(game);
    const auto enemy = game.SpawnEnemy({4, 0}, true);
    const auto projectile = game.SpawnProjectile({0, 0}, {1000, 0});
    game.FixedTick();
    Check(game.Stats().projectiles == 0 && !game.Scene()->IsAlive(projectile),
          "projectile expiration retires its ECS entity");
    Check(game.Get<Enemy>(enemy).phase == EnemyPhase::Alive && game.Stats().kills == 0,
          "an expiring projectile cannot hit beyond the distance traveled during its remaining lifetime");
}

void TestNearestProjectileImpact() {
    auto config = QuietConfig();
    config.playerStart = {-20, 0};
    config.deathDelay = 1;
    GameModule game(config);
    Start(game);
    const auto farEnemy = game.SpawnEnemy({4, 0}, true);
    const auto nearEnemy = game.SpawnEnemy({0, 0}, true);
    game.SpawnProjectile({-2, 0}, {1000, 0});
    game.FixedTick();
    Check(game.Get<Enemy>(nearEnemy).phase == EnemyPhase::Dying &&
          game.Get<Enemy>(farEnemy).phase == EnemyPhase::Alive && game.Stats().kills == 1,
          "projectile hits the first enemy along its sweep regardless of entity insertion order");
}

void TestGenerationAndRowCompaction() {
    GameModule game(QuietConfig());
    Start(game);
    const auto collected = game.SpawnPickup({0, 0});
    const auto survivor = game.SpawnPickup({50, 0});
    game.FixedTick();
    Check(!game.Scene()->IsAlive(collected) && game.Scene()->IsAlive(survivor),
          "collecting one pickup invalidates only its own handle");
    Near(game.Get<Transform>(survivor).position.x, 50, 1e-6,
         "surviving handles still resolve their component after packed-row deletion");
    const auto replacement = game.SpawnPickup({60, 0});
    Check(replacement.GetID() == collected.GetID(), "the scene reuses its available retired entity slot");
    Check(game.Scene()->IsAlive(replacement) && !game.Scene()->IsAlive(collected),
          "generation distinguishes a replacement from the old entity with the same ID");
    bool rejected = false;
    try { (void)game.Get<Pickup>(collected); }
    catch (const std::exception&) { rejected = true; }
    Check(rejected, "stale component lookup cannot access a reused entity slot");
    Near(game.Get<Transform>(replacement).position.x, 60, 1e-6,
         "replacement entity receives its own component values");
}

void CheckSameSpawns(GameModule& first, GameModule& second) {
    Check(first.GetState() == second.GetState() && first.Stats().wave == second.Stats().wave &&
          first.Stats().ticks == second.Stats().ticks && first.Stats().enemies == second.Stats().enemies,
          "identical seeded runs maintain the same wave and enemy counts");
    Near(first.Stats().remaining, second.Stats().remaining, 1e-9,
         "identical seeded runs maintain the same wave clock");
    const auto left = first.Extract(), right = second.Extract();
    Check(left.size() == right.size(), "identical seeded runs produce equal snapshot sizes");
    for (std::size_t i = 0; i < left.size(); ++i) {
        Check(left[i].image == right[i].image, "identical seeded runs produce equal spawn phases");
        Near(glm::length(left[i].position - right[i].position), 0, 1e-6,
             "identical seeded runs produce equal spawn positions");
        Near(left[i].tint.a, right[i].tint.a, 1e-6,
             "identical seeded runs produce equal spawn warning animation");
    }
}

void TestDeterministicSpawnerAndWaveReentry() {
    auto config = QuietConfig();
    config.spawning = true;
    config.waveSeconds = 3;
    config.waveIncrement = 0;
    config.spawnWarning = .05f;
    config.seed = 513;
    GameModule reference(config), reentered(config);
    Start(reference);
    Start(reentered);
    bool sawSpawn = false;
    for (int wave = 1; wave <= 3; ++wave) {
        int ticks = 0;
        while (reference.GetState() == State::Playing && ticks++ < 400) {
            reference.FixedTick();
            // Repeated UI requests while playing must not reset/stack spawning.
            reentered.NextWave();
            reentered.FixedTick();
            sawSpawn |= reference.Stats().enemies > 0;
            CheckSameSpawns(reference, reentered);
        }
        Check(reference.GetState() == State::WaveComplete, "deterministic test wave completes");
        Check(reference.Stats().enemies == 0 && reentered.Stats().enemies == 0,
              "both seeded simulations clear the completed wave");
        reference.NextWave();
        reentered.NextWave();
        reentered.NextWave();
        Check(reference.Stats().wave == wave + 1 && reentered.Stats().wave == wave + 1,
              "repeated next-wave requests advance once and do not install another spawn stream");
        CheckSameSpawns(reference, reentered);
    }
    Check(sawSpawn, "deterministic scenario exercises actual automatic spawning");
    reference.Restart();
    reentered.Restart();
    for (int i = 0; i < 240; ++i) {
        reference.FixedTick();
        reentered.FixedTick();
        CheckSameSpawns(reference, reentered);
    }
}

void TestBoundedCatchUp() {
    GameModule game(QuietConfig());
    Start(game);
    game.Advance(100, {1, 0});
    Check(game.Stats().ticks == 8, "one long frame performs at most eight catch-up steps");
    Near(PlayerTransform(game).position.x, game.Settings().playerSpeed * StepSeconds() * 8, 1e-5,
         "long-frame movement reflects only executed fixed steps");
    game.Advance(StepSeconds() * .25, {1, 0});
    Check(game.Stats().ticks == 8, "excess whole-step backlog is discarded after catch-up");
    game.Advance(StepSeconds(), {1, 0});
    Check(game.Stats().ticks == 9, "normal accumulation resumes after bounded catch-up");
}

void TestInvalidConfiguration() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    const std::vector<std::function<void(Config&)>> invalid = {
        [=](Config& c) { c.minimum.x = nan; },
        [](Config& c) { c.maximum = c.minimum; },
        [=](Config& c) { c.playerStart.y = infinity; },
        [](Config& c) { c.playerSpeed = -1; },
        [](Config& c) { c.enemySpeed = -1; },
        [](Config& c) { c.weapons[WeaponIndex(WeaponKind::Wand)].range = -1; },
        [](Config& c) { c.weapons[WeaponIndex(WeaponKind::Wand)].cooldown = 0; },
        [](Config& c) { c.weapons[WeaponIndex(WeaponKind::Wand)].speed = 0; },
        [](Config& c) { c.weapons[WeaponIndex(WeaponKind::Wand)].lifetime = 0; },
        [=](Config& c) { c.weapons[WeaponIndex(WeaponKind::Wand)].gravity = infinity; },
        [](Config& c) { c.spawnWarning = -1; },
        [](Config& c) { c.deathDelay = -1; },
        [](Config& c) { c.waveSeconds = 0; },
        [](Config& c) { c.waveIncrement = -1; },
        [](Config& c) { c.dropChance = -.01f; },
        [](Config& c) { c.dropChance = 1.01f; },
        [](Config& c) { c.initialHealth = 0; },
        [](Config& c) { c.contactDamage = -1; },
        [](Config& c) { c.maxEnemies = 100001; },
        [](Config& c) { c.maxProjectiles = 100001; },
        [](Config& c) { c.maxPickups = 100001; },
    };
    for (std::size_t index = 0; index < invalid.size(); ++index) {
        auto config = QuietConfig();
        invalid[index](config);
        GameModule game(config);
        Check(!game.Startup(), "invalid configuration rejected, case " + std::to_string(index));
        Check(!game.IsStarted() && game.Scene() == nullptr && !game.Error().empty(),
              "failed startup reports its error and releases the partial scene");
        game.Shutdown();
    }
}

} // namespace

int main() {
    const std::pair<const char*, void (*)()> tests[] = {
        {"movement normalization and boundaries", TestMovementAndBounds},
        {"pause, death, and revive", TestPauseDeathAndRevive},
        {"spawn warning and contact enter", TestSpawnWarningAndContactEnter},
        {"swept projectile hit", TestSweptProjectileHit},
        {"delayed death and independent material pickup", TestDelayedDeathAndIndependentPickup},
        {"zero material drop chance", TestZeroDropChance},
        {"fifty kills level up", TestFiftyKillsLevelUp},
        {"wave transition and transient cleanup", TestWaveTransitionAndCleanup},
        {"restart, lifecycle, and multiple instances", TestRestartLifecycleAndMultipleInstances},
        {"fixed accumulation and invalid elapsed time", TestFixedAccumulationAndInvalidTime},
        {"entity caps", TestEntityCaps},
        {"automatic targeting, range, and cooldown", TestAutomaticTargetingRangeAndCooldown},
        {"projectile lifetime boundary", TestProjectileLifetimeBoundary},
        {"nearest projectile impact", TestNearestProjectileImpact},
        {"entity generations and packed-row deletion", TestGenerationAndRowCompaction},
        {"deterministic spawning and wave re-entry", TestDeterministicSpawnerAndWaveReentry},
        {"bounded catch-up", TestBoundedCatchUp},
        {"invalid configuration", TestInvalidConfiguration},
    };
    int failures = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << (failures ? "Brotato tests failed" : "All Brotato tests passed") << '\n';
    return failures ? 1 : 0;
}

