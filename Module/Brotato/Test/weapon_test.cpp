#include "engine/ECS/Entity/entity.h"
#include "Brotato/Public/game_module.h"
#include "engine/ECS/Query/query.h"

#include <algorithm>
#include <array>
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
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance)
        throw std::runtime_error(std::string(message) + ": expected " +
                                 std::to_string(expected) + ", got " + std::to_string(actual));
}
Config QuietConfig(WeaponKind kind = WeaponKind::Wand) {
    Config config;
    config.minimum = {-100, -100}; config.maximum = {100, 100}; config.playerStart = {0, 0};
    config.enemySpeed = 0; config.waveSeconds = 600; config.dropChance = 0; config.contactDamage = 0;
    config.spawning = false; config.initialWeapon = kind;
    for (auto& weapon : config.weapons) weapon.gravity = 0;
    return config;
}
void Start(GameModule& game) {
    Check(game.Startup(), "Startup: " + game.Error());
    Check(game.Scene() && game.Scene()->IsAlive(game.WeaponEntity()), "startup creates a live weapon entity");
}
Weapon& Equipped(GameModule& game) { return game.Get<Weapon>(game.WeaponEntity()); }
Transform& WeaponTransform(GameModule& game) { return game.Get<Transform>(game.WeaponEntity()); }
void Tick(GameModule& game, int count, Input input = {}) {
    for (int index = 0; index < count; ++index) game.FixedTick(input);
}
void Until(GameModule& game, const std::function<bool()>& complete, int limit, const char* message) {
    for (int index = 0; index < limit && !complete(); ++index) game.FixedTick();
    Check(complete(), message);
}
std::size_t ImageCount(GameModule& game, Image image) {
    const auto sprites = game.Extract();
    return std::count_if(sprites.begin(), sprites.end(), [=](const auto& sprite) { return sprite.image == image; });
}
struct BulletSnapshot { Projectile projectile; Transform transform; Velocity velocity; };
std::vector<BulletSnapshot> Bullets(GameModule& game) {
    ECS::Core::ChunkQuery<ECS::Core::Require<Projectile, Transform, Velocity>,
                          ECS::Core::Optional<>, ECS::Core::Exclude<>> query;
    query.Refresh(*game.Scene());
    std::vector<BulletSnapshot> result;
    for (auto chunk : query) {
        const auto* bullets = chunk.Get<Projectile>();
        const auto* transforms = chunk.Get<Transform>();
        const auto* velocities = chunk.Get<Velocity>();
        for (std::size_t row = 0; row < chunk.count; ++row)
            result.push_back({bullets[row], transforms[row], velocities[row]});
    }
    return result;
}

void TestSourceCatalogAndIndependentEntity() {
    const auto definitions = DefaultWeapons();
    const float cooldowns[] = {1, 1, .63f, .72f, .2f, .8f};
    const float ranges[] = {30, 10.81f, 10, 10.81f, 11.1f, 10.5f};
    const AttackMode modes[] = {AttackMode::Projectile, AttackMode::Thrust, AttackMode::SegmentedBeam,
                               AttackMode::Thrust, AttackMode::Projectile, AttackMode::AuthoredBurst};
    for (std::size_t index = 0; index < WeaponCount; ++index) {
        Near(definitions[index].cooldown, cooldowns[index], 1e-6, "serialized source cooldown");
        Near(definitions[index].range, ranges[index], 1e-6, "serialized source detection range");
        Check(definitions[index].mode == modes[index], "source w1..w6 attack families remain distinct");
        GameModule game(QuietConfig(static_cast<WeaponKind>(index)));
        Start(game);
        Check(game.WeaponEntity().GetID() != game.PlayerEntity().GetID(), "weapon has its own ECS entity");
        Check(Equipped(game).owner.GetID() == game.PlayerEntity().GetID() &&
              game.Scene()->IsAlive(Equipped(game).owner), "weapon stores its live owner handle");
        Check(game.CurrentWeapon() == static_cast<WeaponKind>(index), "configured initial weapon is equipped");
        Check(ImageCount(game, definitions[index].image) == 1, "equipped weapon is present in the render snapshot");
        const auto old = game.CurrentWeapon();
        Check(!game.EquipWeapon(WeaponKind::Count) && game.CurrentWeapon() == old, "invalid selection does not mutate equipment");
    }
}

