#include "Brotato/Public/game_module.h"
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace Brotato;
constexpr float Pi = 3.14159265358979323846f;
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void Near(glm::vec2 actual, glm::vec2 expected, const char* message) {
    if (!std::isfinite(actual.x) || !std::isfinite(actual.y) || glm::length(actual - expected) > 3e-6f)
        throw std::runtime_error(std::string(message) + ": expected (" + std::to_string(expected.x) +
            ", " + std::to_string(expected.y) + "), got (" + std::to_string(actual.x) + ", " + std::to_string(actual.y) + ")");
}
glm::vec2 Rotate(glm::vec2 p, float angle) {
    return {p.x * std::cos(angle) - p.y * std::sin(angle), p.x * std::sin(angle) + p.y * std::cos(angle)};
}
Config Quiet(WeaponKind kind) {
    Config c;
    c.minimum = {-100, -100}; c.maximum = {100, 100}; c.playerStart = {0, 0};
    c.spawning = false; c.enemySpeed = 0; c.contactDamage = 0; c.dropChance = 0; c.waveSeconds = 600;
    c.initialWeapon = kind;
    for (auto& w : c.weapons) w.gravity = 0;
    return c;
}
void Start(GameModule& game) { Check(game.Startup(), "game startup"); }
DrawSprite Find(GameModule& game, Image image) {
    for (const auto& sprite : game.Extract()) if (sprite.image == image) return sprite;
    throw std::runtime_error("missing render sprite");
}
struct Bullet { Projectile state; Transform transform; Velocity velocity; Sprite sprite; };
std::vector<Bullet> Bullets(GameModule& game) {
    Query<Projectile, Transform, Velocity, Sprite> query;
    query.Refresh(*game.Scene());
    std::vector<Bullet> result;
    for (auto chunk : query) for (std::size_t row = 0; row < chunk.count; ++row)
        result.push_back({chunk.Get<Projectile>()[row], chunk.Get<Transform>()[row],
                          chunk.Get<Velocity>()[row], chunk.Get<Sprite>()[row]});
    return result;
}

void TestSourceRootsAndBodyPivots() {
    // Independent facts from bound Sprite assets and child transforms in Brotato.unity.
    const std::array<glm::vec2, 6> sizes{{{1.0887257f,.23880972f}, {.999465f,.28897127f},
        {.58880974f,.38897125f}, {.7089713f,.21919838f}, {.6893034f,.46847755f}, {.909465f,.35880974f}}};
    const std::array<glm::vec2, 6> pivots{{{.51389146f,.5202418f}, {.5f,.55105406f},
        {.61011016f,.79628634f}, {.5779254f,.547265f}, {.5361513f,.7668218f}, {.47251132f,.7782364f}}};
    const std::array<glm::vec2, 6> offsets{{{.22f,0}, {.22f,.05f}, {.22f,.05f}, {.22f,0}, {.22f,0}, {.22f,.056f}}};
    for (std::size_t index = 0; index < WeaponCount; ++index) {
        auto c = Quiet(WeaponKind(index)); c.playerStart = {4,-3};
        GameModule game(c); Start(game);
        const glm::vec2 root = c.playerStart + (index == 0 ? glm::vec2(.014f,-.029f) : glm::vec2(0));
        Near(game.Get<Transform>(game.WeaponEntity()).position, root, "physical root comes from the scene binding");
        const auto body = Find(game, game.Definition(WeaponKind(index)).image);
        Check(body.affine, "source weapon body uses affine extraction");
        glm::vec2 center, axisX, axisY;
        const auto pivotDelta = (glm::vec2(.5f) - pivots[index]) * sizes[index];
        if (index == 0) {
            const float childAngle = std::atan2(.0286194765f, .999590379f);
            center = root + Rotate(offsets[index] + Rotate(pivotDelta, childAngle), .530580027f);
            axisX = Rotate({sizes[index].x,0}, .530580027f + childAngle);
            axisY = Rotate({0,sizes[index].y}, .530580027f + childAngle);
        } else {
            // The initial 3D quaternion projects to X=(0,1), Y=(1,0).
            const auto local = offsets[index] + pivotDelta;
            center = root + glm::vec2(local.y, local.x);
            axisX = {0, sizes[index].x}; axisY = {sizes[index].y,0};
        }
        Near(body.position, center, "visual center includes child offset and noncentral pivot");
        Near(body.axisX, axisX, "source initial body X axis");
        Near(body.axisY, axisY, "source initial body Y axis retains reflection");
        Near(body.size, sizes[index], "simple Sprite ignores serialized renderer m_Size");
        Check(game.Get<Weapon>(game.WeaponEntity()).reflected == (index != 0), "source idle reflection is retained");
        game.Get<Sprite>(game.PlayerEntity()).flipX = true;
        Near(Find(game, body.image).position, center, "player body flip does not affect sibling weapon geometry");
        game.Get<Transform>(game.PlayerEntity()).position += glm::vec2(-2,1);
        game.FixedTick();
        Near(game.Get<Transform>(game.WeaponEntity()).position, root + glm::vec2(-2,1), "idle weapon follows its owner with source root offset");
        Near(Find(game, body.image).position, center + glm::vec2(-2,1), "body center follows translated root");
    }
}

