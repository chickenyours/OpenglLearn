#include "Brotato/Public/game_world.h"
#include "Brotato/Systems/game_systems.h"
#include "Brotato/Public/combat_geometry.h"
#include "Brotato/Public/weapon_geometry.h"
#include <algorithm>
#include <cmath>

namespace Brotato {
namespace {
constexpr float dt = float(SimulationStep);
glm::vec2 Direction(float angle) { return {std::cos(angle), std::sin(angle)}; }
glm::vec2 MoveTowards(glm::vec2 from, glm::vec2 to, float distance) {
    const auto delta = to - from;
    const float length = glm::length(delta);
    return length <= distance || length < 1e-6f ? to : from + delta * (distance / length);
}
ECS::EntityHandle NearestEnemy(GameWorld& world, glm::vec2 position, float range,
                             const Query<Enemy, Transform, Circle, Health>& enemies) {
    ECS::EntityHandle result(0);
    float nearestDistance = range * range;
    for (auto chunk : enemies) {
        const auto* states = chunk.Get<Enemy>();
        const auto* transforms = chunk.Get<Transform>();
        for (std::size_t row = 0; row < chunk.count; ++row) {
            if (states[row].phase != EnemyPhase::Alive) continue;
            const auto delta = transforms[row].position - position;
            const float distance = glm::dot(delta, delta);
            const auto entity = chunk.Entity(row, *world.scene);
            // Storage compaction must not decide an equal-distance target.
            if (distance < nearestDistance || (distance == nearestDistance &&
                (result.GetID() == 0 || entity.GetID() < result.GetID()))) {
                nearestDistance = distance; result = entity;
            }
        }
    }
    return result;
}
void HitCapsule(GameWorld& world, Weapon& weapon, const Damage& damage, const Query<Enemy, Transform, Circle, Health>& enemies,
                glm::vec2 start, glm::vec2 end, float angle, float halfLength, float radius) {
    std::erase_if(weapon.hitTargets, [&](auto entity) { return !world.scene->IsAlive(entity); });
    for (auto chunk : enemies) {
        const auto* states = chunk.Get<Enemy>();
        const auto* transforms = chunk.Get<Transform>();
        const auto* circles = chunk.Get<Circle>();
        for (std::size_t row = 0; row < chunk.count; ++row) {
            if (states[row].phase != EnemyPhase::Alive) continue;
            const auto& transform = transforms[row];
            const auto entity = chunk.Entity(row, *world.scene);
            if (std::any_of(weapon.hitTargets.begin(), weapon.hitTargets.end(),
                [&](auto old) { return old.GetID() == entity.GetID(); })) continue;
            if (Combat::CapsuleImpact(start, end, angle, halfLength, radius,
                    transform.previous, transform.position, circles[row].radius) <= 1 &&
                ResolveHit(world, entity, damage, weapon.kind, Direction(angle))) weapon.hitTargets.push_back(entity);
        }
    }
}
void ThrustTick(GameWorld& world, Weapon& weapon, Transform& transform, Sprite& sprite, const Damage& damage,
                const Query<Enemy, Transform, Circle, Health>& enemies) {
    const auto& definition = world.Definition(weapon.kind);
    const auto owner = world.Get<Transform>(weapon.owner).position+WeaponMount(weapon);
    const auto& geometry = Geometry(weapon.kind);
    const float idleAngle = geometry.idleAngle;
    const auto home = WeaponRoot(owner, weapon.kind);
    const float previousAngle = weapon.angle;
    transform.previous = transform.position;
    weapon.elapsed += dt;
    if (weapon.phase == WeaponPhase::Outbound) {
        if (!AliveEnemy(world, weapon.target) ||
            glm::length(world.Get<Transform>(weapon.target).position - owner) > definition.range) {
            weapon.phase = WeaponPhase::Returning; weapon.target = ECS::EntityHandle(0);
        } else {
            const auto target = world.Get<Transform>(weapon.target).position;
            const auto delta = target - transform.position;
            if (glm::dot(delta, delta) > 1e-10f) weapon.angle = std::atan2(delta.y, delta.x);
            weapon.reflected = false;
            transform.position = MoveTowards(transform.position, target, definition.speed * dt);
            if (glm::length(target - transform.position) < 1e-5f) weapon.phase = WeaponPhase::Returning;
        }
    }
    const bool movedOut = glm::length(transform.position - transform.previous) > 1e-7f;
    if (weapon.phase == WeaponPhase::Returning && !movedOut)
        transform.position = MoveTowards(transform.position, home, definition.speed * dt);
    HitCapsule(world, weapon, damage, enemies,
        transform.previous + Direction(previousAngle) * definition.hitOffset,
        transform.position + Direction(weapon.angle) * definition.hitOffset,
        weapon.angle, definition.halfLength, definition.radius);
    if (weapon.phase == WeaponPhase::Outbound && !AliveEnemy(world, weapon.target)) {
        weapon.phase = WeaponPhase::Returning; weapon.target = ECS::EntityHandle(0);
    }
    const float timeLimit = 2 + 2 * definition.range / definition.speed;
    if ((weapon.phase == WeaponPhase::Returning && glm::length(transform.position - home) < 1e-5f) || weapon.elapsed > timeLimit) {
        weapon.phase = WeaponPhase::Idle; weapon.target = ECS::EntityHandle(0); weapon.elapsed = 0;
        transform.position = transform.previous = home; weapon.angle = idleAngle; weapon.reflected = geometry.idleReflected;
    }
    sprite.angle = weapon.angle;
}
void BeamTick(GameWorld& world, Weapon& weapon, Transform& transform, Sprite& sprite, const Damage& damage,
              const Query<Enemy, Transform, Circle, Health>& enemies) {
    const auto& definition = world.Definition(weapon.kind);
    const auto owner = world.Get<Transform>(weapon.owner).position+WeaponMount(weapon);
    if (AliveEnemy(world, weapon.target)) {
        const auto delta = world.Get<Transform>(weapon.target).position - (owner + Geometry(weapon.kind).scriptOffset);
        if (glm::dot(delta, delta) > 1e-10f) weapon.angle = std::atan2(delta.y, delta.x);
        weapon.reflected = false;
    }
    transform.previous = transform.position;
    transform.position = WeaponRoot(owner, weapon.kind);
    sprite.angle = weapon.angle;
    weapon.elapsed += dt;
    const int stage = int(std::floor((weapon.elapsed + 1e-6f) / definition.segmentSeconds));
    weapon.activeSegments = stage <= 6 ? stage : std::max(0, 12 - stage);
    if (stage >= 12) {
        weapon.phase = WeaponPhase::Idle; weapon.target = ECS::EntityHandle(0); weapon.activeSegments = 0; weapon.elapsed = 0;
        return;
    }
    for (int i = 0; i < weapon.activeSegments; ++i) {
        const auto offset = Combat::Rotate(SourceBeamSegments[i].center, weapon.angle);
        HitCapsule(world, weapon, damage, enemies, transform.previous + offset, transform.position + offset,
                   weapon.angle, definition.halfLength, definition.radius);
    }
}
} // namespace

bool AliveEnemy(GameWorld& world, ECS::EntityHandle entity) {
    if (!world.scene) return false;
    const auto* enemy = world.scene->TryGetComponent<Enemy>(entity);
    return enemy && enemy->phase == EnemyPhase::Alive &&
           world.scene->TryGetComponent<Transform>(entity) && world.scene->TryGetComponent<Health>(entity);
}
void ResetWeapon(GameWorld& world, bool resetCooldowns) {
    if(world.scene && UsesArsenal(world)) {
        Query<Weapon,Transform,Sprite,Damage> query;query.Refresh(*world.scene);
        for(auto chunk:query)for(std::size_t row=0;row<chunk.count;++row) {
            const auto value=chunk.Get<Weapon>()[row];
            if(!world.scene->IsAlive(value.owner) || value.owner.GetID()!=world.player.GetID() || !ValidWeapon(value.kind))continue;
            const auto entity=chunk.Entity(row,*world.scene);
            InitializeEquipment(world,entity,value.owner,value.kind,value.tier,value.equipmentSlot,value.affix);
            world.Get<Weapon>(entity).randomState=value.randomState;
            if(!resetCooldowns) {world.Get<Weapon>(entity).cooldowns=value.cooldowns;world.Get<Weapon>(entity).cooldown=value.cooldown;}
        }
        return;
    }
    if (!world.scene || !world.scene->IsAlive(world.weapon)) return;
    auto& weapon = world.Get<Weapon>(world.weapon);
    const auto cooldowns = weapon.cooldowns;
    weapon = Weapon{};
    weapon.owner = world.player; weapon.kind = world.selected;
    if (!resetCooldowns) weapon.cooldowns = cooldowns;
    weapon.cooldown = weapon.cooldowns[WeaponIndex(world.selected)];
    const auto& geometry = Geometry(world.selected);
    weapon.angle = geometry.idleAngle; weapon.reflected = geometry.idleReflected;
    const auto& definition = world.Definition(world.selected);
    auto& transform = world.Get<Transform>(world.weapon);
    transform.position = transform.previous = WeaponRoot(world.Get<Transform>(world.player).position, world.selected);
    auto& sprite = world.Get<Sprite>(world.weapon);
    sprite.image = definition.image; sprite.size = definition.size; sprite.angle = weapon.angle; sprite.flipX = false;
}
bool EquipWeapon(GameWorld& world, WeaponKind kind) {
    if (!world.scene || world.state == State::LevelUp || world.state == State::Shop || !ValidWeapon(kind)) return false;
    if(UsesArsenal(world)) {
        const auto inventory=ExtractEquipment(world,world.player);
        for(std::size_t i=0;i<EquipmentSlots;++i)if(inventory.slots[i].present && inventory.slots[i].kind==kind)return SelectWeaponSlot(world,i);
        return false;
    }
    if (world.selected == kind) return true;
    world.selected = kind;
    // Public equipment changes occur between ticks, after command playback.
    Query<Projectile> projectiles;
    projectiles.Refresh(*world.scene);
    for (auto chunk : projectiles)
        for (std::size_t row = 0; row < chunk.count; ++row)
            world.commands->Destroy(chunk.Entity(row, *world.scene));
    world.commands->Playback();
    world.reservedProjectiles = 0;
    ResetWeapon(world, false); RefreshCounts(world);
    return true;
}
void WeaponSystem::OnTick() {
    auto& world = *GetContext()->GetService<GameWorld>();
    if (world.state != State::Playing || !world.config.armed) return;
    Query<Weapon, Transform, Sprite, Damage> weapons;
    Query<Enemy, Transform, Circle, Health> enemies;
    weapons.Refresh(*world.scene); enemies.Refresh(*world.scene);
    for (auto chunk : weapons) {
        auto* states = chunk.Get<Weapon>();
        auto* transforms = chunk.Get<Transform>();
        auto* sprites = chunk.Get<Sprite>();
        for (std::size_t row = 0; row < chunk.count; ++row) {
            auto& weapon = states[row]; auto& transform = transforms[row]; auto& sprite = sprites[row];
            if (!ValidWeapon(weapon.kind) || !world.scene->IsAlive(weapon.owner)) continue;
            // Owner/target are entity relationships, so checked random lookup is appropriate.
            if (!world.scene->TryGetComponent<Transform>(weapon.owner)) continue;
            const auto& definition = world.Definition(weapon.kind);
            const auto* stats = world.scene->TryGetComponent<CombatStats>(weapon.owner);
            for (auto& cooldown : weapon.cooldowns) cooldown = std::max(0.f, cooldown - dt * (stats ? stats->attackSpeed : 1));
            weapon.cooldown = weapon.cooldowns[WeaponIndex(weapon.kind)];
            weapon.flash = std::max(0.f, weapon.flash - dt);
            if (weapon.phase == WeaponPhase::Outbound || weapon.phase == WeaponPhase::Returning) {
                ThrustTick(world, weapon, transform, sprite, chunk.Get<Damage>()[row], enemies); continue;
            }
            if (weapon.phase == WeaponPhase::Beam) { BeamTick(world, weapon, transform, sprite, chunk.Get<Damage>()[row], enemies); continue; }
            const auto owner = world.Get<Transform>(weapon.owner).position+WeaponMount(weapon);
            const auto scriptOrigin = owner + Geometry(weapon.kind).scriptOffset;
            weapon.target = NearestEnemy(world, scriptOrigin, definition.range, enemies);
            if (AliveEnemy(world, weapon.target)) {
                const auto delta = world.Get<Transform>(weapon.target).position - scriptOrigin;
                if (glm::dot(delta, delta) > 1e-10f) weapon.angle = std::atan2(delta.y, delta.x);
                weapon.reflected = false;
            }
            transform.previous = transform.position;
            transform.position = WeaponRoot(owner, weapon.kind);
            sprite.angle = weapon.angle;
            if (!AliveEnemy(world, weapon.target) || weapon.cooldown > 1e-5f) continue;
            const std::size_t count = definition.mode == AttackMode::AuthoredBurst ? BurstPaths.size() : 1;
            if ((definition.mode == AttackMode::Projectile || definition.mode == AttackMode::AuthoredBurst) &&
                Count<Projectile>(world) + world.reservedProjectiles + count > world.config.maxProjectiles) continue;
            weapon.cooldown = weapon.cooldowns[WeaponIndex(weapon.kind)] = RankedCooldown(world,weapon);
            weapon.elapsed = 0; weapon.hitTargets.clear(); ++world.stats.shots;
            auto& damage = chunk.Get<Damage>()[row]; damage = WeaponPayload(world,weapon);
            const auto firePoint = WeaponPoint(transform.position, weapon.angle, weapon.reflected, Geometry(weapon.kind).firePoint);
            EmitEvent(world, GameEventKind::WeaponAttack, chunk.Entity(row, *world.scene),
                      definition.mode == AttackMode::Projectile || definition.mode == AttackMode::AuthoredBurst ? firePoint : transform.position, weapon.kind);
            switch (definition.mode) {
            case AttackMode::Thrust:
                weapon.phase = WeaponPhase::Outbound; ThrustTick(world, weapon, transform, sprite, damage, enemies); break;
            case AttackMode::SegmentedBeam:
                weapon.phase = WeaponPhase::Beam; weapon.activeSegments = 0; break;
            case AttackMode::Projectile:
                QueueAttackProjectile(world, firePoint, Direction(weapon.angle) * definition.speed, weapon.kind, -1, weapon.angle, damage);
                if (weapon.kind == WeaponKind::Gun) weapon.flash = .06666667f;
                break;
            case AttackMode::AuthoredBurst:
                for (std::size_t path = 0; path < BurstPaths.size(); ++path)
                    QueueAttackProjectile(world, firePoint, {}, weapon.kind, int(path), weapon.angle, damage);
                weapon.flash = .06666667f;
                break;
            }
        }
    }
}
} // namespace Brotato
