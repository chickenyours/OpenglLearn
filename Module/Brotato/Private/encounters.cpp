#include "Brotato/Systems/game_systems.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace Brotato {
namespace {
struct WaveEncounter {
    std::array<int,EnemyCount> weights;
    int packSize; double delay;
    EncounterEvent event;
};
constexpr std::array<WaveEncounter,6> Waves{{
    {{{80,20,0,0,0,0,0,0}},2,2.0,EncounterEvent::Swarm},
    {{{35,20,30,15,0,0,0,0}},2,1.5,EncounterEvent::Crossfire},
    {{{20,15,20,25,15,5,0,0}},3,1.8,EncounterEvent::Stampede},
    {{{15,15,15,15,15,5,10,10}},3,1.6,EncounterEvent::Reinforcements},
    {{{10,15,15,20,15,10,5,10}},4,1.7,EncounterEvent::Crossfire},
    {{{10,10,15,20,20,10,5,10}},4,1.5,EncounterEvent::Stampede},
}};
std::uint32_t Draw(SpawnDirector& director) {
    auto& state=director.randomState;
    if(!state)state=0x6d2b79f5u;
    state^=state<<13;state^=state>>17;state^=state<<5;return state;
}
EnemyKind Choose(SpawnDirector& director,const WaveEncounter& wave) {
    int ticket=int(Draw(director)%100);
    for(std::size_t i=0;i<EnemyCount;++i){ticket-=wave.weights[i];if(ticket<0)return EnemyKind(i);}
    return EnemyKind::Normal;
}
glm::vec2 EdgePoint(const Config& config,int side,float fraction) {
    const auto lo=config.minimum+glm::min(glm::vec2(.8f),(config.maximum-config.minimum)*.1f);
    const auto hi=config.maximum-glm::min(glm::vec2(.8f),(config.maximum-config.minimum)*.1f);
    switch(side%4){
        case 0:return {glm::mix(lo.x,hi.x,fraction),lo.y};
        case 1:return {hi.x,glm::mix(lo.y,hi.y,fraction)};
        case 2:return {glm::mix(hi.x,lo.x,fraction),hi.y};
        default:return {lo.x,glm::mix(hi.y,lo.y,fraction)};
    }
}
glm::vec2 SafeEdge(GameWorld& world,SpawnDirector& director,int side,float fraction) {
    const auto player=world.Get<Transform>(world.player).position;
    glm::vec2 best=EdgePoint(world.config,side,fraction);float farthest=glm::distance(best,player);
    if(farthest>=4)return best;
    for(int i=0;i<16;++i){
        const auto point=EdgePoint(world.config,int(Draw(director)%4),float(Draw(director)%1001)/1000.f);
        const float distance=glm::distance(point,player);
        if(distance>=4)return point;
        if(distance>farthest){best=point;farthest=distance;}
    }
    // Small custom arenas cannot guarantee four units. Pick the farthest corner.
    for(int i=0;i<4;++i){const auto point=EdgePoint(world.config,i,0);const float distance=glm::distance(point,player);if(distance>farthest){best=point;farthest=distance;}}
    return best;
}
void IssuePack(GameWorld& world,SpawnDirector& director,const WaveEncounter& wave) {
    std::array<EnemySpawn,5> pack{};
    const int side=int(Draw(director)%4);
    const float center=.15f+float(Draw(director)%701)/1000.f;
    int size=wave.packSize;
    if(director.event==EncounterEvent::Swarm)size=5;
    if(director.event==EncounterEvent::Reinforcements)size=4;
    for(int i=0;i<size;++i){
        auto& spawn=pack[i];spawn.kind=Choose(director,wave);
        int edge=side;
        const float fraction=std::clamp(center+(i-(size-1)*.5f)*.07f,.05f,.95f);
        switch(director.event){
            case EncounterEvent::Swarm:spawn.kind=i%3==0?EnemyKind::Normal:EnemyKind::Fast;break;
            case EncounterEvent::Crossfire:spawn.kind=i%2==0?EnemyKind::Ranged:EnemyKind::Armored;edge=(side+i*2)%4;break;
            case EncounterEvent::Stampede:spawn.kind=i%2==0?EnemyKind::Charger:EnemyKind::Fast;edge=(side+i)%4;break;
            case EncounterEvent::Reinforcements:spawn.kind=i==0?EnemyKind::Summoner:i==1?EnemyKind::Healer:EnemyKind::Armored;break;
            default:break;
        }
        if(director.packs==0 && director.event==EncounterEvent::None){
            if(director.wave==3 && i==0)spawn.kind=EnemyKind::Elite;
            if(director.wave>=4 && i<2)spawn.kind=i==0?EnemyKind::Healer:EnemyKind::Summoner;
        }
        spawn.position=SafeEdge(world,director,edge,fraction);
        if(director.wave>=3 && spawn.kind!=EnemyKind::Healer && spawn.kind!=EnemyKind::Summoner && Draw(director)%100<unsigned(5+director.wave*2))
            spawn.champion=Draw(director)%2?ChampionKind::Swift:ChampionKind::Bulwark;
    }
    if(QueueEnemyPack(world,std::span<const EnemySpawn>(pack.data(),size)))++director.packs;
    else ++director.blockedPacks;
}
}
bool UsesEncounters(const GameWorld& world){return world.config.expanded && world.config.encounters;}
int WaveHealthPercent(int wave) {
    const int growth=std::clamp(wave,1,20)-1;
    return 100+30*growth+10*growth*growth;
}
int EnemyAttackDamage(const GameWorld& world,const Enemy& enemy,int base) {
    return EnemyDamage(world,ScaleRuleValue(base,enemy.damagePercent));
}
void BeginEncounterWave(GameWorld& world) {
    if(!world.scene)return;
    Query<SpawnDirector> query;query.Refresh(*world.scene);
    for(auto chunk:query)for(std::size_t row=0;row<chunk.count;++row){
        auto& director=chunk.Get<SpawnDirector>()[row];director=SpawnDirector{};
        director.wave=world.stats.wave;
        director.randomState=world.config.seed^(0x85ebca6bu*std::uint32_t(world.stats.wave));
    }
}
void EncounterSystem::OnTick() {
    auto& world=*GetContext()->GetService<GameWorld>();
    if(world.state!=State::Playing || !UsesEncounters(world) || !world.config.spawning)return;
    Query<SpawnDirector> query;query.Refresh(*world.scene);
    for(auto chunk:query)for(std::size_t row=0;row<chunk.count;++row){
        auto& director=chunk.Get<SpawnDirector>()[row];
        const auto& wave=Waves[std::clamp(director.wave,1,6)-1];
        director.elapsed+=SimulationStep;
        director.eventRemaining=std::max(0.0,director.eventRemaining-SimulationStep);
        if(director.eventRemaining==0)director.event=EncounterEvent::None;
        director.eventTimer-=SimulationStep;
        if(director.eventTimer<=1e-9){
            // Alternate the wave's signature encounter with a mobile swarm.
            director.event=director.events%2==0?wave.event:EncounterEvent::Swarm;
            director.eventRemaining=4;director.eventTimer=13;++director.events;
        }
        director.packTimer-=SimulationStep;
        if(director.packTimer>1e-9)continue;
        IssuePack(world,director,wave);
        const double duration=double(world.config.waveSeconds)+double(world.config.waveIncrement)*(director.wave-1);
        const double fraction=director.elapsed/duration;
        const double pressure=fraction<.3?1.15:fraction>.75?.8:1.0;
        // Jitter changes independently from kills, loot, growth and shop streams.
        const double jitter=.9+double(Draw(director)%201)/1000;
        director.packTimer=SpawnDelay(world,wave.delay*pressure*jitter);
        // Saturation consumes this opportunity; no accumulated spawn backlog.
    }
}
EncounterSnapshot ExtractEncounter(GameWorld& world) {
    EncounterSnapshot result;
    if(!world.scene || !UsesEncounters(world))return result;
    const auto* director=world.scene->TryGetComponent<SpawnDirector>(world.player);
    if(!director)return result;
    result.wave=director->wave;result.packs=director->packs;result.events=director->events;result.event=director->event;result.seconds=director->eventRemaining;
    const double duration=double(world.config.waveSeconds)+double(world.config.waveIncrement)*(director->wave-1);
    const double fraction=director->elapsed/duration;result.phase=fraction<.3?0:fraction>.75?2:1;
    return result;
}
} // namespace Brotato
