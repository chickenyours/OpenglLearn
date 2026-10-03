#include "Brotato/Public/game_module.h"
#include "Brotato/Public/combat_geometry.h"
#include <algorithm>
#include <cmath>

namespace Brotato {
namespace {
constexpr float dt = float(GameModule::FixedStep);
glm::vec2 Direction(float angle) { return {std::cos(angle), std::sin(angle)}; }
glm::vec2 MoveTowards(glm::vec2 from, glm::vec2 to, float distance) {
    const auto delta = to - from;
    const float length = glm::length(delta);
    return length <= distance || length < 1e-6f ? to : from + delta * (distance / length);
}
}

bool GameModule::AliveEnemy(ECS::EntityHandle entity) {
    if (!scene_ || !scene_->IsAlive(entity)) return false;
    const auto* enemy = scene_->GetActiveComponent<Enemy>(entity.GetID()).Get();
    return enemy && enemy->phase == EnemyPhase::Alive;
}

ECS::EntityHandle GameModule::NearestEnemy(float range) {
    ECS::EntityHandle result(0);
    const auto position = Get<Transform>(player_).position;
    float nearestDistance = range * range;
    for (auto entity : enemies_) {
        if (!AliveEnemy(entity)) continue;
        const auto delta = Get<Transform>(entity).position - position;
        const float distance = glm::dot(delta, delta);
        if (distance < nearestDistance || (result.GetID() == 0 && distance <= nearestDistance)) {
            nearestDistance = distance; result = entity;
        }
    }
    return result;
}

void GameModule::ResetWeapon(bool resetCooldowns) {
    if (!started_ || !scene_->IsAlive(weapon_)) return;
    auto& weapon = Get<Weapon>(weapon_);
    const auto cooldowns = weapon.cooldowns;
    weapon = Weapon{};
    weapon.owner = player_; weapon.kind = selected_;
    if (!resetCooldowns) weapon.cooldowns = cooldowns;
    weapon.cooldown = weapon.cooldowns[WeaponIndex(selected_)];
    weapon.angle = Get<Sprite>(player_).flipX ? 3.14159265f : 0.f;
    const auto& definition = Definition(selected_);
    auto& transform = Get<Transform>(weapon_);
    transform.position = transform.previous = Get<Transform>(player_).position + Direction(weapon.angle) * .55f;
    auto& sprite = Get<Sprite>(weapon_);
    sprite.image = definition.image; sprite.size = definition.size; sprite.angle = weapon.angle; sprite.flipX = false;
}

bool GameModule::EquipWeapon(WeaponKind kind) {
    if (!started_ || !ValidWeapon(kind)) return false;
    if (selected_ == kind) return true;
    selected_ = kind;
    // This selector is an explicit prototype loadout action. Retire outgoing
    // attacks; enemies/materials/player progression are unaffected. Cooldown
    // slots survive selection, so changing away and back cannot grant free shots.
    scene_->DeleteEntities(projectiles_); projectiles_.clear();
    std::erase_if(pending_, [](const auto& command) { return command.kind == Image::Projectile; });
    ResetWeapon(false); RefreshCounts();
    return true;
}

void GameModule::HitCapsule(glm::vec2 start, glm::vec2 end, float angle, float halfLength, float radius) {
    for (auto entity : enemies_) {
        if (!AliveEnemy(entity)) continue;
        const auto& transform = Get<Transform>(entity);
        if (Combat::CapsuleImpact(start, end, angle, halfLength, radius,
                transform.previous, transform.position, Get<Circle>(entity).radius) <= 1)
            Kill(entity, Get<Weapon>(weapon_).kind);
    }
}

void GameModule::ThrustTick() {
    auto& weapon = Get<Weapon>(weapon_);
    auto& transform = Get<Transform>(weapon_);
    const auto& definition = Definition(weapon.kind);
    const auto owner = Get<Transform>(player_).position;
    const float idleAngle = Get<Sprite>(player_).flipX ? 3.14159265f : 0.f;
    const auto home = owner + Direction(idleAngle) * .55f;
    const float previousAngle = weapon.angle;
    transform.previous = transform.position;
    weapon.elapsed += dt;
    if (weapon.phase == WeaponPhase::Outbound) {
        if (!AliveEnemy(weapon.target) ||
            glm::length(Get<Transform>(weapon.target).position - owner) > definition.range) {
            weapon.phase = WeaponPhase::Returning; weapon.target = ECS::EntityHandle(0);
        } else {
            const auto target = Get<Transform>(weapon.target).position;
            const auto delta = target - transform.position;
            if (glm::dot(delta, delta) > 1e-10f) weapon.angle = std::atan2(delta.y, delta.x);
            transform.position = MoveTowards(transform.position, target, definition.speed * dt);
            if (glm::length(target - transform.position) < 1e-5f) weapon.phase = WeaponPhase::Returning;
        }
    }
    // A frame which reached the target must not immediately overwrite the
    // outbound path with its return path. Return starts on the following tick.
    const bool movedOut = glm::length(transform.position - transform.previous) > 1e-7f;
    if (weapon.phase == WeaponPhase::Returning && !movedOut)
        transform.position = MoveTowards(transform.position, home, definition.speed * dt);

    HitCapsule(transform.previous + Direction(previousAngle) * definition.hitOffset,
               transform.position + Direction(weapon.angle) * definition.hitOffset,
               weapon.angle, definition.halfLength, definition.radius);
    if (weapon.phase == WeaponPhase::Outbound && !AliveEnemy(weapon.target)) {
        weapon.phase = WeaponPhase::Returning; weapon.target = ECS::EntityHandle(0);
    }
    const float timeLimit = 2 + 2 * definition.range / definition.speed;
    if ((weapon.phase == WeaponPhase::Returning && glm::length(transform.position - home) < 1e-5f) || weapon.elapsed > timeLimit) {
        weapon.phase = WeaponPhase::Idle; weapon.target = ECS::EntityHandle(0); weapon.elapsed = 0;
        transform.position = transform.previous = home; weapon.angle = idleAngle;
    }
    Get<Sprite>(weapon_).angle = weapon.angle;
}

void GameModule::BeamTick() {
    auto& weapon = Get<Weapon>(weapon_);
    auto& transform = Get<Transform>(weapon_);
    const auto& definition = Definition(weapon.kind);
    const auto owner = Get<Transform>(player_).position;
    if (AliveEnemy(weapon.target)) {
        const auto delta = Get<Transform>(weapon.target).position - owner;
        if (glm::dot(delta, delta) > 1e-10f) weapon.angle = std::atan2(delta.y, delta.x);
    }
    transform.previous = transform.position;
    transform.position = owner + Direction(weapon.angle) * .55f;
    Get<Sprite>(weapon_).angle = weapon.angle;
    weapon.elapsed += dt;
    const int stage = int(std::floor((weapon.elapsed + 1e-6f) / definition.segmentSeconds));
    weapon.activeSegments = stage <= 6 ? stage : std::max(0, 12 - stage);
    if (stage >= 12) {
        weapon.phase = WeaponPhase::Idle; weapon.target = ECS::EntityHandle(0); weapon.activeSegments = 0; weapon.elapsed = 0;
        return;
    }
    for (int i = 0; i < weapon.activeSegments; ++i) {
        const auto offset = Direction(weapon.angle) * LaserCenters[i];
        HitCapsule(transform.previous + offset, transform.position + offset,
                   weapon.angle, definition.halfLength, definition.radius);
    }
}

void GameModule::Fire() {
    if (state_ != State::Playing || !config_.armed) return;
    auto& weapon = Get<Weapon>(weapon_);
    const auto& definition = Definition(weapon.kind);
    for (auto& cooldown : weapon.cooldowns) cooldown = std::max(0.f, cooldown - dt);
    weapon.cooldown = weapon.cooldowns[WeaponIndex(weapon.kind)];
    weapon.flash = std::max(0.f, weapon.flash - dt);
    if (weapon.phase == WeaponPhase::Outbound || weapon.phase == WeaponPhase::Returning) { ThrustTick(); return; }
    if (weapon.phase == WeaponPhase::Beam) { BeamTick(); return; }

    const auto owner = Get<Transform>(player_).position;
    weapon.target = NearestEnemy(definition.range);
    if (AliveEnemy(weapon.target)) {
        const auto delta = Get<Transform>(weapon.target).position - owner;
        if (glm::dot(delta, delta) > 1e-10f) weapon.angle = std::atan2(delta.y, delta.x);
    }
    auto& transform = Get<Transform>(weapon_);
    transform.previous = transform.position;
    if (definition.mode == AttackMode::Thrust) {
        const float idleAngle = Get<Sprite>(player_).flipX ? 3.14159265f : 0.f;
        transform.position = owner + Direction(idleAngle) * .55f;
    } else transform.position = owner + Direction(weapon.angle) * .55f;
    Get<Sprite>(weapon_).angle = weapon.angle;
    if (!AliveEnemy(weapon.target) || weapon.cooldown > 1e-5f) return;

    const std::size_t count = definition.mode == AttackMode::AuthoredBurst ? BurstPaths.size() : 1;
    if ((definition.mode == AttackMode::Projectile || definition.mode == AttackMode::AuthoredBurst) &&
        projectiles_.size() + count > config_.maxProjectiles) return;
    weapon.cooldown = weapon.cooldowns[WeaponIndex(weapon.kind)] = definition.cooldown;
    weapon.elapsed = 0; ++stats_.shots;
    EmitEvent(GameEventKind::WeaponAttack, weapon_, transform.position, weapon.kind);
    switch (definition.mode) {
    case AttackMode::Thrust:
        weapon.phase = WeaponPhase::Outbound; ThrustTick();
        break;
    case AttackMode::SegmentedBeam:
        weapon.phase = WeaponPhase::Beam; weapon.activeSegments = 0;
        break;
    case AttackMode::Projectile:
        pending_.push_back({Image::Projectile, transform.position, Direction(weapon.angle) * definition.speed, weapon.kind, -1, weapon.angle});
        if (weapon.kind == WeaponKind::Gun) weapon.flash = .06666667f;
        break;
    case AttackMode::AuthoredBurst:
        for (std::size_t path = 0; path < BurstPaths.size(); ++path)
            pending_.push_back({Image::Projectile, transform.position, {}, weapon.kind, int(path), weapon.angle});
        weapon.flash = .06666667f;
        break;
    }
}
} // namespace Brotato
