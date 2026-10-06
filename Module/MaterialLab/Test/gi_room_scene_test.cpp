#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#include "MaterialLab/Public/gi_room_scene.h"
#include "MaterialLab/Public/lab_geometry.h"

namespace {
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
using Vertex = std::array<float, 3>;
Vertex Key(glm::vec3 position) { return {position.x, position.y, position.z}; }
bool Near(glm::vec3 a, glm::vec3 b) { return glm::length(a - b) < 1e-5f; }
template<class F> bool Rejects(F&& operation) {
    try { operation(); } catch (const std::invalid_argument&) { return true; }
    return false;
}
} // namespace

int main() {
    using namespace MaterialLab;
    const auto plane = MakePlane();
    GiRoomSettings settings;
    Check(ValidateGiRoomSettings(settings), "Default GI room state is invalid");
    Check(GiRoomObjects().size() == 9, "GI room requires six walls and three props");
    std::map<Vertex, unsigned> vertices;
    std::map<std::pair<Vertex, Vertex>, std::pair<unsigned, int>> edges;
    unsigned walls = 0, triangles = 0;
    for (const auto& object : GiRoomObjects()) {
        if (object.movable >= 0) continue;
        ++walls;
        Check(object.mesh == ShowcaseMesh::Plane, "Room architecture contains redundant exterior geometry");
        const auto model = GiRoomTransform(object, settings);
        for (std::size_t index = 0; index < plane.indices.size(); index += 3) {
            std::array<glm::vec3, 3> points;
            for (unsigned corner = 0; corner < 3; ++corner) {
                points[corner] = glm::vec3(model * glm::vec4(plane.vertices[plane.indices[index + corner]].position, 1));
                ++vertices[Key(points[corner])];
            }
            const auto center = (points[0] + points[1] + points[2]) / 3.f;
            const auto normal = glm::cross(points[1] - points[0], points[2] - points[0]);
            Check(glm::dot(normal, glm::vec3(0, 2.5f, 0) - center) > 1,
                  "A room wall faces outward or has degenerate geometry");
            for (unsigned corner = 0; corner < 3; ++corner) {
                auto a = Key(points[corner]), b = Key(points[(corner + 1) % 3]);
                int winding = 1;
                if (b < a) { std::swap(a, b); winding = -1; }
                auto& edge = edges[{a, b}];
                ++edge.first; edge.second += winding;
            }
            ++triangles;
        }
    }
    Check(walls == 6 && vertices.size() == 8 && triangles == 12 && edges.size() == 18,
          "Room shell lost the exact eight-corner closed-box topology");
    for (const auto& [key, edge] : edges)
        Check(edge.first == 2 && edge.second == 0, "Room has a crack, duplicated edge or inconsistent winding");
    for (const auto& [vertex, references] : vertices) {
        Check((vertex[0] == -4 || vertex[0] == 4) && (vertex[1] == 0 || vertex[1] == 5) &&
              (vertex[2] == -4 || vertex[2] == 4), "Room seams do not match the exact requested interior dimensions");
    }

    Check(Near(ClampGiRoomCamera({-100, 100, 100}), {-3.88f, 4.88f, 3.88f}),
          "Fly camera escaped the sealed room");
    Check(Near(ClampGiRoomCamera({.2f, 1.8f, -.3f}), {.2f, 1.8f, -.3f}),
          "Camera clamp modified a valid interior position");
    Check(Rejects([] { ClampGiRoomCamera({0, 1, 0}, 0); }) &&
          Rejects([] { ClampGiRoomCamera({0, 1, 0}, 2.5f); }) &&
          Rejects([] { GiRoomCameraPreset(0, 0); }), "Invalid camera state was accepted");
    for (unsigned preset = 0; preset < 3; ++preset) {
        const auto camera = GiRoomCameraPreset(preset, 16.f / 9);
        Check(Near(camera.position, ClampGiRoomCamera(camera.position)), "Preset camera starts outside the room");
        const auto inverse = glm::inverse(camera.projection * camera.view);
        const auto farPoint = inverse * glm::vec4(0, 0, 1, 1);
        const auto direction = glm::normalize(glm::vec3(farPoint) / farPoint.w - camera.position);
        const auto forward = -glm::vec3(glm::inverse(camera.view)[2]);
        Check(glm::dot(direction, forward) > .999f, "Camera and world picking projection disagree");
    }

    Check(PickGiRoomObject({.45f, 1.55f, 3.5f}, {0, 0, -1}, settings) == 2,
          "Movable blocker cannot be selected from inside the room");
    Check(PickGiRoomObject({.45f, 1.55f, 6}, {0, 0, -1}, settings) == -1,
          "Picking selected a prop through the sealed front wall");
    Check(PickGiRoomObject({0, 1, 0}, {0, 0, 0}, settings) == -1, "Zero picking direction was accepted");
    for (std::size_t index = 0; index < GiRoomMovableCount; ++index) {
        Check(GiRoomMovable(index).movable == int(index), "Movable prop mapping is incomplete");
        Check(MoveGiRoomObject(settings, index, {100, -100, -100}, 1.1f), "Finite prop edit was rejected");
        Check(ValidateGiRoomSettings(settings), "Edited prop crossed the room wall or floor");
        const auto& object = GiRoomMovable(index);
        Check(Near(glm::vec3(GiRoomTransform(object, settings)[3]), object.position + settings.offsets[index]),
              "Edited prop translation differs from its render transform");
        Check(MoveGiRoomObject(settings, index, {-200, 200, 200}, -2.8f), "Reverse prop edit failed");
        Check(ValidateGiRoomSettings(settings), "Rotated prop crossed the opposite room boundary");
    }
    const auto oldOffset = settings.offsets[0];
    const auto oldRotation = settings.rotations[0];
    Check(!MoveGiRoomObject(settings, GiRoomMovableCount, {0, 0, 0}), "Invalid prop index was accepted");
    Check(!MoveGiRoomObject(settings, 0, {std::numeric_limits<float>::quiet_NaN(), 0, 0}) &&
          settings.offsets[0] == oldOffset && settings.rotations[0] == oldRotation,
          "Invalid edit corrupted the room state");
    settings = {};
    settings.offsets[0].x = 10;
    Check(!ValidateGiRoomSettings(settings), "Unconstrained external prop transform was accepted");
    settings = {};
    settings.lightPosition.x = 4;
    Check(!ValidateGiRoomSettings(settings), "Finite lamp radius crossed the sealed wall");
    settings = {};
    settings.lightRadiance.y = -1;
    Check(!ValidateGiRoomSettings(settings), "Negative lamp energy was accepted");
    settings = {};
    settings.selected = int(GiRoomMovableCount);
    Check(!ValidateGiRoomSettings(settings), "Invalid selected prop was accepted");

    settings = {};
    Render::MaterialPipelineSettings effects;
    effects.sky.enabled = true;
    effects.indirect.enabled = true;
    effects.areaLights.count = 2;
    effects.probes.enabled = true;
    effects.realtimeGi.enabled = true;
    ConfigureGiRoomLighting(effects, settings);
    Check(effects.lumenGi.enabled && !effects.lumenGi.screenTraces && !effects.lumenGi.reflections,
          "Diagnostic room retained view-dependent lighting features");
    Check(!effects.shadows.enabled && effects.shadows.sunIntensity == 0 && !effects.sky.enabled &&
          !effects.sky.background && !effects.reflection.enabled && effects.areaLights.count == 0 &&
          !effects.indirect.enabled && !effects.probes.enabled && !effects.realtimeGi.enabled &&
          glm::vec3(effects.lighting.ambientAndExposure) == glm::vec3(0),
          "Diagnostic room retained an unwanted environment or secondary light source");
    Check(glm::vec3(effects.lighting.lightColors[0]) == settings.lightRadiance &&
          effects.lighting.lightColors[0].w == settings.lightRadius &&
          glm::vec3(effects.lighting.lightPositions[0]) == settings.lightPosition,
          "Room point lamp lost its physical input or source radius");
    for (std::size_t index = 1; index < effects.lighting.lightColors.size(); ++index)
        Check(glm::vec3(effects.lighting.lightColors[index]) == glm::vec3(0), "Room contains extra point lights");
    settings.indirectOnly = true;
    ConfigureGiRoomLighting(effects, settings);
    Check(glm::vec3(effects.lighting.lightColors[0]) == settings.lightRadiance,
          "Indirect-only display accidentally extinguished the GI input lamp");
    settings.pointLight = false;
    ConfigureGiRoomLighting(effects, settings);
    for (const auto& color : effects.lighting.lightColors)
        Check(glm::vec3(color) == glm::vec3(0), "Point lamp toggle did not extinguish the world light input");
    settings.lightRadius = std::numeric_limits<float>::infinity();
    Check(Rejects([&] { ConfigureGiRoomLighting(effects, settings); }), "Invalid world lighting input was accepted");
    std::cout << "GI room sealed topology, camera, picking, constrained edits and isolated lighting passed\n";
}
