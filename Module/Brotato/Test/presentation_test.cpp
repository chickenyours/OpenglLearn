#include "Brotato/Public/game_module.h"
#include "Brotato/Public/animation_catalog.h"

#include <cmath>
#include <functional>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace Brotato;
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Near(double actual, double expected, double tolerance, const char* message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance)
        throw std::runtime_error(std::string(message) + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
}
Config QuietConfig() {
    Config config;
    config.minimum = {-100, -100}; config.maximum = {100, 100}; config.playerStart = {0, 0};
    config.enemySpeed = 0; config.spawning = false; config.armed = false; config.dropChance = 0;
    config.waveSeconds = 600;
    for (auto& weapon : config.weapons) weapon.gravity = 0;
    return config;
}
void Start(GameModule& game) { Check(game.Startup(), game.Error().c_str()); }
DrawSprite FindSprite(GameModule& game, Image image) {
    for (const auto& sprite : game.Extract()) if (sprite.image == image) return sprite;
    throw std::runtime_error("expected sprite is absent");
}
std::vector<DrawSprite> FindSprites(GameModule& game, Image image) {
    std::vector<DrawSprite> result;
    for (const auto& sprite : game.Extract()) if (sprite.image == image) result.push_back(sprite);
    return result;
}
void Tick(GameModule& game, int count, Input input = {}) { for (int tick = 0; tick < count; ++tick) game.FixedTick(input); }

void TestIdleSourceCurveAndPhysicalSeparation() {
    GameModule game(QuietConfig()); Start(game);
    Tick(game, 60);
    const auto& animation = game.Get<ActorAnimation>(game.PlayerEntity());
    Check(animation.clip == ActorClip::PlayerIdle, "stationary player uses the source idle clip");
    Near(animation.time, .5, 1e-9, "animation advances at simulation frequency");
    Near(animation.nodes[1].position.y, .2, 1e-6, "source idle body bob reaches .2 at half second");
    Near(animation.nodes[1].scale.x, 1.15, 1e-6, "source idle body width reaches 1.15");
    Near(animation.nodes[1].scale.y, .85, 1e-6, "source idle body height reaches .85");
    Near(glm::length(game.Get<Transform>(game.PlayerEntity()).position), 0, 1e-9, "visual bob never changes actor physics position");
    Near(game.Get<Circle>(game.PlayerEntity()).radius, .46, 1e-6, "squash never changes collision radius");
    Tick(game, 60);
    Near(game.Get<ActorAnimation>(game.PlayerEntity()).nodes[1].position.y, .25, 1e-6, "one-second idle clip loops to its authored start");
}

void TestHorizontalMovementAndIdleReset() {
    GameModule game(QuietConfig()); Start(game); Tick(game, 12, {1, 0});
    const auto& animation = game.Get<ActorAnimation>(game.PlayerEntity());
    Check(animation.clip == ActorClip::PlayerMove, "horizontal movement activates walking as well as vertical input");
    Near(animation.nodes[1].position.y, .16, 1e-6, "move clip samples its .1-second bob key");
    Near(animation.nodes[1].scale.x, 1.2, 1e-6, "move clip samples source stretch");
    Near(animation.nodes[7].angle, -105 * std::numbers::pi / 180, 1e-5, "right leg uses authored angle keys");
    game.FixedTick();
    Check(game.Get<ActorAnimation>(game.PlayerEntity()).clip == ActorClip::PlayerIdle, "stopping resets to idle");
    Near(game.Get<ActorAnimation>(game.PlayerEntity()).nodes[7].scale.x, 1, 1e-6, "idle restores leg scale absent from a previous state");
    auto& transform = game.Get<Transform>(game.PlayerEntity()); transform.position = game.Settings().maximum;
    game.FixedTick({1, 1});
    Check(game.Get<ActorAnimation>(game.PlayerEntity()).clip == ActorClip::PlayerIdle, "clamped movement at the corner does not walk in place");
}

