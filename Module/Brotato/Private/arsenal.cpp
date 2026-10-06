#include "Brotato/Public/game_world.h"
#include "Brotato/Public/weapon_geometry.h"
#include <algorithm>
#include <cmath>

namespace Brotato {
bool UsesArsenal(const GameWorld& world) { return world.config.builds && world.config.arsenal; }
int WeaponPrice(WeaponKind kind,int tier) {
    constexpr int base[]{10,10,12,10,14,12};
    return ValidWeapon(kind) && tier>=0 && tier<4 ? base[WeaponIndex(kind)]*(1<<tier) : 0;
}
int RankedDamage(GameWorld& world,const Weapon& weapon) {
    const int tier=UsesArsenal(world)?std::clamp(weapon.tier,0,3):0;
    const auto* stats=world.scene->TryGetComponent<CombatStats>(weapon.owner);
    const auto base=std::int64_t(world.Definition(weapon.kind).damage);
    return int(std::clamp<std::int64_t>((base*(2+tier)+1)/2+(stats?std::int64_t(stats->bonusDamage)+stats->weaponDamage[WeaponIndex(weapon.kind)]:0),1,100000));
}
float RankedCooldown(GameWorld& world,const Weapon& weapon) {
    const auto* stats=world.scene->TryGetComponent<CombatStats>(weapon.owner);
    const int pairs=UsesTraits(world) && stats && Family(weapon.kind)==WeaponFamily::Ballistic?stats->families[std::size_t(WeaponFamily::Ballistic)]/2:0;
    return world.Definition(weapon.kind).cooldown*(1-.1f*(UsesArsenal(world)?std::clamp(weapon.tier,0,3):0))/(1+.1f*pairs);
}
glm::vec2 WeaponMount(const Weapon& weapon) {
    constexpr glm::vec2 mounts[]{{0,0},{-.85f,.45f},{.85f,.45f},{-.85f,-.45f},{.85f,-.45f},{0,-.9f}};
    return weapon.equipmentSlot>=0 && weapon.equipmentSlot<int(EquipmentSlots)?mounts[weapon.equipmentSlot]:glm::vec2(0);
}
namespace {
bool Owned(GameWorld& world,const Weapon& weapon,ECS::EntityHandle owner) {
    return world.scene->IsAlive(owner) && world.scene->IsAlive(weapon.owner) && weapon.owner.GetID()==owner.GetID();
}
ECS::EntityHandle AtSlot(GameWorld& world,ECS::EntityHandle owner,std::size_t slot) {
    ECS::EntityHandle result(0);
    Query<Weapon,Transform,Sprite,Damage> query; query.Refresh(*world.scene);
    for(auto chunk:query) for(std::size_t row=0;row<chunk.count;++row) {
        const auto& weapon=chunk.Get<Weapon>()[row];
        const auto entity=chunk.Entity(row,*world.scene);
        if(weapon.equipmentSlot==int(slot) && Owned(world,weapon,owner) && ValidWeapon(weapon.kind) &&
           (result.GetID()==0 || entity.GetID()<result.GetID())) result=entity;
    }
    return result;
}
ECS::EntityHandle Match(GameWorld& world,ECS::EntityHandle selected) {
    const auto value=world.Get<Weapon>(selected); // Copy relationships and rank, never borrow across mutation.
    ECS::EntityHandle result(0);
    Query<Weapon,Transform,Sprite,Damage> query; query.Refresh(*world.scene);
    for(auto chunk:query) for(std::size_t row=0;row<chunk.count;++row) {
        const auto& weapon=chunk.Get<Weapon>()[row]; const auto entity=chunk.Entity(row,*world.scene);
        if(entity.GetID()!=selected.GetID() && Owned(world,weapon,value.owner) && weapon.kind==value.kind && weapon.tier==value.tier &&
           weapon.equipmentSlot>=0 && weapon.equipmentSlot<int(EquipmentSlots) &&
           (result.GetID()==0 || entity.GetID()<result.GetID())) result=entity;
    }
    return result;
}
ShopResult Feedback(GameWorld& world,ShopResult result) {
    if(world.scene && world.state==State::Shop)
        if(auto* shop=world.scene->TryGetComponent<Shop>(world.player)) shop->feedback=result;
    return result;
}
void RepairFocus(GameWorld& world) {
    const auto inventory=ExtractEquipment(world,world.player);
    for(const auto& entry:inventory.slots) if(entry.present) {
        world.weapon=entry.entity; world.selected=entry.kind; return;
    }
}
}
void InitializeEquipment(GameWorld& world,ECS::EntityHandle entity,ECS::EntityHandle owner,WeaponKind kind,int tier,int slot,WeaponAffix affix) {
    auto& weapon=world.Get<Weapon>(entity); weapon=Weapon{}; weapon.owner=owner;weapon.kind=kind;weapon.tier=tier;weapon.equipmentSlot=slot;
    weapon.affix=affix;weapon.randomState=world.config.seed^(std::uint32_t(slot+1)*0x85ebca6bu)^(std::uint32_t(kind)*0xc2b2ae35u)^(std::uint32_t(tier)*0x27d4eb2fu);
    weapon.angle=Geometry(kind).idleAngle;weapon.reflected=Geometry(kind).idleReflected;
    const auto home=WeaponRoot(world.Get<Transform>(owner).position+WeaponMount(weapon),kind);
    auto& transform=world.Get<Transform>(entity);transform.position=transform.previous=home;
    auto& sprite=world.Get<Sprite>(entity);sprite=Sprite{};sprite.image=world.Definition(kind).image;sprite.size=world.Definition(kind).size;sprite.angle=weapon.angle;
    world.Get<Damage>(entity)=AttackPayload(world,owner,kind);
    world.Get<Damage>(entity).amount=RankedDamage(world,weapon);
}
EquipmentSnapshot ExtractEquipment(GameWorld& world,ECS::EntityHandle owner) {
    EquipmentSnapshot result;
    if(!world.scene || !UsesArsenal(world)) return result;
    Query<Weapon,Transform,Sprite,Damage> query;query.Refresh(*world.scene);
    for(auto chunk:query) for(std::size_t row=0;row<chunk.count;++row) {
        const auto& weapon=chunk.Get<Weapon>()[row];
        if(!Owned(world,weapon,owner) || !ValidWeapon(weapon.kind) || weapon.equipmentSlot<0 || weapon.equipmentSlot>=int(EquipmentSlots)) continue;
        ++result.count;
        auto& entry=result.slots[weapon.equipmentSlot];const auto entity=chunk.Entity(row,*world.scene);
        if(entry.present && entry.entity.GetID()<entity.GetID())continue;
        entry.entity=entity;entry.kind=weapon.kind;entry.tier=weapon.tier;entry.present=true;
        entry.affix=weapon.affix;
        entry.damage=RankedDamage(world,weapon);entry.sellPrice=std::max(1,WeaponPrice(weapon.kind,weapon.tier)/2);
        const auto* stats=world.scene->TryGetComponent<CombatStats>(owner);
        entry.cooldown=RankedCooldown(world,weapon)/(stats?std::max(1.f,stats->attackSpeed):1.f);
        entry.canMerge=weapon.tier>=0 && weapon.tier<3 && Match(world,entity).GetID()!=0;
    }
    return result;
}
bool SelectWeaponSlot(GameWorld& world,std::size_t slot) {
    if(!world.scene || !UsesArsenal(world) || slot>=EquipmentSlots || world.state==State::Shop || world.state==State::LevelUp)return false;
    const auto entity=AtSlot(world,world.player,slot);if(!entity.GetID())return false;
    world.weapon=entity;world.selected=world.Get<Weapon>(entity).kind;return true;
}
void ResetEquipment(GameWorld& world) {
    if(!UsesArsenal(world))return;
    {
        Query<Weapon,Transform,Sprite,Damage> query;query.Refresh(*world.scene);
        for(auto chunk:query)for(std::size_t row=0;row<chunk.count;++row)
            if(Owned(world,chunk.Get<Weapon>()[row],world.player) && chunk.Entity(row,*world.scene).GetID()!=world.weapon.GetID())
                world.commands->Destroy(chunk.Entity(row,*world.scene));
    }
    world.commands->Playback();
    if(!world.scene->IsAlive(world.weapon))world.weapon=world.scene->CreateEntity(world.weaponType);
    InitializeEquipment(world,world.weapon,world.player,world.selected,0,0);
}
ShopResult BuyShopOffer(GameWorld& world,std::size_t slot) {
    if(!world.scene || world.state!=State::Shop || slot>=ShopSlots || !world.config.builds)return Feedback(world,ShopResult::Invalid);
    const auto offer=world.Get<Shop>(world.player).offers[slot];
    if(offer.type==OfferType::Item)return BuyShopItem(world,slot);
    if(!UsesArsenal(world) || offer.type!=OfferType::Weapon || !ValidAffix(offer.weapon,offer.affix) || offer.tier<0 || offer.tier>3 || offer.price<=0 || offer.price>10000)
        return Feedback(world,ShopResult::Invalid);
    if(offer.sold)return Feedback(world,ShopResult::Sold);
    if(world.Get<Player>(world.player).materials<offer.price)return Feedback(world,ShopResult::NotEnough);
    const auto inventory=ExtractEquipment(world,world.player);
    if(inventory.count>=EquipmentSlots || Count<Weapon>(world)>=world.config.maxWeapons)return Feedback(world,ShopResult::Capacity);
    std::size_t free=0;while(free<EquipmentSlots && inventory.slots[free].present)++free;
    if(free==EquipmentSlots)return Feedback(world,ShopResult::Capacity);
    // Allocation completes before wallet/stock mutation; nothing borrowed crosses creation.
    const auto entity=world.scene->CreateEntity(world.weaponType);
    InitializeEquipment(world,entity,world.player,offer.weapon,offer.tier,int(free),offer.affix);
    RecomputeBuild(world,world.player);
    world.Get<Player>(world.player).materials-=offer.price;
    auto& bought=world.Get<Shop>(world.player).offers[slot];bought.sold=true;bought.locked=false;
    RefreshCounts(world);return Feedback(world,ShopResult::Bought);
}
ShopResult MergeWeapon(GameWorld& world,std::size_t slot) {
    if(!world.scene || !UsesArsenal(world) || world.state!=State::Shop || slot>=EquipmentSlots)return Feedback(world,ShopResult::Invalid);
    const auto selected=AtSlot(world,world.player,slot);if(!selected.GetID())return Feedback(world,ShopResult::Invalid);
    const auto value=world.Get<Weapon>(selected);
    if(value.tier>=3)return Feedback(world,ShopResult::MaxTier);
    if(value.tier<0)return Feedback(world,ShopResult::Invalid);
    const auto other=Match(world,selected);if(!other.GetID())return Feedback(world,ShopResult::NoMatch);
    world.scene->DeleteEntity(other); // Query and component copies above have ended their borrow scopes.
    const int tier=value.tier+1;
    InitializeEquipment(world,selected,value.owner,value.kind,tier,value.equipmentSlot,value.affix);
    RecomputeBuild(world,world.player);
    if(!world.scene->IsAlive(world.weapon)) {world.weapon=selected;world.selected=value.kind;}
    RefreshCounts(world);return Feedback(world,ShopResult::Merged);
}
ShopResult SellWeapon(GameWorld& world,std::size_t slot) {
    if(!world.scene || !UsesArsenal(world) || world.state!=State::Shop || slot>=EquipmentSlots)return Feedback(world,ShopResult::Invalid);
    const auto inventory=ExtractEquipment(world,world.player);
    if(inventory.count<=1)return Feedback(world,ShopResult::LastWeapon);
    const auto selected=AtSlot(world,world.player,slot);if(!selected.GetID())return Feedback(world,ShopResult::Invalid);
    const auto value=world.Get<Weapon>(selected);
    const int price=WeaponPrice(value.kind,value.tier)/2;if(price<=0)return Feedback(world,ShopResult::Invalid);
    world.scene->DeleteEntity(selected);
    RecomputeBuild(world,world.player);
    auto& player=world.Get<Player>(world.player);player.materials=int(std::min<std::int64_t>(MaxMaterials,std::int64_t(player.materials)+price));
    if(!world.scene->IsAlive(world.weapon))RepairFocus(world);
    RefreshCounts(world);return Feedback(world,ShopResult::WeaponSold);
}
} // namespace Brotato