void TestSelectionInputAndIdempotence() {
    GameModule game(QuietConfig());
    Start(game);
    for (int selection = 1; selection <= int(WeaponCount); ++selection) {
        Input input; input.selectWeapon = selection; game.FixedTick(input);
        Check(game.CurrentWeapon() == static_cast<WeaponKind>(selection - 1), "number-key input selects source weapon slot");
    }
    Equipped(game).cooldowns[WeaponIndex(WeaponKind::Burst)] = .4f;
    Equipped(game).cooldown = .4f;
    const auto projectile = game.SpawnProjectile({40, 40}, {0, 0});
    Check(game.EquipWeapon(WeaponKind::Burst), "selecting the equipped weapon succeeds");
    Near(Equipped(game).cooldown, .4f, 1e-6, "same selection preserves recovery");
    Check(game.Scene()->IsAlive(projectile), "same selection preserves existing projectiles");
    Input invalid; invalid.selectWeapon = 99; game.FixedTick(invalid);
    Check(game.CurrentWeapon() == WeaponKind::Burst, "out-of-range input selection is ignored");
}

void TestTargetEligibilityForAllWeapons() {
    for (std::size_t index = 0; index < WeaponCount; ++index) {
        auto config = QuietConfig(static_cast<WeaponKind>(index));
        config.weapons[index].range = 3; config.spawnWarning = 100;
        GameModule game(config); Start(game);
        game.SpawnEnemy({4, 0}, true);
        game.SpawnEnemy({.2f, 0}, false);
        const auto dying = game.SpawnEnemy({.3f, 0}, true);
        game.Get<Enemy>(dying).phase = EnemyPhase::Dying;
        game.Get<Enemy>(dying).remaining = 100;
        game.FixedTick();
        Check(game.Stats().shots == 0, "weapon ignores warnings, dying enemies and out-of-range targets");
        game.SpawnEnemy({2.9f, 0}, true);
        const auto nearest = game.SpawnEnemy({0, 2.5f}, true);
        game.FixedTick();
        Check(game.Stats().shots == 1, "each weapon starts an attack for an eligible target");
        const float expectedAim = config.weapons[index].mode == AttackMode::Thrust ? std::atan2(2.5f, -.55f) : std::atan2(1.f, 0.f);
        Near(Equipped(game).angle, expectedAim, 1e-5, "weapon selects nearest eligible target");
        Check(Equipped(game).target.GetID() == nearest.GetID(), "weapon retains a full target handle");
    }
}

void TestGunSweptFirstImpact() {
    auto config = QuietConfig(WeaponKind::Gun); config.armed = false;
    GameModule game(config); Start(game);
    const auto farEnemy = game.SpawnEnemy({4, 0}, true);
    const auto nearEnemy = game.SpawnEnemy({0, 0}, true);
    game.SpawnProjectile({-2, 0}, {1000, 0});
    game.FixedTick();
    Check(game.Get<Enemy>(nearEnemy).phase == EnemyPhase::Dying &&
          game.Get<Enemy>(farEnemy).phase == EnemyPhase::Alive, "gun capsule hits only the first enemy along its swept path");
    Check(game.Stats().projectiles == 0 && game.Stats().kills == 1, "gun projectile is consumed by one impact");
}

void TestThrustHitsMultipleEnemiesAndReturns() {
    for (auto kind : {WeaponKind::Torch, WeaponKind::Knife}) {
        auto config = QuietConfig(kind);
        config.weapons[WeaponIndex(kind)].cooldown = 10;
        GameModule game(config); Start(game);
        const auto first = game.SpawnEnemy({2, 0}, true);
        const auto second = game.SpawnEnemy({2.1f, 0}, true);
        Until(game, [&] { return game.Stats().kills == 2; }, 30, "one thrust can hit multiple enemies");
        Check(game.Get<Enemy>(first).phase == EnemyPhase::Dying &&
              game.Get<Enemy>(second).phase == EnemyPhase::Dying, "thrust kills are recorded independently");
        Until(game, [&] { return Equipped(game).phase == WeaponPhase::Idle; }, 50, "thrust returns to its owner");
        Check(game.Stats().shots == 1 && game.Stats().projectiles == 0, "one thrust uses no projectile entities");
        Tick(game, 20);
        Check(game.Stats().kills == 2, "persistent melee overlap cannot duplicate death rewards");
    }
}

