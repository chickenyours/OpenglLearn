#pragma once

#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "code/Resource/Material/material.h"
#include "code/ECS/Component/Model/static_model.h"


namespace Render{
    struct ModelRenderItem{
        Model* model;
        std::vector<Material*> materialList;
        glm::mat4* modelMatrix; 
        ECS::Component::MeshRenderer* com;
        // std::vector<glm::vec4>* testValues;
    };
}