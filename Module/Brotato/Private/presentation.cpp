#include "Brotato/Public/game_module.h"
#include "Brotato/Public/animation_catalog.h"
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
std::uint32_t Hash(std::uint32_t value) {
    value ^= value >> 16; value *= 0x7feb352du;
    value ^= value >> 15; value *= 0x846ca68bu;
    return value ^ (value >> 16);
}
float RandomUnit(std::uint32_t& state) {
    state = Hash(state + 0x9e3779b9u);
    return float(state >> 8) / 16777216.f;
}
glm::mat2 Basis(const Render::Animation::Pose& pose) {
    const float cosine = std::cos(pose.angle), sine = std::sin(pose.angle);
    return {{cosine * pose.scale.x, sine * pose.scale.x}, {-sine * pose.scale.y, cosine * pose.scale.y}};
}
}

void GameModule::ResetActorAnimation(ECS::EntityHandle entity, ActorClip clip) {
    auto& animation = Get<ActorAnimation>(entity);
    animation = ActorAnimation{}; animation.clip = clip;
    SampleActorAnimation(entity);
}

void GameModule::SampleActorAnimation(ECS::EntityHandle entity) {
    auto& animation = Get<ActorAnimation>(entity);
    const bool player = animation.clip == ActorClip::PlayerIdle || animation.clip == ActorClip::PlayerMove;
    const std::span<const AnimatedNode> nodes = player ? std::span<const AnimatedNode>(PlayerNodes) : std::span<const AnimatedNode>(EnemyNodes);
    static_assert(PlayerNodes.size() <= ActorAnimation::MaxNodes && EnemyNodes.size() <= ActorAnimation::MaxNodes);
    for (std::size_t index = 0; index < nodes.size(); ++index) animation.nodes[index] = nodes[index].base;
    Render::Animation::Sample(Clip(animation.clip), animation.time,
        std::span<Render::Animation::Pose>(animation.nodes.data(), nodes.size()));
}

void GameModule::Animate() {
    if (state_ != State::Playing) return;
    const auto& transform = Get<Transform>(player_);
    const auto delta = transform.position - transform.previous;
    const auto playerClip = glm::dot(delta, delta) > 1e-12f ? ActorClip::PlayerMove : ActorClip::PlayerIdle;
    if (Get<ActorAnimation>(player_).clip != playerClip) ResetActorAnimation(player_, playerClip);
    Get<ActorAnimation>(player_).time += FixedStep;
    SampleActorAnimation(player_);
    for (auto entity : enemies_) {
        const auto phase = Get<Enemy>(entity).phase;
        if (phase == EnemyPhase::Spawning) continue;
        const auto clip = phase == EnemyPhase::Dying ? ActorClip::EnemyDeath : ActorClip::EnemyMove;
        if (Get<ActorAnimation>(entity).clip != clip) ResetActorAnimation(entity, clip);
        Get<ActorAnimation>(entity).time += FixedStep;
        SampleActorAnimation(entity);
    }
    for (auto entity : effects_) {
        auto& effect = Get<Effect>(entity);
        if (effect.kind == EffectKind::DamageText) {
            // WaveTick schedules corpse retirement before this system runs.
            // Follow that same boundary instead of approximating it with an
            // independent float clock; generation protects a reused ECS slot.
            const auto* owner = scene_->IsAlive(effect.owner) ?
                scene_->GetActiveComponent<Enemy>(effect.owner.GetID()).Get() : nullptr;
            if (!owner || owner->phase != EnemyPhase::Dying || owner->remaining <= 1e-5f) {
                retired_.push_back(entity); continue;
            }
        }
        const double activeSeconds = std::min(FixedStep, std::max(0.0, double(effect.lifetime) - effect.age));
        const float activeDt = float(activeSeconds);
        effect.age += activeSeconds;
        auto& transform = Get<Transform>(entity);
        transform.previous = transform.position;
        if (effect.kind == EffectKind::HitParticle) {
            transform.position += Get<Velocity>(entity).value * activeDt;
            const float progress = float(std::clamp(effect.age / effect.lifetime, 0.0, 1.0));
            Get<Sprite>(entity).size = glm::vec2(effect.initialSize * (1 - progress * progress));
        } else {
            effect.pose = DamageTextBase;
            Render::Animation::Sample(DamageText, effect.age, std::span<Render::Animation::Pose>(&effect.pose, 1));
        }
        if (effect.kind == EffectKind::HitParticle && effect.age + 1e-9 >= effect.lifetime) retired_.push_back(entity);
    }
}

