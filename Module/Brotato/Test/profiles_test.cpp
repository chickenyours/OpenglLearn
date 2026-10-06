#include "Brotato/Public/session_module.h"
#include "Brotato/Public/expanded_gameplay.h"
#include "Brotato/Systems/game_systems.h"
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
using namespace Brotato;
struct Extra:Component<Extra>{int value=43;};
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void Near(float a,float b,const char* message){Check(std::abs(a-b)<1e-4f,message);}
template<class... T>auto Type(ECS::Core::Scene& scene){auto d=scene.CreateArchTypeDescription();(d->AddComponentArray<T>(),...);return scene.CreateArchType(d,2);}
Config Quiet(){auto c=ExpandedGameplay();c.encounters=false; // Historical mechanisms are tested separately from encounters.
c.campaign=false;c.playerStart={0,0};c.minimum={-30,-30};c.maximum={30,30};c.spawning=c.armed=false;c.enemySpeed=0;c.contactDamage=0;c.spawnWarning=0;c.dropChance=0;c.maxEffects=0;c.waveSeconds=600;return c;}
void Start(GameModule& g){if(!g.Startup())throw std::runtime_error(g.Error());}
void Tick(GameModule& g,int count=1,Input input={}){for(int i=0;i<count;++i){if(g.GetState()==State::LevelUp)Check(g.SelectUpgrade(g.Get<Growth>(g.PlayerEntity()).choices[0]),"offered fixture upgrade");else g.FixedTick(input);}}

void StartsAndIdempotence(){
    const int hp[]{150,180,130,125,140};
    const float attack[]{1,1,1.1f,1,.9f},move[]{1,.9f,1.15f,1,1};
    for(std::size_t role=0;role<CharacterProfiles.size();++role){auto c=Quiet();c.character=role;GameModule g(c);Start(g);auto p=g.PlayerEntity();
        Check(g.ExtractBuild().health==hp[role]&&g.ExtractBuild().maximumHealth==hp[role],"role changes initial current and maximum HP");
        Near(g.Get<CombatStats>(p).attackSpeed,attack[role],"role attack rate");Near(g.Get<CombatStats>(p).moveSpeed,move[role],"role movement rate");
        g.Get<Health>(p).current-=10;Tick(g,20);Check(g.Get<Health>(p).current==hp[role]-10,"repeated build computation cannot heal or reapply role");
        auto before=g.Get<Transform>(p).position;Tick(g,1,{1,0});Near(g.Get<Transform>(p).position.x-before.x,5*move[role]*float(SimulationStep),"actual movement uses role speed");
        g.SetPaused(true);const auto ticks=g.Stats().ticks;Tick(g,10);Check(g.Stats().ticks==ticks,"role build respects pause");g.SetPaused(false);
        g.Get<CharacterProfile>(p).kind=999;Tick(g);Check(g.Get<CombatStats>(p).weaponDamage==std::array<int,WeaponCount>{},"invalid ECS role clears derived weapon bonus");
        g.Restart();Check(g.ExtractRunSetup().character==role&&g.ExtractBuild().health==hp[role]&&g.Get<Growth>(p).upgrades==std::array<int,UpgradeCount>{},"restart restores selected role and clean base");
    }
}

