#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace MyTool{

    
inline glm::mat4 GetViewMat4(glm::vec3 position, glm::vec3 forward, glm::vec3 upForward = glm::vec3(0.0,1.0,0.0)){
    float z[3] = {-forward.x,-forward.y,-forward.z};
    float zd = std::sqrt(z[0]*z[0] + z[1]*z[1] + z[2]*z[2]);
    z[0] /= zd;z[1] /= zd;z[2] /= zd;
    float up[3] = {upForward.x,upForward.y,upForward.z};
    float x[3] = {up[1]*z[2]-up[2]*z[1],up[2]*z[0]-up[0]*z[2],up[0]*z[1]-up[1]*z[0]};
    float xd = std::sqrt(x[0]*x[0] + x[1]*x[1] + x[2]*x[2]);
    x[0] /= xd;x[1] /= xd;x[2] /= xd;
    float y[3] = {z[1]*x[2]-z[2]*x[1],z[2]*x[0]-z[0]*x[2],z[0]*x[1]-z[1]*x[0]};
    float yd = std::sqrt(y[0]*y[0] + y[1]*y[1] + y[2]*y[2]);
    y[0] /= yd;y[1] /= yd;y[2] /= yd;
    
    
    glm::mat4 viewTransform = glm::mat4(
        x[0], y[0], z[0], 0.0f,
        x[1], y[1], z[1], 0.0f,
        x[2], y[2], z[2], 0.0f,
        -position.x * x[0] - position.y * x[1] - position.z * x[2],
        -position.x * y[0] - position.y * y[1] - position.z * y[2],
        -position.x * z[0] - position.y * z[1] - position.z * z[2],
        1.0f
    );
    return viewTransform;
}


}
