#pragma once

#include <utility> // std::swap
#include "engine/ECS/ArchType/archtype_instance.h"
#include "engine/ECS/ArchType/archtype_manager.h"

namespace ECS::Core{
    template <typename ComponentT>
    ComponentT* EntityComponentHandle<ComponentT>::Get(){
        ArchType* arch = ownerArchType.Get();
        if(arch == nullptr || chunkAddr == nullptr){
            return nullptr;
        }

        if(cacheIndex >= arch->activeGenerationPerUnit_.size() ||
           generation != arch->activeGenerationPerUnit_[cacheIndex]){
            auto it = arch->entityID2Unit_.find(owner);
            if(it == arch->entityID2Unit_.end()){
                LOG_WARNING("EntityComponentHandle::Get","entity not found: " + std::to_string(owner));
                return nullptr;
            }
            cacheIndex = it->second;
            generation = arch->activeGenerationPerUnit_[cacheIndex];
        }

        auto* chunkPtr = reinterpret_cast<FixedChunkArray<ComponentT>*>(chunkAddr);
        return &((*chunkPtr)[cacheIndex]);
    }

    template <typename ComponentT>
    void ArchTypeDescription::Append(void* chunkAddr){
        auto* chunkPtr = reinterpret_cast<FixedChunkArray<ComponentT>*>(chunkAddr);
        chunkPtr->emplace_back();
    }

    template <typename ComponentT>
    void ArchTypeDescription::Delete(void* chunkAddr, size_t index){
        auto* chunkPtr = reinterpret_cast<FixedChunkArray<ComponentT>*>(chunkAddr);
        chunkPtr->remove(index);
    }

    template <typename ComponentT>
    void ArchTypeDescription::Swap(void* chunkAddr, size_t indexA, size_t indexB){
        auto* chunkPtr = reinterpret_cast<FixedChunkArray<ComponentT>*>(chunkAddr);
        chunkPtr->swap(indexA, indexB);
    }

    template <typename ComponentT>
    void ArchTypeDescription::SwapBetween(void* chunkAddrA, size_t indexA, void* chunkAddrB, size_t indexB){
        auto* a = reinterpret_cast<FixedChunkArray<ComponentT>*>(chunkAddrA);
        auto* b = reinterpret_cast<FixedChunkArray<ComponentT>*>(chunkAddrB);

        if(a == nullptr || b == nullptr){
            LOG_ERROR("ArchTypeDescription::SwapBetween", "chunk ptr is null");
            return;
        }

        if(indexA >= a->size() || indexB >= b->size()){
            LOG_ERROR("ArchTypeDescription::SwapBetween", "index oversize");
            return;
        }

        using std::swap;
        swap((*a)[indexA], (*b)[indexB]);
    }

    template <typename ComponentT>
    ComponentT* ArchTypeDescription::GetActiveComponentRaw(ArchType* archtype, size_t index){
        if(archtype == nullptr || index >= archtype->ActiveCount()){
            LOG_ERROR("ArchTypeDescription::GetActiveComponentRaw","oversize");
            return nullptr;
        }

        size_t componentArrayIndex = 0;
        if(!archtype->description_->TryGetComponentArray<ComponentT>(componentArrayIndex)){
            LOG_ERROR("ArchTypeDescription::GetActiveComponentRaw","component type not found");
            return nullptr;
        }

        auto* chunkPtr = reinterpret_cast<FixedChunkArray<ComponentT>*>(
            archtype->activeAddr2ComponentDenseArray_[componentArrayIndex]
        );

        return &((*chunkPtr)[index]);
    }

    template <typename ComponentT>
    EntityComponentHandle<ComponentT> ArchTypeDescription::GetActiveComponent(ObjectWeakPtr<ArchType> archtype, EntityID entity){
        EntityComponentHandle<ComponentT> handle;
        ArchType* arch = archtype.get();
        if(arch == nullptr){
            return handle;
        }

        auto it = arch->entityID2Unit_.find(entity);
        if(it == arch->entityID2Unit_.end()){
            return handle;
        }

        size_t comKindIndex = 0;
        if(!TryGetComponentArray<ComponentT>(comKindIndex)){
            return handle;
        }

        const size_t entityIndex = it->second;
        auto* array = reinterpret_cast<FixedChunkArray<ComponentT>*>(
            arch->activeAddr2ComponentDenseArray_[comKindIndex]
        );

        handle.owner = entity;
        handle.ownerArchType = archtype;
        handle.chunkAddr = reinterpret_cast<void*>(array);
        handle.cacheIndex = entityIndex;
        handle.generation = arch->activeGenerationPerUnit_[entityIndex];
        return handle;
    }

    template <typename ComponentT>
    void ArchTypeDescription::AddComponentArray(){
        const std::type_index index(typeid(ComponentT));
        if(componentArrayDescription_.count(index)){
            return;
        }

        auto& constructorMap = ECS::Component::GetComponentChuckArrayConstructorMap();
        auto& destructorMap = ECS::Component::GetComponentChuckArrayDestructorMap();
        if(constructorMap.find(index) == constructorMap.end()){
            LOG_ERROR("ArchTypeDescription::AddComponentArray", "no registered constructor");
            return;
        }
        if(destructorMap.find(index) == destructorMap.end()){
            LOG_ERROR("ArchTypeDescription::AddComponentArray", "no registered destructor");
            return;
        }

        const size_t newIndex = componentKinds_;
        index2ComponentArrayType.push_back(index);
        componentArrayDescription_[index] = newIndex;
        addFunctions_.push_back(&Append<ComponentT>);
        deleteFunctions_.push_back(&Delete<ComponentT>);
        swapFunctions_.push_back(&Swap<ComponentT>);
        swapBetweenFunctions_.push_back(&SwapBetween<ComponentT>);
        ++componentKinds_;

        if(responseManager_ && !responseManager_->ResponseAdd(index)){
            LOG_ERROR("ArchTypeDescription::AddComponentArray", "failed to append component array");
        }
    }

    template <typename ComponentT>
    bool ArchTypeDescription::TryGetComponentArray(size_t& out) const {
        auto comp = componentArrayDescription_.find(std::type_index(typeid(ComponentT)));
        if(comp == componentArrayDescription_.end()){
            return false;
        }
        out = comp->second;
        return true;
    }
}