void RealHitsAndRoleSnapshots(){
    for(auto kind:{WeaponKind::Torch,WeaponKind::Gun}){
        auto c=Quiet();c.character=kind==WeaponKind::Torch?1:3;c.initialWeapon=kind;c.armed=true;GameModule g(c);Start(g);
        auto target=g.SpawnEnemy({2,0},true,EnemyKind::Armored);g.Get<Health>(target).current=g.Get<Health>(target).maximum=200;
        for(int i=0;i<180&&!g.Stats().hits;++i)Tick(g);
        const int expected=kind==WeaponKind::Torch?5:2;
        Check(g.Stats().hits==1&&g.Get<Health>(target).current==200-expected&&g.Get<Damage>(g.WeaponEntity()).amount==expected,"role bonus changes real melee or projectile hit");
    }
    auto c=Quiet();c.character=4;c.armed=true;c.initialWeapon=WeaponKind::Wand;GameModule mage(c);Start(mage);
    auto target=mage.SpawnEnemy({2,0},true,EnemyKind::Armored);mage.Get<Health>(target).current=mage.Get<Health>(target).maximum=200;
    for(int i=0;i<180&&!mage.Stats().hits;++i)Tick(mage);
    Check(mage.Stats().hits==1&&mage.Get<Damage>(mage.WeaponEntity()).burning==2,"Mage elemental attack captures burn");
    mage.Get<Weapon>(mage.WeaponEntity()).cooldowns[WeaponIndex(WeaponKind::Wand)]=100;
    mage.Get<CharacterProfile>(mage.PlayerEntity()).kind=0;Tick(mage,60);
    Check(mage.Stats().burnHits==1&&mage.Get<Health>(target).current==196,"existing burn retains payload after role changes");
    c.initialWeapon=WeaponKind::Gun;GameModule nonElemental(c);Start(nonElemental);target=nonElemental.SpawnEnemy({2,0},true,EnemyKind::Armored);Tick(nonElemental);
    Check(nonElemental.Get<Damage>(nonElemental.WeaponEntity()).burning==0,"Mage bonus does not ignite ballistic weapons");
    c.character=2;GameModule crazy(c);Start(crazy);target=crazy.SpawnEnemy({2,0},true,EnemyKind::Armored);
    std::uint32_t chosen=1;for(;;++chosen){auto draw=chosen;if(BuildRandom(draw)%10000<1500)break;}
    crazy.Get<Weapon>(crazy.WeaponEntity()).randomState=chosen;Tick(crazy);
    Check(crazy.Stats().criticalAttacks==1&&crazy.Get<Damage>(crazy.WeaponEntity()).critical&&crazy.Get<Damage>(crazy.WeaponEntity()).amount==2,"Crazy uses real independent critical roll");
}

void ForeignOwnersAndStandaloneBuild(){
    GameWorld w(Quiet());w.scene=std::make_unique<ECS::Core::Scene>();
    auto type=Type<Extra,Player,CharacterProfile,RunRules,Transform,Health,CombatStats,BuildBase,Growth>(*w.scene);
    auto missingType=Type<Player,Transform,Health,CombatStats,BuildBase,Growth>(*w.scene);
    w.player=w.scene->CreateEntity(type);auto foreign=w.scene->CreateEntity(type);auto unprofiled=w.scene->CreateEntity(missingType);
    for(auto e:{w.player,foreign,unprofiled}){w.Get<Health>(e).current=w.Get<Health>(e).maximum=150;w.Get<BuildBase>(e).maximumHealth=150;}
    w.Get<CharacterProfile>(w.player).kind=3;w.Get<CharacterProfile>(foreign).kind=1;
    ECS::System::Context context;context.scene=w.scene.get();context.SetService(&w);ECS::System::Pipeline pipeline;pipeline.Add<BuildSystem>();Check(pipeline.Start(context)&&pipeline.Tick(context),"standalone profile build starts");
    Check(w.Get<Health>(w.player).maximum==125&&w.Get<Health>(foreign).maximum==180&&w.Get<Health>(unprofiled).maximum==150,"query handles each owner's profile and absent optional profile");
    Weapon weapon;weapon.kind=WeaponKind::Torch;weapon.owner=foreign;Check(WeaponPayload(w,weapon,false).amount==5,"foreign owner melee bonus");weapon.owner=w.player;Check(WeaponPayload(w,weapon,false).amount==3,"main Ranger does not share foreign melee bonus");
    const auto stale=foreign;w.scene->DeleteEntity(foreign);bool recycled=false;
    for(int i=0;i<16&&!recycled;++i){auto e=w.scene->CreateEntity(type);w.Get<CharacterProfile>(e).kind=3;w.Get<Health>(e).current=w.Get<Health>(e).maximum=150;recycled=e.GetID()==stale.GetID();}
    Check(recycled&&!w.scene->IsAlive(stale),"profile owner generation is recycled for the test");weapon.kind=WeaponKind::Gun;weapon.owner=stale;Check(WeaponPayload(w,weapon,false).amount==1,"retired owner never borrows replacement role");
    w.config.profiles=false;pipeline.Tick(context);Check(w.Get<Health>(w.player).maximum==150&&w.Get<CombatStats>(w.player).weaponDamage==std::array<int,WeaponCount>{},"disabling profiles removes derived bonuses");pipeline.Stop(context);
}

