#pragma once

#include "code/ECS/data_type.h"

#include "code/ECS/Component/Collision/aabb_3D.h"

namespace ECS::DataType{
    struct ScriptCollsionEvent
    {
        EntityID entity1;
        EntityID entity2;
        ECS::Component::AABB_3D* entity1_collision;
        ECS::Component::AABB_3D* entity2_collision;
    };
    
}