void TestThrustTargetRetirementAndSlotReuse() {
    for (auto kind : {WeaponKind::Torch, WeaponKind::Knife}) {
        auto config = QuietConfig(kind);
        config.weapons[WeaponIndex(kind)].cooldown = 10;
        GameModule game(config); Start(game);
        const auto target = game.SpawnEnemy({8, 0}, true);
        Tick(game, 4);
        Check(Equipped(game).phase == WeaponPhase::Outbound, "distant target starts an outbound thrust");
        auto& enemy = game.Get<Enemy>(target); enemy.phase = EnemyPhase::Dying; enemy.remaining = .001f;
        game.FixedTick({-1, 0});
        Check(!game.Scene()->IsAlive(target), "expired target is retired at the commit boundary");
        const auto replacement = game.SpawnEnemy({-20, 20}, true);
        Check(replacement.GetID() == target.GetID(), "scenario exercises reuse of the target entity slot");
        Check(game.Scene()->IsAlive(replacement) && !game.Scene()->IsAlive(target), "target generation rejects slot replacement");
        Tick(game, 30, {-1, 0});
        Check(Equipped(game).phase == WeaponPhase::Idle && Equipped(game).target.GetID() == 0,
              "retired target releases the lock and allows return to a moving owner");
        Check(glm::length(WeaponTransform(game).position - game.Get<Transform>(game.PlayerEntity()).position) < 1,
              "returned weapon follows the current player position");
        Check(game.Get<Enemy>(replacement).phase == EnemyPhase::Alive, "reused slot is not treated as the old melee target");
    }
}

void TestReturningThrustCanHit() {
    auto config = QuietConfig(WeaponKind::Knife);
    config.weapons[WeaponIndex(WeaponKind::Knife)].cooldown = 10;
    GameModule game(config); Start(game);
    const auto target = game.SpawnEnemy({8, 0}, true);
    Tick(game, 4);
    const auto halfway = WeaponTransform(game).position * .5f;
    auto& old = game.Get<Enemy>(target); old.phase = EnemyPhase::Dying; old.remaining = .001f;
    const auto intercept = game.SpawnEnemy(halfway, true);
    Until(game, [&] { return game.Get<Enemy>(intercept).phase == EnemyPhase::Dying; }, 20,
          "returning thrust still damages an enemy along its sweep");
    Check(game.Stats().kills == 1 && game.Stats().shots == 1, "returning hit belongs to the existing attack");
}

void TestLaserSixSegmentCycle() {
    GameModule game(QuietConfig(WeaponKind::Laser)); Start(game);
    game.SpawnEnemy({9, 0}, true);
    std::vector<int> transitions{0};
    for (int tick = 0; tick < 40; ++tick) {
        game.FixedTick();
        const auto segments = Equipped(game).activeSegments;
        Check(segments >= 0 && segments <= 6, "beam segment count is bounded");
        Check(ImageCount(game, Image::LaserSegment) == std::size_t(segments), "beam snapshot contains exactly its active segments");
        if (segments != transitions.back()) transitions.push_back(segments);
    }
    const std::vector<int> expected{0, 1, 2, 3, 4, 5, 6, 5, 4, 3, 2, 1, 0};
    Check(transitions == expected, "beam activates all six segments and retracts in reverse order");
    Check(Equipped(game).phase == WeaponPhase::Idle && game.Stats().shots == 1,
          "beam finishes without recursively spawning another attack");
}

void TestLaserPiercingAndDeadTargetCompletion() {
    GameModule game(QuietConfig(WeaponKind::Laser)); Start(game);
    const auto target = game.SpawnEnemy({9, 0}, true);
    Until(game, [&] { return Equipped(game).activeSegments == 6; }, 25, "beam reaches its sixth segment");
    const auto nearEnemy = game.SpawnEnemy({1.5f, 0}, true);
    const auto farEnemy = game.SpawnEnemy({2.8f, 0}, true);
    const auto outside = game.SpawnEnemy({2, 2}, true);
    auto& old = game.Get<Enemy>(target); old.phase = EnemyPhase::Dying; old.remaining = .001f;
    game.FixedTick();
    Check(game.Get<Enemy>(nearEnemy).phase == EnemyPhase::Dying &&
          game.Get<Enemy>(farEnemy).phase == EnemyPhase::Dying, "active laser segments pierce multiple enemies");
    Check(game.Get<Enemy>(outside).phase == EnemyPhase::Alive, "laser capsule excludes enemies outside its width");
    Check(!game.Scene()->IsAlive(target), "locked laser target is retired");
    Until(game, [&] { return Equipped(game).phase == WeaponPhase::Idle; }, 30,
          "beam completes its current retraction after losing the locked target");
    Check(game.Stats().shots == 1 && game.Stats().kills == 2, "target death cannot restart the beam or duplicate hits");
}

