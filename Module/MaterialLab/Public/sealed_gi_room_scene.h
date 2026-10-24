#pragma once
#include <array>

#include "MaterialLab/Public/probe_placement_scene.h"

namespace MaterialLab {
inline constexpr glm::vec3 SealedGiRoomMinimum{-4,0,-3.5f};
inline constexpr glm::vec3 SealedGiRoomMaximum{4,4.5f,3.5f};
inline constexpr glm::vec3 SealedGiRoomEmitterCenter{0,4.54f,0};
inline constexpr glm::vec3 SealedGiRoomEmitterHalfAxisU{.9f,0,0};
inline constexpr glm::vec3 SealedGiRoomEmitterHalfAxisV{0,0,.9f};
inline constexpr glm::vec3 SealedGiRoomEmitterRadiance{24,23.04f,21.36f};
inline constexpr unsigned SealedGiRoomLampIndex=18;
inline constexpr unsigned SealedGiRoomPropCount=6;
using SealedGiRoomPropOffsets=std::array<glm::vec3,SealedGiRoomPropCount>;

struct SealedGiRoomMaterial {
    glm::vec3 albedo{.8f};
    float roughness=.2f;
    glm::vec3 emission{0};
};
// All visible meshes, distance-field instances and cards use this object order.
const std::vector<ProbePlacementObject>& SealedGiRoomObjects();
// Shared rigid poses for raster, SDF instances, cards and interactive picking.
glm::mat4 SealedGiRoomObjectTransform(unsigned object,const ProbePlacementSceneSettings&,const SealedGiRoomPropOffsets&);
SealedGiRoomMaterial SealedGiRoomObjectMaterial(unsigned object);
Render::PipelineCamera SealedGiRoomCameraPreset(unsigned preset,float aspect);
void ConfigureSealedGiRoomLighting(Render::MaterialPipelineSettings&);
} // namespace MaterialLab
