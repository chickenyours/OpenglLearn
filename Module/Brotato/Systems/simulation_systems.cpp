#include "Brotato/Systems/game_systems.h"
#include "Brotato/Public/combat_geometry.h"
#include "Brotato/Public/enemy_behavior.h"
#include <algorithm>
#include <cmath>

namespace Brotato {
namespace {
constexpr float dt = float(SimulationStep);
bool Finite(glm::vec2 value) { return std::isfinite(value.x) && std::isfinite(value.y); }
glm::vec2 Unit(glm::vec2 value) { const float length = glm::length(value); return length > 1e-6f ? value / length : glm::vec2(0); }
float DistanceSquared(glm::vec2 a, glm::vec2 b) { const auto delta = a - b; return glm::dot(delta, delta); }
// SpawnPosition's 364 entries, including its duplicated top row. Preserve the
// authored weighting while using one wave timer instead of recursive coroutines.
glm::vec2 SourceSpawn(std::mt19937& random, const Config& config) {
    static constexpr float xs[] = {18.106762f,3.106762f,17.286762f,16.356762f,15.356762f,14.106762f,12.856762f,11.606762f,10.106762f,8.856762f,7.606762f,6.356762f,5.356762f,4.106762f};
    static constexpr float ys[] = {10.235486f,-9.764514f,-8.264514f,-7.264514f,-2.014514f,.985486f,4.735486f,7.735486f,9.485486f,8.735486f,5.985486f,6.985486f,3.485486f,1.735486f,2.485486f,-.014514f,-1.264514f,-.764514f,-3.264514f,-2.514514f,-4.764514f,-6.014514f,-4.264514f,-5.264514f,3.985486f,10.235486f};
    const auto index = random() % (14 * 26);
    return glm::clamp(glm::vec2(xs[index / 26], ys[index % 26]), config.minimum, config.maximum);
}
}

void WaveSystem::OnTick() {
    auto& world = *GetContext()->GetService<GameWorld>();
    if (world.state != State::Playing) return;
    world.stats.remaining = std::max(0.0, world.stats.remaining - SimulationStep);
    if (world.stats.remaining < 1e-9) { world.stats.remaining = 0; world.state = State::WaveComplete; return; }
    const double elapsed = double(world.config.waveSeconds) + double(world.config.waveIncrement)*(world.stats.wave-1) - world.stats.remaining;
    const double bossEntry=std::min(12.0,(double(world.config.waveSeconds)+double(world.config.waveIncrement)*(world.stats.wave-1))*.35);
    if(UsesCampaign(world) && IsBossWave(world) && world.config.spawning && (!UsesEncounters(world) || elapsed>=bossEntry))QueueBoss(world);
    Query<Enemy> query; query.Refresh(*world.scene);
    for (auto chunk : query) {
        auto* enemies = chunk.Get<Enemy>();
        for (std::size_t row = 0; row < chunk.count; ++row) {
            auto& enemy = enemies[row];
            if (enemy.phase == EnemyPhase::Alive) continue;
            enemy.remaining -= dt;
            if (enemy.remaining > 1e-5f) continue;
            const auto entity = chunk.Entity(row, *world.scene);
            if (enemy.phase == EnemyPhase::Spawning) {
                enemy.phase = EnemyPhase::Alive;
                if (world.scene->TryGetComponent<ActorAnimation>(entity))
                    ResetActorAnimation(world, entity, ActorClip::EnemyMove);
            } else {
                auto& player = world.Get<Player>(world.player); player.experience += enemy.experience;
                world.commands->Destroy(entity);
            }
        }
    }
    if (!UsesEncounters(world) && world.config.spawning && (world.spawnTimer -= SimulationStep) <= 0) {
        constexpr EnemyKind pattern[]{EnemyKind::Normal, EnemyKind::Normal, EnemyKind::Fast, EnemyKind::Normal,
            EnemyKind::Armored, EnemyKind::Ranged, EnemyKind::Normal, EnemyKind::Charger, EnemyKind::Fast, EnemyKind::Normal,
            EnemyKind::Ranged, EnemyKind::Fast, EnemyKind::Armored, EnemyKind::Charger, EnemyKind::Normal, EnemyKind::Elite};
        const auto kind = world.config.expanded ? pattern[world.spawnSequence++ % std::size(pattern)] : EnemyKind::Normal;
        QueueEnemy(world, SourceSpawn(world.random, world.config), kind);
        world.spawnTimer = SpawnDelay(world,double(world.random()%2));
    }
}

void MovementSystem::OnTick() {
    auto& world = *GetContext()->GetService<GameWorld>();
    if (world.state != State::Playing) return;
    glm::vec2 direction{world.input.horizontal, world.input.vertical};
    if (!Finite(direction)) direction = {};
    direction = Unit(glm::clamp(direction, glm::vec2(-1), glm::vec2(1)));
    OptionalQuery<ECS::Core::Require<Player, Transform, Velocity, Sprite>, CombatStats> players; players.Refresh(*world.scene);
    for (auto chunk : players) {
        auto* transforms = chunk.Get<Transform>(); auto* velocities = chunk.Get<Velocity>(); auto* sprites = chunk.Get<Sprite>();
        for (std::size_t row = 0; row < chunk.count; ++row) {
            auto& transform = transforms[row]; transform.previous = transform.position;
            const auto* stats = chunk.TryGet<CombatStats>(row);
            velocities[row].value = direction * world.config.playerSpeed * (stats ? stats->moveSpeed : 1);
            transform.position = glm::clamp(transform.position + velocities[row].value * dt, world.config.minimum, world.config.maximum);
            if (direction.x != 0) sprites[row].flipX = direction.x < 0;
        }
    }
    const auto target = world.Get<Transform>(world.player).position;
    const float enemySpeedScale=EnemySpeed(world);
    OptionalQuery<ECS::Core::Require<Enemy, Transform, Velocity, Sprite>, EnemyBrain, Knockback, BossBrain> enemies; enemies.Refresh(*world.scene);
    for (auto chunk : enemies) {
        const auto* states = chunk.Get<Enemy>(); auto* transforms = chunk.Get<Transform>();
        auto* velocities = chunk.Get<Velocity>(); auto* sprites = chunk.Get<Sprite>();
        for (std::size_t row = 0; row < chunk.count; ++row) {
            auto& transform = transforms[row]; transform.previous = transform.position;
            auto& velocity = velocities[row]; velocity.value = {};
            if (states[row].phase != EnemyPhase::Alive) continue;
            const float slow = UsesTraits(world) ? states[row].slowMultiplier : 1;
            const float speedScale = enemySpeedScale * states[row].growthSpeed;
            const float speed = world.config.enemySpeed * states[row].speed * slow * speedScale;
            const auto delta = target - transform.position; velocity.value = Unit(delta) * speed;
            const auto* brain = chunk.TryGet<EnemyBrain>(row);
            const auto& enemy = states[row];
            const auto* boss=chunk.TryGet<BossBrain>(row);
            if(world.config.expanded && boss) {
                if(boss->action==BossAction::Charging)velocity.value=boss->direction*(boss->enraged?12.f:9.f)*slow*speedScale;
                else if(boss->action!=BossAction::Approach)velocity.value={};
            } else if (world.config.expanded && brain && HasEnemyAttack(enemy.kind)) {
                if (brain->action == EnemyAction::Charging) velocity.value = brain->direction * EnemyBehaviors[std::size_t(enemy.kind)].chargeSpeed * slow * speedScale;
                else if (brain->action == EnemyAction::Windup || brain->action == EnemyAction::Recovering) velocity.value = {};
                else if (enemy.kind == EnemyKind::Ranged || enemy.kind == EnemyKind::Healer || enemy.kind == EnemyKind::Summoner) {
                    const auto distance = glm::length(delta);
                    velocity.value = distance < 3.2f ? -Unit(delta)*speed : distance > 5.2f ? Unit(delta)*speed : glm::vec2(0);
                }
            } else if (world.config.expanded) velocity.value = Unit(delta) * std::min(glm::length(delta)/dt, speed);
            if (auto* impulse = chunk.TryGet<Knockback>(row); world.config.expanded && impulse && impulse->remaining > 0) {
                velocity.value += impulse->velocity;
                impulse->remaining = std::max(0.f, impulse->remaining-dt);
                impulse->velocity *= .9f;
            }
            transform.position += world.config.expanded ? velocity.value * dt : Unit(delta) * std::min(glm::length(delta), speed * dt);
            if (world.config.expanded) transform.position = glm::clamp(transform.position,world.config.minimum,world.config.maximum);
            if (delta.x != 0) sprites[row].flipX = delta.x < 0;
        }
    }
}

void Kill(GameWorld& world, ECS::EntityHandle entity, WeaponKind weapon, int damage) {
    auto& enemy = world.Get<Enemy>(entity);
    if (enemy.phase != EnemyPhase::Alive) return;
    enemy.phase = EnemyPhase::Dying; enemy.remaining = world.config.deathDelay; enemy.touchingPlayer = false;
    if (auto* health = world.scene->TryGetComponent<Health>(entity)) health->current = 0;
    if (auto* velocity = world.scene->TryGetComponent<Velocity>(entity)) velocity->value = {};
    ++world.stats.kills;
    if(world.scene->TryGetComponent<BossBrain>(entity))++world.stats.bossKills;
    if (world.scene->TryGetComponent<ActorAnimation>(entity))
        ResetActorAnimation(world, entity, ActorClip::EnemyDeath);
    QueueHitEffects(world, entity, damage);
    const auto position = world.Get<Transform>(entity).position;
    EmitEvent(world, GameEventKind::EnemyKilled, entity, position, weapon);
    if (enemy.rewards && double(world.random()) / 4294967296.0 < world.config.dropChance) QueuePickup(world, position);
}

void ProjectileSystem::OnTick() {
    auto& world = *GetContext()->GetService<GameWorld>();
    if (world.state != State::Playing) return;
    Query<Projectile, Transform, Velocity, Sprite, Circle, Damage> projectiles; projectiles.Refresh(*world.scene);
    Query<Enemy, Transform, Circle, Health> enemies; enemies.Refresh(*world.scene);
    for (auto chunk : projectiles) {
        auto* bullets = chunk.Get<Projectile>(); auto* transforms = chunk.Get<Transform>();
        auto* velocities = chunk.Get<Velocity>(); const auto* sprites = chunk.Get<Sprite>(); const auto* circles = chunk.Get<Circle>();
        for (std::size_t row = 0; row < chunk.count; ++row) {
            const auto entity = chunk.Entity(row, *world.scene);
            auto& bullet = bullets[row]; auto& transform = transforms[row]; auto& velocity = velocities[row];
            const float activeDt = std::min(dt, std::max(0.f, bullet.remaining));
            if (activeDt <= 0 || bullet.consumed) { world.commands->Destroy(entity); continue; }
            transform.previous = transform.position; bullet.age += activeDt;
            if (bullet.path >= 0) {
                const auto& path = BurstPaths.at(bullet.path);
                float time = std::clamp(bullet.age / world.Definition(bullet.kind).animationSeconds, 0.f, 1.f);
                time = time * time * (3 - 2 * time);
                transform.position = bullet.origin + Combat::Rotate(glm::mix(path.start, path.end, time), bullet.heading);
                velocity.value = (transform.position - transform.previous) / activeDt;
            } else {
                velocity.value.y += bullet.gravity * activeDt; transform.position += velocity.value * activeDt;
            }
            bullet.remaining -= activeDt;
            std::erase_if(bullet.hitTargets,[&](auto old){return !world.scene->IsAlive(old);});
            // Repeatedly select the earliest unvisited physical impact. A fast
            // projectile may cross several enemies inside one fixed step.
            for (int impact=0;impact<4;++impact) {
            ECS::EntityHandle target(0); float first = 2;
            for (auto otherChunk : enemies) {
                const auto* states = otherChunk.Get<Enemy>(); const auto* otherTransforms = otherChunk.Get<Transform>();
                const auto* otherCircles = otherChunk.Get<Circle>();
                for (std::size_t otherRow = 0; otherRow < otherChunk.count; ++otherRow) {
                    if (states[otherRow].phase != EnemyPhase::Alive) continue;
                    const auto& other = otherTransforms[otherRow];
                    const auto otherEnd = glm::mix(other.previous, other.position, activeDt / dt);
                    const float hit = Combat::CapsuleImpact(transform.previous, transform.position, sprites[row].angle,
                        bullet.halfLength, circles[row].radius, other.previous, otherEnd, otherCircles[otherRow].radius);
                    const auto candidate = otherChunk.Entity(otherRow, *world.scene);
                    if (std::any_of(bullet.hitTargets.begin(),bullet.hitTargets.end(),[&](auto old){return old.GetID()==candidate.GetID();})) continue;
                    // Physical impact wins; equal impacts use entity identity so
                    // compaction or another matching archetype cannot reorder ties.
                    if (hit < first || (hit == first && candidate.GetID() < target.GetID())) { first = hit; target = candidate; }
                }
            }
            if (first > 1) break;
            if (ResolveHit(world,target,chunk.Get<Damage>()[row],bullet.kind,velocity.value)) {
                if (!bullet.hitTargets.empty()) ++world.stats.piercedHits;
                bullet.hitTargets.push_back(target);
            }
            if (UsesTraits(world) && bullet.pierces>0) --bullet.pierces;
            else { bullet.consumed=true; break; }
            }
            if (bullet.consumed || bullet.remaining <= 0) world.commands->Destroy(entity);
        }
    }
    Query<HostileProjectile, Transform, Velocity, Sprite, Circle, Damage> hostiles; hostiles.Refresh(*world.scene);
    const auto& playerTransform = world.Get<Transform>(world.player);
    const float playerRadius = world.Get<Circle>(world.player).radius;
    for (auto chunk : hostiles) for (std::size_t row = 0; row < chunk.count; ++row) {
        auto& bullet = chunk.Get<HostileProjectile>()[row]; auto& transform = chunk.Get<Transform>()[row];
        const auto entity = chunk.Entity(row,*world.scene);
        const float activeDt = std::min(dt,std::max(0.f,bullet.remaining));
        transform.previous = transform.position;
        transform.position += chunk.Get<Velocity>()[row].value * activeDt; bullet.remaining -= activeDt;
        const float hit = Combat::CapsuleImpact(transform.previous,transform.position,0,0,chunk.Get<Circle>()[row].radius,
            playerTransform.previous,glm::mix(playerTransform.previous,playerTransform.position,activeDt/dt),playerRadius);
        if (activeDt > 0 && hit <= 1) {
            ApplyDamage(world,world.player,chunk.Get<Damage>()[row].amount); world.commands->Destroy(entity);
        } else if (bullet.remaining <= 0) world.commands->Destroy(entity);
    }
}

void ContactSystem::OnTick() {
    auto& world = *GetContext()->GetService<GameWorld>();
    if (world.state != State::Playing) return;
    auto& player = world.Get<Health>(world.player);
    const auto position = world.Get<Transform>(world.player).position;
    const auto previous = world.Get<Transform>(world.player).previous;
    const auto playerRadius = world.Get<Circle>(world.player).radius;
    Query<Enemy, Transform, Circle, Damage> query; query.Refresh(*world.scene);
    for (auto chunk : query) {
        auto* enemies = chunk.Get<Enemy>(); const auto* transforms = chunk.Get<Transform>(); const auto* circles = chunk.Get<Circle>();
        for (std::size_t row = 0; row < chunk.count; ++row) {
            auto& enemy = enemies[row];
            if (enemy.phase != EnemyPhase::Alive) continue;
            const float radius = circles[row].radius + playerRadius;
            const bool touching = world.config.expanded ? Combat::CapsuleImpact(previous,position,0,0,playerRadius,
                transforms[row].previous,transforms[row].position,circles[row].radius) <= 1 : DistanceSquared(position, transforms[row].position) <= radius * radius;
            if (touching && (world.config.expanded || !enemy.touchingPlayer))
                ApplyDamage(world, world.player, chunk.Get<Damage>()[row].amount);
            enemy.touchingPlayer = touching;
        }
    }
    if (player.current <= 0) world.state = State::Dead;
}

void PickupSystem::OnTick() {
    auto& world = *GetContext()->GetService<GameWorld>();
    if (world.state != State::Playing) return;
    const auto position = world.Get<Transform>(world.player).position;
    const auto playerRadius = world.Get<Circle>(world.player).radius;
    auto& player = world.Get<Player>(world.player);
    const auto* combat=world.scene->TryGetComponent<CombatStats>(world.player);
    const float pickupRange=world.config.builds && combat ? combat->pickupRange : 1;
    Query<Pickup, Transform, Circle> query; query.Refresh(*world.scene);
    for (auto chunk : query) {
        auto* pickups = chunk.Get<Pickup>(); const auto* transforms = chunk.Get<Transform>(); const auto* circles = chunk.Get<Circle>();
        for (std::size_t row = 0; row < chunk.count; ++row) {
            auto& pickup = pickups[row]; const float radius = (playerRadius + circles[row].radius)*pickupRange;
            if (!pickup.collected && DistanceSquared(position, transforms[row].position) <= radius * radius) {
                const auto entity = chunk.Entity(row, *world.scene);
                pickup.collected = true;
                if(world.config.builds) player.materials=int(std::clamp<std::int64_t>(std::int64_t(player.materials)+pickup.value,0,MaxMaterials));
                else player.materials += pickup.value;
                world.commands->Destroy(entity);
                EmitEvent(world, GameEventKind::MaterialCollected, entity, transforms[row].position);
            }
        }
    }
}
} // namespace Brotato