void TestAuthoredBurstPathsAndLifetime() {
    GameModule game(QuietConfig(WeaponKind::Burst)); Start(game);
    const auto target = game.SpawnEnemy({9, 0}, true);
    game.FixedTick();
    Check(game.Stats().shots == 1 && game.Stats().projectiles == 4, "double barrel creates the four authored pellets");
    Tick(game, 6);
    const auto bullets = Bullets(game);
    Check(bullets.size() == 4, "authored burst is stored as four ECS projectile entities");
    std::array<bool, 4> paths{};
    // At 0.05 / 0.25 seconds, a zero-tangent Hermite track reaches 0.104 of its path.
    constexpr float fraction = .104f;
    for (const auto& snapshot : bullets) {
        const auto& bullet = snapshot.projectile;
        Check(bullet.kind == WeaponKind::Burst && bullet.path >= 0 && bullet.path < 4, "pellet retains its authored path");
        Check(!paths[bullet.path], "each of the four authored tracks appears once"); paths[bullet.path] = true;
        const auto& track = BurstPaths[bullet.path];
        const auto expected = bullet.origin + track.start + (track.end - track.start) * fraction;
        Near(bullet.age, .05, 1e-6, "burst advances at the fixed simulation clock");
        Near(glm::length(snapshot.transform.position - expected), 0, 1e-5, "pellet follows the source zero-tangent animation");
        Near(bullet.gravity, 0, 1e-6, "authored motion does not accumulate conflicting rigid-body gravity");
    }
    Tick(game, 13);
    Check(game.Stats().projectiles == 0, "burst expires after the source 0.15-second lifetime");
    Check(game.Get<Enemy>(target).phase == EnemyPhase::Alive, "burst does not travel to the unused 0.25-second endpoint");
}

void TestSelectionCleanupAndCooldownPersistence() {
    GameModule game(QuietConfig(WeaponKind::Laser)); Start(game);
    const auto enemy = game.SpawnEnemy({9, 0}, true);
    const auto pickup = game.SpawnPickup({40, 40});
    Tick(game, 8);
    Check(Equipped(game).activeSegments > 0, "cleanup scenario starts with an active beam");
    const float laserRecovery = Equipped(game).cooldown;
    Check(game.EquipWeapon(WeaponKind::Gun), "weapon changes to the SMG");
    Check(Equipped(game).phase == WeaponPhase::Idle && Equipped(game).activeSegments == 0 &&
          Equipped(game).target.GetID() == 0 && ImageCount(game, Image::LaserSegment) == 0,
          "switching cancels the previous beam and target lock");
    Check(game.Scene()->IsAlive(enemy) && game.Scene()->IsAlive(pickup), "switching preserves enemies and materials");
    const auto bullet = game.SpawnProjectile({40, 0}, {1, 0});
    Check(game.EquipWeapon(WeaponKind::Laser), "switching back succeeds");
    Check(!game.Scene()->IsAlive(bullet) && game.Stats().projectiles == 0, "switching retires previous weapon projectiles");
    Near(Equipped(game).cooldown, laserRecovery, 1e-6, "switching back preserves the selected slot's recovery");
    game.FixedTick();
    Check(game.Stats().shots == 1, "switching cannot bypass cooldown");
    game.SetPaused(true);
    const auto recovery = Equipped(game).cooldowns;
    Tick(game, 60);
    Check(Equipped(game).cooldowns == recovery, "all weapon cooldown slots freeze while paused");
}

