#include "Brotato/Systems/game_systems.h"
#include <algorithm>
#include <cmath>

namespace Brotato {
namespace {
bool Owns(GameWorld& world, const OwnedItem& item, ECS::EntityHandle owner) {
    return world.scene->IsAlive(owner) && world.scene->IsAlive(item.owner) && item.owner.GetID()==owner.GetID();
}
std::array<int,ItemCount> ItemCounts(GameWorld& world, ECS::EntityHandle owner) {
    std::array<int,ItemCount> counts{};
    if (!world.scene) return counts;
    Query<OwnedItem> query; query.Refresh(*world.scene);
    for (auto chunk:query) for(std::size_t row=0;row<chunk.count;++row) {
        const auto& item=chunk.Get<OwnedItem>()[row]; const auto kind=std::size_t(item.kind);
        if(kind<ItemCount && item.count>0 && Owns(world,item,owner))
            counts[kind]=int(std::min<std::int64_t>(MaxItemStack,std::int64_t(counts[kind])+item.count));
    }
    return counts;
}
void RollOffer(GameWorld& world,Shop& shop, ShopOffer& offer) {
    offer={};
    if(UsesArsenal(world) && BuildRandom(shop.randomState)%2) {
        offer.type=OfferType::Weapon;offer.weapon=WeaponKind(BuildRandom(shop.randomState)%WeaponCount);
        const auto rank=BuildRandom(shop.randomState)%100;
        offer.tier=shop.visits>=9 && rank<5?3:shop.visits>=5 && rank<15?2:shop.visits>=2 && rank<35?1:0;
        offer.price=WeaponPrice(offer.weapon,offer.tier)+std::min(shop.visits/2,50);
        if(UsesTraits(world)) {
            const auto draw=BuildRandom(shop.randomState)%10;
            offer.affix=draw<6?WeaponAffix::None:WeaponAffix(draw-5);
            if(!ValidAffix(offer.weapon,offer.affix))offer.affix=WeaponAffix::None;
            if(offer.affix!=WeaponAffix::None)offer.price+=3;
        }
    } else {
        offer.kind=ItemKind(BuildRandom(shop.randomState)%(UsesTraits(world)?ItemCount:LegacyItemCount));
        offer.price=ItemDefinitions[std::size_t(offer.kind)].price+std::min(shop.visits/2,50);
    }
    offer.sold=false;
}
Shop* CurrentShop(GameWorld& world) {
    if(!world.scene || !world.config.builds || world.state!=State::Shop) return nullptr;
    return world.scene->TryGetComponent<Shop>(world.player);
}
ShopResult Feedback(GameWorld& world,ShopResult result) {
    if(auto* shop=CurrentShop(world)) shop->feedback=result;
    return result;
}
}

void PrepareGrowthChoices(GameWorld& world, Growth& growth) {
    growth.choices={UpgradeKind::Damage,UpgradeKind::AttackSpeed,UpgradeKind::MoveSpeed};
    if(!world.config.builds) return;
    std::array<UpgradeKind,UpgradeCount> pool{};
    for(std::size_t i=0;i<pool.size();++i) pool[i]=UpgradeKind(i);
    for(std::size_t i=0;i<growth.choices.size();++i) {
        const auto selected=i+BuildRandom(growth.randomState)%(pool.size()-i);
        std::swap(pool[i],pool[selected]); growth.choices[i]=pool[i];
    }
}

void RecomputeBuild(GameWorld& world,ECS::EntityHandle owner) {
    if(!world.config.builds || !world.scene) return;
    auto* stats=world.scene->TryGetComponent<CombatStats>(owner);
    auto* base=world.scene->TryGetComponent<BuildBase>(owner);
    auto* growth=world.scene->TryGetComponent<Growth>(owner);
    auto* health=world.scene->TryGetComponent<Health>(owner);
    if(!stats || !base || !growth || !health) return;
    const auto items=ItemCounts(world,owner);
    std::int64_t damage=0, hp=base->maximumHealth, armor=0;
    double attack=1,move=1,pickup=1,regen=0,critical=0,steal=0;
    std::int64_t pierce=0,burning=0;
    const auto add=[&](const AttributeBonus& b,int count) {
        damage+=std::int64_t(b.damage)*count; hp+=std::int64_t(b.health)*count; armor+=std::int64_t(b.armor)*count;
        attack+=double(b.attackSpeed)*count; move+=double(b.moveSpeed)*count;
        pickup+=double(b.pickupRange)*count; regen+=double(b.regeneration)*count;
        critical+=double(b.criticalChance)*count;steal+=double(b.lifeSteal)*count;
        pierce+=std::int64_t(b.pierce)*count;burning+=std::int64_t(b.burning)*count;
    };
    const auto& profile=CharacterRules(world,owner);add(profile.bonus,1);
    stats->weaponDamage=profile.weaponDamage;stats->elementalBurning=profile.elementalBurning;
    for(std::size_t i=0;i<UpgradeCount;++i) add(UpgradeDefinitions[i].bonus,std::clamp(growth->upgrades[i],0,100000));
    for(std::size_t i=0;i<ItemCount;++i) add(ItemDefinitions[i].bonus,items[i]);
    stats->bonusDamage=int(std::clamp<std::int64_t>(damage,0,10000)); stats->armor=int(std::clamp<std::int64_t>(armor,0,20));
    stats->attackSpeed=float(std::clamp(attack,.25,5.0)); stats->moveSpeed=float(std::clamp(move,.25,3.0));
    stats->pickupRange=float(std::clamp(pickup,1.0,4.0)); stats->regeneration=float(std::clamp(regen,0.0,20.0));
    stats->criticalChance=UsesTraits(world)?float(std::clamp(critical,0.0,.6)):0;
    stats->lifeSteal=UsesTraits(world)?float(std::clamp(steal,0.0,.5)):0;
    stats->pierce=UsesTraits(world)?int(std::clamp<std::int64_t>(pierce,0,3)):0;
    stats->burning=UsesTraits(world)?int(std::clamp<std::int64_t>(burning,0,20)):0;
    stats->families={};
    if(UsesTraits(world)) {
        Query<Weapon,Transform,Sprite,Damage> equipped;equipped.Refresh(*world.scene);
        for(auto chunk:equipped)for(std::size_t row=0;row<chunk.count;++row) {
            const auto& weapon=chunk.Get<Weapon>()[row];
            if(world.scene->IsAlive(weapon.owner) && weapon.owner.GetID()==owner.GetID() && weapon.equipmentSlot>=0 && weapon.equipmentSlot<int(EquipmentSlots) && ValidWeapon(weapon.kind))
                stats->families[std::size_t(Family(weapon.kind))]=std::min(6,stats->families[std::size_t(Family(weapon.kind))]+1);
        }
    }
    const int maximum=int(std::clamp<std::int64_t>(hp,1,100000));
    // Preserve missing HP when the maximum grows; recomputing unchanged inputs
    // cannot grant another heal, and dead owners are never revived by an item.
    if(health->current>0) health->current=int(std::min<std::int64_t>(maximum,std::int64_t(health->current)+std::max(0,maximum-health->maximum)));
    health->maximum=maximum;
}

void BuildSystem::OnTick() {
    auto& world=*GetContext()->GetService<GameWorld>();
    if(!world.config.builds || world.state!=State::Playing) return;
    Query<Player,BuildBase,Growth,CombatStats,Health> query; query.Refresh(*world.scene);
    for(auto chunk:query) for(std::size_t row=0;row<chunk.count;++row) {
        RecomputeBuild(world,chunk.Entity(row,*world.scene));
        auto& base=chunk.Get<BuildBase>()[row]; auto& hp=chunk.Get<Health>()[row];
        if(hp.current<=0 || hp.current>=hp.maximum)base.lifeStealCredit=0;
        const auto& stats=chunk.Get<CombatStats>()[row];
        if(hp.current<=0 || hp.current>=hp.maximum || stats.regeneration<=0) {base.regenerationCredit=0;continue;}
        base.regenerationCredit+=stats.regeneration*float(SimulationStep);
        const int whole=int(std::floor(base.regenerationCredit+1e-5f));
        if(whole>0) {hp.current=std::min(hp.maximum,hp.current+whole);base.regenerationCredit=std::max(0.f,base.regenerationCredit-whole);}
    }
}

void ClearOwnedItems(GameWorld& world) {
    if(!world.scene || !world.commands) return;
    {
        Query<OwnedItem> query; query.Refresh(*world.scene);
        for(auto chunk:query) for(std::size_t row=0;row<chunk.count;++row) world.commands->Destroy(chunk.Entity(row,*world.scene));
    }
    world.commands->Playback();
}
void OpenShop(GameWorld& world) {
    if(!world.config.builds || world.state!=State::WaveComplete) return;
    auto& shop=world.Get<Shop>(world.player); ++shop.visits; shop.rerolls=0; shop.feedback=ShopResult::Ready;
    shop.waveBonus=15+int(std::min<std::int64_t>(1000,std::int64_t(world.stats.wave-1)*3));
    auto& player=world.Get<Player>(world.player);
    player.materials=int(std::clamp<std::int64_t>(std::int64_t(player.materials)+shop.waveBonus,0,MaxMaterials));
    for(auto& offer:shop.offers) if(offer.sold || !offer.locked) RollOffer(world,shop,offer);
    RecomputeBuild(world,world.player); world.state=State::Shop;
    shop.restoredHealth=0;
    if(UsesCampaign(world)) {
        auto& health=world.Get<Health>(world.player);
        if(health.current>0){shop.restoredHealth=std::min(health.maximum-health.current,ScaleRuleValue(health.maximum,DifficultyRules(world).shopHealing));health.current+=shop.restoredHealth;}
    }
}
int ShopRerollCost(const Shop& shop) { return 2+std::clamp(shop.rerolls,0,30); }

ShopResult BuyShopItem(GameWorld& world,std::size_t slot) {
    ShopOffer offer;
    {
        const auto* shop=CurrentShop(world);
        if(!shop || slot>=ShopSlots) return Feedback(world,ShopResult::Invalid);
        offer=shop->offers[slot];
    }
    const auto index=std::size_t(offer.kind);
    if(offer.type!=OfferType::Item)return Feedback(world,ShopResult::Invalid);
    if(offer.sold) return Feedback(world,ShopResult::Sold);
    if(index>=ItemCount || offer.price<=0 || offer.price>10000) return Feedback(world,ShopResult::Invalid);
    if(world.Get<Player>(world.player).materials<offer.price) return Feedback(world,ShopResult::NotEnough);
    if(ItemCounts(world,world.player)[index]>=MaxItemStack) return Feedback(world,ShopResult::StackFull);
    ECS::EntityHandle stack(0);
    {
        Query<OwnedItem> query; query.Refresh(*world.scene);
        for(auto chunk:query) for(std::size_t row=0;row<chunk.count;++row) {
            const auto& item=chunk.Get<OwnedItem>()[row];
            if(item.kind==offer.kind && item.count>=0 && Owns(world,item,world.player) &&
               (stack.GetID()==0 || chunk.Entity(row,*world.scene).GetID()<stack.GetID())) stack=chunk.Entity(row,*world.scene);
        }
    }
    if(stack.GetID()==0) {
        if(!world.itemType || Count<OwnedItem>(world)>=world.config.maxOwnedItems) return Feedback(world,ShopResult::Capacity);
        // No borrowed shop/player/query data crosses this between-ticks create.
        // Debit only after a complete item entity exists; allocation failure
        // propagates with the wallet and offer untouched.
        stack=world.scene->CreateEntity(world.itemType);
        auto& item=world.Get<OwnedItem>(stack); item.owner=world.player; item.kind=offer.kind; item.count=0;
    }
    ++world.Get<OwnedItem>(stack).count;
    world.Get<Player>(world.player).materials-=offer.price;
    auto& bought=world.Get<Shop>(world.player).offers[slot]; bought.sold=true; bought.locked=false;
    RecomputeBuild(world,world.player); RefreshCounts(world); return Feedback(world,ShopResult::Bought);
}
ShopResult RerollShop(GameWorld& world) {
    auto* shop=CurrentShop(world); if(!shop) return ShopResult::Invalid;
    if(std::all_of(shop->offers.begin(),shop->offers.end(),[](const auto& o){return o.locked&&!o.sold;})) return Feedback(world,ShopResult::AllLocked);
    auto& player=world.Get<Player>(world.player); const int cost=ShopRerollCost(*shop);
    if(player.materials<cost) return Feedback(world,ShopResult::NotEnough);
    player.materials-=cost; ++shop->rerolls;
    for(auto& offer:shop->offers) if(offer.sold || !offer.locked) RollOffer(world,*shop,offer);
    return Feedback(world,ShopResult::Rerolled);
}
ShopResult ToggleShopLock(GameWorld& world,std::size_t slot) {
    auto* shop=CurrentShop(world); if(!shop || slot>=ShopSlots) return Feedback(world,ShopResult::Invalid);
    auto& offer=shop->offers[slot]; if(offer.sold) return Feedback(world,ShopResult::Sold);
    offer.locked=!offer.locked; return Feedback(world,offer.locked?ShopResult::Locked:ShopResult::Unlocked);
}
BuildSnapshot ExtractBuild(GameWorld& world,ECS::EntityHandle owner) {
    BuildSnapshot result; result.items=ItemCounts(world,owner);
    if(!world.scene) return result;
    if(const auto* s=world.scene->TryGetComponent<CombatStats>(owner)) result.stats=*s;
    if(const auto* h=world.scene->TryGetComponent<Health>(owner)) {result.health=h->current;result.maximumHealth=h->maximum;}
    if(const auto* p=world.scene->TryGetComponent<Player>(owner)) result.materials=p->materials;
    return result;
}
ShopSnapshot ExtractShop(GameWorld& world) {
    ShopSnapshot result; result.build=ExtractBuild(world,world.player);result.equipment=ExtractEquipment(world,world.player);
    if(world.scene) if(const auto* shop=world.scene->TryGetComponent<Shop>(world.player)) {result.shop=*shop;result.rerollCost=ShopRerollCost(*shop);}
    return result;
}
} // namespace Brotato
