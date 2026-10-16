#pragma once
#include "MaterialLab/Public/showcase_scene.h"
#include "Render/Public/Pipeline/realtime_gi.h"

namespace MaterialLab {
enum class ProbePlacementMaterial : unsigned { Floor, Wall, Block, Sphere, Thin, Count };
struct ProbePlacementSceneSettings { float occluderOffset = 0; };
struct ProbePlacementObject {
    const char* name;
    ShowcaseMesh mesh;
    ProbePlacementMaterial material;
    glm::vec3 position, scale;
    float yaw = 0, tilt = 0;
    glm::vec3 tiltAxis{1, 0, 0};
    bool movable = false;
};
const std::vector<ProbePlacementObject>& ProbePlacementObjects();
const std::vector<ProbePlacementObject>& ProbeSdfObjects();
glm::mat4 ProbePlacementTransform(const ProbePlacementObject&, const ProbePlacementSceneSettings&);
Render::PipelineCamera ProbePlacementCameraPreset(unsigned preset, float aspect);
// Exact same visible meshes, used only for independent geometry verification.
std::vector<Render::ProbeTriangle> ProbePlacementTriangles(const ProbePlacementSceneSettings& = {});
std::vector<Render::ProbeTriangle> ProbeSdfTriangles(const ProbePlacementSceneSettings& = {});
}