void TestActorAffineShearAndSourceCharacterLayers() {
    auto config = QuietConfig(); config.character = 4;
    GameModule game(config); Start(game); Tick(game, 12, {1, 0});
    const auto leg = FindSprite(game, Image::PlayerLegRight);
    Check(leg.affine, "layered legs use the renderer's full affine basis");
    const float angle = -105 * std::numbers::pi_v<float> / 180;
    const auto nativeSize = PlayerNodes[7].size;
    const glm::vec2 expectedX{-std::cos(angle) * 1.25f * 1.2f * nativeSize.x,
                             -std::sin(angle) * 1.25f * .8f * nativeSize.x};
    const glm::vec2 expectedY{-std::sin(angle) * 1.25f * 1.2f * nativeSize.y,
                             std::cos(angle) * 1.25f * .8f * nativeSize.y};
    Near(glm::length(leg.axisX - expectedX), 0, 1e-6, "parent squash precedes leg rotation for the x basis");
    Near(glm::length(leg.axisY - expectedY), 0, 1e-6, "parent squash precedes leg rotation for the y basis");
    Check(std::abs(glm::dot(leg.axisX, leg.axisY)) > .001f, "non-orthogonal leg basis preserves actual hierarchy shear");
    const auto body = FindSprite(game, Image::PlayerBody), overlay = FindSprite(game, Image::Player);
    const auto shadow = FindSprite(game, Image::PlayerShadow);
    Check(body.affine && overlay.affine && shadow.affine, "source potato body, selected overlay and shadow are separate nodes");
    Near(glm::length(overlay.axisX), Characters[4].size.x * .7974548 * 1.2, 1e-6,
         "selected character artwork uses the actual Itemes scale");
    Check(overlay.axisX.x < 0 && body.axisX.x > 0 && shadow.axisY.y < 0,
          "source Itemes reflection and negative shadow y scale survive extraction");
    Near(glm::length(body.axisX), PlayerNodes[3].size.x * 1.2, 1e-6, "character selection does not replace the base body mask");
    Tick(game, 1, {-1, 0});
    Check(FindSprite(game, Image::PlayerBody).axisX.x < 0 && FindSprite(game, Image::Player).axisX.x > 0,
          "facing left mirrors every layer while retaining the overlay's local reflection");
}

void TestActorLayerOrderUsesRootDepth() {
    GameModule game(QuietConfig()); Start(game);
    game.SpawnEnemy({10, 10}, true); game.SpawnEnemy({10, -10}, true);
    const std::array<Image, 6> order{Image::PlayerMark, Image::PlayerBody, Image::PlayerShadow,
        Image::PlayerLegRight, Image::PlayerLegLeft, Image::Player};
    for (int tick = 0; tick < 120; ++tick) {
        const auto sprites = game.Extract();
        Check(sprites.size() == 8 && sprites.front().image == Image::Enemy && sprites.back().image == Image::Enemy,
              "actor groups sort by their physical root depth");
        for (std::size_t index = 0; index < order.size(); ++index)
            Check(sprites[index + 1].image == order[index], "source sorting order keeps the Itemes overlay above its complete body group");
        game.FixedTick({tick < 60 ? 1.f : -1.f, tick < 60 ? 1.f : -1.f});
    }
}

void TestSpawnWarningAndEnemyDeathCurve() {
    auto config = QuietConfig(); config.playerStart = {-20, 0};
    GameModule game(config); Start(game);
    const auto enemy = game.SpawnEnemy({0, 0});
    Near(FindSprite(game, Image::Spawn).tint.a, 1, 1e-6, "warning initially visible");
    Tick(game, 24); Near(FindSprite(game, Image::Spawn).tint.a, 0, 1e-6, "warning turns off at .2 seconds");
    Tick(game, 36); Near(FindSprite(game, Image::Spawn).tint.a, 1, 1e-6, "warning turns on at .5 seconds");
    Tick(game, 120);
    Check(game.Get<Enemy>(enemy).phase == EnemyPhase::Alive && FindSprites(game, Image::Spawn).empty(), "warning becomes a live enemy at 1.5 seconds");
    game.SpawnProjectile({-2, 0}, {1000, 0}); game.FixedTick();
    Tick(game, 29);
    const auto& animation = game.Get<ActorAnimation>(enemy);
    Check(animation.clip == ActorClip::EnemyDeath, "kill enters source non-looping death clip");
    Near(animation.time, .25, 1e-9, "death clip begins when killed and advances deterministically");
    Near(animation.nodes[1].scale.x, .5, 1e-6, "halfway death scale is one half");
    Near(animation.nodes[1].angle, -std::numbers::pi, 1e-5, "halfway death rotation is minus 180 degrees");
    Tick(game, 30);
    Check(FindSprites(game, Image::Enemy).empty() && game.Scene()->IsAlive(enemy), "zero-scale death frame vanishes while corpse still awaits XP");
    Check(game.Get<Player>(game.PlayerEntity()).experience == 0, "visual death completion does not accelerate XP");
    Tick(game, 121);
    Check(!game.Scene()->IsAlive(enemy) && game.Get<Player>(game.PlayerEntity()).experience == 2, "original delayed XP retirement remains intact");
}

