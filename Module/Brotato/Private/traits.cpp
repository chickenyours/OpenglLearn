#include "Brotato/Systems/game_systems.h"
#include <algorithm>
#include <cmath>

namespace Brotato {
bool UsesTraits(const GameWorld& world) { return UsesArsenal(world) && world.config.traits; }
Damage WeaponPayload(GameWorld& world,Weapon& weapon,bool rollCritical) {
    auto payload=AttackPayload(world,weapon.owner,weapon.kind);payload.amount=RankedDamage(world,weapon);
    if(!UsesTraits(world))return payload;
    payload.owner=weapon.owner;
    const auto* stats=world.scene->TryGetComponent<CombatStats>(weapon.owner);
    const auto family=Family(weapon.kind);
    const int elemental=stats && family==WeaponFamily::Elemental ? stats->families[std::size_t(family)]/2 : 0;
    const int precision=stats && family==WeaponFamily::Precision ? stats->families[std::size_t(family)]/2 : 0;
    payload.burning=std::clamp((stats?stats->burning:0)+elemental+(stats && family==WeaponFamily::Elemental?stats->elementalBurning:0)+(weapon.affix==WeaponAffix::Burning?1+std::clamp(weapon.tier,0,3)/2:0),0,20);
    payload.slow=weapon.affix==WeaponAffix::Chilling ? .65f-.05f*std::clamp(weapon.tier,0,3) : 1;
    payload.pierce=std::clamp((stats?stats->pierce:0)+(weapon.affix==WeaponAffix::Piercing?1+std::clamp(weapon.tier,0,3)/2:0),0,3);
    payload.lifeSteal=std::clamp((stats?stats->lifeSteal:0)+(weapon.affix==WeaponAffix::Vampiric?.1f:0),0.f,.5f);
    const float criticalChance=std::clamp((stats?stats->criticalChance:0)+precision*.1f,0.f,.6f);
    if(rollCritical && criticalChance>0 && BuildRandom(weapon.randomState)%10000 < std::uint32_t(std::round(criticalChance*10000))) {
        payload.critical=true;payload.amount=std::min(100000,payload.amount*2);++world.stats.criticalAttacks;
    }
    return payload;
}
namespace {
bool TargetAlive(GameWorld& world,ECS::EntityHandle target) {
    const auto* enemy=world.scene->TryGetComponent<Enemy>(target);
    const auto* hp=world.scene->TryGetComponent<Health>(target);
    return enemy && hp && enemy->phase==EnemyPhase::Alive && hp->current>0;
}
bool RefreshStatus(GameWorld& world,const TimedStatus& request,ECS::EntityHandle ignore=ECS::EntityHandle(0)) {
    Query<TimedStatus> query;query.Refresh(*world.scene);
    for(auto chunk:query)for(std::size_t row=0;row<chunk.count;++row) {
        auto& status=chunk.Get<TimedStatus>()[row];
        if(chunk.Entity(row,*world.scene).GetID()==ignore.GetID() || status.remaining<=1e-5f || status.kind!=request.kind || !world.scene->IsAlive(status.target) || status.target.GetID()!=request.target.GetID())continue;
        status.remaining=std::max(status.remaining,request.remaining);
        if(request.damage>status.damage || request.slow<status.slow){status.damage=std::max(status.damage,request.damage);status.slow=std::min(status.slow,request.slow);status.owner=request.owner;status.weapon=request.weapon;}
        return true;
    }
    return false;
}
void SubmitStatus(GameWorld& world,TimedStatus request) {
    if(RefreshStatus(world,request))return;
    if(!world.statusType || Count<TimedStatus>(world)+world.reservedStatuses>=world.config.maxStatuses)return;
    ++world.reservedStatuses;
    world.commands->Create(world.statusType,[&world,request](auto& scene,auto entity) {
        // Same-tick requests meet here. Merge after all query borrows end, then
        // retire this unused entity; compaction cannot invalidate a live borrow.
        if(!TargetAlive(world,request.target) || RefreshStatus(world,request,entity) || Count<TimedStatus>(world)>world.config.maxStatuses) {scene.DeleteEntity(entity);return;}
        world.Get<TimedStatus>(entity)=request;
    });
}
}
void QueueStatus(GameWorld& world,ECS::EntityHandle target,const Damage& payload,WeaponKind kind) {
    if(!UsesTraits(world) || !world.commands || !TargetAlive(world,target))return;
    TimedStatus request;request.target=target;request.owner=payload.owner;request.weapon=kind;
    if(payload.burning>0){request.kind=StatusKind::Burn;request.damage=std::clamp(payload.burning,1,20);SubmitStatus(world,request);}
    if(std::isfinite(payload.slow) && payload.slow>0 && payload.slow<1){request.kind=StatusKind::Slow;request.damage=0;request.remaining=1.5f;request.slow=std::clamp(payload.slow,.5f,1.f);SubmitStatus(world,request);}
}
void StatusSystem::OnTick() {
    auto& world=*GetContext()->GetService<GameWorld>();
    if(!UsesTraits(world) || world.state!=State::Playing)return;
    Query<Enemy> enemies;enemies.Refresh(*world.scene);
    for(auto chunk:enemies)for(std::size_t row=0;row<chunk.count;++row){chunk.Get<Enemy>()[row].slowMultiplier=1;chunk.Get<Enemy>()[row].burning=false;}
    Query<TimedStatus> query;query.Refresh(*world.scene);
    for(auto chunk:query)for(std::size_t row=0;row<chunk.count;++row) {
        auto& status=chunk.Get<TimedStatus>()[row];const auto entity=chunk.Entity(row,*world.scene);
        if(!TargetAlive(world,status.target) || (status.kind!=StatusKind::Burn && status.kind!=StatusKind::Slow)){world.commands->Destroy(entity);continue;}
        const float active=std::min(float(SimulationStep),std::max(0.f,status.remaining));status.remaining=std::max(0.f,status.remaining-active);
        auto& enemy=world.Get<Enemy>(status.target);
        if(active>0 && status.kind==StatusKind::Slow)enemy.slowMultiplier=std::min(enemy.slowMultiplier,status.slow);
        if(active>0 && status.kind==StatusKind::Burn) {
            enemy.burning=true;status.pulse+=active;
            while(status.pulse+1e-5f>=.5f && TargetAlive(world,status.target)) {
                status.pulse=std::max(0.f,status.pulse-.5f);
                if(ApplyDamage(world,status.target,std::clamp(status.damage,1,20),status.weapon))++world.stats.burnHits;
            }
        }
        if(status.remaining<=1e-5f || !TargetAlive(world,status.target))world.commands->Destroy(entity);
    }
}
} // namespace Brotato
