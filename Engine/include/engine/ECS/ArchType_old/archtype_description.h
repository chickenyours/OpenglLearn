#pragma once

#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace ECS::Core{
    class ArchType;
    class ArchTypeManager;
    class Scene;
}

#include "engine/ECS/ArchType/archtype_instance.h"
#include "engine/ECS/ArchType/archtype_manager.h"
#include "engine/ToolAndAlgorithm/DateType/chunk_array.h"
#include "engine/ECS/Component/component_loader_registry.h"
#include "engine/DebugTool/ConsoleHelp/color_log.h"
#include "engine/ToolAndAlgorithm/object.h"

namespace ECS::Core{

    template <typename ComponentT>
    struct EntityComponentHandle{
        ObjectWeakPtr<ArchType> ownerArchType;
        void* chunkAddr = nullptr;
        EntityID owner = 0;
        size_t cacheIndex = 0;
        uint32_t generation = 0;

        ComponentT* Get(){
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
            return &chunkPtr->at_chunks(chunkPtr->chunkHeads_, chunkPtr->sizePerChuck_, cacheIndex);
        }
    };

    class ArchTypeDescription{
        friend class ArchType;
        friend class ArchTypeManager;
        friend class Scene;

    private:
        using AddFunction = void(*)(void*);
        using DeleteFunction = void(*)(void*, size_t);
        using SwapFunction = void(*)(void*, size_t, size_t);
        using SwapBetweenFunction = void(*)(void*, size_t, void*, size_t);

        ArchTypeDescription(ArchTypeManager* manager)
            : responseManager_(manager) {}

        void OnManagerDestroying(){
            responseManager_ = nullptr;
        }

        template <typename ComponentT>
        static void Append(void* chunkAddr){
            auto* chunkPtr = reinterpret_cast<FixedChunkArray<ComponentT>*>(chunkAddr);
            chunkPtr->push_back();
        }

        template <typename ComponentT>
        static void Delete(void* chunkAddr, size_t index){
            auto* chunkPtr = reinterpret_cast<FixedChunkArray<ComponentT>*>(chunkAddr);
            chunkPtr->remove(index);
        }

        template <typename ComponentT>
        static void Swap(void* chunkAddr, size_t indexA, size_t indexB){
            auto* chunkPtr = reinterpret_cast<FixedChunkArray<ComponentT>*>(chunkAddr);
            chunkPtr->swap(indexA, indexB);
        }

        template <typename ComponentT>
        static void SwapBetween(void* chunkAddrA, size_t indexA, void* chunkAddrB, size_t indexB){
            auto* a = reinterpret_cast<FixedChunkArray<ComponentT>*>(chunkAddrA);
            auto* b = reinterpret_cast<FixedChunkArray<ComponentT>*>(chunkAddrB);
            a->swap_with_other(*b, indexA, indexB);
        }

        template <typename ComponentT>
        static ComponentT* GetActiveComponentRaw(ArchType* archtype, size_t index){
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

            return &chunkPtr->at_chunks(chunkPtr->chunkHeads_, chunkPtr->sizePerChuck_, index);
        }

        template <typename ComponentT>
        EntityComponentHandle<ComponentT> GetActiveComponent(ObjectWeakPtr<ArchType> archtype, EntityID entity){
            EntityComponentHandle<ComponentT> handle;
            ArchType* arch = archtype.Get();
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

    public:
        template <typename ComponentT>
        void AddComponentArray(){
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

            // 先更新 description 元数据，再广播给现存 archtype
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
        bool TryGetComponentArray(size_t& out) const {
            auto comp = componentArrayDescription_.find(std::type_index(typeid(ComponentT)));
            if(comp == componentArrayDescription_.end()){
                return false;
            }
            out = comp->second;
            return true;
        }

    private:
        std::unordered_map<std::type_index, size_t> componentArrayDescription_;
        std::vector<std::type_index> index2ComponentArrayType;
        size_t componentKinds_ = 0;

        std::vector<AddFunction> addFunctions_;
        std::vector<DeleteFunction> deleteFunctions_;
        std::vector<SwapFunction> swapFunctions_;
        std::vector<SwapBetweenFunction> swapBetweenFunctions_;

        ArchTypeManager* responseManager_ = nullptr;
    };
}