void DifficultyEnemiesAndBullets(){
    const int normalHP[]{2,3,3},normalDamage[]{10,12,14},bossHP[]{70,84,102},finalHP[]{180,216,261},bulletDamage[]{6,7,9};
    for(std::size_t difficulty=0;difficulty<RunDifficulties.size();++difficulty){auto c=Quiet();c.difficulty=difficulty;c.contactDamage=10;GameModule g(c);Start(g);
        auto enemy=g.SpawnEnemy({15,0},true);auto boss=g.SpawnBoss({15,10},true);auto final=g.SpawnBoss({15,-10},true,true);
        Check(g.Get<Health>(enemy).maximum==normalHP[difficulty]&&g.Get<Damage>(enemy).amount==normalDamage[difficulty],"difficulty scales real enemy health and contact damage once");
        Check(g.Get<Health>(boss).maximum==bossHP[difficulty]&&g.Get<Health>(final).maximum==finalHP[difficulty],"difficulty scales checkpoint and final Boss health");
        auto shooter=g.SpawnEnemy({4,0},true,EnemyKind::Ranged);g.Get<EnemyBrain>(shooter).cooldown=0;Tick(g,55);
        Query<HostileProjectile,Damage> q;q.Refresh(*g.Scene());Check(q.Count()==1,"difficulty shooter fires real projectile");for(auto chunk:q)for(std::size_t row=0;row<chunk.count;++row)Check(chunk.Get<Damage>()[row].amount==bulletDamage[difficulty],"enemy projectile difficulty damage");
        auto armored=g.SpawnEnemy({12,10},true,EnemyKind::Armored);Check(g.Get<Health>(armored).maximum==(difficulty==0?6:difficulty==1?8:9),"integer ceiling HP is exact");
        Tick(g,10);Check(g.Get<Health>(armored).maximum==(difficulty==0?6:difficulty==1?8:9),"enemy difficulty multiplier is not reapplied each tick");
    }
    Check(ScaleRuleValue(5,120)==6&&ScaleRuleValue(0,135)==0&&ScaleRuleValue(100000,145)==100000,"scale rounding zero and limit");
}

void DifficultyMovementSpawnsAndHealing(){
    for(std::size_t d=0;d<RunDifficulties.size();++d){auto c=Quiet();c.difficulty=d;c.enemySpeed=2.3f;GameModule g(c);Start(g);auto boss=g.SpawnBoss({4,0},true);auto charger=g.SpawnEnemy({8,0},true,EnemyKind::Charger);
        g.Get<BossBrain>(boss).action=BossAction::Charging;g.Get<BossBrain>(boss).direction={1,0};g.Get<BossBrain>(boss).remaining=1;
        g.Get<EnemyBrain>(charger).action=EnemyAction::Charging;g.Get<EnemyBrain>(charger).direction={1,0};g.Get<EnemyBrain>(charger).remaining=1;
        Tick(g);Near(glm::length(g.Get<Velocity>(boss).value),9*RunDifficulties[d].enemySpeed/100.f,"actual Boss charge difficulty speed");Near(glm::length(g.Get<Velocity>(charger).value),10*RunDifficulties[d].enemySpeed/100.f,"actual enemy charge difficulty speed");
        g.Get<BossBrain>(boss).action=BossAction::Windup;g.Get<BossBrain>(boss).attack=BossAttack::Charge;g.Get<BossBrain>(boss).remaining=1;
        const auto cues=g.ExtractCombatCues();bool matched=false;for(const auto& cue:cues)if(std::abs(cue.radius-.82f)<1e-4f){Near(glm::length(cue.end-cue.position),9*.55f*RunDifficulties[d].enemySpeed/100.f,"Boss telegraph matches scaled dash");matched=true;}Check(matched,"actual Boss charge telegraph found");
        c.campaign=true;c.waveSeconds=.05f;c.waveIncrement=0;c.enemySpeed=0;GameModule shop(c);Start(shop);shop.Get<Health>(shop.PlayerEntity()).current=20;Tick(shop,6);
        const int healed[]{38,30,23};Check(shop.GetState()==State::Shop&&shop.ExtractShop().shop.restoredHealth==healed[d]&&shop.ExtractBuild().health==20+healed[d],"difficulty controls real wave shop supplies");
    }
    std::array<int,3> spawns{};std::array<std::uint32_t,3> rewardSeeds{};
    for(std::size_t d=0;d<3;++d){auto c=Quiet();c.difficulty=d;c.spawning=true;GameModule g(c);Start(g);Tick(g,600);spawns[d]=int(g.Stats().enemies);rewardSeeds[d]=g.Get<Growth>(g.PlayerEntity()).randomState;}
    Check(spawns[0]<spawns[1]&&spawns[1]<spawns[2],"actual spawn scheduling becomes faster by difficulty");Check(rewardSeeds[0]==rewardSeeds[1]&&rewardSeeds[1]==rewardSeeds[2],"difficulty never draws reward random stream");
}

