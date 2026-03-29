#pragma once

#include "engine/ECS/Scene/scene.h"
#include "engine/ECS/Schedule/chunk_schedule.h"

namespace ECS::Core{
    struct SceneGlobalContext{
        ECS::Core::Scene* scene;
        ECS::Core::ChunkSchedule* chunkSchedule;
    };
    
    thread_local extern SceneGlobalContext sceneGlobalContext;
}
