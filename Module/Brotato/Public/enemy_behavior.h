#pragma once
#include "game_components.h"

namespace Brotato {
struct EnemyBehavior {
    float range, windup, chargeSeconds, chargeSpeed, recovery, cooldown;
    int projectiles;
};
inline constexpr std::array<EnemyBehavior, EnemyCount> EnemyBehaviors{{
    {}, {}, {},
    {8,.45f,0,0,.3f,1.7f,1},
    {7,.65f,.55f,10,.6f,1.8f,0},
    {8,.7f,.45f,8,.8f,2.8f,3},
    {30,.75f,0,0,.4f,2.5f,0},
    {30,1.f,0,0,.6f,4.5f,0},
}};
inline bool HasEnemyAttack(EnemyKind kind) {
    return kind >= EnemyKind::Ranged && kind < EnemyKind::Count;
}
} // namespace Brotato
