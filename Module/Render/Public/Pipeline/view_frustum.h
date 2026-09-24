#pragma once
#include <array>
#include <glm/glm.hpp>

namespace Render {
// OpenGL clip volume (-w <= x,y,z <= w). Planes need no normalization for
// conservative AABB rejection; touching/intersecting boxes remain visible.
class ViewFrustum {
public:
    explicit ViewFrustum(const glm::mat4& matrix) {
        const auto rows = glm::transpose(matrix);
        for(int axis = 0; axis < 3; ++axis) {
            planes_[axis * 2] = rows[3] + rows[axis];
            planes_[axis * 2 + 1] = rows[3] - rows[axis];
        }
    }
    bool Intersects(const glm::vec3& minimum, const glm::vec3& maximum) const {
        for(const auto& plane : planes_) {
            const glm::vec3 positive{
                plane.x >= 0 ? maximum.x : minimum.x,
                plane.y >= 0 ? maximum.y : minimum.y,
                plane.z >= 0 ? maximum.z : minimum.z};
            if(glm::dot(glm::vec3(plane), positive) + plane.w < 0) return false;
        }
        return true;
    }
private:
    std::array<glm::vec4, 6> planes_;
};
}
