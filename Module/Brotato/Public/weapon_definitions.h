#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <glm/glm.hpp>

namespace Brotato {
enum class WeaponKind { Wand, Torch, Laser, Knife, Gun, Burst, Count };
enum class AttackMode { Projectile, Thrust, SegmentedBeam, AuthoredBurst };
enum class Image {
    Player, Enemy, Projectile, Material, Spawn, Weapon,
    Torch, LaserWeapon, Knife, Gun, BurstWeapon, GunProjectile, BurstProjectile, LaserSegment, Muzzle,
    PlayerLegLeft, PlayerLegRight, PlayerShadow, PlayerMark, HitParticle, PlayerBody
};
constexpr std::size_t WeaponCount = static_cast<std::size_t>(WeaponKind::Count);
constexpr std::size_t WeaponIndex(WeaponKind kind) { return static_cast<std::size_t>(kind); }
inline bool ValidWeapon(WeaponKind kind) { return WeaponIndex(kind) < WeaponCount; }

struct WeaponDefinition {
    std::string name = "WAND";
    AttackMode mode = AttackMode::Projectile;
    Image image = Image::Weapon, attackImage = Image::Projectile;
    glm::vec2 size{1.089f, .239f}, attackSize{.479f, .488f};
    float cooldown = 1, range = 30, speed = 100, gravity = -9.81f, lifetime = 4;
    // A circle is a capsule with halfLength == 0. The axis follows sprite angle.
    float radius = .22f, halfLength = 0, hitOffset = 0;
    float segmentSeconds = .02f, animationSeconds = .25f;
};

inline std::array<WeaponDefinition, WeaponCount> DefaultWeapons() {
    std::array<WeaponDefinition, WeaponCount> result{};
    auto& torch = result[WeaponIndex(WeaponKind::Torch)];
    torch.name = "TORCH"; torch.mode = AttackMode::Thrust; torch.image = Image::Torch;
    torch.size = {1.f, .29f}; torch.cooldown = 1; torch.range = 10.81f; torch.speed = 56.2f;
    torch.radius = .165f; torch.halfLength = .38f; torch.hitOffset = .19f;
    auto& laser = result[WeaponIndex(WeaponKind::Laser)];
    laser.name = "TASER"; laser.mode = AttackMode::SegmentedBeam; laser.image = Image::LaserWeapon;
    laser.attackImage = Image::LaserSegment; laser.size = {.59f, .39f}; laser.attackSize = {1.0399884f, .4412072f};
    laser.cooldown = .63f; laser.range = 10; laser.radius = .2206036f; laser.halfLength = .2993906f;
    auto& knife = result[WeaponIndex(WeaponKind::Knife)];
    knife = torch; knife.name = "LIGHTNING SHIV"; knife.image = Image::Knife;
    knife.size = {.71f, .22f}; knife.cooldown = .72f; knife.radius = .09f; knife.halfLength = .23f;
    auto& gun = result[WeaponIndex(WeaponKind::Gun)];
    gun.name = "SMG"; gun.image = Image::Gun; gun.attackImage = Image::GunProjectile;
    gun.size = {.69f, .47f}; gun.attackSize = {1.02f, .40f};
    gun.cooldown = .2f; gun.range = 11.1f; gun.speed = 105; gun.radius = .2f; gun.halfLength = .31f;
    auto& burst = result[WeaponIndex(WeaponKind::Burst)];
    burst.name = "DOUBLE BARREL"; burst.mode = AttackMode::AuthoredBurst; burst.image = Image::BurstWeapon;
    burst.attackImage = Image::BurstProjectile; burst.size = {.91f, .36f}; burst.attackSize = {1.02f, .40f};
    burst.cooldown = .8f; burst.range = 10.5f; burst.speed = 0; burst.gravity = 0; burst.lifetime = .15f;
    burst.radius = .105f; burst.halfLength = .225f;
    return result;
}

// Scene-local centers already include the 0.78787 laser hierarchy scale.
inline constexpr std::array<float, 6> LaserCenters{.687f, 1.10200028f, 1.48100018f, 1.96100044f, 2.50900048f, 2.93300049f};
struct BurstPath { glm::vec2 start, end; float angle; };
// MoveGun.anim has zero endpoint tangents: cubic Hermite == smoothstep.
inline const std::array<BurstPath, 4> BurstPaths{{
    {{.317f, -.135f}, {3.146f, -.796f}, -.4038328f},
    {{.313f, .096f}, {3.076f, .555f}, .4038328f},
    {{.228f, -.328f}, {3.233f, -1.744f}, -.6540272f},
    {{.233f, .238f}, {3.148f, 1.519f}, .6540272f}
}};
} // namespace Brotato
