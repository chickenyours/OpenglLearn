#pragma once
#include "Brotato/Public/game_module.h"

namespace Brotato {
class WaveSystem final : public ECS::System::System {
public:
    WaveSystem() : System("Brotato.Wave", ECS::System::Phase::Update) { Writes<Enemy>(); Writes<Player>(); Writes<ActorAnimation>(); }
    void OnTick() override { GetContext()->GetService<GameModule>()->WaveTick(); }
};
class MovementSystem final : public ECS::System::System {
public:
    MovementSystem() : System("Brotato.Movement", ECS::System::Phase::Update) {
        Reads<Player>(); Reads<Enemy>(); Writes<Transform>(); Writes<Velocity>(); Writes<Sprite>();
    }
    void OnTick() override { GetContext()->GetService<GameModule>()->Move(); }
};
class WeaponSystem final : public ECS::System::System {
public:
    WeaponSystem() : System("Brotato.Weapon", ECS::System::Phase::Update) {
        Reads<Circle>(); Writes<Transform>(); Writes<Enemy>(); Writes<Velocity>(); Writes<Sprite>(); Writes<Weapon>(); Writes<ActorAnimation>();
    }
    void OnTick() override { GetContext()->GetService<GameModule>()->Fire(); }
};
class ProjectileSystem final : public ECS::System::System {
public:
    ProjectileSystem() : System("Brotato.Projectile", ECS::System::Phase::Update) {
        Reads<Circle>(); Writes<Velocity>(); Writes<Transform>(); Writes<Projectile>(); Writes<Enemy>(); Writes<ActorAnimation>();
    }
    void OnTick() override { GetContext()->GetService<GameModule>()->Projectiles(); }
};
class ContactSystem final : public ECS::System::System {
public:
    ContactSystem() : System("Brotato.Contact", ECS::System::Phase::Update) { Reads<Transform>(); Reads<Circle>(); Writes<Player>(); Writes<Enemy>(); }
    void OnTick() override { GetContext()->GetService<GameModule>()->Contacts(); }
};
class PickupSystem final : public ECS::System::System {
public:
    PickupSystem() : System("Brotato.Pickup", ECS::System::Phase::Update) { Reads<Transform>(); Reads<Circle>(); Writes<Player>(); Writes<Pickup>(); }
    void OnTick() override { GetContext()->GetService<GameModule>()->Pickups(); }
};
class PresentationSystem final : public ECS::System::System {
public:
    PresentationSystem() : System("Brotato.Presentation", ECS::System::Phase::Update) {
        Reads<Enemy>(); Reads<Velocity>(); Writes<Transform>(); Writes<Sprite>(); Writes<ActorAnimation>(); Writes<Effect>();
    }
    void OnTick() override { GetContext()->GetService<GameModule>()->Animate(); }
};
} // namespace Brotato
