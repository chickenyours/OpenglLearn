#pragma once
#include "code/ECS/Component/component.h"

#include "code/Resource/Material/material_type.h"
#include "code/Resource/Material/material.h"

namespace ECS::Component{
    
    struct MeshRender
    {
        Resource::MaterialType type;
        Resource::Material* material;
    };
    

}

