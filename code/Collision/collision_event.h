#pragma once

#include "code/ECS/data_type.h"
#include "code/ECS/Component/component_register.h"

namespace Collision{
    struct Event
    {
        ECS::EntityID owner;
        ECS::EntityID other;
    };
    
}