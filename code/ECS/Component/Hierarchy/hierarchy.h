#pragma once

#include <vector>
#include "code/ECS/data_type.h"

#include "code/ECS/Component/component.h"

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
    // Internal use only. Do not manually add/remove this component. 
    struct Hierarchy : Component<Hierarchy>
    {
        EntityID parent = INVALID_ENTITY;
        std::vector<EntityID> children;
        bool inSceneTree = false;
        Hierarchy() = default;

        // protected:  
        //     friend class ECS::System::SceneTreeSystem; // SceneTreeSystem可以访问 (SceneTreeSystem专用组件)
        //     friend class ECS::Core::ComponentRegister;
        //     friend class ECS::Core::ComponentStorage<Hierarchy>;
    };
}