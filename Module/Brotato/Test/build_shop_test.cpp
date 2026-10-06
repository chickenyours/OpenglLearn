#include "Brotato/Public/game_module.h"
#include "Brotato/Public/expanded_gameplay.h"
#include "Brotato/Public/session_module.h"
#include "Brotato/Systems/game_systems.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace Brotato;
struct Extra : Component<Extra> { int value=37; };
void Check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
void Near(float a,float b,const char* message) {Check(std::isfinite(a)&&std::abs(a-b)<1e-4f,message);}
template<class... T> auto Type(ECS::Core::Scene& scene) {
    auto d=scene.CreateArchTypeDescription();(d->AddComponentArray<T>(),...);return scene.CreateArchType(d,2);
}
Config Quiet() {
    auto c=ExpandedGameplay();c.encounters=false; // Isolate historical mechanics from encounter scheduling.
c.arsenal=false;c.playerStart={0,0};c.minimum={-30,-30};c.maximum={30,30};
    c.spawning=c.armed=false;c.enemySpeed=0;c.contactDamage=0;c.dropChance=0;c.waveSeconds=600;c.waveIncrement=0;
    return c;
}
void Start(GameModule& g){Check(g.Startup(),g.Error().c_str());}
void Tick(GameModule& g,int count,Input input={}){while(count-->0)g.FixedTick(input);}
void UntilShop(GameModule& g,int limit=1000){while(limit-->0&&g.GetState()==State::Playing)g.FixedTick();Check(g.GetState()==State::Shop,"wave opens shop");}
Config Short(){auto c=Quiet();c.waveSeconds=.05f;return c;}
void Stock(GameModule& g,std::size_t slot,ItemKind kind,int price=-1){g.Get<Shop>(g.PlayerEntity()).offers[slot]={kind,price<0?ItemDefinitions[std::size_t(kind)].price:price,false,false};}
ECS::EntityHandle Item(GameModule& g,ItemKind kind,int count=1,ECS::EntityHandle owner=ECS::EntityHandle(0)) {
    auto type=Type<Extra,OwnedItem>(*g.Scene());auto e=g.Scene()->CreateEntity(type);
    auto& item=g.Get<OwnedItem>(e);item.kind=kind;item.count=count;item.owner=owner.GetID()?owner:g.PlayerEntity();return e;
}
void Grant(GameModule& g,int xp){g.Get<Player>(g.PlayerEntity()).experience+=xp;g.FixedTick();Check(g.GetState()==State::LevelUp,"XP opens reward pool");}
bool SameOffer(const ShopOffer& a,const ShopOffer& b){return a.kind==b.kind&&a.price==b.price&&a.sold==b.sold&&a.locked==b.locked;}

void RewardPoolAndInput() {
    GameModule a(Quiet()),b(Quiet());Start(a);Start(b);Grant(a,36);Grant(b,36);
    Check(a.Get<Growth>(a.PlayerEntity()).pending==3,"multiple earned levels preserve three choices");
    std::array<bool,UpgradeCount> seen{};
    for(int round=0;round<100;++round) {
        const auto offers=a.Get<Growth>(a.PlayerEntity()).choices;
        Check(offers==b.Get<Growth>(b.PlayerEntity()).choices,"reward sequence is seed deterministic");
        Check(offers[0]!=offers[1]&&offers[0]!=offers[2]&&offers[1]!=offers[2],"three cards never repeat a kind");
        for(auto kind:offers)seen[std::size_t(kind)]=true;
        auto missing=UpgradeKind::Damage;
        for(std::size_t i=0;i<UpgradeCount;++i)if(std::find(offers.begin(),offers.end(),UpgradeKind(i))==offers.end()){missing=UpgradeKind(i);break;}
        const auto pending=a.Get<Growth>(a.PlayerEntity()).pending; const auto ticks=a.Stats().ticks;
        Check(!a.SelectUpgrade(missing)&&a.Get<Growth>(a.PlayerEntity()).pending==pending,"unoffered reward cannot be claimed");
        a.Advance(.25,{1,1});Check(a.Stats().ticks==ticks,"reward screen freezes simulation");
        Input input;input.chooseUpgrade=round%3+1;const auto chosen=offers[std::size_t(input.chooseUpgrade-1)];
        const auto count=a.Get<Growth>(a.PlayerEntity()).upgrades[std::size_t(chosen)];
        a.FixedTick(input);b.FixedTick(input);
        Check(a.Stats().ticks==ticks&&a.Get<Growth>(a.PlayerEntity()).upgrades[std::size_t(chosen)]==count+1,"slot input grants the displayed kind without advancing time");
        if(a.GetState()==State::Playing){Grant(a,ExperienceThreshold(a.Get<Player>(a.PlayerEntity()).level,true));Grant(b,ExperienceThreshold(b.Get<Player>(b.PlayerEntity()).level,true));}
    }
    Check(std::all_of(seen.begin(),seen.end(),[](bool value){return value;}),"all six reward kinds are reachable");
}

