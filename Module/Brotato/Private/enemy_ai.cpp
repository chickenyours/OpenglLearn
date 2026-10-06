#include "Brotato/Systems/game_systems.h"
#include "Brotato/Public/enemy_behavior.h"
#include <cmath>

namespace Brotato {
namespace {
bool HealNearby(GameWorld& world,ECS::EntityHandle source,glm::vec2 origin,bool apply) {
    bool found=false;Query<Enemy,Health,Transform> allies;allies.Refresh(*world.scene);
    for(auto chunk:allies)for(std::size_t row=0;row<chunk.count;++row){
        const auto entity=chunk.Entity(row,*world.scene);auto& health=chunk.Get<Health>()[row];
        if(entity.GetID()==source.GetID() || world.scene->TryGetComponent<BossBrain>(entity) ||
           chunk.Get<Enemy>()[row].phase!=EnemyPhase::Alive || health.current<=0 || health.current>=health.maximum ||
           glm::distance(origin,chunk.Get<Transform>()[row].position)>5)continue;
        found=true;
        if(apply){health.current=std::min(health.maximum,health.current+std::max(1,health.maximum/4));++world.stats.healedEnemies;}
    }
    return found;
}
void Summon(GameWorld& world,ECS::EntityHandle source,glm::vec2 origin) {
    int living=0;Query<Enemy> minions;minions.Refresh(*world.scene);
    for(auto chunk:minions)for(std::size_t row=0;row<chunk.count;++row){const auto& enemy=chunk.Get<Enemy>()[row];if(enemy.summonedBy.GetID()==source.GetID() && enemy.phase!=EnemyPhase::Dying)++living;}
    if(living>4)return;
    std::array<EnemySpawn,2> pack{};
    const auto player=world.Get<Transform>(world.player).position;
    for(int i=0;i<2;++i){
        auto& spawn=pack[i];spawn.kind=EnemyKind::Fast;spawn.rewards=false;spawn.summonedBy=source;
        spawn.position=glm::clamp(origin+glm::vec2(i?1.2f:-1.2f,.8f),world.config.minimum,world.config.maximum);
        if(glm::distance(spawn.position,player)<3)return;
    }
    QueueEnemyPack(world,pack);
}
}
void EnemyAISystem::OnTick() {
    auto& world = *GetContext()->GetService<GameWorld>();
    if (world.state != State::Playing || !world.config.expanded) return;
    const auto player = world.Get<Transform>(world.player).position;
    OptionalQuery<ECS::Core::Require<Enemy, Transform, EnemyBrain>,BossBrain> query; query.Refresh(*world.scene);
    for (auto chunk : query) {
        const auto* enemies = chunk.Get<Enemy>(); const auto* transforms = chunk.Get<Transform>();
        auto* brains = chunk.Get<EnemyBrain>();
        for (std::size_t row = 0; row < chunk.count; ++row) {
            const auto& enemy = enemies[row]; auto& brain = brains[row];
            if (enemy.phase != EnemyPhase::Alive || chunk.TryGet<BossBrain>(row) || !HasEnemyAttack(enemy.kind)) continue;
            const auto& behavior = EnemyBehaviors[std::size_t(enemy.kind)];
            if (brain.action == EnemyAction::Approach) {
                brain.cooldown = std::max(0.f, brain.cooldown - float(SimulationStep));
                const auto delta = player - transforms[row].position;
                const float distance = glm::length(delta);
                if (brain.cooldown > 1e-5f || distance > behavior.range) continue;
                if(enemy.kind==EnemyKind::Healer && !HealNearby(world,chunk.Entity(row,*world.scene),transforms[row].position,false)){brain.cooldown=.25f;continue;}
                brain.direction = distance > 1e-6f ? delta / distance : glm::vec2(1,0);
                brain.action = EnemyAction::Windup; brain.remaining = behavior.windup;
            } else {
                brain.remaining -= float(SimulationStep);
                if (brain.remaining > 1e-5f) continue;
                if (brain.action == EnemyAction::Windup) {
                    if(enemy.kind==EnemyKind::Healer)HealNearby(world,chunk.Entity(row,*world.scene),transforms[row].position,true);
                    if(enemy.kind==EnemyKind::Summoner)Summon(world,chunk.Entity(row,*world.scene),transforms[row].position);
                    if (behavior.projectiles > 0) {
                        const int damage = EnemyAttackDamage(world,enemy,int(std::round(world.config.contactDamage * (enemy.kind == EnemyKind::Elite ? .8f : .6f))));
                        QueueHostileVolley(world,chunk.Entity(row,*world.scene),
                            transforms[row].position + brain.direction*.55f,brain.direction,behavior.projectiles,damage);
                    }
                    if (behavior.chargeSeconds > 0) {
                        brain.action = EnemyAction::Charging; brain.remaining = behavior.chargeSeconds; ++world.stats.charges;
                    } else { brain.action = EnemyAction::Recovering; brain.remaining = behavior.recovery; }
                } else if (brain.action == EnemyAction::Charging) {
                    brain.action = EnemyAction::Recovering; brain.remaining = behavior.recovery;
                } else {
                    brain.action = EnemyAction::Approach; brain.cooldown = behavior.cooldown;
                }
            }
        }
    }
}
} // namespace Brotato
