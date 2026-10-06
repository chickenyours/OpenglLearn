#include "Brotato/Public/game_world.h"
#include "Brotato/Public/animation_catalog.h"
#include "Brotato/Public/combat_geometry.h"
#include "Brotato/Public/weapon_geometry.h"
#include "Brotato/Public/enemy_art_catalog.h"
#include "Brotato/Public/enemy_behavior.h"
#include <algorithm>
#include <cmath>
#include <span>

namespace Brotato {
namespace {
glm::mat2 Basis(const Render::Animation::Pose& pose) {
    const float cosine = std::cos(pose.angle), sine = std::sin(pose.angle);
    return {{cosine * pose.scale.x, sine * pose.scale.x}, {-sine * pose.scale.y, cosine * pose.scale.y}};
}
DrawSprite SpriteSnapshot(const Transform& transform, const Sprite& sprite) {
    return {sprite.image, transform.position, sprite.size, sprite.angle, glm::vec4(1), sprite.flipX};
}
DrawSprite GeometrySnapshot(Image image, glm::vec2 origin, glm::mat2 parent,
                            const LocalSpriteGeometry& geometry, glm::vec2 size) {
    const auto matrix = parent * glm::mat2(geometry.axisX, geometry.axisY);
    const auto axisX = matrix[0] * size.x, axisY = matrix[1] * size.y;
    const auto center = origin + parent * geometry.offset + matrix * ((glm::vec2(.5f) - geometry.pivot) * size);
    DrawSprite result{image, center, {glm::length(axisX), glm::length(axisY)}, std::atan2(axisX.y, axisX.x)};
    result.axisX = axisX; result.axisY = axisY; result.affine = true;
    return result;
}
void AppendActorSprites(std::vector<DrawSprite>& result, const Transform& transform, const Sprite& sprite,
                        const ActorAnimation& animation, bool player, std::size_t character) {
    const std::span<const AnimatedNode> nodes = player ? std::span<const AnimatedNode>(PlayerNodes) : std::span<const AnimatedNode>(EnemyNodes);
    std::array<glm::mat2, ActorAnimation::MaxNodes> matrices{};
    std::array<glm::vec2, ActorAnimation::MaxNodes> positions{};
    std::array<float, ActorAnimation::MaxNodes> opacity{};
    struct OrderedSprite { int order = 0; DrawSprite draw{}; };
    std::array<OrderedSprite, ActorAnimation::MaxNodes> ordered{};
    std::size_t count = 0;
    const glm::mat2 rootBasis{{sprite.flipX ? -1.f : 1.f, 0}, {0, 1}};
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const auto& node = nodes[index]; const auto& pose = animation.nodes[index];
        const auto parentBasis = node.parent < 0 ? rootBasis : matrices[node.parent];
        const auto parentPosition = node.parent < 0 ? transform.position : positions[node.parent];
        matrices[index] = parentBasis * Basis(pose);
        positions[index] = parentPosition + parentBasis * pose.position;
        opacity[index] = pose.opacity * (node.parent < 0 ? 1.f : opacity[node.parent]);
        if (!node.visible) continue;
        const bool alternateEnemy = !player && node.image == Image::Enemy && sprite.image != Image::Enemy;
        const auto size = node.image == Image::Player || alternateEnemy ? sprite.size : node.size;
        const auto artIndex = sprite.image == Image::EnemyFast ? 0 : sprite.image == Image::EnemyArmored ? 1 :
            sprite.image == Image::EnemyRanged ? 2 : sprite.image == Image::EnemyCharger ? 3 : sprite.image == Image::EnemyBoss ? 5 : sprite.image == Image::EnemyHealer ? 6 : sprite.image == Image::EnemySummoner ? 7 : 4;
        const auto pivot = node.image == Image::Player ? CharacterPivots[character] : alternateEnemy ? ExtraEnemyArt[artIndex].pivot : node.pivot;
        const auto axisX = matrices[index][0] * size.x, axisY = matrices[index][1] * size.y;
        // A zero-scale death frame vanishes while its ECS corpse still waits
        // for the source's delayed XP credit and eventual retirement.
        if (glm::dot(axisX, axisX) <= 1e-12f || glm::dot(axisY, axisY) <= 1e-12f) continue;
        const auto center = positions[index] + matrices[index] * ((glm::vec2(.5f) - pivot) * size);
        DrawSprite draw{alternateEnemy ? sprite.image : node.image, center, {glm::length(axisX), glm::length(axisY)},
            std::atan2(axisX.y, axisX.x), node.tint};
        draw.tint.a *= opacity[index];
        draw.axisX = axisX; draw.axisY = axisY; draw.affine = true;
        ordered[count++] = {node.order, draw};
    }
    std::stable_sort(ordered.begin(), ordered.begin() + count, [](const auto& a, const auto& b) { return a.order < b.order; });
    for (std::size_t index = 0; index < count; ++index) result.push_back(ordered[index].draw);
}
}