void TestPauseAndDeathFreezeActiveAttacks() {
    for (auto kind : {WeaponKind::Knife, WeaponKind::Laser, WeaponKind::Burst}) {
        GameModule game(QuietConfig(kind)); Start(game);
        game.SpawnEnemy({9, 0}, true); Tick(game, 6);
        game.SetPaused(true);
        const auto weapon = Equipped(game); const auto position = WeaponTransform(game).position;
        const auto bullets = Bullets(game); const auto ticks = game.Stats().ticks;
        Tick(game, 100);
        Check(game.Stats().ticks == ticks && Equipped(game).phase == weapon.phase &&
              Equipped(game).activeSegments == weapon.activeSegments, "paused attack state and simulation clock stay unchanged");
        Near(Equipped(game).elapsed, weapon.elapsed, 1e-6, "paused attack animation does not advance");
        Near(glm::length(WeaponTransform(game).position - position), 0, 1e-6, "paused melee transform does not move");
        const auto pausedBullets = Bullets(game);
        Check(pausedBullets.size() == bullets.size(), "paused projectile lifetime does not expire");
        for (std::size_t index = 0; index < bullets.size(); ++index)
            Near(glm::length(pausedBullets[index].transform.position - bullets[index].transform.position), 0, 1e-6,
                 "paused authored projectiles do not move");
        game.SetPaused(false); game.FixedTick();
        Check(game.Stats().ticks == ticks + 1, "attack simulation resumes after unpausing");
        game.Get<Player>(game.PlayerEntity()).health = 0; game.FixedTick();
        Check(game.GetState() == State::Dead, "scenario enters death state");
        const auto deathWeapon = Equipped(game); const auto deathTicks = game.Stats().ticks;
        Tick(game, 100);
        Check(game.Stats().ticks == deathTicks && Equipped(game).phase == deathWeapon.phase &&
              Equipped(game).cooldowns == deathWeapon.cooldowns, "death freezes attack phase and all cooldowns");
        game.Revive(); game.FixedTick();
        Check(game.GetState() == State::Playing && game.Stats().ticks == deathTicks + 1, "revive resumes attack simulation");
    }
}

void TestWaveBoundaryAndRestart() {
    for (auto kind : {WeaponKind::Knife, WeaponKind::Laser, WeaponKind::Burst}) {
        auto config = QuietConfig(kind); config.waveSeconds = .1f; config.waveIncrement = 0;
        GameModule game(config); Start(game);
        game.SpawnEnemy({9, 0}, true); game.FixedTick();
        Check(game.Stats().shots == 1, "wave boundary scenario begins with an attack");
        Until(game, [&] { return game.GetState() == State::WaveComplete; }, 20, "short wave completes");
        Check(game.CurrentWeapon() == kind && Equipped(game).phase == WeaponPhase::Idle &&
              Equipped(game).target.GetID() == 0 && Equipped(game).activeSegments == 0, "wave completion clears active attack state");
        Check(game.Stats().enemies == 0 && game.Stats().projectiles == 0 && game.Stats().pickups == 0,
              "wave completion clears all transient combat entities");
        Check(std::all_of(Equipped(game).cooldowns.begin(), Equipped(game).cooldowns.end(), [](float value) { return value == 0; }),
              "wave completion resets each weapon slot's recovery");
        Check(ImageCount(game, Image::LaserSegment) == 0 && ImageCount(game, Image::Muzzle) == 0,
              "wave-complete rendering contains no stale beam or muzzle effects");
        game.NextWave();
        Check(game.CurrentWeapon() == kind && game.Stats().wave == 2, "next wave retains the selected weapon");
        game.EquipWeapon(WeaponKind::Gun); game.SpawnEnemy({9, 0}, true); game.FixedTick();
        game.Restart();
        Check(game.CurrentWeapon() == WeaponKind::Gun && game.Stats().wave == 1 &&
              game.Stats().shots == 0 && game.Stats().projectiles == 0, "restart keeps selection and resets the run");
        Check(Equipped(game).phase == WeaponPhase::Idle && Equipped(game).cooldown == 0, "restart clears active weapon recovery");
    }
}

void TestProjectileCapacityAndRepeatedSelection() {
    auto config = QuietConfig(WeaponKind::Burst); config.maxProjectiles = 3;
    GameModule game(config); Start(game);
    game.SpawnEnemy({9, 0}, true);
    for (int tick = 0; tick < 240; ++tick) {
        game.FixedTick();
        Check(game.Stats().projectiles <= config.maxProjectiles, "burst never exceeds projectile capacity");
        Check(Bullets(game).size() == game.Stats().projectiles, "projectile statistics match committed ECS rows");
    }
    for (int iteration = 0; iteration < 36; ++iteration) {
        const auto kind = static_cast<WeaponKind>(iteration % int(WeaponCount));
        game.EquipWeapon(kind); game.FixedTick();
        Check(game.Scene()->IsAlive(game.WeaponEntity()), "repeated selection preserves the independent weapon entity");
        Check(ImageCount(game, game.Definition(kind).image) == 1, "repeated selection produces one equipped weapon sprite");
    }
}

