#include "Brotato/Public/game_module.h"
#include "Brotato/Public/expanded_gameplay.h"
#include "Brotato/Public/weapon_geometry.h"
#include "Brotato/Systems/game_systems.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
using namespace Brotato;
struct Extra:Component<Extra>{int value=41;};
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void Near(float a,float b,const char* message){Check(std::abs(a-b)<1e-4f,message);}
Config Short(){auto c=ExpandedGameplay();c.campaign=false;c.traits=false;c.playerStart={0,0};c.minimum={-30,-30};c.maximum={30,30};c.spawning=c.armed=false;c.enemySpeed=0;c.contactDamage=0;c.waveSeconds=.05f;c.waveIncrement=0;return c;}
void Start(GameModule& game){Check(game.Startup(),game.Error().c_str());}
void ToShop(GameModule& game){for(int i=0;i<100&&game.GetState()==State::Playing;++i)game.FixedTick();Check(game.GetState()==State::Shop,"shop opens");}
void Stock(GameModule& g,WeaponKind kind,int tier=0,int price=1,std::size_t card=0){ShopOffer o;o.type=OfferType::Weapon;o.weapon=kind;o.tier=tier;o.price=price;o.sold=false;g.Get<Shop>(g.PlayerEntity()).offers[card]=o;}
void Buy(GameModule& g,WeaponKind kind,int tier=0){Stock(g,kind,tier);Check(g.BuyShopOffer(0)==ShopResult::Bought,"weapon bought");}
template<class... T>auto Type(ECS::Core::Scene& scene){auto d=scene.CreateArchTypeDescription();(d->AddComponentArray<T>(),...);return scene.CreateArchType(d,2);}
ECS::EntityHandle External(GameModule& g,ECS::EntityHandle owner,WeaponKind kind,int tier,int slot){auto type=Type<Weapon,Transform,Sprite,Damage,Extra>(*g.Scene());auto e=g.Scene()->CreateEntity(type);auto& w=g.Get<Weapon>(e);w.owner=owner;w.kind=kind;w.tier=tier;w.equipmentSlot=slot;return e;}
void PurchasesAndSlots(){
    GameModule g(Short());Start(g);ToShop(g);auto first=g.ExtractEquipment();Check(first.count==1&&first.slots[0].present&&first.slots[0].tier==0,"starter is ECS slot zero");
    Stock(g,WeaponKind::Gun,0,20);const auto rng=g.ExtractShop().shop.randomState;
    Check(g.BuyShopOffer(0)==ShopResult::NotEnough&&g.ExtractBuild().materials==15&&g.ExtractEquipment().count==1,"insufficient funds atomic");
    Check(g.ExtractShop().shop.randomState==rng&&!g.ExtractShop().shop.offers[0].sold,"failed buy retains stock and RNG");
    Stock(g,WeaponKind::Gun,1,14);Input input;input.buySlot=1;auto ticks=g.Stats().ticks;g.FixedTick(input);
    auto inventory=g.ExtractEquipment();Check(inventory.count==2&&inventory.slots[1].kind==WeaponKind::Gun&&inventory.slots[1].tier==1&&g.ExtractBuild().materials==1,"slot input allocates ranked weapon and debits");
    Check(g.Stats().ticks==ticks&&g.BuyShopOffer(0)==ShopResult::Sold&&g.ExtractBuild().materials==1,"shop frozen and sold card cannot debit twice");
    g.Get<Player>(g.PlayerEntity()).materials=50;for(int i=0;i<4;++i)Buy(g,WeaponKind::Wand);
    Check(g.ExtractEquipment().count==6&&g.Stats().weapons==6,"six real weapon entities fill equipment");
    Stock(g,WeaponKind::Wand);const auto money=g.ExtractBuild().materials;
    Check(g.BuyShopOffer(0)==ShopResult::Capacity&&g.ExtractBuild().materials==money&&!g.ExtractShop().shop.offers[0].sold,"full slots require explicit merge or sell");
    g.NextWave();Check(g.SelectWeaponSlot(1)&&g.CurrentWeapon()==WeaponKind::Gun,"combat slot selection changes focus");
    Check(g.Stats().weapons==6&&!g.EquipWeapon(WeaponKind::Torch),"selection cannot spawn free weapons");
}
void MergeAndSale(){
    GameModule g(Short());Start(g);ToShop(g);Buy(g,WeaponKind::Wand);auto before=g.ExtractEquipment();auto keep=before.slots[0].entity,retire=before.slots[1].entity;const auto money=g.ExtractBuild().materials;
    Check(g.MergeWeapon(0)==ShopResult::Merged&&g.Scene()->IsAlive(keep)&&!g.Scene()->IsAlive(retire),"merge keeps selected generation and retires duplicate");
    Check(g.ExtractEquipment().count==1&&g.ExtractEquipment().slots[0].tier==1&&g.ExtractBuild().materials==money,"merge upgrades once without payment");
    Check(g.MergeWeapon(0)==ShopResult::NoMatch&&g.SellWeapon(0)==ShopResult::LastWeapon,"repeat merge and last weapon sale rejected");
    Buy(g,WeaponKind::Wand,1);Check(g.MergeWeapon(0)==ShopResult::Merged,"two tier two make tier three");
    Buy(g,WeaponKind::Wand,2);Check(g.MergeWeapon(0)==ShopResult::Merged&&g.ExtractEquipment().slots[0].tier==3,"highest tier reached");
    Buy(g,WeaponKind::Wand,3);Check(g.MergeWeapon(0)==ShopResult::MaxTier&&g.ExtractEquipment().count==2,"highest tier cannot merge");
    const int value=g.ExtractEquipment().slots[0].sellPrice;const auto wallet=g.ExtractBuild().materials;
    Check(g.SellWeapon(0)==ShopResult::WeaponSold&&!g.Scene()->IsAlive(keep),"selected weapon sold and focus repaired");
    Check(g.ExtractBuild().materials==wallet+value&&g.Scene()->IsAlive(g.WeaponEntity())&&g.ExtractEquipment().count==1,"sale pays half base tier price");
    Check(g.SellWeapon(0)!=ShopResult::WeaponSold&&g.ExtractBuild().materials==wallet+value,"stale slot cannot sell twice");
    g.NextWave();g.Restart();Check(g.ExtractEquipment().count==1&&g.ExtractEquipment().slots[0].tier==0&&g.ExtractBuild().materials==0,"restart restores one base weapon");
}
void ForeignArchetypesAndOwner(){
    auto c=Short();c.maxWeapons=2;GameModule g(c);Start(g);ToShop(g);
    auto foreign=External(g,g.PlayerEntity(),WeaponKind::Wand,0,1);
    Check(g.ExtractEquipment().count==2&&g.ExtractEquipment().slots[0].canMerge,"foreign archetype joins inventory and merge");
    Stock(g,WeaponKind::Gun);Check(g.BuyShopOffer(0)==ShopResult::Capacity,"global capacity includes other archetypes");
    Check(g.MergeWeapon(0)==ShopResult::Merged&&!g.Scene()->IsAlive(foreign),"merge accepts foreign archetype");
    auto players=Type<Transform,Player>(*g.Scene());auto owner=g.Scene()->CreateEntity(players);
    External(g,owner,WeaponKind::Gun,0,2);Stock(g,WeaponKind::Gun);
    Check(g.ExtractEquipment().count==1&&g.BuyShopOffer(0)==ShopResult::Capacity,"other owner excluded from inventory but included in scene capacity");
    g.Scene()->DeleteEntity(owner);auto replacement=g.Scene()->CreateEntity(players);
    Check(replacement.GetID()==owner.GetID()&&!g.Scene()->IsAlive(owner),"owner generation recycled");
    Check(g.ExtractEquipment().count==1&&g.MergeWeapon(0)==ShopResult::NoMatch,"stale owner never rebinds or matches player");
    auto snapshot=g.ExtractEquipment();g.NextWave();g.Restart();Check(snapshot.slots[0].tier==1&&g.ExtractEquipment().slots[0].tier==0,"value snapshots do not alias components");
}
void RankedCombatAndCapacity(){
    auto c=Short();c.armed=true;c.waveSeconds=.15f;c.maxProjectiles=6;GameModule g(c);Start(g);ToShop(g);
    g.Get<Player>(g.PlayerEntity()).materials=100;Buy(g,WeaponKind::Gun,3);Buy(g,WeaponKind::Gun,0);
    auto inv=g.ExtractEquipment();Check(inv.slots[1].damage==3&&inv.slots[2].damage==1,"tier damage rounds up before owner bonus");Near(inv.slots[1].cooldown,.14f,"tier four cooldown factor");
    g.NextWave();auto enemy=g.SpawnEnemy({5,0},true,EnemyKind::Armored);g.Get<Health>(enemy).current=g.Get<Health>(enemy).maximum=1000;
    g.FixedTick();Check(g.Stats().shots==3&&g.Stats().projectiles==3,"all owned weapons independently fire in one tick");
    int tierDamage=0;Query<Projectile,Damage> projectiles;projectiles.Refresh(*g.Scene());for(auto chunk:projectiles)for(std::size_t row=0;row<chunk.count;++row)if(chunk.Get<Projectile>()[row].kind==WeaponKind::Gun)tierDamage+=chunk.Get<Damage>()[row].amount;
    Check(tierDamage==4,"projectiles snapshot each weapon rank");
    const auto& mounted=g.Get<Transform>(inv.slots[1].entity);Check(glm::length(mounted.position-g.Get<Transform>(g.PlayerEntity()).position)>.8f,"secondary mount changes physical origin");
    g.SetPaused(true);const auto cooldown=g.Get<Weapon>(inv.slots[1].entity).cooldown;g.Advance(.25);Near(g.Get<Weapon>(inv.slots[1].entity).cooldown,cooldown,"all cooldowns pause");g.SetPaused(false);
    for(int i=0;i<12;++i)g.FixedTick();Check(g.Get<Health>(enemy).current<1000,"ranked weapons actually hit");
    ToShop(g);Check(g.ExtractEquipment().count==3&&g.ExtractEquipment().slots[1].tier==3&&g.Stats().projectiles==0,"wave clears projectiles and retains ranks");
    c.maxProjectiles=3;GameModule cap(c);Start(cap);ToShop(cap);Buy(cap,WeaponKind::Burst);Buy(cap,WeaponKind::Gun);cap.NextWave();enemy=cap.SpawnEnemy({5,0},true,EnemyKind::Armored);cap.Get<Health>(enemy).current=1000;cap.FixedTick();
    Check(cap.Stats().shots==2&&cap.Stats().projectiles==2,"same-tick capacity skips entire four-shot burst without partial firing");
}
void MixedStockAndLocks(){
    GameModule a(Short()),b(Short());Start(a);Start(b);ToShop(a);ToShop(b);bool seenItem=false,seenWeapon=false;std::array<bool,4> tiers{};a.Get<Player>(a.PlayerEntity()).materials=b.Get<Player>(b.PlayerEntity()).materials=MaxMaterials;
    for(int wave=0;wave<12;++wave){for(int roll=0;roll<25;++roll){auto aa=a.ExtractShop(),bb=b.ExtractShop();Check(aa.shop.randomState==bb.shop.randomState,"stock RNG deterministic");for(std::size_t i=0;i<ShopSlots;++i){const auto& o=aa.shop.offers[i];const auto& p=bb.shop.offers[i];Check(o.type==p.type&&o.weapon==p.weapon&&o.kind==p.kind&&o.tier==p.tier&&o.price==p.price,"mixed offers reproducible");if(o.type==OfferType::Weapon){seenWeapon=true;tiers[o.tier]=true;}else seenItem=true;}a.RerollShop();b.RerollShop();}a.NextWave();b.NextWave();ToShop(a);ToShop(b);}
    Check(seenItem&&seenWeapon&&std::all_of(tiers.begin(),tiers.end(),[](bool value){return value;}),"items and four tiers are reachable across waves");
    Stock(a,WeaponKind::Gun,3,79);a.ToggleShopLock(0);a.RerollShop();a.NextWave();ToShop(a);const auto offer=a.ExtractShop().shop.offers[0];Check(offer.type==OfferType::Weapon&&offer.weapon==WeaponKind::Gun&&offer.tier==3&&offer.price==79&&offer.locked,"weapon rank and old price retained by cross-wave lock");
    Stock(a,WeaponKind::Gun,-1);const auto money=a.ExtractBuild().materials;Check(a.BuyShopOffer(0)==ShopResult::Invalid&&a.ExtractBuild().materials==money,"invalid rank rejected before mutation");
    auto source=Short();source.builds=source.expanded=false;GameModule old(source);Start(old);for(int i=0;i<6;++i)old.FixedTick();Check(old.GetState()==State::WaveComplete&&old.ExtractEquipment().count==0&&old.EquipWeapon(WeaponKind::Gun),"source six-choice behavior preserved");
}
void IndependentMeleeAttacks(){
    auto c=Short();c.armed=true;c.waveSeconds=.5f;GameModule g(c);Start(g);ToShop(g);Buy(g,WeaponKind::Torch);Buy(g,WeaponKind::Torch);g.NextWave();
    g.Get<Weapon>(g.WeaponEntity()).cooldowns[0]=100;
    auto enemy=g.SpawnEnemy({2,0},true,EnemyKind::Armored);g.Get<Health>(enemy).current=g.Get<Health>(enemy).maximum=100;
    for(int i=0;i<20;++i)g.FixedTick();
    Check(g.Stats().shots==2&&g.Stats().hits==2&&g.Get<Health>(enemy).current==94,"two torches each hit once without shared deduplication");
    const auto inventory=g.ExtractEquipment();const auto before=g.Get<Transform>(inventory.slots[1].entity).position;
    g.Get<Transform>(g.PlayerEntity()).position={5,5};g.SetPaused(true);g.FixedTick();
    Check(g.Get<Transform>(inventory.slots[1].entity).position==before,"pause freezes every mounted weapon");
}
void NaturalTwoWaveArsenal(){
    auto c=ExpandedGameplay();c.campaign=false;c.traits=false;GameModule g(c);Start(g);
    const glm::vec2 route[]{{c.maximum.x-2,c.maximum.y-2},{c.minimum.x+2,c.maximum.y-2},{c.minimum.x+2,c.minimum.y+2},{c.maximum.x-2,c.minimum.y+2}};
    std::size_t waypoint=0;
    for(int wave=1;wave<=2;++wave){
        int limit=6000;
        while(limit-->0&&(g.GetState()==State::Playing||g.GetState()==State::LevelUp)){
            if(g.GetState()==State::LevelUp){const auto choices=g.Get<Growth>(g.PlayerEntity()).choices;auto chosen=choices[0];for(auto priority:{UpgradeKind::Damage,UpgradeKind::Armor,UpgradeKind::MaxHealth,UpgradeKind::AttackSpeed,UpgradeKind::MoveSpeed,UpgradeKind::PickupRange})if(std::find(choices.begin(),choices.end(),priority)!=choices.end()){chosen=priority;break;}Check(g.SelectUpgrade(chosen),"natural offered reward selected");continue;}
            auto delta=route[waypoint]-g.Get<Transform>(g.PlayerEntity()).position;if(glm::length(delta)<.35f){waypoint=(waypoint+1)%std::size(route);delta=route[waypoint]-g.Get<Transform>(g.PlayerEntity()).position;}
            const auto direction=glm::normalize(delta);g.FixedTick({direction.x,direction.y});
        }
        Check(g.GetState()==State::Shop&&g.ExtractBuild().health>0,"ordinary moving arsenal survives wave");
        if(wave==1){
            bool purchased=false;const auto offers=g.ExtractShop().shop.offers;
            for(auto preferred:{WeaponKind::Gun,WeaponKind::Burst,WeaponKind::Wand,WeaponKind::Laser,WeaponKind::Knife,WeaponKind::Torch}){
                for(std::size_t i=0;i<ShopSlots&&!purchased;++i)if(offers[i].type==OfferType::Weapon&&offers[i].weapon==preferred&&g.BuyShopOffer(i)==ShopResult::Bought)purchased=true;
                if(purchased)break;
            }
            Check(purchased&&g.ExtractEquipment().count==2,"ordinary earned materials purchase a naturally offered second weapon");
            for(std::size_t i=0;i<ShopSlots;++i)if(offers[i].type==OfferType::Item)g.BuyShopOffer(i);
            g.NextWave();
        }
    }
    const auto build=g.ExtractBuild();std::cout<<"[arsenal survival] waves=2 hp="<<build.health<<"/"<<build.maximumHealth<<" kills="<<g.Stats().kills<<" materials="<<build.materials<<" weapons="<<g.ExtractEquipment().count<<" shots="<<g.Stats().shots<<'\n';
    Check(g.ExtractEquipment().count==2&&g.Get<Player>(g.PlayerEntity()).level>0,"natural two-wave loop retains weapons and growth");
}
}
int main(){try{RegisterComponents();REGISTER_COMPONENT("brotato_arsenal_extra",Extra);const std::pair<const char*,std::function<void()>> tests[]{{"purchase slots and frozen inputs",PurchasesAndSlots},{"merge sale and restart",MergeAndSale},{"foreign archetypes owner and generation",ForeignArchetypesAndOwner},{"ranked concurrent combat and capacity",RankedCombatAndCapacity},{"mixed stock rarity locks and source",MixedStockAndLocks},{"independent melee attacks",IndependentMeleeAttacks},{"natural two-wave arsenal",NaturalTwoWaveArsenal}};for(const auto& [name,test]:tests){test();std::cout<<"[PASS] "<<name<<'\n';}std::cout<<"7 arsenal groups passed\n";return 0;}catch(const std::exception& error){std::cerr<<"[FAIL] "<<error.what()<<'\n';return 1;}}
