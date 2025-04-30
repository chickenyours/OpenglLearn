#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "code/ECS/Component/component.h"

namespace ECS::Component{

    struct Transform : Component
    {
        //Local space information
        glm::vec3 position = {0.0f,0.0f,0.0f}; 
        // pitch yaw roll (x,y,z)
        glm::vec3 rotation = {0.0f,0.0f,0.0f};
        glm::vec3 scale = {1.0f,1.0f,1.0f};
        glm::quat quaternion = {0.0f,0.0f,0.0f,1.0f};
        //Global space information concatenate in matrix
        glm::mat4 localMatrix{1.0f};
        glm::mat4 worldMatrix{1.0f};
        void UpdateLocalMatrix() {
            localMatrix = 
                glm::translate(
                    glm::scale(
                        glm::rotate(
                            glm::rotate(
                                glm::rotate(
                                    glm::mat4(1.0f),
                                    glm::radians(rotation.x),
                                    glm::vec3(1.0f, 0.0f, 0.0f)
                                ),
                                glm::radians(rotation.y),
                                glm::vec3(0.0f, 1.0f, 0.0f)
                            ),
                            glm::radians(rotation.z),
                            glm::vec3(0.0f, 0.0f, 1.0f)
                        ),
                        scale
                    ),
                    position
                );
        }
    };
    
}
