#pragma once

// #include "engine/ECS/Scene/scene.h"
// #include "engine/ECS/Schedule/chunk_schedule.h"

namespace ECS::Core
{
    class Scene;

    struct ECSCoreContext{
        Scene* scene;
    };

    extern ECSCoreContext globalECSCoreContext;
    
} // namespace ECS::Core
