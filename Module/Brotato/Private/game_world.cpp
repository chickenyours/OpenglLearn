#include "Brotato/Public/game_world.h"
#include "Brotato/Public/combat_geometry.h"
#include "Brotato/Public/enemy_art_catalog.h"
#include <cmath>

namespace Brotato {
namespace {
bool Finite(glm::vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }
void InitializeEnemy(GameWorld& world, ECS::EntityHandle entity, glm::vec2 position, bool ready, EnemyKind kind, ChampionKind champion = ChampionKind::None, bool rewards = true) {
    auto& transform = world.Get<Transform>(entity); transform.position = transform.previous = position;
    auto& enemy = world.Get<Enemy>(entity);
    enemy.phase = ready ? EnemyPhase::Alive : EnemyPhase::Spawning;
    enemy.remaining = ready ? 0 : world.config.spawnWarning;
    const auto& definition = EnemyDefinitions.at(static_cast<std::size_t>(kind));
    enemy.kind = kind; enemy.speed = world.config.expanded ? definition.speed : 1;
    enemy.experience = world.config.expanded ? definition.experience : 2;
    enemy.rewards = rewards; if(!rewards)enemy.experience=0;
    int healthPercent=100;
    if(UsesEncounters(world)) {
        enemy.birthWave=std::clamp(world.stats.wave,1,20);
        enemy.damagePercent=100+8*(enemy.birthWave-1);
        enemy.growthSpeed=1+.035f*(enemy.birthWave-1);
        healthPercent=WaveHealthPercent(enemy.birthWave);
        enemy.champion=champion;
        if(champion==ChampionKind::Swift){enemy.growthSpeed*=1.3f;healthPercent=healthPercent*150/100;}
        if(champion==ChampionKind::Bulwark){enemy.growthSpeed*=.85f;healthPercent=healthPercent*250/100;enemy.damagePercent=enemy.damagePercent*120/100;}
        if(champion!=ChampionKind::None)++world.stats.championSpawns;
    }
    auto& health = world.Get<Health>(entity); health.current = health.maximum = ScaleRuleValue(ScaleRuleValue(world.config.expanded ? definition.health : 1,healthPercent),DifficultyRules(world).enemyHealth);
    world.Get<Damage>(entity).amount = EnemyAttackDamage(world,enemy,int(std::round(world.config.contactDamage * (world.config.expanded ? definition.contactDamage : 1))));
    world.Get<Circle>(entity).radius = world.config.expanded ? definition.radius : .43f;
    auto& sprite = world.Get<Sprite>(entity); sprite.image = Image::Enemy; sprite.size = {.769f, .898f};
    if (world.config.expanded && kind != EnemyKind::Normal) {
        constexpr Image images[]{Image::EnemyFast, Image::EnemyArmored, Image::EnemyRanged, Image::EnemyCharger, Image::EnemyElite, Image::EnemyHealer, Image::EnemySummoner};
        constexpr float scales[]{.75f,1.3f,1,1.05f,1.65f,1,1.15f};
        const auto index = static_cast<std::size_t>(kind) - 1;
        sprite.image = images[index];
        const auto size = ExtraEnemyArt[index<5?index:index+1].size;
        // Same recovered units, scaled to express these new enemy roles.
        sprite.size = size * scales[index];
    }
    if(enemy.champion!=ChampionKind::None){sprite.size*=1.15f;world.Get<Circle>(entity).radius*=1.15f;}
    ++world.stats.spawned[static_cast<std::size_t>(kind)];
    ResetActorAnimation(world, entity, ActorClip::EnemyMove);
}
void InitializePickup(GameWorld& world, ECS::EntityHandle entity, glm::vec2 position) {
    auto& transform = world.Get<Transform>(entity); transform.position = transform.previous = position;
    world.Get<Circle>(entity).radius = .28f;
    auto& sprite = world.Get<Sprite>(entity); sprite.image = Image::Material; sprite.size = {.679f, .718f};
}
void InitializeBoss(GameWorld& world, ECS::EntityHandle entity, glm::vec2 position, bool ready, bool finalBoss, bool objective) {
    InitializeEnemy(world,entity,position,ready,EnemyKind::Elite);
    auto& boss=world.Get<BossBrain>(entity);boss=BossBrain{};boss.wave=world.stats.wave;boss.finalBoss=finalBoss;boss.objective=objective;
    auto& health=world.Get<Health>(entity);health.current=health.maximum=ScaleRuleValue(ScaleRuleValue(finalBoss?world.config.finalBossHealth:world.config.bossHealth,UsesEncounters(world)?100+25*(std::clamp(world.stats.wave,1,20)-1):100),DifficultyRules(world).bossHealth);
    world.Get<Enemy>(entity).speed=.65f;world.Get<Enemy>(entity).experience=finalBoss?24:12;
    world.Get<Circle>(entity).radius=.82f;
    world.Get<Damage>(entity).amount=EnemyAttackDamage(world,world.Get<Enemy>(entity),int(std::round(world.config.contactDamage*(finalBoss?2.f:1.5f))));
    auto& sprite=world.Get<Sprite>(entity);sprite.image=Image::EnemyBoss;sprite.size=ExtraEnemyArt[5].size*1.2f;
    ++world.stats.bossSpawns;
}
void InitializeProjectile(GameWorld& world, ECS::EntityHandle entity, glm::vec2 position,
                          glm::vec2 velocity, WeaponKind kind, int path, float heading, int damage) {
    const auto& definition = world.Definition(kind);
    auto& transform = world.Get<Transform>(entity); transform.position = transform.previous = position;
    world.Get<Velocity>(entity).value = velocity;
    auto& projectile = world.Get<Projectile>(entity);
    projectile.remaining = definition.lifetime; projectile.gravity = definition.gravity;
    projectile.halfLength = definition.halfLength; projectile.kind = kind;
    projectile.path = path; projectile.heading = heading; projectile.origin = position;
    world.Get<Circle>(entity).radius = definition.radius;
    auto& sprite = world.Get<Sprite>(entity);
    sprite.image = definition.attackImage; sprite.size = definition.attackSize; sprite.angle = heading;
    auto payload = AttackPayload(world, world.player, kind);
    if (damage >= 0) payload.amount = damage;
    world.Get<Damage>(entity) = payload;
    if (path >= 0) {
        transform.position = transform.previous = position + Combat::Rotate(BurstPaths.at(path).start, heading);
        sprite.angle += BurstPaths.at(path).angle;
    }
}
void InitializeHostile(GameWorld& world, ECS::EntityHandle entity, glm::vec2 position, glm::vec2 velocity, int damage, ECS::EntityHandle source) {
    auto& transform = world.Get<Transform>(entity); transform.position = transform.previous = position;
    world.Get<Velocity>(entity).value = velocity;
    world.Get<Circle>(entity).radius = .14f;
    auto& sprite = world.Get<Sprite>(entity); sprite.image = Image::EnemyProjectile; sprite.size = glm::vec2(.32f);
    sprite.angle = std::atan2(velocity.y, velocity.x);
    world.Get<Damage>(entity).amount = damage;
    world.Get<HostileProjectile>(entity).source = source;
}
template<class T> void RetireMatching(GameWorld& world) {
    Query<T> query; query.Refresh(*world.scene);
    for (auto chunk : query)
        for (std::size_t row = 0; row < chunk.count; ++row)
            world.commands->Destroy(chunk.Entity(row, *world.scene));
}
}

ECS::EntityHandle SpawnEnemy(GameWorld& world, glm::vec2 position, bool ready, EnemyKind kind, ChampionKind champion) {
    if (!world.scene || !Finite(position) || static_cast<std::size_t>(kind) >= EnemyDefinitions.size() || unsigned(champion)>unsigned(ChampionKind::Bulwark) || Count<Enemy>(world) >= world.config.maxEnemies) return ECS::EntityHandle(0);
    const auto entity = world.scene->CreateEntity(world.enemyType);
    InitializeEnemy(world, entity, position, ready, kind, champion); RefreshCounts(world); return entity;
}
ECS::EntityHandle SpawnPickup(GameWorld& world, glm::vec2 position) {
    if (!world.scene || !Finite(position) || Count<Pickup>(world) >= world.config.maxPickups) return ECS::EntityHandle(0);
    const auto entity = world.scene->CreateEntity(world.pickupType);
    InitializePickup(world, entity, position); RefreshCounts(world); return entity;
}
ECS::EntityHandle SpawnBoss(GameWorld& world, glm::vec2 position, bool ready, bool finalBoss, bool objective) {
    if(!world.scene || !world.bossType || !world.config.expanded || !Finite(position) || Count<Enemy>(world)>=world.config.maxEnemies)return ECS::EntityHandle(0);
    const auto entity=world.scene->CreateEntity(world.bossType);InitializeBoss(world,entity,position,ready,finalBoss,objective);RefreshCounts(world);return entity;
}
void QueueBoss(GameWorld& world) {
    auto* progress=world.scene->TryGetComponent<RunProgress>(world.player);
    if(!world.commands || !world.bossType || !progress || progress->bossSpawned)return;
    const auto player=world.Get<Transform>(world.player).position;
    auto position=glm::clamp(player+glm::vec2(0,5),world.config.minimum,world.config.maximum);
    if(UsesEncounters(world) && glm::distance(position,player)<4)
        for(const auto offset:{glm::vec2(5,0),glm::vec2(-5,0),glm::vec2(0,-5)}){
            const auto candidate=glm::clamp(player+offset,world.config.minimum,world.config.maximum);
            if(glm::distance(candidate,player)>glm::distance(position,player))position=candidate;
        }
    ++world.reservedEnemies;
    world.commands->Create(world.bossType,[&world,position](auto& scene,auto entity){
        if(Count<Enemy>(world)>world.config.maxEnemies){scene.DeleteEntity(entity);return;}
        InitializeBoss(world,entity,position,false,world.stats.wave==world.config.campaignWaves,true);
        if(auto* p=scene.template TryGetComponent<RunProgress>(world.player))p->bossSpawned=true;
    });
}
ECS::EntityHandle CreateProjectile(GameWorld& world, glm::vec2 position, glm::vec2 velocity,
                                    WeaponKind kind, int path, float heading, int damage) {
    if (!world.scene || !Finite(position) || !Finite(velocity) ||
        Count<Projectile>(world) >= world.config.maxProjectiles) return ECS::EntityHandle(0);
    const auto entity = world.scene->CreateEntity(world.projectileType);
    InitializeProjectile(world, entity, position, velocity, kind, path, heading, damage);
    RefreshCounts(world); return entity;
}
void QueueEnemy(GameWorld& world, glm::vec2 position, EnemyKind kind) {
    if (!world.commands || !Finite(position) || std::size_t(kind)>=EnemyCount) return;
    ++world.reservedEnemies;
    world.commands->Create(world.enemyType, [&world, position, kind](auto& scene, auto entity) {
        // Capacity is checked after retirements, so the last dying enemy can
        // release its slot for a new warning in this same tick.
        if (Count<Enemy>(world) > world.config.maxEnemies) { scene.DeleteEntity(entity); return; }
        InitializeEnemy(world, entity, position, false, kind);
    });
}
bool QueueEnemyPack(GameWorld& world, std::span<const EnemySpawn> pack) {
    if(!world.commands || !world.enemyType || pack.empty() || pack.size()>12 ||
       Count<Enemy>(world)+world.reservedEnemies+pack.size()>world.config.maxEnemies)return false;
    for(const auto& spawn:pack)if(!Finite(spawn.position) || std::size_t(spawn.kind)>=EnemyCount || unsigned(spawn.champion)>unsigned(ChampionKind::Bulwark))return false;
    world.reservedEnemies+=pack.size();
    // No borrowed row pointer escapes into a deferred initializer.
    for(const auto spawn:pack)world.commands->Create(world.enemyType,[&world,spawn](auto&,auto entity){
        InitializeEnemy(world,entity,spawn.position,false,spawn.kind,spawn.champion,spawn.rewards);
        world.Get<Enemy>(entity).summonedBy=spawn.summonedBy;
        if(!spawn.rewards)++world.stats.summonedEnemies;
    });
    return true;
}
void QueuePickup(GameWorld& world, glm::vec2 position) {
    if (!world.commands || !Finite(position)) return;
    ++world.reservedPickups;
    world.commands->Create(world.pickupType, [&world, position](auto& scene, auto entity) {
        if (Count<Pickup>(world) > world.config.maxPickups) { scene.DeleteEntity(entity); return; }
        InitializePickup(world, entity, position);
    });
}
void QueueProjectile(GameWorld& world, glm::vec2 position, glm::vec2 velocity,
                     WeaponKind kind, int path, float heading, int damage) {
    if (!world.commands || !Finite(position) || !Finite(velocity) ||
        Count<Projectile>(world) + world.reservedProjectiles >= world.config.maxProjectiles) return;
    ++world.reservedProjectiles;
    world.commands->Create(world.projectileType, [&world, position, velocity, kind, path, heading, damage](auto& scene, auto entity) {
        if (Count<Projectile>(world) > world.config.maxProjectiles) { scene.DeleteEntity(entity); return; }
        InitializeProjectile(world, entity, position, velocity, kind, path, heading, damage);
    });
}
ECS::EntityHandle CreateHostileProjectile(GameWorld& world, glm::vec2 position, glm::vec2 velocity, int damage, ECS::EntityHandle source) {
    if (!world.scene || !world.config.expanded || !Finite(position) || !Finite(velocity) || damage < 0 || damage > 100000 ||
        Count<HostileProjectile>(world) >= world.config.maxHostileProjectiles) return ECS::EntityHandle(0);
    const auto entity = world.scene->CreateEntity(world.hostileType);
    InitializeHostile(world, entity, position, velocity, damage, source); RefreshCounts(world); return entity;
}
bool QueueHostileVolley(GameWorld& world, ECS::EntityHandle source, glm::vec2 position, glm::vec2 direction, int count, int damage) {
    if (!world.commands || !world.config.expanded || (count != 1 && count != 3) || !Finite(position) || !Finite(direction) ||
        glm::length(direction) < 1e-6f || damage < 0 || damage > 100000 ||
        Count<HostileProjectile>(world) + world.reservedHostileProjectiles + count > world.config.maxHostileProjectiles) return false;
    direction = glm::normalize(direction);
    world.reservedHostileProjectiles += count;
    for (int i = 0; i < count; ++i) {
        const auto velocity = Combat::Rotate(direction, (i - (count-1)*.5f)*.23f) * 7.f;
        world.commands->Create(world.hostileType, [&world,source,position,velocity,damage](auto&,auto entity) {
            InitializeHostile(world,entity,position,velocity,damage,source);
        });
    }
    ++world.stats.enemyVolleys;
    return true;
}
bool QueueHostilePattern(GameWorld& world, ECS::EntityHandle source, glm::vec2 position, glm::vec2 direction, int count, int damage, bool ring) {
    if(!world.commands || !world.config.expanded || count<1 || count>12 || !Finite(position) || !Finite(direction) || glm::length(direction)<1e-6f || damage<0 || damage>100000 || Count<HostileProjectile>(world)+world.reservedHostileProjectiles+count>world.config.maxHostileProjectiles)return false;
    direction=glm::normalize(direction);world.reservedHostileProjectiles+=count;
    for(int i=0;i<count;++i){
        const float angle=ring?6.283185307f*i/count:(i-(count-1)*.5f)*.24f;
        const auto velocity=Combat::Rotate(direction,angle)*(ring?5.5f:7.f);
        world.commands->Create(world.hostileType,[&world,source,position,velocity,damage](auto&,auto entity){InitializeHostile(world,entity,position,velocity,damage,source);});
    }
    ++world.stats.enemyVolleys;return true;
}
void QueueAttackProjectile(GameWorld& world, glm::vec2 position, glm::vec2 velocity,
                           WeaponKind kind, int path, float heading, Damage payload) {
    if (!world.commands || !Finite(position) || !Finite(velocity) ||
        Count<Projectile>(world) + world.reservedProjectiles >= world.config.maxProjectiles) return;
    ++world.reservedProjectiles;
    world.commands->Create(world.projectileType, [&world,position,velocity,kind,path,heading,payload](auto& scene,auto entity) {
        if (Count<Projectile>(world) > world.config.maxProjectiles) { scene.DeleteEntity(entity); return; }
        InitializeProjectile(world,entity,position,velocity,kind,path,heading,payload.amount);
        world.Get<Damage>(entity)=payload;
        world.Get<Projectile>(entity).pierces=UsesTraits(world) ? std::clamp(payload.pierce,0,3) : 0;
    });
}
void RefreshCounts(GameWorld& world) {
    world.stats.enemies = Count<Enemy>(world); world.stats.projectiles = Count<Projectile>(world);
    world.stats.pickups = Count<Pickup>(world); world.stats.effects = Count<Effect>(world);
    world.stats.weapons = Count<Weapon>(world);
    world.stats.hostileProjectiles = Count<HostileProjectile>(world);
    world.stats.ownedItems = Count<OwnedItem>(world);
    world.stats.statuses = Count<TimedStatus>(world);
}
void ClearTransient(GameWorld& world) {
    ResetPresentation(world);
    if (!world.scene) return;
    world.commands->Clear(); world.effectCommands->Clear();
    RetireMatching<Enemy>(world); RetireMatching<Projectile>(world);
    RetireMatching<HostileProjectile>(world);
    RetireMatching<TimedStatus>(world);
    RetireMatching<Pickup>(world); RetireMatching<Effect>(world);
    world.commands->Playback();
    world.reservedEnemies = world.reservedProjectiles = world.reservedPickups = world.reservedEffects = 0;
    world.reservedHostileProjectiles = 0;
    world.reservedStatuses = 0;
    ResetWeapon(world, true); RefreshCounts(world);
}
} // namespace Brotato