void GrowthShopAndResultLifecycle(){
    auto c=Quiet();c.character=1;c.difficulty=2;c.campaign=true;c.waveSeconds=.05f;c.waveIncrement=0;GameModule g(c);Start(g);g.Get<Health>(g.PlayerEntity()).current=50;Tick(g,6);
    Check(g.ExtractBuild().health==77&&g.ExtractBuild().maximumHealth==180,"Brawler and danger supplies compose");
    g.Get<Shop>(g.PlayerEntity()).offers[0]={ItemKind::Cake,7,false,false};Check(g.BuyShopOffer(0)==ShopResult::Bought,"actual cake purchase");Check(g.ExtractBuild().maximumHealth==190&&g.ExtractBuild().health==87&&g.ExtractBuild().stats.armor==2,"item grows role base without dropping armor");
    g.NextWave();g.Get<Player>(g.PlayerEntity()).experience=ExperienceThreshold(0,true);g.FixedTick();Check(g.GetState()==State::LevelUp,"actual growth pauses profile run");
    const auto choices=g.Get<Growth>(g.PlayerEntity()).choices;const auto selected=choices[0];Check(g.SelectUpgrade(selected),"select actual offered role upgrade");
    Check(g.Get<Growth>(g.PlayerEntity()).upgrades[std::size_t(selected)]==1&&g.ExtractBuild().stats.weaponDamage[WeaponIndex(WeaponKind::Torch)]==2,"growth retains profession damage");
    g.Restart();Check(g.ExtractBuild().health==180&&g.ExtractBuild().materials==0&&g.ExtractBuild().items[std::size_t(ItemKind::Cake)]==0&&g.ExtractRunSetup().difficulty==2,"restart keeps choice and resets growth and purchases");
    c.character=4;c.spawning=true;c.campaignWaves=1;c.waveSeconds=.15f;c.deathDelay=.025f;GameModule result(c);Start(result);Tick(result);ECS::EntityHandle boss(0);{Query<BossBrain> q;q.Refresh(*result.Scene());for(auto chunk:q)if(chunk.count)boss=chunk.Entity(0,*result.Scene());}
    auto bullet=result.SpawnProjectile(result.Get<Transform>(boss).position,{});result.Get<Damage>(bullet).amount=100000;Tick(result,30);
    Check(result.GetState()==State::Victory&&result.ExtractResult().character==4&&result.ExtractResult().difficulty==2,"result captures actual role and difficulty");result.Get<CharacterProfile>(result.PlayerEntity()).kind=0;result.Get<RunRules>(result.PlayerEntity()).difficulty=0;
    Check(result.ExtractResult().character==4&&result.ExtractResult().difficulty==2,"result identity is a captured value");result.Restart();Check(result.ExtractRunSetup().character==4&&result.ExtractRunSetup().difficulty==2&&result.ExtractBuild().health==140,"terminal restart restores original setup");
}

