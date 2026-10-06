#pragma once

#include <array>
#include <string>
#include <vector>
#include "MaterialLab/Public/showcase_scene.h"

namespace MaterialLab {
inline constexpr std::size_t GiRoomMovableCount = 3;
inline constexpr glm::vec3 GiRoomMinimum{-4, 0, -4};
inline constexpr glm::vec3 GiRoomMaximum{4, 5, 4};

enum class GiRoomMaterial { WhiteWall, RedWall, GreenWall, RoughFloor, Occluder, Count };
struct GiRoomObject {
    std::string name;
    ShowcaseMesh mesh = ShowcaseMesh::Box;
    GiRoomMaterial material = GiRoomMaterial::WhiteWall;
    glm::vec3 position{0}, scale{1};
    float yaw = 0;
    int movable = -1;
    bool castsShadows = true;
    glm::vec3 rotationAxis{0, 1, 0};
    float tilt = 0;
};
struct GiRoomSettings {
    std::array<glm::vec3, GiRoomMovableCount> offsets{};
    std::array<float, GiRoomMovableCount> rotations{};
    int selected = 2;
    bool pointLight = true, flashlight = false, indirectOnly = false, help = true;
    glm::vec3 lightPosition{-1.8f, 3.4f, -1};
    glm::vec3 lightRadiance{55, 43, 30};
    float lightRadius = .18f;
};

const std::vector<GiRoomObject>& GiRoomObjects();
const GiRoomObject& GiRoomMovable(std::size_t index);
glm::mat4 GiRoomTransform(const GiRoomObject&, const GiRoomSettings&);
// Six inward-facing planes form an exact shared-edge shell. Camera positions
// are clamped inside it; the margin must remain positive to prevent clipping.
glm::vec3 ClampGiRoomCamera(glm::vec3 position, float margin = .12f);
Render::PipelineCamera GiRoomCameraPreset(unsigned preset, float aspect);
// Clamp an edited prop's rotated bounds to the room. Invalid edits leave the
// state unchanged. Direction-normalized ray parameters are world distances.
bool MoveGiRoomObject(GiRoomSettings&, std::size_t index, glm::vec3 delta, float deltaYaw = 0);
int PickGiRoomObject(glm::vec3 origin, glm::vec3 direction, const GiRoomSettings&);
bool ValidateGiRoomSettings(const GiRoomSettings&, std::string* error = nullptr);
// Configure only the world lighting input. The visible direct/bounce display
// mode is selected by the caller, so indirectOnly must not extinguish the lamp.
void ConfigureGiRoomLighting(Render::MaterialPipelineSettings&, const GiRoomSettings&);
} // namespace MaterialLab