void TestHitParticlesAndDamageTextLifetime() {
    auto config = QuietConfig(); config.playerStart = {-20, 0};
    GameModule game(config); Start(game); game.SpawnEnemy({0, 0}, true);
    game.SpawnProjectile({-2, 0}, {1000, 0}); game.FixedTick();
    auto initial = FindSprites(game, Image::HitParticle);
    Check(initial.size() == 6 && game.Stats().effects == 7, "one hit creates a six-particle burst and one damage text entity");
    const auto text = game.ExtractDamageText();
    Check(text.size() == 1 && text[0].value >= 1 && text[0].value <= 8, "floating text is the source's decorative 1..8 value");
    Near(text[0].position.y, .972 - .758, 1e-6, "floating text includes the Canvas offset after the root's plane rotation");
    Near(text[0].position.x, -.003 - .039, 1e-6, "floating text includes its parent Canvas anchored x offset");
    for (const auto& particle : initial) {
        Check(particle.size.x >= .1f && particle.size.x <= .28f, "particle size comes from the source range");
        Check(glm::length(particle.position) <= .100001f, "particles start inside the source emission radius");
        Near(particle.tint.r, .8018868f, 1e-6, "particle tint uses the source color");
    }
    Tick(game, 30);
    const auto halfway = FindSprites(game, Image::HitParticle);
    Check(halfway.size() == initial.size(), "particles live through the first half of their source lifetime");
    for (std::size_t index = 0; index < halfway.size(); ++index) {
        Near(halfway[index].size.x / initial[index].size.x, .75, 2e-6, "particle size follows 1 minus normalized time squared");
        const auto displacement = halfway[index].position - initial[index].position;
        Check(displacement.x > 0 && std::abs(displacement.y / displacement.x) <= std::tan(7 * std::numbers::pi / 180) + 1e-5,
              "particles follow the source's narrow positive-x cone");
    }
    Tick(game, 30);
    Check(FindSprites(game, Image::HitParticle).empty() && game.Stats().effects == 1, "particle entities retire at .5 seconds");
    const auto heldText = game.ExtractDamageText();
    Check(heldText.size() == 1 && heldText[0].value == text[0].value, "text value does not change while animating");
    Near(heldText[0].position.y, .972 + .194, 1e-6, "completed text motion holds its source endpoint relative to the Canvas");
    Tick(game, 119);
    Check(game.Stats().effects == 1 && game.ExtractDamageText().size() == 1, "floating text still exists one tick before corpse retirement");
    Tick(game, 1);
    Check(game.Stats().effects == 0 && game.ExtractDamageText().empty(), "floating text retires with the delayed enemy lifetime");
}

