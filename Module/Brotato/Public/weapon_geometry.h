#pragma once
#include "weapon_definitions.h"
#include <cmath>

namespace Brotato {
inline const WeaponGeometry& Geometry(WeaponKind kind) { return SourceWeaponGeometry.at(WeaponIndex(kind)); }
inline glm::mat2 WeaponBasis(float angle, bool reflected = false) {
    const float cosine = std::cos(angle), sine = std::sin(angle);
    const float y = reflected ? -1.f : 1.f;
    return {{cosine, sine}, {-sine * y, cosine * y}};
}
inline glm::vec2 WeaponRoot(glm::vec2 owner, WeaponKind kind) { return owner + Geometry(kind).rootOffset; }
inline glm::vec2 WeaponPoint(glm::vec2 root, float angle, bool reflected, glm::vec2 point) {
    return root + WeaponBasis(angle, reflected) * point;
}
} // namespace Brotato
