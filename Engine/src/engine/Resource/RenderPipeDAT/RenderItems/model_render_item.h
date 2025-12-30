#pragma once

#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/Resource/Material/material.h"
#include "engine/ECS/Component/Model/static_model.h"
#include "engine/Resource/RenderPipeDAT/UniformBindings.h"


namespace Render{

    struct ModelRenderItem{
        Model* model;
        std::vector<Material*> materialList;
        glm::mat4* modelMatrix; 
        StaticModelComponentDataUBOLayout* ubodata;
    };
}
