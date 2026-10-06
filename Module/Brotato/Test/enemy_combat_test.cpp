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
struct Extra : Component<Extra> { int value = 37; };
void Check(bool test, const char* message) { if (!test) throw std::runtime_error(message); }
void Near(float a, float b, const char* message) { Check(std::isfinite(a) && std::abs(a-b)<1e-4f,message); }
template<class... T> auto Type(ECS::Core::Scene& scene) {
    auto d = scene.CreateArchTypeDescription(); (d->AddComponentArray<T>(),...); return scene.CreateArchType(d,2);
}
Config Quiet(WeaponKind kind = WeaponKind::Wand) {
    auto c = ExpandedGameplay();c.encounters=false; // Isolate historical mechanics from encounter scheduling.
 c.playerStart={0,0}; c.minimum={-30,-30}; c.maximum={30,30};
    c.builds=false; // This fixture isolates phase-8 combat from reward/shop rules.
    c.spawning=c.armed=false; c.enemySpeed=0; c.contactDamage=0; c.dropChance=0; c.waveSeconds=600;
    c.initialWeapon=kind; c.deathDelay=float(SimulationStep*2);
    for(auto& weapon:c.weapons) weapon.gravity=0;
    return c;
}
void Start(GameModule& g) { Check(g.Startup(),g.Error().c_str()); }
void Tick(GameModule& g,int n,Input i={}) { while(n-->0) g.FixedTick(i); }
void Until(GameModule& g,const std::function<bool()>& done,int limit,const char* message) {
    while(limit-->0&&!done()) g.FixedTick(); Check(done(),message);
}
void Hit(GameModule& g,ECS::EntityHandle e,glm::vec2 velocity={}) {
    g.SpawnProjectile(g.Get<Transform>(e).position,velocity); g.FixedTick();
}

void TestRangedWindupAndSpacing() {
    auto c=Quiet(); c.enemySpeed=2;
    GameModule g(c); Start(g);
    auto e=g.SpawnEnemy({5,0},true,EnemyKind::Ranged);
    Until(g,[&]{return g.Get<EnemyBrain>(e).action==EnemyAction::Windup;},120,"shooter winds up within attack range");
    Check(g.Stats().enemyVolleys==0 && g.ExtractCombatCues().size()==1,"aim telegraph precedes the shot");
    const auto locked=g.Get<EnemyBrain>(e).direction;
    const auto position=g.Get<Transform>(e).position;
    Tick(g,30,{0,1});
    Check(g.Stats().hostileProjectiles==0 && g.Get<Transform>(e).position==position,"shooter stops while preparing");
    Until(g,[&]{return g.Stats().enemyVolleys==1;},40,"windup eventually fires");
    Query<HostileProjectile,Velocity> bullets; bullets.Refresh(*g.Scene());
    Check(bullets.Count()==1,"shooter creates exactly one hostile projectile");
    for(auto chunk:bullets) Near(glm::dot(glm::normalize(chunk.Get<Velocity>()[0].value),locked),1,"shot direction remains locked despite player movement");
    auto& t=g.Get<Transform>(e); t.position=t.previous={2,0};
    g.Get<Transform>(g.PlayerEntity()).position={0,0};
    auto& brain=g.Get<EnemyBrain>(e); brain.action=EnemyAction::Approach; brain.cooldown=10;
    g.FixedTick(); Check(g.Get<Transform>(e).position.x>2,"shooter retreats when the player is too close");
}