void TestCardinalFirePointsAndPelletTracks() {
    const std::array<glm::vec2, 4> starts{{{.317f,-.135f},{.313f,.096f},{.228f,-.328f},{.233f,.238f}}};
    const std::array<glm::vec2, 4> ends{{{3.146f,-.796f},{3.076f,.555f},{3.233f,-1.744f},{3.148f,1.519f}}};
    for (auto kind : {WeaponKind::Wand, WeaponKind::Gun, WeaponKind::Burst}) {
        for (float angle : {0.f, Pi*.5f, Pi, -Pi*.5f}) {
            GameModule game(Quiet(kind)); Start(game);
            const glm::vec2 root = kind == WeaponKind::Wand ? glm::vec2(.014f,-.029f) : glm::vec2(0);
            const glm::vec2 local = kind == WeaponKind::Wand ? glm::vec2(.692806249f,.0135370124f) :
                kind == WeaponKind::Gun ? glm::vec2(.516f,-.049f) : glm::vec2(.628f,.053f);
            game.SpawnEnemy(root + Rotate({8,0}, angle), true); game.FixedTick();
            const auto fire = root + Rotate(local, angle);
            const auto bullets = Bullets(game);
            Check(bullets.size() == (kind == WeaponKind::Burst ? 4 : 1), "one accepted attack creates source projectile count");
            Check(!game.Get<Weapon>(game.WeaponEntity()).reflected, "runtime aiming replaces initial reflected world rotation");
            for (const auto& bullet : bullets) {
                Near(bullet.state.origin, fire, "ECS projectile origin is the actual bound TransformShoot");
                Near(Rotate({1,0}, bullet.state.heading), Rotate({1,0}, angle), "projectile heading follows rotator rather than displaced fire point");
                if (kind == WeaponKind::Burst) {
                    Check(bullet.state.path >= 0 && bullet.state.path < 4, "pellet keeps its animation path");
                    Near(bullet.transform.position, fire + Rotate(starts[bullet.state.path], angle), "pellet starts relative to the rotated fire point");
                } else {
                    Near(bullet.transform.position, fire, "new projectile is created after simulation queries finish");
                    const auto speed = game.Definition(kind).speed;
                    Near(bullet.velocity.value / speed, Rotate({1,0}, angle), "projectile velocity follows source firing direction");
                    Check(std::abs(glm::length(bullet.velocity.value) - speed) < 1e-4f, "projectile keeps source speed");
                }
            }
            const auto events = game.DrainEvents();
            Check(events.size() == 1 && events[0].kind == GameEventKind::WeaponAttack, "fire produces one attack event");
            Near(events[0].position, fire, "attack event captures the real firing point");
            if (kind == WeaponKind::Burst) {
                for (int i=0; i<6; ++i) game.FixedTick();
                for (const auto& bullet : Bullets(game)) {
                    const auto path = starts[bullet.state.path] + (ends[bullet.state.path] - starts[bullet.state.path]) * .104f;
                    Near(bullet.transform.position, fire + Rotate(path, angle), "Hermite pellet path rotates with the parent at every cardinal direction");
                }
            } else if (kind == WeaponKind::Wand) {
                const auto draw = Find(game, Image::Projectile);
                const glm::vec2 pivotOffset{(.5f-.5002063f)*.47867508f, (.5f-.48976415f)*.48847755f};
                Near(draw.position, fire + Rotate(pivotOffset, angle), "projectile visual pivot stays separate from physical origin");
                (void)game.Extract();
                Near(Bullets(game)[0].transform.position, fire, "render extraction cannot move projectile physics");
            }
        }
    }
}

