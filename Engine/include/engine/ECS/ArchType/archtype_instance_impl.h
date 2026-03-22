#pragma once

#include "engine/ECS/ArchType/archtype_description.h"

namespace ECS::Core{
    template <typename ComponentT>
    FixedChunkArray<ComponentT>* ArchType::TryCastComponentArray(){
        return TryCastActiveComponentArray<ComponentT>();
    }

    template <typename ComponentT>
    FixedChunkArray<ComponentT>* ArchType::TryCastActiveComponentArray(){
        if(!description_ || isDestroyed_){
            return nullptr;
        }

        auto it = description_->componentArrayDescription_.find(std::type_index(typeid(ComponentT)));
        if(it == description_->componentArrayDescription_.end()){
            LOG_ERROR("ArchType::TryCastActiveComponentArray", "no component type in this archtype");
            return nullptr;
        }

        const size_t index = it->second;
        if(index >= activeAddr2ComponentDenseArray_.size()){
            LOG_ERROR("ArchType::TryCastActiveComponentArray", "index is over size " + std::to_string(index));
            return nullptr;
        }

        return reinterpret_cast<FixedChunkArray<ComponentT>*>(activeAddr2ComponentDenseArray_[index]);
    }
}