void TestChargeDodgeRecoveryAndFreeze() {
    auto c=Quiet(); c.contactDamage=10;
    GameModule g(c); Start(g); auto e=g.SpawnEnemy({4,0},true,EnemyKind::Charger);
    g.Get<EnemyBrain>(e).cooldown=0; g.FixedTick();
    const auto cue=g.ExtractCombatCues().front();
    Check(cue.kind==CombatCueKind::Charge && cue.end.x<0,"dash path shown before movement");
    auto paused=g.Get<EnemyBrain>(e).remaining;
    g.SetPaused(true); Tick(g,120); Near(g.Get<EnemyBrain>(e).remaining,paused,"pause freezes windup"); g.SetPaused(false);
    g.Get<Player>(g.PlayerEntity()).experience=8; g.FixedTick();
    paused=g.Get<EnemyBrain>(e).remaining; const auto ticks=g.Stats().ticks;
    Tick(g,120); Near(g.Get<EnemyBrain>(e).remaining,paused,"level-up freezes windup");
    Check(g.SelectUpgrade(UpgradeKind::MoveSpeed) && g.Stats().ticks==ticks,"choice does not skip AI time");
    while(g.Get<EnemyBrain>(e).action==EnemyAction::Windup) g.FixedTick({0,1});
    Check(g.Get<EnemyBrain>(e).action==EnemyAction::Charging && g.Stats().charges==1,"windup commits to a dash");
    Tick(g,14); Near(g.Get<Transform>(e).position.y,0,"dash does not steer toward the dodging player");
    Check(g.Get<Transform>(e).position.x<3 && g.Get<Health>(g.PlayerEntity()).current==c.initialHealth,"perpendicular dodge avoids the locked dash");
    Until(g,[&]{return g.Get<EnemyBrain>(e).action==EnemyAction::Recovering;},90,"dash has a bounded duration");
    const auto stop=g.Get<Transform>(e).position; Tick(g,30);
    Check(g.Get<Transform>(e).position==stop,"charger has a vulnerable recovery interval");
    GameModule stationary(c); Start(stationary); auto second=stationary.SpawnEnemy({4,0},true,EnemyKind::Charger);
    stationary.Get<EnemyBrain>(second).cooldown=0; Tick(stationary,145);
    Check(stationary.Get<Health>(stationary.PlayerEntity()).current<c.initialHealth,"a player staying in the dash path takes contact damage");
}

void TestEliteVolleyAndRetiredAttribution() {
    auto c=Quiet(); c.contactDamage=10;
    GameModule g(c); Start(g); auto e=g.SpawnEnemy({5,0},true,EnemyKind::Elite);
    g.Get<EnemyBrain>(e).cooldown=0;
    Until(g,[&]{return g.Stats().enemyVolleys==1;},200,"elite fires after a telegraphed windup");
    Check(g.Stats().hostileProjectiles==3 && g.Stats().charges==1 && g.Get<Health>(e).maximum==12,"elite combines a triple volley and dash");
    Query<HostileProjectile,Velocity,Damage> bullets; bullets.Refresh(*g.Scene());
    std::vector<float> directions;
    for(auto chunk:bullets) for(std::size_t row=0;row<chunk.count;++row) {
        const auto velocity=chunk.Get<Velocity>()[row].value; directions.push_back(std::atan2(velocity.y,velocity.x));
        Check(chunk.Get<Damage>()[row].amount==8,"elite projectile has its own damage value");
    }
    Check(directions.size()==3 && directions[0]!=directions[1] && directions[1]!=directions[2],"fan projectiles have distinct trajectories");
    g.Scene()->DeleteEntity(e); const auto replacement=g.SpawnEnemy({20,20},true);
    Check(replacement.GetID()==e.GetID()&&!g.Scene()->IsAlive(e),"fixture recycles the shooter ID");
    Tick(g,100);
    Check(g.Get<Health>(g.PlayerEntity()).current==c.initialHealth-8 && g.Get<Health>(replacement).current==2,
          "in-flight bullets remain dangerous after shooter retirement and never bind to its replacement");
}

void TestHostileSweepFactionAndLifecycle() {
    auto c=Quiet(); GameModule g(c); Start(g);
    auto friendEnemy=g.SpawnEnemy({-5,0},true,EnemyKind::Armored);
    const auto bullet=g.SpawnHostileProjectile({-10,0},{2400,0},6);
    g.FixedTick();
    Check(!g.Scene()->IsAlive(bullet) && g.Get<Health>(g.PlayerEntity()).current==144 && g.Get<Health>(friendEnemy).current==6,
          "fast hostile sweeps hit the player while passing through allied enemies");
    g.SpawnHostileProjectile({0,0},{},6); g.FixedTick();
    Check(g.Stats().hostileProjectiles==0 && g.Get<Health>(g.PlayerEntity()).current==144,"protected player consumes another bullet without stacked damage");
    const auto persistent=g.SpawnHostileProjectile({20,20},{},6);
    Check(g.EquipWeapon(WeaponKind::Gun)&&g.Scene()->IsAlive(persistent),"weapon changes do not clear enemy bullets");
    g.SetPaused(true); const auto lifetime=g.Get<HostileProjectile>(persistent).remaining; Tick(g,100);
    Near(g.Get<HostileProjectile>(persistent).remaining,lifetime,"hostile lifetime freezes on pause");
    g.SetPaused(false); Tick(g,361); Check(!g.Scene()->IsAlive(persistent),"hostile lifetime is bounded");
    g.SpawnHostileProjectile({20,20},{},6); g.Restart(); Check(g.Stats().hostileProjectiles==0,"restart clears hostile entities");
    c.waveSeconds=.05f; GameModule wave(c); Start(wave); wave.SpawnHostileProjectile({20,20},{},6); Tick(wave,7);
    Check(wave.GetState()==State::WaveComplete&&wave.Stats().hostileProjectiles==0,"wave cleanup includes enemy projectiles");
}

