#pragma once

#include "engine/ECS/ArchType/archtype_instance.h"
#include "engine/ECS/ArchType/archtype_preload_instance.h"

namespace ECS::Core{
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
