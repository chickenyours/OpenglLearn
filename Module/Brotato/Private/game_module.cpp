#include "Brotato/Public/game_module.h"
#include "Brotato/Systems/game_systems.h"
#include "Brotato/Public/combat_geometry.h"
#include "Brotato/Public/animation_catalog.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Brotato {
namespace {
constexpr float dt = float(GameModule::FixedStep);
bool Finite(glm::vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }
glm::vec2 Unit(glm::vec2 v) { const float l = glm::length(v); return l > 1e-6f ? v / l : glm::vec2(0); }
float DistanceSquared(glm::vec2 a, glm::vec2 b) { const auto d = a - b; return glm::dot(d, d); }
template<class... T> auto Archetype(ECS::Core::Scene& scene) {
    auto description = scene.CreateArchTypeDescription();
    (description->AddComponentArray<T>(), ...);
    return scene.CreateArchType(description, 128);
}
// World positions of SpawnPosition's 364 entries, including its duplicated top
// row. Preserve the source weighting; choose one spawn timer instead of stacking
// recursive Unity coroutines on every wave.
glm::vec2 SourceSpawn(std::mt19937& random, const Config& config) {
    static constexpr float xs[] = {18.106762f,3.106762f,17.286762f,16.356762f,15.356762f,14.106762f,12.856762f,11.606762f,10.106762f,8.856762f,7.606762f,6.356762f,5.356762f,4.106762f};
    static constexpr float ys[] = {10.235486f,-9.764514f,-8.264514f,-7.264514f,-2.014514f,.985486f,4.735486f,7.735486f,9.485486f,8.735486f,5.985486f,6.985486f,3.485486f,1.735486f,2.485486f,-.014514f,-1.264514f,-.764514f,-3.264514f,-2.514514f,-4.764514f,-6.014514f,-4.264514f,-5.264514f,3.985486f,10.235486f};
    const auto index = random() % (14 * 26);
    return glm::clamp(glm::vec2(xs[index / 26], ys[index % 26]), config.minimum, config.maximum);
}
}