void AttributesAndForeignArchetypes() {
    GameModule g(Quiet());Start(g);g.Get<Health>(g.PlayerEntity()).current=100;
    std::vector<ECS::EntityHandle> items;
    for(std::size_t kind=0;kind<ItemCount;++kind)items.push_back(Item(g,ItemKind(kind)));
    g.FixedTick();auto build=g.ExtractBuild();
    Check(std::all_of(build.items.begin(),build.items.end(),[](int n){return n==1;}),"other archetype items participate without registration");
    Check(build.stats.bonusDamage==1&&build.stats.armor==1&&build.maximumHealth==160&&build.health==110,"item integer attributes and one-time max-health heal apply");
    Near(build.stats.attackSpeed,1.15f,"coffee applies attack rate");Near(build.stats.moveSpeed,1.1f,"beanie applies movement");
    Near(build.stats.pickupRange,1.25f,"plant applies collection range");Near(build.stats.regeneration,.5f,"plant applies regeneration");
    Tick(g,239);Check(g.ExtractBuild().health==111,"fractional regeneration reaches a whole HP at two seconds");
    Check(g.ExtractBuild().maximumHealth==160&&g.ExtractBuild().stats.bonusDamage==1,"repeated aggregation cannot compound item effects");
    g.Scene()->DeleteEntity(items[0]);g.FixedTick();Check(g.ExtractBuild().stats.bonusDamage==0,"deleting an item removes its effect after ECS compaction");
    auto players=Type<Extra,Player,BuildBase,Growth,CombatStats,Health>(*g.Scene());auto owner=g.Scene()->CreateEntity(players);
    auto& hp=g.Get<Health>(owner);hp.current=hp.maximum=150;Item(g,ItemKind::Lens,4,owner);g.FixedTick();
    Check(g.Get<CombatStats>(owner).bonusDamage==4&&g.ExtractBuild().stats.bonusDamage==0,"item attributes belong to their actual owner");
    g.Scene()->DeleteEntity(owner);auto replacement=g.Scene()->CreateEntity(players);
    Check(replacement.GetID()==owner.GetID()&&!g.Scene()->IsAlive(owner),"owner ID is recycled in the fixture");
    g.FixedTick();Check(g.Get<CombatStats>(replacement).bonusDamage==0,"stale item ownership never rebinds to a new generation");
}

