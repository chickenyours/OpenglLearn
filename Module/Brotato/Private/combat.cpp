#include "Brotato/Public/game_world.h"
#include "Brotato/Systems/game_systems.h"
#include <algorithm>
#include <cmath>

namespace Brotato {
int AttackDamage(GameWorld& world, ECS::EntityHandle owner, WeaponKind kind) {
    const auto* stats = world.scene->TryGetComponent<CombatStats>(owner);
    const auto bonus=stats?std::int64_t(stats->bonusDamage)+stats->weaponDamage[WeaponIndex(kind)]:0;
    return int(std::clamp<std::int64_t>(std::int64_t(world.Definition(kind).damage)+bonus,1,100000));
}
Damage AttackPayload(GameWorld& world, ECS::EntityHandle owner, WeaponKind kind) {
    Damage damage; damage.amount = AttackDamage(world,owner,kind);
    if (world.config.expanded) {
        damage.knockback = world.Definition(kind).knockback;
        damage.splashRadius = world.Definition(kind).splashRadius;
    }
    return damage;
}

bool ApplyDamage(GameWorld& world, ECS::EntityHandle entity, int amount, WeaponKind kind) {
    if (!world.scene || amount <= 0) return false;
    auto* health = world.scene->TryGetComponent<Health>(entity);
    if (!health || health->current <= 0 || health->invulnerable > 0) return false;
    auto* enemy = world.scene->TryGetComponent<Enemy>(entity);
    if (enemy && enemy->phase != EnemyPhase::Alive) return false;
    if(world.config.builds && !enemy && world.scene->TryGetComponent<Player>(entity))
        if(const auto* stats=world.scene->TryGetComponent<CombatStats>(entity)) amount=std::max(1,amount-std::clamp(stats->armor,0,20));
    const int actual = std::min(health->current, amount);
    health->current -= actual;
    health->flash = .10f;
    if (enemy) {
        ++world.stats.hits;
        if (health->current == 0) Kill(world, entity, kind, actual);
        else if (world.config.expanded) QueueHitEffects(world, entity, actual);
    } else if (world.scene->TryGetComponent<Player>(entity)) {
        if (world.config.expanded) health->invulnerable = .75f;
        EmitEvent(world, GameEventKind::PlayerHurt, entity, world.Get<Transform>(entity).position);
        if (health->current == 0) world.state = State::Dead;
    }
    return true;
}

namespace {
void Push(GameWorld& world, ECS::EntityHandle entity, float strength, glm::vec2 direction) {
    if (!world.config.expanded || strength <= 0 || glm::length(direction) < 1e-6f) return;
    auto* impulse = world.scene->TryGetComponent<Knockback>(entity);
    const auto* enemy = world.scene->TryGetComponent<Enemy>(entity);
    if (!impulse || !enemy || enemy->phase != EnemyPhase::Alive) return;
    const float resistance = world.scene->TryGetComponent<BossBrain>(entity) ? .1f : enemy->kind == EnemyKind::Elite ? .2f : enemy->kind == EnemyKind::Armored ? .35f : 1;
    impulse->velocity = glm::normalize(direction) * strength * resistance; impulse->remaining = .12f;
    if (auto* brain = world.scene->TryGetComponent<EnemyBrain>(entity); brain && brain->action == EnemyAction::Windup && enemy->kind != EnemyKind::Elite) {
        brain->action = EnemyAction::Approach; brain->cooldown = .6f; brain->remaining = 0;
    }
}
}
bool ResolveHit(GameWorld& world, ECS::EntityHandle target, const Damage& payload, WeaponKind kind, glm::vec2 direction) {
    // Structural changes stay deferred; copy impact values before any nested query.
    const auto damage = payload;
    const auto* transform = world.scene->TryGetComponent<Transform>(target);
    if (!transform) return false;
    const auto position = transform->position;
    const auto* hp=world.scene->TryGetComponent<Health>(target);
    const int actual=hp?std::min(hp->current,damage.amount):0;
    if (!ApplyDamage(world,target,damage.amount,kind)) return false;
    if(UsesTraits(world)) {
        QueueStatus(world,target,damage,kind);
        auto* ownerHealth=world.scene->TryGetComponent<Health>(damage.owner);
        auto* base=world.scene->TryGetComponent<BuildBase>(damage.owner);
        if(ownerHealth && base && ownerHealth->current>0 && ownerHealth->current<ownerHealth->maximum && std::isfinite(damage.lifeSteal) && damage.lifeSteal>0) {
            base->lifeStealCredit+=actual*std::clamp(damage.lifeSteal,0.f,.5f);
            const int whole=int(std::floor(base->lifeStealCredit+1e-5f));
            if(whole>0){const int healed=std::min(whole,ownerHealth->maximum-ownerHealth->current);ownerHealth->current+=healed;world.stats.stolenHealth+=healed;base->lifeStealCredit=std::max(0.f,base->lifeStealCredit-whole);}
            if(ownerHealth->current==ownerHealth->maximum)base->lifeStealCredit=0;
        }
    }
    Push(world,target,damage.knockback,direction);
    if (!world.config.expanded || damage.splashRadius <= 0) return true;
    QueueBlastEffect(world,position,damage.splashRadius);
    Query<Enemy, Transform, Circle, Health> enemies; enemies.Refresh(*world.scene);
    for (auto chunk : enemies) for (std::size_t row = 0; row < chunk.count; ++row) {
        const auto candidate = chunk.Entity(row,*world.scene);
        if (candidate.GetID() == target.GetID() || chunk.Get<Enemy>()[row].phase != EnemyPhase::Alive) continue;
        const auto delta = chunk.Get<Transform>()[row].position - position;
        const float radius = damage.splashRadius + chunk.Get<Circle>()[row].radius;
        if (glm::dot(delta,delta) <= radius*radius && ApplyDamage(world,candidate,damage.amount,kind)) {
            ++world.stats.splashHits; Push(world,candidate,damage.knockback,delta);
        }
    }
    return true;
}

void HealthSystem::OnTick() {
    auto& world = *GetContext()->GetService<GameWorld>();
    if (world.state != State::Playing) return;
    Query<Health> query; query.Refresh(*world.scene);
    for (auto chunk : query) for (std::size_t row = 0; row < chunk.count; ++row) {
        auto& health = chunk.Get<Health>()[row];
        health.flash = std::max(0.f, health.flash - float(SimulationStep));
        health.invulnerable = std::max(0.f, health.invulnerable - float(SimulationStep));
    }
}

void GrowthSystem::OnTick() {
    auto& world = *GetContext()->GetService<GameWorld>();
    if (world.state != State::Playing && !(world.state == State::Dead && !world.config.expanded)) return;
    Query<Player, Growth, CombatStats> query; query.Refresh(*world.scene);
    for (auto chunk : query) for (std::size_t row = 0; row < chunk.count; ++row) {
        auto& player = chunk.Get<Player>()[row]; auto& growth = chunk.Get<Growth>()[row];
        const bool needsChoices=growth.pending==0;
        while (player.experience >= ExperienceThreshold(player.level, world.config.expanded)) {
            player.experience -= ExperienceThreshold(player.level, world.config.expanded);
            ++player.level;
            if (world.config.expanded) ++growth.pending;
        }
        if(needsChoices && growth.pending>0) PrepareGrowthChoices(world,growth);
        if (growth.pending > 0) world.state = State::LevelUp;
    }
}

bool SelectUpgrade(GameWorld& world, UpgradeKind kind) {
    const auto index = static_cast<std::size_t>(kind);
    if (!world.scene || world.state != State::LevelUp || index >= UpgradeCount || (!world.config.builds && index>=3)) return false;
    auto& growth = world.Get<Growth>(world.player);
    if (growth.pending <= 0) return false;
    if(world.config.builds && std::find(growth.choices.begin(),growth.choices.end(),kind)==growth.choices.end()) return false;
    auto& stats = world.Get<CombatStats>(world.player);
    switch (kind) {
    case UpgradeKind::Damage: stats.bonusDamage = std::min(10000, stats.bonusDamage + 1); break;
    case UpgradeKind::AttackSpeed: stats.attackSpeed = std::min(5.f, stats.attackSpeed + .2f); break;
    case UpgradeKind::MoveSpeed: stats.moveSpeed = std::min(3.f, stats.moveSpeed + .15f); break;
    case UpgradeKind::MaxHealth: case UpgradeKind::Armor: case UpgradeKind::PickupRange: break;
    default: return false;
    }
    growth.upgrades[index]=std::min(100000,growth.upgrades[index]+1); --growth.pending;
    RecomputeBuild(world,world.player);
    if(growth.pending>0) PrepareGrowthChoices(world,growth);
    if (growth.pending == 0) world.state = State::Playing;
    return true;
}
} // namespace Brotato
