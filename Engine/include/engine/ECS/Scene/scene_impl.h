#pragma once

#include "engine/ECS/ArchType/archtype_instance.h"
#include "engine/ECS/ArchType/archtype_preload_instance.h"

namespace ECS::Core{
    template <typename ComponentT>
    ComponentT* Scene::TryGetComponent(EntityHandle entity) {
        if (!IsAlive(entity)) return nullptr;
        auto& info = entity2entityInfo_[entity.id_];
        auto* archetype = info.ownArchtype.Get();
        if (!archetype || !archetype->Check()) return nullptr;
        auto* description = archetype->GetDescription();
        size_t index = 0;
        if (!description || !description->TryGetComponentArray<ComponentT>(index)) return nullptr;
        return description->GetActiveComponent<ComponentT>(info.ownArchtype, entity.id_).Get();
    }

    inline EntityHandle Scene::GetEntityHandle(EntityID entity) const {
        EntityHandle handle;
        if (entity == INVALID_ENTITY || entity >= entity2entityInfo_.size()) return handle;
        const auto& info = entity2entityInfo_[entity];
        if (!info.alive || !info.ownArchtype.Get()) return handle;
        handle.id_ = entity;
        handle.generation_ = info.generation;
        return handle;
    }

    inline EntityHandle Scene::GetEntityHandle(EntityID entity, const ArchType* expectedArchetype) const {
        EntityHandle handle;
        if (!expectedArchetype || entity == INVALID_ENTITY || entity >= entity2entityInfo_.size()) return handle;
        const auto& info = entity2entityInfo_[entity];
        if (!info.alive || info.ownArchtype.Get() != expectedArchetype) return handle;
        handle.id_ = entity;
        handle.generation_ = info.generation;
        return handle;
    }

    template <typename ComponentT>
    EntityComponentHandle<ComponentT> Scene::GetActiveComponent(EntityID entity){
        EntityComponentHandle<ComponentT> handle;
        if(entity == 0 || entity >= entity2entityInfo_.size()){
            LOG_ERROR("Scene::GetActiveComponent", "invalid entity id");
            return handle;
        }

        const EntitySceneInfo& info = entity2entityInfo_[entity];
        if(!info.alive || info.ownArchtype == nullptr){
            return handle;
        }

        return info.ownArchtype->description_->GetActiveComponent<ComponentT>(info.ownArchtype, entity);
    }
}