void MenuAndSourceCompatibility(){
    auto c=Quiet();c.character=3;c.difficulty=2;SessionModule session(c);Check(session.Startup()&&session.DifficultyCount()==3&&session.SelectedDifficulty()==2,"initial preview and three choices");
    Check(!session.SelectDifficulty(0)&&!session.Game(),"difficulty has screen gate");Check(session.Navigate(Screen::CharacterSelect)&&session.SelectCharacter(4)&&session.Navigate(Screen::WeaponSelect)&&session.SelectWeapon(WeaponKind::Wand)&&session.Navigate(Screen::DifficultySelect),"real role weapon navigation");
    Check(!session.SelectDifficulty(3)&&session.SelectedDifficulty()==2&&session.SelectDifficulty(1)&&session.Navigate(Screen::MapSelect)&&session.StartRun(),"valid difficulty starts actual run");
    Check(session.Game()->ExtractRunSetup().character==4&&session.Game()->ExtractRunSetup().difficulty==1&&session.Game()->ExtractBuild().health==140,"menu choices reach ECS");session.ReturnHome();Check(!session.Game()&&session.SelectedDifficulty()==1&&session.SelectedCharacter()==4,"home releases run and retains previews");
    c.initialHealth=0;SessionModule failed(c);Check(failed.Startup()&&failed.Navigate(Screen::CharacterSelect)&&failed.Navigate(Screen::WeaponSelect)&&failed.SelectWeapon(WeaponKind::Gun)&&failed.Navigate(Screen::DifficultySelect)&&failed.SelectDifficulty(2)&&failed.Navigate(Screen::MapSelect)&&!failed.StartRun()&&!failed.Game()&&failed.RunSerial()==0,"failed start retains choices without partial run");
    c=Config{};c.profiles=true;c.character=4;GameModule source(c);Start(source);Check(source.ExtractBuild().health==150&&source.Get<CombatStats>(source.PlayerEntity()).attackSpeed==1,"source character still appearance only");auto enemy=source.SpawnEnemy({15,0},true);Check(source.Get<Health>(enemy).maximum==1&&source.Get<Damage>(enemy).amount==10,"source enemy rules untouched");SessionModule sourceSession(c);Check(sourceSession.Startup()&&sourceSession.DifficultyCount()==1,"source has one actual difficulty");
    c.difficulty=1;GameModule invalidSource(c);Check(!invalidSource.Startup(),"source rejects new difficulty");c=Quiet();c.profiles=false;c.difficulty=2;GameModule invalidHistory(c);Check(!invalidHistory.Startup(),"disabled profiles reject unsupported selection");c=Quiet();c.difficulty=3;SessionModule invalid(c);Check(!invalid.Startup(),"invalid initial difficulty rejected");
}

