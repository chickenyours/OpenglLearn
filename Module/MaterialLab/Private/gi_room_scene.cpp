#include "MaterialLab/Public/gi_room_scene.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

namespace MaterialLab {
namespace {
bool Finite(glm::vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
glm::mat4 FixedRotation(float angle, glm::vec3 axis) {
    auto result = glm::rotate(glm::mat4(1), angle, axis);
    // Architecture uses right angles. Exact zeros and unit coefficients keep
    // independently transformed wall vertices identical at every shell seam.
    for (int column = 0; column < 3; ++column) for (int row = 0; row < 3; ++row) {
        auto& value = result[column][row];
        if (std::abs(value) < 1e-6f) value = 0;
        else if (std::abs(std::abs(value) - 1) < 1e-6f) value = std::copysign(1.f, value);
    }
    return result;
}
glm::vec3 PropExtent(const GiRoomObject& object, float yaw) {
    const float c = std::abs(std::cos(yaw)), s = std::abs(std::sin(yaw));
    return {c * object.scale.x + s * object.scale.z, object.scale.y,
            s * object.scale.x + c * object.scale.z};
}
bool Contained(glm::vec3 center, glm::vec3 extent, float margin = 0) {
    const auto lo = center - extent, hi = center + extent;
    for (int axis = 0; axis < 3; ++axis)
        if (lo[axis] < GiRoomMinimum[axis] + margin - 1e-5f ||
            hi[axis] > GiRoomMaximum[axis] - margin + 1e-5f) return false;
    return true;
}
} // namespace

const std::vector<GiRoomObject>& GiRoomObjects() {
    static const auto objects = [] {
        std::vector<GiRoomObject> result;
        using M = GiRoomMaterial;
        const auto wall = [&](const char* name, M material, glm::vec3 position,
                              glm::vec3 scale, float tilt, glm::vec3 axis) {
            result.push_back({name, ShowcaseMesh::Plane, material, position,
                              scale, 0, -1, true, axis, tilt});
        };
        wall("Rough floor", M::RoughFloor, {0, 0, 0}, {4, 1, 4}, 0, {1, 0, 0});
        wall("White ceiling", M::WhiteWall, {0, 5, 0}, {4, 1, 4}, glm::radians(180.f), {1, 0, 0});
        wall("Red left wall", M::RedWall, {-4, 2.5f, 0}, {2.5f, 1, 4}, glm::radians(-90.f), {0, 0, 1});
        wall("Green right wall", M::GreenWall, {4, 2.5f, 0}, {2.5f, 1, 4}, glm::radians(90.f), {0, 0, 1});
        wall("White back wall", M::WhiteWall, {0, 2.5f, -4}, {4, 1, 2.5f}, glm::radians(90.f), {1, 0, 0});
        wall("White front wall", M::WhiteWall, {0, 2.5f, 4}, {4, 1, 2.5f}, glm::radians(-90.f), {1, 0, 0});
        result.push_back({"Low white block", ShowcaseMesh::Box, M::Occluder,
                          {-1.6f, .65f, -.4f}, {.75f, .65f, .65f}, -.2f, 0});
        result.push_back({"Tall white block", ShowcaseMesh::Box, M::Occluder,
                          {1.45f, 1, -2}, {.65f, 1, .7f}, .25f, 1});
        result.push_back({"Movable light blocker", ShowcaseMesh::Box, M::Occluder,
                          {.45f, 1.55f, .45f}, {1.05f, 1.55f, .10f}, -.3f, 2});
        return result;
    }();
    return objects;
}
const GiRoomObject& GiRoomMovable(std::size_t index) {
    for (const auto& object : GiRoomObjects()) if (object.movable == int(index)) return object;
    throw std::out_of_range("Invalid GI room prop");
}
glm::mat4 GiRoomTransform(const GiRoomObject& object, const GiRoomSettings& settings) {
    auto position = object.position;
    auto yaw = object.yaw;
    if (object.movable >= 0) {
        position += settings.offsets.at(object.movable);
        yaw += settings.rotations.at(object.movable);
    }
    return glm::translate(glm::mat4(1), position) *
        glm::rotate(glm::mat4(1), yaw, glm::vec3(0, 1, 0)) *
        FixedRotation(object.tilt, object.rotationAxis) * glm::scale(glm::mat4(1), object.scale);
}
glm::vec3 ClampGiRoomCamera(glm::vec3 position, float margin) {
    if (!Finite(position) || !std::isfinite(margin) || margin <= 0 || margin >= 2.5f)
        throw std::invalid_argument("GI room camera requires a finite position and interior margin");
    return glm::clamp(position, GiRoomMinimum + glm::vec3(margin), GiRoomMaximum - glm::vec3(margin));
}
Render::PipelineCamera GiRoomCameraPreset(unsigned preset, float aspect) {
    if (!std::isfinite(aspect) || aspect <= 0)
        throw std::invalid_argument("GI room camera requires a positive aspect ratio");
    const std::array<glm::vec3, 3> positions{{{0, 2.05f, 3.25f}, {2.9f, 1.65f, 2.4f}, {-2.9f, 3.1f, 2.5f}}};
    const std::array<glm::vec3, 3> targets{{{0, 2.3f, -1.7f}, {-1.5f, 1.6f, -1.1f}, {0, 3.3f, -2}}};
    preset %= positions.size();
    Render::PipelineCamera camera;
    camera.position = ClampGiRoomCamera(positions[preset]);
    camera.nearPlane = .05f;
    camera.farPlane = 20;
    camera.view = glm::lookAt(camera.position, targets[preset], glm::vec3(0, 1, 0));
    camera.projection = glm::perspective(glm::radians(65.f), aspect, camera.nearPlane, camera.farPlane);
    return camera;
}
bool MoveGiRoomObject(GiRoomSettings& settings, std::size_t index, glm::vec3 delta, float deltaYaw) {
    if (index >= GiRoomMovableCount || !Finite(delta) || !std::isfinite(deltaYaw)) return false;
    const auto& object = GiRoomMovable(index);
    if (!Finite(settings.offsets[index]) || !std::isfinite(settings.rotations[index])) return false;
    const auto rotation = std::remainder(settings.rotations[index] + deltaYaw, glm::radians(360.f));
    if (!std::isfinite(rotation)) return false;
    const auto extent = PropExtent(object, object.yaw + rotation);
    const auto center = glm::clamp(object.position + settings.offsets[index] + delta,
        GiRoomMinimum + extent, GiRoomMaximum - extent);
    if (!Finite(center)) return false;
    settings.rotations[index] = rotation;
    settings.offsets[index] = center - object.position;
    return true;
}
int PickGiRoomObject(glm::vec3 origin, glm::vec3 direction, const GiRoomSettings& settings) {
    if (!Finite(origin) || !Finite(direction) || glm::dot(direction, direction) < 1e-12f) return -1;
    float nearest = std::numeric_limits<float>::max();
    int selected = -1;
    for (const auto& object : GiRoomObjects()) {
        const auto inverse = glm::inverse(GiRoomTransform(object, settings));
        const auto p = glm::vec3(inverse * glm::vec4(origin, 1));
        const auto d = glm::vec3(inverse * glm::vec4(direction, 0));
        float distance = nearest;
        if (object.mesh == ShowcaseMesh::Plane) {
            if (std::abs(d.y) < 1e-8f) continue;
            distance = -p.y / d.y;
            const auto hit = p + d * distance;
            if (std::abs(hit.x) > 1 || std::abs(hit.z) > 1) continue;
        } else {
            float nearDistance = 0, farDistance = nearest;
            for (int axis = 0; axis < 3; ++axis) {
                if (std::abs(d[axis]) < 1e-8f) {
                    if (std::abs(p[axis]) > 1) farDistance = -1;
                    continue;
                }
                auto a = (-1 - p[axis]) / d[axis], b = (1 - p[axis]) / d[axis];
                if (a > b) std::swap(a, b);
                nearDistance = std::max(nearDistance, a); farDistance = std::min(farDistance, b);
            }
            if (nearDistance > farDistance) continue;
            distance = nearDistance;
        }
        if (distance < 0 || distance >= nearest) continue;
        nearest = distance; selected = object.movable;
    }
    return selected;
}
bool ValidateGiRoomSettings(const GiRoomSettings& settings, std::string* error) {
    if (error) error->clear();
    const auto fail = [&](const char* message) { if (error) *error = message; return false; };
    if (settings.selected < 0 || settings.selected >= int(GiRoomMovableCount))
        return fail("GI room selected prop is out of range");
    if (!Finite(settings.lightPosition) || !Finite(settings.lightRadiance) ||
        !std::isfinite(settings.lightRadius) || settings.lightRadius < .02f || settings.lightRadius > .5f)
        return fail("GI room lamp must have a finite position, radiance and .02.. .5 radius");
    for (int axis = 0; axis < 3; ++axis)
        if (settings.lightRadiance[axis] < 0 || settings.lightRadiance[axis] > 1000)
            return fail("GI room lamp radiance must be 0..1000");
    if (!Contained(settings.lightPosition, glm::vec3(settings.lightRadius)))
        return fail("GI room lamp crosses the sealed room boundary");
    for (std::size_t index = 0; index < GiRoomMovableCount; ++index) {
        if (!Finite(settings.offsets[index]) || !std::isfinite(settings.rotations[index]))
            return fail("GI room prop transform must be finite");
        const auto& object = GiRoomMovable(index);
        if (!Contained(object.position + settings.offsets[index],
                       PropExtent(object, object.yaw + settings.rotations[index])))
            return fail("GI room prop crosses the sealed room boundary");
    }
    return true;
}
void ConfigureGiRoomLighting(Render::MaterialPipelineSettings& effects, const GiRoomSettings& settings) {
    std::string error;
    if (!ValidateGiRoomSettings(settings, &error)) throw std::invalid_argument(error);
    effects.shadows.enabled = false;
    effects.shadows.sunIntensity = 0;
    effects.reflection.enabled = false;
    effects.sky.enabled = false;
    effects.sky.background = false;
    effects.areaLights.count = 0;
    effects.indirect.enabled = false;
    effects.probes.enabled = false;
    effects.realtimeGi.enabled = false;
    effects.lighting.ambientAndExposure = {0, 0, 0, 1};
    for (std::size_t index = 0; index < effects.lighting.lightColors.size(); ++index) {
        effects.lighting.lightPositions[index] = {0, 0, 0, 1};
        effects.lighting.lightColors[index] = {0, 0, 0, 0};
    }
    effects.lighting.lightPositions[0] = glm::vec4(settings.lightPosition, 1);
    effects.lighting.lightColors[0] = glm::vec4(settings.pointLight ? settings.lightRadiance : glm::vec3(0), settings.lightRadius);
    effects.lumenGi.enabled = true;
    effects.lumenGi.reflections = false;
    effects.lumenGi.screenTraces = false;
    effects.lumenGi.origin = {-3.6f, .4f, -3.6f};
    effects.lumenGi.spacing = {2.4f, 1.4f, 2.4f};
    effects.lumenGi.counts = {4, 4, 4};
    effects.lumenGi.maxDistance = 15;
    effects.lumenGi.bounceFeedback = .9f;
}
} // namespace MaterialLab
