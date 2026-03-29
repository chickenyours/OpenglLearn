#pragma once

// #include "engine/ECS/Scene/scene.h"
// #include "engine/ECS/Schedule/chunk_schedule.h"

namespace ECS::Core
{
    class Scene;
    class ChunkSchedule;

    struct ECSCoreContext{
        Scene* scene;
        ChunkSchedule* chunkSchedule;

    };

    extern ECSCoreContext globalECSCoreContext;
    
} // namespace ECS::Core