std::vector<DrawSprite> ExtractSprites(GameWorld& world) {
    std::vector<DrawSprite> result;
    if (!world.scene) return result;
    // Copy render values while the query's rows are borrowed. Sorting only uses
    // these local snapshots, never component pointers or persistent ID lists.
    struct Group { float depth; std::size_t begin, count; };
    std::vector<DrawSprite> snapshots;
    std::vector<Group> groups;
    Query<Pickup, Transform, Sprite> pickups;
    pickups.Refresh(*world.scene);
    Query<Enemy, Transform, Sprite, ActorAnimation> enemies;
    enemies.Refresh(*world.scene);
    Query<Player, Transform, Sprite, ActorAnimation> players;
    players.Refresh(*world.scene);
    groups.reserve(pickups.Count() + enemies.Count() + players.Count());
    for (auto chunk : pickups) {
        const auto* transforms = chunk.Get<Transform>();
        const auto* sprites = chunk.Get<Sprite>();
        for (std::size_t row = 0; row < chunk.count; ++row) {
            groups.push_back({transforms[row].position.y, snapshots.size(), 1});
            snapshots.push_back(SpriteSnapshot(transforms[row], sprites[row]));
        }
    }
    for (auto chunk : enemies) {
        const auto* actors = chunk.Get<Enemy>();
        const auto* transforms = chunk.Get<Transform>();
        const auto* sprites = chunk.Get<Sprite>();
        const auto* animations = chunk.Get<ActorAnimation>();
        for (std::size_t row = 0; row < chunk.count; ++row) {
            const auto begin = snapshots.size();
            if (actors[row].phase == EnemyPhase::Spawning) {
                auto draw = SpriteSnapshot(transforms[row], sprites[row]);
                draw.image = Image::Spawn; draw.size = {1.1f, 1.1f};
                const float elapsed = world.config.spawnWarning - actors[row].remaining;
                draw.tint.a = elapsed + 1e-6f >= .2f && elapsed < .5f - 1e-6f ? 0.f : 1.f;
                snapshots.push_back(draw);
            } else {
                AppendActorSprites(snapshots, transforms[row], sprites[row], animations[row], false, world.config.character);
                if(actors[row].champion!=ChampionKind::None)
                    for(std::size_t i=begin;i<snapshots.size();++i)snapshots[i].tint*=actors[row].champion==ChampionKind::Swift?glm::vec4(.65f,1,1,1):glm::vec4(1,.75f,.4f,1);
                const auto* health = world.scene->TryGetComponent<Health>(chunk.Entity(row, *world.scene));
                if (world.config.expanded && health && health->flash > 0 && actors[row].phase == EnemyPhase::Alive)
                    for (std::size_t i = begin; i < snapshots.size(); ++i) snapshots[i].tint = {1,.6f,.6f,snapshots[i].tint.a};
            }
            groups.push_back({transforms[row].position.y, begin, snapshots.size() - begin});
        }
    }
    for (auto chunk : players) {
        const auto* transforms = chunk.Get<Transform>();
        const auto* sprites = chunk.Get<Sprite>();
        const auto* animations = chunk.Get<ActorAnimation>();
        for (std::size_t row = 0; row < chunk.count; ++row) {
            const auto begin = snapshots.size();
            AppendActorSprites(snapshots, transforms[row], sprites[row], animations[row], true, world.config.character);
            groups.push_back({transforms[row].position.y, begin, snapshots.size() - begin});
        }
    }
    // Depth belongs to the physical root; animated limbs and bobbing overlays
    // stay in the authored order within their complete actor group.
    std::stable_sort(groups.begin(), groups.end(), [](const auto& a, const auto& b) { return a.depth > b.depth; });
    result.reserve(snapshots.size());
    for (const auto& group : groups)
        result.insert(result.end(), snapshots.begin() + group.begin, snapshots.begin() + group.begin + group.count);

    if (world.config.armed) {
        Query<Weapon, Transform, Sprite> weapons;
        weapons.Refresh(*world.scene);
        for (auto chunk : weapons) {
            const auto* values = chunk.Get<Weapon>();
            const auto* transforms = chunk.Get<Transform>();
            const auto* sprites = chunk.Get<Sprite>();
            for (std::size_t row = 0; row < chunk.count; ++row) {
                const auto& weapon = values[row];
                if (!ValidWeapon(weapon.kind)) continue;
                const auto& geometry = Geometry(weapon.kind);
                const auto basis = WeaponBasis(weapon.angle, weapon.reflected);
                result.push_back(GeometrySnapshot(sprites[row].image, transforms[row].position, basis, geometry.body, sprites[row].size));
                const auto& definition = world.config.weapons.at(WeaponIndex(weapon.kind));
                const auto position = transforms[row].position;
                const int segmentCount = std::clamp(weapon.activeSegments, 0, int(LaserCenters.size()));
                for (int index = 0; index < segmentCount; ++index) {
                    const auto& segment = SourceBeamSegments[index].sprite;
                    const auto size = definition.attackSize / glm::vec2(glm::length(segment.axisX), glm::length(segment.axisY));
                    result.push_back(GeometrySnapshot(Image::LaserSegment, position, basis, segment, size));
                }
                if (weapon.flash > 0) {
                    constexpr float flashDuration = .06666667f;
                    Render::Animation::Pose flash;
                    Render::Animation::Sample(MuzzleFlash, flashDuration - weapon.flash,
                                              std::span<Render::Animation::Pose>(&flash, 1));
                    const auto parent = basis * glm::mat2(geometry.muzzleParentX, geometry.muzzleParentY);
                    auto muzzle = geometry.muzzle;
                    muzzle.offset = flash.position;
                    result.push_back(GeometrySnapshot(Image::Muzzle, position + basis * geometry.muzzleParent,
                        parent, muzzle, muzzle.size * flash.scale));
                    result.back().tint.a = flash.opacity;
                }
            }
        }
    }
    Query<Projectile, Transform, Sprite> projectiles;
    projectiles.Refresh(*world.scene);
    for (auto chunk : projectiles) {
        const auto* values = chunk.Get<Projectile>();
        const auto* transforms = chunk.Get<Transform>();
        const auto* sprites = chunk.Get<Sprite>();
        for (std::size_t row = 0; row < chunk.count; ++row) {
            if (!ValidWeapon(values[row].kind)) continue;
            result.push_back(GeometrySnapshot(sprites[row].image, transforms[row].position,
                WeaponBasis(sprites[row].angle), Geometry(values[row].kind).projectile, sprites[row].size));
        }
    }
    Query<Effect, Transform, Sprite> effects;
    Query<HostileProjectile, Transform, Sprite> hostiles; hostiles.Refresh(*world.scene);
    for (auto chunk : hostiles) for (std::size_t row = 0; row < chunk.count; ++row) {
        auto draw = SpriteSnapshot(chunk.Get<Transform>()[row],chunk.Get<Sprite>()[row]);
        draw.tint = {1,.25f,.08f,1}; result.push_back(draw);
    }
    effects.Refresh(*world.scene);
    for (auto chunk : effects) {
        const auto* values = chunk.Get<Effect>();
        const auto* transforms = chunk.Get<Transform>();
        const auto* sprites = chunk.Get<Sprite>();
        for (std::size_t row = 0; row < chunk.count; ++row) {
            if (values[row].kind != EffectKind::HitParticle) continue;
            result.push_back(SpriteSnapshot(transforms[row], sprites[row]));
            result.back().tint = {.8018868f, .19290671f, .1970778f, 1};
        }
    }
    return result;
}

