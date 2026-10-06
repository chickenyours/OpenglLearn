#include "Brotato/Public/game_module.h"
#include "Brotato/Public/expanded_gameplay.h"
#include "Brotato/Public/enemy_behavior.h"
#include "Brotato/Systems/game_systems.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
using namespace Brotato;
struct Extra:Component<Extra>{int value=42;};
void Check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
void Near(double a,double b,const char* m){Check(std::abs(a-b)<1e-4,m);}
template<class... T>auto Type(ECS::Core::Scene& s){auto d=s.CreateArchTypeDescription();(d->AddComponentArray<T>(),...);return s.CreateArchType(d,2);}
Config Quiet(){auto c=ExpandedGameplay();c.campaign=false;c.armed=c.spawning=false;c.enemySpeed=0;c.contactDamage=0;c.dropChance=0;c.playerStart={0,0};c.minimum={-15,-15};c.maximum={15,15};c.waveSeconds=600;c.deathDelay=float(SimulationStep);return c;}
void Start(GameModule& g){Check(g.Startup(),g.Error().c_str());}
void Tick(GameModule& g,int n=1){while(n-->0)g.FixedTick();}

void ProgressiveWaves(){
    auto c=Quiet();c.spawning=true;c.enemySpeed=2.3f;c.waveSeconds=12;c.waveIncrement=0;
    GameModule g(c);Start(g);std::array<int,EnemyCount> old{};
    for(int wave=1;wave<=6;++wave){
        for(int tick=0;tick<1438;++tick)g.FixedTick();
        const auto spawn=g.Stats().spawned;const auto& director=g.Get<SpawnDirector>(g.PlayerEntity());
        Check(director.wave==wave && director.packs>=5 && director.events==1,"wave has an ECS schedule and signature event");
        if(wave==1)Check(spawn[2]==0&&spawn[3]==0&&spawn[4]==0&&spawn[5]==0&&spawn[6]==0&&spawn[7]==0,"opening only introduces basics and runners");
        if(wave==2)Check(spawn[2]>0&&spawn[3]>0&&spawn[4]==0&&spawn[6]==0,"second wave introduces shields and crossfire");
        if(wave==3)Check(spawn[4]>0&&spawn[5]>0&&spawn[6]==0&&spawn[7]==0,"third wave introduces chargers and elites");
        if(wave>=4)Check(spawn[6]>old[6]&&spawn[7]>old[7],"support enemies participate in late automatic waves");
        std::cout<<"[wave "<<wave<<"] packs="<<director.packs<<" spawned=";for(std::size_t i=0;i<EnemyCount;++i)std::cout<<spawn[i]-old[i]<<',';std::cout<<" champions="<<g.Stats().championSpawns<<'\n';
        old=spawn;Tick(g,3);Check(g.GetState()==State::Shop,"ordinary wave ends at shop");g.NextWave();
        Check(g.Get<SpawnDirector>(g.PlayerEntity()).elapsed==0&&g.Get<SpawnDirector>(g.PlayerEntity()).event==EncounterEvent::None,"next wave resets schedule without carryover events");
    }
    Check(std::all_of(old.begin(),old.end(),[](int n){return n>0;}),"automatic campaign introduces all eight enemy kinds");
    Check(g.Stats().summonedEnemies>0&&g.Stats().championSpawns>0,"summons and champions appear without injection");
}
void GrowthSnapshotsAndChampions(){
    auto c=Quiet();c.waveSeconds=.05f;c.waveIncrement=0;c.contactDamage=10;
    GameModule g(c);Start(g);
    for(int wave=1;wave<=6;++wave){
        auto normal=g.SpawnEnemy({8,0},true);
        Check(g.Get<Health>(normal).maximum==ScaleRuleValue(2,WaveHealthPercent(wave)),"HP grows from the real wave number");
        Check(g.Get<Damage>(normal).amount==ScaleRuleValue(10,100+8*(wave-1)),"contact damage grows with the wave");
        Check(g.Get<Enemy>(normal).birthWave==wave,"spawn stores its birth wave");
        auto swift=g.SpawnEnemy({8,4},true,EnemyKind::Fast,ChampionKind::Swift);
        auto bulwark=g.SpawnEnemy({8,-4},true,EnemyKind::Armored,ChampionKind::Bulwark);
        Check(g.Get<Enemy>(swift).growthSpeed>g.Get<Enemy>(normal).growthSpeed&&g.Get<Health>(bulwark).maximum>g.Get<Health>(normal).maximum,"champions alter speed and durability");
        Check(g.ExtractEnemyStatus()[1].champion==ChampionKind::Swift,"champion identity reaches render snapshots");
        Tick(g,7);g.NextWave();
    }
    g.Restart();Check(g.Stats().championSpawns==0&&g.Get<SpawnDirector>(g.PlayerEntity()).wave==1,"restart resets difficulty schedule and champion counters");
    auto enemy=g.SpawnEnemy({5,0},true,EnemyKind::Charger,ChampionKind::Swift);
    g.Get<EnemyBrain>(enemy).cooldown=0;Tick(g);
    const auto cue=g.ExtractCombatCues()[0];Near(glm::distance(cue.position,cue.end),10*.55*1.3,"dash warning accounts for champion speed");
    const auto power=g.Get<Enemy>(enemy).damagePercent;Tick(g,2);Check(g.Get<Enemy>(enemy).damagePercent==power,"power is a stable spawn snapshot");
}
void HealerPriorityAndCancellation(){
    auto c=Quiet();GameModule g(c);Start(g);
    auto healer=g.SpawnEnemy({5,0},true,EnemyKind::Healer);g.Get<EnemyBrain>(healer).cooldown=0;
    Tick(g);Check(g.Get<EnemyBrain>(healer).action==EnemyAction::Approach,"no empty healing windups");
    auto ally=g.SpawnEnemy({5,2},true,EnemyKind::Armored);g.Get<Health>(ally).current=1;
    auto distantAlly=g.SpawnEnemy({-10,0},true,EnemyKind::Armored);g.Get<Health>(distantAlly).current=1;
    auto boss=g.SpawnBoss({5,3},true);g.Get<Health>(boss).current=1;
    g.Get<EnemyBrain>(healer).cooldown=0;Tick(g);Check(g.ExtractCombatCues()[0].kind==CombatCueKind::Heal,"healing range is telegraphed");
    Tick(g,91);Check(g.Get<Health>(ally).current==2&&g.Get<Health>(distantAlly).current==1&&g.Get<Health>(boss).current==1,"pulse heals only living nearby regular allies");
    Check(g.Stats().healedEnemies==1,"only actual restores are counted");
    auto& brain=g.Get<EnemyBrain>(healer);brain.action=EnemyAction::Windup;brain.remaining=.4f;
    auto bullet=g.SpawnProjectile({5,0},{});g.Get<Damage>(bullet).amount=10000;Tick(g,60);
    Check(g.Stats().healedEnemies==1&&!g.Scene()->IsAlive(healer),"killing healer cancels pending support pulse");
}
void SummonerCapsRewardsAndGenerations(){
    auto c=Quiet();c.dropChance=1;c.initialWeapon=WeaponKind::Gun;GameModule g(c);Start(g);
    auto summoner=g.SpawnEnemy({6,0},true,EnemyKind::Summoner);
    for(int burst=0;burst<5;++burst){auto& brain=g.Get<EnemyBrain>(summoner);brain.action=EnemyAction::Windup;brain.remaining=float(SimulationStep);Tick(g);}
    Check(g.Stats().summonedEnemies==6,"one summoner is limited to six live children");
    Query<Enemy> q;q.Refresh(*g.Scene());std::vector<ECS::EntityHandle> children;
    for(auto chunk:q)for(std::size_t row=0;row<chunk.count;++row){const auto& enemy=chunk.Get<Enemy>()[row];if(enemy.summonedBy.GetID()==summoner.GetID()){Check(!enemy.rewards&&enemy.experience==0&&enemy.phase==EnemyPhase::Spawning,"children are warning entities with no farm rewards");children.push_back(chunk.Entity(row,*g.Scene()));}}
    Tick(g,185);auto target=children[0];auto bullet=g.SpawnProjectile(g.Get<Transform>(target).position,{});g.Get<Damage>(bullet).amount=10000;Tick(g,2);
    Check(g.Get<Player>(g.PlayerEntity()).experience==0&&g.Stats().pickups==0,"summoned kill cannot farm XP or materials");
    g.Scene()->DeleteEntity(summoner);auto replacement=g.SpawnEnemy({6,0},true,EnemyKind::Summoner);
    Check(replacement.GetID()!=summoner.GetID(),"replacement has a distinct generation");
    g.Get<EnemyBrain>(replacement).action=EnemyAction::Windup;g.Get<EnemyBrain>(replacement).remaining=float(SimulationStep);Tick(g);
    Check(g.Stats().summonedEnemies==8,"old children do not fill a new summoner's generation quota");
}
void SafeBirthAndIndependentStreams(){
    auto c=Quiet();c.spawning=true;c.playerStart={14,14};GameModule a(c),b(c);Start(a);Start(b);
    a.SpawnPickup({0,0});a.Get<Shop>(a.PlayerEntity()).randomState=1;a.Get<Growth>(a.PlayerEntity()).randomState=2;
    for(int tick=0;tick<1200;++tick){Tick(a);Tick(b);
        Query<Enemy,Transform> q;q.Refresh(*a.Scene());for(auto chunk:q)for(std::size_t row=0;row<chunk.count;++row)if(chunk.Get<Enemy>()[row].phase==EnemyPhase::Spawning)Check(glm::distance(chunk.Get<Transform>()[row].position,c.playerStart)>=4,"warnings never appear under the player");
    }
    const auto& da=a.Get<SpawnDirector>(a.PlayerEntity());const auto& db=b.Get<SpawnDirector>(b.PlayerEntity());
    Check(da.randomState==db.randomState&&da.packs==db.packs&&a.Stats().spawned==b.Stats().spawned,"loot/shop/growth cannot change spawn schedule");
    a.Restart();Tick(a,1200);Check(a.Stats().spawned==b.Stats().spawned,"restart reproduces the same seed");
}
void FreezeAndBacklog(){
    auto c=Quiet();c.spawning=true;c.maxEnemies=1;GameModule g(c);Start(g);Tick(g,1200);
    auto snapshot=g.Get<SpawnDirector>(g.PlayerEntity());Check(g.Stats().enemies==0&&snapshot.blockedPacks>0,"full packs cannot partially overflow tiny capacity");
    g.SetPaused(true);Tick(g,120);Check(g.Get<SpawnDirector>(g.PlayerEntity()).elapsed==snapshot.elapsed,"pause freezes encounter schedule");g.SetPaused(false);
    g.Get<Player>(g.PlayerEntity()).experience=8;Tick(g);snapshot=g.Get<SpawnDirector>(g.PlayerEntity());Tick(g,120);Check(g.GetState()==State::LevelUp&&g.Get<SpawnDirector>(g.PlayerEntity()).elapsed==snapshot.elapsed,"levelup freezes event and pack timers");
    Check(g.SelectUpgrade(g.Get<Growth>(g.PlayerEntity()).choices[0]),"resume upgrade");Tick(g);Check(g.Stats().enemies==0&&g.Get<SpawnDirector>(g.PlayerEntity()).packs==0,"missed spawn opportunities never form a catchup burst");
}
void BossEntryGrowthAndBoundarySafety(){
    auto c=Quiet();c.campaign=true;c.spawning=true;c.playerStart={14,14};c.waveSeconds=12;c.waveIncrement=0;
    GameModule g(c);Start(g);
    for(int wave=1;wave<=2;++wave){Tick(g,1441);Check(g.GetState()==State::Shop,"setup reaches real wave boundary");g.NextWave();}
    Tick(g,480);Check(g.Stats().bossSpawns==0,"objective does not appear before its wave entry");
    const auto remaining=g.Stats().remaining;g.SetPaused(true);Tick(g,120);Near(g.Stats().remaining,remaining,"pause also freezes boss entry deadline");g.SetPaused(false);
    Tick(g,25);Check(g.Stats().bossSpawns==1,"objective enters at 35 percent of the wave");
    Query<BossBrain,Enemy,Transform,Health> q;q.Refresh(*g.Scene());
    for(auto chunk:q)for(std::size_t row=0;row<chunk.count;++row){
        Check(chunk.Get<Health>()[row].maximum==105&&chunk.Get<Enemy>()[row].phase==EnemyPhase::Spawning,"objective receives growth and birth warning");
        Check(glm::distance(chunk.Get<Transform>()[row].position,c.playerStart)>=4,"boss entry stays safe at arena boundary");
    }
}
void StandaloneForeignAndReservations(){
    GameWorld w(Quiet());w.config.spawning=true;w.config.maxEnemies=4;w.scene=std::make_unique<ECS::Core::Scene>();w.commands.emplace(*w.scene);
    auto players=Type<Transform>(*w.scene);w.player=w.scene->CreateEntity(players);
    auto directors=Type<Extra,SpawnDirector>(*w.scene);auto director=w.scene->CreateEntity(directors);
    w.enemyType=Type<Enemy,Transform,Velocity,Sprite,ActorAnimation,Health,Damage,Circle,EnemyBrain>(*w.scene);
    w.stats.wave=4;BeginEncounterWave(w);w.Get<SpawnDirector>(director).packTimer=0;
    ECS::System::Context context;context.scene=w.scene.get();context.SetService(&w);ECS::System::Pipeline pipeline;pipeline.Add<EncounterSystem>();
    Check(pipeline.Start(context)&&pipeline.Tick(context),"independent ECS director runs without GameModule");
    Check(Count<Enemy>(w)==0&&w.reservedEnemies==3,"creation deferred past query borrowing");w.commands->Playback();
    Check(Count<Enemy>(w)==3&&w.Get<Extra>(director).value==42,"foreign director archetype works with unrelated data intact");
    w.reservedEnemies=0;std::array<EnemySpawn,2> pack{{{{8,0},EnemyKind::Fast},{{8,1},EnemyKind::Fast}}};
    Check(!QueueEnemyPack(w,pack),"pack capacity includes live external entities");
    Query<Enemy> q;q.Refresh(*w.scene);auto retired=q.begin().operator*().Entity(0,*w.scene);w.scene->DeleteEntity(retired);
    Check(QueueEnemyPack(w,pack)&&!QueueEnemyPack(w,pack),"reservations prevent overlapping deferred producers");w.commands->Playback();Check(Count<Enemy>(w)==4,"reserved pack commits within capacity");pipeline.Stop(context);
}
void SourceCompatibility(){
    Config c;c.encounters=true;c.spawning=true;c.armed=false;c.enemySpeed=0;c.contactDamage=0;GameModule g(c);Start(g);Tick(g,2399);
    Check(g.Get<SpawnDirector>(g.PlayerEntity()).elapsed==0&&g.Stats().championSpawns==0&&g.Stats().summonedEnemies==0,"source rules never execute expansion director");
    Check(g.Stats().spawned[0]>0&&std::all_of(g.Stats().spawned.begin()+1,g.Stats().spawned.end(),[](int n){return n==0;}),"source retains its single NPC");
    auto enemy=g.SpawnEnemy({8,0},true);Check(g.Get<Health>(enemy).maximum==1,"source one hit HP unchanged");
}
}
int main(){try{RegisterComponents();REGISTER_COMPONENT("brotato_encounter_extra",Extra);const std::pair<const char*,std::function<void()>> tests[]{{"automatic progression across six waves",ProgressiveWaves},{"wave growth and champion snapshots",GrowthSnapshotsAndChampions},{"healer priority range and death",HealerPriorityAndCancellation},{"summoner limits rewards and generations",SummonerCapsRewardsAndGenerations},{"safe birth and independent streams",SafeBirthAndIndependentStreams},{"freeze saturation and no backlog",FreezeAndBacklog},{"boss schedule growth and safe entry",BossEntryGrowthAndBoundarySafety},{"standalone foreign archetypes and reservations",StandaloneForeignAndReservations},{"source compatibility",SourceCompatibility}};for(const auto& [name,test]:tests){test();std::cout<<"[PASS] "<<name<<std::endl;}std::cout<<"9 encounter groups passed\n";return 0;}catch(const std::exception& e){std::cerr<<"[FAIL] "<<e.what()<<'\n';return 1;}}
