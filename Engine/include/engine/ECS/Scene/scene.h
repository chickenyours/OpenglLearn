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
        class ArchTypePreloadInstance;
    }
    namespace System{
        class SceneTreeSystem;
    }
}

namespace ECS::Core{

    struct EntitySceneInfo{
        ObjectWeakPtr<ArchType> ownArchtype;
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
        bool Check(ArchTypePreloadInstance* preload) const;

    public:
        Scene();

        ObjectWeakPtr<ArchTypeDescription> CreateArchTypeDescription();
        ObjectWeakPtr<ArchType> CreateArchType(ObjectWeakPtr<ArchTypeDescription> description, size_t sizePerChuck);
        void DeleteArchType(ObjectWeakPtr<ArchType>& archtype);

        ObjectWeakPtr<ArchTypePreloadInstance> CreateArchTypePreloadInstance(ObjectWeakPtr<ArchTypeDescription> description, size_t sizePerChuck);
        void DeleteArchTypePreloadInstance(ObjectWeakPtr<ArchTypePreloadInstance>& preload);

        EntityHandle CreateEntity(ObjectWeakPtr<ArchType>& archtype);
        std::vector<EntityHandle> CreateEntities(ObjectWeakPtr<ArchType>& archtype, size_t count);

        size_t RegisterPreloadToArchType(ObjectWeakPtr<ArchTypePreloadInstance>& preload,
                                         ObjectWeakPtr<ArchType>& archtype,
                                         std::vector<EntityHandle>& outEntities);
        size_t RegisterPreloadToArchTypeByMask(ObjectWeakPtr<ArchTypePreloadInstance>& preload,
                                               ObjectWeakPtr<ArchType>& archtype,
                                               const uint8_t* passMask,
                                               size_t maskCount,
                                               std::vector<EntityHandle>& outEntities);

        void DeleteEntity(EntityHandle entity);
        void DeleteEntities(const std::vector<EntityHandle>& entities);

        template <typename ComponentT>
        EntityComponentHandle<ComponentT> GetActiveComponent(EntityID entity);

        bool IsAlive(EntityHandle entity) const;
    };
}

#include "engine/ECS/Scene/scene_impl.h"
