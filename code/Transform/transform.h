#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>


namespace ECS::Component{

    
    struct transform
    {
        //Local space information
        glm::vec3 pos = {0.0f,0.0f,0.0f};
        glm::vec3 rotate = {0.0f,0.0f,0.0f};
        glm::vec3 scale = {0.0f,0.0f,0.0f};
        glm::quat quaternion = {0.0f,0.0f,0.0f,1.0f};
        //Global space information concatenate in matrix
        glm::mat4 modelMatrix = glm::mat4(1.0);
    };
    
}
