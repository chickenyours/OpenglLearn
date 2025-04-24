#pragma once

#include <vector>
#include "code/ECS/data_type.h"

namespace ECS::Component{
    struct Hierarchy
    {
        EntityID parent = INVALID_ENTITY;
        std::vector<EntityID> children;
    };
    
}