void TestVolleyCapacityAndForeignArchetypes() {
    auto c=Quiet(); c.maxHostileProjectiles=2;
    GameModule g(c); Start(g); auto elite=g.SpawnEnemy({4,0},true,EnemyKind::Elite);
    g.Get<EnemyBrain>(elite).cooldown=0; Tick(g,100);
    Check(g.Stats().enemyVolleys==0&&g.Stats().hostileProjectiles==0&&g.Stats().charges==1,"elite volley is rejected as a whole when three slots are unavailable");
    auto type=Type<Extra,HostileProjectile,Transform,Velocity,Circle,Sprite,Damage>(*g.Scene());
    const auto external=g.Scene()->CreateEntity(type); g.Get<Transform>(external).position={20,20};
    const auto second=g.SpawnHostileProjectile({20,20},{},1);
    Check(second.GetID()!=0&&g.SpawnHostileProjectile({20,20},{},1).GetID()==0,"capacity counts hostile entities from other archetypes");
    g.FixedTick(); Check(g.Stats().hostileProjectiles==2,"projectile query discovers other archetypes");
    g.Scene()->DeleteEntity(external); g.Scene()->DeleteEntity(second);
    auto one=g.SpawnEnemy({5,0},true,EnemyKind::Ranged), two=g.SpawnEnemy({-5,0},true,EnemyKind::Ranged);
    for(auto e:{one,two}) { auto& b=g.Get<EnemyBrain>(e); b.action=EnemyAction::Windup; b.remaining=float(SimulationStep); }
    g.FixedTick(); Check(g.Stats().hostileProjectiles==2&&g.Stats().enemyVolleys==2,"same-tick reservations prevent multiple shooters exceeding capacity");
}

void TestKnockbackResistanceAndInterrupt() {
    auto c=Quiet(); c.maxEffects=0;
    GameModule g(c); Start(g);
    auto armor=g.SpawnEnemy({4,0},true,EnemyKind::Armored);
    Hit(g,armor,{1,0}); Near(g.Get<Knockback>(armor).velocity.x,1.4f,"armored target resists knockback");
    Tick(g,5); Check(g.Get<Transform>(armor).position.x>4,"impulse affects ECS movement");
    auto ranged=g.SpawnEnemy({-4,0},true,EnemyKind::Ranged);
    auto& brain=g.Get<EnemyBrain>(ranged); brain.action=EnemyAction::Windup; brain.remaining=.3f;
    Hit(g,ranged,{-1,0});
    Check(g.Get<EnemyBrain>(ranged).action==EnemyAction::Approach&&g.Get<Health>(ranged).current==1,"knockback interrupts a shooter's telegraph");
    auto elite=g.SpawnEnemy({0,5},true,EnemyKind::Elite);
    g.Get<EnemyBrain>(elite).action=EnemyAction::Windup; g.Get<EnemyBrain>(elite).remaining=.3f;
    Hit(g,elite,{0,1});
    Check(g.Get<EnemyBrain>(elite).action==EnemyAction::Windup,"elite windup resists interruption");
    Near(g.Get<Knockback>(elite).velocity.y,.8f,"elite has stronger knockback resistance");
    Tick(g,20); Check(g.Get<Knockback>(armor).remaining==0,"knockback retires its movement impulse");
}