void ActualCombatAndCaps() {
    auto c=Quiet();GameModule g(c);Start(g);Item(g,ItemKind::Vest,3);Item(g,ItemKind::Plant,2);Item(g,ItemKind::Lens,2);
    g.FixedTick();g.SpawnHostileProjectile({0,0},{},8);g.FixedTick();
    Check(g.Get<Health>(g.PlayerEntity()).current==145,"armor reduces an actual hostile hit by three");
    const auto pickup=g.SpawnPickup({.95f,0});g.FixedTick();Check(!g.Scene()->IsAlive(pickup)&&g.Get<Player>(g.PlayerEntity()).materials==1,"larger collection range acquires a pickup beyond base radius");
    auto enemy=g.SpawnEnemy({5,0},true,EnemyKind::Armored);g.SpawnProjectile({5,0},{});g.FixedTick();
    Check(g.Get<Health>(enemy).current==2,"item damage is snapshotted into a real projectile");
    g.SetPaused(true);const auto hp=g.Get<Health>(g.PlayerEntity()).current;Tick(g,300);Check(g.Get<Health>(g.PlayerEntity()).current==hp,"pause freezes regeneration");g.SetPaused(false);
    for(std::size_t i=0;i<UpgradeCount;++i)g.Get<Growth>(g.PlayerEntity()).upgrades[i]=100000;
    g.FixedTick();const auto build=g.ExtractBuild();
    Check(build.stats.bonusDamage==10000&&build.stats.armor==20&&build.maximumHealth==100000,"integer attributes have finite bounds");
    Near(build.stats.attackSpeed,5,"attack speed cap");Near(build.stats.moveSpeed,3,"movement cap");Near(build.stats.pickupRange,4,"pickup range cap");
    auto large=g.SpawnPickup({0,0});g.Get<Pickup>(large).value=std::numeric_limits<int>::max();g.Get<Player>(g.PlayerEntity()).materials=MaxMaterials-1;g.FixedTick();
    Check(g.ExtractBuild().materials==MaxMaterials,"large pickup addition saturates without integer overflow");
}

void ShopFreezeAndTransactions() {
    GameModule g(Short());Start(g);UntilShop(g);const auto initial=g.ExtractShop();
    Check(initial.shop.visits==1&&initial.shop.waveBonus==15&&initial.build.materials==15,"completed wave grants shop budget exactly once");
    const auto ticks=g.Stats().ticks;Input input;input.horizontal=1;input.selectWeapon=5;input.pause=true;Tick(g,100,input);g.Advance(.25,input);
    Check(g.Stats().ticks==ticks&&g.CurrentWeapon()==WeaponKind::Wand&&g.ExtractBuild().materials==15&&!g.EquipWeapon(WeaponKind::Gun),"shop freezes time and rejects combat controls");
    Stock(g,0,ItemKind::Lens);Stock(g,1,ItemKind::Lens);g.Get<Player>(g.PlayerEntity()).materials=5;
    const auto rng=g.ExtractShop().shop.randomState;
    Check(g.BuyShopItem(0)==ShopResult::NotEnough&&g.ExtractBuild().items[0]==0&&!g.ExtractShop().shop.offers[0].sold,"insufficient funds preserve wallet, item and stock");
    Check(g.ExtractShop().shop.randomState==rng&&g.BuyShopItem(ShopSlots)==ShopResult::Invalid,"invalid purchase does not consume RNG");
    g.Get<Player>(g.PlayerEntity()).materials=20;input={};input.buySlot=1;g.FixedTick(input);
    Check(g.ExtractBuild().items[0]==1&&g.ExtractBuild().materials==14&&g.Stats().ownedItems==1,"keyboard purchase creates one ECS stack and debits once");
    Check(g.BuyShopItem(0)==ShopResult::Sold&&g.ExtractBuild().materials==14,"sold card cannot debit twice");
    Check(g.BuyShopItem(1)==ShopResult::Bought&&g.ExtractBuild().items[0]==2&&g.Stats().ownedItems==1&&g.ExtractBuild().stats.bonusDamage==2,"duplicate item stacks on the existing entity");
}