void TestDamageTextUsesCorpseRetirementBoundary() {
    for (const auto duration : {.1f, 1.5f}) {
        auto config = QuietConfig(); config.playerStart = {-20, 0}; config.deathDelay = duration; config.maxEffects = 1;
        GameModule game(config); Start(game);
        const auto enemy = game.SpawnEnemy({0, 0}, true);
        game.SpawnProjectile({-2, 0}, {1000, 0}); game.FixedTick();
        const int lifetimeTicks = int(std::round(duration / GameModule::FixedStep));
        Tick(game, lifetimeTicks - 1);
        Check(game.Scene()->IsAlive(enemy) && game.ExtractDamageText().size() == 1,
              "corpse and its text both survive the penultimate delay tick");
        Tick(game, 1);
        Check(!game.Scene()->IsAlive(enemy) && game.Stats().effects == 0 && game.ExtractDamageText().empty(),
              "corpse and text retire on exactly the same tick for fractional lifetimes");
    }
    auto config = QuietConfig(); config.playerStart = {-20, 0};
    GameModule game(config); Start(game);
    const auto enemy = game.SpawnEnemy({0, 0}, true);
    game.SpawnProjectile({-2, 0}, {1000, 0}); game.FixedTick();
    game.Get<Enemy>(enemy).remaining = .001f; game.FixedTick();
    Check(!game.Scene()->IsAlive(enemy) && game.ExtractDamageText().empty() && game.Stats().effects == 6,
          "early corpse retirement removes only its attached text while independent particles continue");
    game.SpawnEnemy({10, 10}, true); game.SpawnEnemy({11, 10}, true); game.FixedTick();
    Check(!game.Scene()->IsAlive(enemy) && game.ExtractDamageText().empty(), "replacement enemy generations cannot retain old damage text");
}

void TestVisualFreezeExtractionAndRestart() {
    auto config = QuietConfig(); config.playerStart = {-20, 0};
    GameModule game(config); Start(game); game.SpawnEnemy({0, 0}, true);
    game.SpawnProjectile({-2, 0}, {1000, 0}); game.FixedTick(); Tick(game, 12, {1, 0});
    game.SetPaused(true);
    const auto animationTime = game.Get<ActorAnimation>(game.PlayerEntity()).time;
    const auto before = game.Extract(); const auto textBefore = game.ExtractDamageText();
    Tick(game, 30, {1, 1}); game.Advance(.2, {-1, 0});
    const auto after = game.Extract();
    Near(game.Get<ActorAnimation>(game.PlayerEntity()).time, animationTime, 1e-12, "pause freezes the animation clock");
    Check(before.size() == after.size(), "pause does not retire visual effects");
    for (std::size_t index = 0; index < before.size(); ++index) {
        Near(glm::length(before[index].position - after[index].position), 0, 1e-9, "extraction preserves sampled positions");
        Near(glm::length(before[index].axisX - after[index].axisX), 0, 1e-9, "extraction preserves sampled affine poses");
    }
    Near(game.ExtractDamageText()[0].position.y, textBefore[0].position.y, 1e-9, "pause freezes floating text as well as particles");
    game.Restart();
    Check(game.Stats().effects == 0 && game.ExtractDamageText().empty(), "restart clears all old effect archetypes");
    Near(game.Get<ActorAnimation>(game.PlayerEntity()).time, 0, 1e-12, "restart resets the player animation clock");
}

void TestVisualCapacityAndWaveCleanup() {
    auto config = QuietConfig(); config.maxEffects = 3; config.playerStart = {-20, 0};
    config.waveSeconds = float(GameModule::FixedStep * 3);
    GameModule game(config); Start(game); game.SpawnEnemy({0, 0}, true);
    game.SpawnProjectile({-2, 0}, {1000, 0}); game.FixedTick();
    Check(game.Stats().kills == 1 && game.Stats().effects == 3, "partial cosmetic burst obeys capacity while the kill succeeds");
    Tick(game, 2);
    Check(game.GetState() == State::WaveComplete && game.Stats().effects == 0 && game.ExtractDamageText().empty(), "wave completion clears all finite effects");
    const auto time = game.Get<ActorAnimation>(game.PlayerEntity()).time;
    Tick(game, 10); Near(game.Get<ActorAnimation>(game.PlayerEntity()).time, time, 1e-12, "wave menu freezes actor animation");
    auto noEffects = QuietConfig(); noEffects.maxEffects = 0; noEffects.playerStart = {-20, 0};
    GameModule disabled(noEffects); Start(disabled); disabled.SpawnEnemy({0, 0}, true);
    disabled.SpawnProjectile({-2, 0}, {1000, 0}); disabled.FixedTick();
    Check(disabled.Stats().kills == 1 && disabled.Stats().effects == 0 && disabled.DrainEvents().size() == 1,
          "zero effect budget does not suppress gameplay or its audio event");
}