void TestSplashBoundariesAndSingleCredit() {
    auto c=Quiet(); c.maxEffects=0;
    GameModule g(c); Start(g);
    auto primary=g.SpawnEnemy({4,0},true,EnemyKind::Armored);
    auto neighbor=g.SpawnEnemy({5,0},true), distant=g.SpawnEnemy({7,0},true), warning=g.SpawnEnemy({4,.8f},false);
    g.SpawnProjectile({4,0},{}); g.SpawnProjectile({4,0},{}); g.FixedTick();
    Check(g.Get<Health>(primary).current==2&&g.Stats().kills==1&&g.Stats().splashHits==1,"direct target is not hit twice by its own splash; dead neighbors are not credited again");
    Check(g.Get<Enemy>(neighbor).phase==EnemyPhase::Dying&&g.Get<Health>(distant).current==2&&g.Get<Health>(warning).current==2,"splash respects radius and spawn immunity");
    Check(g.Get<Health>(g.PlayerEntity()).current==150&&g.Stats().effects==0,"splash has no friendly damage and does not depend on cosmetic capacity");
    Tick(g,2); Check(g.Get<Player>(g.PlayerEntity()).experience==2,"splash kill credits XP exactly once");
    Config source=c; source.expanded=false; GameModule old(source); Start(old);
    auto a=old.SpawnEnemy({4,0},true), b=old.SpawnEnemy({5,0},true); Hit(old,a);
    Check(old.Get<Health>(b).current==1&&old.Stats().splashHits==0,"source rules remain single-target despite the supplied expanded weapon table");
}

void TestCueLifetimeAndNewRun() {
    GameModule g(Quiet()); Start(g); auto e=g.SpawnEnemy({4,0},true,EnemyKind::Armored); Hit(g,e);
    Check(g.ExtractCombatCues().size()==1&&g.ExtractCombatCues()[0].kind==CombatCueKind::Blast,"impact creates a deferred blast cue");
    g.SetPaused(true); Tick(g,100); Check(g.ExtractCombatCues()[0].progress==0,"blast cue freezes with gameplay");
    g.SetPaused(false); Tick(g,30); Check(g.ExtractCombatCues().empty(),"blast cue expires independently of its target");
    auto elite=g.SpawnEnemy({6,0},true,EnemyKind::Elite); g.Get<EnemyBrain>(elite).cooldown=0; g.FixedTick();
    Check(!g.ExtractCombatCues().empty(),"windup visible before restart"); g.Restart();
    Check(g.ExtractCombatCues().empty()&&g.Stats().charges==0&&g.Stats().enemyVolleys==0,"new run resets all AI and cue state");
}

void TestStandaloneAIAndNaturalMix() {
    GameWorld world(Quiet()); world.scene=std::make_unique<ECS::Core::Scene>(); world.commands.emplace(*world.scene);
    auto players=Type<Transform>(*world.scene); world.player=world.scene->CreateEntity(players);
    auto enemies=Type<Extra,Enemy,Transform,EnemyBrain>(*world.scene); auto e=world.scene->CreateEntity(enemies);
    world.Get<Enemy>(e).phase=EnemyPhase::Alive; world.Get<Enemy>(e).kind=EnemyKind::Ranged;
    world.Get<EnemyBrain>(e).action=EnemyAction::Windup; world.Get<EnemyBrain>(e).remaining=float(SimulationStep);
    world.hostileType=Type<HostileProjectile,Transform,Velocity,Circle,Sprite,Damage>(*world.scene);
    ECS::System::Context context; context.scene=world.scene.get(); context.SetService(&world);
    ECS::System::Pipeline pipeline; pipeline.Add<EnemyAISystem>();
    Check(pipeline.Start(context)&&pipeline.Tick(context),"AI executes without GameModule or presentation components");
    Check(Count<HostileProjectile>(world)==0&&world.reservedHostileProjectiles==1,"enemy firing respects the structural boundary");
    world.commands->Playback(); Check(Count<HostileProjectile>(world)==1,"standalone deferred hostile spawn commits"); pipeline.Stop(context);
    auto c=Quiet(); c.spawning=true; c.enemySpeed=2;
    GameModule mix(c); Start(mix); Tick(mix,2400);
    Check(std::all_of(mix.Stats().spawned.begin(),mix.Stats().spawned.begin()+6,[](int n){return n>0;}),"legacy waves introduce all six original extension types");
    Check(mix.Stats().enemyVolleys>0&&mix.Stats().charges>0,"natural mixed waves execute the new attacks");
}

