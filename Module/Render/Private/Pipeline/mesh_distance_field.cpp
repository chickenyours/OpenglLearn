#include "Render/Public/Pipeline/mesh_distance_field.h"
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
namespace Render {
namespace {
float TriangleDistance(glm::vec3 p, glm::vec3 a, glm::vec3 b, glm::vec3 c) {
    auto ab = b - a, ac = c - a, ap = p - a;
    float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    if (d1 <= 0 && d2 <= 0)
        return glm::length(ap);
    auto bp = p - b;
    float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
    if (d3 >= 0 && d4 <= d3)
        return glm::length(bp);
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0)
        return glm::length(p - (a + ab * (d1 / (d1 - d3))));
    auto cp = p - c;
    float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
    if (d6 >= 0 && d5 <= d6)
        return glm::length(cp);
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0)
        return glm::length(p - (a + ac * (d2 / (d2 - d6))));
    float va = d3 * d6 - d5 * d4;
    if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0)
        return glm::length(p - (b + (c - b) * ((d4 - d3) / (d4 - d3 + d5 - d6))));
    auto n = glm::normalize(glm::cross(ab, ac));
    return std::abs(glm::dot(ap, n));
}
float BoxDistance(glm::vec3 p, glm::vec3 lo, glm::vec3 hi) {
    return glm::length(glm::max(glm::max(lo - p, p - hi), glm::vec3(0)));
}
bool BoxRay(glm::vec3 p, glm::vec3 d, glm::vec3 lo, glm::vec3 hi) {
    float near = 0, far = 1e9f;
    for (int a = 0; a < 3; ++a) {
        float x = (lo[a] - p[a]) / d[a], y = (hi[a] - p[a]) / d[a];
        near = std::max(near, std::min(x, y));
        far = std::min(far, std::max(x, y));
    }
    return near <= far;
}
} // namespace
std::shared_ptr<const MeshDistanceField> BuildMeshDistanceField(const RealtimeGiScene &scene,
                                                                uint32_t resolution, bool closed) {
    if (resolution < 8 || resolution > 128)
        throw std::invalid_argument("Distance field resolution must be 8..128");
    auto field = std::make_shared<MeshDistanceField>();
    if (!scene.NodeCount()) {
        field->pixels_.resize(1024, glm::vec4(0));
        return field;
    }
    // Coincident copies do not change distance. Remove them before querying;
    // this bounds extraction cost for instanced/duplicated legacy geometry.
    std::map<std::array<float, 9>, ProbeTriangle> unique;
    const auto originalOffset = scene.NodeCount() * 2;
    for (uint32_t t = 0; t < scene.TriangleCount(); ++t) {
        auto a = glm::vec3(scene.Pixels()[originalOffset + t * 5]),
             b = glm::vec3(scene.Pixels()[originalOffset + t * 5 + 1]),
             c = glm::vec3(scene.Pixels()[originalOffset + t * 5 + 2]);
        unique.try_emplace({a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z}, ProbeTriangle{a, b, c});
    }
    std::shared_ptr<const RealtimeGiScene> compact;
    if (unique.size() < scene.TriangleCount()) {
        std::vector<ProbeTriangle> triangles;
        triangles.reserve(unique.size());
        for (auto &[key, t] : unique)
            triangles.push_back(t);
        compact = BuildRealtimeGiScene(triangles);
    }
    const auto &geometry = compact ? *compact : scene;
    const auto &pixels = geometry.Pixels();
    auto lo = glm::vec3(pixels[0]), hi = glm::vec3(pixels[1]);
    auto extent = hi - lo;
    float cell = std::max(glm::max(extent.x, glm::max(extent.y, extent.z)) / float(resolution - 3), .001f);
    field->origin_ = lo - glm::vec3(cell);
    field->spacing_ = glm::vec3(cell);
    field->counts_ = glm::uvec3(glm::ceil(extent / cell)) + glm::uvec3(3);
    uint32_t count = field->counts_.x * field->counts_.y * field->counts_.z;
    field->pixels_.resize(((count + 1023) / 1024) * 1024, glm::vec4(0));
    auto triangleOffset = geometry.NodeCount() * 2;
    const glm::vec3 direction = glm::normalize(glm::vec3(.719f, .331f, .607f));
    for (uint32_t z = 0; z < field->counts_.z; ++z)
        for (uint32_t y = 0; y < field->counts_.y; ++y)
            for (uint32_t x = 0; x < field->counts_.x; ++x) {
                auto p = field->origin_ + glm::vec3(x, y, z) * cell;
                float closest = std::numeric_limits<float>::max();
                int nearest = -1, hits = 0;
                std::array<int, 32> stack{};
                int size = 1;
                while (size) {
                    int node = stack[--size];
                    auto nlo = pixels[node * 2], nhi = pixels[node * 2 + 1];
                    bool distance = BoxDistance(p, glm::vec3(nlo), glm::vec3(nhi)) < closest;
                    bool parity = closed && BoxRay(p, direction, glm::vec3(nlo), glm::vec3(nhi));
                    if (!distance && !parity)
                        continue;
                    if (nhi.w >= 0) {
                        int a = int(nlo.w), b = int(nhi.w);
                        float da = BoxDistance(p, glm::vec3(pixels[a * 2]), glm::vec3(pixels[a * 2 + 1])),
                              db = BoxDistance(p, glm::vec3(pixels[b * 2]), glm::vec3(pixels[b * 2 + 1]));
                        if (da < db) {
                            stack[size++] = b;
                            stack[size++] = a;
                        } else {
                            stack[size++] = a;
                            stack[size++] = b;
                        }
                        continue;
                    }
                    for (int t = int(nlo.w); t < int(nlo.w - nhi.w); ++t) {
                        auto a = glm::vec3(pixels[triangleOffset + t * 5]),
                             b = glm::vec3(pixels[triangleOffset + t * 5 + 1]),
                             c = glm::vec3(pixels[triangleOffset + t * 5 + 2]);
                        if (distance) {
                            float value = TriangleDistance(p, a, b, c);
                            if (value < closest) {
                                closest = value;
                                nearest = t;
                            }
                        }
                        if (parity) {
                            auto ab = b - a, ac = c - a, q = glm::cross(direction, ac);
                            float det = glm::dot(ab, q);
                            if (std::abs(det) < 1e-9f)
                                continue;
                            auto rel = p - a;
                            float u = glm::dot(rel, q) / det;
                            if (u < 0 || u > 1)
                                continue;
                            auto r = glm::cross(rel, ab);
                            float v = glm::dot(direction, r) / det;
                            if (v < 0 || u + v > 1)
                                continue;
                            if (glm::dot(ac, r) / det > 1e-5f)
                                ++hits;
                        }
                    }
                }
                field->pixels_[(z * field->counts_.y + y) * field->counts_.x + x] = {
                    closed && (hits & 1) ? -closest : closest, float(nearest), 0, 0};
            }
    return field;
}
float MeshDistanceField::Sample(glm::uvec3 p) const {
    p = glm::min(p, counts_ - glm::uvec3(1));
    return pixels_[(p.z * counts_.y + p.y) * counts_.x + p.x].x;
}
float MeshDistanceField::ConservativeDistance(glm::vec3 p) const {
    auto at = (p - origin_) / spacing_;
    if (glm::any(glm::lessThan(at, glm::vec3(0))) ||
        glm::any(glm::greaterThan(at, glm::vec3(counts_ - glm::uvec3(1)))))
        return 0;
    auto cell = glm::uvec3(glm::round(at));
    auto center = origin_ + glm::vec3(cell) * spacing_;
    return std::max(0.f, std::abs(Sample(cell)) - glm::length(p - center) - 1e-5f);
}
} // namespace Render
