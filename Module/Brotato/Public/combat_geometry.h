#pragma once
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace Brotato::Combat {
inline glm::vec2 Rotate(glm::vec2 point, float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    return {c * point.x - s * point.y, s * point.x + c * point.y};
}
inline float CircleImpact(glm::vec2 p, glm::vec2 v, float radius) {
    const float c = glm::dot(p, p) - radius * radius;
    if (c <= 0) return 0;
    const float a = glm::dot(v, v), b = glm::dot(p, v), d = b*b - a*c;
    if (a < 1e-12f || d < 0) return 2;
    const float t = (-b - std::sqrt(d)) / a;
    return t >= 0 && t <= 1 ? t : 2;
}
// Earliest relative-motion circle vs fixed-orientation capsule impact, [0,1].
// Capsule = center strip plus two round caps. This does not inflate a long
// projectile into a large circle, so it cannot hit far to either side.
inline float CapsuleImpact(glm::vec2 start, glm::vec2 end, float angle, float halfLength,
                           float radius, glm::vec2 otherStart, glm::vec2 otherEnd, float otherRadius) {
    const auto p = Rotate(otherStart - start, -angle);
    const auto v = Rotate((otherEnd - otherStart) - (end - start), -angle);
    const float r = radius + otherRadius;
    float first = std::min(CircleImpact(p - glm::vec2(halfLength, 0), v, r),
                           CircleImpact(p + glm::vec2(halfLength, 0), v, r));
    float enter = 0, leave = 1;
    for (int axis = 0; axis < 2; ++axis) {
        const float extent = axis == 0 ? halfLength : r;
        if (std::abs(v[axis]) < 1e-8f) {
            if (std::abs(p[axis]) > extent) return first;
        } else {
            float a = (-extent - p[axis]) / v[axis], b = (extent - p[axis]) / v[axis];
            if (a > b) std::swap(a, b);
            enter = std::max(enter, a); leave = std::min(leave, b);
            if (enter > leave) return first;
        }
    }
    return enter <= leave ? std::min(first, enter) : first;
}
} // namespace Brotato::Combat