void CapacityAndRejectedPrices() {
    auto c=Short();c.maxOwnedItems=0;GameModule g(c);Start(g);UntilShop(g);Stock(g,0,ItemKind::Cake);
    Check(g.BuyShopItem(0)==ShopResult::Capacity&&g.ExtractBuild().materials==15&&!g.ExtractShop().shop.offers[0].sold,"capacity failure cannot spend money or sell stock");
    Item(g,ItemKind::Cake,99);Check(g.BuyShopItem(0)==ShopResult::StackFull&&g.ExtractBuild().materials==15,"full stack rejects another purchase");
    Stock(g,0,ItemKind::Lens,0);Check(g.BuyShopItem(0)==ShopResult::Invalid&&g.ExtractBuild().materials==15,"corrupt price cannot mint free items");
    c.maxOwnedItems=1;GameModule external(c);Start(external);UntilShop(external);Item(external,ItemKind::Lens);
    Stock(external,0,ItemKind::Coffee);Check(external.BuyShopItem(0)==ShopResult::Capacity,"capacity counts foreign item archetypes");
    Stock(external,0,ItemKind::Lens);Check(external.BuyShopItem(0)==ShopResult::Bought&&external.Stats().ownedItems==1,"stacking needs no extra capacity");
}

void RerollsLocksAndLifecycle() {
    GameModule g(Short());Start(g);UntilShop(g);g.Get<Player>(g.PlayerEntity()).materials=100;
    Check(g.ToggleShopLock(3)==ShopResult::Locked,"stock can be locked");const auto locked=g.ExtractShop().shop.offers[3];
    Check(g.RerollShop()==ShopResult::Rerolled&&g.ExtractBuild().materials==98&&g.ExtractShop().rerollCost==3,"reroll debits current cost and increments next cost");
    Check(SameOffer(locked,g.ExtractShop().shop.offers[3]),"reroll preserves locked kind and price");
    for(std::size_t i=0;i<3;++i)g.ToggleShopLock(i);
    const auto before=g.ExtractShop();Check(g.RerollShop()==ShopResult::AllLocked,"fully locked shop refuses refresh");
    Check(g.ExtractBuild().materials==before.build.materials&&g.ExtractShop().shop.randomState==before.shop.randomState,"rejected reroll does not spend currency or RNG");
    g.ToggleShopLock(0);Stock(g,0,ItemKind::Cake);g.Get<Health>(g.PlayerEntity()).current=100;
    Check(g.BuyShopItem(0)==ShopResult::Bought&&g.ExtractBuild().health==110,"health item heals once at purchase");
    const auto build=g.ExtractBuild();const auto ticks=g.Stats().ticks;Input next;next.nextWave=true;g.Advance(.25,next);
    Check(g.GetState()==State::Playing&&g.Stats().ticks==ticks&&g.ExtractBuild().items==build.items&&g.ExtractBuild().health==build.health,"shop dismissal does not catch up time or clear the build");
    UntilShop(g);Check(g.ExtractShop().shop.visits==2&&g.ExtractBuild().materials==build.materials+18,"second wave grants its own budget once");
    Check(SameOffer(locked,g.ExtractShop().shop.offers[3]),"locked stock survives a wave without repricing");
    g.Restart();Check(g.Stats().ownedItems==0&&g.ExtractBuild().materials==0&&g.ExtractBuild().maximumHealth==150&&g.Get<Shop>(g.PlayerEntity()).visits==0,"restart clears inventory, wallet, stats and shop session");
    UntilShop(g);GameModule fresh(Short());Start(fresh);UntilShop(fresh);
    for(std::size_t i=0;i<ShopSlots;++i)Check(SameOffer(g.ExtractShop().shop.offers[i],fresh.ExtractShop().shop.offers[i]),"restart restores deterministic shop seed");
}

