#pragma once



namespace ECS::Core
{
    class Scene;

    struct ECSCoreContext{
        Scene* scene;
    };

    extern ECSCoreContext globalECSCoreContext;
    
} // namespace ECS::Core
