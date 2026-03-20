#pragma once

#include "engine/ECS/ArchType/archtype_description.h"
#include "engine/ECS/ArchType/archtype_preload_instance.h"

namespace ECS::Core{

template <typename ComponentT>
inline FixedChunkArray<ComponentT>* ArchTypePreloadInstance::TryCastComponentArray() {
    if(!Check() || description_ == nullptr){
        return nullptr;
    }

    size_t idx = 0;
    if(!description_->TryGetComponentArray<ComponentT>(idx)){
        LOG_ERROR("ArchTypePreloadInstance::TryCastComponentArray",
                  "component type not found");
        return nullptr;
    }

    if(idx >= addr2ComponentDenseArray_.size()){
        LOG_ERROR("ArchTypePreloadInstance::TryCastComponentArray",
                  "index oversize " + std::to_string(idx));
        return nullptr;
    }

    return reinterpret_cast<FixedChunkArray<ComponentT>*>(
        addr2ComponentDenseArray_[idx]
    );
}

} // namespace ECS::Core