void TestMovingSurvivalAcrossWaves() {
    // Exercise ordinary spawning, health and upgrades with player input. No
    // forced kills, extra health or reduced enemy/weapon speeds are used.
    for (auto weapon : {WeaponKind::Wand, WeaponKind::Burst}) {
        auto c=ExpandedGameplay(); c.initialWeapon=weapon;
        c.builds=false;c.encounters=false; // Historical phase-8 survival fixture.
        GameModule g(c); Start(g);
        const glm::vec2 route[]{
            {c.maximum.x-2,c.maximum.y-2}, {c.minimum.x+2,c.maximum.y-2},
            {c.minimum.x+2,c.minimum.y+2}, {c.maximum.x-2,c.minimum.y+2},
        };
        std::size_t waypoint=0;
        for(int wave=1;wave<=2;++wave) {
            int limit=5000;
            while(limit-->0 && (g.GetState()==State::Playing || g.GetState()==State::LevelUp)) {
                if(g.GetState()==State::LevelUp) {
                    const auto upgrade=weapon==WeaponKind::Burst ? UpgradeKind::Damage : UpgradeKind(g.Get<Player>(g.PlayerEntity()).level%3);
                    Check(g.SelectUpgrade(upgrade),"moving run can consume a growth choice");
                    continue;
                }
                auto delta=route[waypoint]-g.Get<Transform>(g.PlayerEntity()).position;
                if(glm::length(delta)<.35f) {
                    waypoint=(waypoint+1)%std::size(route);
                    delta=route[waypoint]-g.Get<Transform>(g.PlayerEntity()).position;
                }
                const auto direction=glm::normalize(delta);
                g.FixedTick({direction.x,direction.y});
            }
            std::cout<<"[survival] weapon="<<int(weapon)<<" wave="<<wave<<" hp="<<g.Get<Health>(g.PlayerEntity()).current
                     <<" kills="<<g.Stats().kills<<" volleys="<<g.Stats().enemyVolleys<<" charges="<<g.Stats().charges<<'\n';
            Check(g.GetState()==State::WaveComplete,"moving player survives a complete mixed wave with normal balance");
            Check(g.Get<Player>(g.PlayerEntity()).level>0&&g.Stats().kills>0,"moving run gains growth through actual combat");
            Check(g.Stats().hostileProjectiles==0&&g.Stats().enemies==0&&g.ExtractCombatCues().empty(),"wave completion retires hostiles and telegraphs");
            const auto health=g.Get<Health>(g.PlayerEntity()).current;
            const auto growth=g.Get<CombatStats>(g.PlayerEntity());
            g.NextWave();
            Check(g.Stats().wave==wave+1&&g.Get<Health>(g.PlayerEntity()).current==health
                  &&g.Get<CombatStats>(g.PlayerEntity()).bonusDamage==growth.bonusDamage
                  &&g.Get<CombatStats>(g.PlayerEntity()).attackSpeed==growth.attackSpeed
                  &&g.Get<CombatStats>(g.PlayerEntity()).moveSpeed==growth.moveSpeed,"next wave preserves earned growth and remaining health");
        }
        Check(g.Stats().enemyVolleys>0&&g.Stats().charges>0,"moving survival includes hostile volleys and charges");
    }
}
}
int main() {
    RegisterComponents(); REGISTER_COMPONENT("brotato_enemy_combat_extra",Extra);
    const std::pair<const char*,std::function<void()>> tests[]{
        {"ranged windup and spacing",TestRangedWindupAndSpacing}, {"charge dodge, recovery and freeze",TestChargeDodgeRecoveryAndFreeze},
        {"elite volley and retired attribution",TestEliteVolleyAndRetiredAttribution}, {"hostile sweep, faction and lifecycle",TestHostileSweepFactionAndLifecycle},
        {"volley capacity and foreign archetypes",TestVolleyCapacityAndForeignArchetypes}, {"knockback and interruption",TestKnockbackResistanceAndInterrupt},
        {"splash boundaries and single credit",TestSplashBoundariesAndSingleCredit}, {"cue lifetime and new run",TestCueLifetimeAndNewRun},
        {"standalone AI and natural mix",TestStandaloneAIAndNaturalMix},
        {"moving survival across waves",TestMovingSurvivalAcrossWaves},
    };
    for(const auto& [name,test]:tests) { try {test();std::cout<<"[pass] "<<name<<'\n';} catch(const std::exception& e){std::cerr<<"[fail] "<<name<<": "<<e.what()<<'\n';return 1;} }
}
