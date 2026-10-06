#include "Brotato/Public/game_world.h"
#include "Brotato/Public/animation_catalog.h"
#include "Brotato/Systems/game_systems.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>

namespace Brotato {
namespace {
const Render::Animation::Clip& Clip(ActorClip clip) {
    switch (clip) {
    case ActorClip::PlayerIdle: return PlayerIdle;
    case ActorClip::PlayerMove: return PlayerMove;
    case ActorClip::EnemyMove: return EnemyMove;
    case ActorClip::EnemyDeath: return EnemyDeath;
    }
    return PlayerIdle;
}
void Sample(ActorAnimation& animation) {
    const bool player = animation.clip == ActorClip::PlayerIdle || animation.clip == ActorClip::PlayerMove;
    const std::span<const AnimatedNode> nodes = player ? std::span<const AnimatedNode>(PlayerNodes) : std::span<const AnimatedNode>(EnemyNodes);
    static_assert(PlayerNodes.size() <= ActorAnimation::MaxNodes && EnemyNodes.size() <= ActorAnimation::MaxNodes);
    for (std::size_t index = 0; index < nodes.size(); ++index) animation.nodes[index] = nodes[index].base;
    Render::Animation::Sample(Clip(animation.clip), animation.time,
        std::span<Render::Animation::Pose>(animation.nodes.data(), nodes.size()));
}
void AdvanceAnimation(ActorAnimation& animation, ActorClip clip) {
    if (animation.clip != clip) {
        animation = ActorAnimation{};
        animation.clip = clip;
    }
    animation.time += SimulationStep;
    Sample(animation);
}
std::uint32_t Hash(std::uint32_t value) {
    value ^= value >> 16; value *= 0x7feb352du;
    value ^= value >> 15; value *= 0x846ca68bu;
    return value ^ (value >> 16);
}
float RandomUnit(std::uint32_t& state) {
    state = Hash(state + 0x9e3779b9u);
    return float(state >> 8) / 16777216.f;
}
}

void ResetActorAnimation(GameWorld& world, ECS::EntityHandle entity, ActorClip clip) {
    auto& animation = world.Get<ActorAnimation>(entity);
    animation = ActorAnimation{};
    animation.clip = clip;
    Sample(animation);
}

void SampleActorAnimation(GameWorld& world, ECS::EntityHandle entity) {
    Sample(world.Get<ActorAnimation>(entity));
}

void PresentationSystem::OnTick() {
    auto& world = *GetContext()->GetService<GameWorld>();
    if (world.state != State::Playing) return;

    // Each query spans every matching archetype. No game-owned entity registry
    // determines which actors or effects participate in this system.
    Query<Player, Transform, ActorAnimation> players;
    players.Refresh(*world.scene);
    for (auto chunk : players) {
        const auto* transforms = chunk.Get<Transform>();
        auto* animations = chunk.Get<ActorAnimation>();
        for (std::size_t row = 0; row < chunk.count; ++row) {
            const auto delta = transforms[row].position - transforms[row].previous;
            AdvanceAnimation(animations[row], glm::dot(delta, delta) > 1e-12f ? ActorClip::PlayerMove : ActorClip::PlayerIdle);
        }
    }
    Query<Enemy, ActorAnimation> enemies;
    enemies.Refresh(*world.scene);
    for (auto chunk : enemies) {
        const auto* actors = chunk.Get<Enemy>();
        auto* animations = chunk.Get<ActorAnimation>();
        for (std::size_t row = 0; row < chunk.count; ++row) {
            if (actors[row].phase == EnemyPhase::Spawning) continue;
            AdvanceAnimation(animations[row], actors[row].phase == EnemyPhase::Dying ? ActorClip::EnemyDeath : ActorClip::EnemyMove);
        }
    }
    Query<Effect, Transform, Velocity, Sprite> effects;
    effects.Refresh(*world.scene);
    for (auto chunk : effects) {
        auto* values = chunk.Get<Effect>();
        auto* transforms = chunk.Get<Transform>();
        const auto* velocities = chunk.Get<Velocity>();
        auto* sprites = chunk.Get<Sprite>();
        for (std::size_t row = 0; row < chunk.count; ++row) {
            auto& effect = values[row];
            if (effect.kind == EffectKind::DamageText && effect.owner.GetID() != 0) {
                // A relationship lookup is generation checked; the effect set
                // itself comes from the query. Wave retirement occurs earlier
                // this tick and its command is not played back until the end.
                const auto* owner = world.scene->IsAlive(effect.owner) ?
                    world.scene->GetActiveComponent<Enemy>(effect.owner.GetID()).Get() : nullptr;
                if (!owner || owner->phase != EnemyPhase::Dying || owner->remaining <= 1e-5f) {
                    world.commands->Destroy(chunk.Entity(row, *world.scene));
                    continue;
                }
            }
            const double activeSeconds = std::min(SimulationStep, std::max(0.0, double(effect.lifetime) - effect.age));
            effect.age += activeSeconds;
            if (effect.kind == EffectKind::BlastRing) {
                if (effect.age + 1e-9 >= effect.lifetime) world.commands->Destroy(chunk.Entity(row,*world.scene));
                continue;
            }
            auto& transform = transforms[row];
            transform.previous = transform.position;
            if (effect.kind == EffectKind::HitParticle) {
                transform.position += velocities[row].value * float(activeSeconds);
                const float progress = float(std::clamp(effect.age / effect.lifetime, 0.0, 1.0));
                sprites[row].size = glm::vec2(effect.initialSize * (1 - progress * progress));
                if (effect.age + 1e-9 >= effect.lifetime)
                    world.commands->Destroy(chunk.Entity(row, *world.scene));
            } else {
                if (effect.owner.GetID() == 0 && effect.age + 1e-9 >= effect.lifetime) {
                    world.commands->Destroy(chunk.Entity(row, *world.scene)); continue;
                }
                effect.pose = DamageTextBase;
                Render::Animation::Sample(DamageText, effect.age, std::span<Render::Animation::Pose>(&effect.pose, 1));
            }
        }
    }
}

void QueueHitEffects(GameWorld& world, ECS::EntityHandle entity, int damage) {
    const auto position = world.Get<Transform>(entity).position;
    // Visual randomness is derived from captured values, never the combat RNG.
    std::uint32_t random = Hash(world.config.seed ^ std::uint32_t(world.stats.ticks) ^
                                (std::uint32_t(entity.GetID()) * 0x9e3779b9u));
    const auto existing = Count<Effect>(world);
    auto room = [&] { return existing + world.reservedEffects < world.config.maxEffects; };
    auto queue = [&](EffectKind kind, glm::vec2 origin, glm::vec2 velocity, float size, int value,
                     ECS::EntityHandle owner = ECS::EntityHandle(0)) {
        const float lifetime = kind == EffectKind::HitParticle ? .5f : world.config.expanded ? .45f : world.config.deathDelay;
        const bool actualDamage = world.config.expanded;
        // Copy all initialization values. The playback boundary may compact any
        // queried chunk; callbacks must never retain component references.
        world.effectCommands->Create(world.effectType,
            [kind, origin, velocity, size, value, owner, lifetime, actualDamage](ECS::Core::Scene& scene, ECS::EntityHandle created) {
                auto& transform = *scene.GetActiveComponent<Transform>(created.GetID()).Get();
                transform.position = transform.previous = origin;
                scene.GetActiveComponent<Velocity>(created.GetID()).Get()->value = velocity;
                auto& sprite = *scene.GetActiveComponent<Sprite>(created.GetID()).Get();
                sprite.image = Image::HitParticle;
                sprite.size = glm::vec2(size);
                auto& effect = *scene.GetActiveComponent<Effect>(created.GetID()).Get();
                effect.kind = kind; effect.initialSize = size; effect.value = value;
                effect.owner = owner; effect.lifetime = lifetime;
                effect.damage = actualDamage;
                if (kind == EffectKind::DamageText) {
                    effect.pose = DamageTextBase;
                    Render::Animation::Sample(DamageText, 0, std::span<Render::Animation::Pose>(&effect.pose, 1));
                }
            });
        ++world.reservedEffects;
    };
    if ((world.config.expanded || world.config.deathDelay > 0) && room())
        queue(EffectKind::DamageText, position, {}, DamageTextFontSize,
            world.config.expanded ? std::max(1, damage) : 1 + int(RandomUnit(random) * 8),
            world.config.expanded ? ECS::EntityHandle(0) : entity);
    for (int index = 0; index < 6 && room(); ++index) {
        const float angle = (RandomUnit(random) * 2 - 1) * (7 * std::numbers::pi_v<float> / 180);
        const float speed = 1 + RandomUnit(random) * 7;
        const float radius = std::sqrt(RandomUnit(random)) * .1f;
        const float offsetAngle = RandomUnit(random) * (2 * std::numbers::pi_v<float>);
        const glm::vec2 offset{std::cos(offsetAngle) * radius, std::sin(offsetAngle) * radius};
        queue(EffectKind::HitParticle, position + offset, {std::cos(angle) * speed, std::sin(angle) * speed},
              .1f + RandomUnit(random) * .18f, 1);
    }
}

void QueueBlastEffect(GameWorld& world, glm::vec2 position, float radius) {
    if (!world.effectCommands || Count<Effect>(world) + world.reservedEffects >= world.config.maxEffects) return;
    ++world.reservedEffects;
    world.effectCommands->Create(world.effectType,[position,radius](auto& scene,auto entity) {
        auto& transform = *scene.template TryGetComponent<Transform>(entity); transform.position = transform.previous = position;
        auto& effect = *scene.template TryGetComponent<Effect>(entity);
        effect.kind = EffectKind::BlastRing; effect.radius = radius; effect.lifetime = .22f;
    });
}
void EmitEvent(GameWorld& world, GameEventKind kind, ECS::EntityHandle entity, glm::vec2 position, WeaponKind weapon) {
    if (world.events.size() == MaxPendingGameEvents) { ++world.droppedEvents; return; }
    world.events.push_back({kind, world.stats.ticks, entity, position, weapon});
}

void ResetPresentation(GameWorld& world) {
    ++world.presentationEpoch;
    world.events.clear();
    world.droppedEvents = 0;
}
} // namespace Brotato