void TestMuzzleParentAndReflectedSprite() {
    for (auto kind : {WeaponKind::Gun, WeaponKind::Burst}) for (float angle : {0.f, Pi*.5f}) {
        GameModule game(Quiet(kind)); Start(game);
        game.SpawnEnemy(Rotate({8,0},angle), true); game.FixedTick();
        const auto flash = Find(game, Image::Muzzle);
        const glm::vec2 center = kind == WeaponKind::Gun ? glm::vec2(.610f,-.024f) : glm::vec2(.722f,.078f);
        Near(flash.position, Rotate(center,angle), "muzzle combines its fire-point parent and animation position");
        Near(flash.axisX, Rotate({0,1},angle), "muzzle source quaternion swaps XY axes");
        Near(flash.axisY, Rotate({.96f,0},angle), "muzzle source reflection is preserved under world aiming");
        Near(flash.size, {1,.96f}, "muzzle uses native Sprite size rather than prior half-size approximation");
        Check(flash.affine && flash.tint.a == 1, "initial muzzle sample is opaque and affine");
    }
}

void TestThrustPhysicalRootAndReturnPose() {
    for (auto kind : {WeaponKind::Torch, WeaponKind::Knife}) {
        auto c = Quiet(kind); c.weapons[WeaponIndex(kind)].cooldown = 10;
        GameModule game(c); Start(game); const auto target = game.SpawnEnemy({8,0}, true);
        game.FixedTick();
        Near(game.Get<Transform>(game.WeaponEntity()).previous, {0,0}, "thrust starts from owner origin without an artificial mount distance");
        Near(game.Get<Transform>(game.WeaponEntity()).position, {56.2f/120.f,0}, "first outward step uses the source thrust speed");
        auto& old = game.Get<Enemy>(target); old.phase = EnemyPhase::Dying; old.remaining = .001f;
        game.Get<Transform>(game.PlayerEntity()).position = {-1,1};
        for (int i=0; i<30 && game.Get<Weapon>(game.WeaponEntity()).phase != WeaponPhase::Idle; ++i) game.FixedTick();
        const auto& weapon = game.Get<Weapon>(game.WeaponEntity());
        Check(weapon.phase == WeaponPhase::Idle && weapon.reflected, "return restores authored initial reflected pose");
        Near(game.Get<Transform>(game.WeaponEntity()).position, {-1,1}, "return follows the current physical owner origin");
        Near(Rotate({1,0},weapon.angle), {0,1}, "return restores source initial world angle");
    }
}

void TestLaserRenderAndFarCollisionBoundary() {
    GameModule game(Quiet(WeaponKind::Laser)); Start(game); game.SpawnEnemy({9,0}, true);
    for (int i=0; i<25 && game.Get<Weapon>(game.WeaponEntity()).activeSegments != 6; ++i) game.FixedTick();
    Check(game.Get<Weapon>(game.WeaponEntity()).activeSegments == 6, "laser reaches all six source segments");
    const std::array<float,6> centers{.687f,1.10200028f,1.48100018f,1.96100044f,2.50900048f,2.93300049f};
    std::size_t index=0;
    for (const auto& draw : game.Extract()) if (draw.image == Image::LaserSegment) {
        Check(index < centers.size(), "no excess beam segments");
        Near(draw.position, {centers[index++],0}, "beam center is relative to owner origin");
        Near(draw.axisX, {1.0399884f,0}, "beam source scale applies once");
        Near(draw.axisY, {0,.4412072f}, "beam source world width");
    }
    Check(index == 6, "each source segment is rendered");
    const auto inside = game.SpawnEnemy({3.7f,0},true);
    const auto outside = game.SpawnEnemy({3.95f,0},true);
    game.FixedTick();
    Check(game.Get<Enemy>(inside).phase == EnemyPhase::Dying, "sixth segment reaches the source far collision boundary");
    Check(game.Get<Enemy>(outside).phase == EnemyPhase::Alive, "removed mount offset cannot extend laser range by 0.55 units");
}
} // namespace

int main() {
    const std::vector<std::pair<const char*,std::function<void()>>> tests{
        {"source roots and noncentral body pivots", TestSourceRootsAndBodyPivots},
        {"cardinal fire points and authored pellet tracks", TestCardinalFirePointsAndPelletTracks},
        {"muzzle parent and reflected Sprite", TestMuzzleParentAndReflectedSprite},
        {"thrust physical root and source return pose", TestThrustPhysicalRootAndReturnPose},
        {"laser render and far collision boundary", TestLaserRenderAndFarCollisionBoundary},
    };
    int failed=0;
    for (const auto& [name,test] : tests) {
        try { test(); std::cout << "[PASS] " << name << '\n'; }
        catch (const std::exception& e) { ++failed; std::cerr << "[FAIL] " << name << ": " << e.what() << '\n'; }
    }
    std::cout << tests.size()-failed << '/' << tests.size() << " weapon geometry checks passed\n";
    return failed ? 1 : 0;
}
