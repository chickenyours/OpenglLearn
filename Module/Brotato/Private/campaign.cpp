#include "Brotato/Systems/game_systems.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace Brotato {
bool UsesCampaign(const GameWorld& world){return world.config.expanded && world.config.builds && world.config.arsenal && world.config.campaign;}
bool IsBossWave(const GameWorld& world){return UsesCampaign(world) && (world.stats.wave==world.config.campaignWaves || (world.config.campaignWaves>=4 && world.stats.wave==world.config.campaignWaves/2));}
void BeginCampaignWave(GameWorld& world){
    if(auto* progress=world.scene->TryGetComponent<RunProgress>(world.player)){
        progress->objectiveWave=IsBossWave(world)?world.stats.wave:0;progress->bossSpawned=false;progress->bossDefeated=false;
    }
}
void BossAISystem::OnTick(){
    auto& world=*GetContext()->GetService<GameWorld>();
    if(world.state!=State::Playing || !world.config.expanded)return;
    const auto player=world.Get<Transform>(world.player).position;
    Query<BossBrain,Enemy,Health,Transform> query;query.Refresh(*world.scene);
    for(auto chunk:query)for(std::size_t row=0;row<chunk.count;++row){
        auto& boss=chunk.Get<BossBrain>()[row];const auto& hp=chunk.Get<Health>()[row];
        if(chunk.Get<Enemy>()[row].phase!=EnemyPhase::Alive || hp.current<=0)continue;
        boss.enraged=boss.enraged || hp.current<=hp.maximum/2;
        const auto position=chunk.Get<Transform>()[row].position;
        if(boss.action==BossAction::Approach){
            boss.cooldown=std::max(0.f,boss.cooldown-float(SimulationStep));const auto delta=player-position;const float distance=glm::length(delta);
            if(boss.cooldown>1e-5f || distance>12)continue;
            boss.direction=distance>1e-6f?delta/distance:glm::vec2(1,0);boss.attack=BossAttack(boss.sequence%3);
            boss.action=BossAction::Windup;
            boss.remaining=boss.attack==BossAttack::Fan?(boss.enraged?.45f:.65f):boss.attack==BossAttack::Ring?(boss.enraged?.6f:.85f):(boss.enraged?.55f:.8f);
            continue;
        }
        boss.remaining-=float(SimulationStep);if(boss.remaining>1e-5f)continue;
        if(boss.action==BossAction::Windup){
            ++world.stats.bossAttacks;++boss.sequence;
            if(boss.attack==BossAttack::Charge){boss.action=BossAction::Charging;boss.remaining=.55f;++world.stats.charges;}
            else {
                const int count=boss.attack==BossAttack::Ring?(boss.enraged?12:8):(boss.enraged?7:5);
                const int damage=EnemyAttackDamage(world,chunk.Get<Enemy>()[row],int(std::round(world.config.contactDamage*(boss.finalBoss?1.f:.8f))));
                QueueHostilePattern(world,chunk.Entity(row,*world.scene),position,boss.direction,count,damage,boss.attack==BossAttack::Ring);
                boss.action=BossAction::Recovering;boss.remaining=boss.enraged?.45f:.7f;
            }
        } else if(boss.action==BossAction::Charging){boss.action=BossAction::Recovering;boss.remaining=boss.enraged?.45f:.7f;}
        else {boss.action=BossAction::Approach;boss.cooldown=boss.enraged?.9f:1.5f;}
    }
}
void RunSystem::OnTick(){
    auto& world=*GetContext()->GetService<GameWorld>();
    if(!UsesCampaign(world) || world.state==State::Paused || world.state==State::Shop || world.state==State::Victory || world.state==State::Defeat)return;
    auto* progress=world.scene->TryGetComponent<RunProgress>(world.player);
    if(!progress || progress->result.outcome!=RunOutcome::None)return;
    progress->elapsedTicks=world.stats.ticks;
    if(progress->bossSpawned && !progress->bossDefeated){
        bool found=false,defeated=true;Query<BossBrain,Enemy,Health> bosses;bosses.Refresh(*world.scene);
        for(auto chunk:bosses)for(std::size_t row=0;row<chunk.count;++row){const auto& boss=chunk.Get<BossBrain>()[row];if(!boss.objective || boss.wave!=progress->objectiveWave)continue;found=true;if(chunk.Get<Enemy>()[row].phase!=EnemyPhase::Dying || chunk.Get<Health>()[row].current>0)defeated=false;}
        if(found && defeated)progress->bossDefeated=true;
    }
    RunOutcome outcome=RunOutcome::None;RunEndReason reason=RunEndReason::None;
    if(world.state==State::Dead || world.Get<Health>(world.player).current<=0){outcome=RunOutcome::Defeat;reason=RunEndReason::PlayerDied;}
    else if(world.state==State::WaveComplete){
        if(IsBossWave(world) && !progress->bossDefeated){outcome=RunOutcome::Defeat;reason=RunEndReason::BossEscaped;}
        else if(world.stats.wave>=world.config.campaignWaves)outcome=RunOutcome::Victory;
    }
    if(outcome==RunOutcome::None)return;
    const auto build=ExtractBuild(world,world.player);const auto equipment=ExtractEquipment(world,world.player);
    auto& result=progress->result;result.outcome=outcome;result.reason=reason;
    result.wave=world.stats.wave;result.kills=world.stats.kills;result.bossKills=world.stats.bossKills;
    result.level=world.Get<Player>(world.player).level;result.materials=build.materials;result.health=build.health;result.maximumHealth=build.maximumHealth;
    result.weapons=equipment.count;result.items=std::accumulate(build.items.begin(),build.items.end(),std::size_t(0));result.seconds=double(progress->elapsedTicks)*SimulationStep;
    const auto setup=ExtractRunSetup(world);result.character=setup.character;result.difficulty=setup.difficulty;
    world.state=outcome==RunOutcome::Victory?State::Victory:State::Defeat;
}
std::vector<DrawBossStatus> ExtractBossStatus(GameWorld& world){
    std::vector<DrawBossStatus> result;if(!world.scene || !world.config.expanded)return result;
    Query<BossBrain,Enemy,Health> query;query.Refresh(*world.scene);
    for(auto chunk:query)for(std::size_t row=0;row<chunk.count;++row){const auto& hp=chunk.Get<Health>()[row];const auto& boss=chunk.Get<BossBrain>()[row];if(chunk.Get<Enemy>()[row].phase!=EnemyPhase::Dying && hp.maximum>0)result.push_back({hp.current,hp.maximum,boss.enraged,boss.finalBoss});}
    return result;
}
} // namespace Brotato
