#pragma once
#include "Brotato/Public/game_world.h"
#include "engine/ECS/System/system.h"

namespace Brotato {
class BuildSystem final : public ECS::System::System {
public:
    BuildSystem() : System("Brotato.Build", ECS::System::Phase::Update) { Reads<OwnedItem>(); Reads<Weapon>(); Reads<Growth>(); Reads<Player>(); Reads<CharacterProfile>(); Writes<CombatStats>(); Writes<BuildBase>(); Writes<Health>(); }
    void OnTick() override;
};
class StatusSystem final : public ECS::System::System {
public:
    StatusSystem() : System("Brotato.Status", ECS::System::Phase::Update) { Reads<Transform>(); Writes<TimedStatus>(); Writes<Enemy>(); Writes<Health>(); Writes<Velocity>(); Writes<ActorAnimation>(); }
    void OnTick() override;
};
class EnemyAISystem final : public ECS::System::System {
public:
    EnemyAISystem() : System("Brotato.EnemyAI", ECS::System::Phase::Update) { Reads<Enemy>(); Reads<BossBrain>(); Reads<RunRules>(); Reads<Transform>(); Writes<Health>(); Writes<EnemyBrain>(); }
    void OnTick() override;
};
class BossAISystem final : public ECS::System::System {
public:
    BossAISystem() : System("Brotato.BossAI", ECS::System::Phase::Update) { Reads<Enemy>(); Reads<Health>(); Reads<RunRules>(); Reads<Transform>(); Writes<BossBrain>(); }
    void OnTick() override;
};
class RunSystem final : public ECS::System::System {
public:
    RunSystem() : System("Brotato.Run", ECS::System::Phase::Update) { Reads<Player>(); Reads<Health>(); Reads<Enemy>(); Reads<BossBrain>(); Reads<OwnedItem>(); Reads<Weapon>(); Reads<CombatStats>(); Reads<Transform>(); Reads<Sprite>(); Reads<Damage>(); Reads<CharacterProfile>(); Reads<RunRules>(); Writes<RunProgress>(); }
    void OnTick() override;
};
class HealthSystem final : public ECS::System::System {
public:
    HealthSystem() : System("Brotato.Health", ECS::System::Phase::Update) { Writes<Health>(); }
    void OnTick() override;
};
class GrowthSystem final : public ECS::System::System {
public:
    GrowthSystem() : System("Brotato.Growth", ECS::System::Phase::Update) { Writes<Player>(); Writes<Growth>(); Reads<CombatStats>(); }
    void OnTick() override;
};
class WaveSystem final : public ECS::System::System {
public:
    WaveSystem() : System("Brotato.Wave", ECS::System::Phase::Update) { Reads<Transform>(); Reads<RunRules>(); Writes<RunProgress>(); Writes<Enemy>(); Writes<Player>(); Writes<ActorAnimation>(); }
    void OnTick() override;
};
class EncounterSystem final : public ECS::System::System {
public:
    EncounterSystem() : System("Brotato.Encounter", ECS::System::Phase::Update) { Reads<Enemy>(); Reads<Transform>(); Reads<RunRules>(); Writes<SpawnDirector>(); }
    void OnTick() override;
};
class MovementSystem final : public ECS::System::System {
public:
    MovementSystem() : System("Brotato.Movement", ECS::System::Phase::Update) {
        Reads<Player>(); Reads<Enemy>(); Reads<CombatStats>(); Reads<EnemyBrain>(); Reads<BossBrain>(); Reads<RunRules>(); Writes<Knockback>(); Writes<Transform>(); Writes<Velocity>(); Writes<Sprite>();
    }
    void OnTick() override;
};
class WeaponSystem final : public ECS::System::System {
public:
    WeaponSystem() : System("Brotato.Weapon", ECS::System::Phase::Update) {
        Reads<Circle>(); Reads<CombatStats>(); Writes<TimedStatus>(); Writes<BuildBase>(); Writes<Knockback>(); Writes<EnemyBrain>(); Writes<Health>(); Writes<Damage>(); Writes<Transform>(); Writes<Enemy>(); Writes<Velocity>(); Writes<Sprite>(); Writes<Weapon>(); Writes<ActorAnimation>();
    }
    void OnTick() override;
};
class ProjectileSystem final : public ECS::System::System {
public:
    ProjectileSystem() : System("Brotato.Projectile", ECS::System::Phase::Update) {
        Reads<Circle>(); Reads<Sprite>(); Reads<Damage>(); Writes<TimedStatus>(); Writes<BuildBase>(); Writes<Knockback>(); Writes<EnemyBrain>(); Writes<Health>(); Writes<Velocity>(); Writes<Transform>(); Writes<Projectile>(); Writes<HostileProjectile>(); Writes<Enemy>(); Writes<ActorAnimation>();
    }
    void OnTick() override;
};
class ContactSystem final : public ECS::System::System {
public:
    ContactSystem() : System("Brotato.Contact", ECS::System::Phase::Update) { Reads<Transform>(); Reads<Circle>(); Reads<Damage>(); Writes<Health>(); Writes<Enemy>(); }
    void OnTick() override;
};
class PickupSystem final : public ECS::System::System {
public:
    PickupSystem() : System("Brotato.Pickup", ECS::System::Phase::Update) { Reads<Transform>(); Reads<Circle>(); Writes<Player>(); Writes<Pickup>(); }
    void OnTick() override;
};
class PresentationSystem final : public ECS::System::System {
public:
    PresentationSystem() : System("Brotato.Presentation", ECS::System::Phase::Update) {
        Reads<Player>(); Reads<Enemy>(); Reads<Velocity>(); Writes<Transform>(); Writes<Sprite>(); Writes<ActorAnimation>(); Writes<Effect>();
    }
    void OnTick() override;
};
} // namespace Brotato