std::vector<glm::vec2> EnemyPositions(GameModule& g) {
    std::vector<glm::vec2> result;Query<Enemy,Transform> query;query.Refresh(*g.Scene());
    for(auto chunk:query)for(std::size_t i=0;i<chunk.count;++i)result.push_back(chunk.Get<Transform>()[i].position);
    std::sort(result.begin(),result.end(),[](auto a,auto b){return a.x==b.x?a.y<b.y:a.x<b.x;});return result;
}
void RandomIsolationAndStandaloneBuild() {
    auto c=Quiet();c.spawning=true;c.waveSeconds=3;GameModule a(c),b(c);Start(a);Start(b);
    a.Get<Player>(a.PlayerEntity()).experience=8;a.FixedTick();b.FixedTick();
    Check(a.SelectUpgrade(a.Get<Growth>(a.PlayerEntity()).choices[0]),"reward pool consumes only its own random stream");Tick(a,200);Tick(b,200);
    Check(!EnemyPositions(a).empty()&&EnemyPositions(a)==EnemyPositions(b)&&a.Stats().spawned==b.Stats().spawned,"reward draw leaves spawn positions and types unchanged");
    UntilShop(a);UntilShop(b);a.Get<Player>(a.PlayerEntity()).materials=100;for(int i=0;i<5;++i)a.RerollShop();a.BuyShopItem(0);
    a.NextWave();b.NextWave();Tick(a,200);Tick(b,200);
    Check(EnemyPositions(a)==EnemyPositions(b)&&a.Stats().spawned==b.Stats().spawned,"shop refresh and item creation leave combat RNG unchanged");
    GameWorld world(Quiet());world.scene=std::make_unique<ECS::Core::Scene>();
    auto players=Type<Player,BuildBase,Growth,CombatStats,Health>(*world.scene);world.player=world.scene->CreateEntity(players);
    auto items=Type<Extra,OwnedItem>(*world.scene);auto item=world.scene->CreateEntity(items);
    auto& owned=world.Get<OwnedItem>(item);owned.owner=world.player;owned.kind=ItemKind::Lens;owned.count=3;
    ECS::System::Context context;context.scene=world.scene.get();context.SetService(&world);ECS::System::Pipeline pipeline;pipeline.Add<BuildSystem>();
    Check(pipeline.Start(context)&&pipeline.Tick(context)&&world.Get<CombatStats>(world.player).bonusDamage==3,"independent BuildSystem aggregates ECS items without GameModule");pipeline.Stop(context);
}

void SessionSourceAndRevive() {
    auto c=Short();GameModule game(c);Start(game);Item(game,ItemKind::Cake);game.FixedTick();game.Get<Health>(game.PlayerEntity()).current=1;
    game.SpawnHostileProjectile({0,0},{},8);game.FixedTick();Check(game.GetState()==State::Dead,"item build can still die");game.Revive();
    Check(game.ExtractBuild().health==160&&game.ExtractBuild().items[std::size_t(ItemKind::Cake)]==1,"revive restores the modified maximum and keeps items");
    Config source=c;source.expanded=source.builds=false;GameModule old(source);Start(old);Tick(old,8);
    Check(old.GetState()==State::WaveComplete&&old.Get<Player>(old.PlayerEntity()).materials==0&&old.BuyShopItem(0)==ShopResult::Invalid,"source mode never grants a budget or opens shop");
    SessionModule session(c);Check(session.Startup(),"session startup");
    const auto begin=[&](){Check(session.Navigate(Screen::CharacterSelect)&&session.Navigate(Screen::WeaponSelect)&&session.SelectWeapon(WeaponKind::Wand)&&session.Navigate(Screen::DifficultySelect)&&session.Navigate(Screen::MapSelect)&&session.StartRun(),"start a real session run");};
    begin();UntilShop(*session.Game());Stock(*session.Game(),0,ItemKind::Lens);session.Game()->BuyShopItem(0);
    Check(session.ReturnHome()&&!session.Game(),"return home destroys the item/shop scene");begin();
    Check(session.Game()->ExtractBuild().items[0]==0&&session.Game()->ExtractBuild().materials==0,"new run cannot inherit prior items or currency");
}

