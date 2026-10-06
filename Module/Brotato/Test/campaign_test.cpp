#include "Brotato/Public/game_module.h"
#include "Brotato/Public/expanded_gameplay.h"
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
void Near(float a,float b,const char* m){Check(std::abs(a-b)<1e-4f,m);}
template<class... T>auto Type(ECS::Core::Scene& s){auto d=s.CreateArchTypeDescription();(d->AddComponentArray<T>(),...);return s.CreateArchType(d,2);}
Config Quiet(){auto c=ExpandedGameplay();c.encounters=false; // Historical mechanisms are tested separately from encounters.
c.playerStart={0,0};c.minimum={-30,-30};c.maximum={30,30};c.spawning=c.armed=false;c.enemySpeed=0;c.contactDamage=0;c.dropChance=0;c.spawnWarning=0;c.deathDelay=.025f;c.waveSeconds=600;c.waveIncrement=0;c.maxEffects=0;return c;}
void Start(GameModule& g){if(!g.Startup())throw std::runtime_error(g.Error());}
void Tick(GameModule& g,int n=1){for(int i=0;i<n;++i){if(g.GetState()==State::LevelUp)Check(g.SelectUpgrade(g.Get<Growth>(g.PlayerEntity()).choices[0]),"fixture offered upgrade");else g.FixedTick();}}
ECS::EntityHandle Objective(GameModule& g){Query<BossBrain> q;q.Refresh(*g.Scene());for(auto c:q)for(std::size_t i=0;i<c.count;++i)if(c.Get<BossBrain>()[i].objective)return c.Entity(i,*g.Scene());return ECS::EntityHandle(0);}
void KillBoss(GameModule& g){auto boss=Objective(g);Check(g.Scene()->IsAlive(boss),"objective present");auto b=g.SpawnProjectile(g.Get<Transform>(boss).position,{});g.Get<Damage>(b).amount=100000;Tick(g);Check(g.Get<Enemy>(boss).phase==EnemyPhase::Dying,"ordinary projectile kills objective");}
void ToShop(GameModule& g){for(int i=0;i<200&&(g.GetState()==State::Playing||g.GetState()==State::LevelUp);++i)Tick(g);Check(g.GetState()==State::Shop,"ordinary wave opens shop");}
void ObjectiveLifecycleAndFreeze(){
    auto c=Quiet();c.spawning=true;c.waveSeconds=.5f;GameModule g(c);Start(g);g.Get<Health>(g.PlayerEntity()).current=50;
    ToShop(g);Check(g.Stats().bossSpawns==0&&g.Get<Shop>(g.PlayerEntity()).restoredHealth==38&&g.ExtractBuild().health==88,"normal first wave restores quarter max health");
    g.NextWave();ToShop(g);Check(g.Stats().bossSpawns==0,"second wave no boss");g.NextWave();Tick(g);
    auto boss=Objective(g);Check(g.Scene()->IsAlive(boss)&&g.Get<RunProgress>(g.PlayerEntity()).bossSpawned&&g.Get<Health>(boss).maximum==70,"third wave spawns one checkpoint objective");
    auto remaining=g.Stats().remaining;auto cooldown=g.Get<BossBrain>(boss).cooldown;g.SetPaused(true);g.Advance(.25);g.FixedTick();Near(float(g.Stats().remaining),float(remaining),"pause freezes deadline");Near(g.Get<BossBrain>(boss).cooldown,cooldown,"pause freezes boss");g.SetPaused(false);
    g.Get<Player>(g.PlayerEntity()).experience=ExperienceThreshold(g.Get<Player>(g.PlayerEntity()).level,true);g.FixedTick();Check(g.GetState()==State::LevelUp,"ordinary growth interrupts boss wave");remaining=g.Stats().remaining;cooldown=g.Get<BossBrain>(boss).cooldown;g.Advance(.25);Near(float(g.Stats().remaining),float(remaining),"upgrade freezes boss deadline");Near(g.Get<BossBrain>(boss).cooldown,cooldown,"upgrade freezes boss state");Tick(g);KillBoss(g);
    ToShop(g);Check(g.Stats().bossKills==1&&g.Get<RunProgress>(g.PlayerEntity()).bossDefeated&&g.Stats().enemies==0,"checkpoint requires actual kill and clears transients");g.NextWave();Check(g.Get<RunProgress>(g.PlayerEntity()).objectiveWave==0&&!g.Get<RunProgress>(g.PlayerEntity()).bossSpawned,"next wave resets objective flags");
}
void PatternsAndLockedCharge(){
    auto c=Quiet();c.campaign=false;GameModule g(c);Start(g);auto boss=g.SpawnBoss({4,0},true);g.Get<BossBrain>(boss).cooldown=0;Tick(g,79);
    Check(g.Stats().enemyVolleys==1&&g.Stats().hostileProjectiles==5&&g.Stats().bossAttacks==1,"fan is five projectiles with no duplicate elite AI");
    auto& brain=g.Get<BossBrain>(boss);brain.action=BossAction::Approach;brain.sequence=1;brain.cooldown=0;Tick(g,103);Check(g.Stats().enemyVolleys==2&&g.Stats().hostileProjectiles>=8&&g.Stats().hostileProjectiles<=13,"ring creates eight more projectiles");
    glm::vec2 sum{};int radial=0;Query<HostileProjectile,Velocity> q;q.Refresh(*g.Scene());for(auto chunk:q)for(std::size_t i=0;i<chunk.count;++i){auto v=chunk.Get<Velocity>()[i].value;if(std::abs(glm::length(v)-5.5f)<1e-4f){sum+=v;++radial;}}
    Check(radial==8&&glm::length(sum)<1e-4f,"ring distributes directions around full circle");
    brain.action=BossAction::Approach;brain.sequence=2;brain.cooldown=0;Tick(g);const auto locked=brain.direction;g.Get<Transform>(g.PlayerEntity()).position={0,20};Tick(g,96);
    Check(brain.action==BossAction::Charging&&brain.direction==locked&&g.Stats().charges==1,"charge retains telegraphed direction");Near(glm::length(g.Get<Velocity>(boss).value),9,"phase one charge speed");
    Tick(g,70);Check(brain.action==BossAction::Recovering,"charge ends in recovery");
    c.maxHostileProjectiles=4;GameModule cap(c);Start(cap);boss=cap.SpawnBoss({4,0},true);cap.Get<BossBrain>(boss).cooldown=0;Tick(cap,79);Check(cap.Stats().enemyVolleys==0&&cap.Stats().hostileProjectiles==0&&cap.Get<BossBrain>(boss).action==BossAction::Recovering,"insufficient capacity drops complete fan and recovers");
}
void EnrageAndStatusInteraction(){
    auto c=Quiet();c.campaign=false;GameModule g(c);Start(g);auto boss=g.SpawnBoss({4,0},true,true);g.Get<Health>(boss).current=90;g.Get<BossBrain>(boss).cooldown=0;Tick(g);Check(g.Get<BossBrain>(boss).enraged&&g.ExtractBossStatus()[0].enraged,"half HP enters rage and HUD");g.Get<Health>(boss).current=180;Tick(g);Check(g.Get<BossBrain>(boss).enraged,"rage cannot be undone by a heal");
    g.Get<BossBrain>(boss).action=BossAction::Approach;g.Get<BossBrain>(boss).sequence=1;g.Get<BossBrain>(boss).cooldown=0;Tick(g,73);Check(g.Stats().hostileProjectiles==12,"enraged ring has twelve bullets");
    auto d=g.Scene()->CreateArchTypeDescription();d->AddComponentArray<TimedStatus>();auto type=g.Scene()->CreateArchType(d,2);auto slow=g.Scene()->CreateEntity(type);auto& status=g.Get<TimedStatus>(slow);status.target=boss;status.kind=StatusKind::Slow;status.remaining=2;status.slow=.5f;
    g.Get<BossBrain>(boss).action=BossAction::Approach;g.Get<BossBrain>(boss).sequence=2;g.Get<BossBrain>(boss).cooldown=0;Tick(g,67);Check(g.Get<BossBrain>(boss).action==BossAction::Charging,"enraged charge starts after shorter windup");Near(glm::length(g.Get<Velocity>(boss).value),6,"real ECS slow reduces enraged charge speed");
    g.Restart();Check(g.ExtractBossStatus().empty()&&g.Stats().statuses==0,"restart clears boss and status archetypes");
}
void CapacityRetryAndDeletedObjective(){
    auto c=Quiet();c.spawning=true;c.campaignWaves=1;c.waveSeconds=.5f;c.maxEnemies=1;GameModule g(c);Start(g);auto blocker=g.SpawnEnemy({10,0},true);Tick(g,3);Check(g.Stats().bossSpawns==0&&!g.Get<RunProgress>(g.PlayerEntity()).bossSpawned,"full capacity does not mark nonexistent boss spawned");g.Scene()->DeleteEntity(blocker);Tick(g);auto boss=Objective(g);Check(g.Scene()->IsAlive(boss)&&g.Stats().bossSpawns==1&&g.Stats().enemies==1,"boss retries and wins freed slot before normal spawn");Tick(g,3);Check(g.Stats().bossSpawns==1,"objective only created once");
    g.Scene()->DeleteEntity(boss);auto replacement=g.SpawnEnemy({10,0},true);Check(!g.Scene()->IsAlive(boss)&&g.Scene()->IsAlive(replacement),"retired objective stays dead");
    auto decorations=Type<Transform>(*g.Scene());bool recycled=replacement.GetID()==boss.GetID();for(int i=0;i<16&&!recycled;++i){auto e=g.Scene()->CreateEntity(decorations);recycled=e.GetID()==boss.GetID();}Check(recycled&&!g.Scene()->IsAlive(boss),"objective ID reused without reviving old generation");
    Tick(g,70);Check(g.GetState()==State::Defeat&&g.ExtractResult().reason==RunEndReason::BossEscaped&&g.ExtractResult().health>0,"deleting boss cannot satisfy kill objective");
    c.maxEnemies=0;GameModule invalid(c);Check(!invalid.Startup(),"campaign rejects zero enemy capacity");
}
void CompleteSixWavesAndResult(){
    auto c=Quiet();c.spawning=true;c.waveSeconds=.3f;GameModule g(c);Start(g);int spent=0;
    for(int wave=1;wave<=6;++wave){Tick(g);if(wave==3||wave==6)KillBoss(g);if(wave<6){ToShop(g);if(wave==1){ShopOffer offer;offer.type=OfferType::Weapon;offer.weapon=WeaponKind::Gun;offer.price=1;offer.sold=false;g.Get<Shop>(g.PlayerEntity()).offers[0]=offer;Check(g.BuyShopOffer(0)==ShopResult::Bought,"result fixture actual equipment purchase");spent=1;}g.NextWave();}else Tick(g,100);}
    const auto result=g.ExtractResult();Check(g.GetState()==State::Victory&&result.outcome==RunOutcome::Victory&&result.wave==6&&result.bossKills==2&&result.weapons==2,"six wave terminal victory snapshot");Check(result.materials==105-spent&&g.Get<Shop>(g.PlayerEntity()).visits==5,"final victory never creates sixth shop or extra payout");Check(g.Stats().enemies==0&&g.Stats().projectiles==0&&g.Stats().hostileProjectiles==0&&g.Stats().effects==0&&g.Stats().statuses==0,"result clears every transient kind");
    auto ticks=g.Stats().ticks;g.NextWave();g.Revive();g.Advance(.25);g.FixedTick();Check(g.Stats().ticks==ticks&&g.GetState()==State::Victory,"victory cannot continue or revive");g.Get<Player>(g.PlayerEntity()).materials+=100;Check(g.ExtractResult().materials==result.materials,"result is a captured value");g.Restart();Check(g.ExtractResult().outcome==RunOutcome::None&&g.Stats().wave==1&&g.ExtractEquipment().count==1&&g.Get<RunProgress>(g.PlayerEntity()).elapsedTicks==0,"restart resets completed campaign and inventory");
}
void DeathPriorityAndTimeout(){
    auto c=Quiet();c.spawning=true;c.campaignWaves=1;c.waveSeconds=.2f;GameModule g(c);Start(g);Tick(g);auto boss=Objective(g);auto b=g.SpawnProjectile(g.Get<Transform>(boss).position,{});g.Get<Damage>(b).amount=100000;g.SpawnHostileProjectile({0,0},{},100000);Tick(g);
    Check(g.Stats().bossKills==1&&g.GetState()==State::Defeat&&g.ExtractResult().reason==RunEndReason::PlayerDied,"same tick boss kill and player death is defeat");g.Revive();g.NextWave();Check(g.GetState()==State::Defeat,"campaign defeat is terminal");
    GameModule timeout(c);Start(timeout);Tick(timeout,40);Check(timeout.GetState()==State::Defeat&&timeout.ExtractResult().reason==RunEndReason::BossEscaped&&timeout.ExtractResult().health==150,"deadline never grants survival victory with living boss");
}
void SourceAndUnboundedCompatibility(){
    auto c=Quiet();c.expanded=c.builds=c.arsenal=c.traits=false;c.campaign=true;c.campaignWaves=1;c.waveSeconds=.05f;GameModule source(c);Start(source);Tick(source,10);Check(source.GetState()==State::WaveComplete&&source.ExtractResult().outcome==RunOutcome::None&&source.Stats().bossSpawns==0,"source does not gain campaign goals");
    c=Quiet();c.campaign=false;c.waveSeconds=.05f;GameModule endless(c);Start(endless);for(int i=0;i<8;++i){ToShop(endless);endless.NextWave();}Check(endless.Stats().wave==9&&endless.ExtractResult().outcome==RunOutcome::None,"history mode retains unbounded waves");
}
void StandaloneSystemsAndForeignArchetypes(){
    GameWorld w(Quiet());w.scene=std::make_unique<ECS::Core::Scene>();w.commands.emplace(*w.scene);w.effectCommands.emplace(*w.scene);
    auto players=Type<Player,Health,RunProgress,Transform,BuildBase,CombatStats,Growth,Shop>(*w.scene);w.player=w.scene->CreateEntity(players);w.Get<Health>(w.player).current=w.Get<Health>(w.player).maximum=150;
    auto type=Type<Extra,BossBrain,Enemy,Health,Transform>(*w.scene);auto boss=w.scene->CreateEntity(type);w.Get<Enemy>(boss).phase=EnemyPhase::Alive;w.Get<Health>(boss).current=w.Get<Health>(boss).maximum=70;w.Get<Transform>(boss).position={4,0};w.Get<BossBrain>(boss).cooldown=0;w.Get<BossBrain>(boss).objective=true;w.Get<BossBrain>(boss).wave=6;
    w.hostileType=Type<HostileProjectile,Transform,Velocity,Circle,Sprite,Damage>(*w.scene);w.stats.wave=6;w.stats.ticks=30;BeginCampaignWave(w);w.Get<RunProgress>(w.player).bossSpawned=true;
    ECS::System::Context context;context.scene=w.scene.get();context.SetService(&w);ECS::System::Pipeline pipeline;pipeline.Add<BossAISystem>();pipeline.Add<RunSystem>();pipeline.RunBefore<BossAISystem,RunSystem>();Check(pipeline.Start(context),"standalone boss/run systems start");
    for(int i=0;i<79;++i){Check(pipeline.Tick(context),"standalone tick");w.commands->Playback();w.reservedHostileProjectiles=0;}
    Check(Count<HostileProjectile>(w)==5&&ExtractBossStatus(w).size()==1&&w.Get<Extra>(boss).value==42,"foreign boss runs without GameModule or legacy EnemyBrain");
    w.Get<Enemy>(boss).phase=EnemyPhase::Dying;w.Get<Health>(boss).current=0;w.state=State::WaveComplete;pipeline.Tick(context);Check(w.state==State::Victory&&w.Get<RunProgress>(w.player).result.seconds==.25,"standalone result derives terminal state and elapsed simulation");pipeline.Stop(context);
}
void NaturalCompleteRun(){
    auto c=ExpandedGameplay();c.initialWeapon=WeaponKind::Gun;GameModule g(c);Start(g);
    const glm::vec2 route[]{{c.maximum.x-2,c.maximum.y-2},{c.minimum.x+2,c.maximum.y-2},{c.minimum.x+2,c.minimum.y+2},{c.maximum.x-2,c.minimum.y+2}};std::size_t waypoint=0;
    for(int wave=1;wave<=6;++wave){int limit=14000;while(limit-->0&&(g.GetState()==State::Playing||g.GetState()==State::LevelUp)){
        if(g.GetState()==State::LevelUp){const auto choices=g.Get<Growth>(g.PlayerEntity()).choices;auto chosen=choices[0];for(auto p:{UpgradeKind::Damage,UpgradeKind::Armor,UpgradeKind::MaxHealth,UpgradeKind::AttackSpeed,UpgradeKind::MoveSpeed,UpgradeKind::PickupRange})if(std::find(choices.begin(),choices.end(),p)!=choices.end()){chosen=p;break;}Check(g.SelectUpgrade(chosen),"natural offered upgrade");continue;}
        auto delta=route[waypoint]-g.Get<Transform>(g.PlayerEntity()).position;if(glm::length(delta)<.35f){waypoint=(waypoint+1)%std::size(route);delta=route[waypoint]-g.Get<Transform>(g.PlayerEntity()).position;}auto direction=glm::normalize(delta);g.FixedTick({direction.x,direction.y});
    }
    std::cout<<"[natural campaign] wave="<<wave<<" state="<<int(g.GetState())<<" hp="<<g.ExtractBuild().health<<" kills="<<g.Stats().kills<<" weapons="<<g.ExtractEquipment().count<<" bossKills="<<g.Stats().bossKills<<" materials="<<g.ExtractBuild().materials<<'\n';
    if(wave==6){Check(g.GetState()==State::Victory&&g.Stats().bossKills==2,"natural six-wave build defeats both bosses and wins");break;}
    Check(g.GetState()==State::Shop,"natural run reaches intervening shop");
    for(int roll=0;roll<4;++roll){auto stock=g.ExtractShop().shop.offers;bool bought=false;
        if(g.ExtractEquipment().count<6)for(auto preferred:{WeaponKind::Gun,WeaponKind::Burst,WeaponKind::Wand,WeaponKind::Laser,WeaponKind::Torch,WeaponKind::Knife})for(std::size_t i=0;i<ShopSlots;++i)if(stock[i].type==OfferType::Weapon&&stock[i].weapon==preferred&&g.BuyShopOffer(i)==ShopResult::Bought)bought=true;
        for(auto preferred:{ItemKind::Plant,ItemKind::Vest,ItemKind::Cake,ItemKind::Lens,ItemKind::Coffee,ItemKind::Sausage,ItemKind::Bat,ItemKind::Sunglasses,ItemKind::Bandana,ItemKind::Beanie})for(std::size_t i=0;i<ShopSlots;++i)if(stock[i].type==OfferType::Item&&stock[i].kind==preferred&&g.BuyShopOffer(i)==ShopResult::Bought)bought=true;
        if(!bought || g.ExtractBuild().materials<12 || g.RerollShop()!=ShopResult::Rerolled)break;
    }g.NextWave();}
    auto result=g.ExtractResult();std::cout<<"[campaign victory] hp="<<result.health<<"/"<<result.maximumHealth<<" kills="<<result.kills<<" bosses="<<result.bossKills<<" level="<<result.level<<" weapons="<<result.weapons<<" items="<<result.items<<" materials="<<result.materials<<" seconds="<<result.seconds<<'\n';
}
}
int main(){try{RegisterComponents();REGISTER_COMPONENT("brotato_campaign_extra",Extra);const std::pair<const char*,std::function<void()>> tests[]{{"objective lifecycle and freezes",ObjectiveLifecycleAndFreeze},{"patterns locked charge and capacity",PatternsAndLockedCharge},{"enrage and ECS slow",EnrageAndStatusInteraction},{"capacity retry and stale objective",CapacityRetryAndDeletedObjective},{"six waves immutable result and restart",CompleteSixWavesAndResult},{"death priority and deadline",DeathPriorityAndTimeout},{"source and history compatibility",SourceAndUnboundedCompatibility},{"standalone and foreign archetypes",StandaloneSystemsAndForeignArchetypes},{"natural complete run",NaturalCompleteRun}};for(const auto& [name,test]:tests){test();std::cout<<"[PASS] "<<name<<'\n';}std::cout<<"9 campaign groups passed\n";return 0;}catch(const std::exception& e){std::cerr<<"[FAIL] "<<e.what()<<'\n';return 1;}}