void NaturalCampaignMatrix(){
    std::array<int,3> victories{};bool normalComplete=true;
    for(std::size_t difficulty=0;difficulty<3;++difficulty)for(std::size_t role=0;role<5;++role){
        auto c=ExpandedGameplay();c.initialWeapon=WeaponKind::Gun;c.character=role;c.difficulty=difficulty;GameModule g(c);Start(g);
        const glm::vec2 route[]{{c.maximum.x-2,c.maximum.y-2},{c.minimum.x+2,c.maximum.y-2},{c.minimum.x+2,c.minimum.y+2},{c.maximum.x-2,c.minimum.y+2}};std::size_t waypoint=0;
        for(int wave=1;wave<=6;++wave){int limit=14000;while(limit-->0&&(g.GetState()==State::Playing||g.GetState()==State::LevelUp)){
            if(g.GetState()==State::LevelUp){const auto choices=g.Get<Growth>(g.PlayerEntity()).choices;auto chosen=choices[0];for(auto p:{UpgradeKind::Damage,UpgradeKind::Armor,UpgradeKind::MaxHealth,UpgradeKind::AttackSpeed,UpgradeKind::MoveSpeed,UpgradeKind::PickupRange})if(std::find(choices.begin(),choices.end(),p)!=choices.end()){chosen=p;break;}Check(g.SelectUpgrade(chosen),"natural offered upgrade");continue;}
            auto delta=route[waypoint]-g.Get<Transform>(g.PlayerEntity()).position;if(glm::length(delta)<.35f){waypoint=(waypoint+1)%std::size(route);delta=route[waypoint]-g.Get<Transform>(g.PlayerEntity()).position;}auto direction=glm::normalize(delta);g.FixedTick({direction.x,direction.y});
        }
        if(g.GetState()==State::Victory||g.GetState()==State::Defeat)break;
        Check(g.GetState()==State::Shop,"natural run must reach shop or terminal outcome");
        for(int roll=0;roll<4;++roll){const auto stock=g.ExtractShop().shop.offers;bool bought=false;
            if(g.ExtractEquipment().count<EquipmentSlots)for(auto preferred:{WeaponKind::Gun,WeaponKind::Burst,WeaponKind::Wand,WeaponKind::Laser,WeaponKind::Torch,WeaponKind::Knife})for(std::size_t i=0;i<ShopSlots;++i)if(stock[i].type==OfferType::Weapon&&stock[i].weapon==preferred&&g.BuyShopOffer(i)==ShopResult::Bought)bought=true;
            for(auto preferred:{ItemKind::Plant,ItemKind::Vest,ItemKind::Cake,ItemKind::Lens,ItemKind::Coffee,ItemKind::Sausage,ItemKind::Bat,ItemKind::Sunglasses,ItemKind::Bandana,ItemKind::Beanie})for(std::size_t i=0;i<ShopSlots;++i)if(stock[i].type==OfferType::Item&&stock[i].kind==preferred&&g.BuyShopOffer(i)==ShopResult::Bought)bought=true;
            if(!bought||g.ExtractBuild().materials<12||g.RerollShop()!=ShopResult::Rerolled)break;
        }g.NextWave();}
        const auto result=g.ExtractResult();Check(result.outcome!=RunOutcome::None&&result.character==role&&result.difficulty==difficulty,"natural role/difficulty run produces matching terminal result");
        if(result.outcome==RunOutcome::Victory){++victories[difficulty];Check(result.bossKills==2&&result.seconds==225,"natural victory defeats objectives on full timing");}else if(difficulty==0)normalComplete=false;
        std::cout<<"[natural profile] role="<<role<<" difficulty="<<difficulty<<" outcome="<<int(result.outcome)<<" wave="<<result.wave<<" hp="<<result.health<<"/"<<result.maximumHealth<<" kills="<<result.kills<<" bosses="<<result.bossKills<<" level="<<result.level<<" weapons="<<result.weapons<<" items="<<result.items<<" materials="<<result.materials<<" seconds="<<result.seconds<<std::endl;
    }
    Check(normalComplete,"all five ordinary SMG routes complete standard campaign");Check(victories[1]>0&&victories[2]>0,"both harder difficulties have ordinary complete campaign routes");
}
}
int main(int argc,char**){try{RegisterComponents();REGISTER_COMPONENT("brotato_profile_extra",Extra);const std::pair<const char*,std::function<void()>> tests[]{{"initial attributes and no repeated bonus",StartsAndIdempotence},{"real melee ranged burn and critical hits",RealHitsAndRoleSnapshots},{"standalone build and foreign owner generations",ForeignOwnersAndStandaloneBuild},{"difficulty enemy health and bullets",DifficultyEnemiesAndBullets},{"difficulty movement telegraphs spawns and supplies",DifficultyMovementSpawnsAndHealing},{"growth shopping and result lifecycle",GrowthShopAndResultLifecycle},{"menu transactions and source compatibility",MenuAndSourceCompatibility}};for(const auto& [name,test]:tests){test();std::cout<<"[PASS] "<<name<<std::endl;}if(argc==1){NaturalCampaignMatrix();std::cout<<"[PASS] natural 5 x 3 campaign matrix\n8 profile groups passed\n";}else std::cout<<"7 quick profile groups passed\n";return 0;}catch(const std::exception& e){std::cerr<<"[FAIL] "<<e.what()<<'\n';return 1;}}
