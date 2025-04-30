#pragma once

#include <vector>
#include "code/ECS/data_type.h"

namespace ECS {
    namespace System {
        class SceneTreeSystem;
    }
    namespace Core{
        class ComponentRegister;
        template <typename ComponentT>
        class ComponentStorage;
    }
}

namespace ECS::Component{
    // 你想作死可以显式为entity创建组件
    // Internal use only. Do not manually add/remove this component. 
    struct Hierarchy : Component
    {
        EntityID parent = INVALID_ENTITY;
        std::vector<EntityID> children;
        bool inSceneTree = false;

        protected:  
            Hierarchy() = default;
            friend class ECS::System::SceneTreeSystem; // SceneTreeSystem可以访问 (SceneTreeSystem专用组件)
            friend class ECS::Core::ComponentRegister;
            friend class ECS::Core::ComponentStorage<Hierarchy>;
    };
}