void TestMuzzleFlashAuthoredHermiteFade() {
    auto config = QuietConfig(); config.armed = true; config.initialWeapon = WeaponKind::Gun;
    GameModule game(config); Start(game); game.SpawnEnemy({5, 0}, true); game.FixedTick();
    auto& weapon = game.Get<Weapon>(game.WeaponEntity());
    weapon.flash = .06666667f - .054166667f;
    const auto flash = FindSprite(game, Image::Muzzle);
    const float u = (.054166667f - .05f) / (.06666667f - .05f);
    const float expected = 1 - u * u * (3 - 2 * u);
    Near(flash.tint.a, expected, 2e-6, "muzzle flash alpha uses source Hermite fade instead of a linear approximation");
    Near(flash.position.y, game.Get<Transform>(game.WeaponEntity()).position.y - .024, 1e-6, "muzzle flash preserves both fire-point and authored y offsets");
    const auto remaining = weapon.flash;
    (void)game.Extract(); Near(weapon.flash, remaining, 1e-12, "flash extraction never advances the weapon timer");
}

void TestAttackAcceptanceAndWeaponIdentity() {
    for (std::size_t index = 0; index < WeaponCount; ++index) {
        auto config = QuietConfig(); config.armed = true; config.initialWeapon = WeaponKind(index);
        GameModule game(config); Start(game);
        game.SpawnEnemy({2.5f, 0}, true); game.FixedTick();
        const auto events = game.DrainEvents();
        Check(!events.empty() && events.front().kind == GameEventKind::WeaponAttack, "accepted attack emits its first event");
        Check(events.front().weapon == WeaponKind(index) && events.front().entity.GetID() == game.WeaponEntity().GetID(),
              "attack event carries its weapon kind and independent weapon entity");
        Check(events.front().tick == 1, "attack event captures its fixed tick");
        Check(game.DrainEvents().empty(), "draining an event consumes it exactly once");
    }
    auto config = QuietConfig(); config.armed = true; config.maxProjectiles = 0;
    GameModule blocked(config); Start(blocked); blocked.SpawnEnemy({2, 0}, true); blocked.FixedTick();
    Check(blocked.Stats().shots == 0 && blocked.DrainEvents().empty(), "capacity-rejected attack emits no sound event");
    blocked.Restart(); blocked.FixedTick();
    Check(blocked.DrainEvents().empty(), "an empty arena emits no weapon events");
}

void TestKillOnceAndCaptureActualWeapon() {
    auto config = QuietConfig(); config.armed = true; config.initialWeapon = WeaponKind::Torch;
    GameModule game(config); Start(game);
    const auto target = game.SpawnEnemy({1.3f, 0}, true); game.FixedTick();
    Check(game.Get<Enemy>(target).phase == EnemyPhase::Dying, "torch scenario kills during its outbound sweep");
    game.EquipWeapon(WeaponKind::Gun);
    auto events = game.DrainEvents();
    Check(events.size() == 2 && events[0].kind == GameEventKind::WeaponAttack && events[1].kind == GameEventKind::EnemyKilled,
          "accepted attack precedes its single kill event");
    Check(events[1].weapon == WeaponKind::Torch && events[1].entity.GetID() == target.GetID(),
          "kill retains the actual attack kind after the selected weapon changes");
    Near(events[1].position.x, 1.3f, 1e-6, "kill stores the impacted position");
    for (int tick = 0; tick < 60; ++tick) game.FixedTick();
    Check(game.DrainEvents().empty(), "dying enemies do not repeatedly emit kill events");

    auto projectileConfig = QuietConfig(); projectileConfig.playerStart = {-20, 0};
    projectileConfig.initialWeapon = WeaponKind::Burst;
    GameModule projectile(projectileConfig); Start(projectile);
    projectile.SpawnEnemy({0, 0}, true); projectile.SpawnProjectile({-2, 0}, {1000, 0}); projectile.FixedTick();
    events = projectile.DrainEvents();
    Check(events.size() == 1 && events[0].kind == GameEventKind::EnemyKilled && events[0].weapon == WeaponKind::Burst,
          "projectile impact reports its stored weapon kind");
}