void TestMultipleModulesAndShutdownReentry() {
    GameModule first(QuietConfig(WeaponKind::Laser)), second(QuietConfig(WeaponKind::Knife));
    Start(first); Start(second);
    first.SpawnEnemy({9, 0}, true); second.SpawnEnemy({9, 0}, true);
    Tick(first, 5); Tick(second, 3);
    const auto secondWeapon = Equipped(second); const auto secondPosition = WeaponTransform(second).position;
    first.EquipWeapon(WeaponKind::Burst); first.Restart();
    Check(second.CurrentWeapon() == WeaponKind::Knife && Equipped(second).phase == secondWeapon.phase,
          "one module's selection and restart do not affect another module's weapon");
    Near(glm::length(WeaponTransform(second).position - secondPosition), 0, 1e-6, "weapon transforms belong to their own scene");
    first.Shutdown(); Start(first); first.SpawnEnemy({9, 0}, true); first.FixedTick();
    Check(first.Stats().shots == 1 && first.Scene()->IsAlive(first.WeaponEntity()), "module can rebuild and fire after shutdown");
}

void TestInvalidWeaponConfiguration() {
    const std::vector<std::function<void(Config&)>> invalid = {
        [](Config& c) { c.initialWeapon = WeaponKind::Count; },
        [](Config& c) { c.weapons[WeaponIndex(WeaponKind::Knife)].speed = 0; },
        [](Config& c) { c.weapons[WeaponIndex(WeaponKind::Laser)].segmentSeconds = 0; },
        [](Config& c) { c.weapons[WeaponIndex(WeaponKind::Burst)].animationSeconds = 0; },
        [](Config& c) { c.weapons[WeaponIndex(WeaponKind::Gun)].radius = -1; },
        [](Config& c) { c.weapons[WeaponIndex(WeaponKind::Gun)].halfLength = -1; },
        [](Config& c) { c.weapons[WeaponIndex(WeaponKind::Torch)].range = std::numeric_limits<float>::quiet_NaN(); }
    };
    for (const auto& invalidate : invalid) {
        auto config = QuietConfig(); invalidate(config); GameModule game(config);
        Check(!game.Startup() && !game.IsStarted() && game.Scene() == nullptr && !game.Error().empty(),
              "invalid weapon definition is rejected before entering simulation");
    }
}
} // namespace

int main() {
    const std::pair<const char*, void (*)()> tests[] = {
        {"source weapon catalog and independent ECS entity", TestSourceCatalogAndIndependentEntity},
        {"selection input and same-selection idempotence", TestSelectionInputAndIdempotence},
        {"target eligibility for all six weapons", TestTargetEligibilityForAllWeapons},
        {"gun swept first impact", TestGunSweptFirstImpact},
        {"melee multiple hits and return", TestThrustHitsMultipleEnemiesAndReturns},
        {"melee target retirement, slot reuse and moving owner", TestThrustTargetRetirementAndSlotReuse},
        {"returning melee sweep", TestReturningThrustCanHit},
        {"laser complete six-segment cycle", TestLaserSixSegmentCycle},
        {"laser piercing and dead-target completion", TestLaserPiercingAndDeadTargetCompletion},
        {"four authored burst paths and source lifetime", TestAuthoredBurstPathsAndLifetime},
        {"selection cleanup and cooldown persistence", TestSelectionCleanupAndCooldownPersistence},
        {"pause, death and revive during active attacks", TestPauseAndDeathFreezeActiveAttacks},
        {"wave boundary and restart cleanup", TestWaveBoundaryAndRestart},
        {"projectile capacity and repeated selection", TestProjectileCapacityAndRepeatedSelection},
        {"multiple modules and shutdown reentry", TestMultipleModulesAndShutdownReentry},
        {"invalid weapon configuration", TestInvalidWeaponConfiguration}
    };
    int failures = 0;
    for (const auto& [name, test] : tests) {
        try { test(); std::cout << "[PASS] " << name << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "[FAIL] " << name << ": " << error.what() << '\n'; }
    }
    std::cout << (failures ? "Brotato weapon tests failed" : "All Brotato weapon tests passed") << '\n';
    return failures ? 1 : 0;
}
