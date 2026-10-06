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
struct Extra:Component<Extra>{int value=37;};
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void Near(float a,float b,const char* message){Check(std::abs(a-b)<1e-4f,message);}
template<class... T>auto Type(ECS::Core::Scene& scene){auto d=scene.CreateArchTypeDescription();(d->AddComponentArray<T>(),...);return scene.CreateArchType(d,2);}
Config Quiet(){auto c=ExpandedGameplay();c.encounters=false; // Historical mechanisms are tested separately from encounters.
c.campaign=false;c.playerStart={0,0};c.minimum={-100,-100};c.maximum={100,100};c.spawning=c.armed=false;c.enemySpeed=0;c.contactDamage=0;c.dropChance=0;c.maxEffects=0;c.waveSeconds=600;c.deathDelay=.05f;return c;}
struct Fixture {
    GameWorld w{Quiet()};ECS::System::Context context;ECS::System::Pipeline pipeline;
    Fixture(){
        w.stats.remaining=w.config.waveSeconds;
        w.scene=std::make_unique<ECS::Core::Scene>();w.commands.emplace(*w.scene);w.effectCommands.emplace(*w.scene);
        w.playerType=Type<Player,Transform,Velocity,Circle,Sprite,Health,CombatStats,BuildBase,Growth,Shop>(*w.scene);
        w.enemyType=Type<Extra,Enemy,Transform,Velocity,Circle,Sprite,Health,Damage,EnemyBrain,Knockback>(*w.scene);
        w.weaponType=Type<Weapon,Transform,Sprite,Damage>(*w.scene);w.statusType=Type<TimedStatus>(*w.scene);
        w.projectileType=Type<Projectile,Transform,Velocity,Circle,Sprite,Damage>(*w.scene);
        w.itemType=Type<OwnedItem>(*w.scene);w.pickupType=Type<Pickup,Transform,Circle,Sprite>(*w.scene);
        w.player=w.scene->CreateEntity(w.playerType);w.Get<Health>(w.player).current=w.Get<Health>(w.player).maximum=100;w.Get<BuildBase>(w.player).maximumHealth=100;
        w.weapon=Equip(WeaponKind::Gun,0);context.scene=w.scene.get();context.SetService(&w);context.deltaSeconds=SimulationStep;
        pipeline.Add<WaveSystem>();pipeline.Add<StatusSystem>();pipeline.Add<MovementSystem>();pipeline.Add<WeaponSystem>();pipeline.Add<ProjectileSystem>();
        pipeline.RunBefore<WaveSystem,StatusSystem>();pipeline.RunBefore<StatusSystem,MovementSystem>();pipeline.RunBefore<MovementSystem,WeaponSystem>();pipeline.RunBefore<WeaponSystem,ProjectileSystem>();
        Check(pipeline.Start(context),"standalone systems start");
    }
    ~Fixture(){pipeline.Stop(context);}
    ECS::EntityHandle Equip(WeaponKind kind,int slot,WeaponAffix affix=WeaponAffix::None){auto e=w.scene->CreateEntity(w.weaponType);InitializeEquipment(w,e,w.player,kind,0,slot,affix);return e;}
    ECS::EntityHandle EnemyAt(glm::vec2 position,int hp=100,EnemyKind kind=EnemyKind::Normal){auto e=w.scene->CreateEntity(w.enemyType);w.Get<Enemy>(e).phase=EnemyPhase::Alive;w.Get<Enemy>(e).kind=kind;w.Get<Transform>(e).position=w.Get<Transform>(e).previous=position;w.Get<Health>(e).current=w.Get<Health>(e).maximum=hp;w.Get<Circle>(e).radius=.3f;return e;}
    void Flush(){w.commands->Playback();w.effectCommands->Playback();w.reservedStatuses=w.reservedProjectiles=w.reservedPickups=w.reservedEffects=0;RefreshCounts(w);}
    void Tick(int n=1){for(int i=0;i<n;++i){Check(pipeline.Tick(context),"standalone tick");Flush();}}
    void Item(ItemKind kind,int count=1){auto e=w.scene->CreateEntity(w.itemType);auto& item=w.Get<OwnedItem>(e);item.owner=w.player;item.kind=kind;item.count=count;}
    ECS::EntityHandle Bullet(glm::vec2 position,glm::vec2 velocity,Damage damage){auto e=CreateProjectile(w,position,velocity,WeaponKind::Gun,-1,0);w.Get<Damage>(e)=damage;w.Get<Projectile>(e).pierces=damage.pierce;w.Get<Projectile>(e).gravity=0;return e;}
};
TimedStatus Status(Fixture& f,ECS::EntityHandle target){Query<TimedStatus> q;q.Refresh(*f.w.scene);for(auto c:q)for(std::size_t i=0;i<c.count;++i)if(c.Get<TimedStatus>()[i].target.GetID()==target.GetID())return c.Get<TimedStatus>()[i];throw std::runtime_error("missing status");}
void FamiliesAndItems(){
    Fixture f;auto& w=f.w;f.Equip(WeaponKind::Burst,1);f.Equip(WeaponKind::Wand,2);f.Equip(WeaponKind::Torch,3);f.Equip(WeaponKind::Laser,4);f.Equip(WeaponKind::Knife,5);
    auto type=Type<Extra,Weapon,Transform,Sprite,Damage>(*w.scene);auto foreign=w.scene->CreateEntity(type);InitializeEquipment(w,foreign,w.player,WeaponKind::Gun,0,-1);
    RecomputeBuild(w,w.player);Check(w.Get<CombatStats>(w.player).families==std::array<int,3>{2,2,2},"only equipped entities contribute family pairs");
    Near(RankedCooldown(w,w.Get<Weapon>(w.weapon)),w.Definition(WeaponKind::Gun).cooldown/1.1f,"ballistic pair changes actual firing cooldown");
    Weapon elemental;elemental.owner=w.player;elemental.kind=WeaponKind::Wand;Check(WeaponPayload(w,elemental,false).burning==1&&WeaponPayload(w,w.Get<Weapon>(w.weapon),false).burning==0,"elemental bonus restricted to its family");
    for(auto item:{ItemKind::Bandana,ItemKind::Bat,ItemKind::Sunglasses,ItemKind::Sausage})f.Item(item,100);
    RecomputeBuild(w,w.player);auto stats=w.Get<CombatStats>(w.player);Check(stats.pierce==3&&stats.burning==20,"item effect caps");Near(stats.criticalChance,.6f,"crit cap");Near(stats.lifeSteal,.5f,"life steal cap");
    for(int i=0;i<20;++i)RecomputeBuild(w,w.player);Near(w.Get<CombatStats>(w.player).lifeSteal,.5f,"recompute never compounds bonuses");
    w.Get<Weapon>(foreign).equipmentSlot=1;w.Get<Weapon>(foreign).owner=ECS::EntityHandle(0);RecomputeBuild(w,w.player);Check(w.Get<CombatStats>(w.player).families[2]==2,"stale owners excluded");
    w.config.traits=false;RecomputeBuild(w,w.player);stats=w.Get<CombatStats>(w.player);Check(stats.pierce==0&&stats.burning==0&&stats.families==std::array<int,3>{},"history mode has no trait effects");Near(stats.criticalChance,0,"history mode no crit");
}
void BurnRefreshExpiryAndDeath(){
    Fixture f;auto& w=f.w;auto enemy=f.EnemyAt({10,0});Damage d;d.burning=2;d.owner=w.player;
    QueueStatus(w,enemy,d,WeaponKind::Gun);QueueStatus(w,enemy,d,WeaponKind::Gun);f.Flush();Check(Count<TimedStatus>(w)==1,"same tick duplicate requests coalesce");
    f.Tick(25);const auto pulse=Status(f,enemy).pulse;d.burning=4;QueueStatus(w,enemy,d,WeaponKind::Gun);f.Flush();Near(Status(f,enemy).pulse,pulse,"refresh preserves elapsed pulse");
    f.Tick(35);Check(w.Get<Health>(enemy).current==96&&w.stats.burnHits==1,"strongest refreshed damage on first pulse");
    f.Tick(230);Check(w.Get<Health>(enemy).current==84&&w.stats.burnHits==4&&Count<TimedStatus>(w)==0,"exactly four pulses then expiry");
    auto doomed=f.EnemyAt({20,0},3);d.burning=1;QueueStatus(w,doomed,d,WeaponKind::Gun);f.Flush();f.Tick(180);
    Check(w.Get<Enemy>(doomed).phase==EnemyPhase::Dying&&w.stats.kills==1&&w.Get<Player>(w.player).experience==0,"burn uses ordinary death with delayed XP");
    f.Tick(130);Check(!w.scene->IsAlive(doomed)&&w.Get<Player>(w.player).experience==2,"burn death awards XP once after delay");
    auto source=w.scene->CreateEntity(w.playerType);enemy=f.EnemyAt({30,0});d.owner=source;QueueStatus(w,enemy,d,WeaponKind::Gun);f.Flush();w.scene->DeleteEntity(source);f.Tick(60);Check(w.Get<Health>(enemy).current==99,"source retirement does not cancel burn");
}
void StatusLifecycleAndCapacity(){
    Fixture f;auto& w=f.w;auto enemy=f.EnemyAt({10,0});Damage d;d.burning=1;QueueStatus(w,enemy,d,WeaponKind::Gun);f.Flush();
    for(auto state:{State::Paused,State::LevelUp,State::Shop,State::Dead}){auto before=Status(f,enemy);w.state=state;f.Tick(60);Near(Status(f,enemy).remaining,before.remaining,"non-playing states freeze statuses");}
    w.state=State::Playing;w.scene->DeleteEntity(enemy);auto replacement=f.EnemyAt({10,0});Check(replacement.GetID()==enemy.GetID()&&!w.scene->IsAlive(enemy),"target ID recycled with fresh generation");f.Tick(60);Check(Count<TimedStatus>(w)==0&&w.Get<Health>(replacement).current==100,"stale status never rebinds");
    auto other=Type<Extra,TimedStatus>(*w.scene);auto e=w.scene->CreateEntity(other);w.Get<TimedStatus>(e).target=replacement;w.Get<TimedStatus>(e).damage=2;
    w.config.maxStatuses=1;auto blocked=f.EnemyAt({20,0});QueueStatus(w,blocked,d,WeaponKind::Gun);f.Flush();Check(Count<TimedStatus>(w)==1,"capacity includes foreign archetype");f.Tick(60);Check(w.Get<Health>(replacement).current==98,"foreign status participates in standalone system");
    ClearTransient(w);Check(Count<TimedStatus>(w)==0&&w.reservedStatuses==0,"wave cleanup retires every status archetype");
    w.config.maxStatuses=0;enemy=f.EnemyAt({10,0});Check(ResolveHit(w,enemy,d,WeaponKind::Gun,{1,0}),"capacity does not swallow direct damage");f.Flush();Check(Count<TimedStatus>(w)==0&&w.Get<Health>(enemy).current==99,"zero capacity prevents status allocation");
}
void SlowAndCharge(){
    Fixture f;auto& w=f.w;w.config.enemySpeed=2;auto enemy=f.EnemyAt({10,0});Damage d;d.slow=.5f;QueueStatus(w,enemy,d,WeaponKind::Gun);f.Flush();f.Tick();Near(w.Get<Velocity>(enemy).value.x,-1,"slow modifies ordinary movement");
    d.slow=.8f;QueueStatus(w,enemy,d,WeaponKind::Gun);f.Flush();Near(Status(f,enemy).slow,.5f,"weaker slow cannot replace stronger");
    auto charger=f.EnemyAt({20,0},100,EnemyKind::Charger);auto& brain=w.Get<EnemyBrain>(charger);brain.action=EnemyAction::Charging;brain.direction={1,0};QueueStatus(w,charger,d,WeaponKind::Gun);f.Flush();f.Tick();Near(w.Get<Velocity>(charger).value.x,EnemyBehaviors[std::size_t(EnemyKind::Charger)].chargeSpeed*.8f,"slow also changes charge speed");
    f.Tick(190);Near(w.Get<Enemy>(enemy).slowMultiplier,1,"expired slow resets derived multiplier");Near(w.Get<Velocity>(enemy).value.x,-2,"movement restored after expiry");
}
void DirectLifestealAndNoRecursiveProcs(){
    Fixture f;auto& w=f.w;auto& hp=w.Get<Health>(w.player);hp.current=50;Damage d;d.amount=1;d.owner=w.player;d.lifeSteal=.1f;
    auto enemy=f.EnemyAt({10,0},100);for(int i=0;i<10;++i)Check(ResolveHit(w,enemy,d,WeaponKind::Gun,{1,0}),"fractional steal hit");Check(hp.current==51&&w.stats.stolenHealth==1,"fractional steal accumulates actual damage");
    auto weak=f.EnemyAt({20,0},1);d.amount=100;d.lifeSteal=.5f;ResolveHit(w,weak,d,WeaponKind::Gun,{1,0});Check(hp.current==51,"overkill cannot heal nominal damage");Near(w.Get<BuildBase>(w.player).lifeStealCredit,.5f,"overkill credit based on one HP");
    d.amount=2;d.burning=2;d.splashRadius=3;auto splash=f.EnemyAt({10,1},100);ResolveHit(w,enemy,d,WeaponKind::Wand,{1,0});f.Flush();Check(Count<TimedStatus>(w)==1&&w.Get<Health>(splash).current==98,"splash does not apply burn or life steal");const int healed=hp.current;f.Tick(60);Check(hp.current==healed&&w.Get<Health>(splash).current==98,"burn does not recursively steal or explode");
    hp.current=0;ResolveHit(w,enemy,d,WeaponKind::Gun,{1,0});Check(hp.current==0,"life steal cannot revive");hp.current=99;d.amount=20;ResolveHit(w,enemy,d,WeaponKind::Gun,{1,0});Check(hp.current==100&&w.Get<BuildBase>(w.player).lifeStealCredit==0,"healing caps at max and discards full-health credit");
}
void PiercingOrderSnapshotsAndCompaction(){
    Fixture f;auto& w=f.w;auto farEnemy=f.EnemyAt({-4,0});auto nearEnemy=f.EnemyAt({-8,0});auto middle=f.EnemyAt({-6,0});auto last=f.EnemyAt({-2,0});Damage d;d.amount=3;d.owner=w.player;d.pierce=2;
    auto bullet=f.Bullet({-10,0},{1200,0},d);w.scene->DeleteEntity(w.weapon);w.Get<CombatStats>(w.player).bonusDamage=100;f.Tick();
    Check(w.Get<Health>(nearEnemy).current==97&&w.Get<Health>(middle).current==97&&w.Get<Health>(farEnemy).current==97&&w.Get<Health>(last).current==100,"sweep follows physical order and retains attack snapshot");Check(!w.scene->IsAlive(bullet)&&w.stats.piercedHits==2,"pierce target budget exhausted");
    auto target=f.EnemyAt({40,0});d.pierce=3;bullet=f.Bullet({40,0},{0,0},d);f.Tick(3);Check(w.Get<Health>(target).current==97&&w.Get<Projectile>(bullet).hitTargets.size()==1,"retained projectile cannot hit same target twice");
    w.scene->DeleteEntity(target);auto recycled=f.EnemyAt({40,0});Check(!w.scene->IsAlive(target)&&recycled.GetID()==target.GetID(),"hit target generation recycled");f.Tick();Check(w.Get<Health>(recycled).current==97,"new generation is a new physical target");
    auto first=f.Bullet({70,0},{0,0},d);auto second=f.Bullet({80,0},{0,0},d);w.Get<Projectile>(second).hitTargets.push_back(recycled);w.scene->DeleteEntity(first);Check(w.Get<Projectile>(second).hitTargets.size()==1&&w.scene->IsAlive(w.Get<Projectile>(second).hitTargets[0]),"nontrivial projectile component survives compaction");
}
void CriticalRngAndBurst(){
    Fixture f;auto& w=f.w;w.config.armed=true;auto& weapon=w.Get<Weapon>(w.weapon);InitializeEquipment(w,w.weapon,w.player,WeaponKind::Burst,0,0,WeaponAffix::Burning);w.Get<CombatStats>(w.player).criticalChance=.6f;
    auto target=f.EnemyAt({5,0},1000);weapon.randomState=1;std::uint32_t expected=1;const bool critical=BuildRandom(expected)%10000<6000;const auto spawnRng=w.random;const auto shopRng=w.Get<Shop>(w.player).randomState;
    w.config.maxProjectiles=3;f.Tick();Check(w.stats.shots==0&&weapon.randomState==1,"capacity rejection consumes no attack RNG");w.config.maxProjectiles=4;f.Tick();Check(w.stats.shots==1&&Count<Projectile>(w)==4&&weapon.randomState==expected,"one crit draw for entire four-shot burst");
    Check(w.random==spawnRng&&w.Get<Shop>(w.player).randomState==shopRng,"crit stream isolated from spawn drop and shop RNG");
    Query<Projectile,Damage> q;q.Refresh(*w.scene);for(auto c:q)for(std::size_t i=0;i<c.count;++i){const auto& p=c.Get<Damage>()[i];Check(p.critical==critical&&p.burning==1&&p.owner.GetID()==w.player.GetID()&&w.scene->IsAlive(p.owner),"every burst projectile copies complete attack payload");}
    Check(w.Get<Health>(target).current==1000,"fixture observes payload before impact");
    auto old=weapon.randomState;ResetWeapon(w,true);Check(w.Get<Weapon>(w.weapon).randomState==old,"wave reset preserves crit stream");
}
void Stock(GameModule& g,WeaponKind kind,WeaponAffix affix,int tier=0){ShopOffer o;o.type=OfferType::Weapon;o.weapon=kind;o.affix=affix;o.tier=tier;o.price=1;o.sold=false;g.Get<Shop>(g.PlayerEntity()).offers[0]=o;}
void ToShop(GameModule& g){for(int i=0;i<100&&g.GetState()==State::Playing;++i)g.FixedTick();Check(g.GetState()==State::Shop,"short wave shop");}
void ShopAffixesMergeAndRestart(){
    auto c=Quiet();c.waveSeconds=.05f;c.waveIncrement=0;GameModule g(c);Check(g.Startup(),"shop start");ToShop(g);g.Get<Player>(g.PlayerEntity()).materials=MaxMaterials;
    Stock(g,WeaponKind::Wand,WeaponAffix::Chilling);Check(g.BuyShopOffer(0)==ShopResult::Bought,"affixed weapon bought");g.Get<Weapon>(g.WeaponEntity()).affix=WeaponAffix::Burning;
    Check(g.MergeWeapon(0)==ShopResult::Merged&&g.ExtractEquipment().slots[0].affix==WeaponAffix::Burning&&g.ExtractEquipment().slots[0].tier==1,"merge retains selected affix");Check(g.Get<CombatStats>(g.PlayerEntity()).families[0]==1,"merge immediately removes family threshold");
    Stock(g,WeaponKind::Torch,WeaponAffix::Piercing);auto money=g.ExtractBuild().materials;Check(g.BuyShopOffer(0)==ShopResult::Invalid&&g.ExtractBuild().materials==money,"inapplicable affix rejected atomically");
    Stock(g,WeaponKind::Gun,WeaponAffix::Vampiric);g.ToggleShopLock(0);g.RerollShop();g.NextWave();ToShop(g);Check(g.ExtractShop().shop.offers[0].locked&&g.ExtractShop().shop.offers[0].affix==WeaponAffix::Vampiric,"lock preserves affix across reroll and wave");g.ToggleShopLock(0);
    g.RerollShop();std::array<bool,5> affixes{};std::array<bool,ItemCount> items{};
    for(int i=0;i<500;++i){auto shop=g.ExtractShop().shop;for(const auto& o:shop.offers){if(o.type==OfferType::Weapon){Check(ValidAffix(o.weapon,o.affix),"generated affix applicable");affixes[std::size_t(o.affix)]=true;Check(o.price==WeaponPrice(o.weapon,o.tier)+std::min(shop.visits/2,50)+(o.affix==WeaponAffix::None?0:3),"affix price premium");}else items[std::size_t(o.kind)]=true;}g.RerollShop();}
    Check(std::all_of(affixes.begin(),affixes.end(),[](bool b){return b;})&&std::all_of(items.begin(),items.end(),[](bool b){return b;}),"all affixes and ten items reachable in ordinary stock");
    g.Get<Weapon>(g.WeaponEntity()).randomState=123;g.NextWave();Check(g.Get<Weapon>(g.WeaponEntity()).randomState==123&&g.ExtractEquipment().slots[0].affix==WeaponAffix::Burning,"wave retains affix and RNG");g.Restart();Check(g.ExtractEquipment().count==1&&g.ExtractEquipment().slots[0].affix==WeaponAffix::None&&g.Stats().statuses==0,"restart clears traits and transient statuses");
}
void NaturalTwoWaves(){
    auto c=ExpandedGameplay();c.campaign=false;GameModule g(c);Check(g.Startup(),"natural start");const glm::vec2 route[]{{c.maximum.x-2,c.maximum.y-2},{c.minimum.x+2,c.maximum.y-2},{c.minimum.x+2,c.minimum.y+2},{c.maximum.x-2,c.minimum.y+2}};std::size_t waypoint=0;
    for(int wave=1;wave<=2;++wave){int limit=6000;while(limit-->0&&(g.GetState()==State::Playing||g.GetState()==State::LevelUp)){
        if(g.GetState()==State::LevelUp){const auto choices=g.Get<Growth>(g.PlayerEntity()).choices;auto chosen=choices[0];for(auto priority:{UpgradeKind::Damage,UpgradeKind::Armor,UpgradeKind::MaxHealth,UpgradeKind::AttackSpeed,UpgradeKind::MoveSpeed,UpgradeKind::PickupRange})if(std::find(choices.begin(),choices.end(),priority)!=choices.end()){chosen=priority;break;}Check(g.SelectUpgrade(chosen),"natural upgrade");continue;}
        auto delta=route[waypoint]-g.Get<Transform>(g.PlayerEntity()).position;if(glm::length(delta)<.35f){waypoint=(waypoint+1)%std::size(route);delta=route[waypoint]-g.Get<Transform>(g.PlayerEntity()).position;}auto direction=glm::normalize(delta);g.FixedTick({direction.x,direction.y});
    }Check(g.GetState()==State::Shop&&g.ExtractBuild().health>0,"natural traits run survives wave");if(wave==1){auto offers=g.ExtractShop().shop.offers;bool bought=false;for(auto preferred:{WeaponKind::Gun,WeaponKind::Burst,WeaponKind::Wand,WeaponKind::Laser,WeaponKind::Knife,WeaponKind::Torch}){for(std::size_t i=0;i<ShopSlots&&!bought;++i)if(offers[i].type==OfferType::Weapon&&offers[i].weapon==preferred&&g.BuyShopOffer(i)==ShopResult::Bought)bought=true;if(bought)break;}Check(bought,"earned materials buy natural weapon");for(std::size_t i=0;i<ShopSlots;++i)if(offers[i].type==OfferType::Item)g.BuyShopOffer(i);g.NextWave();}}
    auto b=g.ExtractBuild();std::cout<<"[traits survival] hp="<<b.health<<"/"<<b.maximumHealth<<" kills="<<g.Stats().kills<<" materials="<<b.materials<<" weapons="<<g.ExtractEquipment().count<<" burn="<<g.Stats().burnHits<<" crit="<<g.Stats().criticalAttacks<<'\n';Check(g.ExtractEquipment().count==2&&g.Get<Player>(g.PlayerEntity()).level>0,"ordinary loop retains equipment and growth");
}
}
int main(){try{RegisterComponents();REGISTER_COMPONENT("brotato_traits_extra",Extra);const std::pair<const char*,std::function<void()>> tests[]{{"family and item derivation",FamiliesAndItems},{"burn refresh expiry and delayed death",BurnRefreshExpiryAndDeath},{"status lifecycle generation and capacity",StatusLifecycleAndCapacity},{"slow movement and charge",SlowAndCharge},{"actual lifesteal and no recursive procs",DirectLifestealAndNoRecursiveProcs},{"pierce sweep snapshots and compaction",PiercingOrderSnapshotsAndCompaction},{"critical RNG and burst snapshot",CriticalRngAndBurst},{"shop affixes merge lock and restart",ShopAffixesMergeAndRestart},{"natural two waves",NaturalTwoWaves}};for(const auto& [name,test]:tests){test();std::cout<<"[PASS] "<<name<<'\n';}std::cout<<"9 trait groups passed\n";return 0;}catch(const std::exception& e){std::cerr<<"[FAIL] "<<e.what()<<'\n';return 1;}}