void TestPickupEventSurvivesRetirement() {
    GameModule game(QuietConfig()); Start(game);
    const auto pickup = game.SpawnPickup({.1f, 0}); game.FixedTick();
    auto events = game.DrainEvents();
    Check(events.size() == 1 && events[0].kind == GameEventKind::MaterialCollected, "material collection emits one event");
    Check(!game.Scene()->IsAlive(events[0].entity), "event safely retains the full retired handle");
    const auto replacement = game.SpawnPickup({20, 0});
    Check(replacement.GetID() == pickup.GetID() && !game.Scene()->IsAlive(events[0].entity),
          "generation prevents an event subject from referring to a replacement entity");
    Near(events[0].position.x, .1f, 1e-6, "event position remains captured after packed-row reuse");
    game.FixedTick(); Check(game.DrainEvents().empty(), "retired collection is not replayed on following ticks");
}

void TestHurtContactEntryAndNoDamage() {
    GameModule game(QuietConfig()); Start(game);
    const auto enemy = game.SpawnEnemy({0, 0}, true); game.FixedTick();
    auto events = game.DrainEvents();
    Check(events.size() == 1 && events[0].kind == GameEventKind::PlayerHurt &&
          events[0].entity.GetID() == game.PlayerEntity().GetID(), "actual health loss emits a player subject event");
    for (int tick = 0; tick < 20; ++tick) game.FixedTick();
    Check(game.DrainEvents().empty(), "continuous contact does not repeatedly emit damage");
    game.Get<Transform>(enemy).position = {5, 0}; game.FixedTick();
    game.Get<Transform>(enemy).position = {0, 0}; game.FixedTick();
    Check(game.DrainEvents().size() == 1, "leaving and re-entering contact emits new damage");

    auto config = QuietConfig(); config.contactDamage = 0;
    GameModule harmless(config); Start(harmless); harmless.SpawnEnemy({0, 0}, true); harmless.FixedTick();
    Check(harmless.DrainEvents().empty(), "zero damage does not emit a hurt event");
}

void TestEventCapacityAndDrainRecovery() {
    GameModule game(QuietConfig()); Start(game);
    for (std::size_t index = 0; index < GameModule::MaxPendingEvents + 44; ++index)
        Check(game.SpawnPickup({0, 0}).GetID() != 0, "stress pickups fit the independent gameplay capacity");
    game.FixedTick();
    Check(game.Stats().pickups == 0 && game.Get<Player>(game.PlayerEntity()).materials == 300,
          "presentation overflow never drops gameplay rewards");
    Check(game.DroppedEventCount() == 44, "full event buffer counts precisely the dropped presentation events");
    const auto events = game.DrainEvents();
    Check(events.size() == GameModule::MaxPendingEvents, "unconsumed presentation memory is bounded");
    for (const auto& event : events) Check(event.kind == GameEventKind::MaterialCollected && event.tick == 1, "capacity preserves earliest ordered messages");
    game.SpawnPickup({0, 0}); game.FixedTick();
    Check(game.DrainEvents().size() == 1 && game.DroppedEventCount() == 44, "draining restores the complete buffer capacity");
    game.Restart(); Check(game.DroppedEventCount() == 0, "new presentation epoch resets its overflow count");
}

void TestEpochClearsWorldTransitions() {
    auto config = QuietConfig(); config.waveSeconds = float(GameModule::FixedStep * 3); config.waveIncrement = 0;
    GameModule game(config); Start(game);
    const auto initial = game.PresentationEpoch();
    game.SpawnPickup({0, 0}); game.FixedTick();
    game.FixedTick(); game.FixedTick();
    Check(game.GetState() == State::WaveComplete && game.PresentationEpoch() > initial && game.DrainEvents().empty(),
          "wave teardown invalidates queued events from its old entities");
    const auto completed = game.PresentationEpoch(); game.NextWave();
    Check(game.PresentationEpoch() > completed, "next wave has a fresh presentation epoch");
    game.SpawnPickup({0, 0}); game.FixedTick();
    const auto beforeRestart = game.PresentationEpoch(); game.Restart();
    Check(game.PresentationEpoch() > beforeRestart && game.DrainEvents().empty(), "restart discards pending old-world events");
    game.SpawnPickup({0, 0}); game.FixedTick();
    const auto beforeShutdown = game.PresentationEpoch(); game.Shutdown();
    Check(game.PresentationEpoch() > beforeShutdown && game.DrainEvents().empty(), "shutdown clears its presentation queue");
    Start(game); Check(game.PresentationEpoch() > beforeShutdown, "same module startup never reuses its old epoch");
}

