#pragma once

#include "engine/ECS/Component/component_loader_registry.h"
#include "engine/ECS/Entity/entity.h"
#include "weapon_definitions.h"
#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace Brotato {

template<class T> struct Component : ECS::Component::Component<T> {
    bool LoadFromMetaDataImpl(const Json::Value&, Log::StackLogErrorHandle) { return false; }
};

struct Transform : Component<Transform> { glm::vec2 position{}, previous{}; };
struct Velocity : Component<Velocity> { glm::vec2 value{}; };
struct Circle : Component<Circle> { float radius = .3f; };
struct Sprite : Component<Sprite> {
    Image image = Image::Player;
    glm::vec2 size{1};
    bool flipX = false;
    float angle = 0;
};
struct Player : Component<Player> {
    int health = 150, level = 0, experience = 0, materials = 0;
    // Integer XP: 2 / 100 replaces source's 0.02 float increments.
};
enum class WeaponPhase { Idle, Outbound, Returning, Beam };
struct Weapon : Component<Weapon> {
    ECS::EntityHandle owner{0}, target{0};
    WeaponKind kind = WeaponKind::Wand;
    WeaponPhase phase = WeaponPhase::Idle;
    float cooldown = 0, angle = 0, elapsed = 0, flash = 0;
    int activeSegments = 0;
    // Cooldowns continue across selection, so switching cannot bypass recovery.
    std::array<float, WeaponCount> cooldowns{};
};
enum class EnemyPhase { Spawning, Alive, Dying };
struct Enemy : Component<Enemy> {
    EnemyPhase phase = EnemyPhase::Spawning;
    float remaining = 1.5f;
    bool touchingPlayer = false;
};
struct Projectile : Component<Projectile> {
    float remaining = 4, age = 0, gravity = -9.81f, halfLength = 0, heading = 0;
    bool consumed = false;
    WeaponKind kind = WeaponKind::Wand;
    int path = -1;
    glm::vec2 origin{};
};
struct Pickup : Component<Pickup> { int value = 1; bool collected = false; };

inline void RegisterComponents() {
    static std::once_flag registered;
    std::call_once(registered, [] {
        REGISTER_COMPONENT("brotato_transform", Transform);
        REGISTER_COMPONENT("brotato_velocity", Velocity);
        REGISTER_COMPONENT("brotato_circle", Circle);
        REGISTER_COMPONENT("brotato_sprite", Sprite);
        REGISTER_COMPONENT("brotato_player", Player);
        REGISTER_COMPONENT("brotato_weapon", Weapon);
        REGISTER_COMPONENT("brotato_enemy", Enemy);
        REGISTER_COMPONENT("brotato_projectile", Projectile);
        REGISTER_COMPONENT("brotato_pickup", Pickup);
    });
}

struct Input {
    float horizontal = 0, vertical = 0;
    bool pause = false, restart = false, nextWave = false, revive = false;
    int selectWeapon = 0; // 0: unchanged, 1..6: source w1..w6.
};
enum class State { Playing, Paused, WaveComplete, Dead };
struct Config {
    glm::vec2 minimum{2.84f, -10.6f}, maximum{18.9f, 10.51f}, playerStart{10.80735f, -.1775364f};
    float playerSpeed = 5, enemySpeed = 2.3f;
    int initialHealth = 150, contactDamage = 10;
    std::array<WeaponDefinition, WeaponCount> weapons = DefaultWeapons();
    WeaponKind initialWeapon = WeaponKind::Wand;
    float dropChance = .5f;
    float spawnWarning = 1.5f, deathDelay = 1.5f, waveSeconds = 20, waveIncrement = 7;
    std::uint32_t seed = 2026;
    std::size_t maxEnemies = 256, maxProjectiles = 256, maxPickups = 512;
    bool spawning = true, armed = true;
};
struct Statistics {
    std::uint64_t ticks = 0;
    int wave = 1, kills = 0, shots = 0;
    std::size_t enemies = 0, projectiles = 0, pickups = 0, weapons = 0;
    double remaining = 20;
};
struct DrawSprite {
    Image image;
    glm::vec2 position, size;
    float angle = 0;
    glm::vec4 tint{1};
    bool flipX = false;
};

} // namespace Brotato
