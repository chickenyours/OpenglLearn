#pragma once

#include <array>
#include <string>
#include <vector>
#include "Render/Public/Pipeline/material_render_pipeline.h"

namespace MaterialLab {
inline constexpr std::size_t ShowcaseMovableCount = 6;
enum class ShowcaseMesh { Box, Sphere, Plane };
enum class ShowcaseMaterial { Stone, Plaster, Terracotta, Wood, Leaves, Gold, Chrome,
    Ceramic, Glass, Water, WarmLamp, CoolLamp, EmissivePanel, Tiles, CeilingLamp, Count };
struct ShowcaseObject {
    std::string name;
    ShowcaseMesh mesh = ShowcaseMesh::Box;
    ShowcaseMaterial material = ShowcaseMaterial::Stone;
    glm::vec3 position{0}, scale{1};
    float yaw = 0;
    // -1 is architecture; nonnegative indices are editable props.
    int movable = -1;
    bool castsShadows = true;
    glm::vec3 rotationAxis{0,1,0};
    float tilt = 0;
};
struct ShowcaseSettings {
    std::array<glm::vec3, ShowcaseMovableCount> offsets{};
    std::array<float, ShowcaseMovableCount> rotations{};
    int selected = 0;
    bool sunlight = true, pointLights = true, areaLights = true, emission = true;
    bool animateLights = false, help = true;
};
const std::vector<ShowcaseObject>& ShowcaseObjects();
const ShowcaseObject& ShowcaseMovable(std::size_t index);
glm::mat4 ShowcaseTransform(const ShowcaseObject&, const ShowcaseSettings&);
glm::vec3 ShowcaseForward(float yaw, float pitch);
Render::PipelineCamera ShowcaseCamera(glm::vec3 position, float yaw, float pitch, float aspect);
Render::PipelineCamera ShowcaseCameraPreset(unsigned preset, float aspect);
// Ray parameter is world distance when direction is normalized. Architecture
// occludes selection; picking cannot grab a prop through an opaque wall.
int PickShowcaseObject(glm::vec3 origin, glm::vec3 direction, const ShowcaseSettings&);
void ConfigureShowcaseLighting(Render::MaterialPipelineSettings&, const ShowcaseSettings&, float time);
} // namespace MaterialLab
