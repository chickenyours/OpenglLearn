#pragma once


#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Resource{
    class Camera{
        glm::mat4 view;
        glm::mat4 projection; 
    };
};