std::vector<DrawDamageText> ExtractDamageText(GameWorld& world) {
    std::vector<DrawDamageText> result;
    if (!world.scene) return result;
    Query<Effect, Transform> effects;
    effects.Refresh(*world.scene);
    for (auto chunk : effects) {
        const auto* values = chunk.Get<Effect>();
        const auto* transforms = chunk.Get<Transform>();
        for (std::size_t row = 0; row < chunk.count; ++row) {
            const auto& effect = values[row];
            if (effect.kind != EffectKind::DamageText) continue;
            result.push_back({effect.value, transforms[row].position + DamageTextParentOffset + effect.pose.position,
                DamageTextFontSize * effect.pose.scale.y,
                effect.damage ? glm::vec4(1,.7f,.4f,effect.pose.opacity) : glm::vec4(1,1,1,effect.pose.opacity), effect.damage});
        }
    }
    return result;
}
std::vector<DrawEnemyStatus> ExtractEnemyStatus(GameWorld& world) {
    std::vector<DrawEnemyStatus> result;
    if (!world.scene || !world.config.expanded) return result;
    Query<Enemy, Transform, Health, Sprite> query; query.Refresh(*world.scene);
    for (auto chunk : query) for (std::size_t row = 0; row < chunk.count; ++row) {
        const auto& enemy = chunk.Get<Enemy>()[row]; const auto& health = chunk.Get<Health>()[row];
        if (enemy.phase == EnemyPhase::Alive && !world.scene->TryGetComponent<BossBrain>(chunk.Entity(row,*world.scene)) && health.maximum > 0 && enemy.kind < EnemyKind::Count) result.push_back({enemy.kind,
            chunk.Get<Transform>()[row].position + glm::vec2(0, chunk.Get<Sprite>()[row].size.y*.65f + .15f), health.current, health.maximum,
            enemy.burning, enemy.slowMultiplier, enemy.champion});
    }
    return result;
}
std::vector<DrawCombatCue> ExtractCombatCues(GameWorld& world) {
    std::vector<DrawCombatCue> result;
    if (!world.scene || !world.config.expanded) return result;
    Query<Enemy, EnemyBrain, Transform> enemies; enemies.Refresh(*world.scene);
    for (auto chunk : enemies) for (std::size_t row = 0; row < chunk.count; ++row) {
        const auto& enemy = chunk.Get<Enemy>()[row]; const auto& brain = chunk.Get<EnemyBrain>()[row];
        if(world.scene->TryGetComponent<BossBrain>(chunk.Entity(row,*world.scene)))continue;
        if (enemy.phase != EnemyPhase::Alive || !HasEnemyAttack(enemy.kind) || brain.action != EnemyAction::Windup) continue;
        const auto& behavior = EnemyBehaviors[std::size_t(enemy.kind)];
        const auto position = chunk.Get<Transform>()[row].position;
        const float length = behavior.chargeSeconds > 0 ? behavior.chargeSpeed*behavior.chargeSeconds*EnemySpeed(world)*enemy.growthSpeed : behavior.range;
        result.push_back({enemy.kind==EnemyKind::Healer?CombatCueKind::Heal:enemy.kind==EnemyKind::Summoner?CombatCueKind::Summon:behavior.chargeSeconds > 0 ? CombatCueKind::Charge : CombatCueKind::Aim,
            position,position+brain.direction*length,enemy.kind==EnemyKind::Healer?5.f:enemy.kind==EnemyKind::Summoner?1.5f:enemy.kind == EnemyKind::Elite ? .70f : .43f,
            std::clamp(1-brain.remaining/behavior.windup,0.f,1.f)});
    }
    Query<BossBrain,Enemy,Transform> bosses;bosses.Refresh(*world.scene);
    for(auto chunk:bosses)for(std::size_t row=0;row<chunk.count;++row){
        const auto& boss=chunk.Get<BossBrain>()[row];if(chunk.Get<Enemy>()[row].phase!=EnemyPhase::Alive || boss.action!=BossAction::Windup)continue;
        const auto position=chunk.Get<Transform>()[row].position;
        result.push_back({boss.attack==BossAttack::Charge?CombatCueKind::Charge:boss.attack==BossAttack::Ring?CombatCueKind::BossRing:CombatCueKind::BossFan,
            position,position+boss.direction*(boss.attack==BossAttack::Charge?(boss.enraged?12.f:9.f)*.55f*EnemySpeed(world)*chunk.Get<Enemy>()[row].growthSpeed:6.f),boss.attack==BossAttack::Ring?2.f:.82f,boss.enraged?.72f:.48f});
    }
    Query<Effect, Transform> effects; effects.Refresh(*world.scene);
    for (auto chunk : effects) for (std::size_t row = 0; row < chunk.count; ++row) {
        const auto& effect = chunk.Get<Effect>()[row];
        if (effect.kind == EffectKind::BlastRing) result.push_back({CombatCueKind::Blast,chunk.Get<Transform>()[row].position,{},
            effect.radius,std::clamp(float(effect.age/effect.lifetime),0.f,1.f)});
    }
    return result;
}
} // namespace Brotato
