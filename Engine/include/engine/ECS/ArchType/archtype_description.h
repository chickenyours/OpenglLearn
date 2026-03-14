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
        struct ComponentHandle{
            ObjectWeakPtr<ArchType> ownerArchType;
            void* chunkAddr;
            EntityID owner = 0;
            size_t cacheIndex = 0;
            uint32_t generation = 0;
            ComponentT* Get(){
                if(ownerArchType.lock()){
                    FixedChunkArray<ComponentT>* chunkPtr = reinterpret_cast<FixedChunkArray<ComponentT>*>(chunkAddr);
                    if(generation != ownerArchType->activeGenerationPerUnit_[cacheIndex]){
                        auto it = ownerArchType->entityID2Unit_.find(owner);
                        if(it == ownerArchType->entityID2Unit_.end()){
                            LOG_WARNING("ComponentHandle","no find" + std::to_string(owner));
                            return nullptr;
                        }
                        cacheIndex = it->second;
                        generation = ownerArchType->activeGenerationPerUnit_[cacheIndex];
                    }
                    return &chunkPtr->at_chunks(chunkPtr->chunkHeads_, chunkPtr->sizePerChuck_, cacheIndex);
                }
                return nullptr;
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
        // using GetComponentFunction = 

        ArchTypeDescription(ArchTypeManager* manager)
            : responseManager_(manager) {}

        bool Check(ArchType* archType) const {
            return archType
                && archType->description_ == this
                && addFunctions_.size() == deleteFunctions_.size()
                && addFunctions_.size() == swapFunctions_.size()
                && addFunctions_.size() == swapBetweenFunctions_.size()
                && addFunctions_.size() == archType->activeAddr2ComponentDenseArray_.size()
                && addFunctions_.size() == archType->preloadAddr2ComponentDenseArray_.size();
        }

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
        static ComponentT* GetActiveComponent(ArchType* archtype ,size_t index){
#ifdef DEVELOP
            if(index >= archtype->TotalUsed()){
                LOG_ERROR("ArchTypeDescription","oversise");
                return nullptr;
            }
#endif
            size_t componentArrayIndex =  TryGetComponentArray<ComponentT>();
            FixedChunkArray<ComponentT>* chunkPtr = reinterpret_cast<FixedChunkArray<ComponentT>*>(archtype->activeAddr2ComponentDenseArray_[componentArrayIndex]);
            &chunkPtr->at_chunks(chunkPtr->chunkHeads_, chunkPtr->sizePerChuck_, index);
        }

        

        template <typename ComponentT>
        static ComponentHandle<ComponentT> GetComponent(void* chunkAddr, EntityID entity){
            
        }

        template <typename ComponentT>
        void AddComponentArray(){
            const std::type_index index(typeid(ComponentT));
            if(componentArrayDescription_.count(index)){
                return;
            }

            auto& constructorMap = ECS::Component::GetComponentChuckArrayConstructorMap();
            auto& destructorMap = ECS::Component::GetComponentChuckArrayDestructorMap();
            if(constructorMap.find(index) == constructorMap.end()){
                LOG_ERROR("ArchTypeDescription::AddComponentArray", "no registered constructor for " + std::string(index.name()));
                return;
            }
            if(destructorMap.find(index) == destructorMap.end()){
                LOG_ERROR("ArchTypeDescription::AddComponentArray", "no registered destructor for " + std::string(index.name()));
                return;
            }

            if(responseManager_ && !responseManager_->ResponseAdd(index)){
                LOG_ERROR("ArchTypeDescription::AddComponentArray", "failed to append component array to existing archtypes");
                return;
            }

            index2ComponentArrayType.push_back(index);
            componentArrayDescription_[index] = componentKinds_++;
            addFunctions_.push_back(&Append<ComponentT>);
            deleteFunctions_.push_back(&Delete<ComponentT>);
            swapFunctions_.push_back(&Swap<ComponentT>);
            swapBetweenFunctions_.push_back(&SwapBetween<ComponentT>);
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

} // namespace ECS::Core
