#pragma once

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define OPENGL_MAX_UBO_AMOUNT 84

namespace Graphic::Opengl{

constexpr int UBO_BINDING_MATERIAL = 1;

// layout (std140, binding = 1) uniform CameraData
// {
//     mat4 view;
//     mat4 projection;
//     vec3 viewPos;
//     vec3 viewDir;
// };
struct alignas(16) CameraDataUBOLayout{
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
    alignas(16) glm::vec3 viewPos;
    alignas(16) glm::vec3 viewDereict;
};
constexpr int UBO_BINDING_CAMERA = 2;

constexpr int UBO_BINDING_GLOBAL_LIGHT = 2;

constexpr int UBO_BINDING_OBJECT = 3;

// layout (std140, binding = 3) uniform LightMatrices
// {
//     mat4 lightSpaceMatrices[16];
// };
constexpr int UBO_BINDING_LIGHT_MATRICES = 4;

// layout(std140, binding = 5) uniform CSMInfo {
//     float cascadePlaneDistances[16];
//     int cascadeCount;
//     vec3 lightDir;
//     float farPlane;
// };
struct alignas(16) CSMInfoUBOLayout{
    glm::vec4 F_CascadePlaneDistances[16];
    alignas(16) int cascadeCount;
    alignas(16) glm::vec3 lightDir;
    alignas(16) glm::vec4 F_camfarPlane;
};
constexpr int UBO_BINDING_CSM_INFO = 5;

}