#pragma once

#include <cstdint>
#include <queue>
#include <vector>

#include "engine/ECS/data_type.h"
#include "engine/ECS/Entity/entity.h"
#include "engine/DebugTool/ConsoleHelp/color_log.h"
#include "engine/ToolAndAlgorithm/object.h"
#include "engine/ECS/ArchType/archtype_manager.h"
#include "engine/ECS/ArchType/archtype_description.h"

namespace ECS{
    namespace Core{
        class ComponentRegister;
        class ArchType;
        class ArchTypeManager;
        class ArchTypeDescription;
    }
    namespace System{
        class SceneTreeSystem;
    }
}

namespace ECS::Core{

    struct EntitySceneInfo{
        ArchType* ownArchtype = nullptr;
        uint32_t generation = 1;
        bool alive = false;
    };

    class Scene{
    private:
        std::vector<ObjectPtr<ArchTypeManager>> archtypeManagers_;
        std::vector<EntitySceneInfo> entity2entityInfo_;
        std::queue<EntityID> recycleEntityID_;
        uint32_t entityCount_ = 0;

        bool Check(ArchType* archtype) const;

    public:
        Scene();

        ArchTypeDescription* CreateArchTypeDescription();
        ObjectWeakPtr<ArchType> CreateArchType(ArchTypeDescription* description, size_t sizePerChuck);
        void DeleteArchType(ArchType* archtype);

        EntityHandle CreateEntity(ArchType* archtype);
        void DeleteEntity(EntityHandle entity);

        template <typename ComponentT>
        EntityComponentHandle<ComponentT> GetActiveComponent(EntityID entity);

        bool IsAlive(EntityHandle entity) const;
    };
}

#include "engine/ECS/Scene/scene_impl.h"
