#pragma once

#include "engine/ECS/Entity/entity.h"
#include "weapon_definitions.h"
#include <cstdint>
#include <glm/glm.hpp>

namespace Brotato {
enum class GameEventKind { WeaponAttack, EnemyKilled, MaterialCollected, PlayerHurt };

// Value-only presentation messages. The handle identifies the subject, but may
// already be retired when a consumer drains the event. Never dereference it
// without the producing scene's generation check; position is the captured value.
struct GameEvent {
    GameEventKind kind = GameEventKind::WeaponAttack;
    std::uint64_t tick = 0;
    ECS::EntityHandle entity{0};
    glm::vec2 position{};
    WeaponKind weapon = WeaponKind::Wand;
};
} // namespace Brotato