void GameModule::AppendActorSprites(std::vector<DrawSprite>& result, ECS::EntityHandle entity, bool player) {
    const auto& animation = Get<ActorAnimation>(entity);
    const auto& sprite = Get<Sprite>(entity);
    const auto origin = Get<Transform>(entity).position;
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
        const auto parentPosition = node.parent < 0 ? origin : positions[node.parent];
        matrices[index] = parentBasis * Basis(pose);
        positions[index] = parentPosition + parentBasis * pose.position;
        opacity[index] = pose.opacity * (node.parent < 0 ? 1.f : opacity[node.parent]);
        if (!node.visible) continue;
        const auto size = node.image == Image::Player ? sprite.size : node.size;
        const auto pivot = node.image == Image::Player ? CharacterPivots[config_.character] : node.pivot;
        const auto axisX = matrices[index][0] * size.x, axisY = matrices[index][1] * size.y;
        // The non-looping death clip reaches zero scale at .5 seconds but the
        // ECS corpse stays alive until the source's delayed XP credit at 1.5.
        if (glm::dot(axisX, axisX) <= 1e-12f || glm::dot(axisY, axisY) <= 1e-12f) continue;
        const auto center = positions[index] + matrices[index] * ((glm::vec2(.5f) - pivot) * size);
        DrawSprite draw{node.image, center, {glm::length(axisX), glm::length(axisY)},
            std::atan2(axisX.y, axisX.x), node.tint};
        draw.tint.a *= opacity[index];
        draw.axisX = axisX; draw.axisY = axisY; draw.affine = true;
        ordered[count++] = {node.order, draw};
    }
    std::stable_sort(ordered.begin(), ordered.begin() + count, [](const auto& a, const auto& b) { return a.order < b.order; });
    for (std::size_t index = 0; index < count; ++index) result.push_back(ordered[index].draw);
}

void GameModule::QueueHitEffects(ECS::EntityHandle entity) {
    const auto position = Get<Transform>(entity).position;
    // Visual randomness is derived from captured values, never the combat RNG.
    std::uint32_t random = Hash(config_.seed ^ std::uint32_t(stats_.ticks) ^
                                (std::uint32_t(entity.GetID()) * 0x9e3779b9u));
    auto room = [&] { return effects_.size() + pendingEffects_.size() < config_.maxEffects; };
    if (config_.deathDelay > 0 && room())
        pendingEffects_.push_back({EffectKind::DamageText, position, {}, DamageTextFontSize, 1 + int(RandomUnit(random) * 8), entity});
    for (int index = 0; index < 6 && room(); ++index) {
        const float angle = (RandomUnit(random) * 2 - 1) * (7 * std::numbers::pi_v<float> / 180);
        const float speed = 1 + RandomUnit(random) * 7;
        const float radius = std::sqrt(RandomUnit(random)) * .1f;
        const float offsetAngle = RandomUnit(random) * (2 * std::numbers::pi_v<float>);
        const glm::vec2 offset{std::cos(offsetAngle) * radius, std::sin(offsetAngle) * radius};
        pendingEffects_.push_back({EffectKind::HitParticle, position + offset,
            {std::cos(angle) * speed, std::sin(angle) * speed}, .1f + RandomUnit(random) * .18f, 1});
    }
}

void GameModule::CommitEffects() {
    for (const auto& command : pendingEffects_) {
        if (effects_.size() >= config_.maxEffects) break;
        const auto entity = scene_->CreateEntity(effectType_); effects_.push_back(entity);
        auto& transform = Get<Transform>(entity); transform.position = transform.previous = command.position;
        Get<Velocity>(entity).value = command.velocity;
        auto& sprite = Get<Sprite>(entity); sprite.image = Image::HitParticle; sprite.size = glm::vec2(command.size);
        auto& effect = Get<Effect>(entity); effect.kind = command.kind; effect.initialSize = command.size; effect.value = command.value;
        effect.owner = command.owner;
        effect.lifetime = command.kind == EffectKind::HitParticle ? .5f : config_.deathDelay;
        if (effect.kind == EffectKind::DamageText) {
            effect.pose = DamageTextBase;
            Render::Animation::Sample(DamageText, 0, std::span<Render::Animation::Pose>(&effect.pose, 1));
        }
    }
    pendingEffects_.clear();
}

std::vector<DrawDamageText> GameModule::ExtractDamageText() {
    std::vector<DrawDamageText> result;
    if (!started_) return result;
    for (auto entity : effects_) {
        const auto& effect = Get<Effect>(entity);
        if (effect.kind != EffectKind::DamageText) continue;
        result.push_back({effect.value, Get<Transform>(entity).position + DamageTextParentOffset + effect.pose.position,
                          DamageTextFontSize * effect.pose.scale.y, {1, 1, 1, effect.pose.opacity}});
    }
    return result;
}

void GameModule::EmitEvent(GameEventKind kind, ECS::EntityHandle entity, glm::vec2 position, WeaponKind weapon) {
    if (events_.size() == MaxPendingEvents) { ++droppedEvents_; return; }
    events_.push_back({kind, stats_.ticks, entity, position, weapon});
}

std::vector<GameEvent> GameModule::DrainEvents() {
    std::vector<GameEvent> result(events_.begin(), events_.end());
    events_.clear();
    return result;
}

void GameModule::ResetPresentation() {
    ++presentationEpoch_;
    events_.clear(); droppedEvents_ = 0;
}
} // namespace Brotato