void TestEventsFreezeAndExtractDoesNotEmit() {
    auto config = QuietConfig(); config.initialHealth = config.contactDamage;
    GameModule game(config); Start(game);
    game.SetPaused(true); game.SpawnEnemy({0, 0}, true);
    for (int tick = 0; tick < 12; ++tick) { game.FixedTick(); (void)game.Extract(); }
    Check(game.DrainEvents().empty(), "pause and extraction cannot emit gameplay events");
    game.SetPaused(false); game.FixedTick();
    Check(game.GetState() == State::Dead && game.DrainEvents().size() == 1, "lethal damage still emits its accepted event");
    for (int tick = 0; tick < 12; ++tick) { game.FixedTick(); (void)game.Extract(); }
    Check(game.DrainEvents().empty(), "dead-state render extraction cannot replay the lethal event");
}

void TestPresentationLeavesCombatRandomnessIndependent() {
    auto config = QuietConfig(); config.spawning = true; config.dropChance = .5f; config.armed = true;
    GameModule first(config), second(config); Start(first); Start(second);
    for (int tick = 0; tick < 1200; ++tick) {
        first.FixedTick({1, 0}); second.FixedTick({1, 0});
        (void)first.DrainEvents(); (void)first.Extract(); (void)first.Extract();
    }
    Check(first.Stats().kills == second.Stats().kills && first.Stats().shots == second.Stats().shots &&
          first.Stats().enemies == second.Stats().enemies && first.Stats().pickups == second.Stats().pickups,
          "presentation consumption does not perturb combat or loot random streams");
    const auto a = first.Extract(), b = second.Extract();
    Check(a.size() == b.size(), "deterministic presentation scenarios contain the same sprites");
    for (std::size_t index = 0; index < a.size(); ++index)
        Near(glm::length(a[index].position - b[index].position), 0, 1e-6, "presentation extraction never moves simulation entities");
}
} // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"source idle curves and physics separation", TestIdleSourceCurveAndPhysicalSeparation},
        {"horizontal move and idle reset", TestHorizontalMovementAndIdleReset},
        {"affine hierarchy and character layers", TestActorAffineShearAndSourceCharacterLayers},
        {"stable actor layer and root depth order", TestActorLayerOrderUsesRootDepth},
        {"spawn warning and delayed death curve", TestSpawnWarningAndEnemyDeathCurve},
        {"hit particles and damage text lifetime", TestHitParticlesAndDamageTextLifetime},
        {"damage text follows exact corpse retirement", TestDamageTextUsesCorpseRetirementBoundary},
        {"visual freeze extraction and restart", TestVisualFreezeExtractionAndRestart},
        {"effect capacity and wave cleanup", TestVisualCapacityAndWaveCleanup},
        {"authored muzzle Hermite fade", TestMuzzleFlashAuthoredHermiteFade},
        {"accepted attack event identity", TestAttackAcceptanceAndWeaponIdentity},
        {"kill event once and actual weapon", TestKillOnceAndCaptureActualWeapon},
        {"retired pickup event generation", TestPickupEventSurvivesRetirement},
        {"hurt event on actual contact damage", TestHurtContactEntryAndNoDamage},
        {"bounded event capacity and drain", TestEventCapacityAndDrainRecovery},
        {"world transitions clear event epoch", TestEpochClearsWorldTransitions},
        {"frozen state and pure extraction events", TestEventsFreezeAndExtractDoesNotEmit},
        {"presentation preserves combat randomness", TestPresentationLeavesCombatRandomnessIndependent},
    };
    int failures = 0;
    for (const auto& [name, test] : tests) {
        try { test(); std::cout << "[PASS] " << name << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "[FAIL] " << name << ": " << error.what() << '\n'; }
    }
    std::cout << tests.size() - failures << '/' << tests.size() << " presentation checks passed\n";
    return failures ? 1 : 0;
}