GameModule::GameModule(Config config) : config_(config), selected_(config.initialWeapon), random_(config.seed) {
    pipeline_.Add<WaveSystem>(); pipeline_.Add<MovementSystem>(); pipeline_.Add<WeaponSystem>();
    pipeline_.Add<ProjectileSystem>(); pipeline_.Add<ContactSystem>(); pipeline_.Add<PickupSystem>();
    pipeline_.Add<PresentationSystem>();
    pipeline_.RunBefore<WaveSystem, MovementSystem>();
    pipeline_.RunBefore<MovementSystem, WeaponSystem>();
    pipeline_.RunBefore<WeaponSystem, ProjectileSystem>();
    pipeline_.RunBefore<ProjectileSystem, ContactSystem>();
    pipeline_.RunBefore<ContactSystem, PickupSystem>();
    pipeline_.RunBefore<PickupSystem, PresentationSystem>();
}
void GameModule::Validate() const {
    if (config_.character >= Characters.size() || config_.map >= Maps.size())
        throw std::invalid_argument("Brotato: invalid character/map selection");
    if (!Finite(config_.minimum) || !Finite(config_.maximum) || !Finite(config_.playerStart) ||
        config_.minimum.x >= config_.maximum.x || config_.minimum.y >= config_.maximum.y)
        throw std::invalid_argument("Brotato: invalid arena bounds");
    for (float value : {config_.playerSpeed, config_.enemySpeed, config_.spawnWarning,
                       config_.deathDelay, config_.waveIncrement})
        if (!std::isfinite(value) || value < 0) throw std::invalid_argument("Brotato: invalid nonnegative setting");
    for (float value : {config_.waveSeconds})
        if (!std::isfinite(value) || value <= 0) throw std::invalid_argument("Brotato: invalid positive setting");
    if (!std::isfinite(config_.dropChance) || config_.dropChance < 0 || config_.dropChance > 1 ||
        config_.initialHealth <= 0 || config_.contactDamage < 0 ||
        config_.maxEnemies > 100000 || config_.maxProjectiles > 100000 || config_.maxPickups > 100000 || config_.maxEffects > 100000)
        throw std::invalid_argument("Brotato: invalid combat/capacity setting");
    if (!ValidWeapon(config_.initialWeapon)) throw std::invalid_argument("Brotato: invalid initial weapon");
    for (const auto& weapon : config_.weapons) {
        if (weapon.name.empty() || !Finite(weapon.size) || !Finite(weapon.attackSize) ||
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
        events_.reserve(MaxPendingEvents);
        scene_ = std::make_unique<ECS::Core::Scene>();
        playerType_ = Archetype<Transform, Velocity, Circle, Sprite, Player, ActorAnimation>(*scene_);
        weaponType_ = Archetype<Transform, Sprite, Weapon>(*scene_);
        enemyType_ = Archetype<Transform, Velocity, Circle, Sprite, Enemy, ActorAnimation>(*scene_);
        projectileType_ = Archetype<Transform, Velocity, Circle, Sprite, Projectile>(*scene_);
        pickupType_ = Archetype<Transform, Circle, Sprite, Pickup>(*scene_);
        effectType_ = Archetype<Transform, Velocity, Sprite, Effect>(*scene_);
        player_ = scene_->CreateEntity(playerType_);
        weapon_ = scene_->CreateEntity(weaponType_);
        context_.scene = scene_.get(); context_.SetService(this); context_.deltaSeconds = FixedStep;
        if (!pipeline_.Start(context_)) throw std::runtime_error("Brotato system pipeline failed to start");
        started_ = true; Restart(); error_.clear(); return true;
    } catch (const std::exception& error) { error_ = error.what(); Shutdown(); return false; }
}
void GameModule::Shutdown() {
    pipeline_.Stop(context_);
    ResetPresentation();
    enemies_.clear(); projectiles_.clear(); pickups_.clear(); effects_.clear(); pending_.clear(); pendingEffects_.clear(); retired_.clear();
    playerType_ = nullptr; weaponType_ = nullptr; enemyType_ = nullptr; projectileType_ = nullptr; pickupType_ = nullptr;
    effectType_ = nullptr;
    scene_.reset(); player_ = ECS::EntityHandle(0); weapon_ = ECS::EntityHandle(0); context_.scene = nullptr; context_.frameIndex = 0;
    started_ = false; accumulator_ = 0;
}
void GameModule::ClearTransient() {
    ResetPresentation();
    for (const auto& list : {&enemies_, &projectiles_, &pickups_, &effects_}) {
        scene_->DeleteEntities(*list); list->clear();
    }
    pending_.clear(); pendingEffects_.clear(); retired_.clear(); RefreshCounts();
    ResetWeapon(true);
}
void GameModule::Restart() {
    if (!started_) return;
    ClearTransient(); random_.seed(config_.seed); stats_ = {}; stats_.remaining = config_.waveSeconds;
    state_ = State::Playing; accumulator_ = 0; spawnTimer_ = double(random_() % 2); context_.frameIndex = 0;
    Get<Player>(player_) = Player{}; Get<Player>(player_).health = config_.initialHealth;
    auto& transform = Get<Transform>(player_); transform.position = glm::clamp(config_.playerStart, config_.minimum, config_.maximum); transform.previous = transform.position;
    Get<Velocity>(player_).value = {}; Get<Circle>(player_).radius = .46f;
    auto& sprite = Get<Sprite>(player_); sprite = Sprite{}; sprite.size = Characters[config_.character].size;
    ResetActorAnimation(player_, ActorClip::PlayerIdle);
    ResetWeapon(true); RefreshCounts();
}
void GameModule::NextWave() {
    if (!started_ || state_ != State::WaveComplete) return;
    ClearTransient(); ++stats_.wave;
    stats_.remaining = double(config_.waveSeconds) + double(config_.waveIncrement) * (stats_.wave - 1);
    state_ = State::Playing; accumulator_ = 0; spawnTimer_ = double(random_() % 2);
}
void GameModule::Revive() {
    if (started_ && state_ == State::Dead) { Get<Player>(player_).health = config_.initialHealth; state_ = State::Playing; accumulator_ = 0; }
}
void GameModule::SetPaused(bool paused) {
    if (paused && state_ == State::Playing) state_ = State::Paused;
    else if (!paused && state_ == State::Paused) state_ = State::Playing;
    accumulator_ = 0;
}
void GameModule::HandleInput(Input input) {
    if (input.selectWeapon >= 1 && input.selectWeapon <= int(WeaponCount)) EquipWeapon(WeaponKind(input.selectWeapon - 1));
    if (input.restart) Restart();
    else if (input.nextWave) NextWave();
    else if (input.revive) Revive();
    else if (input.pause) SetPaused(state_ != State::Paused);
}
void GameModule::FixedTick(Input input) { if (!started_) return; HandleInput(input); Step(input); }
void GameModule::Advance(double seconds, Input input) {
    if (!started_) return;
    HandleInput(input);
    if (state_ != State::Playing || !std::isfinite(seconds) || seconds <= 0) return;
    accumulator_ += std::min(seconds, .25);
    int steps = 0;
    while (accumulator_ + 1e-12 >= FixedStep && steps++ < 8 && state_ == State::Playing) {
        accumulator_ -= FixedStep; Step(input);
    }
    if (accumulator_ >= FixedStep) accumulator_ = std::fmod(accumulator_, FixedStep);
}
void GameModule::Step(Input input) {
    if (state_ != State::Playing) return;
    input_ = input; ++stats_.ticks; context_.frameIndex = stats_.ticks;
    if (!pipeline_.Tick(context_)) throw std::runtime_error("Brotato pipeline tick failed");
    Commit(); RefreshCounts();
}
ECS::EntityHandle GameModule::SpawnEnemy(glm::vec2 position, bool ready) {
    if (!started_ || !Finite(position) || enemies_.size() >= config_.maxEnemies) return ECS::EntityHandle(0);
    auto entity = scene_->CreateEntity(enemyType_); enemies_.push_back(entity);
    auto& t = Get<Transform>(entity); t.position = t.previous = position;
    auto& e = Get<Enemy>(entity); e.phase = ready ? EnemyPhase::Alive : EnemyPhase::Spawning; e.remaining = ready ? 0 : config_.spawnWarning;
    Get<Circle>(entity).radius = .43f; auto& s = Get<Sprite>(entity); s.image = Image::Enemy; s.size = {.769f, .898f};
    ResetActorAnimation(entity, ActorClip::EnemyMove);
    RefreshCounts(); return entity;
}
ECS::EntityHandle GameModule::SpawnPickup(glm::vec2 position) {
    if (!started_ || !Finite(position) || pickups_.size() >= config_.maxPickups) return ECS::EntityHandle(0);
    auto entity = scene_->CreateEntity(pickupType_); pickups_.push_back(entity);
    auto& t = Get<Transform>(entity); t.position = t.previous = position; Get<Circle>(entity).radius = .28f;
    auto& s = Get<Sprite>(entity); s.image = Image::Material; s.size = {.679f, .718f};
    RefreshCounts(); return entity;
}
ECS::EntityHandle GameModule::SpawnProjectile(glm::vec2 position, glm::vec2 velocity) {
    return CreateProjectile(position, velocity, selected_, -1, std::atan2(velocity.y, velocity.x));
}
ECS::EntityHandle GameModule::CreateProjectile(glm::vec2 position, glm::vec2 velocity, WeaponKind kind, int path, float heading) {
    if (!started_ || !Finite(position) || !Finite(velocity) || projectiles_.size() >= config_.maxProjectiles) return ECS::EntityHandle(0);
    const auto& definition = Definition(kind);
    auto entity = scene_->CreateEntity(projectileType_); projectiles_.push_back(entity);
    auto& t = Get<Transform>(entity); t.position = t.previous = position; Get<Velocity>(entity).value = velocity;
    auto& projectile = Get<Projectile>(entity);
    projectile.remaining = definition.lifetime; projectile.gravity = definition.gravity; projectile.halfLength = definition.halfLength;
    projectile.kind = kind; projectile.path = path; projectile.heading = heading; projectile.origin = position;
    Get<Circle>(entity).radius = definition.radius;
    auto& s = Get<Sprite>(entity); s.image = definition.attackImage; s.size = definition.attackSize; s.angle = heading;
    if (path >= 0) {
        t.position = t.previous = position + Combat::Rotate(BurstPaths.at(path).start, heading);
        s.angle += BurstPaths.at(path).angle;
    }
    RefreshCounts(); return entity;
}
void GameModule::RefreshCounts() {
    stats_.enemies = enemies_.size(); stats_.projectiles = projectiles_.size(); stats_.pickups = pickups_.size();
    stats_.weapons = started_ ? 1 : 0; stats_.effects = effects_.size();
}
void GameModule::WaveTick() {
    stats_.remaining = std::max(0.0, stats_.remaining - FixedStep);
    if (stats_.remaining < 1e-9) { stats_.remaining = 0; state_ = State::WaveComplete; return; }
    for (auto entity : enemies_) {
        auto& enemy = Get<Enemy>(entity);
        if (enemy.phase == EnemyPhase::Alive) continue;
        enemy.remaining -= dt;
        if (enemy.remaining > 1e-5f) continue;
        if (enemy.phase == EnemyPhase::Spawning) {
            enemy.phase = EnemyPhase::Alive;
            ResetActorAnimation(entity, ActorClip::EnemyMove);
        }
        else {
            auto& player = Get<Player>(player_); player.experience += 2;
            while (player.experience >= 100) { player.experience -= 100; ++player.level; }
            retired_.push_back(entity);
        }
    }
    if (config_.spawning && (spawnTimer_ -= FixedStep) <= 0) {
        pending_.push_back({Image::Enemy, SourceSpawn(random_, config_)});
        spawnTimer_ = std::max(FixedStep, double(random_() % 2));
    }
}
void GameModule::Move() {
    if (state_ != State::Playing) return;
    auto& p = Get<Transform>(player_); p.previous = p.position;
    glm::vec2 direction{input_.horizontal, input_.vertical};
    if (!Finite(direction)) direction = {};
    direction = Unit(glm::clamp(direction, glm::vec2(-1), glm::vec2(1)));
    Get<Velocity>(player_).value = direction * config_.playerSpeed;
    p.position = glm::clamp(p.position + direction * config_.playerSpeed * dt, config_.minimum, config_.maximum);
    if (direction.x != 0) Get<Sprite>(player_).flipX = direction.x < 0;
    for (auto entity : enemies_) {
        auto& t = Get<Transform>(entity); t.previous = t.position;
        auto& v = Get<Velocity>(entity); v.value = {};
        if (Get<Enemy>(entity).phase != EnemyPhase::Alive) continue;
        auto delta = p.position - t.position; v.value = Unit(delta) * config_.enemySpeed;
        t.position += Unit(delta) * std::min(glm::length(delta), config_.enemySpeed * dt);
        if (delta.x != 0) Get<Sprite>(entity).flipX = delta.x < 0;
    }
}
void GameModule::Kill(ECS::EntityHandle entity, WeaponKind weapon) {
    auto& enemy = Get<Enemy>(entity);
    if (enemy.phase != EnemyPhase::Alive) return;
    enemy.phase = EnemyPhase::Dying; enemy.remaining = config_.deathDelay; enemy.touchingPlayer = false;
    Get<Velocity>(entity).value = {}; ++stats_.kills;
    ResetActorAnimation(entity, ActorClip::EnemyDeath);
    QueueHitEffects(entity);
    EmitEvent(GameEventKind::EnemyKilled, entity, Get<Transform>(entity).position, weapon);
    if (double(random_()) / 4294967296.0 < config_.dropChance)
        pending_.push_back({Image::Material, Get<Transform>(entity).position});
}
void GameModule::Projectiles() {
    if (state_ != State::Playing) return;
    for (auto entity : projectiles_) {
        auto& bullet = Get<Projectile>(entity); auto& t = Get<Transform>(entity); auto& v = Get<Velocity>(entity);
        const float activeDt = std::min(dt, std::max(0.f, bullet.remaining));
        if (activeDt <= 0 || bullet.consumed) { retired_.push_back(entity); continue; }
        t.previous = t.position;
        bullet.age += activeDt;
        if (bullet.path >= 0) {
            const auto& path = BurstPaths.at(bullet.path);
            float time = std::clamp(bullet.age / Definition(bullet.kind).animationSeconds, 0.f, 1.f);
            time = time * time * (3 - 2 * time);
            t.position = bullet.origin + Combat::Rotate(glm::mix(path.start, path.end, time), bullet.heading);
            v.value = (t.position - t.previous) / activeDt;
        } else {
            v.value.y += bullet.gravity * activeDt; t.position += v.value * activeDt;
        }
        bullet.remaining -= activeDt;
        ECS::EntityHandle target(0); float first = 2;
        for (auto enemy : enemies_) {
            if (Get<Enemy>(enemy).phase != EnemyPhase::Alive) continue;
            const auto& other = Get<Transform>(enemy);
            const auto otherEnd = glm::mix(other.previous, other.position, activeDt / dt);
            const float hit = Combat::CapsuleImpact(t.previous, t.position, Get<Sprite>(entity).angle, bullet.halfLength,
                Get<Circle>(entity).radius, other.previous, otherEnd, Get<Circle>(enemy).radius);
            if (hit < first) { first = hit; target = enemy; }
        }
        if (first <= 1) { Kill(target, bullet.kind); bullet.consumed = true; }
        if (bullet.consumed || bullet.remaining <= 0) retired_.push_back(entity);
    }
}
void GameModule::Contacts() {
    if (state_ != State::Playing) return;
    auto& player = Get<Player>(player_); auto position = Get<Transform>(player_).position;
    for (auto entity : enemies_) {
        auto& enemy = Get<Enemy>(entity);
        if (enemy.phase != EnemyPhase::Alive) continue;
        const float radius = Get<Circle>(entity).radius + Get<Circle>(player_).radius;
        const bool touching = DistanceSquared(position, Get<Transform>(entity).position) <= radius * radius;
        if (touching && !enemy.touchingPlayer) {
            const int health = player.health;
            player.health = std::max(0, player.health - config_.contactDamage);
            if (player.health < health) EmitEvent(GameEventKind::PlayerHurt, player_, position);
        }
        enemy.touchingPlayer = touching;
    }
    if (player.health <= 0) state_ = State::Dead;
}
void GameModule::Pickups() {
    if (state_ != State::Playing) return;
    const auto position = Get<Transform>(player_).position;
    for (auto entity : pickups_) {
        auto& pickup = Get<Pickup>(entity); const float radius = Get<Circle>(player_).radius + Get<Circle>(entity).radius;
        if (!pickup.collected && DistanceSquared(position, Get<Transform>(entity).position) <= radius * radius) {
            pickup.collected = true; Get<Player>(player_).materials += pickup.value; retired_.push_back(entity);
            EmitEvent(GameEventKind::MaterialCollected, entity, Get<Transform>(entity).position);
        }
    }
}
void GameModule::Commit() {
    if (state_ == State::WaveComplete) { ClearTransient(); return; }
    // Deleting an ECS row can move another row. No borrowed component addresses
    // survive this boundary. Full generation handles reject stale references.
    scene_->DeleteEntities(retired_); retired_.clear();
    for (const auto list : {&enemies_, &projectiles_, &pickups_, &effects_})
        std::erase_if(*list, [&](auto entity) { return !scene_->IsAlive(entity); });
    for (const auto& command : pending_) {
        if (command.kind == Image::Enemy) SpawnEnemy(command.position);
        else if (command.kind == Image::Projectile) CreateProjectile(command.position, command.velocity, command.weaponKind, command.path, command.heading);
        else SpawnPickup(command.position);
    }
    pending_.clear();
    CommitEffects();
}
std::vector<DrawSprite> GameModule::Extract() {
    std::vector<DrawSprite> result;
    if (!started_) return result;
    auto append = [&](ECS::EntityHandle entity) {
        const auto& t = Get<Transform>(entity); const auto& s = Get<Sprite>(entity);
        result.push_back({s.image, t.position, s.size, s.angle, glm::vec4(1), s.flipX});
    };
    // Depth belongs to the actor's physical root. Animated limbs and bobbing
    // overlays must never move through other layers of their own actor.
    struct Group { ECS::EntityHandle entity; int kind; };
    std::vector<Group> groups;
    groups.reserve(pickups_.size() + enemies_.size() + 1);
    for (auto entity : pickups_) groups.push_back({entity, 0});
    for (auto entity : enemies_) groups.push_back({entity, 1});
    groups.push_back({player_, 2});
    std::stable_sort(groups.begin(), groups.end(), [&](const auto& a, const auto& b) {
        return Get<Transform>(a.entity).position.y > Get<Transform>(b.entity).position.y;
    });
    for (const auto& group : groups) {
        if (group.kind == 0) { append(group.entity); continue; }
        if (group.kind == 2) { AppendActorSprites(result, group.entity, true); continue; }
        const auto& enemy = Get<Enemy>(group.entity);
        if (enemy.phase != EnemyPhase::Spawning) { AppendActorSprites(result, group.entity, false); continue; }
        append(group.entity);
        auto& draw = result.back(); draw.image = Image::Spawn; draw.size = {1.1f, 1.1f};
        const float elapsed = config_.spawnWarning - enemy.remaining;
        draw.tint.a = elapsed + 1e-6f >= .2f && elapsed < .5f - 1e-6f ? 0.f : 1.f;
    }
    if (config_.armed) {
        append(weapon_);
        const auto& weapon = Get<Weapon>(weapon_); const auto& definition = Definition(weapon.kind);
        const auto position = Get<Transform>(weapon_).position;
        for (int i = 0; i < weapon.activeSegments; ++i)
            result.push_back({Image::LaserSegment, position + Combat::Rotate({LaserCenters[i], 0}, weapon.angle),
                definition.attackSize, weapon.angle});
        if (weapon.flash > 0) {
            constexpr float flashDuration = .06666667f;
            Render::Animation::Pose flash;
            Render::Animation::Sample(MuzzleFlash, flashDuration - weapon.flash,
                                      std::span<Render::Animation::Pose>(&flash, 1));
            result.push_back({Image::Muzzle, position + Combat::Rotate(flash.position, weapon.angle),
                {.5f, .48f}, weapon.angle, {1, 1, 1, flash.opacity}});
        }
    }
    for (auto entity : projectiles_) append(entity);
    for (auto entity : effects_) {
        if (Get<Effect>(entity).kind == EffectKind::HitParticle) {
            append(entity);
            result.back().tint = {.8018868f, .19290671f, .1970778f, 1};
        }
    }
    return result;
}
} // namespace Brotato
