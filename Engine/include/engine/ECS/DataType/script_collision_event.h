#pragma once

#include "engine/ECS/data_type.h"

#include "engine/ECS/Component/Collision/aabb_3D.h"

namespace ECS::DataType{
    struct ScriptCollsionEvent
    {
        EntityID entity1;
        EntityID entity2;
        ECS::Component::AABB_3D* entity1_collision;
        ECS::Component::AABB_3D* entity2_collision;
    };
    
}