#pragma once

#include "engine/ECS/data_type.h"
#include "engine/ECS/Component/component_register.h"

namespace Collision{
    struct Event
    {
        ECS::EntityID owner;
        ECS::EntityID other;
    };
    
}
