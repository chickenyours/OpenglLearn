#pragma once

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>


#define MAX_UBO_AMOUNT 84
namespace Render {

constexpr int UBO_BINDING_ENVIRONMENT = 0;
constexpr int UBO_BINDING_CAMERA = 1;


// layout (std140, binding = 0) uniform Environment
// {
//     float values[16];
//     float iTime;
// };
struct alignas(16) EnvironmentDataUBOLayout{
    glm::vec4 values[16];
    float iTime;
};

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






}