void NaturalTwoWaveBuild() {
    for(auto weapon:{WeaponKind::Wand,WeaponKind::Burst}) {
        auto c=ExpandedGameplay();c.arsenal=false;c.initialWeapon=weapon;GameModule g(c);Start(g);
        const glm::vec2 route[]{{c.maximum.x-2,c.maximum.y-2},{c.minimum.x+2,c.maximum.y-2},
            {c.minimum.x+2,c.minimum.y+2},{c.maximum.x-2,c.minimum.y+2}};
        std::size_t waypoint=0;
        for(int wave=1;wave<=2;++wave) {
            int limit=5000;
            while(limit-->0&&(g.GetState()==State::Playing||g.GetState()==State::LevelUp)) {
                if(g.GetState()==State::LevelUp) {
                    const auto choices=g.Get<Growth>(g.PlayerEntity()).choices;
                    UpgradeKind chosen=choices[0];
                    for(auto priority:{UpgradeKind::Damage,UpgradeKind::Armor,UpgradeKind::MaxHealth,UpgradeKind::AttackSpeed,UpgradeKind::MoveSpeed,UpgradeKind::PickupRange})
                        if(std::find(choices.begin(),choices.end(),priority)!=choices.end()){chosen=priority;break;}
                    Check(g.SelectUpgrade(chosen),"natural run chooses only an offered reward");continue;
                }
                auto delta=route[waypoint]-g.Get<Transform>(g.PlayerEntity()).position;
                if(glm::length(delta)<.35f){waypoint=(waypoint+1)%std::size(route);delta=route[waypoint]-g.Get<Transform>(g.PlayerEntity()).position;}
                const auto direction=glm::normalize(delta);g.FixedTick({direction.x,direction.y});
            }
            const auto build=g.ExtractBuild();
            std::cout<<"[build survival] weapon="<<int(weapon)<<" wave="<<wave<<" hp="<<build.health<<"/"<<build.maximumHealth
                     <<" kills="<<g.Stats().kills<<" materials="<<build.materials<<" items="<<g.Stats().ownedItems<<'\n';
            Check(g.GetState()==State::Shop&&build.health>0,"ordinary moving run survives a whole wave into shop");
            if(wave==1) {
                int purchased=0;
                for(auto preferred:{ItemKind::Cake,ItemKind::Plant,ItemKind::Vest,ItemKind::Lens,ItemKind::Coffee,ItemKind::Beanie})
                    for(std::size_t slot=0;slot<ShopSlots;++slot)if(g.ExtractShop().shop.offers[slot].kind==preferred&&g.BuyShopItem(slot)==ShopResult::Bought)++purchased;
                Check(purchased>0,"normal wave earnings buy a real item before wave two");g.NextWave();
            }
        }
        Check(g.Stats().ownedItems>0&&g.Get<Player>(g.PlayerEntity()).level>0,"full loop retains real items and earned levels");
    }
}
}
int main(){
    RegisterComponents();REGISTER_COMPONENT("brotato_build_extra",Extra);
    const std::pair<const char*,std::function<void()>> tests[]{
        {"reward pool and slot input",RewardPoolAndInput},{"attributes and foreign archetypes",AttributesAndForeignArchetypes},
        {"actual combat and caps",ActualCombatAndCaps},{"shop freeze and transactions",ShopFreezeAndTransactions},
        {"capacity and rejected prices",CapacityAndRejectedPrices},{"rerolls, locks and lifecycle",RerollsLocksAndLifecycle},
        {"random isolation and standalone build",RandomIsolationAndStandaloneBuild},{"session, source and revive",SessionSourceAndRevive},
        {"natural two-wave build",NaturalTwoWaveBuild},
    };
    for(const auto& [name,test]:tests)try{test();std::cout<<"[pass] "<<name<<'\n';}catch(const std::exception& e){std::cerr<<"[fail] "<<name<<": "<<e.what()<<'\n';return 1;}
}
